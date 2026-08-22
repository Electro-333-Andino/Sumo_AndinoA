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

#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>
#include <NimBLEDevice.h>

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
    // Último comando gana (latest command wins): no hay cola de comandos.
    bool getCommand(char* buffer, size_t bufferSize);

    // Se ejecuta inmediatamente cuando el BLE se desconecta (ideal para robot.stop())
    void setSafetyStopCallback(SafetyStopCallback cb);

    // Usados internamente por los callbacks de NimBLEServer/NimBLECharacteristic
    void setConnectionState(bool state);
    void setReceivedCommand(const char* cmd, size_t len);

private:
    const char* deviceName;

    char lastCommand[BLE_CMD_BUFFER_SIZE];
    volatile bool connected;
    volatile bool commandReady;

    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    SafetyStopCallback safetyStopCb;
};

#endif
