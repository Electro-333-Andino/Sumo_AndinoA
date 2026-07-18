#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H

#include <Arduino.h>
#include <Preferences.h>

class MotorController {
private:
    // Pines físicos de conexión
    uint8_t pinEna, pinIn1, pinIn2; // Motor Izquierdo (A)
    uint8_t pinEnb, pinIn3, pinIn4; // Motor Derecho (B)
    uint8_t pinStby;                // Standby del TB6612 (activo en alto = driver habilitado)

    // Configuración para el PWM en la API Core 3.0+
    const uint32_t pwmFreq = 5000;    // 5 kHz
    const uint8_t pwmResolution = 10; // 10 bits (Rango de velocidad: 0 a 1023)

    // --- TRIMS DE CALIBRACIÓN (1.0 = sin corrección) ---
    // Un factor por motor y por sentido, porque la asimetría del puente H
    // suele ser distinta en adelante que en atrás.
    float trimLF = 1.0f; // Izquierda, Adelante
    float trimLB = 1.0f; // Izquierda, Atrás
    float trimRF = 1.0f; // Derecha, Adelante
    float trimRB = 1.0f; // Derecha, Atrás

    Preferences prefs;

    uint16_t applyTrim(uint16_t speed, float trim);

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
    // Los valores recibidos son "lógicos"; el trim se aplica internamente.
    // Cada uno re-habilita el driver automáticamente por si venía de un emergencyStop().
    void moveForward(uint16_t speedLeft, uint16_t speedRight);
    void moveBackward(uint16_t speedLeft, uint16_t speedRight);
    void turnLeft(uint16_t speedLeft, uint16_t speedRight);
    void turnRight(uint16_t speedLeft, uint16_t speedRight);

    // --- Calibración ---
    // motor: 'L' o 'R'   dir: 'F' (forward) o 'B' (backward)   value: 0.0 - 1.0
    void setTrim(char motor, char dir, float value);
    float getTrim(char motor, char dir);
    void loadCalibration();
    void saveCalibration();
    void resetCalibration();
};

#endif
