// Stub mínimo de Arduino.h para compilar los tests nativos en el host.
// Solo cubre lo que usan GamepadParser.h / GamepadMixer.cpp.
#pragma once
#include <stdint.h>
#include <stddef.h>

// El constrain real de Arduino es una macro: acepta tipos mezclados.
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

template <typename T> T max(T a, T b) { return a > b ? a : b; }
template <typename T> T min(T a, T b) { return a < b ? a : b; }
