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

#define PIN_STBY 7

// Si está conectado pero no llega ningún comando en este tiempo, se frena solo.
#define COMMAND_TIMEOUT_MS 400

// --- CALIBRACIÓN DE VELOCIDAD BASE POR DEFECTO (0 a 1023) ---
uint16_t VELOCIDAD_IZQUIERDA = 1023;
uint16_t VELOCIDAD_DERECHA   = 1023;
uint16_t VELOCIDAD_GIRO      = 800;

StatusLed ledEstado(PIN_LED);
MotorController robot(PIN_ENA, PIN_IN1, PIN_IN2, PIN_ENB, PIN_IN3, PIN_IN4, PIN_STBY);
BleManager bluetooth("Andino_Sumo_X");

// Se ejecuta desde el callback de desconexión BLE (posiblemente otra tarea),
// por eso solo toca pines directamente, nada de heap ni Strings aquí.
void safetyStop() {
  robot.emergencyStop(); // corte duro: STBY a LOW, el TB6612 queda en alta impedancia
}

void setup() {
  Serial.begin(115200);

  ledEstado.begin();
  robot.begin(); // carga los trims guardados en NVS
  bluetooth.setSafetyStopCallback(safetyStop);
  bluetooth.begin();

  Serial.println("Sistema iniciado. Trims cargados:");
  Serial.print("  LF="); Serial.print(robot.getTrim('L', 'F'));
  Serial.print("  LB="); Serial.print(robot.getTrim('L', 'B'));
  Serial.print("  RF="); Serial.print(robot.getTrim('R', 'F'));
  Serial.print("  RB="); Serial.println(robot.getTrim('R', 'B'));
}

void loop() {
  ledEstado.setConnected(bluetooth.isConnected());
  ledEstado.update();

  // Watchdog: conectado pero sin comandos nuevos por demasiado tiempo -> frenar
  if (bluetooth.isConnected() && bluetooth.millisSinceLastCommand() > COMMAND_TIMEOUT_MS) {
    robot.emergencyStop();
  }

  char paquete[BLE_CMD_BUFFER_SIZE];
  if (bluetooth.getCommand(paquete, sizeof(paquete))) {

    // --- COMANDO DE CALIBRACIÓN: "T,LF,0.91" ---
    if (strncmp(paquete, "T,", 2) == 0) {
      if (strcmp(paquete, "T,RESET") == 0) {
        robot.resetCalibration();
        Serial.println("Trims reseteados a 1.0");
      } else {
        char motor, dir;
        float valor;
        if (sscanf(paquete, "T,%c%c,%f", &motor, &dir, &valor) == 3) {
          robot.setTrim(motor, dir, valor);
          robot.saveCalibration();
          Serial.print("Trim guardado "); Serial.print(motor); Serial.print(dir);
          Serial.print(" = "); Serial.println(valor);
        }
      }
      return; // no seguir al parser de movimiento
    }

    char comando = 'S';
    int vIzqInput = VELOCIDAD_IZQUIERDA;
    int vDerInput = VELOCIDAD_DERECHA;

    int camposLeidos = sscanf(paquete, "%c,%d,%d", &comando, &vIzqInput, &vDerInput);

    uint16_t speedL = constrain(vIzqInput, 0, 1023);
    uint16_t speedR = constrain(vDerInput, 0, 1023);

    switch (comando) {
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

    Serial.print("Cmd: "); Serial.print(comando);
    Serial.print(" | Motor Izq: "); Serial.print(speedL);
    Serial.print(" | Motor Der: "); Serial.println(speedR);
  }
}
