#include "StatusLed.h"

StatusLed::StatusLed(uint8_t pin) : pin(pin), isConnected(false), previousMillis(0), ledState(false) {}

void StatusLed::begin() {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

void StatusLed::setConnected(bool connected) {
  isConnected = connected;
  if (isConnected) {
    digitalWrite(pin, HIGH); // Fijo cuando está conectado
  }
}

void StatusLed::update() {
  if (isConnected) return; // Si está conectado, no parpadea

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    ledState = !ledState;
    digitalWrite(pin, ledState);
  }
}
