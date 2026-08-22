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
#include "GamepadFilter.h"
#include "GamepadInputState.h"

// Timeout sin reporte VÁLIDO antes de detener el robot y desconectar.
#ifndef GAMEPAD_TIMEOUT_MS
#define GAMEPAD_TIMEOUT_MS 200
#endif

// Timeout de cada intento de conexión (client->connect() es bloqueante para
// loop()). En competencia conviene un valor corto: 2000 ms.
#ifndef GAMEPAD_CONNECT_TIMEOUT_MS
#define GAMEPAD_CONNECT_TIMEOUT_MS 2000
#endif

// Tiempo máximo esperando el PRIMER reporte válido tras habilitar Notify.
// El 1708 reporta continuamente durante la conducción; si no llega nada en
// este plazo la conexión se considera no operativa y se reintenta.
#ifndef GAMEPAD_FIRST_REPORT_TIMEOUT_MS
#define GAMEPAD_FIRST_REPORT_TIMEOUT_MS 1000
#endif

// Servicio HID estándar, su characteristic de Report y el descriptor
// Report Reference (0x2908: identifica el tipo de reporte; 1 = Input).
#define GAMEPAD_HID_SERVICE_UUID       0x1812
#define GAMEPAD_REPORT_CHAR_UUID       0x2A4D
#define GAMEPAD_REPORT_REFERENCE_UUID  0x2908

// Tamaño máximo de un HID report (los de gamepads suelen ser <= 20 bytes)
#define GAMEPAD_MAX_REPORT_LEN 20

// DEBUG DE REPORTS HID: 1 = volcar por Serial la longitud, los bytes en hex
// y los sticks decodificados (con throttle ~100 ms); 0 = silencio (default).
#ifndef DEBUG_GAMEPAD_REPORTS
#define DEBUG_GAMEPAD_REPORTS 0
#endif

// DEBUG DE ESCANEO: 1 = imprime por dispositivo visto su dirección, nombre,
// RSSI, appearance y manufacturer data; 0 = solo logs de etapa (default).
#ifndef GAMEPAD_DEBUG_SCAN
#define GAMEPAD_DEBUG_SCAN 0
#endif

// Callback de seguridad: se invoca para detener el robot ante desconexión,
// timeout de reports o antes de un intento de conexión bloqueante.
typedef void (*GamepadStopCallback)();

// Cliente BLE (central) que busca el mando, se conecta, se suscribe al
// HID Input Report y actualiza un GamepadState.
//
// STACK BLE: NimBLE-Arduino (la misma que usa la implementación de referencia
// BLE-Gamepad-Client para el Xbox 1708). El servidor de la app (BleManager) y
// este cliente comparten el stack NimBLE.
//
// Estados de la conexión (el movimiento SOLO se autoriza en el Estado 4):
//   1. GATT conectado            -> isConnected()
//   2. HID service 0x1812        -> log "HID SERVICE FOUND"
//   3. Input Report + Notify     -> notifyEnabled
//   4. Primer reporte válido     -> isInputActive() == true (inputState)
class GamepadController {
public:
    GamepadController();

    // Inicializa el cliente BLE: NimBLEDevice::init() (idempotente) y
    // seguridad requerida por el Xbox (Secure Connections + Bonding, Just Works).
    void begin();

    // Máquina de estados: escaneo -> identificación -> conexión -> seguridad
    // -> HID -> suscripción -> reports. Llamar desde loop().
    void update();

    bool isConnected();    // Estado 1: enlace GATT establecido
    bool isInputActive();  // Estado 4: input válido (único que autoriza movimiento)
    GamepadState getState();

    void setStopCallback(GamepadStopCallback cb);

    // --- Usados internamente por los callbacks BLE (tarea del stack) ---
    void rememberFoundDevice(const NimBLEAdvertisedDevice* device);
    void scanStopped();
    void clientDisconnected();
    void storeReport(const uint8_t* data, size_t len);
    void noteScanResult(const NimBLEAdvertisedDevice* device);

private:
    enum class Phase : uint8_t { SCANNING, CONNECTING, CONNECTED, RETRY_WAIT };

    void startScan();
    void connectToPad();
    void disconnectClient();

    // Registra el fallo por etapa, cierra la conexión y programa el reintento.
    void failAndRetry(const char* stage, const char* reason);

    NimBLEScan* scan;
    NimBLEClient* client;

    // Copia del mando objetivo identificado en el escaneo. Se conserva el
    // NimBLEAdvertisedDevice completo (no solo la MAC) para que connect() use
    // la dirección Y su tipo (public/random) exactamente como se anunció.
    NimBLEAdvertisedDevice padDevice;
    bool haveDevice;

    Phase phase;
    volatile bool foundDevice;  // el escaneo identificó un mando válido
    volatile bool scanFinished; // el escaneo terminó (éxito o timeout)
    volatile uint32_t devicesSeen; // dispositivos vistos en el último escaneo
    volatile bool linkLost;     // el enlace BLE se perdió

    volatile bool connected;        // Estado 1: GATT conectado
    volatile bool notifyEnabled;    // Estado 3: notificaciones habilitadas
    volatile unsigned long connectedAt; // instante de conexión (deadline primer reporte)

    volatile unsigned long lastReportMillis; // SOLO se actualiza con reports válidos
    volatile bool reportReady;

    uint8_t reportBuffer[GAMEPAD_MAX_REPORT_LEN];
    volatile uint8_t reportLen;

    GamepadState state;         // solo se toca desde loop(): sin mux
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED; // protege reportBuffer/reportReady

    GamepadStopCallback stopCb;
    unsigned long phaseEnteredAt;

    GamepadInputState inputState; // Estado 4: validez del input (watchdog)
    GamepadParser parser;
};

#endif
