/*
 * Copyright 2026 Anderson Andino
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "GamepadController.h"
#include <string.h>

static GamepadController* instance = nullptr;

// Duración de cada ciclo de escaneo en MILISEGUNDOS (async, no bloquea loop()).
// IMPORTANTE: en NimBLE-Arduino 2.x, NimBLEScan::start(duration, ...) recibe
// la duración en ms (no en segundos como en 1.x). Con un valor de 2 el
// escaneo duraba solo 2 ms y jamás alcanzaba a recibir ninguna trama BLE.
static constexpr uint32_t GAMEPAD_SCAN_TIME_MS = 3000;

// Pausa entre intentos fallidos de escaneo/conexión
static constexpr uint32_t GAMEPAD_RETRY_DELAY_MS = 1000;

// Company ID de Microsoft en el manufacturer data (primeros 2 bytes, LE)
static constexpr uint8_t MICROSOFT_COMPANY_ID_LOW  = 0x06;
static constexpr uint8_t MICROSOFT_COMPANY_ID_HIGH = 0x00;

// ---------------------------------------------------------------------------
// Callbacks BLE.
//
// IMPORTANTE: se instancian UNA SOLA VEZ como objetos estáticos (y el de
// report como función libre) y se reutilizan en cada reconexión. No usar
// `new` aquí: cada reconexión del mando en una jornada de competencia
// acumularía heap hasta agotarlo.
// ---------------------------------------------------------------------------

// Comprueba que el manufacturer data del adv packet pertenezca a Microsoft.
static bool isMicrosoftDevice(const NimBLEAdvertisedDevice& device) {
    if (!device.haveManufacturerData()) return false;
    std::string md = device.getManufacturerData();
    if (md.length() < 2) return false;
    return (uint8_t)md[0] == MICROSOFT_COMPANY_ID_LOW &&
           (uint8_t)md[1] == MICROSOFT_COMPANY_ID_HIGH;
}

// Se ejecutan en la tarea del stack BLE; solo marcan flags (thread-safe).
class ScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
        if (instance == nullptr || advertisedDevice == nullptr) return;

        // Diagnóstico: registra (throttled) todo lo que el ESP32 ve, para
        // saber si el mando se está anunciando y con qué nombre.
        instance->noteScanResult(advertisedDevice);

        // VÍA PRINCIPAL: nombre del mando (GAMEPAD_NAME_FILTER,
        // "Xbox Wireless Controller"). El nombre puede viajar en el scan
        // response (por eso el escaneo es activo), no solo en el adv packet.
        // El servicio HID (0x1812) NO se exige aquí: se valida después de
        // conectar (getService en connectToPad()), que es donde el 1708
        // realmente lo expone.
        std::string name;
        if (advertisedDevice->haveName()) {
            name = advertisedDevice->getName();
        }
        bool nameMatches = GAMEPAD_NAME_FILTER[0] != '\0' &&
                           !name.empty() &&
                           name.find(GAMEPAD_NAME_FILTER) != std::string::npos;

        // VÍAS ALTERNATIVAS para variantes del 1708 que anuncian poco en el
        // ADV_IND (nombre y manufacturer data suelen viajar en el SCAN_RSP):
        // - fabricante Microsoft (0x0006), o
        // - dispositivo sin nombre propio: se acepta como candidato y se
        //   valida después de conectar con el servicio HID (0x1812).
#if GAMEPAD_VALIDATE_MANUFACTURER
        bool microsoftVendor = isMicrosoftDevice(*advertisedDevice);
#else
        bool microsoftVendor = false;
#endif

        if (!nameMatches) {
            if (!microsoftVendor && !name.empty()) return;
        }

        NimBLEDevice::getScan()->stop(); // detiene el escaneo al encontrarlo
        instance->rememberFoundDevice(advertisedDevice->getAddress());
    }

    void onScanEnd(const NimBLEScanResults& scanResults, int reason) override {
        if (instance != nullptr) instance->scanStopped();
    }
};

static ScanCallbacks scanCallbacks;

class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) override {
        // nada: el éxito se comprueba en connectToPad()
    }
    void onDisconnect(NimBLEClient* pClient, int reason) override {
        if (instance != nullptr) instance->clientDisconnected();
    }
};

static ClientCallbacks clientCallbacks;

// Callback de notifications del HID Report (notify_callback de NimBLE)
static void onReportNotify(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    if (instance != nullptr) instance->storeReport(pData, length);
}

// --- Implementación ---

GamepadController::GamepadController()
    : scan(nullptr), client(nullptr), haveAddress(false),
      phase(Phase::SCANNING), foundDevice(false), scanFinished(false),
      devicesSeen(0),
      linkLost(false), connected(false), lastReportMillis(0),
      reportReady(false), reportLen(0), stopCb(nullptr), phaseEnteredAt(0),
      firstValidReport(false) {
    memset(&state, 0, sizeof(state));
    instance = this;
}

void GamepadController::begin() {
    Serial.println("[GAMEPAD] Initializing NimBLE...");

    // Inicializa el stack BLE si aún no está activo. NimBLEDevice::init() es
    // idempotente (usa un flag interno), así que es seguro llamarlo aunque el
    // servidor de la app ya lo haya inicializado: cada modo es autónomo.
    NimBLEDevice::init("Andino_Sumo");

    // --- SEGURIDAD OBLIGATORIA PARA XBOX (HID over GATT) ---
    // El perfil HID exige conexión encriptada y vinculada (bonding): sin esto,
    // el mando rechaza la suscripción al Report con "autenticación insuficiente"
    // o corta la conexión. Secure Connections + Bonding, sin MITM (Just Works).
    NimBLEDevice::setSecurityAuth(true, false, true); // bonding + SC, sin MITM
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    phase = Phase::SCANNING;
    phaseEnteredAt = millis();
    startScan();
}

void GamepadController::startScan() {
    if (scan == nullptr) {
        scan = NimBLEDevice::getScan();
        scan->setScanCallbacks(&scanCallbacks);
        // Parámetros explícitos del escaneo. PASIVO obligatorio en este
        // ESP32-C3 + NimBLE: con escaneo activo (SCAN_REQ) el radio no recibe
        // ninguna trama (0 devices); pasivo sí recibe. El Xbox 1708 anuncia
        // nombre/fabricante en el SCAN_RSP, que no llega en pasivo: por eso el
        // filtro acepta candidatos sin nombre y se valida después de conectar
        // con el servicio HID (0x1812).
        scan->setActiveScan(false);       // pasivo: sin SCAN_REQ
        scan->setInterval(100);           // 100 ms entre ventanas
        scan->setWindow(100);             // ventana continua (máxima captura)
        scan->setDuplicateFilter(false);  // reporta todas las tramas
    }
    devicesSeen = 0;
    Serial.println("[GAMEPAD] Scanning...");

    // IMPORTANTE: scanCompleteCB = true. Con `false`, NimBLE NO invoca
    // onScanEnd al terminar el periodo: scanFinished nunca se activa y la
    // máquina de estados queda atascada en SCANNING para siempre (solo
    // escanea una vez al arrancar y jamás reintenta ni conecta).
    // Async: no bloquea loop(); al terminar se invoca onScanEnd.
    if (!scan->start(GAMEPAD_SCAN_TIME_MS, true)) {
        Serial.println("[GAMEPAD] ERROR: scan->start() failed - retry in 3 s");
        phase = Phase::RETRY_WAIT;
        phaseEnteredAt = millis();
    }
}

void GamepadController::update() {
    // --- Eventos llegados desde la tarea BLE ---

    if (linkLost) {
        linkLost = false;
        if (phase == Phase::CONNECTED) {
            connected = false;
            state.connected = false;
            disconnectClient();
            phase = Phase::RETRY_WAIT;
            phaseEnteredAt = millis();
            if (stopCb != nullptr) stopCb(); // parada inmediata
            Serial.println("[GAMEPAD] Disconnected");
        }
    }

    if (scanFinished) {
        scanFinished = false;
        if (phase == Phase::SCANNING) {
            bool found = foundDevice;
            foundDevice = false; // vale solo para este ciclo de escaneo
            if (found) {
                phase = Phase::CONNECTING;
            } else {
                // No se encontró el mando: reintentar tras una pausa
                phase = Phase::RETRY_WAIT;
                Serial.printf("[GAMEPAD] Scan finished (%u devices) - retry in %u s\n",
                              (unsigned)devicesSeen,
                              (unsigned)(GAMEPAD_RETRY_DELAY_MS / 1000));
            }
            phaseEnteredAt = millis();
        }
    }

    // Fallback de robustez: si el mando se encontró pero onScanEnd no llegó
    // (p. ej. porque se llamó scan->stop() dentro del propio callback), se
    // pasa igualmente a CONNECTING.
    if (foundDevice && phase == Phase::SCANNING) {
        foundDevice = false;
        phase = Phase::CONNECTING;
        phaseEnteredAt = millis();
        Serial.println("[GAMEPAD] Controller found - connecting");
    }

    // --- Procesar el último report recibido ---
    if (reportReady) {
        uint8_t buf[GAMEPAD_MAX_REPORT_LEN];
        uint8_t len;
        portENTER_CRITICAL(&mux);
        memcpy(buf, reportBuffer, reportLen);
        len = reportLen;
        reportReady = false;
        portEXIT_CRITICAL(&mux);

#if DEBUG_GAMEPAD_REPORTS
        // Volcado controlado (throttled) para verificar el mando real
        static unsigned long lastReportLog = 0;
        if (millis() - lastReportLog >= 100) {
            lastReportLog = millis();
            Serial.printf("[GAMEPAD] len=%u\n", (unsigned)len);
            Serial.print("[GAMEPAD] data:");
            for (uint8_t i = 0; i < len; i++) {
                Serial.printf(" %02X", buf[i]);
            }
            Serial.println();
        }
#endif

        if (parser.parseReport(buf, len, state)) {
            // El watchdog solo se alimenta con reportes VÁLIDOS: un reporte
            // inválido no mantiene vivo al robot.
            lastReportMillis = millis();
            state.connected = true; // estado nuevo listo para getState()

            if (!firstValidReport) {
                firstValidReport = true;
                Serial.println("[GAMEPAD] First valid report received");
            }

#if DEBUG_GAMEPAD_REPORTS
            Serial.printf("[GAMEPAD] LX=%u LY=%u RX=%u RY=%u\n",
                          state.rawLeftX, state.rawLeftY,
                          state.rawRightX, state.rawRightY);
            Serial.printf("[GAMEPAD] normalized LX=%d LY=%d RX=%d RY=%d\n",
                          state.leftX, state.leftY,
                          state.rightX, state.rightY);
#endif
        }
    }

    // --- Máquina de estados ---
    switch (phase) {
        case Phase::SCANNING:
            // escaneo async en curso; onScanEnd decidirá el siguiente paso
            break;

        case Phase::CONNECTING:
            connectToPad(); // bloqueante; siempre transiciona de fase
            break;

        case Phase::CONNECTED:
            // Failsafe crítico: sin reporte VÁLIDO en GAMEPAD_TIMEOUT_MS el
            // estado pasa a desconectado de verdad: se detienen los motores,
            // se corta el enlace y se entra en reconexión. Así el último
            // estado jamás vuelve a mover el robot.
            if (millis() - lastReportMillis > GAMEPAD_TIMEOUT_MS) {
                connected = false;
                state.connected = false;
                disconnectClient();
                if (stopCb != nullptr) stopCb();
                phase = Phase::RETRY_WAIT;
                phaseEnteredAt = millis();
                Serial.println("[GAMEPAD] Report timeout - disconnected");
            }
            break;

        case Phase::RETRY_WAIT:
            if (millis() - phaseEnteredAt >= GAMEPAD_RETRY_DELAY_MS) {
                phase = Phase::SCANNING;
                phaseEnteredAt = millis();
                startScan();
            }
            break;
    }
}

void GamepadController::connectToPad() {
    if (!haveAddress) {
        phase = Phase::RETRY_WAIT;
        phaseEnteredAt = millis();
        return;
    }

    // Seguridad: detener el robot antes de bloquear loop() con la conexión
    if (stopCb != nullptr) stopCb();

    Serial.println("[GAMEPAD] Connecting...");

    // El cliente se crea una sola vez y se reutiliza en cada reconexión:
    // evita fugas de heap.
    if (client == nullptr) {
        client = NimBLEDevice::createClient();
        client->setClientCallbacks(&clientCallbacks, false); // estáticos: no borrar
        client->setConnectTimeout(GAMEPAD_CONNECT_TIMEOUT_MS);
    } else if (client->isConnected()) {
        client->disconnect();
    }

    // connect() es bloqueante; el timeout (setConnectTimeout) limita la espera.
    if (!client->connect(padAddress)) {
        Serial.println("[GAMEPAD] Connect failed");
        client->disconnect();
        phase = Phase::RETRY_WAIT;
        phaseEnteredAt = millis();
        return;
    }
    Serial.println("[GAMEPAD] Connected");

    // El Xbox 1708 exige encriptación/bonding para HID over GATT: sin esto
    // rechaza la suscripción al Report (0x2A4D). secureConnection() inicia el
    // pairing Just Works si no hay claves guardadas, o restaura el bond.
    if (client->secureConnection()) {
        Serial.println("[GAMEPAD] Link secured (pairing OK)");
    } else {
        Serial.println("[GAMEPAD] Warning: link not secured, subscription may fail");
    }

    // Descubrir el servicio HID (el descubrimiento GATT es automático en NimBLE)
    NimBLERemoteService* hidService = client->getService(NimBLEUUID((uint16_t)GAMEPAD_HID_SERVICE_UUID));
    if (hidService == nullptr) {
        Serial.println("[GAMEPAD] HID service not found");
        client->disconnect();
        phase = Phase::RETRY_WAIT;
        phaseEnteredAt = millis();
        return;
    }
    Serial.println("[GAMEPAD] HID service found");

    // Suscribirse a notifications de la(s) characteristic(s) de Report.
    // El servicio HID puede exponer varios 0x2A4D (input/output/feature): solo
    // el Report de entrada (input) soporta Notify. Suscribirse a un Report de
    // salida (vibración) o de feature puede provocar que el mando cierre la
    // conexión, por eso se filtra con canNotify() antes de suscribirse.
    bool subscribed = false;
    const std::vector<NimBLERemoteCharacteristic*>& chars = hidService->getCharacteristics();
    for (NimBLERemoteCharacteristic* ch : chars) {
        if (ch->getUUID() == NimBLEUUID((uint16_t)GAMEPAD_REPORT_CHAR_UUID) && ch->canNotify()) {
            Serial.println("[GAMEPAD] Report characteristic found");
            if (ch->subscribe(true, onReportNotify)) {
                subscribed = true;
                Serial.println("[GAMEPAD] Notification enabled");
            }
        }
    }

    if (!subscribed) {
        Serial.println("[GAMEPAD] Report not found");
        client->disconnect();
        phase = Phase::RETRY_WAIT;
        phaseEnteredAt = millis();
        return;
    }

    connected = true;
    lastReportMillis = millis();
    firstValidReport = false; // aún no hay un reporte válido de esta conexión
    phase = Phase::CONNECTED;
    phaseEnteredAt = millis();
}

void GamepadController::disconnectClient() {
    if (client != nullptr && client->isConnected()) {
        client->disconnect();
    }
}

bool GamepadController::isConnected() {
    return connected;
}

GamepadState GamepadController::getState() {
    GamepadState s = state; // solo se modifica desde loop(): sin mux
    s.connected = connected;
    return s;
}

void GamepadController::setStopCallback(GamepadStopCallback cb) {
    stopCb = cb;
}

// --- Entradas desde los callbacks BLE (tarea del stack) ---

// Diagnóstico del escaneo: cuenta dispositivos vistos y registra con throttle
// el nombre, fabricante y RSSI, para verificar qué anuncia el entorno.
void GamepadController::noteScanResult(const NimBLEAdvertisedDevice* device) {
    if (device == nullptr) return;
    devicesSeen++;

    static unsigned long lastLog = 0;
    unsigned long now = millis();
    if (now - lastLog < 300) return; // máximo ~3 líneas/s
    lastLog = now;

    Serial.printf("[GAMEPAD] Seen (%u): ", (unsigned)devicesSeen);
    if (device->haveName()) {
        Serial.printf("'%s' ", device->getName().c_str());
    } else {
        Serial.print("(sin nombre) ");
    }
    Serial.printf("addr=%s MS=%u rssi=%d\n",
                  device->getAddress().toString().c_str(),
                  (unsigned)isMicrosoftDevice(*device),
                  device->getRSSI());
}

void GamepadController::rememberFoundDevice(const NimBLEAddress& addr) {
    Serial.println("[GAMEPAD] Found controller");
    padAddress = addr;
    haveAddress = true;
    foundDevice = true;
}

void GamepadController::scanStopped() {
    scanFinished = true;
}

void GamepadController::clientDisconnected() {
    linkLost = true;
}

void GamepadController::storeReport(const uint8_t* data, size_t len) {
    if (len > GAMEPAD_MAX_REPORT_LEN) len = GAMEPAD_MAX_REPORT_LEN;

    // Solo se copia el reporte: el timestamp del watchdog lo actualiza
    // update() DESPUÉS de que parseReport() valide los datos.
    portENTER_CRITICAL(&mux);
    memcpy(reportBuffer, data, len);
    reportLen = (uint8_t)len;
    reportReady = true;
    portEXIT_CRITICAL(&mux);
}
