#include "BleManager.h"
#include <BLE2902.h>
#include <string.h>

static BleManager* instance = nullptr;

// UUIDs PROPIOS de SparkDrive/Sumo_AndinoA (generados una sola vez, no reutilizar).
// Antes usábamos el Nordic UART Service estándar, reconocible por cualquier app
// genérica de BLE. Con UUIDs propios, un scanner random ya no identifica de
// entrada qué características son de escritura/notificación.
#define SERVICE_UUID  "D5C4A74E-B869-4744-90D8-37BB68B6ABBC"
#define RX_UUID       "5ED81982-1610-4A5D-979B-98E58EF12D31" // App -> ESP32
#define TX_UUID       "D2904D82-9FBF-4BD1-86FB-84D2C89E40A0" // ESP32 -> App

class ServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        if (instance != nullptr) instance->setConnectionState(true);
    }
    void onDisconnect(BLEServer* pServer) override {
        // setConnectionState(false) dispara el safety-stop callback de inmediato,
        // sin esperar al siguiente ciclo de loop().
        if (instance != nullptr) instance->setConnectionState(false);
        BLEDevice::startAdvertising();
    }
};

class RxCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
        // Nota: en el core ESP32 Arduino 3.x, getValue() devuelve Arduino String
        // (en versiones más viejas del core devolvía std::string). Aquí solo se
        // usa de paso: se copia de inmediato a un buffer fijo en setReceivedCommand()
        // y no se retiene, así que no acumula fragmentación de heap.
        String rxValue = pCharacteristic->getValue();
        if (rxValue.length() > 0 && instance != nullptr) {
            instance->setReceivedCommand(rxValue.c_str(), rxValue.length());
        }
    }
};

BleManager::BleManager(const char* name)
    : deviceName(name), connected(false), commandReady(false),
      lastCommandMillis(0), safetyStopCb(nullptr) {
    lastCommand[0] = 'S';
    lastCommand[1] = '\0';
    instance = this;
}

void BleManager::begin() {
    BLEDevice::init(deviceName);
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);

    BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
                                             RX_UUID,
                                             BLECharacteristic::PROPERTY_WRITE |
                                             BLECharacteristic::PROPERTY_WRITE_NR
                                           );
    pRxCharacteristic->setCallbacks(new RxCallbacks());

    BLECharacteristic *pTxCharacteristic = pService->createCharacteristic(
                                             TX_UUID,
                                             BLECharacteristic::PROPERTY_NOTIFY
                                           );
    pTxCharacteristic->addDescriptor(new BLE2902());

    pService->start();

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);

    BLEDevice::startAdvertising();
}

void BleManager::setConnectionState(bool state) {
    connected = state;
    if (!state && safetyStopCb != nullptr) {
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
