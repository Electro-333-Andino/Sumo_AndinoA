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
#include <Preferences.h>
#include <Esp.h>

#include <cstdio>
#include <nvs_flash.h>

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
#define BOOT_DEBOUNCE_MS 30   // anti-rebote mínimo

// --- CONFIGURACIÓN DE PINES ---
#define PIN_LED  8

#define PIN_ENA  5
#define PIN_IN1  6
#define PIN_IN2  20

#define PIN_ENB  0
#define PIN_IN3  1
#define PIN_IN4  4

#define PIN_STBY 7

// Watchdog del teléfono: si no llega un comando en este tiempo, parada preventiva
#define COMMAND_TIMEOUT_MS 1500

// --- VELOCIDADES POR DEFECTO (0 a 1023) ---
uint16_t DEFAULT_SPEED_LEFT  = 1023;
uint16_t DEFAULT_SPEED_RIGHT = 1023;
uint16_t DEFAULT_TURN_SPEED  = 1023;

// DEBUG DEL MANDO: 1 = imprime RAW/NORMALIZADO/SALIDA por Serial; 0 = silencio
#ifndef GAMEPAD_DEBUG
#define GAMEPAD_DEBUG 1
#endif

// --- INSTANCIACIÓN ---
StatusLed statusLed(PIN_LED);
MotorController robot(PIN_ENA, PIN_IN1, PIN_IN2, PIN_ENB, PIN_IN3, PIN_IN4, PIN_STBY);
BleManager bluetooth("Andino_Sumo");
SafetyManager safety(robot, bluetooth, COMMAND_TIMEOUT_MS);
GamepadController gamepad;
GamepadMixer mixer;

// Velocidad máxima que usa el mando. La configura el teléfono con sus comandos
// de movimiento (p. ej. "F,700,700" -> 700). Por defecto: 1023.
uint16_t configuredSpeed = 1023;

// Modo activo, persistido en NVS (namespace "sumo", clave "opMode")
uint8_t opMode = MODE_APP;

// --- CAMBIO DE MODO ---

// Persiste el modo nuevo en NVS y reinicia para arrancar en él.
void setModeAndRestart(uint8_t newMode) {
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
  robot.emergencyStop(); // corte duro: STBY a LOW, el TB6612 queda en alta impedancia
}

// El mando invoca este callback ante desconexión, timeout de reports o antes
// de un intento de conexión (que bloquea loop() unos segundos).
void gamepadStop() {
  robot.emergencyStop();
}

// --- RUTINAS DE CONTROL (Modo App) ---

// Comando del teléfono: interpreta los comandos de movimiento del protocolo
// Android (F, B, L, R, S).
void processPhoneCommand(char* packet) {
  char command = 'S';
  int vLeftInput = DEFAULT_SPEED_LEFT;
  int vRightInput = DEFAULT_SPEED_RIGHT;

  int fieldsRead = sscanf(packet, "%c,%d,%d", &command, &vLeftInput, &vRightInput);

  uint16_t speedL = constrain(vLeftInput, 0, 1023);
  uint16_t speedR = constrain(vRightInput, 0, 1023);

  // La velocidad configurada por Android se guarda para el modo Xbox
  if ((command == 'F' || command == 'B' || command == 'L' || command == 'R') &&
      fieldsRead == 3) {
    uint16_t maxSpeed = max(speedL, speedR);
    if (maxSpeed > 0) configuredSpeed = maxSpeed;
  }

  switch (command) {
    case 'F':
      robot.moveForward(speedL, speedR);
      break;

    case 'B':
      robot.moveBackward(speedL, speedR);
      break;

    case 'L':
      if (fieldsRead == 3) {
        robot.turnLeft(speedL, speedR);
      } else {
        robot.turnLeft(DEFAULT_TURN_SPEED, DEFAULT_TURN_SPEED);
      }
      break;

    case 'R':
      if (fieldsRead == 3) {
        robot.turnRight(speedL, speedR);
      } else {
        robot.turnRight(DEFAULT_TURN_SPEED, DEFAULT_TURN_SPEED);
      }
      break;

    case 'S':
    default:
      robot.stop();
      break;
  }

  Serial.print("Cmd: "); Serial.print(command);
  Serial.print(" | Izq: "); Serial.print(speedL);
  Serial.print(" | Der: "); Serial.println(speedR);
}

// --- RUTINAS DE CONTROL (Modo Xbox) ---

// Control con el mando: re-aplica el estado a 50 Hz. Si no llegan reports,
// GamepadController ya detiene el robot por timeout (200 ms).
void processGamepadControl() {
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
  bluetooth.setSafetyStopCallback(safetyStop);
  bluetooth.begin();
  Serial.println("Modo: APP (BLE Android)");
}

void setupXboxMode() {
  gamepad.setStopCallback(gamepadStop);
  gamepad.begin(); // inicializa el stack BLE de forma autónoma (idempotente)
  Serial.println("Modo: XBOX (BLE HID)");
}

// --- BUCLES EXCLUSIVOS POR MODO ---

void loopAppMode() {
  statusLed.setConnected(bluetooth.isConnected());
  statusLed.update();

  safety.check(); // watchdog del teléfono (1.5 s)

  char packet[BLE_CMD_BUFFER_SIZE];
  if (bluetooth.getCommand(packet, sizeof(packet))) {
    processPhoneCommand(packet);
  }
}

void loopXboxMode() {
  gamepad.update(); // escaneo/conexión/reports del mando

  bool isPadActive = gamepad.isConnected();
  statusLed.setConnected(isPadActive);
  statusLed.update();

  if (isPadActive) {
    processGamepadControl();
  }
}

//Borrar la NVS
void clearBleBonds() {
        Serial.println("[BLE] Limpiando memoria NVS de emparejamientos...");
        // Inicializa o borra la partición NVS completa
        esp_err_t err = nvs_flash_erase();
        if (err == ESP_OK) {
            nvs_flash_init();
            Serial.println("[BLE] Memoria NVS limpiada con éxito.");
        } else {
            Serial.printf("[BLE] Error al borrar NVS: %d\n", err);
        }
    }

// --- SETUP / LOOP ---

void setup() {
    //clearBleBonds();


    Serial.begin(115200);

    // 1. Leer el modo persistido en NVS (namespace "sumo"); por defecto Modo App
    {
        Preferences prefs;
        prefs.begin(NVS_NAMESPACE, true);
        opMode = prefs.getUChar(NVS_KEY_MODE, MODE_APP);
        prefs.end();
    }

    // 2. Feedback visual del modo de arranque
    blinkModeLed();

    // 3. Inicialización común a ambos modos
    statusLed.begin();
    robot.begin();

    // 4. Inicialización EXCLUSIVA: solo el stack BLE del modo activo
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
