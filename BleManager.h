#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

// Tamaño máximo de un paquete de comando (sobra para "F,1023,1023" o "T,LF,0.91")
#define BLE_CMD_BUFFER_SIZE 32

// Callback que se dispara EN EL INSTANTE de la desconexión, sin esperar al loop()
typedef void (*SafetyStopCallback)();

class BleManager {
public:
    BleManager(const char* name);

    void begin();
    bool isConnected();

    // Copia el último comando a buffer (thread-safe). Retorna false si no hay nada nuevo.
    bool getCommand(char* buffer, size_t bufferSize);
    bool hasNewCommand();

    // Milisegundos transcurridos desde el último comando recibido (para watchdog)
    unsigned long millisSinceLastCommand();

    // Se ejecuta inmediatamente cuando el BLE se desconecta (ideal para robot.stop())
    void setSafetyStopCallback(SafetyStopCallback cb);

    // Usados internamente por los callbacks de BLEServer/BLECharacteristic
    void setConnectionState(bool state);
    void setReceivedCommand(const char* cmd, size_t len);

private:
    const char* deviceName;

    char lastCommand[BLE_CMD_BUFFER_SIZE];
    volatile bool connected;
    volatile bool commandReady;
    volatile unsigned long lastCommandMillis;

    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    SafetyStopCallback safetyStopCb;
};

#endif
