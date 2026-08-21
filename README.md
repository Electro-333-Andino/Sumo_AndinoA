# SumoAndinoA — Robot Mini-Sumo con control dual por BLE

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Platform](https://img.shields.io/badge/Platform-ESP32--C3-orange.svg)](https://www.espressif.com/en/products/socs/esp32)
[![BLE](https://img.shields.io/badge/BLE-NimBLE--Arduino-green.svg)](https://github.com/h2zero/NimBLE-Arduino)

**SumoAndinoA** es el firmware de un robot **Mini-Sumo** construido con un
**ESP32-C3** y un driver de motores **TB6612FNG**. El robot se controla de forma
inalámbrica por Bluetooth Low Energy (BLE) de dos maneras, una a la vez:

1. **Modo App** — desde un teléfono Android con una app BLE.
2. **Modo Xbox** — desde un mando **Xbox Wireless Controller** (modelo 1708).

Los dos modos conviven en el mismo firmware y se eligen con un único botón
físico: el botón **BOOT** del propio ESP32-C3. No se necesitan botones externos
ni comandos especiales para cambiar de modo.

El código está organizado en módulos pequeños, documentados en español y
diseñados para que cualquier persona pueda leerlos, modificarlos y ampliarlos
sin romper el resto del sistema.

---

## Características

*   **Control dual BLE con modos excluyentes**
    *   Solo se inicializa el stack BLE del modo activo: menos RAM y cero conflictos.
    *   El modo elegido se guarda en memoria (NVS) y se mantiene al apagar y encender.
*   **Control proporcional con mezcla diferencial**
    *   La velocidad crece suavemente según cuánto muevas el joystick (respuesta lineal con deadzone).
*   **Seguridad integrada (anti-escape)**
    *   Parada inmediata de los motores ante cualquier desconexión o pérdida de señal.
*   **Indicador LED de estado**
    *   Parpadeo rápido: buscando conexión. Parpadeo lento: conectado.
*   **Ligero y pensado para el ESP32-C3**
    *   Sin librerías pesadas de gamepads; buffers estáticos y aritmética entera.

---

## Hardware necesario

| Componente | Descripción |
| :--- | :--- |
| Microcontrolador | **ESP32-C3** (solo BLE; no tiene Bluetooth Classic) |
| Driver de motores | TB6612FNG (puente H dual) |
| Motores | 2 motores DC con reductora |
| Alimentación | Batería para los motores y 3,3 V / 5 V para el ESP32 |
| Control remoto | Teléfono Android con app BLE y/o mando Xbox Wireless Controller 1708 |

> **Mando soportado:** Xbox Wireless Controller **Model 1708** (Xbox One S),
> conectado por BLE usando el perfil HID. Otros mandos HID (p. ej. PlayStation)
> requieren ajustar el parser en `GamepadParser.cpp`; la arquitectura ya está
> preparada para ello.

---

## Conexiones del hardware (pinout)

Conecta el **ESP32-C3** al **TB6612FNG** tal y como indica la tabla. Estos pines
son fijos y no deben cambiarse sin recalibrar el robot:

| Pin ESP32-C3 | Etiqueta en código | Conexión en el TB6612FNG | Función |
| :---: | :--- | :--- | :--- |
| **8** | `PIN_LED` | — | LED de estado |
| **5** | `PIN_ENA` | **PWMA** | Velocidad del motor izquierdo (PWM) |
| **6** | `PIN_IN1` | **AIN1** | Sentido del motor izquierdo |
| **20** | `PIN_IN2` | **AIN2** | Sentido del motor izquierdo |
| **0** | `PIN_ENB` | **PWMB** | Velocidad del motor derecho (PWM) |
| **1** | `PIN_IN3` | **BIN1** | Sentido del motor derecho |
| **4** | `PIN_IN4` | **BIN2** | Sentido del motor derecho |
| **7** | `PIN_STBY` | **STBY** | Habilitación general del driver |
| **9** | `PIN_BOOT` | — | Botón interno del ESP32 (cambio de modo) |

El PWM se configura a **5 kHz con resolución de 10 bits**: las velocidades se
expresan en el rango **0 – 1023**.

---

## Modos de operación

El robot arranca siempre en uno de estos dos modos, guardados en NVS:

| Modo | Valor `opMode` | Fuente de control |
| :--- | :---: | :--- |
| **App** | `0` (por defecto) | Teléfono Android (servidor BLE) |
| **Xbox** | `1` | Mando Xbox 1708 (cliente BLE HID) |

### Cómo cambiar de modo

1. Mantén presionado el botón **BOOT** del ESP32-C3 durante **3 segundos**.
2. El modo se invierte, se guarda en memoria y el robot se reinicia.

Al arrancar, el LED indica el modo activo:

*   **1 parpadeo lento** → Modo App.
*   **2 parpadeos rápidos** → Modo Xbox.

---

## Modo App — control desde el teléfono

En este modo el robot actúa como un **servidor BLE** y se anuncia con el nombre
**`Robotini16`** (configurable en `Sumo_AndinoA.ino`). La app se conecta y envía
comandos de texto por la característica de escritura.

### Servicio y características

| Elemento | UUID |
| :--- | :--- |
| Servicio | `D5C4A74E-B869-4744-90D8-37BB68B6ABBC` |
| RX (app → ESP32) | `5ED81982-1610-4A5D-979B-98E58EF12D31` |
| TX (ESP32 → app) | `D2904D82-9FBF-4BD1-86FB-84D2C89E40A0` |

### Protocolo de comandos

Formato general: `DIRECCIÓN,VEL_IZQ,VEL_DER`, con velocidades de **0 a 1023**:

| Comando | Ejemplo | Descripción |
| :--- | :--- | :--- |
| `F` | `F,1023,1023` | Avanza a la velocidad indicada |
| `B` | `B,800,800` | Retrocede a la velocidad indicada |
| `L` | `L` o `L,500,500` | Gira a la izquierda (sin velocidades: 1023) |
| `R` | `R` o `R,500,500` | Gira a la derecha (sin velocidades: 1023) |
| `S` | `S` | Frena en seco |

> **Velocidad compartida:** el último comando del teléfono fija `configuredSpeed`,
> que el modo Xbox reutiliza como velocidad máxima del mando.

---

## Modo Xbox — control con el mando

En este modo el ESP32 actúa como **cliente BLE (central)**: escanea, detecta el
mando, se conecta, hace el emparejamiento (pairing automático, sin PIN) y se
suscribe a las notificaciones HID.

| Parámetro | Valor |
| :--- | :--- |
| Mando soportado | Xbox Wireless Controller Model 1708 |
| Protocolo | Bluetooth Low Energy (BLE) |
| HID Service | `0x1812` |
| HID Report | `0x2A4D` |
| Formato de ejes | uint16 little-endian, centro 32768 |

### Procedimiento de conexión

```text
1. Enciende el robot (debe estar en Modo Xbox).
2. Enciende el mando con el botón Xbox.
3. Mantén pulsado el botón Pair del mando hasta que el LED parpadee.
4. El ESP32 escanea y encuentra "Xbox Wireless Controller".
5. Se realiza la conexión BLE y el pairing (automático, sin PIN).
6. Se activan las notificaciones HID y el robot queda listo.
```

Si el mando se desconecta, el ESP32 lo detecta, detiene el robot y vuelve a
escaneando automáticamente para reconectarse.

### Mapa de control

| Control | Acción |
| :--- | :--- |
| **Stick izquierdo, eje Y** | Arriba = avanza · abajo = retrocede · centro = se detiene |
| **Stick derecho, eje X** | Derecha = gira a la derecha · izquierda = gira a la izquierda |

### Velocidad proporcional

La velocidad no es un interruptor: crece de forma proporcional a cuánto
desplaces el joystick y nunca supera `configuredSpeed`. Con
`configuredSpeed = 700`, por ejemplo:

| Posición del stick | Velocidad resultante |
| :---: | :---: |
| Centro (0 %) | 0 |
| 25 % | 175 |
| 50 % | 350 |
| 75 % | 525 |
| 100 % | 700 |

### Mezcla diferencial

El firmware combina ambas señales para cada motor:

```text
motorIzquierdo = avance + giro
motorDerecho   = avance − giro
```

El resultado se limita a `±configuredSpeed`. Esto permite avance con giro
(motores a velocidades distintas) y giro sobre el sitio (motores en sentidos
opuestos).

### Seguridad del mando

*   El mando reporta continuamente (≈ cada 10 ms). Si deja de transmitir
    durante **200 ms** (`GAMEPAD_TIMEOUT_MS`), el robot se **detiene de
    inmediato**, pasa a estado desconectado y no vuelve a usar el último
    comando hasta recibir un reporte nuevo y válido.
*   Un reporte corrupto o inválido **no** mantiene vivo al robot ni lo mueve.
*   En el arranque los motores están apagados: el robot no se mueve hasta que
    llega el primer reporte válido del mando.

---

## Seguridad integrada (ambos modos)

*   **Modo App:** si el teléfono no envía comandos durante **450 ms**
    (`COMMAND_TIMEOUT_MS`), parada preventiva.
*   **Modo Xbox:** si no llegan reportes válidos durante **200 ms**, parada
    preventiva y reconexión automática.
*   En cualquier desconexión BLE, los motores se frenan **al instante** desde el
    callback de desconexión (sin esperar al siguiente ciclo del programa).
*   El botón BOOT sigue funcionando en ambos modos: es el único mecanismo para
    cambiar de modo, incluso en plena competición.

---

## Estructura del software

| Archivo | Responsabilidad |
| :--- | :--- |
| `Sumo_AndinoA.ino` | Orquestación: modos, botón BOOT, comandos del teléfono y bucle de control del mando |
| `BleManager.h` / `.cpp` | Servidor BLE que atiende al teléfono Android |
| `GamepadController.h` / `.cpp` | Cliente BLE: escaneo, conexión, pairing, suscripción HID y reconexión |
| `GamepadParser.h` / `.cpp` | Convierte el reporte HID del mando en un estado normalizado (deadzone y polaridad) |
| `GamepadMixer.h` / `.cpp` | Mezcla diferencial: estado del mando → velocidad de cada motor |
| `MotorController.h` / `.cpp` | Control del PWM y del sentido de giro del TB6612 |
| `SafetyManager.h` | Watchdog de seguridad del teléfono |
| `StatusLed.h` / `.cpp` | Indicador LED de estado |

---

## Compilar y cargar el firmware

### Opción 1 — Arduino IDE (para empezar rápido)

1. Instala el [Arduino IDE](https://www.arduino.cc/en/software).
2. Añade el soporte ESP32:
   * *Archivo → Preferencias* → "Gestor de URLs Adicionales de Tarjetas":
     `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
   * *Herramientas → Placa → Gestor de Tarjetas* → busca `esp32` e instala
     **v3.0.0 o superior**.
3. Instala la librería **NimBLE-Arduino** desde el Gestor de Librerías
   (todo el firmware usa NimBLE).
4. Selecciona la placa **ESP32C3 Dev Module**, abre `Sumo_AndinoA.ino` y pulsa **Subir**.

### Opción 2 — arduino-cli (recomendada)

Instala la dependencia (una sola vez):

```bash
arduino-cli lib install NimBLE-Arduino
```

Compila y sube el firmware al ESP32-C3:

```bash
./bin/arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc -p /dev/ttyACM0 -u Sumo_AndinoA/
```

Abre el monitor serie a 115200 baudios para ver la actividad del robot:

```bash
./bin/arduino-cli monitor -p /dev/ttyACM0 --config baudrate=115200
```

### Pruebas nativas (sin hardware)

`GamepadParser` y `GamepadMixer` son funciones puras y se prueban en el
ordenador antes de tocar el robot:

```bash
make test
```

Ejecuta **13 casos** (centro, avance, retroceso, giros, deadzone, reportes de
15/16/17 bytes, botones y mezcla) y debe terminar con `ALL TESTS PASSED`.

---

## Parámetros configurables

| Parámetro | Ubicación | Valor por defecto |
| :--- | :--- | :--- |
| Nombre BLE del robot | `Sumo_AndinoA.ino` → `BleManager bluetooth(...)` | `Robotini16` |
| Watchdog del teléfono | `Sumo_AndinoA.ino` → `COMMAND_TIMEOUT_MS` | 450 ms |
| Filtro de nombre del mando | `GamepadController.h` → `GAMEPAD_NAME_FILTER` | `"Xbox Wireless Controller"` |
| Timeout de reports del mando | `GamepadController.h` → `GAMEPAD_TIMEOUT_MS` | 200 ms |
| Timeout de conexión BLE | `GamepadController.h` → `GAMEPAD_CONNECT_TIMEOUT_MS` | 2000 ms |
| Deadzone de los sticks | `GamepadParser.cpp` → `GAMEPAD_DEADZONE_PERCENT` | 10 % |
| Debug del mando (Serial) | `Sumo_AndinoA.ino` → `GAMEPAD_DEBUG` | 0 (apagado) |
| Dump de reports HID | `GamepadController.h` → `DEBUG_GAMEPAD_REPORTS` | 0 (apagado) |

Con `GAMEPAD_DEBUG = 1` el monitor serie muestra el estado decodificado del
mando, útil para verificar el parser y la mezcla:

```text
[PAD] RAW LY=0 RX=65535 | NLY=1000 NRX=1000 | L=700 R=-700
```

---

## Licencia

Proyecto de código abierto distribuido bajo la **Apache License 2.0**. Puedes
usarlo, estudiarlo, modificarlo y compartirlo libremente en proyectos escolares,
personales o de competición.

---

*Desarrollado y mantenido por **Anderson Andino** (Copyright © 2026).*
