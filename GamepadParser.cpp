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
// MANDO OBJETIVO: Xbox Wireless Controller Model 1708 (Xbox One S) por BLE
// ============================================================================
// Layout verificado contra la implementación de referencia BLE-Gamepad-Client
// (https://github.com/tbekas/BLE-Gamepad-Client), que soporta explícitamente
// el modelo 1708, y contra el descriptor HID real del mando.
//
// El mando entrega el payload de la característica Report (0x2A4D) SIN el
// Report ID como primer byte: la referencia decodifica data[0..1] como LX.
// Los ejes son uint16 little-endian con rango 0..65535 (centro 32768).
//
//   Offset  Tamaño   Campo
//   0..1    u16 LE   Left Stick X   0 = izquierda, 32768 = centro, 65535 = derecha
//   2..3    u16 LE   Left Stick Y   0 = arriba,    32768 = centro, 65535 = abajo
//   4..5    u16 LE   Right Stick X  0 = izquierda, 32768 = centro, 65535 = derecha
//   6..7    u16 LE   Right Stick Y  0 = arriba,    32768 = centro, 65535 = abajo
//   8..9    u16 LE   Left Trigger   0..1023
//   10..11  u16 LE   Right Trigger  0..1023
//   12      1 byte   D-Pad          1=arriba, 2=arriba-der, ..., 8=arriba-izq, 0=none
//   13      1 byte   Botones 1      A=0x01, B=0x02, X=0x08, Y=0x10, LB=0x40, RB=0x80
//   14      1 byte   Botones 2      View=0x04, Menu=0x08, Xbox=0x10, LS=0x20, RS=0x40
//   15      1 byte   Botones 3      Share=0x01  [solo en el reporte de 16 bytes]
//
// Longitudes soportadas (variantes reales del 1708):
//   - 16 bytes: reporte completo (incluye el byte 15 de Share).
//   - 15 bytes: el descriptor puede anunciar 16 bytes mientras el firmware
//     transmite 15 (documentado en ciertos 1708 con firmware moderno). Se
//     parsean los offsets 0..14 y el campo de 16 bytes queda en false; NO se
//     desplazan los offsets para compensar el tamaño.
//   - 17 bytes: variante que SÍ incluye el Report ID 0x01 como primer byte
//     (depende de cómo cada stack BLE entregue el valor); se salta ese byte.
// ============================================================================

static constexpr uint8_t XBOX_REPORT_ID = 0x01;

// Offsets del payload (sin contar el posible Report ID)
static constexpr uint8_t OFF_LX = 0;
static constexpr uint8_t OFF_LY = 2;
static constexpr uint8_t OFF_RX = 4;
static constexpr uint8_t OFF_RY = 6;
static constexpr uint8_t OFF_DPAD = 12;
static constexpr uint8_t OFF_BUTTONS_1 = 13;
static constexpr uint8_t OFF_BUTTONS_2 = 14;
static constexpr uint8_t OFF_BUTTONS_3 = 15;

// Longitudes válidas del payload (variantes reales del 1708)
static constexpr uint8_t MIN_REPORT_LEN = 15;
static constexpr uint8_t MAX_REPORT_LEN = 16;

// Rango de los ejes (uint16 LE, centro 32768)
static constexpr int32_t AXIS_MIN    = 0;
static constexpr int32_t AXIS_MAX    = 65535;
static constexpr int32_t AXIS_CENTER = 32768;

// Deadzone en % del recorrido útil del joystick (por lado del centro).
static constexpr uint8_t GAMEPAD_DEADZONE_PERCENT = 10;

// Polaridad de los ejes según el layout real del 1708:
//   LY: arriba (0) = adelante          -> invertir el eje
//   RX: derecha (65535) = girar der.   -> no invertir
//   LX / RY: sin uso, normalizados por consistencia.
static constexpr bool INVERT_LX = false;
static constexpr bool INVERT_LY = true;
static constexpr bool INVERT_RX = false;
static constexpr bool INVERT_RY = true;

GamepadParser::GamepadParser() {}

// Une dos bytes little-endian en un uint16 (rango del mando: 0..65535).
static uint16_t makeUint16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

bool GamepadParser::parseReport(const uint8_t* data, size_t len, GamepadState& out) const {
    if (data == nullptr || len == 0) {
        return false;
    }

    // Variante que incluye el Report ID (según el stack BLE): se salta el
    // primer byte. La variante principal (sin ID) no exige data[0] == 0x01.
    size_t offset = 0;
    if (len == (size_t)MAX_REPORT_LEN + 1 && data[0] == XBOX_REPORT_ID) {
        offset = 1;
    }
    size_t payloadLen = len - offset;

    // Solo las longitudes reales del 1708 (15 y 16 bytes) son válidas.
    if (payloadLen < MIN_REPORT_LEN || payloadLen > MAX_REPORT_LEN) {
        return false;
    }

    const uint8_t* p = data + offset;

    uint16_t lx = makeUint16(p + OFF_LX);
    uint16_t ly = makeUint16(p + OFF_LY);
    uint16_t rx = makeUint16(p + OFF_RX);
    uint16_t ry = makeUint16(p + OFF_RY);

    out.rawLeftX  = lx;
    out.rawLeftY  = ly;
    out.rawRightX = rx;
    out.rawRightY = ry;

    out.leftX  = normalizeAxis(lx, AXIS_CENTER, AXIS_MIN, AXIS_MAX, INVERT_LX);
    out.leftY  = normalizeAxis(ly, AXIS_CENTER, AXIS_MIN, AXIS_MAX, INVERT_LY);
    out.rightX = normalizeAxis(rx, AXIS_CENTER, AXIS_MIN, AXIS_MAX, INVERT_RX);
    out.rightY = normalizeAxis(ry, AXIS_CENTER, AXIS_MIN, AXIS_MAX, INVERT_RY);

    out.dpad = p[OFF_DPAD];

    // Botones 1 (byte 13): A=bit0, B=bit1, X=bit3, Y=bit4 (layout real 1708)
    out.buttonA = (p[OFF_BUTTONS_1] & 0x01) != 0;
    out.buttonB = (p[OFF_BUTTONS_1] & 0x02) != 0;
    out.buttonX = (p[OFF_BUTTONS_1] & 0x08) != 0;
    out.buttonY = (p[OFF_BUTTONS_1] & 0x10) != 0;

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
