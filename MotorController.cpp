#include "MotorController.h"

MotorController::MotorController(uint8_t ena, uint8_t in1, uint8_t in2,
                                 uint8_t enb, uint8_t in3, uint8_t in4) {
  pinEna = ena; pinIn1 = in1; pinIn2 = in2;
  pinEnb = enb; pinIn3 = in3; pinIn4 = in4;
}

void MotorController::begin() {
  pinMode(pinIn1, OUTPUT); pinMode(pinIn2, OUTPUT);
  pinMode(pinIn3, OUTPUT); pinMode(pinIn4, OUTPUT);

  // --- NUEVA API DE ESPRESSIF (Versión 3.0+) ---
  // Ahora solo asocias el pin, la frecuencia y la resolución en bits en un solo paso:
  ledcAttach(pinEna, pwmFreq, pwmResolution);
  ledcAttach(pinEnb, pwmFreq, pwmResolution);

  stop();
}

void MotorController::stop() {
  digitalWrite(pinIn1, LOW); digitalWrite(pinIn2, LOW);
  digitalWrite(pinIn3, LOW); digitalWrite(pinIn4, LOW);

  // En la nueva API, ledcWrite usa directamente el PIN de hardware en vez de canales
  ledcWrite(pinEna, 0);
  ledcWrite(pinEnb, 0);
}

void MotorController::moveForward(uint16_t speedLeft, uint16_t speedRight) {
  digitalWrite(pinIn1, HIGH); digitalWrite(pinIn2, LOW);
  digitalWrite(pinIn3, HIGH); digitalWrite(pinIn4, LOW);

  ledcWrite(pinEna, speedLeft);
  ledcWrite(pinEnb, speedRight);
}

void MotorController::moveBackward(uint16_t speedLeft, uint16_t speedRight) {
  digitalWrite(pinIn1, LOW); digitalWrite(pinIn2, HIGH);
  digitalWrite(pinIn3, LOW); digitalWrite(pinIn4, HIGH);

  ledcWrite(pinEna, speedLeft);
  ledcWrite(pinEnb, speedRight);
}

void MotorController::turnLeft(uint16_t speedLeft, uint16_t speedRight) {
  digitalWrite(pinIn1, LOW); digitalWrite(pinIn2, HIGH);
  digitalWrite(pinIn3, HIGH); digitalWrite(pinIn4, LOW);

  ledcWrite(pinEna, speedLeft);
  ledcWrite(pinEnb, speedRight);
}

void MotorController::turnRight(uint16_t speedLeft, uint16_t speedRight) {
  digitalWrite(pinIn1, HIGH); digitalWrite(pinIn2, LOW);
  digitalWrite(pinIn3, LOW); digitalWrite(pinIn4, HIGH);

  ledcWrite(pinEna, speedLeft);
  ledcWrite(pinEnb, speedRight);
}
