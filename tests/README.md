# Pruebas de validación conjunta del firmware

Compilan los archivos de producción `CommandHandler.cpp` y `MagnetoController.cpp` con ArduinoJson 7. Los sustitutos de `stubs` simulan el reloj, las salidas, BLE, batería y escrituras en memoria; no duplican la lógica de validación.

## Ejecución

Se requiere Python 3, ArduinoJson 7 y MSVC Build Tools en Windows, o g++/clang++ en Linux:

```powershell
python tests/run_tests.py --arduino-json "RUTA/ArduinoJson/src"
```

Los ejecutables se generan en `build/firmware_command_tests`. Para compilar además el firmware completo con los controladores reales de ESP32:

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32 MagnetoPEMF
```

## Casos comprobados

- Tipos incorrectos, valores negativos, desbordamientos, canal inexistente y campos no admitidos.
- Rechazo de órdenes que mezclan un parámetro válido con otro inválido.
- Consistencia entre duty e intensidad, modo y fase, y límites del patrón RMT.
- Inicio, reanudación y duración incompatibles con el estado actual.
- Batería crítica sin aceptar ni guardar cambios.
- Conservación de parámetros, tiempo, estado, escrituras de salida y memoria ante cada rechazo.
- Pausa sin descontar tiempo, reanudación y apagado al finalizar.
- Configuración en espera/pausa sin energizar la salida.
- Cambio entre PWM y ráfaga, canales deshabilitados y todos los perfiles existentes.

Las pruebas verifican la lógica del programa. La cola FreeRTOS y los controladores reales se incluyen en la compilación ESP32; su ejecución en placa, la señal GPIO4, la precisión temporal y la calibración de batería requieren comprobación física.
