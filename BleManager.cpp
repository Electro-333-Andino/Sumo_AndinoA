#include "BleManager.h"

// Variables globales estáticas para los callbacks
static BleManager* instance = nullptr;

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define RX_UUID                "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

class ServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      if(instance) instance->setConnectionState(true);
    };
    void onDisconnect(BLEServer* pServer) {
      if(instance) instance->setConnectionState(false);
      pServer->startAdvertising(); // Reiniciar publicidad
    }
};

class RxCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String rxValue = pCharacteristic->getValue();
      if (rxValue.length() > 0) {
        if(instance) instance->setReceivedCommand(rxValue[0]);
      }
    }
};

BleManager::BleManager(const char* name) : deviceName(name), lastCommand('S'), connected(false), commandReady(false) {
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
                                           BLECharacteristic::PROPERTY_NOTIFY |
                                           BLECharacteristic::PROPERTY_READ
                                         );
  pRxCharacteristic->setCallbacks(new RxCallbacks());

  pService->start();
  pServer->getAdvertising()->addServiceUUID(SERVICE_UUID);
  pServer->getAdvertising()->start();
}

void BleManager::setConnectionState(bool state) {
  connected = state;
}

void BleManager::setReceivedCommand(char cmd) {
  lastCommand = cmd;
  commandReady = true;
}

bool BleManager::isConnected() { return connected; }

bool BleManager::hasNewCommand() { return commandReady; }

char BleManager::getCommand() {
  commandReady = false;
  return lastCommand;
}
