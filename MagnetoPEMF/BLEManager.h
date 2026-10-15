#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Config.h"
#include "BleFrames.h"

class BLEManager {
public:
    typedef void (*OnCommandReceived)(const String&, uint16_t, uint32_t);
    BLEManager();
    void begin(const String&, OnCommandReceived);
    void update();
    void sendNotification(const String&);
    void sendNotification(const String&, uint16_t requestId);
    bool isConnected() const { return _connected; }
    uint32_t epoch() const { return _epoch; }
    bool takeDisconnect();
    void replyTo(uint16_t id) { _replyId = id; }
    void openPairing() { Serial.println("[BLE] Enlace inalámbrico directo activo."); }
    void clearBonds() { Serial.println("[BLE] Enlace inalámbrico directo activo."); }

private:
    struct Message { uint16_t id; uint16_t size; uint32_t epoch; char data[1025]; };
    BLEServer* _server = nullptr;
    BLECharacteristic* _characteristic = nullptr;
    OnCommandReceived _callback = nullptr;
    QueueHandle_t _txQueue = nullptr;
    Message _tx{};
    BleFrames::Receiver _rx;
    volatile bool _connected = false, _disconnected = false;
    volatile uint32_t _epoch = 0;
    uint16_t _conn = 0, _replyId = 0;
    uint8_t _peer[6]{};
    bool _sending = false;
    size_t _offset = 0;
    uint8_t _sequence = 0;
    uint32_t _lastTx = 0, _connectedAt = 0;
    static BLEManager* instance;

    class ServerCallbacks : public BLEServerCallbacks {
        void onConnect(BLEServer*, esp_ble_gatts_cb_param_t*) override;
        void onDisconnect(BLEServer*, esp_ble_gatts_cb_param_t*) override;
    };
    class CharacteristicCallbacks : public BLECharacteristicCallbacks {
        void onWrite(BLECharacteristic*) override;
    };
};

#endif
