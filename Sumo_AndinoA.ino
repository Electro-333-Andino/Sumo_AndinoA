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

// --- CONFIGURACIÓN DE PINES ---
#define PIN_LED  8
#define PIN_ENA  0
#define PIN_IN1  1
#define PIN_IN2  3
#define PIN_ENB  4
#define PIN_IN3  5
#define PIN_IN4  6
#define PIN_STBY 7

// Watchdog del teléfono: si deja de enviar comandos durante este tiempo, parada preventiva
#define COMMAND_TIMEOUT_MS 1500

// --- VELOCIDADES POR DEFECTO (0 a 1023) ---
uint16_t VELOCIDAD_IZQUIERDA = 1023;
uint16_t VELOCIDAD_DERECHA   = 1023;
uint16_t VELOCIDAD_GIRO      = 1023;

// DEBUG DEL MANDO: 1 = imprime RAW/NORMALIZED/OUTPUT por Serial; 0 = silencio
#ifndef GAMEPAD_DEBUG
#define GAMEPAD_DEBUG 1
#endif

// --- INSTANCIACIÓN ---
StatusLed ledEstado(PIN_LED);
MotorController robot(PIN_ENA, PIN_IN1, PIN_IN2, PIN_ENB, PIN_IN3, PIN_IN4, PIN_STBY);
BleManager bluetooth("SumoAndinoA");
SafetyManager safety(robot, bluetooth, COMMAND_TIMEOUT_MS);
GamepadController gamepad;
GamepadMixer mixer;

// Velocidad máxima que usa el mando. La configura el teléfono con sus comandos
// de movimiento (p. ej. "F,700,700" -> 700). Por defecto: 1023.
uint16_t configuredSpeed = 1023;

// --- CALLBACKS DE SEGURIDAD ---
// Se ejecuta desde el callback de desconexión BLE (posiblemente otra tarea):
// solo toca pines, nada de heap ni Strings.
void safetyStop() {
  // Si el mando está conectado, él es la fuente activa: no frenar por el teléfono
  if (!gamepad.isConnected()) {
    robot.emergencyStop(); // corte duro: STBY a LOW, el TB6612 queda en alta impedancia
  }
}

// El mando invoca este callback ante desconexión, timeout sin reports o antes
// de un intento de conexión (bloquea loop() unos segundos).
void gamepadStop() {
  robot.emergencyStop();
}

// --- SUB-RUTINAS DE CONTROL ---

// Comando del teléfono: SIEMPRE se interpreta (para actualizar configuredSpeed),
// pero solo mueve el robot si el mando no está activo (prioridad del mando).
void processPhoneCommand(char* paquete, bool padActive) {
  char comando = 'S';
  int vIzqInput = VELOCIDAD_IZQUIERDA;
  int vDerInput = VELOCIDAD_DERECHA;

  int camposLeidos = sscanf(paquete, "%c,%d,%d", &comando, &vIzqInput, &vDerInput);

  uint16_t speedL = constrain(vIzqInput, 0, 1023);
  uint16_t speedR = constrain(vDerInput, 0, 1023);

  // La velocidad configurada por Android se comparte con el mando; se actualiza
  // siempre (incluso con el mando activo) para que el nuevo límite aplique ya.
  if ((comando == 'F' || comando == 'B' || comando == 'L' || comando == 'R') &&
      camposLeidos == 3) {
    uint16_t maxSpeed = max(speedL, speedR);
    if (maxSpeed > 0) configuredSpeed = maxSpeed;
  }

  // Mando conectado = fuente de control activa: no mover motores con el teléfono
  if (padActive) {
    return;
  }

  switch (comando) {
    case 'F':
      robot.moveForward(speedL, speedR);
      break;

    case 'B':
      robot.moveBackward(speedL, speedR);
      break;

    case 'L':
      if (camposLeidos == 3) {
        robot.turnLeft(speedL, speedR);
      } else {
        robot.turnLeft(VELOCIDAD_GIRO, VELOCIDAD_GIRO);
      }
      break;

    case 'R':
      if (camposLeidos == 3) {
        robot.turnRight(speedL, speedR);
      } else {
        robot.turnRight(VELOCIDAD_GIRO, VELOCIDAD_GIRO);
      }
      break;

    case 'S':
    default:
      robot.stop();
      break;
  }

  Serial.print("Cmd: "); Serial.print(comando);
  Serial.print(" | Motor Izq: "); Serial.print(speedL);
  Serial.print(" | Motor Der: "); Serial.println(speedR);
}

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
    Serial.printf("[PAD] RAW LY=%d RX=%d | NLY=%d NRX=%d | L=%d R=%d\n",
                  st.rawLeftY, st.rawRightX, st.leftY, st.rightX,
                  out.left, out.right);
  }
#endif
}

// --- SETUP / LOOP ---

void setup() {
  Serial.begin(115200);

  ledEstado.begin();
  robot.begin();

  bluetooth.setSafetyStopCallback(safetyStop);
  bluetooth.begin();

  gamepad.setStopCallback(gamepadStop);
  gamepad.begin(); // DESPUÉS de bluetooth.begin(): BLEDevice ya está iniciado

  Serial.println("Sistema listo - Modo: Control dual");
}

void loop() {
  // 1. Estado fresco del mando (escaneo/conexión/reports)
  gamepad.update();

  // 2. Fuente de control activa y LED
  bool isPadActive = gamepad.isConnected();
  ledEstado.setConnected(bluetooth.isConnected() || isPadActive);
  ledEstado.update();

  safety.setGamepadActive(isPadActive);
  safety.check();

  // 3. Teléfono: siempre se lee; mueve solo si el mando no está activo
  char paquete[BLE_CMD_BUFFER_SIZE];
  if (bluetooth.getCommand(paquete, sizeof(paquete))) {
    processPhoneCommand(paquete, isPadActive);
  }

  // 4. Mando: prioridad sobre el teléfono
  if (isPadActive) {
    processGamepadControl();
  }
}
