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
// En NimBLE-Arduino 2.x, NimBLEScan::start(duration, ...) recibe ms.
static constexpr uint32_t GAMEPAD_SCAN_TIME_MS = 3000;

// Pausa entre intentos fallidos de escaneo/conexión
static constexpr uint32_t GAMEPAD_RETRY_DELAY_MS = 1000;

// ---------------------------------------------------------------------------
// Callbacks BLE.
//
// IMPORTANTE: se instancian UNA SOLA VEZ como objetos estáticos (y el de
// report como función libre) y se reutilizan en cada reconexión. No usar
// `new` aquí: cada reconexión del mando en una jornada de competencia
// acumularía heap hasta agotarlo.
// ---------------------------------------------------------------------------

// Se ejecutan en la tarea del stack BLE; solo marcan flags (thread-safe).
class ScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
        if (instance == nullptr || advertisedDevice == nullptr) return;

        // Diagnóstico (solo detallado si GAMEPAD_DEBUG_SCAN == 1)
        instance->noteScanResult(advertisedDevice);

        // IDENTIFICACIÓN ESTRICTA del Xbox 1708 (GamepadFilter):
        //   Caso A: nombre "Xbox Wireless Controller" (completo o acortado).
        //   Caso B (sin nombre): appearance HID + manufacturer Microsoft.
        // Un dispositivo desconocido jamás se acepta ni detiene el escaneo.
        //
        // Nombre: se soporta el Complete Local Name (0x09) y el Shortened/
        // Incomplete Local Name (0x08). getName() ya prefiere el completo y
        // usa el acortado como respaldo; además se consulta explícitamente el
        // acortado con getPayloadByType() para garantizar el soporte de ambos
        // formatos y pasar la decisión por resolveName() (prioridad al completo).
        std::string fullName = advertisedDevice->getName();
        std::string shortName =
            advertisedDevice->getPayloadByType(BLE_HS_ADV_TYPE_INCOMP_NAME);
        std::string name = GamepadFilter::resolveName(fullName, shortName);
        bool hasName = !name.empty();

        bool hasManufacturerData = false;
        uint16_t manufacturerId = 0;
        if (advertisedDevice->haveManufacturerData()) {
            std::string md = advertisedDevice->getManufacturerData();
            if (md.length() >= 2) {
                manufacturerId = (uint16_t)((uint8_t)md[0] | ((uint8_t)md[1] << 8));
                hasManufacturerData = true;
            }
        }

        bool hasAppearance = advertisedDevice->haveAppearance();
        uint16_t appearance = hasAppearance ? advertisedDevice->getAppearance() : 0;

        GamepadFilter::MatchResult match = GamepadFilter::evaluate(
            hasName, name,
            hasAppearance, appearance,
            hasManufacturerData, manufacturerId);

        if (match == GamepadFilter::MatchResult::NO_MATCH) {
            return; // no es el mando: se sigue escaneando
        }

        // Mando objetivo identificado: se guarda su dirección y se detiene el
        // escaneo. La conexión no se intenta con ningún otro dispositivo.
        NimBLEDevice::getScan()->stop();
        instance->rememberFoundDevice(advertisedDevice);
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

// Callback de notifications del HID Input Report (notify_callback de NimBLE)
static void onReportNotify(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    if (instance != nullptr) instance->storeReport(pData, length);
}

// --- Implementación ---

GamepadController::GamepadController()
    : scan(nullptr), client(nullptr), haveDevice(false),
      phase(Phase::SCANNING), foundDevice(false), scanFinished(false),
      devicesSeen(0), linkLost(false),
      connected(false), notifyEnabled(false), connectedAt(0),
      lastReportMillis(0), reportReady(false), reportLen(0),
      stopCb(nullptr), phaseEnteredAt(0) {
    memset(&state, 0, sizeof(state));
    instance = this;
}

void GamepadController::begin() {
    Serial.println("[GAMEPAD] Initializing NimBLE...");

    // Inicializa el stack BLE si aún no está activo (idempotente).
    NimBLEDevice::init("Andino_Sumo");

    // --- SEGURIDAD OBLIGATORIA PARA XBOX (HID over GATT) ---
    // El perfil HID exige conexión encriptada y vinculada (bonding): sin esto,
    // el mando rechaza la suscripción al Input Report o corta la conexión.
    // Secure Connections + Bonding, sin MITM (Just Works).
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    phase = Phase::SCANNING;
    phaseEnteredAt = millis();
    startScan();
}

void GamepadController::startScan() {
    if (scan == nullptr) {
        scan = NimBLEDevice::getScan();
        scan->setScanCallbacks(&scanCallbacks);
        // ESCANEO ACTIVO (SCAN_REQ): necesario para recibir el Scan Response,
        // donde el Xbox 1708 anuncia nombre y manufacturer data; en escaneo
        // pasivo esa información se pierde y la identificación estricta
        // fallaría. Si el radio real no recibiera tramas con SCAN_REQ (0
        // devices en escaneos consecutivos), volver a pasivo cambiando esta
        // línea a setActiveScan(false).
        scan->setActiveScan(true);
        scan->setInterval(100);
        scan->setWindow(100);
        scan->setDuplicateFilter(false);
    }
    devicesSeen = 0;
    Serial.println("[GAMEPAD] SCANNING");

    // scanCompleteCB = true: sin él, NimBLE no invoca onScanEnd y la máquina
    // de estados quedaría atascada en SCANNING. Async: no bloquea loop().
    if (!scan->start(GAMEPAD_SCAN_TIME_MS, true)) {
        Serial.println("[GAMEPAD] FAILED: scan start");
        phase = Phase::RETRY_WAIT;
        phaseEnteredAt = millis();
    }
}

void GamepadController::update() {
    // --- Eventos llegados desde la tarea BLE ---

    if (linkLost) {
        linkLost = false;
        if (phase == Phase::CONNECTED) {
            Serial.println("[GAMEPAD] FAILED: link lost");
            connected = false;
            notifyEnabled = false;
            inputState.invalidate();
            state.connected = false;
            disconnectClient();
            phase = Phase::RETRY_WAIT;
            phaseEnteredAt = millis();
            if (stopCb != nullptr) stopCb(); // parada inmediata
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
                // No se identificó el mando: reintentar tras una pausa
                phase = Phase::RETRY_WAIT;
                Serial.printf("[GAMEPAD] Scan finished (%u devices) - retry in %u s\n",
                              (unsigned)devicesSeen,
                              (unsigned)(GAMEPAD_RETRY_DELAY_MS / 1000));
            }
            phaseEnteredAt = millis();
        }
    }

    // Fallback de robustez: si el mando se identificó pero onScanEnd no llegó
    // (p. ej. porque se llamó scan->stop() dentro del propio callback), se
    // pasa igualmente a CONNECTING.
    if (foundDevice && phase == Phase::SCANNING) {
        foundDevice = false;
        phase = Phase::CONNECTING;
        phaseEnteredAt = millis();
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
            Serial.printf("[GAMEPAD] report len=%u\n", (unsigned)len);
            Serial.print("[GAMEPAD] data:");
            for (uint8_t i = 0; i < len; i++) {
                Serial.printf(" %02X", buf[i]);
            }
            Serial.println();
        }
#endif

        if (parser.parseReport(buf, len, state)) {
            // El watchdog SOLO se alimenta con reportes VÁLIDOS: un reporte
            // inválido no mantiene vivo al robot.
            lastReportMillis = millis();
            state.connected = true;

            if (!inputState.isValid()) {
                Serial.println("[GAMEPAD] FIRST VALID REPORT");
            }
            inputState.markValid(millis()); // transición al Estado 4

#if DEBUG_GAMEPAD_REPORTS
            Serial.printf("[GAMEPAD] LX=%u LY=%u RX=%u RY=%u\n",
                          state.rawLeftX, state.rawLeftY,
                          state.rawRightX, state.rawRightY);
            Serial.printf("[GAMEPAD] normalized LX=%d LY=%d RX=%d RY=%d\n",
                          state.leftX, state.leftY,
                          state.rightX, state.rightY);
#endif
        } else {
#if DEBUG_GAMEPAD_REPORTS
            // Ayuda al diagnóstico: el mando reporta pero el parser rechaza.
            // Con la longitud se ve si el 1708 envía 16 bytes o una variante.
            Serial.printf("[GAMEPAD] Report rejected: len=%u (esperado 16)\n", (unsigned)len);
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

        case Phase::CONNECTED: {
            if (inputState.isValid()) {
                // Watchdog normal: sin reporte VÁLIDO en GAMEPAD_TIMEOUT_MS el
                // estado pasa a desconectado de verdad: se detienen los
                // motores, se corta el enlace y se entra en reconexión. Así el
                // último estado jamás vuelve a mover el robot.
                if (!inputState.checkValid(millis(), GAMEPAD_TIMEOUT_MS)) {
                    Serial.println("[GAMEPAD] FAILED: report timeout");
                    connected = false;
                    notifyEnabled = false;
                    state.connected = false;
                    disconnectClient();
                    if (stopCb != nullptr) stopCb();
                    phase = Phase::RETRY_WAIT;
                    phaseEnteredAt = millis();
                }
            } else if (millis() - connectedAt > GAMEPAD_FIRST_REPORT_TIMEOUT_MS) {
                // Conectado y notificando pero sin ningún reporte válido:
                // la conexión no es operativa, se cierra y se reintenta.
                Serial.println("[GAMEPAD] FAILED: no valid report after connect");
                connected = false;
                notifyEnabled = false;
                state.connected = false;
                disconnectClient();
                if (stopCb != nullptr) stopCb();
                phase = Phase::RETRY_WAIT;
                phaseEnteredAt = millis();
            }
            break;
        }

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
    if (!haveDevice) {
        failAndRetry("connect", "no target device");
        return;
    }

    // Seguridad: detener el robot antes de bloquear loop() con la conexión
    if (stopCb != nullptr) stopCb();

    Serial.println("[GAMEPAD] CONNECTING");

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
    // Se conecta con el NimBLEAdvertisedDevice identificado (no solo con la
    // MAC): así se preserva el TIPO de dirección (public/random) del advertising,
    // imprescindible para el Xbox 1708, que puede anunciarse con dirección
    // aleatoria. El segundo argumento (true) borra la caché de atributos previa.
    if (!client->connect(padDevice, true)) {
        client->disconnect();
        failAndRetry("connect", "connection refused");
        return;
    }
    Serial.println("[GAMEPAD] CONNECTED");

    // SEGURIDAD: HID over GATT exige enlace encriptado y vinculado. Si el
    // pairing/secure connection falla, la conexión NO es válida: se cierra y
    // se reintenta. No se continúa hacia HID en un enlace no seguro.
    if (!client->secureConnection()) {
        client->disconnect();
        failAndRetry("security", "pairing/encryption failed");
        return;
    }
    Serial.println("[GAMEPAD] SECURITY OK");

    // Descubrimiento GATT EXPLÍCITO: garantiza que servicios, características
    // y descriptores queden en caché ANTES de buscar el HID (0x1812). Se hace
    // sobre el enlace cifrado, que es donde el mando expone sus atributos.
    if (!client->discoverAttributes()) {
        failAndRetry("gatt discovery", "attribute discovery failed");
        return;
    }
    Serial.println("[GAMEPAD] GATT ATTRIBUTES FOUND");

    // Descubrir el servicio HID (ya en caché por el descubrimiento explícito)
    NimBLERemoteService* hidService =
        client->getService(NimBLEUUID((uint16_t)GAMEPAD_HID_SERVICE_UUID));
    if (hidService == nullptr) {
        failAndRetry("hid service", "0x1812 not found");
        return;
    }
    Serial.println("[GAMEPAD] HID SERVICE FOUND");

    // Seleccionar el INPUT Report (0x2A4D notificable). El servicio HID puede
    // exponer varios 0x2A4D (input/output/feature): se usa el descriptor
    // Report Reference (0x2908) para elegir el de tipo Input (1). Sin
    // descriptor disponible, se toma la única notificable como reserva.
    NimBLERemoteCharacteristic* inputReport = nullptr;
    bool usedReportReference = false;
    const std::vector<NimBLERemoteCharacteristic*>& chars = hidService->getCharacteristics();
    for (NimBLERemoteCharacteristic* ch : chars) {
        if (ch->getUUID() != NimBLEUUID((uint16_t)GAMEPAD_REPORT_CHAR_UUID)) {
            continue;
        }
        if (!ch->canNotify()) {
            continue; // el Input Report es el único que soporta Notify
        }

        bool isInput = false;
        NimBLERemoteDescriptor* ref =
            ch->getDescriptor(NimBLEUUID((uint16_t)GAMEPAD_REPORT_REFERENCE_UUID));
        if (ref != nullptr) {
            std::string value = ref->readValue();
            // Report Reference: [Report ID, Report Type]; 1 = Input
            if (value.length() >= 2 && (uint8_t)value[1] == 1) {
                isInput = true;
                usedReportReference = true;
            }
        }

        if (isInput) {
            inputReport = ch;
            break;
        }
        if (inputReport == nullptr) {
            inputReport = ch; // reserva: única notificable sin Report Reference
        }
    }

    if (inputReport == nullptr) {
        failAndRetry("input report", "no notifiable 0x2A4D");
        return;
    }
    Serial.println(usedReportReference
        ? "[GAMEPAD] INPUT REPORT FOUND (Report Reference)"
        : "[GAMEPAD] INPUT REPORT FOUND (fallback: única notificable)");

    if (!inputReport->subscribe(true, onReportNotify)) {
        failAndRetry("notify", "subscription rejected");
        return;
    }
    Serial.println("[GAMEPAD] NOTIFY ENABLED");

    // Estado 3 alcanzado. El Estado 4 (input válido) exige el primer reporte
    // decodificado correctamente; mientras tanto NO se autoriza movimiento.
    connected = true;
    notifyEnabled = true;
    inputState.invalidate();
    connectedAt = millis();
    phase = Phase::CONNECTED;
    phaseEnteredAt = millis();
}

void GamepadController::disconnectClient() {
    if (client != nullptr && client->isConnected()) {
        client->disconnect();
    }
}

void GamepadController::failAndRetry(const char* stage, const char* reason) {
    Serial.printf("[GAMEPAD] FAILED: %s (%s)\n", stage, reason);
    disconnectClient();
    connected = false;
    notifyEnabled = false;
    inputState.invalidate();
    state.connected = false;
    if (stopCb != nullptr) stopCb();
    phase = Phase::RETRY_WAIT;
    phaseEnteredAt = millis();
}

bool GamepadController::isConnected() {
    return connected; // Estado 1: GATT conectado
}

bool GamepadController::isInputActive() {
    return inputState.isValid(); // Estado 4: único que autoriza movimiento
}

GamepadState GamepadController::getState() {
    GamepadState s = state; // solo se modifica desde loop(): sin mux
    s.connected = isInputActive();
    return s;
}

void GamepadController::setStopCallback(GamepadStopCallback cb) {
    stopCb = cb;
}

// --- Entradas desde los callbacks BLE (tarea del stack) ---

// Diagnóstico del escaneo: cuenta dispositivos vistos. El detalle (dirección,
// nombre, RSSI, appearance) solo se imprime con GAMEPAD_DEBUG_SCAN == 1; en
// producción la dirección BLE no debe aparecer en los logs.
void GamepadController::noteScanResult(const NimBLEAdvertisedDevice* device) {
    if (device == nullptr) return;
    devicesSeen++;

#if GAMEPAD_DEBUG_SCAN
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
    Serial.printf("addr=%s appearance=0x%04X rssi=%d\n",
                  device->getAddress().toString().c_str(),
                  device->haveAppearance() ? device->getAppearance() : 0,
                  device->getRSSI());
#endif
}

void GamepadController::rememberFoundDevice(const NimBLEAdvertisedDevice* device) {
    // Copia del dispositivo anunciado: conserva la MAC y el TIPO de dirección
    // del advertising (public/random). Guardar solo la MAC haría que connect()
    // asumiera dirección pública y fallara con un mando de dirección aleatoria.
    padDevice = *device;
    haveDevice = true;
    foundDevice = true;

    Serial.print("[GAMEPAD] CANDIDATE FOUND");
#if GAMEPAD_DEBUG_SCAN
    Serial.printf(" addr=%s", padDevice.getAddress().toString().c_str());
#endif
    if (device->haveName()) {
        Serial.printf(" name='%s'", device->getName().c_str());
    }
    Serial.printf(" rssi=%d\n", device->getRSSI());
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
