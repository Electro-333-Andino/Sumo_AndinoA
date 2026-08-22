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

#include "StatusLed.h"
#include "MotorController.h"
#include "BleManager.h"
#include "SafetyManager.h"
#include "GamepadController.h"
#include "GamepadMixer.h"
#include "CommandParser.h"
#include <Preferences.h>
#include <Esp.h>

#include <cstdio>

// --- MODOS DE OPERACIÓN (persistidos en NVS) ---
#define MODE_APP  0 // teléfono Android (servidor BLE)
#define MODE_XBOX 1 // mando Xbox (cliente BLE HID)

// Namespace y clave usados por Preferences para persistir el modo
#define NVS_NAMESPACE "Andino_Sumo"
#define NVS_KEY_MODE  "opMode"

// Fail-safe de hardware: botón interno BOOT del ESP32-C3 (activo en LOW).
// Es el ÚNICO mecanismo para cambiar de modo.
#define PIN_BOOT         9
#define BOOT_HOLD_MS     3000 // mantener 3 s para invertir el modo

// --- CONFIGURACIÓN DE PINES ---
#define PIN_LED  8

#define PIN_ENA  5
#define PIN_IN1  6
#define PIN_IN2  20

#define PIN_ENB  0
#define PIN_IN3  1
#define PIN_IN4  4

#define PIN_STBY 7

// Watchdog del teléfono: si no llega un comando VÁLIDO en este tiempo, parada
// preventiva. La app envía cada ~50 ms; 250 ms es seguro y no dispara en falso.
#define COMMAND_TIMEOUT_MS 250

// --- VELOCIDAD POR DEFECTO (0 a 1023) ---
// Se aplica a los giros "L"/"R" sin velocidades explícitas de la app.
uint16_t DEFAULT_TURN_SPEED = 1023;

// --- DEBUG (todo desactivado por defecto; activar solo para diagnóstico) ---
// GAMEPAD_DEBUG: estado RAW/normalizado/salida del mando (modo Xbox)
// BLE_DEBUG:     comandos recibidos del teléfono (modo App)
// SAFETY_DEBUG:  transiciones de la máquina de seguridad (solo cambios de estado)
#ifndef GAMEPAD_DEBUG
#define GAMEPAD_DEBUG 0
#endif
#ifndef BLE_DEBUG
#define BLE_DEBUG 0
#endif
#ifndef SAFETY_DEBUG
#define SAFETY_DEBUG 0
#endif

// --- INSTANCIACIÓN ---
StatusLed statusLed(PIN_LED);
MotorController robot(PIN_ENA, PIN_IN1, PIN_IN2, PIN_ENB, PIN_IN3, PIN_IN4, PIN_STBY);
BleManager bluetooth("Robotini16");
SafetyManager safety(robot, bluetooth, COMMAND_TIMEOUT_MS);
GamepadController gamepad;
GamepadMixer mixer;

// Velocidad máxima que usa el mando. La configura el teléfono con sus comandos
// de movimiento (p. ej. "F,700,700" -> 700). Por defecto: 1023.
uint16_t configuredSpeed = 1023;

// Modo activo, persistido en NVS (namespace "Andino_Sumo", clave "opMode")
uint8_t opMode = MODE_APP;

// --- CAMBIO DE MODO ---

// Persiste el modo nuevo en NVS y reinicia para arrancar en él.
void setModeAndRestart(uint8_t newMode) {
  robot.emergencyStop(); // nunca cambiar de modo con los motores activos

  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putUChar(NVS_KEY_MODE, newMode);
  prefs.end();

  Serial.print("[MODO] Cambiando a modo ");
  Serial.println(newMode);
  ESP.restart();
}

// --- CALLBACKS DE SEGURIDAD ---

// Se ejecuta desde el callback de desconexión BLE (posiblemente otra tarea):
// solo toca pines, nada de heap ni Strings.
void safetyStop() {
#if SAFETY_DEBUG
  Serial.println("[SAFETY] BLE DISCONNECTED -> STOP");
#endif
  robot.emergencyStop(); // corte duro: STBY a LOW, el TB6612 queda en alta impedancia
}

// El mando invoca este callback ante desconexión, timeout de reports o antes
// de un intento de conexión (que bloquea loop() unos segundos).
void gamepadStop() {
#if SAFETY_DEBUG
  Serial.println("[SAFETY] GAMEPAD TIMEOUT -> STOP");
#endif
  robot.emergencyStop();
}

// --- RUTINAS DE CONTROL (Modo App) ---

// Comando del teléfono: valida ESTRICTAMENTE el protocolo SparkPilot y ejecuta
// el movimiento. Un comando inválido NO alimenta el watchdog y dispara parada
// de seguridad (el robot no se conserva vivo con paquetes corruptos).
void processPhoneCommand(char* packet) {
  ParsedCommand cmd = CommandParser::parse(packet, strlen(packet), DEFAULT_TURN_SPEED);

  if (!cmd.valid) {
    safety.onCommandRejected(); // emergencyStop + estado EMERGENCY_STOP
#if BLE_DEBUG
    Serial.print("[BLE] Invalid command: '");
    Serial.print(packet);
    Serial.println("'");
#endif
    return;
  }

  // Único punto que alimenta el watchdog del teléfono: comando VALIDADO.
  safety.onCommandAccepted(cmd.command != 'S');

  // La velocidad configurada por Android se guarda para el modo Xbox
  if (cmd.command != 'S') {
    uint16_t maxSpeed = max(cmd.speedLeft, cmd.speedRight);
    if (maxSpeed > 0) configuredSpeed = maxSpeed;
  }

  switch (cmd.command) {
    case 'F': robot.moveForward(cmd.speedLeft, cmd.speedRight); break;
    case 'B': robot.moveBackward(cmd.speedLeft, cmd.speedRight); break;
    case 'L': robot.turnLeft(cmd.speedLeft, cmd.speedRight); break;
    case 'R': robot.turnRight(cmd.speedLeft, cmd.speedRight); break;
    case 'S':
    default:  robot.stop(); break; // parada normal (no es condición de fallo)
  }

#if BLE_DEBUG
  Serial.print("[BLE] Cmd: "); Serial.print(cmd.command);
  Serial.print(" | Izq: "); Serial.print(cmd.speedLeft);
  Serial.print(" | Der: "); Serial.println(cmd.speedRight);
#endif
}

// --- RUTINAS DE CONTROL (Modo Xbox) ---

// Control con el mando: re-aplica el estado a 50 Hz. Si no llegan reports,
// GamepadController ya detiene el robot por timeout (200 ms).
void processGamepadControl() {
  // Defensa en profundidad: sin input válido (Estado 4) jamás se escriben
  // los motores, aunque algo llame a esta rutina por error.
  if (!gamepad.isInputActive()) {
    robot.emergencyStop();
    return;
  }

  static unsigned long lastMotorUpdate = 0;
  static unsigned long lastPadLog = 0;
  unsigned long now = millis();

  if (now - lastMotorUpdate < 20) {
    return;
  }
  lastMotorUpdate = now;

  GamepadState st = gamepad.getState();
  MotorOutput out = mixer.calculate(st, configuredSpeed);
  robot.setMotorSpeeds(out.left, out.right);

#if GAMEPAD_DEBUG
  // ~20 líneas/s para no saturar Serial
  if (now - lastPadLog >= 50) {
    lastPadLog = now;
    Serial.printf("[PAD] RAW LY=%u RX=%u | NLY=%d NRX=%d | L=%d R=%d\n",
                  (unsigned)st.rawLeftY, (unsigned)st.rawRightX,
                  st.leftY, st.rightX,
                  out.left, out.right);
  }
#endif
}

// --- FAIL-SAFE DE HARDWARE (botón BOOT) ---

// Temporizador no bloqueante. Si BOOT se mantiene 3 s, invierte el modo en NVS
// y reinicia. Es el único mecanismo de cambio de modo y funciona en ambos.
void checkBootButton() {
  static unsigned long pressStart = 0;
  static bool pressed = false;

  bool isPressed = (digitalRead(PIN_BOOT) == LOW);
  unsigned long now = millis();

  if (isPressed && !pressed) {
    pressed = true;
    pressStart = now;
  } else if (!isPressed && pressed) {
    pressed = false;
  } else if (isPressed && pressed && (now - pressStart >= BOOT_HOLD_MS)) {
    pressed = false; // evita reintentos mientras siga pulsado
    uint8_t newMode = (opMode == MODE_APP) ? MODE_XBOX : MODE_APP;
    setModeAndRestart(newMode);
  }
}

// --- FEEDBACK VISUAL ---

// Al arrancar indica el modo activo:
//   1 parpadeo lento (~1 s) = Modo App; 2 parpadeos rápidos = Modo Xbox.
void blinkModeLed() {
  pinMode(PIN_LED, OUTPUT);

  if (opMode == MODE_APP) {
    digitalWrite(PIN_LED, HIGH);
    delay(1000);
    digitalWrite(PIN_LED, LOW);
  } else {
    for (int i = 0; i < 2; i++) {
      digitalWrite(PIN_LED, HIGH);
      delay(200);
      digitalWrite(PIN_LED, LOW);
      delay(200);
    }
  }
}

// --- INICIALIZACIÓN EXCLUSIVA POR MODO ---

void setupAppMode() {
  Serial.println("Modo: APP (BLE Android)");
  safety.begin(); // arranque seguro: motores detenidos, esperando comandos
  bluetooth.setSafetyStopCallback(safetyStop);
  bluetooth.begin();
}

void setupXboxMode() {
  Serial.println("Modo: XBOX (BLE HID) - mando Xbox Wireless Controller 1708");
  Serial.println("Para volver al Modo App: mantener BOOT 3 segundos.");
  gamepad.setStopCallback(gamepadStop);
  gamepad.begin(); // inicializa el stack BLE de forma autónoma (idempotente)
}

// --- BUCLES EXCLUSIVOS POR MODO ---

void loopAppMode() {
  statusLed.setConnected(bluetooth.isConnected());
  statusLed.update();

  safety.update(); // watchdog de comandos válidos (250 ms) + máquina de estados

  char packet[BLE_CMD_BUFFER_SIZE];
  if (bluetooth.getCommand(packet, sizeof(packet))) {
    processPhoneCommand(packet);
  }
}

void loopXboxMode() {
  gamepad.update(); // escaneo/conexión/reports del mando

  // Solo el Estado 4 (GATT + Notify + primer reporte válido + sin timeout)
  // autoriza el movimiento: isConnected() no basta para mover el robot.
  bool padReady = gamepad.isInputActive();
  statusLed.setConnected(padReady);
  statusLed.update();

  if (padReady) {
    processGamepadControl();
  }
}

// --- SETUP / LOOP ---

void setup() {
    Serial.begin(115200);

    // 1. Leer el modo persistido en NVS (namespace "Andino_Sumo"); por defecto Modo App
    {
        Preferences prefs;
        prefs.begin(NVS_NAMESPACE, true);
        opMode = prefs.getUChar(NVS_KEY_MODE, MODE_APP);
        prefs.end();
    }

    // 2. Feedback visual del modo de arranque
    blinkModeLed();

    // 3. Log del modo ANTES de tocar BLE (para saber siempre dónde arranca)
    Serial.print("Modo persistido en NVS: ");
    Serial.println(opMode == MODE_APP ? "APP (0)" : "XBOX (1)");

    // 4. Inicialización común a ambos modos
    statusLed.begin();
    robot.begin();
    robot.emergencyStop(); // el robot arranca SIEMPRE detenido

    // 5. Inicialización EXCLUSIVA: solo el stack BLE del modo activo
    if (opMode == MODE_APP) {
        setupAppMode();
    } else {
        setupXboxMode();
    }
}

void loop() {
    // Fail-safe global: el botón BOOT está disponible en ambos modos
    checkBootButton();

    // Bloques estrictamente aislados por opMode: el bucle nunca toca el stack
    // BLE del modo inactivo.
    if (opMode == MODE_APP) {
        loopAppMode();
    } else {
        loopXboxMode();
    }
}
