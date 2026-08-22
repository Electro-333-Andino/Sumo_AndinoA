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

#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include <stdint.h>
#include <stddef.h>

// ============================================================================
// PARSER DEL PROTOCOLO DE COMANDOS DEL TELÉFONO (SparkPilot)
// ============================================================================
// Lógica pura (sin dependencias de Arduino) para validar y decodificar un
// paquete del protocolo BLE. Se aísla aquí para poder probarla en el host con
// `make test`.
//
// Formatos aceptados:
//   S                       -> parada normal
//   L | R                   -> giro sin velocidades (usa defaultTurnSpeed)
//   F|B|L|R,velIzq,velDer   -> movimiento con velocidades 0..1023
//
// Todo lo demás se rechaza (valid=false): comando desconocido, parámetros
// incompletos, texto sobrante, no numérico o velocidades fuera de rango.
// Un comando inválido NUNCA alimenta el watchdog del robot.
// ============================================================================

struct ParsedCommand {
    char command;        // 'F','B','L','R','S' o '\0' si el paquete es inválido
    uint16_t speedLeft;  // 0..1023
    uint16_t speedRight; // 0..1023
    bool valid;
};

class CommandParser {
public:
    // `packet` no tiene que terminar en '\0' (se usa `len`), pero se toleran
    // CR/LF/espacios finales (la app puede enviar CRLF). `defaultTurnSpeed`
    // se aplica a los giros "L"/"R" sin velocidades explícitas.
    static ParsedCommand parse(const char* packet, size_t len, uint16_t defaultTurnSpeed);
};

#endif
