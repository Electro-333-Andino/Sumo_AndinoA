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

*   **Control inalámbrico dual BLE (modos mutuamente excluyentes)**
    *   **Modo App:** teléfono Android mediante el protocolo de texto del firmware.
    *   **Modo Xbox:** mando Bluetooth (HID) con joysticks y control proporcional de velocidad.
    *   Solo se inicializa el stack BLE del modo activo (ahorra RAM y evita conflictos).
*   **Control proporcional con mezcla diferencial**
    *   El stick izquierdo (eje Y) controla avance/retroceso.
    *   El stick derecho (eje X) controla los giros.
    *   La velocidad aumenta progresivamente según cuánto se desplace el joystick (respuesta lineal con deadzone configurable).
*   **Velocidad máxima única (`configuredSpeed`)**
    *   La configura el teléfono y el mando la reutiliza automáticamente; no existe una segunda configuración independiente.
*   **Selección de modo con el botón BOOT (fail-safe físico)**
    *   El único mecanismo para cambiar de modo es el botón interno **BOOT** (GPIO 9):
        mantenerlo presionado 3 segundos invierte el modo y reinicia.
    *   El modo activo se guarda en NVS (Preferences, namespace `sumo`) y el LED
        indica el modo al arrancar (1 parpadeo lento = App, 2 rápidos = Xbox).
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

**Mando soportado:** Xbox Wireless Controller Model 1708 (Xbox One S)

| Parámetro | Valor |
| :--- | :--- |
| Protocolo | Bluetooth Low Energy (BLE) |
| ESP32 | ESP32-C3 |
| HID Service | `0x1812` |
| HID Report | `0x2A4D` |
| Ejes | uint16 LE, 0..65535 (centro 32768) |

El firmware actúa como **cliente BLE (central)**: escanea, detecta el mando, se
conecta, realiza el pairing (Secure Connections + Bonding, Just Works) y se
suscribe a las notificaciones del HID Report. El layout exacto del reporte
(offsets de ejes, triggers, D-Pad y botones) está documentado en
`GamepadParser.cpp` y soporta las variantes reales del 1708 (15 y 16 bytes,
con y sin Report ID).

### Procedimiento de conexión

```text
1. Encender el robot (Modo Xbox).
2. Encender el mando con el botón Xbox.
3. Mantener pulsado el botón Pair del mando hasta que el LED parpadee.
4. El ESP32 escanea y encuentra "Xbox Wireless Controller".
5. Se realiza la conexión BLE y el pairing (automático, sin PIN).
6. Se suscriben las notificaciones HID.
7. Los controles quedan activos (Serial: "[GAMEPAD] Notification enabled").
```

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

*   Si el mando se desconecta o deja de transmitir, el estado pasa a
    **desconectado**: los motores se detienen de inmediato y el robot
    permanece detenido (no se reutiliza el último comando) hasta que llega un
    nuevo reporte HID válido.
*   Timeout sin reporte **válido**: **200 ms** (`GAMEPAD_TIMEOUT_MS`) → STOP +
    desconexión + reconexión automática.
*   Un reporte inválido **no** alimenta el watchdog ni mueve el robot.

### Configuración del mando objetivo

| Parámetro | Ubicación | Valor por defecto |
| :--- | :--- | :--- |
| Filtro de nombre del mando | `GamepadController.h` → `GAMEPAD_NAME_FILTER` | `"Xbox"` (vacío = cualquier dispositivo HID) |
| Formato del HID report | `GamepadParser.cpp` (offsets, centro, rango y polaridad) | Xbox 1708, ejes u16 LE, 15/16 bytes |
| Deadzone | `GamepadParser.cpp` → `GAMEPAD_DEADZONE_PERCENT` | 10 % |
| Timeout de seguridad | `GamepadController.h` → `GAMEPAD_TIMEOUT_MS` | 200 ms |
| Debug por Serial | `Sumo_AndinoA.ino` → `GAMEPAD_DEBUG` | 1 (activo) |
| Dump de reports HID | `GamepadController.h` → `DEBUG_GAMEPAD_REPORTS` | 0 (desactivado) |

Con `GAMEPAD_DEBUG = 1` el monitor serie (115200 baudios) muestra el estado del mando para verificar el parser y la mezcla:

```
[PAD] RAW LY=0 RX=65535 | NLY=1000 NRX=1000 | L=700 R=-700
```

Con `DEBUG_GAMEPAD_REPORTS = 1` se vuelca además la longitud, los bytes en hex y los sticks decodificados (throttled a ~100 ms), útil para verificar el mando físico:

```
[GAMEPAD] len=16
[GAMEPAD] data: 00 80 00 80 00 80 00 80 ...
[GAMEPAD] LX=32768 LY=32768 RX=32768 RY=32768
[GAMEPAD] normalized LX=0 LY=0 RX=0 RY=0
```

---

## Modos de operación

El robot arranca en uno de dos modos **mutuamente excluyentes**, persistidos en
NVS (`Preferences`, namespace `sumo`, clave `opMode`). Solo se inicializa el
stack BLE del modo activo, lo que reduce el consumo de RAM y evita conflictos.

| Modo | `opMode` | Fuente de control | Cómo se activa |
| :--- | :---: | :--- | :--- |
| **App** | `0` (por defecto) | Teléfono Android (BLE GATT) | BOOT 3 s estando en Modo Xbox |
| **Xbox** | `1` | Mando Bluetooth (BLE HID) | BOOT 3 s estando en Modo App |

El cambio de modo se realiza **exclusivamente** con el botón interno **BOOT**
(GPIO 9) mantenido 3 segundos: invierte `opMode` en NVS y reinicia. No se
utilizan comandos de la app ni botones del mando para cambiar de modo.

> **Nota sobre Bluepad32:** esta librería requiere Bluetooth *Classic*
> (disponible solo en ESP32 clásico). El ESP32-C3 únicamente dispone de BLE,
> por lo que el Modo Xbox usa el controlador BLE HID ligero del proyecto
> (`GamepadController`), con la misma funcionalidad y sin fugas de heap.

### Seguridad de la conexión (pairing)

El perfil HID over GATT exige conexión encriptada y vinculada (bonding). El
firmware configura el pairing automático **Secure Connections + Bonding** con
capacidad `NoInputNoOutput` (Just Works, sin confirmación manual): el mando y
el ESP32 se vinculan solos al conectar, sin interacción del usuario.

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
