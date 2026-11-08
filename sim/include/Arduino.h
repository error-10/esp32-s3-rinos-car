#pragma once

// Minimal Arduino compatibility layer for the LovyanGFX SDL2 desktop preview.
// It intentionally implements only the APIs used by src/main.cpp.

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <SDL.h>

#define INPUT          0
#define OUTPUT         1
#define INPUT_PULLUP   2
#define SERIAL_8N1     0
#define ADC_11db       3

typedef uint8_t byte;

class String {
public:
    String() = default;
    String(const char* s) : _s(s ? s : "") {}
    String(const std::string& s) : _s(s) {}

    const char* c_str() const { return _s.c_str(); }
    unsigned int length() const { return (unsigned int)_s.size(); }
    bool isEmpty() const { return _s.empty(); }

    void toLowerCase() {
        std::transform(_s.begin(), _s.end(), _s.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
    }

    int indexOf(const char* needle) const {
        if (!needle) return -1;
        size_t p = _s.find(needle);
        return p == std::string::npos ? -1 : (int)p;
    }

    String substring(unsigned int from, unsigned int to) const {
        if (from > _s.size()) return String();
        if (to > _s.size()) to = (unsigned int)_s.size();
        if (to < from) return String();
        return String(_s.substr(from, to - from));
    }

    String substring(unsigned int from) const {
        return substring(from, (unsigned int)_s.size());
    }

    bool equalsIgnoreCase(const char* other) const {
        if (!other) return false;
        std::string a = _s;
        std::string b(other);
        std::transform(a.begin(), a.end(), a.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        std::transform(b.begin(), b.end(), b.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        return a == b;
    }

    bool startsWith(const char* prefix) const {
        return prefix && _s.rfind(prefix, 0) == 0;
    }

    String& operator=(const char* s) { _s = s ? s : ""; return *this; }
    String& operator=(const String& other) = default;
    String& operator+=(const char* s) { if (s) _s += s; return *this; }
    String& operator+=(const String& s) { _s += s._s; return *this; }

    operator const char*() const { return _s.c_str(); }

    bool operator==(const char* s) const { return _s == (s ? s : ""); }
    bool operator!=(const char* s) const { return !(*this == s); }

private:
    std::string _s;
};

inline String operator+(const String& a, const char* b) {
    String r(a);
    r += b;
    return r;
}
inline String operator+(const String& a, const String& b) {
    String r(a);
    r += b;
    return r;
}
inline bool operator==(const char* a, const String& b) { return b == a; }

class SerialClass {
public:
    void begin(unsigned long) {}
    void begin(unsigned long, int, int, int) {}
    void end() {}
    void setRxBufferSize(size_t) {}
    int available() { return 0; }
    int read() { return -1; }
    void flush() { std::fflush(stdout); }

    void print(const char* s) { if (s) std::fputs(s, stdout); }
    void print(const String& s) { std::fputs(s.c_str(), stdout); }
    void print(int v) { std::printf("%d", v); }
    void print(unsigned int v) { std::printf("%u", v); }

    void println() { std::fputs("\n", stdout); }
    void println(const char* s) { if (s) std::fputs(s, stdout); std::fputc('\n', stdout); }
    void println(const String& s) { std::fputs(s.c_str(), stdout); std::fputc('\n', stdout); }
    void println(int v) { std::printf("%d\n", v); }
    void println(unsigned int v) { std::printf("%u\n", v); }

    int printf(const char* fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        int n = std::vprintf(fmt, ap);
        va_end(ap);
        return n;
    }
};

extern SerialClass Serial;
extern SerialClass Serial1;

// main.cpp reads the ADC key ladder. The SDL backend maps arrow keys to
// emulated GPIO 36/37/38/39; simHostRawKey() maps the host keyboard directly.
int simReadKeyAdc();
int simHostRawKey();

inline int analogRead(uint8_t pin) {
    // scanKeypad() takes exactly 16 samples per scan. Cache one host key
    // value for the whole scan so a key transition cannot produce a mixed
    // resistor-ladder average (e.g. 770/4095 -> 2432 -> phantom K2).
    static int cached = 770;
    static uint8_t sampleIndex = 0;
    if (pin != 8) return 0;
    if (sampleIndex == 0) cached = simReadKeyAdc();
    sampleIndex = (sampleIndex + 1) & 0x0F;
    return cached;
}
inline void analogReadResolution(int) {}
inline void analogSetAttenuation(int) {}
inline void pinMode(uint8_t, uint8_t) {}
inline void delay(unsigned long ms) { SDL_Delay((Uint32)ms); }
inline void delayMicroseconds(unsigned int us) { SDL_Delay((us + 999u) / 1000u); }
inline uint32_t millis() { return (uint32_t)SDL_GetTicks(); }
inline uint32_t micros() { return (uint32_t)SDL_GetTicks() * 1000u; }

class EspClass {
public:
    uint32_t getCpuFreqMHz() const { return 240; }
    uint32_t getFreeHeap() const { return 300000; }
};
extern EspClass ESP;

typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) do { (void)(mux); } while (0)
#define portEXIT_CRITICAL(mux)  do { (void)(mux); } while (0)

[[noreturn]] inline void esp_restart() { std::exit(0); }
