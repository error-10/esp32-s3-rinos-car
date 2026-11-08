#pragma once

// Compile-only NimBLE-Arduino stub for the SDL2 desktop preview.
// The preview never scans or connects; all calls are safe no-ops.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

class NimBLEUUID {
public:
    NimBLEUUID() = default;
    explicit NimBLEUUID(uint16_t v) : _v(v) {}
    uint16_t value() const { return _v; }
private:
    uint16_t _v = 0;
};

class NimBLEAddress {
public:
    NimBLEAddress() = default;
    explicit NimBLEAddress(const char* s) : _s(s ? s : "") {}
    NimBLEAddress(uint8_t address[6], uint8_t type = 0) : _type(type) {
        for (int i = 0; i < 6; ++i) _raw[i] = address[i];
        char buf[18];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 _raw[0], _raw[1], _raw[2], _raw[3], _raw[4], _raw[5]);
        _s = buf;
    }
    std::string toString() const { return _s.empty() ? "00:00:00:00:00:00" : _s; }
    const uint8_t* getNative() const { return _raw; }
    uint8_t getType() const { return _type; }
    bool operator==(const NimBLEAddress& other) const { return toString() == other.toString(); }
    bool operator!=(const NimBLEAddress& other) const { return !(*this == other); }
private:
    std::string _s;
    uint8_t _raw[6] = {0};
    uint8_t _type = 0;
};

class NimBLEAdvertisedDevice {
public:
    NimBLEAddress getAddress() const { return NimBLEAddress(); }
    int getRSSI() const { return -127; }
    bool haveName() const { return false; }
    std::string getName() const { return std::string(); }
    bool haveServiceUUID() const { return false; }
    bool isAdvertisingService(const NimBLEUUID&) const { return false; }
    bool haveManufacturerData() const { return false; }
    std::string getManufacturerData() const { return std::string(); }
};

class NimBLEAdvertisedDeviceCallbacks {
public:
    virtual ~NimBLEAdvertisedDeviceCallbacks() = default;
    virtual void onResult(NimBLEAdvertisedDevice*) {}
};

class NimBLEScanResults {
public:
    int getCount() const { return 0; }
};

class NimBLERemoteCharacteristic {
public:
    bool canNotify() const { return false; }
    bool canWrite() const { return false; }
    bool canWriteNoResponse() const { return false; }
    bool subscribe(bool, void (*)(NimBLERemoteCharacteristic*, uint8_t*, size_t, bool)) { return true; }
    void writeValue(const uint8_t*, size_t, bool = false) {}
};

class NimBLERemoteService {
public:
    std::vector<NimBLERemoteCharacteristic*>* getCharacteristics(bool = false) { return nullptr; }
};

class NimBLEClient;

class NimBLEClientCallbacks {
public:
    virtual ~NimBLEClientCallbacks() = default;
    virtual void onConnect(NimBLEClient*) {}
    virtual void onDisconnect(NimBLEClient*) {}
};

class NimBLEClient {
public:
    void setClientCallbacks(NimBLEClientCallbacks*) {}
    void setConnectTimeout(uint32_t) {}
    bool connect(const NimBLEAddress&, bool = false) { return false; }
    void disconnect() {}
    bool isConnected() const { return false; }
    NimBLERemoteService* getService(const NimBLEUUID&) { return nullptr; }
    std::vector<NimBLERemoteService*>* getServices(bool = false) { return nullptr; }
    int getConnId() const { return -1; }
    int getLastError() const { return 0; }
};

class NimBLEScan {
public:
    typedef void (*ScanCompleteCallback)(NimBLEScanResults);
    bool isScanning() const { return false; }
    void stop() {}
    void setAdvertisedDeviceCallbacks(NimBLEAdvertisedDeviceCallbacks*, bool = false) {}
    void setActiveScan(bool) {}
    void setDuplicateFilter(bool) {}
    void setInterval(uint16_t) {}
    void setWindow(uint16_t) {}
    void clearResults() {}
    bool start(uint32_t, ScanCompleteCallback = nullptr, bool = false) { return true; }
};

class NimBLEDevice {
public:
    static void init(const char*) {}
    static void setPower(int, int) {}
    static NimBLEScan* getScan() { static NimBLEScan scan; return &scan; }
    static NimBLEClient* createClient() { static NimBLEClient client; return &client; }
};

enum {
    ESP_PWR_LVL_P9 = 9,
    ESP_BLE_PWR_TYPE_DEFAULT = 0,
    ESP_BLE_PWR_TYPE_ADV = 1,
    ESP_BLE_PWR_TYPE_SCAN = 2
};
