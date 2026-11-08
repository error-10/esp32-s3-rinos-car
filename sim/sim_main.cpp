// PC SDL2 entry point for the interactive dashboard preview.
// Reuses the real firmware UI code in src/main.cpp through the SIM_BUILD shims.

#define SIM_BUILD

#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

#include "../src/main.cpp"

#ifdef _WIN32
extern "C" __declspec(dllimport) short __stdcall GetAsyncKeyState(int vKey);
#define SIM_VK_F1 0x70
#define SIM_VK_F2 0x71
#define SIM_VK_F3 0x72
#define SIM_VK_UP 0x26
#define SIM_VK_DOWN 0x28
#define SIM_VK_LEFT 0x25
#define SIM_VK_RIGHT 0x27
#endif

// Definitions for the Arduino globals declared in sim/include/Arduino.h
SerialClass Serial;
SerialClass Serial1;
EspClass ESP;

static void pollHotkeys() {
    static bool lastF1 = false;
    static bool lastF2 = false;
    static bool lastF3 = false;
#ifdef _WIN32
    bool f1 = (GetAsyncKeyState(SIM_VK_F1) & 0x8000) != 0;
    bool f2 = (GetAsyncKeyState(SIM_VK_F2) & 0x8000) != 0;
    bool f3 = (GetAsyncKeyState(SIM_VK_F3) & 0x8000) != 0;
#else
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    bool f1 = keys[SDL_SCANCODE_F1] != 0;
    bool f2 = keys[SDL_SCANCODE_F2] != 0;
    bool f3 = keys[SDL_SCANCODE_F3] != 0;
#endif
    if (f1 != lastF1) {
        f1 ? lgfx::gpio_lo(40) : lgfx::gpio_hi(40);
        lastF1 = f1;
    }
    if (f2 != lastF2) {
        f2 ? lgfx::gpio_lo(41) : lgfx::gpio_hi(41);
        lastF2 = f2;
    }
    if (f3 != lastF3) {
        f3 ? lgfx::gpio_lo(42) : lgfx::gpio_hi(42);
        lastF3 = f3;
    }
}

int simHostRawKey() {
#ifdef _WIN32
    if (GetAsyncKeyState(SIM_VK_UP) & 0x8000) return 1675;    // K1
    if (GetAsyncKeyState(SIM_VK_RIGHT) & 0x8000) return 2405; // K2
    if (GetAsyncKeyState(SIM_VK_LEFT) & 0x8000) return 3200;  // K3
    if (GetAsyncKeyState(SIM_VK_DOWN) & 0x8000) return 4095;  // K4
#endif
    return 770;
}

static int user_func(bool* running) {
    setup();
    do {
        pollHotkeys();
        loop();
    } while (*running);
    return 0;
}

int main(int, char**) {
    lgfx::Panel_sdl::addKeyCodeMapping(SDLK_F1, 40);
    lgfx::Panel_sdl::addKeyCodeMapping(SDLK_F2, 41);
    return lgfx::Panel_sdl::main(user_func);
}
