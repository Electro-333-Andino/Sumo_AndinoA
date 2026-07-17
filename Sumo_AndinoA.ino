#include "StatusLed.h"
#include "MotorController.h"
#include "BleManager.h"

#define PIN_LED 8

#define PIN_ENA 0
#define PIN_IN1 1
#define PIN_IN2 3

#define PIN_ENB 4
#define PIN_IN3 5
#define PIN_IN4 6

// --- NUEVA CALIBRACIÓN DE VELOCIDAD (0 a 1023) ---
// CAMBIO: Ahora usamos uint16_t. Tienes un control mucho más fino.
uint16_t VELOCIDAD_IZQUIERDA = 1023; // 100%
uint16_t VELOCIDAD_DERECHA   = 1023; // 100%
uint16_t VELOCIDAD_GIRO      = 800;  // Aprox 80% de velocidad para giros controlados

StatusLed ledEstado(PIN_LED);
MotorController robot(PIN_ENA, PIN_IN1, PIN_IN2, PIN_ENB, PIN_IN3, PIN_IN4);
BleManager bluetooth("Andino_Sumo");

void setup() {
  Serial.begin(115200);

  ledEstado.begin();
  robot.begin();
  bluetooth.begin();

  Serial.println("Sistema iniciado. Esperando conexión...");
}

void loop() {
  ledEstado.setConnected(bluetooth.isConnected());
  ledEstado.update();

  if (bluetooth.hasNewCommand()) {
    char comando = bluetooth.getCommand();

    if (!bluetooth.isConnected()) {
      comando = 'S';
    }

    switch(comando) {
      case 'F':
        robot.moveForward(VELOCIDAD_IZQUIERDA, VELOCIDAD_DERECHA);
        break;
      case 'B':
        robot.moveBackward(VELOCIDAD_IZQUIERDA, VELOCIDAD_DERECHA);
        break;
      case 'L':
        robot.turnLeft(VELOCIDAD_GIRO, VELOCIDAD_GIRO);
        break;
      case 'R':
        robot.turnRight(VELOCIDAD_GIRO, VELOCIDAD_GIRO);
        break;
      case 'S':
        robot.stop();
        break;
      default:
        robot.stop();
        break;
    }
  }
}
