#include "MotorController.h"

MotorController::MotorController(uint8_t ena, uint8_t in1, uint8_t in2,
                                 uint8_t enb, uint8_t in3, uint8_t in4,
                                 uint8_t stby) {
  pinEna = ena; pinIn1 = in1; pinIn2 = in2;
  pinEnb = enb; pinIn3 = in3; pinIn4 = in4;
  pinStby = stby;
}

void MotorController::begin() {
  pinMode(pinIn1, OUTPUT); pinMode(pinIn2, OUTPUT);
  pinMode(pinIn3, OUTPUT); pinMode(pinIn4, OUTPUT);
  pinMode(pinStby, OUTPUT);

  // --- API DE ESPRESSIF (Versión 3.0+) ---
  ledcAttach(pinEna, pwmFreq, pwmResolution);
  ledcAttach(pinEnb, pwmFreq, pwmResolution);

  loadCalibration();

  enableDriver(); // STBY en HIGH: referencia estable desde el arranque, nunca flotante
  stop();
}

uint16_t MotorController::applyTrim(uint16_t speed, float trim) {
  // Acota el trim a un rango sensato para evitar valores absurdos guardados por error
  if (trim < 0.3f) trim = 0.3f;
  if (trim > 1.0f) trim = 1.0f;
  return constrain((uint16_t)((float)speed * trim), 0, 1023);
}

void MotorController::stop() {
  digitalWrite(pinIn1, LOW); digitalWrite(pinIn2, LOW);
  digitalWrite(pinIn3, LOW); digitalWrite(pinIn4, LOW);

  ledcWrite(pinEna, 0);
  ledcWrite(pinEnb, 0);
}

void MotorController::enableDriver() {
  digitalWrite(pinStby, HIGH);
}

void MotorController::disableDriver() {
  digitalWrite(pinStby, LOW);
}

bool MotorController::isDriverEnabled() {
  return digitalRead(pinStby) == HIGH;
}

void MotorController::emergencyStop() {
  stop();
  disableDriver(); // El TB6612 entero queda en alta impedancia, no solo PWM en 0
}

void MotorController::moveForward(uint16_t speedLeft, uint16_t speedRight) {
  enableDriver();
  digitalWrite(pinIn1, HIGH); digitalWrite(pinIn2, LOW);
  digitalWrite(pinIn3, HIGH); digitalWrite(pinIn4, LOW);

  ledcWrite(pinEna, applyTrim(speedLeft, trimLF));
  ledcWrite(pinEnb, applyTrim(speedRight, trimRF));
}

void MotorController::moveBackward(uint16_t speedLeft, uint16_t speedRight) {
  enableDriver();
  digitalWrite(pinIn1, LOW); digitalWrite(pinIn2, HIGH);
  digitalWrite(pinIn3, LOW); digitalWrite(pinIn4, HIGH);

  ledcWrite(pinEna, applyTrim(speedLeft, trimLB));
  ledcWrite(pinEnb, applyTrim(speedRight, trimRB));
}

void MotorController::turnLeft(uint16_t speedLeft, uint16_t speedRight) {
  enableDriver();
  // Motor izquierdo gira en reversa, motor derecho gira adelante
  digitalWrite(pinIn1, LOW); digitalWrite(pinIn2, HIGH);
  digitalWrite(pinIn3, HIGH); digitalWrite(pinIn4, LOW);

  ledcWrite(pinEna, applyTrim(speedLeft, trimLB));
  ledcWrite(pinEnb, applyTrim(speedRight, trimRF));
}

void MotorController::turnRight(uint16_t speedLeft, uint16_t speedRight) {
  enableDriver();
  // Motor izquierdo gira adelante, motor derecho gira en reversa
  digitalWrite(pinIn1, HIGH); digitalWrite(pinIn2, LOW);
  digitalWrite(pinIn3, LOW); digitalWrite(pinIn4, HIGH);

  ledcWrite(pinEna, applyTrim(speedLeft, trimLF));
  ledcWrite(pinEnb, applyTrim(speedRight, trimRB));
}

// ---------------- Calibración ----------------

void MotorController::setTrim(char motor, char dir, float value) {
  if (value < 0.3f) value = 0.3f;
  if (value > 1.0f) value = 1.0f;

  if (motor == 'L' && dir == 'F') trimLF = value;
  else if (motor == 'L' && dir == 'B') trimLB = value;
  else if (motor == 'R' && dir == 'F') trimRF = value;
  else if (motor == 'R' && dir == 'B') trimRB = value;
}

float MotorController::getTrim(char motor, char dir) {
  if (motor == 'L' && dir == 'F') return trimLF;
  if (motor == 'L' && dir == 'B') return trimLB;
  if (motor == 'R' && dir == 'F') return trimRF;
  if (motor == 'R' && dir == 'B') return trimRB;
  return 1.0f;
}

void MotorController::loadCalibration() {
  prefs.begin("trim", true); // solo lectura
  trimLF = prefs.getFloat("LF", 1.0f);
  trimLB = prefs.getFloat("LB", 1.0f);
  trimRF = prefs.getFloat("RF", 1.0f);
  trimRB = prefs.getFloat("RB", 1.0f);
  prefs.end();
}

void MotorController::saveCalibration() {
  prefs.begin("trim", false); // lectura/escritura
  prefs.putFloat("LF", trimLF);
  prefs.putFloat("LB", trimLB);
  prefs.putFloat("RF", trimRF);
  prefs.putFloat("RB", trimRB);
  prefs.end();
}

void MotorController::resetCalibration() {
  trimLF = trimLB = trimRF = trimRB = 1.0f;
  saveCalibration();
}
