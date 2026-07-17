
#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H

#include <Arduino.h>

class MotorController {
private:
    // Pines físicos de conexión
    uint8_t pinEna, pinIn1, pinIn2; // Motor Izquierdo (A)
    uint8_t pinEnb, pinIn3, pinIn4; // Motor Derecho (B)

    // Configuración para el PWM en la API Core 3.0+
    const uint32_t pwmFreq = 5000;    // 5 kHz
    const uint8_t pwmResolution = 10; // 10 bits (Rango de velocidad: 0 a 1023)

public:
    // Constructor de la clase
    MotorController(uint8_t ena, uint8_t in1, uint8_t in2,
                    uint8_t enb, uint8_t in3, uint8_t in4);

    // Métodos de control del ciclo de vida
    void begin();
    void stop();

    // Métodos de movimiento con control independiente de velocidad (0 - 1023)
    void moveForward(uint16_t speedLeft, uint16_t speedRight);
    void moveBackward(uint16_t speedLeft, uint16_t speedRight);
    void turnLeft(uint16_t speedLeft, uint16_t speedRight);
    void turnRight(uint16_t speedLeft, uint16_t speedRight);
};

#endif
