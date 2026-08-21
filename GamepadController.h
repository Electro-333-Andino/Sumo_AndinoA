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

#ifndef GAMEPAD_CONTROLLER_H
#define GAMEPAD_CONTROLLER_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "GamepadParser.h"

// Tiempo máximo sin recibir un report del mando antes de detener el robot.
// El SafetyManager del teléfono usa 1500 ms; el mando necesita un failsafe
// mucho más agresivo porque reporta continuamente durante la conducción.
#ifndef GAMEPAD_TIMEOUT_MS
#define GAMEPAD_TIMEOUT_MS 200
#endif

// Timeout de cada intento de conexión en milisegundos (client->connect() es
// bloqueante para loop()). En competencia conviene un valor corto: 2000 ms
// evita perder el combate esperando una reconexión que no llega; subirlo a
// 4000-5000 ms da más margen en entornos con mucha interferencia RF.
#ifndef GAMEPAD_CONNECT_TIMEOUT_MS
#define GAMEPAD_CONNECT_TIMEOUT_MS 2000
#endif

// Filtro de nombre del mando (subcadena). Vacío = aceptar cualquier
// dispositivo. Por defecto busca el nombre real del Xbox 1708.
#ifndef GAMEPAD_NAME_FILTER
#define GAMEPAD_NAME_FILTER "Xbox Wireless Controller"
#endif

// Validación opcional del manufacturer data durante el escaneo:
// 1 = habilitada: si el nombre no coincide, se acepta un dispositivo de
//     Microsoft (0x0006) sin nombre anunciado (variante del 1708 que no
//     anuncia nombre); 0 = solo validar por nombre.
#ifndef GAMEPAD_VALIDATE_MANUFACTURER
#define GAMEPAD_VALIDATE_MANUFACTURER 1
#endif

// Servicio HID estándar y su characteristic de Report
#define GAMEPAD_HID_SERVICE_UUID 0x1812
#define GAMEPAD_REPORT_CHAR_UUID 0x2A4D

// Tamaño máximo de un HID report (los de gamepads suelen ser <= 20 bytes)
#define GAMEPAD_MAX_REPORT_LEN 20

// DEBUG DE REPORTS HID: 1 = volcar por Serial la longitud, los bytes en hex
// y los sticks decodificados (con throttle ~100 ms); 0 = silencio.
#ifndef DEBUG_GAMEPAD_REPORTS
#define DEBUG_GAMEPAD_REPORTS 0
#endif

// Callback de seguridad: se invoca para detener el robot ante
// desconexión del mando, timeout de reports o antes de un intento
// de conexión (porque este es bloqueante y congela loop()).
typedef void (*GamepadStopCallback)();

// Cliente BLE (central) que busca el mando, se conecta, se suscribe al
// HID Report y actualiza un GamepadState.
//
// STACK BLE: NimBLE-Arduino (dependencia externa, la misma que usa la
// implementación de referencia BLE-Gamepad-Client para el Xbox 1708). Todo el
// proyecto usa NimBLE: el servidor de la app (BleManager) y este cliente
// comparten el mismo stack, por lo que los modos App/Xbox son compatibles.
class GamepadController {
public:
    GamepadController();

    // Inicializa el cliente BLE de forma autónoma: llama a NimBLEDevice::init()
    // (idempotente) y configura la seguridad requerida por el Xbox (Secure
    // Connections + Bonding, Just Works).
    void begin();

    // Máquina de estados: escaneo -> conexión -> suscripción -> reports.
    // Llamar desde loop(). Durante un intento de conexión puede bloquear
    // hasta GAMEPAD_CONNECT_TIMEOUT_MS; antes de eso invoca stopCb.
    void update();

    bool isConnected();
    unsigned long millisSinceLastReport();
    GamepadState getState();

    void setStopCallback(GamepadStopCallback cb);

    // --- Usados internamente por los callbacks BLE (tarea del stack) ---
    void rememberFoundDevice(const NimBLEAddress& addr);
    void scanStopped();
    void clientDisconnected();
    void storeReport(const uint8_t* data, size_t len);
    // Diagnóstico del escaneo (invocado desde onResult, tarea BLE)
    void noteScanResult(const NimBLEAdvertisedDevice* device);

private:
    enum class Phase : uint8_t { SCANNING, CONNECTING, CONNECTED, RETRY_WAIT };

    void startScan();
    void connectToPad();
    void disconnectClient();

    NimBLEScan* scan;
    NimBLEClient* client;

    NimBLEAddress padAddress;   // mando objetivo encontrado
    bool haveAddress;

    Phase phase;
    volatile bool foundDevice;  // el escaneo encontró un mando válido
    volatile bool scanFinished; // el escaneo terminó (éxito o timeout)
    volatile uint32_t devicesSeen; // dispositivos vistos en el último escaneo (diagnóstico)
    volatile bool linkLost;     // el enlace BLE se perdió

    volatile bool connected;
    volatile unsigned long lastReportMillis;
    volatile bool reportReady;

    uint8_t reportBuffer[GAMEPAD_MAX_REPORT_LEN];
    volatile uint8_t reportLen;

    GamepadState state;         // solo se toca desde loop(): sin mux
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED; // protege reportBuffer/reportReady/lastReport

    GamepadStopCallback stopCb;
    unsigned long phaseEnteredAt;
    bool firstValidReport; // true tras el primer reporte válido de la conexión

    GamepadParser parser;
};

#endif
