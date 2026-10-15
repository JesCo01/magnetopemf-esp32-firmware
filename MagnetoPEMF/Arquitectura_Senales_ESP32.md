# Arquitectura de Generación de Señal en ESP32 para Magnetoterapia

Este documento define la base técnica y la selección de periféricos del microcontrolador ESP32 para la generación de señales de Campo Electromagnético Pulsado (PEMF). La decisión arquitectónica no se basa en la selección arbitraria de un pin, sino en la asignación del periférico de hardware nativo más adecuado para la naturaleza temporal de cada protocolo clínico.

## 1. Naturaleza de la Señal y Selección de Periféricos

El ESP32 dispone de múltiples coprocesadores de hardware para la generación de señales. La correspondencia entre el requerimiento clínico y el hardware del ESP32 se define de la siguiente manera:

### 1.1. Señales Rectangulares y Cuadradas Simples (Protocolo IGEA)
Para protocolos basados en ondas cuadradas o rectangulares continuas (ej. **75 Hz con pulso activo de 1.3 ms** o **15 Hz al 50%**), el periférico nativo adecuado es el **LEDC (LED Control)**.
* **Justificación:** LEDC es el generador PWM de propósito general del ESP32. Su arquitectura permite fijar una frecuencia base y un *Duty Cycle* (ciclo de trabajo) que el hardware mantendrá indefinidamente sin intervención de la CPU.
* **Ejemplo 75 Hz (1.3 ms ON):** El período total es de 13.333 ms. Un tiempo activo de 1.3 ms equivale a un *duty cycle* de ~9.75%.

### 1.2. Ráfagas (Bursts) con Micropulsos Exactos (Protocolo Orthofix / EBI)
Para protocolos complejos como un **burst de 5 ms compuesto por micropulsos de 225 µs, repitiéndose a 15 Hz**, el PWM tradicional (LEDC) es insuficiente. La arquitectura requiere la combinación de dos periféricos: **Hardware Timer (GPTimer)** y **RMT (Remote Control Transceiver)**.
* **Justificación RMT:** El periférico RMT está diseñado para emitir secuencias arbitrarias de pulsos y espacios con resoluciones de microsegundos (ej. 1 MHz = 1 µs por *tick*). Esto permite "dibujar" exactamente el patrón asimétrico dentro de los 5 ms.
* **Justificación Timer (El Límite de 15 bits):** El RMT codifica duraciones en 15 bits (máximo 32,767 *ticks*). A 1 MHz, no puede representar la pausa larga de ~61.6 ms entre ráfagas de 15 Hz. Por ende, un **Timer de Hardware puro a 15 Hz** actúa como director de orquesta: interrumpe a la CPU cada 66.6 ms, dispara la transmisión de la ráfaga RMT de 5 ms, y al finalizar, el hardware vuelve a estado de reposo (LOW) automáticamente.

### 1.3. Ondas Analógicas (Triangulares / Trapezoidales Puras)
Si el requerimiento físico en el pin (antes de la bobina) exige una rampa analógica pura (onda triangular de voltaje), la salida digital no es factible. En este escenario, el recurso nativo es el **DAC (Digital to Analog Converter)**, limitado exclusivamente a los pines **GPIO25 y GPIO26**, o en su defecto, una señal LEDC filtrada activamente mediante electrónica pasa-bajos. 

*Nota: En magnetoterapia inductiva, un pulso PWM rectangular de bajo duty cycle aplicado a un inductor (bobina) genera inherentemente una rampa de corriente triangular debido a la reactancia inductiva del circuito.*

---

## 2. Resolución temporal y compatibilidad entre variantes

Los bits de la CPU, del ADC y del PWM describen recursos distintos. En el ESP32 clásico, LEDC admite hasta 20 bits y 16 canales; ESP32-C3 admite hasta 14 bits y 6 canales, y ESP32-S3 hasta 14 bits y 8 canales. La resolución alcanzable depende también de la frecuencia y del reloj configurados. Fuente: [LEDC, documentación oficial de Espressif](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/ledc.html).

El código utiliza **16 bits** para el modo rectangular (`PWM_RESOLUTION` en `Config.h`). Migrar a C3 o S3 requiere modificar esa configuración y el cálculo del duty, además de revisar el ADC y los recursos de temporización. La comparación de placas se encuentra en 2.3.6.1 de la tesis.

Para ilustrar la granularidad temporal, a 15 Hz el período es aproximadamente 66,667 ms y el paso ideal corresponde a $T/2^n$:

| Resolución PWM | Pasos por período | Paso temporal ideal a 15 Hz |
|---|---:|---:|
| 12 bits | 4096 | 16,28 µs |
| 14 bits | 16 384 | 4,07 µs |
| 16 bits | 65 536 | 1,02 µs |

Estos valores describen la cuantización del tiempo de encendido. La exactitud temporal, los flancos y la respuesta de la etapa de potencia se comprueban mediante medición; los 16 bits se conservan por correspondencia con la implementación actual.

---

## 3. Matriz GPIO y Asignación de Puertos

Esta asignación corresponde a la **MH-ET LIVE ESP32 MiniKit basada en el ESP32 clásico**. Su matriz GPIO permite llevar LEDC y RMT a distintos terminales de salida; se seleccionan según disponibilidad, funciones de arranque y conexiones del módulo. No debe trasladarse este mapa directamente a un ESP32-C3 o a una placa Arduino con otro microcontrolador.

### 3.1. Pines Críticos (NO RECOMENDADOS para Salida de Señal)
* **GPIO 6 al 11:** Ocupados por la memoria flash del módulo; no se asignan a conexiones externas.
* **GPIO 34 al 39:** Son pines de *Entrada Solamente*. Carecen de hardware de salida.
* **GPIO 0, 2, 5, 12, 15 (Strapping Pins):** Son leídos por el hardware durante los primeros milisegundos de encendido para determinar el modo de arranque (Flash, UART Download, Voltaje, etc.). Conectar cargas externas de baja impedancia a estos pines puede causar fallos de inicio severos.
* **GPIO 1 y 3:** Reservados por defecto para UART0 (Monitor Serial / Flasheo).

### 3.2. Asignación utilizada

| Interfaz | Recurso y terminal | Evidencia en el proyecto |
|---|---|---|
| Mando de potencia | GPIO4; LEDC o RMT según el modo | `Config.h` selecciona `GPIO_NUM_4`. |
| Medición del banco | GPIO34, ADC1_CH6 | `BatteryMonitor.cpp` configura `ADC1_CHANNEL_6`; divisor y calibración descritos en 2.3.6 de la tesis. |
| Programación y diagnóstico | UART0: GPIO1 TX y GPIO3 RX, mediante USB–serie | Terminales TX/RX definidos por la variante de la MiniKit. |
| Enlace con la aplicación | Radio BLE integrada | `BLEManager`; no requiere GPIO externos para transmitir o recibir. |

GPIO4 admite salida digital y no es un pin de configuración de arranque en este ESP32. GPIO34 es exclusivamente de entrada. La señal de GPIO4 se conecta a la entrada de mando de la etapa de potencia; la capacidad de accionamiento del MOSFET se verifica en esa etapa.

Otras salidas posibles, si están libres y expuestas en la placa, son GPIO18, 19, 21, 22, 23, 25, 26, 27, 32 y 33. GPIO22 queda como alternativa, no como conexión seleccionada.

La variante `mhetesp32minikit` también define I²C (SDA21/SCL22) y SPI (SCK18/MISO19/MOSI23/SS5). Estas interfaces no se utilizan en el diseño actual. Si se incorporan, deben reservarse sus terminales y considerar que GPIO5 interviene en el arranque.

Fuentes: [GPIO del ESP32, Espressif](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/gpio.html) y [definición oficial de pines de la MiniKit](https://github.com/espressif/arduino-esp32/blob/master/variants/mhetesp32minikit/pins_arduino.h). La comparación y justificación de placas se desarrolla en [SECCIÓN 2.3.md, apartado 2.3.6](../../../AVANCE%20TESIS/SECCI%C3%93N%202.3.md).

---

## 4. Resumen de Implementación de la Arquitectura

**Especificación a Implementar:** Ráfagas de 15 Hz con *burst* activo de 5 ms compuesto por micropulsos (Protocolo Tipo Orthofix).

**Esquema de Hardware Integrado:**
1. **Temporizador Macro (Hardware Timer):** Interrumpe exactamente a 15 Hz (cada 66.666 ms).
2. **Generador Micro (RMT):** Configurado a 1 MHz (resolución 1 µs/tick). Contiene en memoria la plantilla digital del *burst* de 5 ms.
3. **Flujo de Ejecución:** El Timer dispara la interrupción; la interrupción ordena al RMT transmitir la plantilla una sola vez por el **GPIO4**; el RMT retorna el pin a estado `LOW` al finalizar.
4. **Validación (Fase 1):** Verificación de la señal lógica en osciloscopio desde GPIO4 antes de acoplar la etapa de potencia inductiva.
