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

// --- PIN CONFIGURATION ---
#define PIN_LED  8
#define PIN_ENA  0
#define PIN_IN1  1
#define PIN_IN2  3
#define PIN_ENB  4
#define PIN_IN3  5
#define PIN_IN4  6
#define PIN_STBY 7

// Phone watchdog: if no command arrives within this time, preventive stop
#define COMMAND_TIMEOUT_MS 1500

// --- DEFAULT SPEEDS (0 to 1023) ---
uint16_t DEFAULT_SPEED_LEFT   = 1023;
uint16_t DEFAULT_SPEED_RIGHT  = 1023;
uint16_t DEFAULT_TURN_SPEED   = 1023;

// GAMEPAD DEBUG: 1 = print RAW/NORMALIZED/OUTPUT over Serial; 0 = silent
#ifndef GAMEPAD_DEBUG
#define GAMEPAD_DEBUG 1
#endif

// --- INSTANTIATION ---
StatusLed statusLed(PIN_LED);
MotorController robot(PIN_ENA, PIN_IN1, PIN_IN2, PIN_ENB, PIN_IN3, PIN_IN4, PIN_STBY);
BleManager bluetooth("SumoAndinoA");
SafetyManager safety(robot, bluetooth, COMMAND_TIMEOUT_MS);
GamepadController gamepad;
GamepadMixer mixer;

// Maximum speed used by the gamepad. It is set by the phone with its movement
// commands (e.g. "F,700,700" -> 700). Default: 1023.
uint16_t configuredSpeed = 1023;

// --- SAFETY CALLBACKS ---
// Runs from the BLE disconnect callback (possibly another task):
// only touches pins, no heap or Strings here.
void safetyStop() {
  // If the gamepad is connected it is the active source: do not stop on phone
  // disconnect.
  if (!gamepad.isConnected()) {
    robot.emergencyStop(); // hard cut: STBY LOW, the TB6612 goes high impedance
  }
}

// The gamepad invokes this callback on disconnect, report timeout or before a
// connection attempt (which blocks loop() for a few seconds).
void gamepadStop() {
  robot.emergencyStop();
}

// --- CONTROL ROUTINES ---

// Phone command: ALWAYS parsed (to update configuredSpeed), but only moves the
// robot when the gamepad is not active (gamepad priority).
void processPhoneCommand(char* packet, bool padActive) {
  char command = 'S';
  int vLeftInput = DEFAULT_SPEED_LEFT;
  int vRightInput = DEFAULT_SPEED_RIGHT;

  int fieldsRead = sscanf(packet, "%c,%d,%d", &command, &vLeftInput, &vRightInput);

  uint16_t speedL = constrain(vLeftInput, 0, 1023);
  uint16_t speedR = constrain(vRightInput, 0, 1023);

  // The speed configured by Android is shared with the gamepad; it is updated
  // ALWAYS (even with the gamepad active) so the new limit applies at once.
  if ((command == 'F' || command == 'B' || command == 'L' || command == 'R') &&
      fieldsRead == 3) {
    uint16_t maxSpeed = max(speedL, speedR);
    if (maxSpeed > 0) configuredSpeed = maxSpeed;
  }

  // Gamepad connected = active control source: do not move motors from phone
  if (padActive) {
    return;
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
  Serial.print(" | Left: "); Serial.print(speedL);
  Serial.print(" | Right: "); Serial.println(speedR);
}

// Gamepad control: re-applies the state at 50 Hz. If no reports arrive,
// GamepadController already stops the robot by timeout (200 ms).
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
  // ~20 lines/s to avoid saturating Serial
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

  statusLed.begin();
  robot.begin();

  bluetooth.setSafetyStopCallback(safetyStop);
  bluetooth.begin();

  gamepad.setStopCallback(gamepadStop);
  gamepad.begin(); // AFTER bluetooth.begin(): BLEDevice is already initialized

  Serial.println("System ready - Dual control mode");
}

void loop() {
  // 1. Fresh gamepad state (scan/connect/reports)
  gamepad.update();

  // 2. Active control source and LED
  bool isPadActive = gamepad.isConnected();
  statusLed.setConnected(bluetooth.isConnected() || isPadActive);
  statusLed.update();

  safety.setGamepadActive(isPadActive);
  safety.check();

  // 3. Phone: always read; moves only if the gamepad is not active
  char packet[BLE_CMD_BUFFER_SIZE];
  if (bluetooth.getCommand(packet, sizeof(packet))) {
    processPhoneCommand(packet, isPadActive);
  }

  // 4. Gamepad: priority over the phone
  if (isPadActive) {
    processGamepadControl();
  }
}
