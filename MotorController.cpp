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

  enableDriver(); // STBY en HIGH: referencia estable desde el arranque, nunca flotante
  stop();
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

void MotorController::emergencyStop() {
  stop();
  disableDriver(); // El TB6612 entero queda en alta impedancia, no solo PWM en 0
}

void MotorController::moveForward(uint16_t speedLeft, uint16_t speedRight) {
  enableDriver();
  digitalWrite(pinIn1, HIGH); digitalWrite(pinIn2, LOW);
  digitalWrite(pinIn3, HIGH); digitalWrite(pinIn4, LOW);

  ledcWrite(pinEna, speedLeft);
  ledcWrite(pinEnb, speedRight);
}

void MotorController::moveBackward(uint16_t speedLeft, uint16_t speedRight) {
  enableDriver();
  digitalWrite(pinIn1, LOW); digitalWrite(pinIn2, HIGH);
  digitalWrite(pinIn3, LOW); digitalWrite(pinIn4, HIGH);

  ledcWrite(pinEna, speedLeft);
  ledcWrite(pinEnb, speedRight);
}

void MotorController::turnLeft(uint16_t speedLeft, uint16_t speedRight) {
  enableDriver();
  // Motor izquierdo gira en reversa, motor derecho gira adelante
  digitalWrite(pinIn1, LOW); digitalWrite(pinIn2, HIGH);
  digitalWrite(pinIn3, HIGH); digitalWrite(pinIn4, LOW);

  ledcWrite(pinEna, speedLeft);
  ledcWrite(pinEnb, speedRight);
}

void MotorController::turnRight(uint16_t speedLeft, uint16_t speedRight) {
  enableDriver();
  // Motor izquierdo gira adelante, motor derecho gira en reversa
  digitalWrite(pinIn1, HIGH); digitalWrite(pinIn2, LOW);
  digitalWrite(pinIn3, LOW); digitalWrite(pinIn4, HIGH);

  ledcWrite(pinEna, speedLeft);
  ledcWrite(pinEnb, speedRight);
}

void MotorController::setMotorSpeeds(int16_t speedLeft, int16_t speedRight) {
  speedLeft = constrain(speedLeft, -1023, 1023);
  speedRight = constrain(speedRight, -1023, 1023);

  if (speedLeft == 0 && speedRight == 0) {
    stop();
    return;
  }

  enableDriver(); // re-habilita el driver por si venía de un emergencyStop()

  // Motor izquierdo: sentido según el signo
  if (speedLeft > 0) {
    digitalWrite(pinIn1, HIGH); digitalWrite(pinIn2, LOW);
    ledcWrite(pinEna, (uint16_t)speedLeft);
  } else if (speedLeft < 0) {
    digitalWrite(pinIn1, LOW); digitalWrite(pinIn2, HIGH);
    ledcWrite(pinEna, (uint16_t)(-speedLeft));
  } else {
    digitalWrite(pinIn1, LOW); digitalWrite(pinIn2, LOW);
    ledcWrite(pinEna, 0);
  }

  // Motor derecho: igual, con sus propios pines
  if (speedRight > 0) {
    digitalWrite(pinIn3, HIGH); digitalWrite(pinIn4, LOW);
    ledcWrite(pinEnb, (uint16_t)speedRight);
  } else if (speedRight < 0) {
    digitalWrite(pinIn3, LOW); digitalWrite(pinIn4, HIGH);
    ledcWrite(pinEnb, (uint16_t)(-speedRight));
  } else {
    digitalWrite(pinIn3, LOW); digitalWrite(pinIn4, LOW);
    ledcWrite(pinEnb, 0);
  }
}
