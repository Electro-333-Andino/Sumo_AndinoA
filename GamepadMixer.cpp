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

#include "GamepadMixer.h"

MotorOutput GamepadMixer::calculate(const GamepadState& state, int16_t maxSpeed) const {
    if (maxSpeed < 0) maxSpeed = 0;

    // Escalamiento entero: normalizado [-1000, +1000] -> [-maxSpeed, +maxSpeed]
    // int32 evita overflow al sumar forward + turn (máx. 2 * 1023).
    int32_t forward = ((int32_t)state.leftY * maxSpeed) / 1000;
    int32_t turn    = ((int32_t)state.rightX * maxSpeed) / 1000;

    int32_t left  = forward + turn;
    int32_t right = forward - turn;

    left  = constrain(left, -maxSpeed, maxSpeed);
    right = constrain(right, -maxSpeed, maxSpeed);

    MotorOutput out;
    out.left  = (int16_t)left;
    out.right = (int16_t)right;
    return out;
}
