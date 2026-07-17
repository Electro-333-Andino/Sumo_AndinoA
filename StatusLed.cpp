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
