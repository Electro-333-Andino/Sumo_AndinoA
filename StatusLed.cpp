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

#include "StatusLed.h"

StatusLed::StatusLed(uint8_t pin)
    : ledPin(pin), ultimoParpadeo(0), estadoLed(false), conectado(false) {}

void StatusLed::begin() {
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, LOW);
}

void StatusLed::setConnected(bool state) {
    conectado = state;
}

// Control asíncrono y no bloqueante del parpadeo
void StatusLed::update() {
    unsigned long tiempoActual = millis();
    unsigned long intervalo = conectado ? 600 : 150; // Lento si está conectado, rápido buscando señal

    if (tiempoActual - ultimoParpadeo >= intervalo) {
        ultimoParpadeo = tiempoActual;
        estadoLed = !estadoLed;
        digitalWrite(ledPin, estadoLed ? HIGH : LOW);
    }
}
