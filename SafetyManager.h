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

#ifndef SAFETY_MANAGER_H
#define SAFETY_MANAGER_H

#include <Arduino.h>
#include "MotorController.h"
#include "BleManager.h"

class SafetyManager {
public:
    SafetyManager(MotorController& motor, BleManager& ble, unsigned long timeoutMs)
        : robot(motor), bluetooth(ble), timeout(timeoutMs) {}

    void check() {
        // El watchdog del teléfono solo aplica cuando el mando NO está al mando:
        // con el gamepad activo, el failsafe lo gestiona GamepadController
        // (GAMEPAD_TIMEOUT_MS), mucho más agresivo que este timeout.
        if (!gamepadActive && bluetooth.isConnected() && bluetooth.millisSinceLastCommand() > timeout) {
            robot.emergencyStop();
        }
    }

    void setGamepadActive(bool active) {
        gamepadActive = active;
    }

    void setTimeout(unsigned long newTimeout) {
        timeout = newTimeout;
    }

private:
    MotorController& robot;
    BleManager& bluetooth;
    unsigned long timeout;
    bool gamepadActive = false;
};

#endif
