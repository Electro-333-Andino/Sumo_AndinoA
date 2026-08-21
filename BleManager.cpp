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

#include "BleManager.h"
#include <string.h>

static BleManager* instance = nullptr;

// UUIDs PROPIOS de SparkDrive/Sumo_AndinoA (generados una sola vez, no reutilizar).
// Antes usábamos el Nordic UART Service estándar, reconocible por cualquier app
// genérica de BLE. Con UUIDs propios, un scanner random ya no identifica de
// entrada qué características son de escritura/notificación.
#define SERVICE_UUID  "D5C4A74E-B869-4744-90D8-37BB68B6ABBC"
#define RX_UUID       "5ED81982-1610-4A5D-979B-98E58EF12D31" // App -> ESP32
#define TX_UUID       "D2904D82-9FBF-4BD1-86FB-84D2C89E40A0" // ESP32 -> App

// --- Callbacks del servidor NimBLE (objetos estáticos: sin fugas de heap) ---

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        if (instance != nullptr) instance->setConnectionState(true);
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        // setConnectionState(false) dispara el safety-stop callback de inmediato,
        // sin esperar al siguiente ciclo de loop().
        if (instance != nullptr) instance->setConnectionState(false);
        // Reanunciar el servicio para que la app pueda reconectar
        NimBLEDevice::startAdvertising();
    }
};

static ServerCallbacks serverCallbacks;

class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        if (instance == nullptr) return;
        // NimBLE entrega el valor como std::string; se copia de inmediato a un
        // buffer fijo en setReceivedCommand() y no se retiene.
        std::string rxValue = pCharacteristic->getValue<std::string>();
        if (!rxValue.empty()) {
            instance->setReceivedCommand(rxValue.c_str(), rxValue.length());
        }
    }
};

static RxCallbacks rxCallbacks;

BleManager::BleManager(const char* name)
    : deviceName(name), connected(false), commandReady(false),
      lastCommandMillis(0), safetyStopCb(nullptr) {
    lastCommand[0] = 'S';
    lastCommand[1] = '\0';
    instance = this;
}

void BleManager::begin() {
    // init() es idempotente (flag interno): seguro aunque GamepadController ya
    // haya inicializado el stack NimBLE.
    NimBLEDevice::init(deviceName);

    NimBLEServer* pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(&serverCallbacks, false); // callbacks estáticos: no borrar

    NimBLEService* pService = pServer->createService(SERVICE_UUID);

    NimBLECharacteristic* pRxCharacteristic = pService->createCharacteristic(
        RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    pRxCharacteristic->setCallbacks(&rxCallbacks);

    NimBLECharacteristic* pTxCharacteristic = pService->createCharacteristic(
        TX_UUID,
        NIMBLE_PROPERTY::NOTIFY); // el descriptor CCCD (0x2902) se crea solo

    pService->start();

    // Advertising con scan response. IMPORTANTE: en NimBLE el nombre del
    // dispositivo NO se añade solo al advertising (en Bluedroid sí); hay que
    // configurarlo explícitamente con setName() para que la app lo encuentre
    // por nombre en el scan response.
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setName(deviceName);
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->enableScanResponse(true);
    NimBLEDevice::startAdvertising();
}

void BleManager::setConnectionState(bool state) {
    connected = state;
    if (state) {
        // Al conectarse el teléfono puede tardar en enviar su primer comando:
        // reiniciar el contador evita un falso disparo del watchdog
        // (millis() - 0 sería un valor enorme en millisSinceLastCommand()).
        lastCommandMillis = millis();
    } else if (safetyStopCb != nullptr) {
        safetyStopCb(); // Frena motores en el instante mismo de la desconexión
    }
}

void BleManager::setReceivedCommand(const char* cmd, size_t len) {
    if (len >= BLE_CMD_BUFFER_SIZE) {
        len = BLE_CMD_BUFFER_SIZE - 1; // trunca en vez de desbordar
    }

    portENTER_CRITICAL(&mux);
    memcpy(lastCommand, cmd, len);
    lastCommand[len] = '\0';
    commandReady = true;
    lastCommandMillis = millis();
    portEXIT_CRITICAL(&mux);
}

bool BleManager::isConnected() {
    return connected;
}

bool BleManager::hasNewCommand() {
    return commandReady;
}

bool BleManager::getCommand(char* buffer, size_t bufferSize) {
    portENTER_CRITICAL(&mux);
    bool tenemosComando = commandReady;
    if (tenemosComando) {
        strncpy(buffer, lastCommand, bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
        commandReady = false;
    }
    portEXIT_CRITICAL(&mux);
    return tenemosComando;
}

unsigned long BleManager::millisSinceLastCommand() {
    portENTER_CRITICAL(&mux);
    unsigned long delta = millis() - lastCommandMillis;
    portEXIT_CRITICAL(&mux);
    return delta;
}

void BleManager::setSafetyStopCallback(SafetyStopCallback cb) {
    safetyStopCb = cb;
}
