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

#include "GamepadParser.h"

// ============================================================================
// MANDO OBJETIVO: Xbox Wireless Controller (Model 1914 / Series X|S) por BLE
// ============================================================================
// HID input report (Report ID 0x01, 14 bytes):
//
//   Byte  Contenido
//   0     Report ID (0x01)
//   1     LX    0..255, centro 128 (0 = izquierda, 255 = derecha)
//   2     LY    0..255, centro 128 (0 = arriba,    255 = abajo)
//   3     RX    0..255, centro 128 (0 = izquierda, 255 = derecha)
//   4     RY    0..255, centro 128 (0 = arriba,    255 = abajo)
//   5     LT    gatillo izquierdo 0..255
//   6     RT    gatillo derecho   0..255
//   7     Reservado (0)
//   8     Botones 1: bit0 A, bit1 B, bit2 X, bit3 Y, bit4 LB, bit5 RB
//   9     Botones 2: bit4 LS, bit5 RS, ...
//   10    D-pad (0..7 direcciones, 8 = centrado)
//   11-13 Reservados (0)
//
// IMPORTANTE: verificar este layout con el mando real usando el dump
// [PAD] RAW... del sketch (GAMEPAD_DEBUG). Si el mando real es otro modelo
// (p. ej. DualShock 4), ajustar report ID, offsets y rango de ejes en este
// bloque: la arquitectura permite añadir parsers por modelo sin tocar el
// resto del firmware.
// ============================================================================

static constexpr uint8_t XBOX_REPORT_ID   = 0x01;

static constexpr uint8_t OFF_LX = 1;
static constexpr uint8_t OFF_LY = 2;
static constexpr uint8_t OFF_RX = 3;
static constexpr uint8_t OFF_RY = 4;
static constexpr uint8_t OFF_BUTTONS_1 = 8;

static constexpr int16_t AXIS_MIN    = 0;
static constexpr int16_t AXIS_MAX    = 255;
static constexpr int16_t AXIS_CENTER = 128;

// Deadzone en % del recorrido útil del joystick (por lado del centro).
static constexpr uint8_t GAMEPAD_DEADZONE_PERCENT = 10;

// Polaridad de los ejes según este HID report:
//   LY: arriba (0) = adelante     -> invertir el eje
//   RX: derecha (255) = girar der.-> no invertir
//   LX / RY: sin uso, solo se dejan normalizados por consistencia.
static constexpr bool INVERT_LX = false;
static constexpr bool INVERT_LY = true;
static constexpr bool INVERT_RX = false;
static constexpr bool INVERT_RY = true;

GamepadParser::GamepadParser() {}

bool GamepadParser::parseReport(const uint8_t* data, size_t len, GamepadState& out) const {
    if (data == nullptr || len < 5 || data[0] != XBOX_REPORT_ID) {
        return false;
    }

    out.rawLeftX  = data[OFF_LX];
    out.rawLeftY  = data[OFF_LY];
    out.rawRightX = data[OFF_RX];
    out.rawRightY = data[OFF_RY];

    out.leftX  = normalizeAxis(data[OFF_LX], AXIS_CENTER, AXIS_MIN, AXIS_MAX, INVERT_LX);
    out.leftY  = normalizeAxis(data[OFF_LY], AXIS_CENTER, AXIS_MIN, AXIS_MAX, INVERT_LY);
    out.rightX = normalizeAxis(data[OFF_RX], AXIS_CENTER, AXIS_MIN, AXIS_MAX, INVERT_RX);
    out.rightY = normalizeAxis(data[OFF_RY], AXIS_CENTER, AXIS_MIN, AXIS_MAX, INVERT_RY);

    if (len > OFF_BUTTONS_1) {
        out.buttonA = (data[OFF_BUTTONS_1] & 0x01) != 0;
        out.buttonB = (data[OFF_BUTTONS_1] & 0x02) != 0;
        out.buttonX = (data[OFF_BUTTONS_1] & 0x04) != 0;
        out.buttonY = (data[OFF_BUTTONS_1] & 0x08) != 0;
    } else {
        out.buttonA = out.buttonB = out.buttonX = out.buttonY = false;
    }

    out.connected = true;
    return true;
}

int16_t GamepadParser::normalizeAxis(int32_t raw, int32_t center, int32_t min,
                                     int32_t max, bool invert) {
    int32_t delta = raw - center;
    if (invert) delta = -delta;

    // Recorrido útil a cada lado del centro (puede ser asimétrico)
    int32_t range = (delta >= 0) ? (max - center) : (center - min);
    if (range <= 0) return 0;

    int32_t magnitude = delta >= 0 ? delta : -delta;

    // Deadzone: por debajo del umbral el eje se considera neutro
    int32_t deadzone = (range * GAMEPAD_DEADZONE_PERCENT) / 100;
    if (magnitude <= deadzone) return 0;

    // Remapeo lineal [deadzone, range] -> [0, 1000]
    int32_t usable = range - deadzone;
    if (usable <= 0) return 0;

    int32_t norm = ((magnitude - deadzone) * 1000) / usable;
    if (norm > 1000) norm = 1000;

    norm = applyResponseCurve((int16_t)norm);

    return (delta >= 0) ? (int16_t)norm : (int16_t)(-norm);
}

int16_t GamepadParser::applyResponseCurve(int16_t magnitude) {
    // Curva lineal: la velocidad es directamente proporcional al stick.
    // Para una respuesta exponencial se podría hacer, p. ej.:
    //   return (int16_t)(((int32_t)magnitude * magnitude) / 1000);
    return magnitude;
}
