// Pruebas nativas (host, sin hardware) de GamepadParser + GamepadMixer.
// Compilar y ejecutar con: make test
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "GamepadParser.h"
#include "GamepadMixer.h"

static int failures = 0;

static void check(const char* name, int16_t got, int16_t expected) {
  if (got != expected) {
    printf("FAIL %s: got %d, expected %d\n", name, got, expected);
    failures++;
  } else {
    printf("ok   %s: %d\n", name, got);
  }
}

// Report Xbox: id=1, LX, LY, RX, RY, LT, RT, 0, btns1, btns2, dpad, 0,0,0
static void makeReport(uint8_t* r, int ly, int rx) {
  r[0] = 0x01;
  r[1] = 128;         // LX center
  r[2] = (uint8_t)ly; // LY
  r[3] = (uint8_t)rx; // RX
  r[4] = 128;         // RY center
  r[5] = 0; r[6] = 0; r[7] = 0;
  r[8] = 0; r[9] = 0; r[10] = 8; r[11] = 0; r[12] = 0; r[13] = 0;
}

int main() {
  GamepadParser parser;
  GamepadMixer mixer;
  const int16_t MAX = 700; // configuredSpeed = 700

  // 1) Ambos sticks centrados -> 0,0
  {
    uint8_t r[14]; makeReport(r, 128, 128);
    GamepadState st; parser.parseReport(r, 14, st);
    MotorOutput o = mixer.calculate(st, MAX);
    check("T1 center: left", o.left, 0);
    check("T1 center: right", o.right, 0);
  }

  // 2) LY 100% arriba (0) -> ambos motores +MAX
  {
    uint8_t r[14]; makeReport(r, 0, 128);
    GamepadState st; parser.parseReport(r, 14, st);
    check("T2 NLY full up", st.leftY, 1000);
    MotorOutput o = mixer.calculate(st, MAX);
    check("T2 left", o.left, MAX);
    check("T2 right", o.right, MAX);
  }

  // 3) LY 100% abajo (255) -> ambos motores -MAX
  //    (asimetría real del stick: 128 arriba / 127 abajo -> -991, aceptable)
  {
    uint8_t r[14]; makeReport(r, 255, 128);
    GamepadState st; parser.parseReport(r, 14, st);
    check("T3 NLY full down", st.leftY, -991);
    MotorOutput o = mixer.calculate(st, MAX);
    check("T3 left", o.left, -693);
    check("T3 right", o.right, -693);
  }

  // 4) RX 100% a la derecha (255) -> giro a la derecha sobre el sitio: +MAX / -MAX
  {
    uint8_t r[14]; makeReport(r, 128, 255);
    GamepadState st; parser.parseReport(r, 14, st);
    check("T4 NRX full right", st.rightX, 1000);
    MotorOutput o = mixer.calculate(st, MAX);
    check("T4 left", o.left, MAX);
    check("T4 right", o.right, -MAX);
  }

  // 5) RX 100% a la izquierda (0) -> giro a la izquierda sobre el sitio: -MAX / +MAX
  {
    uint8_t r[14]; makeReport(r, 128, 0);
    GamepadState st; parser.parseReport(r, 14, st);
    check("T5 NRX full left", st.rightX, -1000);
    MotorOutput o = mixer.calculate(st, MAX);
    check("T5 left", o.left, -MAX);
    check("T5 right", o.right, MAX);
  }

  // 6) Avance + giro: left = forward+turn, right = forward-turn
  //    LY -> NLY=495 (raw 59), RX -> NRX=200 (raw 163); 495*700/1000=346
  {
    uint8_t r[14]; makeReport(r, 59, 163);
    GamepadState st; parser.parseReport(r, 14, st);
    check("T6 NLY", st.leftY, 495);
    check("T6 NRX", st.rightX, 200);
    MotorOutput o = mixer.calculate(st, MAX); // f=346 t=140
    check("T6 left", o.left, 486);
    check("T6 right", o.right, 206);
  }

  // 7) Stick dentro de la deadzone (LY raw 118 -> magnitud 10 < 12) -> 0
  {
    uint8_t r[14]; makeReport(r, 118, 128);
    GamepadState st; parser.parseReport(r, 14, st);
    check("T7 deadzone LY", st.leftY, 0);
  }

  // 8) Límite: avance total + giro total -> left limitado a +MAX
  {
    uint8_t r[14]; makeReport(r, 0, 255);
    GamepadState st; parser.parseReport(r, 14, st);
    MotorOutput o = mixer.calculate(st, MAX);
    check("T8 left clamp", o.left, MAX);
    check("T8 right clamp", o.right, 0);
  }

  // 9) Ejemplo del contexto: NLY=1000 NRX=366 -> L=700 R=444
  {
    GamepadState st; memset(&st, 0, sizeof(st));
    st.leftY = 1000; st.rightX = 366;
    MotorOutput o = mixer.calculate(st, MAX);
    check("T9 left (context example)", o.left, 700);
    check("T9 right (context example)", o.right, 444);
  }

  // 10) Report inválido (otro report ID) -> el parse falla
  {
    uint8_t r[14]; makeReport(r, 128, 128); r[0] = 0x11;
    GamepadState st; memset(&st, 0, sizeof(st));
    if (parser.parseReport(r, 14, st)) {
      printf("FAIL T10: invalid report accepted\n");
      failures++;
    } else {
      printf("ok   T10: invalid report rejected\n");
    }
  }

  printf(failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d FAILURES\n", failures);
  return failures;
}
