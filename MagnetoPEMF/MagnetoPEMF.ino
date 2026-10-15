/**
 * @file MagnetoPEMF.ino
 * @brief Sistema PEMF modular para ESP32 con control BLE y monitoreo de bateria.
 */

#include <Arduino.h>
#include "Config.h"
#include "MagnetoTypes.h"
#include "TherapyPresets.h"
#include "MagnetoController.h"
#include "BatteryMonitor.h"
#include "BLEManager.h"
#include "CommandHandler.h"
#include "StorageManager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

MagnetoController magneto;
BatteryMonitor    battery;
BLEManager        ble;
StorageManager    storage;
CommandHandler    cmdHandler(magneto, battery, ble, storage);

unsigned long lastBatReport = 0;
bool pendingBatteryPushAfterConnect = false;
unsigned long bleConnectedAt = 0;

struct PendingBleCommand {
    char json[JSON_BUFFER_SIZE];
    uint16_t id;
    uint32_t epoch;
};
QueueHandle_t commandQueue = nullptr;

void onBleCommand(const String& command, uint16_t id, uint32_t epoch) {
    if (command.length() >= JSON_BUFFER_SIZE) {
        ble.sendNotification("{\"error\":\"COMMAND_TOO_LONG\"}", id);
        return;
    }
    PendingBleCommand pending = {};
    pending.id = id; pending.epoch = epoch;
    command.toCharArray(pending.json, sizeof(pending.json));
    if (commandQueue == nullptr || xQueueSend(commandQueue, &pending, 0) != pdTRUE) {
        ble.sendNotification("{\"error\":\"COMMAND_QUEUE_FULL\"}", id);
    }
}

void sendBatteryStatus(const char* reason) {
    String batMsg = "{\"bat\":" + String(battery.getPercentage())
                  + ",\"v\":" + String(battery.getVoltage(), 2) + "}";

    if (ble.isConnected()) {
        ble.sendNotification(batMsg);
    }
}

void setup() {
    Serial.begin(DEBUG_SERIAL_BAUD);
    delay(2000);
    Serial.println("\n==========================================");
    Serial.println("  PEMF Prototipo - transporte BLE v1");
    Serial.println("==========================================");

    storage.begin();

    magneto.begin();
    battery.begin();

    // Arranque determinista: perfil PWM de banco, salida detenida.
    magneto.loadPreset(PRESET_CUSTOM);
    magneto.stopTreatment();

    Serial.println("[PRESETS] Disponibles via BLE ({\"cmd\":\"preset\",\"id\":N}):");
    for (int i = 0; i < PRESET_COUNT; i++) {
        Serial.printf(
            "  ID %d | %-28s | %3lu Hz | %.2f%% | %3d min | %s\n",
            PRESETS[i].id,
            PRESETS[i].name,
            PRESETS[i].frequency,
            PRESETS[i].dutyPct,
            PRESETS[i].sessionMinutes,
            PRESETS[i].evidence
        );
    }

    // El callback sólo encola; loop() valida y aplica las órdenes sin competir
    // con el temporizador de sesión ni con las actualizaciones de los canales.
    commandQueue = xQueueCreate(8, sizeof(PendingBleCommand));
    if (commandQueue == nullptr) {
        Serial.println("[SYSTEM] No se pudo crear la cola de comandos. BLE no iniciado.");
        return;
    }
    ble.begin(DEVICE_NAME, onBleCommand);

    Serial.printf(
        "[BAT] Config GPIO34 / ADC1_CH6 | rango %.2f-%.2f V del banco 4S\n",
        BAT_MIN_VOLT,
        BAT_MAX_VOLT
    );
    Serial.println("[SYSTEM] Setup completo. Esperando BLE...");
}

void loop() {
    magneto.update();
    battery.update();
    if (ble.takeDisconnect()) {
        magneto.stopTreatment("disconnected");
        if (commandQueue) xQueueReset(commandQueue);
    }
    ble.update();
    // Alta/revocacion de telefonos solo mediante acceso fisico USB en reposo.
    static String console;
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') {
            console.trim();
            if (magneto.getState()==SystemState::IDLE) {
                if (console=="PAIR") ble.openPairing();
                else if (console=="UNPAIR") ble.clearBonds();
            }
            console="";
        } else if (console.length()<16) console+=c;
    }
    PendingBleCommand pending;
    if (commandQueue != nullptr && xQueueReceive(commandQueue, &pending, 0) == pdTRUE) {
        if (ble.isConnected() && pending.epoch == ble.epoch()) {
            ble.replyTo(pending.id);
            cmdHandler.handle(String(pending.json));
            ble.replyTo(0);
        }
    }

    // Detectar conexion / desconexion BLE
    static bool lastConnected = false;
    if (ble.isConnected() && !lastConnected) {
        Serial.println("\n[BLE] App conectada. Listo para recibir comandos.");
        bleConnectedAt = millis();
        pendingBatteryPushAfterConnect = true;
        lastConnected = true;
    } else if (!ble.isConnected() && lastConnected) {
        Serial.println("\n[BLE] App desconectada.");
        pendingBatteryPushAfterConnect = false;
        lastConnected = false;
    }

    // Seguridad: bateria critica -> apagar bobinas
    if (battery.isEmpty() && magneto.getState() == SystemState::RUNNING) {
        Serial.println("[SAFETY] Bateria critica (<5%). Auto-apagado.");
        magneto.stopTreatment("low_battery");
        String msg = "{\"error\":\"LOW_BAT\",\"action\":\"auto_stop\",\"bat\":"
                   + String(battery.getPercentage())
                   + ",\"v\":" + String(battery.getVoltage(), 2) + "}";
        ble.sendNotification(msg);
    }

    // Enviar bateria poco despues de conectar la app, para no esperar al intervalo.
    if (pendingBatteryPushAfterConnect &&
        ble.isConnected() &&
        millis() - bleConnectedAt >= 2000) {
        sendBatteryStatus("post_connect");
        lastBatReport = millis();
        pendingBatteryPushAfterConnect = false;
    }

    // Envio periodico de bateria por BLE.
    if (millis() - lastBatReport >= BAT_SEND_INTERVAL_MS) {
        lastBatReport = millis();
        sendBatteryStatus("periodic");
    }

    // Estado completo cada segundo, incluidos PAUSED e IDLE.
    static unsigned long lastStatsReport = 0;
    if (millis()-lastStatsReport >= 1000) {
        lastStatsReport=millis();
        if (ble.isConnected()) ble.sendNotification(magneto.buildStatusString());
    }

    // Estado resumido en Serial cada 10 s si hay tratamiento activo
    static unsigned long lastSerialStatus = 0;
    if (magneto.getState() == SystemState::RUNNING &&
        millis() - lastSerialStatus >= 10000) {
        lastSerialStatus = millis();
        TreatmentStatus tStatus = magneto.getTreatmentStatus();
        unsigned long elapsed = (tStatus.totalDurationMs > tStatus.remainingMs)
            ? (tStatus.totalDurationMs - tStatus.remainingMs) / 1000UL
            : 0;
        unsigned long remaining = tStatus.remainingMs / 1000UL;
        MagnetoChannelParams p = magneto.getChannelParams(0);

        int elapsedMin = elapsed / 60;
        int elapsedSec = elapsed % 60;
        int remainMin = remaining / 60;
        int remainSec = remaining % 60;

        Serial.printf(
            "[RUN] %02d:%02d / %02d:%02d restante | %lu Hz | %.1f%% | GPIO%d\n",
            elapsedMin,
            elapsedSec,
            remainMin,
            remainSec,
            p.frequency,
            duty16ToPct(p.duty),
            PWM_PINS[0].pin
        );
    }

    // Cede CPU sin bloquear timers/BLE. El gating BURST ya no depende del loop().
    delay(0);
}
