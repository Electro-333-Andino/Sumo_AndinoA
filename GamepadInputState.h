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

#ifndef GAMEPAD_INPUT_STATE_H
#define GAMEPAD_INPUT_STATE_H

#include <stdint.h>

// ============================================================================
// VALIDEZ DEL INPUT DEL MANDO (Estado 4 de la conexión)
// ============================================================================
// El robot SOLO acepta comandos de movimiento cuando el mando alcanza el
// Estado 4:
//   GATT conectado + notificaciones habilitadas + primer reporte HID válido
//   + timeout de comunicación no expirado.
//
// Esta clase encapsula esa lógica de forma pura (sin Arduino/NimBLE) para
// poder probarla en el host con `make test`.
//
// Contrato:
//   - markValid(now): se llama ÚNICAMENTE cuando GamepadParser aceptó un
//     reporte válido. Nunca por actividad BLE genérica ni por reportes
//     inválidos (un reporte corrupto no debe mantener vivo al robot).
//   - checkValid(now, timeoutMs): evalúa el watchdog; si expiró, invalida el
//     estado (los motores deben detenerse y el último comando no se reaplica).
//   - isValid(): consulta el estado sin modificar.
// ============================================================================

class GamepadInputState {
public:
    // Reporte válido recibido: alimenta el watchdog.
    void markValid(uint32_t now) {
        valid = true;
        lastValidAt = now;
    }

    // Devuelve true si hay input válido y el timeout no ha expirado.
    // Si expiró, invalida el estado (efecto: detención y bloqueo de input).
    bool checkValid(uint32_t now, uint32_t timeoutMs) {
        if (!valid) return false;
        if (now - lastValidAt > timeoutMs) {
            valid = false;
            return false;
        }
        return true;
    }

    bool isValid() const {
        return valid;
    }

    // Invalida sin más (desconexión, fallo de seguridad, conexión nueva...)
    void invalidate() {
        valid = false;
    }

private:
    bool valid = false;
    uint32_t lastValidAt = 0;
};

#endif
