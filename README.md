# SumoAndinoA — Robot Mini-Sumo con control dual BLE

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Platform](https://img.shields.io/badge/Platform-ESP32--C3-orange.svg)](https://www.espressif.com/en/products/socs/esp32)
[![Nivel](https://img.shields.io/badge/Nivel-Principiante%20%2F%20Educativo-brightgreen.svg)](#)

**SumoAndinoA** es un firmware de control para un robot **Mini-Sumo** construido alrededor de un **ESP32-C3** y un driver de motores **TB6612FNG**. El robot puede controlarse de dos maneras:

1. **Desde un teléfono Android** mediante Bluetooth Low Energy (BLE).
2. **Desde un mando Bluetooth** con dos joysticks (estilo Xbox/PlayStation), con **control proporcional de velocidad**.

El código está organizado en módulos pequeños y documentados, pensado para que sea fácil de leer, modificar y ampliar. Es una base sólida para estudiantes, aficionados y entusiastas que se inician en la robótica de competición.

---

## Características

*   **Control inalámbrico dual BLE**
    *   Teléfono Android mediante el protocolo de texto del firmware.
    *   Mando Bluetooth (HID) con joysticks y control proporcional de velocidad.
*   **Control proporcional con mezcla diferencial**
    *   El stick izquierdo (eje Y) controla avance/retroceso.
    *   El stick derecho (eje X) controla los giros.
    *   La velocidad aumenta progresivamente según cuánto se desplace el joystick (respuesta lineal con deadzone configurable).
*   **Velocidad máxima única (`configuredSpeed`)**
    *   La configura el teléfono y el mando la reutiliza automáticamente; no existe una segunda configuración independiente.
*   **Prioridad automática de fuente de control**
    *   Con el mando conectado, el mando tiene prioridad.
    *   Al desconectarse el mando, el teléfono recupera el control.
*   **Seguridad integrada (anti-escape)**
    *   Parada inmediata de los motores ante cualquier desconexión.
    *   Watchdog del teléfono: 1,5 s sin comandos → parada preventiva.
    *   Watchdog del mando: 200 ms sin reports HID → parada preventiva.
*   **Indicador LED de estado**
    *   Parpadeo rápido: buscando conexión.
    *   Parpadeo lento: conectado (teléfono o mando).
*   **Ligero y orientado al ESP32-C3**
    *   Sin librerías pesadas de gamepads; buffers estáticos y aritmética entera para minimizar el consumo de RAM y flash.

---

## Requisitos de hardware

| Componente | Descripción |
| :--- | :--- |
| Microcontrolador | ESP32-C3 (solo BLE, no dispone de Bluetooth Classic) |
| Driver de motores | TB6612FNG (puente H dual) |
| Motores | 2 motores DC con caja reductora |
| Alimentación | Batería para el driver y 3,3 V/5 V para el ESP32 |
| Control remoto | Teléfono Android con app BLE y/o mando Bluetooth compatible con HID |

> **Nota sobre el mando:** el firmware incluye el parser del formato HID de un **Xbox Wireless Controller** (Report ID `0x01`). Para usar otro modelo (p. ej. DualShock 4), basta con ajustar las constantes al inicio de `GamepadParser.cpp`; la arquitectura permite añadir parsers específicos sin tocar el resto del firmware.

---

## Conexiones del hardware (pinout)

Conecta el **ESP32-C3** al **TB6612FNG** según la siguiente tabla:

| Pin ESP32 | Etiqueta en código | Función en el TB6612FNG | Descripción |
| :---: | :--- | :--- | :--- |
| **8** | `PIN_LED` | — | LED de estado (conexión) |
| **0** | `PIN_ENA` | **PWMA** | Velocidad del motor izquierdo (PWM) |
| **1** | `PIN_IN1` | **AIN1** | Sentido de giro del motor izquierdo |
| **3** | `PIN_IN2` | **AIN2** | Sentido de giro del motor izquierdo |
| **4** | `PIN_ENB` | **PWMB** | Velocidad del motor derecho (PWM) |
| **5** | `PIN_IN3` | **BIN1** | Sentido de giro del motor derecho |
| **6** | `PIN_IN4` | **BIN2** | Sentido de giro del motor derecho |
| **7** | `PIN_STBY` | **STBY** | Habilitación general del driver (standby) |

El PWM se configura a **5 kHz con resolución de 10 bits**, por lo que las velocidades se expresan en el rango **0 – 1023**.

---

## Control desde el teléfono (protocolo BLE)

El robot se anuncia como **`SumoAndinoA`** (nombre configurable en `Sumo_AndinoA.ino`). La aplicación envía cadenas de texto por la característica de escritura del servicio BLE:

| UUID | Descripción |
| :--- | :--- |
| Servicio | `D5C4A74E-B869-4744-90D8-37BB68B6ABBC` |
| Característica RX (app → ESP32) | `5ED81982-1610-4A5D-979B-98E58EF12D31` — escritura de comandos |
| Característica TX (ESP32 → app) | `D2904D82-9FBF-4BD1-86FB-84D2C89E40A0` — notificaciones |

### Comandos de movimiento

Formato general: `DIRECCIÓN,VEL_IZQ,VEL_DER` (velocidades de `0` a `1023`):

| Comando | Descripción |
| :--- | :--- |
| `F,1023,1023` | Avanza a máxima velocidad |
| `B,800,800` | Retrocede a velocidad controlada |
| `L` | Gira sobre su propio eje a la izquierda |
| `R` | Gira sobre su propio eje a la derecha |
| `S` | Frena en seco |

> La velocidad configurada por el teléfono (p. ej. `F,700,700`) queda registrada como **`configuredSpeed`** y es la misma que utiliza el mando como límite máximo.

---

## Control con mando Bluetooth

El firmware actúa como **cliente BLE (central)**: escanea, detecta el mando, se conecta, se suscribe a los *reports* HID y los procesa en tiempo real.

### Mapa de control

| Control | Acción |
| :--- | :--- |
| **Stick izquierdo, eje Y** | Avance (arriba) / retroceso (abajo) / detenerse (centro) |
| **Stick derecho, eje X** | Giro a la derecha / giro a la izquierda / sin giro (centro) |

### Velocidad proporcional

La velocidad no es un simple interruptor de dirección: crece de forma proporcional al desplazamiento del joystick y nunca supera `configuredSpeed`.

Con `configuredSpeed = 700`:

| Posición del stick | Velocidad resultante |
| :---: | :---: |
| 0 % (centro) | 0 |
| 25 % | 175 |
| 50 % | 350 |
| 75 % | 525 |
| 100 % | 700 |

### Mezcla diferencial

El firmware combina ambas señales por motor:

```
leftMotor  = forward + turn
rightMotor = forward − turn
```

Los resultados se limitan al rango `±configuredSpeed`. Esto permite avance con giro (motores a velocidades distintas) y giro sobre el sitio (motores en sentidos opuestos).

### Seguridad

*   Si el mando se desconecta, los motores se detienen de inmediato.
*   Si no llega ningún *report* HID durante **200 ms** (`GAMEPAD_TIMEOUT_MS`), los motores se detienen por prevención.

### Configuración del mando objetivo

| Parámetro | Ubicación | Valor por defecto |
| :--- | :--- | :--- |
| Filtro de nombre del mando | `GamepadController.h` → `GAMEPAD_NAME_FILTER` | `"Xbox"` (vacío = cualquier dispositivo HID) |
| Formato del HID report | `GamepadParser.cpp` (offsets, centro, rango y polaridad de los ejes) | Xbox Wireless Controller, Report ID `0x01` |
| Deadzone | `GamepadParser.cpp` → `GAMEPAD_DEADZONE_PERCENT` | 10 % |
| Timeout de seguridad | `GamepadController.h` → `GAMEPAD_TIMEOUT_MS` | 200 ms |
| Debug por Serial | `Sumo_AndinoA.ino` → `GAMEPAD_DEBUG` | 1 (activo) |

Con `GAMEPAD_DEBUG = 1` el monitor serie (115200 baudios) muestra el estado del mando para verificar el parser y la mezcla:

```
[PAD] RAW LY=0 RX=255 | NLY=1000 NRX=1000 | L=700 R=-700
```

---

## Estructura del software

| Archivo | Responsabilidad |
| :--- | :--- |
| `Sumo_AndinoA.ino` | Orquestación: prioridad de fuente (mando/teléfono), bucle de control del mando y parser de comandos Android |
| `BleManager.h` / `.cpp` | Servidor BLE GATT que atiende al teléfono |
| `GamepadController.h` / `.cpp` | Cliente BLE central: escaneo, conexión, suscripción a reports HID y reconexión |
| `GamepadParser.h` / `.cpp` | Convierte el HID report en un `GamepadState` normalizado (deadzone, polaridad, curva de respuesta) |
| `GamepadMixer.h` / `.cpp` | Mezcla diferencial: estado del mando → velocidades por motor |
| `MotorController.h` / `.cpp` | Control de PWM y sentido de giro del TB6612 |
| `SafetyManager.h` | Watchdog de seguridad del teléfono |
| `StatusLed.h` / `.cpp` | Indicador LED de estado |

---

## Cómo compilar y cargar el firmware

### Con Arduino IDE

1.  **Instala el Arduino IDE** desde [arduino.cc](https://www.arduino.cc/en/software).
2.  **Añade el soporte para ESP32:**
    *   En *Archivo → Preferencias*, en "Gestor de URLs Adicionales de Tarjetas", añade:
        `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
    *   En *Herramientas → Placa → Gestor de Tarjetas*, busca `esp32` e instala la última versión (se requiere **v3.0.0 o superior**).
3.  **Selecciona la placa:**
    *   *Herramientas → Placa → esp32 → ESP32C3 Dev Module* (o la placa concreta que uses).
4.  **Conecta y sube:**
    *   Conecta el ESP32-C3 por USB y selecciona el puerto en *Herramientas → Puerto*.
    *   Abre `Sumo_AndinoA.ino` y pulsa **Subir**.
    *   Abre el Monitor Serie a **115200 baudios** para ver la actividad del robot.

### Con arduino-cli (build reproducible)

Para fijar la versión exacta del core ESP32 (evita que un cambio futuro de la
librería BLE rompa el build sin aviso):

```bash
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.10   # versión con la que se validó este proyecto
arduino-cli compile --fqbn esp32:esp32:esp32c3 Sumo_AndinoA
arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32c3 Sumo_AndinoA
```

O con el `Makefile` del repositorio: `make compile`, `make upload` (ajusta
`CONFIG_FILE` si tu `arduino-cli.yaml` está en otra ruta).

### Pruebas nativas (sin hardware)

`GamepadParser` y `GamepadMixer` son funciones puras y se prueban en el host
antes de tocar el robot:

```bash
make test
```

Ejecuta los 10 casos del contrato (sticks centrados, avance/retroceso 100 %,
giros sobre el sitio, avance + giro, deadzone, clamping y rechazo de reports
inválidos) y debe terminar con `ALL TESTS PASSED`.

---

## Licencia

Este proyecto es de código abierto y se distribuye bajo la **Apache License 2.0**. Puedes usarlo, estudiarlo, modificarlo y compartirlo libremente en proyectos escolares, personales o de competición.

---

*Desarrollado y mantenido por **Anderson Andino** (Copyright © 2026).*
