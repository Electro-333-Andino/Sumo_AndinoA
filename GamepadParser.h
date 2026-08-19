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

#ifndef GAMEPAD_PARSER_H
#define GAMEPAD_PARSER_H

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

// ============================================================================
// GAMEPAD STATE
// ============================================================================

// Estado decodificado del mando a partir del último HID report recibido.
//
// Convención de los ejes NORMALIZADOS (rango [-1000, +1000]):
//   leftY  : positivo = hacia adelante (stick hacia arriba)
//            negativo = hacia atrás
//   rightX : positivo = stick hacia la derecha (giro a la derecha)
//            negativo = stick hacia la izquierda (giro a la izquierda)
//   leftX / rightY : normalizados igualmente; sin uso por ahora.
struct GamepadState {
    // Valores RAW del Xbox 1708: uint16 little-endian, 0..65535 (centro 32768)
    uint16_t rawLeftX, rawLeftY, rawRightX, rawRightY;

    // Valores normalizados a [-1000, +1000] con deadzone aplicada
    int16_t leftX, leftY, rightX, rightY;

    // Botones principales (bits reales del layout Xbox 1708)
    bool buttonA, buttonB, buttonX, buttonY;

    // D-Pad crudo (byte 12): 1=arriba, 2=arriba-der, ..., 8=arriba-izq, 0=none
    uint8_t dpad;

    // true mientras haya un mando conectado y reportando
    bool connected;
};

// ============================================================================
// PARSER DEL HID REPORT
// ============================================================================

// Convierte el report HID crudo del mando en un GamepadState normalizado.
// El layout concreto (offsets, centro, rango, polaridad) vive en
// GamepadParser.cpp, aislado del resto del firmware: para soportar otro
// modelo (p. ej. PlayStation) solo hay que tocar esa capa.
class GamepadParser {
public:
    GamepadParser();

    // Devuelve false si el report no corresponde al mando configurado.
    // Solo actualiza `out` cuando devuelve true.
    bool parseReport(const uint8_t* data, size_t len, GamepadState& out) const;

private:
    // raw -> [-1000, +1000]: compensa el centro, aplica deadzone, remapea
    // linealmente y conserva el signo. `invert` invierte la polaridad del eje.
    static int16_t normalizeAxis(int32_t raw, int32_t center, int32_t min,
                                 int32_t max, bool invert);

    // Curva de respuesta: por ahora lineal (devuelve magnitude tal cual).
    // Aquí se podrá añadir una curva exponencial sin tocar el resto.
    static int16_t applyResponseCurve(int16_t magnitude);
};

#endif
