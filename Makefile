# SumoAndinoA — tooling de build y pruebas
#
# Uso:
#   make test      -> compila y ejecuta los tests nativos de parser/mixer
#   make compile   -> compila el firmware con arduino-cli
#   make upload    -> sube el firmware al ESP32-C3 (requiere puerto USB)
#
# Variables configurables (ej.: make compile CONFIG_FILE=/ruta/arduino-cli.yaml)

ARDUINO_CLI ?= arduino-cli
CONFIG_FILE ?= arduino-cli.yaml
FQBN        ?= esp32:esp32:esp32c3
SKETCH      ?= Sumo_AndinoA
PORT        ?= $(shell ls /dev/ttyACM* 2>/dev/null | head -n 1)

CXX        ?= g++
CXXFLAGS   ?= -std=gnu++17 -Wall -Wextra -I. -Itests/stubs

.PHONY: all test compile upload clean

all: test

test: tests/test_gamepad
	./tests/test_gamepad

tests/test_gamepad: tests/test_gamepad.cpp GamepadParser.cpp GamepadMixer.cpp tests/stubs/Arduino.h
	$(CXX) $(CXXFLAGS) $^ -o $@

compile:
	$(ARDUINO_CLI) compile --config-file $(CONFIG_FILE) --fqbn $(FQBN) $(SKETCH)

upload: compile
	$(ARDUINO_CLI) upload --config-file $(CONFIG_FILE) -p $(PORT) --fqbn $(FQBN) $(SKETCH)

clean:
	rm -f tests/test_gamepad
