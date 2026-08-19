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
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEUtils.h>
#include <BLEAddress.h>
#include "GamepadParser.h"

// Tiempo máximo sin recibir un report del mando antes de detener el robot.
// El SafetyManager del teléfono usa 1500 ms; el mando necesita un failsafe
// mucho más agresivo porque reporta continuamente durante la conducción.
#ifndef GAMEPAD_TIMEOUT_MS
#define GAMEPAD_TIMEOUT_MS 200
#endif

// Filtro de nombre del mando (subcadena). Vacío = aceptar cualquier
// dispositivo que anuncie el servicio HID. Ajustar al mando real.
#ifndef GAMEPAD_NAME_FILTER
#define GAMEPAD_NAME_FILTER ""
#endif

// Servicio HID estándar y su characteristic de Report
#define GAMEPAD_HID_SERVICE_UUID 0x1812
#define GAMEPAD_REPORT_CHAR_UUID 0x2A4D

// Tamaño máximo de un HID report (los de gamepads suelen ser <= 20 bytes)
#define GAMEPAD_MAX_REPORT_LEN 20

// Timeout de cada intento de conexión en milisegundos (client->connect() es
// bloqueante para loop()). En competencia conviene un valor corto: 2000 ms
// evita perder el combate esperando una reconexión que no llega; subirlo a
// 4000-5000 ms da más margen en entornos con mucha interferencia RF.
#ifndef GAMEPAD_CONNECT_TIMEOUT_MS
#define GAMEPAD_CONNECT_TIMEOUT_MS 2000
#endif

// Callback de seguridad: se invoca para detener el robot ante
// desconexión del mando, timeout de reports o antes de un intento
// de conexión (porque este es bloqueante y congela loop()).
typedef void (*GamepadStopCallback)();

// Cliente BLE (central) que busca el mando, se conecta, se suscribe al
// HID Report y actualiza un GamepadState. Usa la misma API BLEDevice que
// BleManager porque el ESP32-C3 no puede ejecutar Bluedroid y NimBLE a la
// vez: el servidor Android (Bluedroid) y este cliente deben coexistir.
class GamepadController {
public:
    GamepadController();

    // Inicializa el cliente BLE de forma autónoma: llama a BLEDevice::init()
    // (idempotente) y no requiere que el servidor de la app se haya iniciado.
    // Adecuado para la arquitectura de modos mutuamente excluyentes.
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
    void rememberFoundDevice(const BLEAddress& addr);
    void scanStopped();
    void clientDisconnected();
    void storeReport(const uint8_t* data, size_t len);

private:
    enum class Phase : uint8_t { SCANNING, CONNECTING, CONNECTED, RETRY_WAIT };

    void startScan();
    void connectToPad();
    void disconnectClient();

    BLEScan* scan;
    BLEClient* client;

    BLEAddress padAddress;      // mando objetivo encontrado
    bool haveAddress;

    Phase phase;
    volatile bool foundDevice;  // el escaneo encontró un mando válido
    volatile bool scanFinished; // el escaneo terminó (éxito o timeout)
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

    GamepadParser parser;
};

#endif
