// Bluetooth LE peripheral (NimBLE). One encrypted, bonded connection from
// the OpenRouter Meter app; messages arrive as writes to the DATA
// characteristic and are queued for the main loop.
#ifndef SIMULATOR
#include "link.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>

#define SVC_UUID  "589b0001-eb3f-4774-98f5-45672a450cf3"
#define DATA_UUID "589b0002-eb3f-4774-98f5-45672a450cf3"
#define REQ_UUID  "589b0003-eb3f-4774-98f5-45672a450cf3"
#define DEVICE_NAME "OpenRouter Meter"

namespace {

constexpr int MSG_MAX = 512;
constexpr int QUEUE_LEN = 8;

// Single-producer (NimBLE host task) / single-consumer (loop) ring buffer.
char q_buf[QUEUE_LEN][MSG_MAX];
volatile uint8_t q_head = 0, q_tail = 0;

NimBLEServer*         server = nullptr;
NimBLECharacteristic* req_chr = nullptr;
NimBLECharacteristic* batt_chr = nullptr;
volatile bool connected = false;
volatile bool restart_adv = false;
// New pairings are accepted only while unpaired, or for 2 minutes after the
// PWR-hold "pair a new PC" gesture — a paired meter can't be hijacked by
// another PC in range.
volatile bool     pairing_open = false;
volatile uint32_t pairing_open_until = 0;
volatile int      bonds_at_connect = 0;

void enqueue(const std::string& v) {
    uint8_t next = (uint8_t)((q_head + 1) % QUEUE_LEN);
    if (next == q_tail) q_tail = (uint8_t)((q_tail + 1) % QUEUE_LEN);   // drop oldest
    size_t n = v.size() < (size_t)MSG_MAX - 1 ? v.size() : (size_t)MSG_MAX - 1;
    memcpy(q_buf[q_head], v.data(), n);
    q_buf[q_head][n] = 0;
    q_head = next;
}

class ServerEvents : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
        connected = true;
        bonds_at_connect = NimBLEDevice::getNumBonds();
        // Ask for a calmer interval once settled: 30-50 ms, 4 s supervision.
        s->updateConnParams(info.getConnHandle(), 24, 40, 0, 400);
    }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override {
        connected = false;
        restart_adv = true;
        Serial.printf("[link] disconnected (reason %d)\n", reason);
    }
    void onAuthenticationComplete(NimBLEConnInfo& info) override {
        if (!info.isEncrypted()) {
            Serial.println("[link] pairing failed — dropping link");
            NimBLEDevice::getServer()->disconnect(info.getConnHandle());
            return;
        }
        bool new_bond = NimBLEDevice::getNumBonds() > bonds_at_connect;
        bool allowed = bonds_at_connect == 0 ||
                       (pairing_open && (int32_t)(pairing_open_until - millis()) > 0);
        if (new_bond && !allowed) {
            Serial.println("[link] refusing a new PC (hold PWR 3 s to allow pairing)");
            NimBLEDevice::deleteBond(info.getIdAddress());
            NimBLEDevice::getServer()->disconnect(info.getConnHandle());
            return;
        }
        if (new_bond) {
            pairing_open = false;
            Serial.println("[link] paired with a new PC");
        }
    }
};

class DataEvents : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
        if (!info.isEncrypted()) return;          // WRITE_ENC should prevent this
        enqueue(c->getValue());
    }
};

class ReqEvents : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t sub) override {
        if (sub) link_request_refresh();          // fresh data right after connect
    }
};

void advertise(void) {
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->stop();
    NimBLEAdvertisementData data, scan;
    data.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    data.addServiceUUID(NimBLEUUID(SVC_UUID));
    scan.setName(DEVICE_NAME);
    adv->setAdvertisementData(data);
    adv->setScanResponseData(scan);
    adv->start();
}

}  // namespace

void link_begin(void) {
    NimBLEDevice::init(DEVICE_NAME);
    NimBLEDevice::setPower(3);                               // dBm
    NimBLEDevice::setSecurityAuth(true, false, true);        // bond, no MITM, secure connections
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);   // "Just Works" pairing
    NimBLEDevice::setMTU(247);

    server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerEvents());

    NimBLEService* svc = server->createService(SVC_UUID);
    NimBLECharacteristic* data = svc->createCharacteristic(
        DATA_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC, MSG_MAX);
    data->setCallbacks(new DataEvents());
    req_chr = svc->createCharacteristic(
        REQ_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC);
    req_chr->setCallbacks(new ReqEvents());
    svc->start();

    NimBLEService* bas = server->createService(NimBLEUUID((uint16_t)0x180F));
    batt_chr = bas->createCharacteristic(NimBLEUUID((uint16_t)0x2A19),
                                         NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    uint8_t lvl = 100;
    batt_chr->setValue(&lvl, 1);
    bas->start();

    advertise();
    Serial.printf("[link] advertising as \"%s\" (%d paired PC)\n", DEVICE_NAME, NimBLEDevice::getNumBonds());
}

void link_tick(void) {
    if (restart_adv) {
        restart_adv = false;
        advertise();
    }
}

LinkState link_state(void) { return connected ? LINK_CONNECTED : LINK_ADVERTISING; }

bool link_has_bond(void) { return NimBLEDevice::getNumBonds() > 0; }

void link_forget_pairings(void) {
    if (server && connected) {
        for (uint16_t h : server->getPeerDevices()) server->disconnect(h);
    }
    NimBLEDevice::deleteAllBonds();
    pairing_open = true;
    pairing_open_until = millis() + 120000;
    Serial.println("[link] pairings cleared — ready to pair a new PC");
    restart_adv = true;
}

bool link_next_message(char* out, int cap) {
    if (q_head == q_tail) return false;
    strncpy(out, q_buf[q_tail], cap - 1);
    out[cap - 1] = 0;
    q_tail = (uint8_t)((q_tail + 1) % QUEUE_LEN);
    return true;
}

void link_request_refresh(void) {
    if (!req_chr || !connected) return;
    req_chr->setValue("r");
    req_chr->notify();
}

void link_set_battery(int percent) {
    if (!batt_chr || percent < 0) return;
    uint8_t v = (uint8_t)(percent > 100 ? 100 : percent);
    batt_chr->setValue(&v, 1);
    if (connected) batt_chr->notify();
}

#endif  // !SIMULATOR
