# 🤖 SumoAndinoA — Robot Mini-Sumo BLE para Principiantes

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Platform](https://img.shields.io/badge/Platform-ESP32-orange.svg)](https://www.espressif.com/en/products/socs/esp32)
[![Nivel](https://img.shields.io/badge/Nivel-Principiante%20%2F%20Educativo-brightgreen.svg)](#)

**SumoAndinoA** es un firmware de control diseñado específicamente para un robot **Mini-Sumo de nivel principiante y educativo**. Utiliza la potencia del microcontrolador **ESP32** para ofrecer un sistema de control inalámbrico por Bluetooth (BLE) que es robusto, seguro y, sobre todo, fácil de entender y calibrar.

Este proyecto es ideal para estudiantes, aficionados y entusiastas que se están iniciando en la robótica de competición. Sirve como una base sólida para aprender sobre control de motores, comunicación inalámbrica, seguridad en sistemas embebidos y persistencia de datos.

---

## 🌟 Características Principales

*   **🎓 Diseñado para el Aprendizaje:** Código limpio, modular y documentado, ideal para quienes están dando sus primeros pasos en programación de robots de competencia.
*   **📱 Control Inalámbrico BLE:** Permite controlar el robot en tiempo real mediante cualquier aplicación móvil compatible con Bluetooth Low Energy (BLE). El robot se anuncia en la red como **"SumoAndinoA"** (nombre personalizable en el código).
*   **🛡️ Seguridad Integrada (Anti-Escape):**
    *   **Freno por Desconexión:** Si el teléfono se desconecta, el robot frena al instante en microsegundos.
    *   **Watchdog de Seguridad:** Si dejas de enviar comandos durante **1.5 segundos**, el robot se detendrá de manera preventiva para evitar que choque o escape del Tatami (Dojo).
*   **💡 Indicador de Estado visual:** Usa un solo LED para saber si el robot está listo para competir:
    *   *Parpadeo rápido:* Buscando conexión Bluetooth.
    *   *Parpadeo lento:* Conectado y listo para recibir comandos.

---

## 🎛️ Conexiones del Hardware (Pinout)

Para armar tu **SumoAndinoA**, conecta tu placa **ESP32** al controlador de motores dual **TB6612FNG** utilizando el siguiente esquema simplificado:

| Pin ESP32 | Etiqueta en Código | Función en Driver TB6612FNG | ¿Qué hace? |
| :---: | :--- | :--- | :--- |
| **8** | `PIN_LED` | LED de Estado | Muestra el estado del Bluetooth |
| **0** | `PIN_ENA` | **PWMA** | Control de velocidad del Motor Izquierdo |
| **1** | `PIN_IN1` | **AIN1** | Sentido de giro Motor Izquierdo |
| **3** | `PIN_IN2` | **AIN2** | Sentido de giro Motor Izquierdo |
| **4** | `PIN_ENB` | **PWMB** | Control de velocidad del Motor Derecho |
| **5** | `PIN_IN3` | **BIN1** | Sentido de giro Motor Derecho |
| **6** | `PIN_IN4` | **BIN2** | Sentido de giro Motor Derecho |
| **7** | `PIN_STBY` | **STBY** | Interruptor de seguridad general (Standby) |

---

## 📡 Protocolo de Control (Comandos BLE)

La aplicación móvil se comunica con **SumoAndinoA** enviando cadenas de texto simples (strings) a través de Bluetooth. 

### 1. Comandos de Movimiento
Envía comandos con el formato: `DIRECCIÓN,VEL_IZQ,VEL_DER` (Rango de velocidad: `0` a `1023`):

*   **Adelante:** `F,1023,1023` (Avanza a máxima velocidad).
*   **Atrás:** `B,800,800` (Retrocede a velocidad controlada).
*   **Giro Izquierda:** `L` (Gira sobre su propio eje a la izquierda).
*   **Giro Derecha:** `R` (Gira sobre su propio eje a la derecha).
*   **Detener:** `S` (Frena en seco).

---

## 📂 Estructura del Software

El código está dividido en pequeños archivos para que sea fácil de leer y modificar:

```text
Sumo_AndinoA/
├── Sumo_AndinoA.ino      # Archivo principal de Arduino (Arranque y lectura de comandos)
├── BleManager.h / .cpp   # Controla la antena Bluetooth del ESP32
├── MotorController.h/.cpp# Controla la velocidad y el sentido de los motores
├── SafetyManager.h       # Evita que el robot se escape si se pierde la señal
└── StatusLed.h / .cpp    # Controla el parpadeo del LED de estado
```

---

## 🛠️ ¿Cómo compilar y cargar el código?

1.  **Descarga el Arduino IDE:** Instala la última versión de [Arduino IDE](https://www.arduino.cc/en/software).
2.  **Instala el soporte para ESP32:**
    *   En Arduino IDE, ve a *Archivo > Preferencias*.
    *   En "Gestor de URLs Adicionales de Tarjetas", pega la URL de Espressif:
        `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
    *   Ve a *Herramientas > Placa > Gestor de Tarjetas*, busca `esp32` e instala la versión más reciente (se requiere **v3.0.0 o superior**).
3.  **Configura tu placa:**
    *   Selecciona tu placa ESP32 en *Herramientas > Placa > esp32 > ESP32 Dev Module* (o la tarjeta correspondiente).
    *   Conecta el ESP32 a la computadora con un cable USB de datos y selecciona el puerto COM correcto en *Herramientas > Puerto*.
4.  **Sube el programa:**
    *   Abre el archivo `Sumo_AndinoA.ino`.
    *   Haz clic en el botón de la flecha (**Subir**) y ¡listo! Puedes abrir el Monitor Serie a `115200` baudios para ver la actividad del robot.

---

## ⚖️ Licencia

Este proyecto es de código abierto y está bajo la licencia **Apache License 2.0**. Puedes usarlo, estudiarlo, modificarlo y compartirlo libremente para tus proyectos escolares o de competencia.

---

*Diseñado para inspirar a la próxima generación de ingenieros en robótica.*
*Desarrollado y mantenido por **Anderson Andino** (Copyright © 2026).*
