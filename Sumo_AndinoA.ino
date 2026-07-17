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

// --- CALIBRACIÓN DE VELOCIDAD BASE POR DEFECTO (0 a 1023) ---
uint16_t VELOCIDAD_IZQUIERDA = 1023;
uint16_t VELOCIDAD_DERECHA   = 1023;
uint16_t VELOCIDAD_GIRO      = 800;

StatusLed ledEstado(PIN_LED);
MotorController robot(PIN_ENA, PIN_IN1, PIN_IN2, PIN_ENB, PIN_IN3, PIN_IN4);
BleManager bluetooth("Andino_Sumo_X");

void setup() {
  Serial.begin(115200);

  ledEstado.begin();
  robot.begin();
  bluetooth.begin();

  Serial.println("Sistema iniciado. Modo velocidad independiente activo (Core 3.0+).");
}

void loop() {
  ledEstado.setConnected(bluetooth.isConnected());
  ledEstado.update();

  if (bluetooth.hasNewCommand()) {
    String paquete = bluetooth.getCommand();

    // Parada de emergencia automática por pérdida de enlace
    if (!bluetooth.isConnected()) {
      paquete = "S,0,0";
    }

    char comando = 'S';
    int vIzqInput = VELOCIDAD_IZQUIERDA;
    int vDerInput = VELOCIDAD_DERECHA;

    // PARSEADOR: Descompone el paquete de datos (Ej de entrada de la app: "F,1023,650")
    int camposLeidos = sscanf(paquete.c_str(), "%c,%d,%d", &comando, &vIzqInput, &vDerInput);

    // Acotación estricta de seguridad dentro del espectro de modulación del PWM (0 a 1023)
    uint16_t speedL = constrain(vIzqInput, 0, 1023);
    uint16_t speedR = constrain(vDerInput, 0, 1023);

    switch(comando) {
      case 'F':
        robot.moveForward(speedL, speedR);
        break;

      case 'B':
        robot.moveBackward(speedL, speedR);
        break;

      case 'L':
        if (camposLeidos == 3) {
          robot.turnLeft(speedL, speedR);
        } else {
          robot.turnLeft(VELOCIDAD_GIRO, VELOCIDAD_GIRO);
        }
        break;

      case 'R':
        if (camposLeidos == 3) {
          robot.turnRight(speedL, speedR);
        } else {
          robot.turnRight(VELOCIDAD_GIRO, VELOCIDAD_GIRO);
        }
        break;

      case 'S':
      default:
        robot.stop();
        break;
    }

    // Telemetría en el monitor serie para comprobar el control en tiempo real
    Serial.print("Cmd: "); Serial.print(comando);
    Serial.print(" | Motor Izq: "); Serial.print(speedL);
    Serial.print(" | Motor Der: "); Serial.println(speedR);
  }
}
