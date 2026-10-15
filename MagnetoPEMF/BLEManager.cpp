#include "BLEManager.h"
#include <esp_system.h>
#include <esp_gap_ble_api.h>
#include <BLE2902.h>

BLEManager* BLEManager::instance = nullptr;
BLEManager::BLEManager() { instance = this; }
bool BLEManager::pairingOpen() const {
    return _pairUntil != 0 && int32_t(_pairUntil - millis()) > 0;
}
bool BLEManager::bonded(const uint8_t* peer) const {
    int count = esp_ble_get_bond_device_num();
    if (count <= 0 || count > 20) return false;
    esp_ble_bond_dev_t list[20];
    if (esp_ble_get_bond_device_list(&count, list) != ESP_OK) return false;
    for (int i=0; i<count; ++i) if (memcmp(peer,list[i].bd_addr,6)==0) return true;
    return false;
}
void BLEManager::begin(const String& name, OnCommandReceived cb) {
    _callback = cb;
    _txQueue = xQueueCreate(8, sizeof(Message));
    if (!_txQueue) return;
    BLEDevice::init(name.c_str());
    BLEDevice::setMTU(512);
    BLEDevice::setSecurityCallbacks(new SecurityCallbacks());
    BLESecurity::setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
    BLESecurity::setCapability(ESP_IO_CAP_OUT);
    BLESecurity::setKeySize(16);
    BLESecurity::setInitEncryptionKey();
    BLESecurity::setRespEncryptionKey();
    _server = BLEDevice::createServer();
    _server->setCallbacks(new ServerCallbacks());
    BLEService* service = _server->createService(SERVICE_UUID);
    _characteristic = service->createCharacteristic(CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY);
    _characteristic->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM | ESP_GATT_PERM_WRITE_ENC_MITM);
    _characteristic->setCallbacks(new CharacteristicCallbacks());
    auto* cccd = new BLE2902();
    cccd->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM | ESP_GATT_PERM_WRITE_ENC_MITM);
    _characteristic->addDescriptor(cccd);
    service->start();
    auto* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(SERVICE_UUID);
    adv->setScanResponse(true);
    adv->start();
    Serial.println("[BLE] Use PAIR por USB en reposo para autorizar un telefono; UNPAIR revoca los vinculos.");
}
void BLEManager::openPairing() {
    if (!_server || _connected) return;
    _passkey = esp_random() % 1000000;
    BLESecurity::setPassKey(true, _passkey);
    _pairUntil = millis() + 60000;
    Serial.printf("[PAIR] Ventana 60 s. Introduzca en el telefono el PIN %06lu\n", _passkey);
}
void BLEManager::clearBonds() {
    if (!_server || _connected) return;
    int count = esp_ble_get_bond_device_num();
    esp_ble_bond_dev_t list[20];
    if (count > 0 && count <= 20 && esp_ble_get_bond_device_list(&count,list)==ESP_OK)
        for(int i=0;i<count;++i) esp_ble_remove_bond_device(list[i].bd_addr);
    _pairUntil = 0;
    Serial.println("[PAIR] Vinculos revocados. Olvide tambien el dispositivo en Android.");
}
bool BLEManager::takeDisconnect() {
    // This event is consumed only by loop; the epoch also invalidates queued commands.
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
    message.id = requestId; message.epoch = _epoch;
    message.size = text.length();
    memcpy(message.data, text.c_str(), message.size);
    // No blocking in control loop. Missing delivery is resolved by a status query.
    xQueueSend(_txQueue, &message, 0);
}
void BLEManager::update() {
    if (!_server) return;
    if (_connected && !_authorized && millis()-_connectedAt > 30000) _server->disconnect(_conn);
    if (!isConnected()) { _sending=false; return; }
    if (!_sending) {
        if (xQueueReceive(_txQueue,&_tx,0)!=pdTRUE) return;
        if (_tx.epoch != _epoch) return;
        _sending=true; _offset=0; _sequence=0;
    }
    if (millis()-_lastTx < 4) return;
    const size_t mtu = _server->getPeerMTU(_conn);
    const size_t packet = min(size_t(509), (mtu >= 23 ? mtu : 23)-3);
    const size_t count = min(packet-BleFrames::HEADER,size_t(_tx.size)-_offset);
    uint8_t frame[509];
    frame[0]=0xA5; frame[1]=1; frame[2]=_tx.id&255; frame[3]=_tx.id>>8;
    frame[4]=_sequence++; frame[5]=(_offset==0 ? 1:0) | (_offset+count==_tx.size ? 2:0);
    memcpy(frame+6,_tx.data+_offset,count);
    _characteristic->setValue(frame,count+6);
    _characteristic->notify();
    _offset+=count; _lastTx=millis();
    if(_offset==_tx.size) _sending=false;
}
void BLEManager::ServerCallbacks::onConnect(BLEServer* server, esp_ble_gatts_cb_param_t* p) {
    auto& b=*instance;
    if (b._connected) { server->disconnect(p->connect.conn_id); return; }
    b._conn=p->connect.conn_id; memcpy(b._peer,p->connect.remote_bda,6);
    b._known=b.bonded(b._peer); b._connected=true; b._authorized=false;
    b._connectedAt=millis(); ++b._epoch; b._rx.reset();
    if (!b._known && !b.pairingOpen()) { server->disconnect(b._conn); return; }
    esp_ble_set_encryption(b._peer,ESP_BLE_SEC_ENCRYPT_MITM);
}
void BLEManager::ServerCallbacks::onDisconnect(BLEServer* server, esp_ble_gatts_cb_param_t* p) {
    auto& b=*instance;
    if(p->disconnect.conn_id!=b._conn) return;
    b._connected=false; b._authorized=false; ++b._epoch;
    b._rx.reset(); b._disconnected=true;
    server->startAdvertising();
}
void BLEManager::CharacteristicCallbacks::onWrite(BLECharacteristic* ch) {
    auto& b=*instance;
    if(!b.isConnected()) return;
    String value=ch->getValue();
    auto result=b._rx.accept(reinterpret_cast<const uint8_t*>(value.c_str()), value.length(), millis());
    if(result!=BleFrames::Result::Complete) return;
    if(b._rx.id==0 || b._rx.size>=JSON_BUFFER_SIZE || memchr(b._rx.data,0,b._rx.size)) return;
    if(b._callback) b._callback(String(reinterpret_cast<char*>(b._rx.data)),b._rx.id,b._epoch);
}
uint32_t BLEManager::SecurityCallbacks::onPassKeyRequest() { return instance->_passkey; }
void BLEManager::SecurityCallbacks::onPassKeyNotify(uint32_t pin) {
    if(instance->pairingOpen()) Serial.printf("[PAIR] PIN %06lu\n",pin);
}
bool BLEManager::SecurityCallbacks::onSecurityRequest() {
    return instance->_known || instance->pairingOpen();
}
void BLEManager::SecurityCallbacks::onAuthenticationComplete(esp_ble_auth_cmpl_t result) {
    auto& b=*instance;
    if(!b._connected || memcmp(result.bd_addr,b._peer,6)!=0) return;
    const bool strong=(result.auth_mode & ESP_LE_AUTH_REQ_SC_MITM_BOND)==ESP_LE_AUTH_REQ_SC_MITM_BOND;
    b._authorized=result.success && strong && (b._known || b.pairingOpen());
    if(b._authorized) b._pairUntil=0;
    else b._server->disconnect(b._conn);
}
