# MagnetoPEMF: firmware ESP32

Firmware del prototipo de tesis MagnetoPEMF para ESP32, con control por Bluetooth Low Energy (BLE), generacion de senales PWM y rafagas, monitoreo de bateria y almacenamiento de configuracion en NVS.

## Estructura

- `MagnetoPEMF/`: sketch Arduino y modulos del firmware.
- `tests/`: pruebas de validacion con perifericos simulados.

## Compilacion y carga

1. Instalar el soporte para ESP32 de Espressif basado en Arduino-ESP32 3.x (ESP-IDF 5.x; se utilizan los controladores RMT TX y GPTimer).
2. Instalar ArduinoJson 7.
3. Abrir `MagnetoPEMF/MagnetoPEMF.ino` en Arduino IDE y seleccionar la placa ESP32 y el puerto correspondientes.
4. Revisar los pines y parametros de `MagnetoPEMF/Config.h` antes de compilar y cargar.

Si el entorno Arduino CLI ya esta configurado:

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32 MagnetoPEMF
```

La configuracion actual utiliza GPIO4 para la salida y GPIO34 para la medicion de bateria. El dispositivo BLE se anuncia como `MAGNETO_TESIS`. La app se conecta directamente por BLE, sin comandos USB, PIN ni ventana temporal de vinculacion. La consola serie a 115200 baudios se utiliza para diagnostico. El firmware actual no exige bonding ni cifrado autenticado para acceder a la caracteristica BLE.

## Pruebas

Desde la raiz del repositorio:

```powershell
python tests/run_tests.py --arduino-json "RUTA/ArduinoJson/src"
```

Consultar [tests/README.md](tests/README.md) para los requisitos y el alcance de las pruebas. Los resultados de compilacion se generan en `build/`, excluido de Git.

## Documentacion

- [Arquitectura de senales](MagnetoPEMF/Arquitectura_Senales_ESP32.md)
- [Documentacion tecnica](MagnetoPEMF/DOCUMENTACION_TECNICA_COMPLETA.md)

Los documentos tecnicos contienen referencias de desarrollo que deben contrastarse con la configuracion actual del codigo. Los presets son configuraciones de ensayo del prototipo; el firmware no mide ni calibra directamente el campo magnetico.
