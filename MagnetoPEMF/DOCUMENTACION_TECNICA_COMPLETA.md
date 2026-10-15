> **Version de ensayo BLE actualizada:** consultar [BLE_IMPLEMENTACION.md](../../BLE_IMPLEMENTACION.md) para transporte version 1, vinculacion, limites y pruebas. Ese documento sustituye las especificaciones historicas de comunicacion y perfiles de este archivo.

# Documentacion tecnica completa - Magneto Pro / ESP32 PEMF

Fecha de revision: 2026-05-01

Este documento resume el proyecto actual Flutter y documenta en detalle la
carpeta `SP32/MagnetoPEMF`, donde esta la generacion real de senales PEMF.

Nota importante: este documento describe el funcionamiento tecnico del codigo.
No reemplaza una validacion clinica, electrica, termica ni regulatoria. Los
valores de campo magnetico en mT que aparecen en el firmware son referenciales:
el codigo no mide mT en lazo cerrado, por lo que se necesita calibracion externa
con magnetometro y pruebas de banco.

---

## 1. Resumen ejecutivo

El sistema esta dividido en dos partes:

- App Flutter: interfaz de usuario. Escanea BLE, se conecta al ESP32, envia
  comandos JSON y muestra estado, bateria, temporizador e historial.
- Firmware ESP32: recibe los comandos por BLE y genera la senal digital que va
  al driver de potencia y a la bobina. Esta es la parte critica.

Flujo general:

```text
Usuario
  -> App Flutter
  -> BLE GATT: JSON por caracteristica WRITE
  -> ESP32: CommandHandler
  -> MagnetoController
  -> Perifericos de hardware: LEDC / RMT / GPTimer / ADC / NVS
  -> GPIO4
  -> Driver de potencia o MOSFET
  -> Bobina
  -> Campo magnetico pulsado
```

La app no crea PWM ni campo magnetico. La app solo selecciona parametros y
envia ordenes. La senal fisica se forma en el ESP32.

---

## 2. Archivos principales revisados

Firmware ESP32:

- `SP32/MagnetoPEMF/MagnetoPEMF.ino`: setup principal, loop, bateria, BLE,
  reporte de estado y auto-apagado.
- `SP32/MagnetoPEMF/Config.h`: constantes globales, UUID BLE, GPIO, rangos de
  seguridad, presets, formas de onda y parametros de burst/SPWM.
- `SP32/MagnetoPEMF/MagnetoTypes.h`: estados, tipos de onda, estructura de
  canal, estado de tratamiento y estructura de presets.
- `SP32/MagnetoPEMF/TherapyPresets.h`: tabla de presets y conversiones
  porcentaje <-> duty raw.
- `SP32/MagnetoPEMF/MagnetoController.cpp/.h`: nucleo de generacion de senal,
  control de sesion, LEDC, RMT, GPTimer y SPWM.
- `SP32/MagnetoPEMF/CommandHandler.cpp/.h`: protocolo JSON recibido por BLE.
- `SP32/MagnetoPEMF/BLEManager.cpp/.h`: servidor BLE GATT.
- `SP32/MagnetoPEMF/BatteryMonitor.cpp/.h`: lectura de bateria por ADC.
- `SP32/MagnetoPEMF/StorageManager.cpp/.h`: persistencia NVS.

App Flutter:

- `lib/data/repositories/ble_repository.dart`: escaneo, conexion, MTU,
  descubrimiento de servicio/caracteristica y escritura BLE.
- `lib/presentation/providers/ble_provider.dart`: estado BLE, notificaciones,
  bateria, reconexion y respuestas pendientes.
- `lib/presentation/providers/dashboard_provider.dart`: comandos de terapia,
  pausa, reanudacion, presets rapidos y sincronizacion con estado del ESP32.
- `lib/presentation/screens/manual_screen.dart`: controles manuales de
  frecuencia, potencia y tiempo.
- `lib/presentation/screens/dashboard_screen.dart`: panel principal, monitor y
  presets rapidos de forma de onda.
- `android/app/src/main/AndroidManifest.xml`: permisos BLE y ubicacion.

---

## 3. Arquitectura de la app Flutter

La app esta construida con Flutter y usa:

- `flutter_riverpod`: estado de BLE, dashboard, pacientes e historial.
- `flutter_blue_plus`: comunicacion Bluetooth Low Energy.
- `permission_handler`: permisos de escaneo/conexion BLE y ubicacion.
- `shared_preferences`: equipos guardados y recuperacion basica de sesion.
- `go_router`: navegacion.
- `hive`: persistencia de datos de sesiones/pacientes.

### 3.1 Rol real de la app

La app:

- Busca dispositivos BLE que anuncien el UUID de servicio del ESP32.
- Se conecta al dispositivo.
- Negocia MTU de hasta 512 bytes para poder enviar JSON mas largo.
- Descubre la caracteristica BLE configurada en el firmware.
- Activa notificaciones.
- Envia comandos como texto ASCII/JSON.
- Espera respuestas `{"ok":...}`, `{"error":...}`, `STATS:...` o JSON de
  estado/bateria.

La app no produce formas de onda ni senales electricas. Solo envia parametros:

```json
{"cmd":"start","duration":5,"freq":75,"intensity":50.0,"wave":1}
```

El ESP32 interpreta ese mensaje y genera la senal por hardware.

### 3.2 BLE en la app

UUIDs usados por la app y el ESP32:

```text
Device name: MAGNETO_TESIS
Service UUID: 4fafc201-1fb5-459e-8fcc-c5c9c331914b
Characteristic UUID: beb5483e-36e1-4688-b7f5-ea07361b26a8
```

El repositorio BLE escanea con filtro de servicio:

```dart
FlutterBluePlus.startScan(
  withServices: [Guid(SERVICE_UUID)],
  timeout: timeout,
);
```

Cuando encuentra la caracteristica, escribe el comando:

```dart
List<int> bytes = command.codeUnits;
await characteristic.write(bytes, withoutResponse: false);
```

### 3.3 Comandos que genera la app

Desde `dashboard_provider.dart`, la app envia principalmente:

Inicio:

```json
{
  "cmd": "start",
  "duration": 5,
  "freq": 75,
  "intensity": 50.0,
  "wave": 1
}
```

Pausa:

```json
{"cmd":"pause"}
```

Reanudar:

```json
{"cmd":"resume"}
```

Reanudar aplicando cambios manuales:

```json
{"cmd":"resume","freq":50,"intensity":10.0,"wave":4}
```

Detener:

```json
{"cmd":"stop"}
```

Pedir estado despues de conectar:

```json
{"cmd":"status"}
```

### 3.4 Presets rapidos en la app

La pantalla principal ofrece presets rapidos por forma de onda:

| Nombre app | Frecuencia | Potencia | Duracion | `wave` |
|---|---:|---:|---:|---:|
| Triangular | 75 Hz | 50 % | 5 min | 1 |
| Trapezoidal | 50 Hz | 50 % | 5 min | 2 |
| Cuadrada | 15 Hz | 10 % | 5 min | 0 |
| Burst | 15 Hz | 8 % | 5 min | 3 |

Estos presets de la app no llaman directamente al comando `preset` del
firmware. La app carga esos parametros en el estado local y despues envia un
`start` con `freq`, `intensity`, `duration` y `wave`.

---

## 4. Arquitectura del firmware ESP32

El firmware esta escrito para ESP32 usando el entorno Arduino con acceso a
drivers nativos ESP-IDF.

Componentes principales:

- `BLEManager`: servidor GATT. Recibe texto por WRITE y envia notificaciones.
- `CommandHandler`: parsea JSON, valida una copia completa de la configuración y solicita su aplicación al controlador.
- `MagnetoController`: genera y detiene senales. Maneja estados y sesiones.
- `BatteryMonitor`: lee bateria por ADC1/GPIO34.
- `StorageManager`: guarda parametros en NVS con `Preferences`.

### 4.1 Setup principal

En `MagnetoPEMF.ino`:

1. Inicializa Serial a `115200`.
2. Inicializa NVS.
3. Inicializa `MagnetoController`.
4. Inicializa monitor de bateria.
5. Carga parametros guardados por canal.
6. Carga por defecto el preset IGEA (`PRESET_IGEA`).
7. Imprime los presets disponibles.
8. Crea una cola de ocho comandos e inicia BLE con nombre `MAGNETO_TESIS`.

### 4.2 Loop principal

En cada ciclo:

```cpp
magneto.update();
battery.update();
ble.update();
```

Ademas:

- Procesa como máximo una orden encolada por vuelta. El callback BLE sólo copia el mensaje; no modifica el controlador mientras se ejecuta su actualización.
- Detecta conexion/desconexion BLE.
- Envia bateria despues de conectar y luego cada 5 s.
- Si la bateria esta critica y hay tratamiento activo, detiene la salida.
- Envia `STATS` cada 1 s durante tratamiento.
- Escribe estado resumido por Serial cada 10 s.

---

## 5. Hardware e interfaces usadas

### 5.1 GPIO de salida de senal

El GPIO efectivo en el codigo actual es:

```cpp
const PwmPinConfig PWM_PINS[PWM_CH_COUNT] = {
    {GPIO_NUM_4, LEDC_CHANNEL_0, LEDC_TIMER_0}
};
```

Por tanto:

```text
Salida actual de senal: GPIO4
Canal LEDC: LEDC_CHANNEL_0
Timer LEDC: LEDC_TIMER_0
Cantidad de canales: 1
```

Ruta electrica esperada:

```text
GPIO4 -> entrada del driver/MOSFET -> bobina -> campo magnetico
```

Si se usa DRV8871 en modo unidireccional, el comentario del proyecto indica
entrada de control a IN1 y IN2 a GND. Si el hardware real usa otra etapa, debe
validarse el esquema electrico.

### 5.2 BLE GATT

Interfaz usada para app -> ESP32.

Propiedades de la caracteristica:

- READ
- WRITE
- NOTIFY

Uso:

- App escribe JSON.
- ESP32 responde por notificacion.
- ESP32 tambien envia bateria y STATS de forma periodica.

### 5.3 LEDC

LEDC es el generador PWM por hardware del ESP32.

Usos en este proyecto:

- Onda cuadrada.
- PWM puro.
- Portadora SPWM para triangular/trapezoidal.

Configuraciones:

```text
Modo cuadrada/PWM puro:
  Resolucion: 16 bits
  Duty raw: 0-65535
  Frecuencia terapeutica: 1-100 Hz

Modo SPWM:
  Portadora: 10000 Hz
  Resolucion: 12 bits
  Duty raw: 0-4095
```

Motivo del cambio en SPWM:

- A 16 bits el ESP32 no soporta portadoras altas como 10 kHz.
- Por eso se usa 12 bits para la portadora de 10 kHz.

### 5.4 RMT

RMT se usa para generar micropulsos exactos en modo `BURST`.

Configuracion:

```text
Resolucion RMT: 1 MHz
1 tick = 1 us
GPIO: GPIO4
Maximo simbolos: 48
```

RMT permite definir una secuencia de HIGH/LOW con duraciones en microsegundos.
En este proyecto se usa para crear una rafaga de micropulsos dentro de una
ventana de 5 ms.

### 5.5 GPTimer

GPTimer se usa como temporizador macro en el modo `BURST`.

Ejemplo para 15 Hz:

```text
Periodo macro = 1 / 15 Hz = 66.667 ms
Cada 66.667 ms el GPTimer activa una bandera
El loop detecta la bandera y dispara RMT una sola vez
```

La ISR no transmite directamente la rafaga. Solo marca `_burstTrigger = true`.
La transmision RMT se hace luego en `MagnetoController::update()`.

### 5.6 `esp_timer`

Se usa para:

- Medir tiempo de sesion en microsegundos.
- Calcular fase dentro del ciclo SPWM triangular/trapezoidal.

Esto evita depender de `millis()` para calculos finos de fase.

### 5.7 ADC1 / GPIO34

El monitor de bateria usa:

```text
GPIO34 / ADC1_CHANNEL_6
Resolucion ADC: 12 bits
Atenuacion: ADC_ATTEN_DB_11
Muestras: 10
Tiempo total aproximado de muestreo: 1 ms
```

El voltaje reportado es el voltaje en el pin ADC despues del divisor, no
necesariamente el voltaje real de la bateria antes del divisor.

### 5.8 NVS / Preferences

Se usa `Preferences` con namespace:

```text
magneto
```

Claves:

```text
ch0f -> frecuencia del canal 0
ch0d -> duty raw del canal 0
dur  -> ultima duracion
```

---

## 6. Parametros globales del firmware

### 6.1 Rangos de seguridad

```text
Frecuencia minima: 1 Hz
Frecuencia maxima: 100 Hz
Duty minimo documentado: 5 %
Duty maximo firmware: 60 %
Duty maximo app: 50 %
Duracion minima firmware: 1 min
Duracion maxima firmware: 480 min
Duracion minima especial para PRESET_BURST_QUICK: 5 min
```

### 6.2 Bateria

```text
BAT_MIN_VOLT: 0.1 V
BAT_MAX_VOLT: 3.2 V
BAT_LOW_THRESHOLD: 15 %
BAT_CRITICAL_TH: 5 %
BAT_NOISE_FLOOR_V: 0.35 V
Intervalo de envio BLE: 5000 ms
```

Si la bateria esta por debajo de 5 %, el firmware bloquea `start`/`resume`.
Si ya habia tratamiento activo, hace auto-apagado y envia error `LOW_BAT`.

---

## 7. Como se forma la senal

### 7.1 Concepto fisico del sistema

El ESP32 no genera directamente "mT". Genera una senal logica digital en GPIO4.
Esa senal controla una etapa de potencia que alimenta una bobina.

La cadena fisica es:

```text
PWM digital -> corriente en bobina -> campo B -> cambio de B en el tiempo
```

En una bobina:

- El campo magnetico depende de la corriente, numero de espiras y geometria.
- La corriente depende de la fuente, resistencia, inductancia, driver y duty.
- El efecto inducido se relaciona con `dB/dt`, es decir, con que tan rapido
  cambia el campo.

Por eso el firmware controla:

- Frecuencia.
- Duty.
- Forma de envolvente.
- Duracion de sesion.
- Rafagas/micropulsos.

Pero no controla directamente:

- Campo magnetico real en mT.
- Temperatura de bobina.
- Corriente real.
- Distancia efectiva al tejido.

Para convertir duty/frecuencia a mT se requiere calibracion.

### 7.2 Formula de periodo

Para una frecuencia terapeutica:

```text
T = 1 / f
T_us = 1000000 / f
```

Ejemplos:

```text
75 Hz -> T = 13.333 ms
50 Hz -> T = 20.000 ms
15 Hz -> T = 66.667 ms
```

### 7.3 Formula de duty

El firmware recibe potencia como porcentaje (`intensity`) o duty raw.

Conversion en `TherapyPresets.h`:

```cpp
duty16 = pct / 100.0 * 65535
pct = duty16 / 65535.0 * 100.0
```

Ejemplos:

```text
10.00 % -> duty16 aproximado 6553
9.75  % -> duty16 aproximado 6389
8.19  % -> duty16 aproximado 5367
50.00 % -> duty16 aproximado 32767
```

El tiempo activo equivalente en PWM directo es:

```text
t_on = T * duty_pct / 100
```

Ejemplo IGEA:

```text
f = 75 Hz
T = 13.333 ms
duty = 9.75 %
t_on = 1.300 ms
```

---

## 8. Tipos de senal implementados

El enum `WaveShape` define cinco modos:

| Valor | Nombre | Motor usado |
|---:|---|---|
| 0 | SQUARE | LEDC 16-bit a frecuencia terapeutica |
| 1 | TRIANGULAR | SPWM sobre LEDC 10 kHz / 12-bit |
| 2 | TRAPEZOIDAL | SPWM sobre LEDC 10 kHz / 12-bit |
| 3 | BURST | GPTimer + RMT |
| 4 | PWM_PURE | LEDC 16-bit a frecuencia terapeutica |

### 8.1 SQUARE

Modo de onda cuadrada/rectangular simple.

Implementacion:

- `MagnetoController::applyPwm()`.
- LEDC se configura a la frecuencia terapeutica solicitada.
- Duty fijo en escala de 16 bits.
- GPIO4 alterna HIGH/LOW automaticamente por hardware.

Ejemplo:

```json
{"cmd":"start","duration":5,"freq":15,"intensity":10,"wave":0}
```

Resultado ideal:

```text
Frecuencia: 15 Hz
Periodo: 66.667 ms
Duty: 10 %
Tiempo HIGH: 6.667 ms
Tiempo LOW: 60.000 ms
```

Ventajas:

- Simple.
- Muy estable.
- Bajo consumo de CPU.
- Adecuado para pruebas basicas de driver/bobina.

Limitacion:

- No dibuja una envolvente triangular/trapezoidal. Es pulso rectangular.

### 8.2 PWM_PURE

En el codigo actual, `PWM_PURE` usa el mismo motor que `SQUARE`:

- LEDC.
- Frecuencia terapeutica directa.
- Duty fijo.

La diferencia es semantica:

- `SQUARE` representa una forma de onda cuadrada.
- `PWM_PURE` representa control directo de frecuencia + duty sin intentar
  simular una forma clinica especifica.

La app manual fuerza `wave = 4` cuando el usuario modifica parametros manuales
durante pausa.

### 8.3 TRIANGULAR por SPWM

No es una salida analogica triangular pura. Es PWM de 10 kHz cuya anchura de
pulso se modula para que la envolvente del duty tenga forma triangular.

Motor:

- `startSpwm()`.
- `updateSpwm()`.
- LEDC a 10 kHz.
- Resolucion LEDC 12 bits.
- Fase calculada con `esp_timer_get_time()`.

Secuencia por ciclo terapeutico:

```text
0 % del periodo       -> duty minimo
50 % del periodo      -> duty maximo
100 % del periodo     -> duty minimo
```

Formula conceptual:

```text
Si phase < T/2:
  duty = dutyMin + range * phase / (T/2)

Si phase >= T/2:
  duty = dutyMax - range * (phase - T/2) / (T/2)
```

Donde:

```text
dutyMin = 5 % convertido a 12 bits
dutyMax = intensity convertido a 12 bits
range = dutyMax - dutyMin
```

Ejemplo con 75 Hz e intensity 50 %:

```text
Periodo terapeutico: 13.333 ms
Portadora PWM: 10 kHz
Periodo portadora: 100 us
Envolvente:
  0.000 ms  -> 5 %
  6.667 ms  -> 50 %
  13.333 ms -> 5 %
```

El driver/bobina recibe pulsos rapidos. La forma triangular aparece como
envolvente de energia/corriente, no como voltaje analogico limpio en GPIO4.

### 8.4 TRAPEZOIDAL por SPWM

Tambien usa PWM de 10 kHz con duty variable.

Secuencia por ciclo terapeutico:

```text
0 % a 25 % del periodo   -> rampa de dutyMin a dutyMax
25 % a 75 % del periodo  -> dutyMax sostenido
75 % a 100 % del periodo -> rampa de dutyMax a dutyMin
```

Formula conceptual:

```text
Q1 = T / 4
Q3 = 3T / 4

Si phase < Q1:
  duty sube linealmente

Si Q1 <= phase < Q3:
  duty = dutyMax

Si phase >= Q3:
  duty baja linealmente
```

Ejemplo con 50 Hz e intensity 50 %:

```text
Periodo terapeutico: 20.000 ms
Rampa subida: 0 a 5.000 ms
Meseta: 5.000 a 15.000 ms
Rampa bajada: 15.000 a 20.000 ms
```

### 8.5 BURST con GPTimer + RMT

Este es el modo mas tecnico del firmware.

Objetivo:

- Generar una rafaga cada cierto periodo macro.
- Dentro de cada rafaga, emitir micropulsos exactos.
- Dejar reposo LOW entre rafagas.

Configuracion base:

```text
Frecuencia macro: normalmente 15 Hz
Periodo macro: 66.667 ms
Ventana de rafaga: 5 ms
Micropulso HIGH: 225 us
Micropulso LOW: 25 us
Tiempo por micropulso: 250 us
Micropulsos por ventana: 5000 / 250 = 20
Reposo por ciclo: 66.667 ms - 5 ms = 61.667 ms
```

Secuencia ideal:

```text
|<------------------------ 66.667 ms ------------------------>|
|<-- 5 ms burst -->|<-------------- 61.667 ms LOW ------------>|

Dentro de 5 ms:
225us HIGH + 25us LOW, repetido 20 veces
```

Por que no solo LEDC:

- LEDC genera PWM periodico simple.
- El burst necesita dos escalas de tiempo:
  - Escala macro: 15 Hz.
  - Escala micro: pulsos de cientos de microsegundos dentro de una ventana.
- RMT es mejor para dibujar micropulsos exactos.
- GPTimer es mejor para repetir la rafaga completa en el periodo macro.

Implementacion:

1. `startBurstMode()` detiene SPWM y LEDC.
2. Inicializa RMT si aun no existe.
3. `buildBurstPattern()` crea la plantilla de micropulsos.
4. Habilita RMT.
5. Configura GPTimer a `1 MHz`.
6. GPTimer dispara una alarma cada `1000000 / frequency` us.
7. La ISR marca `_burstTrigger = true`.
8. `update()` transmite la plantilla RMT cuando ve el trigger.

Si se envia `burst_freq`, el firmware recalcula el micropulso:

```text
totalPulseUs = 1000000 / burst_freq
pulseHighUs = 90 % de totalPulseUs
pulseLowUs = 10 % de totalPulseUs
```

---

## 9. Presets del firmware

Los presets estan en `TherapyPresets.h`. La app actual no usa directamente
`cmd:"preset"` en los presets rapidos, pero el firmware si soporta el comando.

| ID | Nombre | Frecuencia | Duty | Onda | Duracion codigo | Campo ref. |
|---:|---|---:|---:|---|---:|---:|
| 0 | Personalizado | 75 Hz | 10.00 % | SQUARE | 30 min | 2.0 mT |
| 1 | IGEA/Faldini 75Hz | 75 Hz | 9.75 % | TRIANGULAR | 5 min | 2.0 mT |
| 2 | Orthofix/EBI 15Hz Burst | 15 Hz | 8.19 % | BURST | 5 min | 1.5 mT |
| 3 | Antiinflamatorio 50Hz | 50 Hz | 10.00 % | TRAPEZOIDAL | 30 min | 2.0 mT |
| 4 | Preclinico/Parate 15Hz | 15 Hz | 9.00 % | SQUARE | 10 min | 2.0 mT |
| 5 | Burst 15Hz 5min | 15 Hz | 7.50 % | BURST | 5 min | 1.5 mT |

Notas:

- Los campos `fieldTargetMT` son referencia documental. No hay sensor de campo.
- ID 1 e ID 2 estan configurados a 5 min en el codigo actual para prueba
  rapida, aunque los comentarios clinicos mencionan sesiones largas.
- En modo BURST, el duty del preset queda como dato/status, pero la forma real
  de rafaga depende principalmente de `BURST_WINDOW_MS`,
  `BURST_PULSE_WIDTH_US`, `BURST_GAP_US` y frecuencia macro.

Comando de preset:

```json
{"cmd":"preset","id":1}
```

Respuesta:

```json
{
  "ok": "preset",
  "id": 1,
  "name": "IGEA/Faldini 75Hz",
  "freq": 75,
  "duty_pct": 9.75,
  "session_min": 5
}
```

---

## 10. Protocolo BLE JSON del firmware

El firmware acepta JSON de hasta `JSON_BUFFER_SIZE = 256`.

### 10.1 Comandos soportados

| Comando | Ejemplo | Funcion |
|---|---|---|
| `preset` | `{"cmd":"preset","id":1}` | Carga preset clinico del firmware |
| `start` | `{"cmd":"start","duration":5}` | Inicia con preset/parametros actuales |
| `start` custom | `{"cmd":"start","duration":5,"freq":75,"intensity":10,"wave":1}` | Aplica parametros y arranca |
| `pause` | `{"cmd":"pause"}` | Pausa y apaga salida sin perder restante |
| `resume` | `{"cmd":"resume"}` | Reanuda con parametros actuales |
| `resume` custom | `{"cmd":"resume","freq":75,"intensity":10,"wave":4}` | Cambia parametros y reanuda |
| `stop` | `{"cmd":"stop"}` | Detiene tratamiento y pone bobina en 0 |
| `set` | `{"cmd":"set","ch":0,"freq":75,"intensity":9.75}` | Cambia parametros, incluso en caliente |
| `status` | `{"cmd":"status"}` | Devuelve estado completo |

### 10.2 Campos aceptados

| Campo | Tipo | Descripcion |
|---|---|---|
| `cmd` | string | Comando principal |
| `id` | int | ID de preset |
| `ch` | int | Canal. `0` para canal unico, `-1` para todos |
| `freq` | uint32 | Frecuencia en Hz, rango 1-100 |
| `duty` | entero | Cuenta de 16 bits, limitada a la correspondiente a `DUTY_MAX_PCT` |
| `intensity` | float | Duty en porcentaje |
| `wave` | int | 0 square, 1 triangular, 2 trapezoidal, 3 burst, 4 pwm pure |
| `phase` | int | Desfase 0-360 grados en PWM directo; 360 equivale a 0. SPWM y BURST requieren fase 0 |
| `en` | bool | Habilita/deshabilita canal |
| `duration` | int | Minutos, sólo en `start`; se rechaza fuera de 1-480 (mínimo 5 para `PRESET_BURST_QUICK`) |
| `burst_freq` | uint32 | Frecuencia interna de micropulsos burst |

Si se reciben `duty` e `intensity`, ambos deben representar la misma cuenta de 16 bits. Los valores incompatibles producen `CONFLICTING_FIELDS`, sin aplicar ninguno.

### 10.2.1 Validación conjunta de órdenes

`start`, `resume` y `set` construyen una copia de todos los parámetros; comprueban tipos, rangos y compatibilidad entre campos antes de aplicarla. Un valor negativo, un entero demasiado grande, un modo inexistente o un canal distinto de 0 o −1 no puede aceptarse mediante truncamiento de tipo. Se rechazan también campos desconocidos o no admitidos por la orden.

El inicio sólo se admite desde `IDLE`; la reanudación requiere `PAUSED`, sesión activa y tiempo restante. Se mantiene el bloqueo por batería crítica. La duración se rechaza cuando está fuera del intervalo, en lugar de ajustarse silenciosamente. En SPWM, el máximo de la envolvente no puede quedar por debajo de su mínimo. Para `burst_freq` se comprueba que los tiempos y la cantidad de símbolos quepan en la ventana y en los recursos RMT.

Tras aprobar la orden, el controlador desactiva el generador anterior, copia la configuración completa y activa únicamente los canales habilitados si la sesión está en ejecución. En espera o pausa, configurar la fase u otro parámetro mantiene la salida inactiva. Los valores se guardan en NVS después de aceptar la operación.

Un rechazo informa, por ejemplo, `{"error":"INVALID_PARAM","field":"freq"}`, sin cambiar la configuración, reiniciar el tiempo ni guardar valores. La cola devuelve `COMMAND_TOO_LONG` para mensajes de 256 bytes o más y `COMMAND_QUEUE_FULL` si no puede admitir otra orden. Estos mensajes son errores, no confirmaciones de ejecución.

### 10.3 Respuestas y notificaciones

Inicio correcto:

```json
{"ok":"started","duration":5}
```

Pausa:

```json
{"ok":"paused","remaining_sec":240,"freq":75,"intensity":10.00}
```

Reanudacion:

```json
{"ok":"resumed","remaining_sec":240,"freq":75,"intensity":10.00}
```

Stop:

```json
{"ok":"stopped"}
```

Bateria:

```json
{"bat":87,"v":2.78}
```

Estado completo:

```json
{
  "state": "RUNNING",
  "preset": 1,
  "preset_name": "IGEA/Faldini 75Hz",
  "remaining_sec": 240,
  "remaining_min": 4,
  "burst_active": false,
  "channels": [
    {
      "ch": 0,
      "freq": 75,
      "duty_pct": 9.8,
      "wave": 1,
      "phase": 0,
      "en": true
    }
  ]
}
```

Stats periodico mientras corre:

```text
STATS:1:<remainingSecs>:<freq>:<dutyRaw>;
```

Ejemplo:

```text
STATS:1:240:75:6389;
```

---

## 11. Estados del sistema

`SystemState` define:

| Estado | Significado |
|---|---|
| `IDLE` | Sin tratamiento activo |
| `RUNNING` | Generando senal |
| `PAUSED` | Tratamiento pausado, salida apagada |
| `LOW_BATTERY` | Estado reservado/documentado |
| `ERROR` | Estado reservado/documentado |

Flujo normal:

```text
IDLE -> RUNNING -> PAUSED -> RUNNING -> IDLE
```

Eventos:

- `start`: pasa a `RUNNING`.
- `pause`: pasa a `PAUSED` y apaga salida.
- `resume`: vuelve a `RUNNING`.
- `stop`: vuelve a `IDLE` y apaga salida.
- tiempo terminado: `stopTreatment()`.
- bateria critica corriendo: `stopTreatment()` + notificacion `LOW_BAT`.

---

## 12. Seguridad implementada

### 12.1 Limites de frecuencia

`validateFreq()` rechaza valores fuera de:

```text
1 Hz <= freq <= 100 Hz
```

### 12.2 Limite de duty

`validateDuty()` rechaza duty con porcentaje mayor a:

```text
DUTY_MAX_PCT = 60 %
```

La app limita manualmente a 50 %, por debajo del limite firmware.

### 12.3 Auto-apagado por tiempo

El firmware calcula el tiempo transcurrido con `esp_timer_get_time()`.

Si el tiempo transcurrido supera la duracion programada:

```text
stopTreatment()
```

La duracion maxima esta limitada a:

```text
480 min
```

### 12.4 Pausa segura

Cuando se pausa:

- Se acumula el tiempo ya consumido.
- Se calcula restante.
- Se apaga SPWM.
- Se apaga BURST si estaba activo.
- Se detiene LEDC.
- Estado pasa a `PAUSED`.

### 12.5 Stop seguro

Cuando se detiene:

- `stopSpwm()`
- `stopBurstMode()`
- `stopLedc()`
- Salida queda en duty 0.
- Estado vuelve a `IDLE`.

### 12.6 Bateria critica

El firmware bloquea inicio/reanudacion si:

```text
battery.getPercentage() < 5
```

Si la terapia estaba en curso:

```json
{"error":"LOW_BAT","action":"auto_stop","bat":4,"v":0.20}
```

---

## 13. Detalle de bateria

Lectura:

```cpp
adc1_get_raw(ADC1_CHANNEL_6)
```

Promedio:

```text
10 muestras
100 us entre muestras
1 ms total aproximado
```

Conversion:

```text
voltage = rawAdc * 3.3 / 4095
```

Porcentaje:

- Si `voltage < 0.35 V`: 0 %, se asume pin flotante o sin bateria.
- Si `voltage >= 3.2 V`: 100 %.
- Entre 0.1 V y 3.2 V: mapeo lineal 1-100 %.

Observacion:

El codigo reporta el voltaje en el pin ADC. Si hay divisor resistivo, para
conocer el voltaje real de bateria se debe multiplicar por la relacion del
divisor.

---

## 14. Persistencia

`StorageManager` guarda:

- Frecuencia por canal.
- Duty por canal.
- Ultima duracion.

Durante setup se cargan parametros guardados, pero luego se carga el preset IGEA
por defecto. Eso significa que el preset IGEA puede sobrescribir la configuracion
de arranque aunque se hayan leido valores de NVS.

---

## 15. Observaciones tecnicas importantes encontradas

### 15.1 Correspondencia entre interfaces, pines y documentación

La asignación documentada para la MH-ET LIVE ESP32 MiniKit coincide con el código:
GPIO4 para LEDC/RMT (`Config.h`) y GPIO34 / ADC1_CH6 para la batería
(`BatteryMonitor.cpp`). GPIO1 y GPIO3 se reservan para UART0; BLE utiliza la radio
integrada. El documento `Arquitectura_Senales_ESP32.md` se ha corregido para
mantener GPIO22 únicamente como alternativa.

La comparación de placas, las restricciones de pines y el divisor de batería
se desarrollan en [SECCIÓN 2.3.md, apartado 2.3.6](../../../AVANCE%20TESIS/SECCI%C3%93N%202.3.md).
El divisor seleccionado es de 68 kΩ y 10 kΩ, con 100 nF en la entrada ADC.
La conversión calibrada al voltaje del banco mediante el factor 7,8 y el ajuste
de umbrales siguen pendientes de implementar en el firmware.

### 15.2 Comentario de SPWM indica 20 kHz, codigo usa 10 kHz

En el comentario de `applyPwm()` aparece una referencia a 20 kHz, pero la
constante real es:

```text
SPWM_CARRIER_HZ = 10000
```

Por tanto la portadora actual es 10 kHz.

### 15.3 Orthofix: duty documental vs ventana real

`DUTY_ORTHOFIX_PCT = 8.19 %` representa 5.46 ms sobre 66.67 ms segun comentario
clinico. Sin embargo, el burst real del codigo usa:

```text
BURST_WINDOW_MS = 5
```

Entonces el burst fisico actual dura 5.00 ms por ciclo macro, equivalente a
7.50 % a 15 Hz.

### 15.4 App y estado `wave`

El estado JSON del firmware coloca `wave` dentro de `channels[0]`.
La app intenta leer `data['wave']` en nivel superior al procesar status. Para
sincronizar perfectamente la forma de onda, conviene leer:

```text
channels[0]["wave"]
```

Esto no impide enviar comandos, pero puede afectar como la app reconstruye el
estado visual despues de reconectar.

### 15.5 Comprobación de la validación de comandos

Se incorporaron pruebas ejecutables en `../tests` que utilizan el parser y el controlador reales, ArduinoJson y sustitutos de los periféricos, BLE, batería y NVS. Comprueban órdenes inválidas sin efectos parciales, parámetros válidos, perfiles, pausa/reanudación, agotamiento del tiempo, cambios de modo y salida inactiva al configurar fuera de una sesión. El archivo `../tests/README.md` describe su ejecución y alcance.

Los límites efectivos continúan en `Config.h`: 1–100 Hz, duty máximo del 60 % y 1–480 min, con el mínimo específico del perfil de ráfaga rápida. Esta corrección implementa su validación conjunta; no modifica esos límites ni completa la calibración de batería o los ensayos del hardware.

---

## 16. Validacion recomendada en banco

Antes de usar con bobina/carga real:

1. Verificar GPIO correcto.
   - Medir GPIO4 con osciloscopio.
   - Confirmar que el cableado real usa GPIO4.

2. Validar LEDC cuadrado/PWM puro.
   - Comando: `{"cmd":"start","duration":1,"freq":15,"intensity":10,"wave":4}`
   - Esperado: 15 Hz, 10 % duty.

3. Validar triangular SPWM.
   - Comando: `{"cmd":"start","duration":1,"freq":75,"intensity":50,"wave":1}`
   - Esperado: portadora 10 kHz y duty variando con envolvente triangular a
     75 Hz.

4. Validar trapezoidal SPWM.
   - Comando: `{"cmd":"start","duration":1,"freq":50,"intensity":50,"wave":2}`
   - Esperado: duty sube, se mantiene, baja, repitiendo a 50 Hz.

5. Validar burst.
   - Comando: `{"cmd":"start","duration":5,"freq":15,"intensity":8,"wave":3}`
   - Esperado: cada 66.667 ms aparece una ventana de 5 ms con 20 micropulsos
     de 225 us HIGH + 25 us LOW.

6. Medir driver y bobina.
   - Corriente.
   - Temperatura.
   - Caida de tension.
   - Forma real en la bobina.
   - Campo magnetico con magnetometro.

7. Verificar seguridad.
   - Stop apaga GPIO.
   - Pause apaga GPIO.
   - Fin de tiempo apaga GPIO.
   - Bateria critica bloquea inicio.

---

## 17. Resumen tecnico final

La parte mas importante del proyecto esta en el ESP32. El firmware implementa
una arquitectura de generacion de senal basada en perifericos de hardware:

- LEDC para PWM simple, onda cuadrada y PWM puro.
- LEDC a 10 kHz con modulacion SPWM para envolventes triangular y trapezoidal.
- GPTimer + RMT para rafagas de micropulsos tipo burst.
- BLE GATT para recibir comandos de la app.
- ADC1 para bateria.
- NVS para guardar parametros.

La app es un control remoto BLE con interfaz visual. Su responsabilidad es
seleccionar parametros, enviarlos como JSON y reflejar respuestas. La generacion
temporal fina, la forma de onda, los temporizadores y el apagado de seguridad
estan implementados en `MagnetoController` y en el `loop` principal del firmware.

Punto clave para tesis:

```text
La senal no nace en la aplicacion movil. La aplicacion solo transmite comandos.
La senal se forma en el ESP32 mediante PWM, SPWM o RMT, segun el tipo de onda
seleccionado, y se aplica al driver de potencia por GPIO4.
```
