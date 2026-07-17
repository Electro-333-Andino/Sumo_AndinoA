#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

class BleManager {
  private:
    const char* deviceName;
    char lastCommand;
    bool connected;
    bool commandReady;

  public:
    BleManager(const char* name);
    void begin();
    bool isConnected();
    bool hasNewCommand();
    char getCommand();

    // Callbacks públicos pero usados internamente
    void setConnectionState(bool state);
    void setReceivedCommand(char cmd);
};

#endif
