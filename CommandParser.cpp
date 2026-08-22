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

#include "CommandParser.h"

ParsedCommand CommandParser::parse(const char* packet, size_t len, uint16_t defaultTurnSpeed) {
    ParsedCommand out = {'\0', 0, 0, false};
    if (packet == nullptr || len == 0) {
        return out;
    }

    // Quita CR/LF/espacios finales (la app puede enviar CRLF)
    size_t n = len;
    while (n > 0 && (packet[n - 1] == '\r' || packet[n - 1] == '\n' || packet[n - 1] == ' ')) {
        n--;
    }
    if (n == 0) {
        return out; // paquete vacío
    }

    out.command = packet[0];

    // --- Comando sin parámetros ---
    if (n == 1) {
        if (out.command == 'S') {
            out.valid = true; // parada normal (no es una condición de fallo)
            return out;
        }
        if (out.command == 'L' || out.command == 'R') {
            // Compatibilidad SparkPilot: giro sin velocidades explícitas
            out.speedLeft = defaultTurnSpeed;
            out.speedRight = defaultTurnSpeed;
            out.valid = true;
            return out;
        }
        return out; // comando desconocido
    }

    // --- Comando con velocidades: C,velIzq,velDer (sin espacios) ---
    if (out.command != 'F' && out.command != 'B' &&
        out.command != 'L' && out.command != 'R') {
        return out;
    }
    if (packet[1] != ',') {
        return out;
    }

    size_t i = 2;

    // Velocidad izquierda
    uint32_t vLeft = 0;
    size_t digits = 0;
    while (i < n && packet[i] >= '0' && packet[i] <= '9') {
        vLeft = vLeft * 10 + (uint32_t)(packet[i] - '0');
        digits++;
        if (vLeft > 1023) {
            return out; // fuera de rango (detecta también overflow)
        }
        i++;
    }
    if (digits == 0 || i >= n || packet[i] != ',') {
        return out; // sin dígitos o separador ausente
    }
    i++;

    // Velocidad derecha
    uint32_t vRight = 0;
    digits = 0;
    while (i < n && packet[i] >= '0' && packet[i] <= '9') {
        vRight = vRight * 10 + (uint32_t)(packet[i] - '0');
        digits++;
        if (vRight > 1023) {
            return out; // fuera de rango
        }
        i++;
    }
    if (digits == 0 || i != n) {
        return out; // sin dígitos o texto sobrante tras el segundo número
    }

    out.speedLeft = (uint16_t)vLeft;
    out.speedRight = (uint16_t)vRight;
    out.valid = true;
    return out;
}
