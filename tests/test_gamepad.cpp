// Pruebas nativas (host, sin hardware) de GamepadParser + GamepadMixer.
// Compilar y ejecutar con: make test
//
// Protocolo probado: Xbox Wireless Controller Model 1708 por BLE.
// Payload de la característica Report (0x2A4D) SIN Report ID:
//   [0..1]  LX   u16 LE (0..65535, centro 32768)
//   [2..3]  LY   u16 LE
//   [4..5]  RX   u16 LE
//   [6..7]  RY   u16 LE
//   [8..11] LT/RT u16 LE (sin uso)
//   [12]    D-Pad
//   [13]    Botones 1 (A=0x01, B=0x02, X=0x08, Y=0x10)
//   [14]    Botones 2
//   [15]    Botones 3 (solo en el reporte de 16 bytes)

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "GamepadParser.h"
#include "GamepadMixer.h"

static int failures = 0;

static void check(const char* name, int32_t got, int32_t expected) {
  if (got != expected) {
    printf("FAIL %s: got %d, expected %d\n", name, got, expected);
    failures++;
  } else {
    printf("ok   %s: %d\n", name, got);
  }
}

// Construye un reporte de 16 bytes del Xbox 1708 con los sticks dados.
static void makeReport(uint8_t* r, uint16_t lx, uint16_t ly, uint16_t rx, uint16_t ry) {
  r[0] = (uint8_t)(lx & 0xFF);       r[1] = (uint8_t)((lx >> 8) & 0xFF);
  r[2] = (uint8_t)(ly & 0xFF);       r[3] = (uint8_t)((ly >> 8) & 0xFF);
  r[4] = (uint8_t)(rx & 0xFF);       r[5] = (uint8_t)((rx >> 8) & 0xFF);
  r[6] = (uint8_t)(ry & 0xFF);       r[7] = (uint8_t)((ry >> 8) & 0xFF);
  r[8] = 0x00; r[9] = 0x00;          // LT
  r[10] = 0x00; r[11] = 0x00;        // RT
  r[12] = 0x00;                      // D-Pad: 0 = none
  r[13] = 0x00;                      // Botones 1
  r[14] = 0x00;                      // Botones 2
  r[15] = 0x00;                      // Botones 3 (Share)
}

static const uint16_t CENTER = 32768; // centro real del eje (uint16 LE)

int main() {
  GamepadParser parser;
  GamepadMixer mixer;
  const int16_t MAX = 700; // configuredSpeed = 700

  // T1 — Centro: todos los sticks en 32768 -> normalizados a 0 y motores a 0
  {
    uint8_t r[16]; makeReport(r, CENTER, CENTER, CENTER, CENTER);
    GamepadState st; memset(&st, 0, sizeof(st));
    if (!parser.parseReport(r, 16, st)) {
      printf("FAIL T1: parse report\n");
      failures++;
    }
    check("T1 LX", st.leftX, 0);
    check("T1 LY", st.leftY, 0);
    check("T1 RX", st.rightX, 0);
    check("T1 RY", st.rightY, 0);
    MotorOutput o = mixer.calculate(st, MAX);
    check("T1 motor left", o.left, 0);
    check("T1 motor right", o.right, 0);
  }

  // T2 — Avance: LY = 0 (arriba) -> LY ≈ +1000, ambos motores +MAX
  {
    uint8_t r[16]; makeReport(r, CENTER, 0, CENTER, CENTER);
    GamepadState st; memset(&st, 0, sizeof(st));
    parser.parseReport(r, 16, st);
    check("T2 LY up", st.leftY, 1000);
    MotorOutput o = mixer.calculate(st, MAX);
    check("T2 motor left", o.left, MAX);
    check("T2 motor right", o.right, MAX);
  }

  // T3 — Retroceso: LY = 0xFFFF (abajo) -> LY ≈ -1000 (asimetría de 1 punto
  //     del recorrido real: 32768 arriba / 32767 abajo -> -999)
  {
    uint8_t r[16]; makeReport(r, CENTER, 0xFFFF, CENTER, CENTER);
    GamepadState st; memset(&st, 0, sizeof(st));
    parser.parseReport(r, 16, st);
    check("T3 LY down", st.leftY, -999);
    MotorOutput o = mixer.calculate(st, MAX);
    check("T3 motor left", o.left, -699);
    check("T3 motor right", o.right, -699);
  }

  // T4 — Giro derecha: RX = 0xFFFF -> RX ≈ +1000, giro sobre el sitio
  {
    uint8_t r[16]; makeReport(r, CENTER, CENTER, 0xFFFF, CENTER);
    GamepadState st; memset(&st, 0, sizeof(st));
    parser.parseReport(r, 16, st);
    check("T4 RX right", st.rightX, 1000);
    MotorOutput o = mixer.calculate(st, MAX);
    check("T4 motor left", o.left, MAX);
    check("T4 motor right", o.right, -MAX);
  }

  // T5 — Giro izquierda: RX = 0 -> RX ≈ -1000, giro sobre el sitio
  {
    uint8_t r[16]; makeReport(r, CENTER, CENTER, 0, CENTER);
    GamepadState st; memset(&st, 0, sizeof(st));
    parser.parseReport(r, 16, st);
    check("T5 RX left", st.rightX, -1000);
    MotorOutput o = mixer.calculate(st, MAX);
    check("T5 motor left", o.left, -MAX);
    check("T5 motor right", o.right, MAX);
  }

  // T6 — Deadzone: sticks a ±1000 del centro (< 10 % del recorrido) -> 0
  {
    uint8_t r[16]; makeReport(r, CENTER + 1000, CENTER - 1000, CENTER + 1000, CENTER - 1000);
    GamepadState st; memset(&st, 0, sizeof(st));
    parser.parseReport(r, 16, st);
    check("T6 deadzone LX", st.leftX, 0);
    check("T6 deadzone LY", st.leftY, 0);
    check("T6 deadzone RX", st.rightX, 0);
    check("T6 deadzone RY", st.rightY, 0);
  }

  // T7 — Reporte demasiado corto (10 bytes) -> false y estado NO modificado
  {
    uint8_t r[16]; makeReport(r, CENTER, CENTER, CENTER, CENTER);
    GamepadState st; memset(&st, 0, sizeof(st));
    st.leftY = 123; // valor centinela
    if (parser.parseReport(r, 10, st)) {
      printf("FAIL T7: short report accepted\n");
      failures++;
    }
    check("T7 state untouched", st.leftY, 123);
  }

  // T8 — Reporte inválido (14 bytes: no soportado por el 1708) -> false
  {
    uint8_t r[16]; makeReport(r, CENTER, CENTER, CENTER, CENTER);
    GamepadState st; memset(&st, 0, sizeof(st));
    if (parser.parseReport(r, 14, st)) {
      printf("FAIL T8: 14-byte report accepted\n");
      failures++;
    } else {
      printf("ok   T8: 14-byte report rejected\n");
    }
  }

  // T9 — Variante de 15 bytes (firmware que omite el byte 15) -> válido
  {
    uint8_t r[16]; makeReport(r, CENTER, 0x8001, CENTER, CENTER); // LY = 32769
    GamepadState st; memset(&st, 0, sizeof(st));
    if (!parser.parseReport(r, 15, st)) {
      printf("FAIL T9: 15-byte report rejected\n");
      failures++;
    }
    check("T9 LY raw (15b)", st.rawLeftY, 0x8001);
    check("T9 LY norm (15b)", st.leftY, 0);
  }

  // T10 — Reporte completo de 16 bytes -> válido
  {
    uint8_t r[16]; makeReport(r, CENTER, 0x8002, CENTER, CENTER); // LY = 32770
    GamepadState st; memset(&st, 0, sizeof(st));
    if (!parser.parseReport(r, 16, st)) {
      printf("FAIL T10: 16-byte report rejected\n");
      failures++;
    }
    check("T10 LY raw (16b)", st.rawLeftY, 0x8002);
    check("T10 LY norm (16b)", st.leftY, 0);
  }

  // T11 — Variante con Report ID 0x01 como primer byte (17 bytes) -> válido
  {
    uint8_t r[17];
    r[0] = 0x01; // Report ID
    makeReport(r + 1, CENTER, 0x8003, CENTER, CENTER);
    GamepadState st; memset(&st, 0, sizeof(st));
    if (!parser.parseReport(r, 17, st)) {
      printf("FAIL T11: 17-byte report (with Report ID) rejected\n");
      failures++;
    }
    check("T11 LY raw (17b)", st.rawLeftY, 0x8003);
  }

  // T12 — Botones reales (byte 13): A=bit0, X=bit3, Y=bit4
  {
    uint8_t r[16]; makeReport(r, CENTER, CENTER, CENTER, CENTER);
    r[13] = 0x01 | 0x08 | 0x10; // A + X + Y
    GamepadState st; memset(&st, 0, sizeof(st));
    parser.parseReport(r, 16, st);
    if (st.buttonA && st.buttonX && st.buttonY && !st.buttonB) {
      printf("ok   T12: buttons A/X/Y decoded\n");
    } else {
      printf("FAIL T12: buttons wrong (A=%d B=%d X=%d Y=%d)\n",
             st.buttonA, st.buttonB, st.buttonX, st.buttonY);
      failures++;
    }
  }

  // T13 — Ejemplo del contexto: NLY=1000 NRX=366 -> L=700 R=444 (mezcla)
  {
    GamepadState st; memset(&st, 0, sizeof(st));
    st.leftY = 1000; st.rightX = 366;
    MotorOutput o = mixer.calculate(st, MAX);
    check("T13 motor left (ejemplo)", o.left, 700);
    check("T13 motor right (ejemplo)", o.right, 444);
  }

  printf(failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d FAILURES\n", failures);
  return failures;
}
