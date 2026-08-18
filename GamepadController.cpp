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
#include <map>

static GamepadController* instance = nullptr;

// Duración de cada ciclo de escaneo (async, no bloquea loop())
static constexpr uint8_t GAMEPAD_SCAN_TIME_S = 2;

// Pausa entre intentos fallidos de escaneo/conexión
static constexpr uint32_t GAMEPAD_RETRY_DELAY_MS = 3000;

// ---------------------------------------------------------------------------
// Callbacks BLE.
//
// IMPORTANTE: se instancian UNA SOLA VEZ como objetos estáticos (y los de
// report como función libre) y se reutilizan en cada reconexión. No usar
// `new` aquí: cada reconexión del mando en una jornada de competencia
// acumularía heap hasta agotarlo.
// ---------------------------------------------------------------------------

// Se ejecutan en la tarea del stack BLE; solo marcan flags (thread-safe).
class ScanCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        if (instance == nullptr) return;

        // Filtro 1: debe anunciar el servicio HID estándar
        if (!advertisedDevice.isAdvertisingService(BLEUUID((uint16_t)GAMEPAD_HID_SERVICE_UUID))) {
            return;
        }

        // Filtro 2 (opcional): subcadena del nombre del mando
        String name;
        if (advertisedDevice.haveName()) {
            name = advertisedDevice.getName();
        }
        if (GAMEPAD_NAME_FILTER[0] != '\0') {
            if (name.length() == 0) return;                       // sin nombre: no es el objetivo
            if (name.indexOf(GAMEPAD_NAME_FILTER) < 0) return;    // nombre no coincide
        }

        advertisedDevice.getScan()->stop(); // detiene el escaneo al encontrarlo
        instance->rememberFoundDevice(advertisedDevice.getAddress());
    }
};

static ScanCallbacks scanCallbacks;

// Callback de fin de escaneo (API del core 3.x: función, no método virtual)
static void onScanComplete(BLEScanResults results) {
    if (instance != nullptr) instance->scanStopped();
}

class ClientCallbacks : public BLEClientCallbacks {
    void onConnect(BLEClient* pClient) override {
        // nada: el éxito se comprueba en connectToPad()
    }
    void onDisconnect(BLEClient* pClient) override {
        if (instance != nullptr) instance->clientDisconnected();
    }
};

static ClientCallbacks clientCallbacks;

// Callback de notifications del HID Report (notify_callback del core 3.x)
static void onReportNotify(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    if (instance != nullptr) instance->storeReport(pData, length);
}

// --- Implementación ---

GamepadController::GamepadController()
    : scan(nullptr), client(nullptr), haveAddress(false),
      phase(Phase::SCANNING), foundDevice(false), scanFinished(false),
      linkLost(false), connected(false), lastReportMillis(0),
      reportReady(false), reportLen(0), stopCb(nullptr), phaseEnteredAt(0) {
    memset(&state, 0, sizeof(state));
    instance = this;
}

void GamepadController::begin() {
    // BLEDevice::init() ya lo ejecutó BleManager::begin(). El ESP32-C3 no
    // puede tener Bluedroid y NimBLE activos a la vez, así que el cliente
    // del mando usa la misma API BLEDevice que el servidor del teléfono.
    Serial.println("[GAMEPAD] Scanning...");
    phase = Phase::SCANNING;
    phaseEnteredAt = millis();
    startScan();
}

void GamepadController::startScan() {
    if (scan == nullptr) {
        scan = BLEDevice::getScan();
        scan->setAdvertisedDeviceCallbacks(&scanCallbacks);
        scan->setActiveScan(true); // escaneo activo: obtenemos el nombre
    }
    // Async: no bloquea loop(); al terminar se invoca onScanComplete.
    scan->start(GAMEPAD_SCAN_TIME_S, onScanComplete);
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
            }
            phaseEnteredAt = millis();
        }
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

        if (parser.parseReport(buf, len, state)) {
            state.connected = true; // estado nuevo listo para getState()
        }
    }

    // --- Máquina de estados ---
    switch (phase) {
        case Phase::SCANNING:
            // escaneo async en curso; onScanComplete decidirá el siguiente paso
            break;

        case Phase::CONNECTING:
            connectToPad(); // bloqueante; siempre transiciona de fase
            break;

        case Phase::CONNECTED:
            // Failsafe: si el mando deja de reportar, detener el robot
            if (millis() - lastReportMillis > GAMEPAD_TIMEOUT_MS) {
                if (stopCb != nullptr) stopCb();
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
    // evita fugas de heap y es la forma correcta con la API del core 3.x
    // (no existe BLEDevice::deleteClient()).
    if (client == nullptr) {
        client = BLEDevice::createClient();
        client->setClientCallbacks(&clientCallbacks);
    } else if (client->isConnected()) {
        client->disconnect();
    }

    // connect() es bloqueante; el timeout en ms limita la espera.
    if (!client->connect(padAddress, 0xFF, GAMEPAD_CONNECT_TIMEOUT_MS)) {
        Serial.println("[GAMEPAD] Connect failed");
        client->disconnect();
        phase = Phase::RETRY_WAIT;
        phaseEnteredAt = millis();
        return;
    }
    Serial.println("[GAMEPAD] Connected");

    // Descubrir el servicio HID
    BLERemoteService* hidService = client->getService(BLEUUID((uint16_t)GAMEPAD_HID_SERVICE_UUID));
    if (hidService == nullptr) {
        Serial.println("[GAMEPAD] HID service not found");
        client->disconnect();
        phase = Phase::RETRY_WAIT;
        phaseEnteredAt = millis();
        return;
    }
    Serial.println("[GAMEPAD] HID service found");

    // Suscribirse a notifications de la(s) characteristic(s) de Report.
    // subscribe() falla si la characteristic no tiene CCCD, así que no hace
    // falta filtrar por propiedades (getProperties() no existe en core 3.x).
    bool subscribed = false;
    std::map<std::string, BLERemoteCharacteristic*>* chars = hidService->getCharacteristics();
    if (chars != nullptr) {
        for (auto& pair : *chars) {
            BLERemoteCharacteristic* ch = pair.second;
            if (ch->getUUID().equals(BLEUUID((uint16_t)GAMEPAD_REPORT_CHAR_UUID))) {
                if (ch->subscribe(true, onReportNotify)) {
                    subscribed = true;
                }
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
    Serial.println("[GAMEPAD] Report subscribed");

    connected = true;
    lastReportMillis = millis();
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

unsigned long GamepadController::millisSinceLastReport() {
    return millis() - lastReportMillis;
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

void GamepadController::rememberFoundDevice(const BLEAddress& addr) {
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

    portENTER_CRITICAL(&mux);
    memcpy(reportBuffer, data, len);
    reportLen = (uint8_t)len;
    reportReady = true;
    lastReportMillis = millis();
    portEXIT_CRITICAL(&mux);
}
