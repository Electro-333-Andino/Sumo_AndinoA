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

#ifndef GAMEPAD_MIXER_H
#define GAMEPAD_MIXER_H

#include <Arduino.h>
#include <stdint.h>
#include "GamepadParser.h"

// ============================================================================
// MEZCLA DIFERENCIAL
// ============================================================================

// Salida del mezclador: velocidad por motor.
// Signo positivo = adelante, negativo = atrás. Rango [-maxSpeed, +maxSpeed].
struct MotorOutput {
    int16_t left;   // Motor izquierdo
    int16_t right;  // Motor derecho
};

// Convierte el estado del mando en velocidades por motor:
//   forward = leftY  * maxSpeed   (leftY positivo = adelante)
//   turn    = rightX * maxSpeed   (rightX positivo = girar a la derecha)
//   left  = forward + turn
//   right = forward - turn
// Ambos resultados se limitan a [-maxSpeed, +maxSpeed].
class GamepadMixer {
public:
    MotorOutput calculate(const GamepadState& state, int16_t maxSpeed) const;
};

#endif
