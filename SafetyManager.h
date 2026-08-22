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
#include "GamepadInputState.h"

// DEBUG de seguridad: 1 = logs SOLO en cambios de estado; 0 = silencio.
#ifndef SAFETY_DEBUG
#define SAFETY_DEBUG 0
#endif

// Máquina de estados de seguridad del robot (modo APP).
//   DISCONNECTED        sin enlace BLE; motores detenidos
//   WAITING_FOR_COMMAND BLE conectado, aún sin comando válido; motores detenidos
//   READY               comunicación válida (último comando: S)
//   MOVING              ejecutando F/B/L/R válido
//   TIMEOUT             watchdog expirado; motores detenidos
//   EMERGENCY_STOP      parada de máxima prioridad (comando inválido)
enum class SafetyState : uint8_t {
    DISCONNECTED,
    WAITING_FOR_COMMAND,
    READY,
    MOVING,
    TIMEOUT,
    EMERGENCY_STOP
};

// Watchdog fail-safe del teléfono. Principio fundamental:
//   - El robot NUNCA asume que la app enviará 'S'.
//   - Si dejan de llegar comandos VÁLIDOS (F/B/L/R/S) en `timeout` ms,
//     se ejecuta emergencyStop() UNA sola vez (transición edge a TIMEOUT).
//   - El último comando jamás permanece activo indefinidamente.
//   - Solo un comando válido nuevo recupera el control (CONTROL RESTORED).
class SafetyManager {
public:
    SafetyManager(MotorController& motor, BleManager& ble, unsigned long timeoutMs)
        : robot(motor), bluetooth(ble), timeout(timeoutMs), state(SafetyState::DISCONNECTED) {}

    // Estado inicial seguro: motores parados, esperando conexión.
    void begin() {
        inputState.invalidate();
        robot.emergencyStop();
        setState(SafetyState::DISCONNECTED);
    }

    // Llamar una vez por loop (modo APP).
    void update() {
        if (!bluetooth.isConnected()) {
            if (state != SafetyState::DISCONNECTED) {
                setState(SafetyState::DISCONNECTED);
                robot.emergencyStop();
                logTransition("[SAFETY] BLE DISCONNECTED -> STOP");
            }
            return;
        }

        if (state == SafetyState::DISCONNECTED) {
            // Al conectar se reinicia el watchdog: el primer comando debe
            // llegar dentro del timeout; mientras tanto el robot no se mueve.
            inputState.markValid(millis());
            setState(SafetyState::WAITING_FOR_COMMAND);
            return;
        }

        // Watchdog: SOLO lo alimentan comandos validados (onCommandAccepted).
        // Un paquete inválido no mantiene vivo al robot.
        if ((state == SafetyState::WAITING_FOR_COMMAND ||
             state == SafetyState::READY ||
             state == SafetyState::MOVING) &&
            !inputState.checkValid(millis(), timeout)) {
            setState(SafetyState::TIMEOUT); // transición única (edge)
            robot.emergencyStop();
            logTransition("[SAFETY] APP TIMEOUT -> STOP");
        }
    }

    // Comando válido aceptado (F/B/L/R/S): alimenta el watchdog.
    void onCommandAccepted(bool moving) {
        inputState.markValid(millis());
        if (state == SafetyState::TIMEOUT || state == SafetyState::EMERGENCY_STOP) {
            logTransition("[SAFETY] CONTROL RESTORED");
        }
        setState(moving ? SafetyState::MOVING : SafetyState::READY);
    }

    // Comando inválido: parada de seguridad; NO alimenta el watchdog.
    void onCommandRejected() {
        setState(SafetyState::EMERGENCY_STOP);
        robot.emergencyStop();
        logTransition("[SAFETY] INVALID COMMAND -> STOP");
    }

    SafetyState getState() const {
        return state;
    }

private:
    void setState(SafetyState s) {
        state = s;
    }

    void logTransition(const char* msg) {
#if SAFETY_DEBUG
        Serial.println(msg);
#endif
    }

    MotorController& robot;
    BleManager& bluetooth;
    unsigned long timeout;
    SafetyState state;
    GamepadInputState inputState; // watchdog: SOLO comandos válidos
};

#endif
