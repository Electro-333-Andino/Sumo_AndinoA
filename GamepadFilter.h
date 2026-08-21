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

#ifndef GAMEPAD_FILTER_H
#define GAMEPAD_FILTER_H

#include <stdint.h>
#include <string>

// ============================================================================
// IDENTIFICACIÓN ESTRICTA DEL XBOX WIRELESS CONTROLLER 1708 (escaneo BLE)
// ============================================================================
// Lógica pura (sin dependencias de Arduino/NimBLE) para decidir si un
// dispositivo visto durante el escaneo es el mando objetivo. Se aísla en este
// header para poder probarla en el host con `make test`.
//
// Reglas (orden estricto):
//   Caso A — el dispositivo tiene nombre:
//       Se acepta SOLO si el nombre contiene "Xbox Wireless Controller".
//       Un nombre distinto se rechaza sin más comprobaciones.
//   Caso B — el dispositivo NO tiene nombre:
//       Se acepta SOLO si cumple AMBAS condiciones a la vez:
//         - Appearance dentro del rango del perfil HID (0x0380 .. 0x039F)
//         - Manufacturer Data con Company ID de Microsoft (0x0006)
//   Cualquier otro caso se rechaza. Un dispositivo BLE desconocido NUNCA se
//   convierte en candidato a controlador del robot.
//
// El Manufacturer Data es únicamente un criterio de IDENTIFICACIÓN durante el
// escaneo, no una prueba de identidad: la autenticación real la aporta BLE
// Secure Connections + bonding en el momento de conectar.
// ============================================================================

namespace GamepadFilter {

// Nombre anunciado por el Xbox 1708 (se busca como subcadena)
static constexpr const char* TARGET_NAME = "Xbox Wireless Controller";

// Longitud mínima de un nombre ACORTADO para aceptarlo como prefijo del
// objetivo (evita aceptar prefijos triviales como "X" o "Xbox").
static constexpr size_t MIN_SHORT_NAME_LEN = 8;

// AD types de nombre del advertising BLE (Bluetooth Assigned Numbers)
static constexpr uint8_t AD_TYPE_COMPLETE_NAME = 0x09; // Complete Local Name
static constexpr uint8_t AD_TYPE_SHORT_NAME    = 0x08; // Shortened Local Name

// Nombre efectivo del dispositivo: prioridad al nombre completo (0x09); si no
// está anunciado, se usa el acortado (0x08). El Xbox 1708 puede anunciar
// cualquiera de los dos formatos.
inline std::string resolveName(const std::string& completeName,
                               const std::string& shortName) {
    if (!completeName.empty()) return completeName;
    return shortName;
}

// Rango de Appearance del perfil HID (Bluetooth Assigned Numbers)
static constexpr uint16_t HID_APPEARANCE_MIN = 0x0380;
static constexpr uint16_t HID_APPEARANCE_MAX = 0x039F;

// Company ID de Microsoft en el Manufacturer Data (primeros 2 bytes, LE)
static constexpr uint16_t MICROSOFT_COMPANY_ID = 0x0006;

enum class MatchResult : uint8_t {
    NAME_MATCH,                    // Caso A: nombre correcto
    APPEARANCE_MANUFACTURER_MATCH, // Caso B: sin nombre + appearance HID + Microsoft
    NO_MATCH                       // rechazado
};

// Decisión pura y testeable.
//   hasName          : haveName() del adv packet
//   name             : nombre anunciado (vacío si no hay)
//   hasAppearance    : presencia de Appearance
//   appearance       : valor de Appearance (0 si no hay)
//   hasManufacturerData : presencia de Manufacturer Data
//   manufacturerId   : Company ID de 16 bits (little-endian) del Manufacturer Data
inline MatchResult evaluate(bool hasName, const std::string& name,
                            bool hasAppearance, uint16_t appearance,
                            bool hasManufacturerData, uint16_t manufacturerId) {
    // Caso A: dispositivo con nombre
    if (hasName && !name.empty()) {
        // Nombre COMPLETO: el nombre anunciado contiene el objetivo
        // (cubre subcadenas como "Xbox Wireless Controller 1234").
        if (name.find(TARGET_NAME) != std::string::npos) {
            return MatchResult::NAME_MATCH;
        }

        // Nombre ACORTADO (Shortened Local Name): es un truncamiento del
        // nombre completo (p. ej. "Xbox Wireless Cont"). Se acepta si es un
        // prefijo del objetivo, tiene una longitud mínima y, como respaldo de
        // seguridad, el fabricante es Microsoft.
        if (name.length() >= MIN_SHORT_NAME_LEN &&
            std::string(TARGET_NAME).compare(0, name.length(), name) == 0 &&
            hasManufacturerData && manufacturerId == MICROSOFT_COMPANY_ID) {
            return MatchResult::NAME_MATCH;
        }

        return MatchResult::NO_MATCH; // nombre distinto: rechazado
    }

    // Caso B: dispositivo sin nombre -> appearance HID Y Microsoft a la vez
    if (hasAppearance &&
        appearance >= HID_APPEARANCE_MIN && appearance <= HID_APPEARANCE_MAX &&
        hasManufacturerData && manufacturerId == MICROSOFT_COMPANY_ID) {
        return MatchResult::APPEARANCE_MANUFACTURER_MATCH;
    }

    return MatchResult::NO_MATCH;
}

} // namespace GamepadFilter

#endif
