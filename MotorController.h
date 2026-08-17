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

#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H

#include <Arduino.h>

class MotorController {
private:
    // Pines físicos de conexión
    uint8_t pinEna, pinIn1, pinIn2; // Motor Izquierdo (A)
    uint8_t pinEnb, pinIn3, pinIn4; // Motor Derecho (B)
    uint8_t pinStby;                // Standby del TB6612 (activo en alto = driver habilitado)

    // Configuración para el PWM en la API Core 3.0+
    const uint32_t pwmFreq = 5000;    // 5 kHz
    const uint8_t pwmResolution = 10; // 10 bits (Rango de velocidad: 0 a 1023)

public:
    // Constructor de la clase
    MotorController(uint8_t ena, uint8_t in1, uint8_t in2,
                    uint8_t enb, uint8_t in3, uint8_t in4,
                    uint8_t stby);

    // Métodos de control del ciclo de vida
    void begin();
    void stop();               // Paro normal: PWM a 0, driver sigue habilitado (respuesta rápida)
    void emergencyStop();      // Paro duro: PWM a 0 Y el TB6612 entero pasa a standby (STBY = LOW)

    // Habilitar/deshabilitar el driver TB6612 a nivel de hardware
    void enableDriver();
    void disableDriver();
    bool isDriverEnabled();

    // Métodos de movimiento con control independiente de velocidad (0 - 1023)
    // Los valores recibidos se aplican tal cual al PWM (0 - 1023).
    // Cada uno re-habilita el driver automáticamente por si venía de un emergencyStop().
    void moveForward(uint16_t speedLeft, uint16_t speedRight);
    void moveBackward(uint16_t speedLeft, uint16_t speedRight);
    void turnLeft(uint16_t speedLeft, uint16_t speedRight);
    void turnRight(uint16_t speedLeft, uint16_t speedRight);

    // Mezcla diferencial proporcional: velocidades y sentidos independientes
    // por motor. Positivo = adelante, negativo = atrás (rango interno ±1023).
    void setMotorSpeeds(int16_t speedLeft, int16_t speedRight);
};

#endif
