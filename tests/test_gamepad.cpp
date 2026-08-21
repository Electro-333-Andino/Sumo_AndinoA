// Pruebas nativas (host, sin hardware) de GamepadParser + GamepadMixer +
// GamepadFilter + GamepadInputState. Compilar y ejecutar con: make test
//
// Protocolo probado: Xbox Wireless Controller Model 1708 por BLE.
// Payload de la característica Report (0x2A4D) SIN Report ID, 16 bytes:
//   [0..1]  LX   u16 LE (0..65535, centro 32768)
//   [2..3]  LY   u16 LE
//   [4..5]  RX   u16 LE
//   [6..7]  RY   u16 LE
//   [8..11] LT/RT u16 LE (sin uso)
//   [12]    D-Pad
//   [13]    Botones 1 (A=0x01, B=0x02, X=0x08, Y=0x10)
//   [14]    Botones 2
//   [15]    Botones 3 (Share)

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "GamepadParser.h"
#include "GamepadMixer.h"
#include "GamepadFilter.h"
#include "GamepadInputState.h"

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

// ---------------------------------------------------------------------------
// 1. PARSER — reportes del Xbox 1708 (16 bytes)
// ---------------------------------------------------------------------------
static void testParser(GamepadParser& parser, GamepadMixer& mixer, int16_t MAX) {
  printf("--- Parser (16 bytes) ---\n");

  // P1 — Centro: todos los sticks en 32768 -> normalizados a 0 y motores a 0
  {
    uint8_t r[16]; makeReport(r, CENTER, CENTER, CENTER, CENTER);
    GamepadState st; memset(&st, 0, sizeof(st));
    if (!parser.parseReport(r, 16, st)) {
      printf("FAIL P1: parse report\n");
      failures++;
    }
    check("P1 LX", st.leftX, 0);
    check("P1 LY", st.leftY, 0);
    check("P1 RX", st.rightX, 0);
    check("P1 RY", st.rightY, 0);
    MotorOutput o = mixer.calculate(st, MAX);
    check("P1 motor left", o.left, 0);
    check("P1 motor right", o.right, 0);
  }

  // P2 — Avance: LY = 0 (arriba) -> LY ≈ +1000, ambos motores +MAX
  {
    uint8_t r[16]; makeReport(r, CENTER, 0, CENTER, CENTER);
    GamepadState st; memset(&st, 0, sizeof(st));
    parser.parseReport(r, 16, st);
    check("P2 LY up", st.leftY, 1000);
    MotorOutput o = mixer.calculate(st, MAX);
    check("P2 motor left", o.left, MAX);
    check("P2 motor right", o.right, MAX);
  }

  // P3 — Retroceso: LY = 0xFFFF (abajo) -> LY ≈ -1000 (asimetría real de 1 punto)
  {
    uint8_t r[16]; makeReport(r, CENTER, 0xFFFF, CENTER, CENTER);
    GamepadState st; memset(&st, 0, sizeof(st));
    parser.parseReport(r, 16, st);
    check("P3 LY down", st.leftY, -999);
    MotorOutput o = mixer.calculate(st, MAX);
    check("P3 motor left", o.left, -699);
    check("P3 motor right", o.right, -699);
  }

  // P4 — Giro derecha: RX = 0xFFFF -> RX ≈ +1000, giro sobre el sitio
  {
    uint8_t r[16]; makeReport(r, CENTER, CENTER, 0xFFFF, CENTER);
    GamepadState st; memset(&st, 0, sizeof(st));
    parser.parseReport(r, 16, st);
    check("P4 RX right", st.rightX, 1000);
    MotorOutput o = mixer.calculate(st, MAX);
    check("P4 motor left", o.left, MAX);
    check("P4 motor right", o.right, -MAX);
  }

  // P5 — Giro izquierda: RX = 0 -> RX ≈ -1000, giro sobre el sitio
  {
    uint8_t r[16]; makeReport(r, CENTER, CENTER, 0, CENTER);
    GamepadState st; memset(&st, 0, sizeof(st));
    parser.parseReport(r, 16, st);
    check("P5 RX left", st.rightX, -1000);
    MotorOutput o = mixer.calculate(st, MAX);
    check("P5 motor left", o.left, -MAX);
    check("P5 motor right", o.right, MAX);
  }

  // P6 — Deadzone: sticks a ±1000 del centro (< 10 % del recorrido) -> 0
  {
    uint8_t r[16]; makeReport(r, CENTER + 1000, CENTER - 1000, CENTER + 1000, CENTER - 1000);
    GamepadState st; memset(&st, 0, sizeof(st));
    parser.parseReport(r, 16, st);
    check("P6 deadzone LX", st.leftX, 0);
    check("P6 deadzone LY", st.leftY, 0);
    check("P6 deadzone RX", st.rightX, 0);
    check("P6 deadzone RY", st.rightY, 0);
  }

  // P7 — Reporte demasiado corto (10 bytes) -> false y estado NO modificado
  {
    uint8_t r[16]; makeReport(r, CENTER, CENTER, CENTER, CENTER);
    GamepadState st; memset(&st, 0, sizeof(st));
    st.leftY = 123; // valor centinela
    if (parser.parseReport(r, 10, st)) {
      printf("FAIL P7: short report accepted\n");
      failures++;
    }
    check("P7 state untouched", st.leftY, 123);
  }

  // P8 — Reporte inválido (14 bytes) -> false
  {
    uint8_t r[16]; makeReport(r, CENTER, CENTER, CENTER, CENTER);
    GamepadState st; memset(&st, 0, sizeof(st));
    if (parser.parseReport(r, 14, st)) {
      printf("FAIL P8: 14-byte report accepted\n");
      failures++;
    } else {
      printf("ok   P8: 14-byte report rejected\n");
    }
  }

  // P9 — Variante de 15 bytes (anomalía de BT Classic, NO del transporte BLE
  //      del 1708) -> debe ser rechazada
  {
    uint8_t r[16]; makeReport(r, CENTER, 0x8001, CENTER, CENTER);
    GamepadState st; memset(&st, 0, sizeof(st));
    if (parser.parseReport(r, 15, st)) {
      printf("FAIL P9: 15-byte report accepted\n");
      failures++;
    } else {
      printf("ok   P9: 15-byte report rejected\n");
    }
  }

  // P10 — Reporte de 16 bytes -> válido
  {
    uint8_t r[16]; makeReport(r, CENTER, 0x8002, CENTER, CENTER); // LY = 32770
    GamepadState st; memset(&st, 0, sizeof(st));
    if (!parser.parseReport(r, 16, st)) {
      printf("FAIL P10: 16-byte report rejected\n");
      failures++;
    }
    check("P10 LY raw (16b)", st.rawLeftY, 0x8002);
    check("P10 LY norm (16b)", st.leftY, 0);
  }

  // P11 — Variante de 17 bytes (con Report ID añadido por un stack ajeno) ->
  //       debe ser rechazada: el payload del 1708 es de 16 bytes sin Report ID
  {
    uint8_t r[17];
    r[0] = 0x01; // Report ID
    makeReport(r + 1, CENTER, 0x8003, CENTER, CENTER);
    GamepadState st; memset(&st, 0, sizeof(st));
    if (parser.parseReport(r, 17, st)) {
      printf("FAIL P11: 17-byte report (with Report ID) accepted\n");
      failures++;
    } else {
      printf("ok   P11: 17-byte report rejected\n");
    }
  }

  // P12 — Botones reales (byte 13): A=bit0, X=bit3, Y=bit4
  {
    uint8_t r[16]; makeReport(r, CENTER, CENTER, CENTER, CENTER);
    r[13] = 0x01 | 0x08 | 0x10; // A + X + Y
    GamepadState st; memset(&st, 0, sizeof(st));
    parser.parseReport(r, 16, st);
    if (st.buttonA && st.buttonX && st.buttonY && !st.buttonB) {
      printf("ok   P12: buttons A/X/Y decoded\n");
    } else {
      printf("FAIL P12: buttons wrong (A=%d B=%d X=%d Y=%d)\n",
             st.buttonA, st.buttonB, st.buttonX, st.buttonY);
      failures++;
    }
  }

  // P13 — Mezcla: NLY=1000 NRX=366 -> L=700 R=444 (velocidad proporcional)
  {
    GamepadState st; memset(&st, 0, sizeof(st));
    st.leftY = 1000; st.rightX = 366;
    MotorOutput o = mixer.calculate(st, MAX);
    check("P13 motor left", o.left, 700);
    check("P13 motor right", o.right, 444);
  }
}

// ---------------------------------------------------------------------------
// 2. FILTRO — identificación estricta del Xbox (test de dispositivo incorrecto)
// ---------------------------------------------------------------------------
static void testFilter() {
  printf("--- GamepadFilter (identificación estricta) ---\n");

  using namespace GamepadFilter;

  // F1 — Nombre correcto -> aceptado por nombre
  check("F1 name match", (int32_t)evaluate(true, "Xbox Wireless Controller", false, 0, false, 0) == (int32_t)MatchResult::NAME_MATCH, 1);

  // F2 — Nombre diferente -> rechazado (aunque tenga appearance/MS)
  check("F2 wrong name", (int32_t)evaluate(true, "Android", true, 0x0384, true, MICROSOFT_COMPANY_ID) == (int32_t)MatchResult::NO_MATCH, 1);

  // F3 — Sin nombre + appearance gamepad + Microsoft -> aceptado (Caso B)
  check("F3 no name + appearance + MS", (int32_t)evaluate(false, "", true, 0x0384, true, MICROSOFT_COMPANY_ID) == (int32_t)MatchResult::APPEARANCE_MANUFACTURER_MATCH, 1);

  // F4 — Sin nombre + appearance HID genérico (0x0380) + Microsoft -> aceptado
  check("F4 no name + HID generic + MS", (int32_t)evaluate(false, "", true, 0x0380, true, MICROSOFT_COMPANY_ID) == (int32_t)MatchResult::APPEARANCE_MANUFACTURER_MATCH, 1);

  // F5 — Sin nombre + appearance HID pero SIN Microsoft -> rechazado
  check("F5 no name + appearance, no MS", (int32_t)evaluate(false, "", true, 0x0384, true, 0x004C) == (int32_t)MatchResult::NO_MATCH, 1);

  // F6 — Sin nombre + Microsoft pero appearance NO HID -> rechazado
  check("F6 no name + MS, no HID appearance", (int32_t)evaluate(false, "", true, 0x0040, true, MICROSOFT_COMPANY_ID) == (int32_t)MatchResult::NO_MATCH, 1);

  // F7 — Sin nombre + solo Microsoft (sin appearance) -> rechazado
  check("F7 no name + MS only", (int32_t)evaluate(false, "", false, 0, true, MICROSOFT_COMPANY_ID) == (int32_t)MatchResult::NO_MATCH, 1);

  // F8 — Sin nombre y sin ningún dato -> rechazado (el clásico "sin nombre se acepta" YA NO)
  check("F8 anonymous rejected", (int32_t)evaluate(false, "", false, 0, false, 0) == (int32_t)MatchResult::NO_MATCH, 1);

  // F9 — Sin nombre + HID sin manufacturer data -> rechazado
  check("F9 no name + appearance only", (int32_t)evaluate(false, "", true, 0x0384, false, 0) == (int32_t)MatchResult::NO_MATCH, 1);

  // F10 — Nombre correcto (subcadena) aunque el dispositivo anuncie poco -> aceptado
  check("F10 name substring", (int32_t)evaluate(true, "Xbox Wireless Controller 1234", false, 0, false, 0) == (int32_t)MatchResult::NAME_MATCH, 1);
}

// ---------------------------------------------------------------------------
// 3. WATCHDOG — validez del input (test de seguridad obligatorio)
// ---------------------------------------------------------------------------
static void testWatchdog(GamepadMixer& mixer, int16_t MAX) {
  printf("--- GamepadInputState (watchdog) ---\n");

  GamepadInputState input;
  GamepadState st; memset(&st, 0, sizeof(st));
  st.leftY = 1000; // último comando válido recibido

  // W1 — Inicialmente NO hay input válido: el robot no se mueve
  check("W1 initial invalid", (int32_t)input.isValid(), 0);
  MotorOutput o0 = input.isValid() ? mixer.calculate(st, MAX) : MotorOutput{0, 0};
  check("W1 motors stopped", o0.left, 0);

  // W2 — Reporte válido a t=1000 ms -> input activo y motores responden
  input.markValid(1000);
  check("W2 valid after report", (int32_t)input.isValid(), 1);
  MotorOutput o1 = input.isValid() ? mixer.calculate(st, MAX) : MotorOutput{0, 0};
  check("W2 motors move", o1.left, MAX);

  // W3 — Dentro del timeout (200 ms) el input sigue válido
  check("W3 within timeout", (int32_t)input.checkValid(1100, 200), 1);

  // W4 — Timeout superado -> input inválido (motores a cero, comando NO reaplicado)
  check("W4 timeout invalidates", (int32_t)input.checkValid(2000, 200), 0);
  check("W4 invalid flag", (int32_t)input.isValid(), 0);
  MotorOutput o2 = input.isValid() ? mixer.calculate(st, MAX) : MotorOutput{0, 0};
  check("W4 motors stopped", o2.left, 0);

  // W5 — El último comando NO se reaplica mientras siga inválido
  MotorOutput o3 = input.isValid() ? mixer.calculate(st, MAX) : MotorOutput{0, 0};
  check("W5 command not reapplied", o3.left, 0);

  // W6 — invalidate() explícito (desconexión/fallo de seguridad)
  input.markValid(3000);
  input.invalidate();
  check("W6 invalidated", (int32_t)input.isValid(), 0);

  // W7 — Nuevo reporte válido -> recuperación del control
  input.markValid(4000);
  check("W7 recovered", (int32_t)input.isValid(), 1);
  MotorOutput o4 = input.isValid() ? mixer.calculate(st, MAX) : MotorOutput{0, 0};
  check("W7 motors move again", o4.left, MAX);
}

int main() {
  GamepadParser parser;
  GamepadMixer mixer;
  const int16_t MAX = 700; // configuredSpeed = 700

  testParser(parser, mixer, MAX);
  testFilter();
  testWatchdog(mixer, MAX);

  printf(failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d FAILURES\n", failures);
  return failures;
}
