#include "BleManager.h"
#include <BLE2902.h>

static BleManager* instance = nullptr;

// Identificadores Estándar NUS (Nordic UART Service)
#define SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define RX_UUID             "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // Canal de Escritura (App -> ESP32)
#define TX_UUID             "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // Canal de Notificación (ESP32 -> App)

class ServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        if (instance != nullptr) instance->setConnectionState(true);
    }
    void onDisconnect(BLEServer* pServer) override {
        if (instance != nullptr) instance->setConnectionState(false);
        BLEDevice::startAdvertising();
    }
};

class RxCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
        String rxValue = pCharacteristic->getValue();
        rxValue.trim();

        if (rxValue.length() > 0) {
            if (instance != nullptr) {
                instance->setReceivedCommand(rxValue);
            }
        }
    }
};

BleManager::BleManager(const char* name)
    : deviceName(name), lastCommand("S"), connected(false), commandReady(false) {
    instance = this;
}

void BleManager::begin() {
    BLEDevice::init(deviceName);
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);

    // CARACTERÍSTICA RX: Canal de entrada
    BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
                                             RX_UUID,
                                             BLECharacteristic::PROPERTY_WRITE |
                                             BLECharacteristic::PROPERTY_WRITE_NR
                                           );
    pRxCharacteristic->setCallbacks(new RxCallbacks());

    // CARACTERÍSTICA TX: Canal de salida
    BLECharacteristic *pTxCharacteristic = pService->createCharacteristic(
                                             TX_UUID,
                                             BLECharacteristic::PROPERTY_NOTIFY
                                           );
    pTxCharacteristic->addDescriptor(new BLE2902());

    pService->start();

    // Publicidad Core 3.0+
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);

    BLEDevice::startAdvertising();
}

void BleManager::setConnectionState(bool state) { connected = state; }
void BleManager::setReceivedCommand(String cmd) { lastCommand = cmd; commandReady = true; }
bool BleManager::isConnected() { return connected; }
bool BleManager::hasNewCommand() { return commandReady; }

// SOLUCIONADO: Ahora la función pertenece legítimamente a la clase BleManager
String BleManager::getCommand() {
    commandReady = false;
    return lastCommand;
}
