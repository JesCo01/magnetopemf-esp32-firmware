#include "BLEManager.h"
#include <esp_system.h>
#include <esp_gap_ble_api.h>
#include <BLE2902.h>

BLEManager* BLEManager::instance = nullptr;

BLEManager::BLEManager() {
    instance = this;
}

void BLEManager::begin(const String& name, OnCommandReceived cb) {
    _callback = cb;
    _txQueue = xQueueCreate(8, sizeof(Message));
    if (!_txQueue) return;

    BLEDevice::init(name.c_str());
    BLEDevice::setMTU(512);

    _server = BLEDevice::createServer();
    _server->setCallbacks(new ServerCallbacks());

    BLEService* service = _server->createService(SERVICE_UUID);
    _characteristic = service->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    _characteristic->setAccessPermissions(ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE);
    _characteristic->setCallbacks(new CharacteristicCallbacks());

    auto* cccd = new BLE2902();
    cccd->setAccessPermissions(ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE);
    _characteristic->addDescriptor(cccd);

    service->start();

    auto* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(SERVICE_UUID);
    adv->setScanResponse(true);
    adv->start();

    Serial.println("[BLE] Servidor BLE iniciado. Enlace inalambrico directo activo.");
}

bool BLEManager::takeDisconnect() {
    if (!_disconnected) return false;
    _disconnected = false;
    _sending = false;
    return true;
}

void BLEManager::sendNotification(const String& text) {
    sendNotification(text, _replyId);
}

void BLEManager::sendNotification(const String& text, uint16_t requestId) {
    if (!isConnected() || !_txQueue || text.length() > BleFrames::MAX_MESSAGE) return;
    Message message{};
    message.id = requestId;
    message.epoch = _epoch;
    message.size = text.length();
    memcpy(message.data, text.c_str(), message.size);
    xQueueSend(_txQueue, &message, 0);
}

void BLEManager::update() {
    if (!_server) return;
    if (!isConnected()) {
        _sending = false;
        return;
    }
    if (!_sending) {
        if (xQueueReceive(_txQueue, &_tx, 0) != pdTRUE) return;
        if (_tx.epoch != _epoch) return;
        _sending = true;
        _offset = 0;
        _sequence = 0;
    }
    if (millis() - _lastTx < 4) return;

    const size_t mtu = _server->getPeerMTU(_conn);
    const size_t packet = min(size_t(509), (mtu >= 23 ? mtu : 23) - 3);
    const size_t count = min(packet - BleFrames::HEADER, size_t(_tx.size) - _offset);

    uint8_t frame[509];
    frame[0] = 0xA5;
    frame[1] = 1;
    frame[2] = _tx.id & 255;
    frame[3] = _tx.id >> 8;
    frame[4] = _sequence++;
    frame[5] = (_offset == 0 ? 1 : 0) | (_offset + count == _tx.size ? 2 : 0);
    memcpy(frame + 6, _tx.data + _offset, count);

    _characteristic->setValue(frame, count + 6);
    _characteristic->notify();

    _offset += count;
    _lastTx = millis();
    if (_offset == _tx.size) _sending = false;
}

void BLEManager::ServerCallbacks::onConnect(BLEServer* server, esp_ble_gatts_cb_param_t* p) {
    auto& b = *instance;
    b._conn = p->connect.conn_id;
    memcpy(b._peer, p->connect.remote_bda, 6);
    b._connected = true;
    b._connectedAt = millis();
    ++b._epoch;
    b._rx.reset();
    Serial.printf("[BLE] Dispositivo conectado: conn_id=%d\n", b._conn);
}

void BLEManager::ServerCallbacks::onDisconnect(BLEServer* server, esp_ble_gatts_cb_param_t* p) {
    auto& b = *instance;
    if (p->disconnect.conn_id != b._conn) return;
    b._connected = false;
    ++b._epoch;
    b._rx.reset();
    b._disconnected = true;
    Serial.println("[BLE] Dispositivo desconectado. Reiniciando publicidad BLE...");
    server->startAdvertising();
}

void BLEManager::CharacteristicCallbacks::onWrite(BLECharacteristic* ch) {
    auto& b = *instance;
    if (!b.isConnected()) return;
    String value = ch->getValue();
    auto result = b._rx.accept(reinterpret_cast<const uint8_t*>(value.c_str()), value.length(), millis());
    if (result != BleFrames::Result::Complete) return;
    if (b._rx.id == 0 || b._rx.size >= JSON_BUFFER_SIZE || memchr(b._rx.data, 0, b._rx.size)) return;
    if (b._callback) b._callback(String(reinterpret_cast<char*>(b._rx.data)), b._rx.id, b._epoch);
}
