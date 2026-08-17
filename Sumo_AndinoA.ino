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
#include <string.h>

#define PIN_LED 8

#define PIN_ENA 0
#define PIN_IN1 1
#define PIN_IN2 3

#define PIN_ENB 4
#define PIN_IN3 5
#define PIN_IN4 6

#define PIN_STBY 7

// Aumentamos el tiempo de espera para mayor tolerancia con apps de control manual
#define COMMAND_TIMEOUT_MS 1500

// --- CALIBRACIÓN DE VELOCIDAD BASE POR DEFECTO (0 a 1023) ---
uint16_t VELOCIDAD_IZQUIERDA = 1023;
uint16_t VELOCIDAD_DERECHA   = 1023;
uint16_t VELOCIDAD_GIRO      = 1023;

// Velocidad máxima que usa el mando. La configura el teléfono con sus
// comandos de movimiento (p. ej. "F,700,700" -> 700). Hasta que Android
// envíe el primero, se usa el máximo por defecto (1023).
uint16_t configuredSpeed = 1023;

// DEBUG DEL MANDO: 1 = imprime RAW/NORMALIZED/OUTPUT por Serial para
// verificar el parser y la mezcla; 0 = silencio (ahorra CPU).
#ifndef GAMEPAD_DEBUG
#define GAMEPAD_DEBUG 1
#endif

StatusLed ledEstado(PIN_LED);
MotorController robot(PIN_ENA, PIN_IN1, PIN_IN2, PIN_ENB, PIN_IN3, PIN_IN4, PIN_STBY);
BleManager bluetooth("SumoAndinoA");
SafetyManager safety(robot, bluetooth, COMMAND_TIMEOUT_MS);
GamepadController gamepad;
GamepadMixer mixer;

// Se ejecuta desde el callback de desconexión BLE (posiblemente otra tarea),
// por eso solo toca pines directamente, nada de heap ni Strings aquí.
void safetyStop() {
  // Si el mando está conectado, él es la fuente de control activa: la
  // desconexión del teléfono no debe frenar al robot.
  if (!gamepad.isConnected()) {
    robot.emergencyStop(); // corte duro: STBY a LOW, el TB6612 queda en alta impedancia
  }
}

// El mando invoca este callback (desde loop o desde la tarea BLE) ante:
//  - desconexión del mando
//  - timeout sin reports (GAMEPAD_TIMEOUT_MS)
//  - antes de un intento de conexión (bloquea loop() unos segundos)
void gamepadStop() {
  robot.emergencyStop();
}

void setup() {
  Serial.begin(115200);

  ledEstado.begin();
  robot.begin(); // carga los trims guardados en NVS
  bluetooth.setSafetyStopCallback(safetyStop);
  bluetooth.begin();
  gamepad.setStopCallback(gamepadStop);
  gamepad.begin(); // DESPUÉS de bluetooth.begin(): BLEDevice ya está iniciado

  Serial.println("Sistema iniciado. Trims cargados:");
  Serial.print("  LF="); Serial.print(robot.getTrim('L', 'F'));
  Serial.print("  LB="); Serial.print(robot.getTrim('L', 'B'));
  Serial.print("  RF="); Serial.print(robot.getTrim('R', 'F'));
  Serial.print("  RB="); Serial.println(robot.getTrim('R', 'B'));
}

void loop() {
  ledEstado.setConnected(bluetooth.isConnected() || gamepad.isConnected());
  ledEstado.update();

  // El mando conectado tiene prioridad: desactiva el watchdog del teléfono
  safety.setGamepadActive(gamepad.isConnected());
  safety.check();

  // Escaneo/conexión/reports del mando (puede bloquear durante la conexión)
  gamepad.update();

  // --- Control con el mando (solo cuando está conectado) ---
  if (gamepad.isConnected()) {
    static unsigned long lastMotorUpdate = 0;
    static unsigned long lastPadLog = 0;
    unsigned long now = millis();

    // Re-aplica el estado del mando a 50 Hz; si no hay reports nuevos,
    // GamepadController ya habrá detenido el robot por timeout.
    if (now - lastMotorUpdate >= 20) {
      lastMotorUpdate = now;

      GamepadState st = gamepad.getState();
      MotorOutput out = mixer.calculate(st, configuredSpeed);
      robot.setMotorSpeeds(out.left, out.right);

#if GAMEPAD_DEBUG
      // ~20 líneas/s para no saturar Serial ni robar tiempo al loop
      if (now - lastPadLog >= 50) {
        lastPadLog = now;
        Serial.printf("[PAD] RAW LY=%d RX=%d | NLY=%d NRX=%d | L=%d R=%d\n",
                      st.rawLeftY, st.rawRightX, st.leftY, st.rightX,
                      out.left, out.right);
      }
#endif
    }
  }

  // --- Comandos desde Android (protocolo intacto) ---
  char paquete[BLE_CMD_BUFFER_SIZE];
  if (bluetooth.getCommand(paquete, sizeof(paquete))) {

    // --- COMANDO DE CALIBRACIÓN: "T,LF,0.91" (siempre disponible) ---
    if (strncmp(paquete, "T,", 2) == 0) {
      if (strcmp(paquete, "T,RESET") == 0) {
        robot.resetCalibration();
        Serial.println("Trims reseteados a 1.0");
      } else {
        char motor, dir;
        float valor;
        if (sscanf(paquete, "T,%c%c,%f", &motor, &dir, &valor) == 3) {
          robot.setTrim(motor, dir, valor);
          robot.saveCalibration();
          Serial.print("Trim guardado "); Serial.print(motor); Serial.print(dir);
          Serial.print(" = "); Serial.println(valor);
        }
      }
      return; // no seguir al parser de movimiento
    }

    char comando = 'S';
    int vIzqInput = VELOCIDAD_IZQUIERDA;
    int vDerInput = VELOCIDAD_DERECHA;

    int camposLeidos = sscanf(paquete, "%c,%d,%d", &comando, &vIzqInput, &vDerInput);

    uint16_t speedL = constrain(vIzqInput, 0, 1023);
    uint16_t speedR = constrain(vDerInput, 0, 1023);

    // El mando reutiliza la velocidad que Android configura en cada comando:
    // se actualiza SIEMPRE (aunque el mando esté al mando) para que el nuevo
    // límite se aplique de inmediato.
    if ((comando == 'F' || comando == 'B' || comando == 'L' || comando == 'R') &&
        camposLeidos == 3) {
      uint16_t maxSpeed = max(speedL, speedR);
      if (maxSpeed > 0) configuredSpeed = maxSpeed;
    }

    // Mando conectado = fuente de control activa: se ignoran los comandos
    // de movimiento del teléfono (la calibración y la velocidad configurada
    // sí se procesan, como se ve arriba).
    if (gamepad.isConnected()) {
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
}
