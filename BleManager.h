#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

class BleManager {
public:
    BleManager(const char* name);
    void begin();
    bool isConnected();
    bool hasNewCommand();
    String getCommand(); // Retorna el String del paquete completo recibido

    void setConnectionState(bool state);
    void setReceivedCommand(String cmd);

private:
    const char* deviceName;
    String lastCommand;
    bool connected;
    bool commandReady;
};

#endif
