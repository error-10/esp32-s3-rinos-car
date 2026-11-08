#pragma once

// In-memory Preferences implementation for the SDL2 preview.
// Values live only for the duration of the process; no flash is touched.

#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>

#include "Arduino.h"

class Preferences {
public:
    bool begin(const char* name, bool readOnly = false) {
        (void)readOnly;
        _namespace = name ? name : "";
        return true;
    }

    void end() {}

    float getFloat(const char* key, float defaultValue = 0.0f) {
        auto it = _data.find(fullKey(key));
        if (it == _data.end()) return defaultValue;
        return std::strtof(it->second.c_str(), nullptr);
    }

    uint32_t getUInt(const char* key, uint32_t defaultValue = 0) {
        auto it = _data.find(fullKey(key));
        if (it == _data.end()) return defaultValue;
        return (uint32_t)std::strtoul(it->second.c_str(), nullptr, 10);
    }

    bool getBool(const char* key, bool defaultValue = false) {
        auto it = _data.find(fullKey(key));
        if (it == _data.end()) return defaultValue;
        return it->second != "0" && it->second != "false";
    }

    String getString(const char* key, const char* defaultValue = "") {
        auto it = _data.find(fullKey(key));
        if (it == _data.end()) return String(defaultValue);
        return String(it->second);
    }

    size_t putFloat(const char* key, float value) {
        _data[fullKey(key)] = std::to_string(value);
        return sizeof(float);
    }

    size_t putUInt(const char* key, uint32_t value) {
        _data[fullKey(key)] = std::to_string(value);
        return sizeof(uint32_t);
    }

    size_t putBool(const char* key, bool value) {
        _data[fullKey(key)] = value ? "1" : "0";
        return 1;
    }

    size_t putString(const char* key, const String& value) {
        _data[fullKey(key)] = value.c_str();
        return value.length();
    }

    size_t putString(const char* key, const char* value) {
        std::string v = value ? value : "";
        _data[fullKey(key)] = v;
        return v.size();
    }

    bool remove(const char* key) {
        return _data.erase(fullKey(key)) > 0;
    }

    bool isKey(const char* key) {
        return _data.find(fullKey(key)) != _data.end();
    }

private:
    std::string fullKey(const char* key) const {
        return _namespace + "/" + (key ? key : "");
    }

    std::string _namespace;
    std::map<std::string, std::string> _data;
};
