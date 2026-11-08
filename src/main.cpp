#include <Arduino.h>
#include <vector>
#include <algorithm>
#include <Preferences.h>
#include <time.h>

#ifndef SIM_BUILD
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#ifdef SIM_BUILD
  #ifndef LGFX_SDL
    #define LGFX_SDL
  #endif
  #ifndef LGFX_AUTODETECT
    #define LGFX_AUTODETECT
  #endif
#endif

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <NimBLEDevice.h>

// ================= 引脚与硬件配�?=================
#define PIN_POWER_ADC   1
#define PIN_KEY_ADC     8
#define PIN_LCD_SCL     12
#define PIN_LCD_SDA     11
#define PIN_LCD_DC      9
#define PIN_LCD_CS      10
#define PIN_LCD_RST     13

// GPS 硬件串口 (HardwareSerial 1)
#define PIN_GPS_RX      17
#define PIN_GPS_TX      18
#define GPS_BAUDRATE    115200
#define GpsSerial       Serial1

// ================= LovyanGFX 驱动配置 (GC9A01 240x240) =================
#ifndef SIM_BUILD
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_GC9A01  _panel_instance;
    lgfx::Bus_SPI       _bus_instance;

public:
    LGFX() {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.spi_3wire  = true;
            cfg.use_lock   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk   = PIN_LCD_SCL;
            cfg.pin_mosi   = PIN_LCD_SDA;
            cfg.pin_miso   = -1;
            cfg.pin_dc     = PIN_LCD_DC;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs           = PIN_LCD_CS;
            cfg.pin_rst          = PIN_LCD_RST;
            cfg.pin_busy         = -1;
            cfg.panel_width      = 240;
            cfg.panel_height     = 240;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable         = false;
            cfg.invert           = true;
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = false;
            _panel_instance.config(cfg);
        }
        setPanel(&_panel_instance);
    }
};
#endif

#ifdef SIM_BUILD
LGFX lcd(240, 240, 2);
#else
LGFX lcd;
#endif
LGFX_Sprite canvas(&lcd);
Preferences prefs;

// ================= 现代化机能风配色定义 (RGB565) =================
#define COLOR_BG            0x0000  // 纯黑底色
#define COLOR_CARD_NORMAL   0x10A2  // 默认暗灰深色卡片
#define COLOR_CARD_SELECT   0x116A  // 选中项幽蓝高亮底�?
#define COLOR_CYAN_ACCENT   0x07FF  // 霓虹电光�?
#define COLOR_BLUE_ACCENT   0x24BD  // 凛OS经典天青�?
#define COLOR_WHITE_TEXT    0xFFFF  // 高对比纯�?
#define COLOR_GRAY_TEXT     0x8410  // 次级�?
#define COLOR_RED_ACCENT    0xFA04  // 极速赛道红
#define COLOR_GREEN_SIGNAL  0x07E0  // 强信号绿
#define COLOR_YELLOW_SIGNAL 0xFDE0  // 中信号黄
#define COLOR_ARC_TRACK     0x18C3  // 弧线底槽�?// 清爽速度环：冰川�?-> 薄荷 -> 暖琥珀
#define COLOR_SPEED_GLACIER 0x673C
#define COLOR_SPEED_MINT    0x9FB9
#define COLOR_SPEED_AMBER   0xFE2D
#define COLOR_SPEED_GLOW    0xFFE0
// 主表盘电量条：翡翠绿但带青灰底，避免纯绿“死绿”；低电量才转珊瑚红
#define COLOR_BATTERY_JADE  0x3D8E  // 翡翠绿
#define COLOR_BATTERY_DEEP  0x1C66  // 深翡翠
#define COLOR_BATTERY_MINT  0x9FB9  // 薄荷高光
#define COLOR_BATTERY_LOW   0xF9A6  // 珊瑚红
#define COLOR_BATTERY_ORANGE 0xFD20 // 末尾能量段的橙色起点
// 功能页统一视觉：深海军蓝底 + 雾青绿强调，和功能菜单同一套语言
#define COLOR_UI_BG         0x0863
#define COLOR_UI_CARD       0x10C4
#define COLOR_UI_CARD_HI    0x1926
#define COLOR_UI_LINE       0x2945
#define COLOR_UI_TEXT       0xCEBD
#define COLOR_UI_MUTED      0x8410
#define COLOR_UI_ACCENT     0x3BED
#define COLOR_UI_ACCENT_DIM 0x224A

// ================= 颜色渐变辅助函数 =================
uint16_t interpolateColor(uint16_t c1, uint16_t c2, float t) {
    if (t <= 0.0f) return c1;
    if (t >= 1.0f) return c2;
    uint8_t r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
    uint8_t r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F;
    uint8_t r = r1 + (r2 - r1) * t;
    uint8_t g = g1 + (g2 - g1) * t;
    uint8_t b = b1 + (b2 - b1) * t;
    return (r << 11) | (g << 5) | b;
}

uint16_t getSpeedArcColor(float speed) {
    if (speed <= 45.0f) {
        return interpolateColor(COLOR_SPEED_GLACIER, COLOR_SPEED_MINT, speed / 45.0f);
    } else if (speed <= 85.0f) {
        return interpolateColor(COLOR_SPEED_MINT, COLOR_SPEED_AMBER, (speed - 45.0f) / 40.0f);
    }
    return COLOR_SPEED_AMBER;
}

uint16_t getBatteryArcColor(int soc) {
    if (soc >= 60) {
        return COLOR_BATTERY_JADE;
    } else if (soc >= 25) {
        return interpolateColor(COLOR_SPEED_AMBER, COLOR_BATTERY_JADE, (soc - 25) / 35.0f);
    } else if (soc >= 12) {
        return interpolateColor(COLOR_BATTERY_LOW, COLOR_SPEED_AMBER, (soc - 12) / 13.0f);
    } else {
        return COLOR_BATTERY_LOW;
    }
}

// 主表盘量条渐变：沿已填充弧长插值，而不是整条只取一个颜色
static inline uint16_t blend565(uint16_t a, uint16_t b, uint8_t t);

uint16_t speedGradientColor(float t, uint16_t headColor) {
    if (t <= 0.5f) {
        return interpolateColor(COLOR_SPEED_GLACIER, COLOR_SPEED_MINT, t / 0.5f);
    }
    return interpolateColor(COLOR_SPEED_MINT, headColor, (t - 0.5f) / 0.5f);
}

uint16_t batteryGradientColor(float t, uint16_t headColor, int soc) {
    // 正常电量恢复翡翠绿同色系渐变；只有低于 20% 才整段转为橙→红。
    if (soc < 20) {
        return interpolateColor(COLOR_BATTERY_ORANGE, COLOR_RED_ACCENT, t);
    }
    uint16_t shaded = blend565(headColor, COLOR_BATTERY_DEEP, (soc < 25) ? 210 : 170);
    return interpolateColor(headColor, shaded, t);
}

uint16_t getDeltaVColor(int deltaV) {
    if (deltaV <= 30) return COLOR_GREEN_SIGNAL;
    if (deltaV <= 60) return COLOR_YELLOW_SIGNAL;
    return COLOR_RED_ACCENT;
}

uint16_t getBatteryTempColor(int temp) {
    if (temp <= 45) return COLOR_GREEN_SIGNAL;
    if (temp <= 55) return COLOR_YELLOW_SIGNAL;
    return COLOR_RED_ACCENT;
}

// ================= 全局统一按键驱动 =================
enum KeyEvent {
    KEY_EVENT_NONE = 0,
    KEY_EVENT_K1,      // 向上 / 切换下半区模�?
    KEY_EVENT_K4,      // 向下 / 切换上半区模�?
    KEY_EVENT_K2,      // 确认 / 桌面唤起功能菜单
    KEY_EVENT_K3,      // 全局返回 / 退�?
    KEY_EVENT_K2_LONG, // K2 长按 1.8 秒：重新进入蓝牙配对向导
    KEY_EVENT_K3_LONG  // K3 长按 5.0 秒：全局卡死紧急强行重启
};

// 全局实时按键 ADC 调试监控变量 (供屏幕与串口实时校准显示)
volatile uint32_t g_lastRawKeyAdc = 0;
volatile uint8_t g_currentDetectedKey = 0;

KeyEvent scanKeypad() {
    static uint32_t pressStartTime = 0;
    static bool isPressing = false;
    static bool longTriggered = false;
    static uint8_t activeKey = 0;
    static uint8_t releaseCounter = 0;

    uint32_t samples[16];
    for (int i = 0; i < 16; i++) {
        samples[i] = analogRead(PIN_KEY_ADC);
        delayMicroseconds(25);
    }
    for (int i = 0; i < 15; i++) {
        for (int j = 0; j < 15 - i; j++) {
            if (samples[j] > samples[j + 1]) {
                uint32_t tmp = samples[j];
                samples[j] = samples[j + 1];
                samples[j + 1] = tmp;
            }
        }
    }
    uint32_t sum = 0;
    for (int i = 4; i < 12; i++) {
        sum += samples[i];
    }
    uint32_t raw = sum / 8;

    g_lastRawKeyAdc = raw;

    uint8_t currentKey = 0;
    if (raw < 1200) {
        currentKey = 0; // 没按 (实测 770，阈�?1200 留足 430 裕度)
    } else if (raw < 2050) {
        currentKey = 1; // K1 (实测 1675，中间�?2050)
    } else if (raw < 2800) {
        currentKey = 2; // K2 (实测 2405，中间�?2800)
    } else if (raw < 3650) {
        currentKey = 3; // K3 (实测 3200，中间�?3650)
    } else {
        currentKey = 4; // K4 (实测 4095)
    }

    g_currentDetectedKey = currentKey;

    // 实时串口校准日志输出：发生按键变化或�?400ms 打印一次当前实�?ADC 采样�?
    static uint32_t s_lastLogTime = 0;
    static uint32_t s_lastLoggedRaw = 0;
    static uint8_t s_lastLoggedKey = 255;
    if ((millis() - s_lastLogTime >= 400) || (currentKey != s_lastLoggedKey) || (abs((int)raw - (int)s_lastLoggedRaw) > 150)) {
        s_lastLogTime = millis();
        s_lastLoggedRaw = raw;
        s_lastLoggedKey = currentKey;
        Serial.printf("[KEY CALIB] ADC Raw: %4u | Volts: %1.2fV | Mapped: K%d | Pressing: %s\n",
                      raw, (float)raw * 3.3f / 4095.0f, currentKey, isPressing ? "YES" : "NO");
    }

    if (currentKey != 0) {
        releaseCounter = 0;
        if (!isPressing) {
            isPressing = true;
            activeKey = currentKey;
            pressStartTime = millis();
            longTriggered = false;
            Serial.printf(">>> [KEY DOWN] K%d 按下 (Raw: %u)\n", currentKey, raw);
        } else {
            if (activeKey == 2 && !longTriggered && (millis() - pressStartTime >= 1800)) {
                longTriggered = true;
                Serial.println(">>> [KEY LONG] K2 长按 1.8s 触发！重新进入配对向导");
                return KEY_EVENT_K2_LONG;
            } else if (activeKey == 3 && !longTriggered && (millis() - pressStartTime >= 5000)) {
                longTriggered = true;
                Serial.println(">>> [EMERGENCY] K3 长按 5s 触发！全局卡死准备重启系统");
                return KEY_EVENT_K3_LONG;
            }
        }
    } else {
        if (isPressing) {
            releaseCounter++;
            if (releaseCounter >= 2) {
                isPressing = false;
                uint32_t duration = millis() - pressStartTime;
                Serial.printf("<<< [KEY UP] K%d 松开 (持续 %u ms)\n", activeKey, duration);
                if (!longTriggered && duration >= 25) {
                    switch (activeKey) {
                        case 1: return KEY_EVENT_K1;
                        case 4: return KEY_EVENT_K4;
                        case 2: return KEY_EVENT_K2;
                        case 3: return KEY_EVENT_K3;
                    }
                }
            }
        }
    }
    return KEY_EVENT_NONE;
}

// ================= 系统状态机 =================
enum SystemState {
    STATE_BOOT_ANIM,
    STATE_WIZARD_BMS,
    STATE_CONNECTING_BMS,
    STATE_DASHBOARD_MAIN,     // 主仪表盘
    STATE_CHARGING_DASH,      // 独占全屏充电动画
    STATE_MENU_MAIN,          // 功能菜单
    STATE_PANEL_CLEAR_MONTH,  // 清除本月大计确认
    STATE_PANEL_BATTERY,      // 电池参数详情面板
    STATE_PANEL_GPS,          // GPS 参数详情面板
    STATE_PANEL_ABOUT,        // 关于系统面板
    STATE_PANEL_SWITCHES,     // 开关设置面板
    STATE_BMS_RELEASED,       // 释放保护板蓝牙给手机 App 连接
    STATE_SCREEN_OFF          // 息屏待机状�?(关闭屏幕显示)
};

SystemState currentState = STATE_BOOT_ANIM;

// ================= 全局画面过渡�? 位旧帧交叉淡化） =================
LGFX_Sprite transitionSnapshot(&lcd);
bool transitionActive = false;
uint32_t transitionStartMs = 0;
uint16_t transitionDurationMs = 320;
uint32_t uiStateEnterMs = 0;
uint32_t panelAnimStart = 0;       // 功能 app 内容入场/翻页动画起点
uint8_t panelCardSeq = 0;          // 当前帧卡片序号，用于错峰入场
bool appExitActive = false;        // 功能 app 退场淡出
uint32_t appExitStart = 0;
SystemState appExitTarget = STATE_MENU_MAIN;

static inline uint16_t rgb332To565(uint8_t p) {
    uint16_t r = (p >> 5) & 0x07;
    uint16_t g = (p >> 2) & 0x07;
    uint16_t b = p & 0x03;
    r = (r << 5) | (r << 2) | (r >> 1);
    g = (g << 5) | (g << 2) | (g >> 1);
    b = (b << 6) | (b << 4) | (b << 2) | (b >> 0);
    // 上面�?b 展开使用 2 位到 8 位：0/85/170/255
    return (uint16_t)((r & 0xF8) << 8) | (uint16_t)((g & 0xFC) << 3) | (uint16_t)(b >> 3);
}

static inline uint16_t blend565(uint16_t a, uint16_t b, uint8_t t) {
    uint16_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    uint16_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    uint16_t r = (ar * (255 - t) + br * t) / 255;
    uint16_t g = (ag * (255 - t) + bg * t) / 255;
    uint16_t bl = (ab * (255 - t) + bb * t) / 255;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

static bool ensureTransitionSnapshot() {
    if (transitionSnapshot.getBuffer() != nullptr) return true;
    transitionSnapshot.setColorDepth(8);
    transitionSnapshot.createSprite(240, 240);
    return transitionSnapshot.getBuffer() != nullptr;
}

static void captureTransitionSnapshot() {
    if (!ensureTransitionSnapshot()) return;
    canvas.pushSprite(&transitionSnapshot, 0, 0);
}

static void requestStateTransition(SystemState target, uint16_t durationMs = 320) {
    (void)durationMs;
    if (target == currentState) return;
    // 各界面使用自己的入场/退场动画；不再为菜单分配 57.6KB 的 8 位全屏快照。
    currentState = target;
    uiStateEnterMs = millis();
    panelAnimStart = uiStateEnterMs;
    transitionActive = false;
}

static bool isAppPanelState(SystemState s) {
    return s == STATE_PANEL_CLEAR_MONTH || s == STATE_PANEL_BATTERY ||
           s == STATE_PANEL_GPS || s == STATE_PANEL_ABOUT ||
           s == STATE_PANEL_SWITCHES || s == STATE_BMS_RELEASED;
}

static void beginAppExit(SystemState target) {
    if (appExitActive) return;
    appExitActive = true;
    appExitStart = millis();
    appExitTarget = target;
}

// 轻量边缘平滑：只处理主表盘圆环区域，保留中心文字锐利�?
static void applyEdgeSmooth() {
    uint16_t* buf = (uint16_t*)canvas.getBuffer();
    if (!buf) return;

    for (int y = 1; y < 239; y++) {
        uint16_t* row = buf + y * 240;
        int dy = y - 120;
        for (int x = 1; x < 239; x++) {
            int dx = x - 120;
            int dist2 = dx * dx + dy * dy;
            // 只平�?96~120px 的圆�?圆环带，中心文字与外部图标保持锐�?
            if (dist2 < 96 * 96 || dist2 > 120 * 120) continue;
            uint16_t c = lgfx::getSwap16(row[x]);
            uint16_t l = lgfx::getSwap16(row[x - 1]), r = lgfx::getSwap16(row[x + 1]);
            uint16_t u = lgfx::getSwap16(row[x - 240]), d = lgfx::getSwap16(row[x + 240]);
            int cr = (c >> 11) & 0x1F, cg = (c >> 5) & 0x3F, cb = c & 0x1F;
            int lr = (l >> 11) & 0x1F, lg = (l >> 5) & 0x3F, lb = l & 0x1F;
            int rr = (r >> 11) & 0x1F, rg = (r >> 5) & 0x3F, rb = r & 0x1F;
            int ur = (u >> 11) & 0x1F, ug = (u >> 5) & 0x3F, ub = u & 0x1F;
            int dr = (d >> 11) & 0x1F, dg = (d >> 5) & 0x3F, db = d & 0x1F;

            int lum = cr * 2 + cg + cb;
            int maxDiff = abs((cr - lr) * 2 + (cg - lg) + (cb - lb));
            int diff = abs((cr - rr) * 2 + (cg - rg) + (cb - rb));
            if (diff > maxDiff) maxDiff = diff;
            diff = abs((cr - ur) * 2 + (cg - ug) + (cb - ub));
            if (diff > maxDiff) maxDiff = diff;
            diff = abs((cr - dr) * 2 + (cg - dg) + (cb - db));
            if (diff > maxDiff) maxDiff = diff;
            if (maxDiff < 42) continue;

            int nr = (cr * 4 + lr + rr + ur + dr) / 8;
            int ng = (cg * 4 + lg + rg + ug + dg) / 8;
            int nb = (cb * 4 + lb + rb + ub + db) / 8;
            (void)lum;
            row[x] = lgfx::getSwap16((uint16_t)((nr << 11) | (ng << 5) | nb));
        }
    }
}

static void blendTransitionFrame(uint8_t progress) {
    if (!transitionActive || progress >= 255) return;
    uint8_t* old = (uint8_t*)transitionSnapshot.getBuffer();
    uint16_t* dst = (uint16_t*)canvas.getBuffer();
    if (!old || !dst) return;
    const uint32_t count = 240u * 240u;
    for (uint32_t i = 0; i < count; i++) {
        dst[i] = lgfx::getSwap16(blend565(lgfx::getSwap16(dst[i]), rgb332To565(old[i]), progress));
    }
}

static void presentCanvas() {
    if (transitionActive) {
        uint32_t elapsed = millis() - transitionStartMs;
        if (elapsed >= transitionDurationMs) {
            transitionActive = false;
        } else {
            uint8_t p = (uint8_t)((elapsed * 255u) / transitionDurationMs);
            // ease-in-out，让淡入淡出更柔�?
            uint16_t e = (uint16_t)p * p * (3 * 255 - 2 * p) / (255 * 255);
            blendTransitionFrame((uint8_t)e);
        }
    }

    if (appExitActive) {
        uint32_t elapsed = millis() - appExitStart;
        float p = elapsed >= 180 ? 1.0f : (float)elapsed / 180.0f;
        p = p * p * (3.0f - 2.0f * p);
        canvas.fillRectAlpha(0, 0, 240, 240, (uint8_t)(p * 235.0f), COLOR_UI_BG);
    } else if (isAppPanelState(currentState)) {
        uint32_t elapsed = millis() - panelAnimStart;
        float p = elapsed >= 240 ? 1.0f : (float)elapsed / 240.0f;
        p = p * p * (3.0f - 2.0f * p);
        if (p < 1.0f) {
            canvas.fillRectAlpha(0, 0, 240, 240, (uint8_t)((1.0f - p) * 210.0f), COLOR_UI_BG);
        }
    }

    canvas.pushSprite(0, 0);
}

// BLE 设备条目
struct BleDeviceItem {
    std::string name;
    NimBLEAddress address;
    int rssi;
    bool isTarget;
};

std::vector<BleDeviceItem> scannedDevices;
int selectedIndex = 0;
int topVisibleIndex = 0;
String statusMessage = "";
bool isScanning = false;

// 已配对保护板信息
String pairedBmsName = "";
String pairedBmsMac = "";

NimBLEClient* pBmsClient = nullptr;
NimBLERemoteCharacteristic* pBmsRxChar = nullptr;
NimBLERemoteCharacteristic* pBmsTxChar = nullptr;
std::vector<uint8_t> bmsRxBuffer;

#ifndef SIM_BUILD
// 自动回连放到独立任务里，避免 NimBLE 阻塞式 connect 卡住主循环。
static volatile bool s_bmsReconnectInFlight = false;
static volatile bool s_bmsReconnectDone = false;
static volatile bool s_bmsReconnectOk = false;
static BleDeviceItem s_bmsReconnectTarget;
#endif

// ================= 实时车机与电池数据模�?=================
struct VehicleData {
    float speed = 0.0f;            // 实时车�?km/h (来自 GPS NMEA)
    float tripKm = 0.0f;           // 本次开机小计里�?km (来自 GPS 航程积分)
    float monthKm = 0.0f;          // 本月大计总里�?km (来自 GPS 航程积分并存�?NVS)
    float lastSavedMonthKm = 0.0f; // 上次存入 NVS 的本月里�?
    float voltage = 0.0f;          // 电池总压 V (来自 ANT-BMS 实测)
    int soc = 0;                   // 实时电量 0~100% (来自 ANT-BMS 实测)
    float currentAh = 0.0f;        // 剩余容量 Ah (来自 ANT-BMS 实测)
    float totalAh = 0.0f;          // 额定总容�?Ah (来自 ANT-BMS 实测)
    float currentA = 0.0f;         // 瞬时放电/充电电流 A (来自 ANT-BMS 实测，充电为负�?
    float powerW = 0.0f;           // 瞬时功率 W

    int deltaV_mV = 0;             // 单体最大压�?mV (来自 ANT-BMS 实测)
    uint16_t minCell_mV = 0;       // 单体最低电�?mV (来自 ANT-BMS 实测)
    uint16_t maxCell_mV = 0;       // 单体最高电�?mV (来自 ANT-BMS 实测)
    int soh = 100;                 // 电池健康�?% (来自 ANT-BMS 实测)
    int tempC = 0;                 // 电池温度 °C (来自 ANT-BMS 实测 MOS 温度)

    uint8_t bmsStatus = 0;         // 电池运行状�?(0:未知, 1:待机, 2:充电�? 3:放电�? 4:休眠, 5:故障)
    uint8_t chargeMosStatus = 0;   // 充电 MOS 状�?(0:�? 1:开, 2:过充保护, 4:已充�?..)
    bool isBmsCharging = false;    // 保护板是否明确判定为外部充电�?(bmsStatus == 0x02)

    bool isLiveBms = false;        // 是否有真�?BMS 数据接入
    uint32_t bmsPacketCount = 0;   // 真实接收到的 BMS 协议包计�?

};

VehicleData carData;

// ================= 电池健康度与多重 Buff 综合估算引擎 =================
// 依据单体压差、MOS/电池温度、极限电压进行动力电池健康状态动态解算，并支持多�?Buff 叠加
void getBatteryHealthInfo(char* outBuf, size_t maxLen, uint16_t& outColor, bool compact = false) {
    if (!carData.isLiveBms) {
        snprintf(outBuf, maxLen, "--");
        outColor = COLOR_GRAY_TEXT;
        return;
    }

    bool isDanger = false;
    bool isCritDelta = false;
    bool isHighDelta = false;
    bool isCritTemp = false;
    bool isOverTemp = false;
    bool isHighTemp = false;
    bool isLowTemp = false;
    bool isOverV = false;
    bool isUnderV = false;

    // 1. 压差检�?
    if (carData.deltaV_mV >= 150) {
        isCritDelta = true;
        isDanger = true;
    } else if (carData.deltaV_mV >= 60) {
        isHighDelta = true;
    }

    // 2. 温度检�?(MOS/电芯温度)
    if (carData.tempC >= 65 || carData.tempC <= -15) {
        isCritTemp = true;
        isDanger = true;
    } else if (carData.tempC >= 55) {
        isOverTemp = true;
    } else if (carData.tempC >= 45) {
        isHighTemp = true;
    } else if (carData.tempC < 0) {
        isLowTemp = true;
    }

    // 3. 电压检�?(单体电压优先，整包电压兜�?
    if (carData.maxCell_mV >= 4350 || (carData.minCell_mV > 0 && carData.minCell_mV <= 2500)) {
        isDanger = true;
        if (carData.maxCell_mV >= 4350) isOverV = true;
        if (carData.minCell_mV > 0 && carData.minCell_mV <= 2500) isUnderV = true;
    } else {
        if (carData.maxCell_mV >= 4250) isOverV = true;
        if ((carData.minCell_mV > 0 && carData.minCell_mV <= 3000) || (carData.soc <= 5 && carData.voltage > 10.0f)) {
            isUnderV = true;
        }
    }

    // 4. 危险状态最高优先级处理
    if (isDanger) {
        outColor = COLOR_RED_ACCENT;
        if (isCritDelta && isCritTemp) {
            snprintf(outBuf, maxLen, compact ? "危险!(温+差)" : "危险!(高温+压差)");
        } else if (isCritDelta) {
            snprintf(outBuf, maxLen, compact ? "危险!(压差)" : "危险!(严重压差)");
        } else if (isCritTemp) {
            snprintf(outBuf, maxLen, compact ? "危险!(过温)" : "危险!(严重过温)");
        } else if (isOverV) {
            snprintf(outBuf, maxLen, compact ? "危险!(过充)" : "危险!(严重过压)");
        } else if (isUnderV) {
            snprintf(outBuf, maxLen, compact ? "危险!(欠压)" : "危险!(深度欠压)");
        } else {
            snprintf(outBuf, maxLen, "危险！");
        }
        return;
    }

    // 5. 状态 Buff 收集与叠加组装
    std::vector<const char*> buffs;
    if (isOverV) buffs.push_back("过压");
    if (isUnderV) buffs.push_back("欠压");
    if (isOverTemp) buffs.push_back(compact ? "温过高" : "温度过高");
    else if (isHighTemp) buffs.push_back(compact ? "温高" : "温度高");
    else if (isLowTemp) buffs.push_back("低温");

    if (isHighDelta) buffs.push_back(compact ? "压差高" : "压差过高");

    // 6. 无异�?Buff 则判定为全绿健康
    if (buffs.empty()) {
        snprintf(outBuf, maxLen, "健康");
        outColor = COLOR_GREEN_SIGNAL;
        return;
    }

    // 7. 颜色分级：欠�?过压/温度过高属于高风险红，一般温�?压差高为警示�?
    if (isOverV || isUnderV || isOverTemp) {
        outColor = COLOR_RED_ACCENT;
    } else {
        outColor = COLOR_YELLOW_SIGNAL;
    }

    // 8. 叠加拼接 (�?"温高+压差�? �?"欠压+压差过高")
    outBuf[0] = '\0';
    for (size_t i = 0; i < buffs.size(); i++) {
        if (i > 0) strncat(outBuf, "+", maxLen - strlen(outBuf) - 1);
        strncat(outBuf, buffs[i], maxLen - strlen(outBuf) - 1);
    }
}

// ================= GPS 硬件解析数据模型 =================
struct GpsInfo {
    bool isFix = false;            // 是否获得有效 3D 卫星定位
    float speedKmH = 0.0f;         // 实时地面航�?(km/h)
    float courseDeg = 0.0f;        // 地面航向角度 (0~360°)
    int satellites = 0;            // 参与定位解算卫星�?(GGA)
    int satellitesInView = 0;      // 视空/搜星可见卫星总数 (GSV)
    int gpsViewCount = 0;          // GPS 可见卫星�?(GPGSV)
    int bdsViewCount = 0;          // 北斗可见卫星�?(BDGSV/GBGSV)
    int maxSnr = 0;                // 当前搜星最高载噪比/信号强度 (dB-Hz)
    int trackedWithSnr = 0;        // 当前有实际接收信�?SNR>0)的卫星数
    float altitudeM = 0.0f;        // 海拔高度�?(GGA)
    
    char latStr[18] = "--°--.--' -"; // 格式化纬度字符串
    char lonStr[18] = "---°--.--' -";// 格式化经度字符串

    int year = 0;                  // 北京时间�?(�?2026)
    int month = 0;                 // 北京时间�?(1~12)
    int day = 0;                   // 北京时间�?(1~31)
    int hour = 0;                  // 北京时间�?(0~23)
    int minute = 0;                // 北京时间�?(0~59)
    int second = 0;                // 北京时间�?(0~59)
    bool timeValid = false;        // 卫星授时是否有效
    uint32_t lastFixTime = 0;      // 上次有效定位时间�?(millis)
    uint32_t lastGsvTime = 0;      // 上次接收 GSV 报文时间�?(millis)

    uint32_t nmeaPacketCount = 0;  // 真实接收到的 NMEA 报文总数
    uint32_t nmeaErrorCount = 0;   // 校验和错误总数
};

GpsInfo gpsData;
// 定位超时后请求下一次 RMC 解析前清空速度窗口，防止复锁时带入旧速度
static volatile bool s_gpsSpeedFilterReset = false;

// ================= 模拟模式状�?=================
// 配网界面置顶入口：跳过保护板连接，使用内置假数据驱动全部仪表 UI
bool g_simulationMode = false;
uint32_t g_simStartMs = 0;
uint32_t g_simLastUpdateMs = 0;

// 副显示模式切换：
// 上半区副�?(K4切换)�?=本次小计TRIP�?=本月大计MONTH�?=GPS卫星时钟�?=GPS海拔高度
int topSubMode = 0;
// 下半区副�?(K1切换)�?=剩余容量Ah/总容量Ah�?=瞬时放电功率/电流�?=电池总压与状�?
int bottomSubMode = 0;

// ================= 灵动数据（上下文自动副显�?=================
bool smartDataEnabled = true;          // 功能菜单开关，NVS: smart_data
uint32_t topManualUntil = 0;           // K4 手动选择后的 8 秒保护期
uint32_t bottomManualUntil = 0;        // K1 手动选择后的 8 秒保护期
uint32_t lastMovingMs = 0;
uint32_t justStoppedUntil = 0;
uint32_t altTrendUntil = 0;
uint32_t topRotateAt = 0;
uint32_t bottomRotateAt = 0;
uint8_t topRotateIdx = 0;
uint8_t bottomRotateIdx = 0;
bool hasMoved = false;
bool wasMoving = false;
float altEma = 0.0f;
float altHist[8] = {0};
uint8_t altHistCount = 0;
uint8_t altHistIdx = 0;
uint32_t altSampleMs = 0;
float powerPrev = 0.0f;
uint32_t powerSampleMs = 0;
uint32_t powerBurstUntil = 0;

// 丝滑物理动效时间�?
uint32_t chargeEnterTime = 0;
uint32_t menuEnterTime = 0;
uint32_t menuExitStart = 0;
bool menuExiting = false;
float menuScrollVisual = 0.0f;
float menuScrollFrom = 0.0f;
float menuScrollTo = 0.0f;
uint32_t menuScrollStart = 0;

#ifdef SIM_BUILD
// ================= PC 模拟器输入桥 =================
// Panel_sdl 默认把方向键映射到模�?GPIO：UP=36 / DOWN=38 / LEFT=39 / RIGHT=37�?// 按键按下�?gpio_in() �?false；F1/F2 分别映射�?40/41，用于切回配网向导和充电预览�?
static bool simHostKeyDown(uint8_t pin) {
    return !lgfx::gpio_in(pin);
}

static bool simHostEdge(uint8_t pin, bool& lastState) {
    bool down = simHostKeyDown(pin);
    bool edge = down && !lastState;
    lastState = down;
    return edge;
}

bool simHostWizardRequested() {
    static bool last = false;
    return simHostEdge(40, last);
}

bool simHostChargingRequested() {
    static bool last = false;
    return simHostEdge(41, last);
}

bool simHostReleaseRequested() {
    static bool last = false;
    return simHostEdge(42, last);
}

int simReadKeyAdc() {
    // The host build reads the physical keyboard directly. This avoids
    // mixing samples from two different emulated GPIO pins during one scan.
    return simHostRawKey();
}
#endif
uint32_t topSubAnimStart = 0;
uint32_t bottomSubAnimStart = 0;

// 功能菜单项光标与选项
int menuCursor = 0;
uint8_t batteryPanelPage = 0;
uint8_t gpsPanelPage = 0;
const int MENU_TOTAL_ITEMS = 8;
const char* MENU_ITEMS[MENU_TOTAL_ITEMS] = {
    "1. 释放保护板蓝牙",
    "2. 清除本月大计",
    "3. 关闭屏幕显示",
    "4. 重新配对保护板",
    "5. 电池遥测监控",
    "6. GPS 卫星遥测",
    "7. 灵动数据开关",
    "8. 关于车机系统"
};

// 清除确认光标 (0=取消, 1=确认清零)
int clearConfirmCursor = 0;

// ================= 缓动函数 =================
float easeOutBack(float t) {
    const float c1 = 1.25f;
    const float c3 = c1 + 1.0f;
    t = t - 1.0f;
    return 1.0f + c3 * t * t * t + c1 * t * t;
}
float easeOutCubic(float t) {
    t = t - 1.0f;
    return t * t * t + 1.0f;
}

// 开机动�?
void playBootAnimation() {
    const uint32_t DURATION = 1100;
    uint32_t startTime = millis();
    const int16_t cx = 120, cy = 95;
    const float targetRadius = 42.0f, dotRadius = 13.0f, centerRadius = 15.0f;

    while (millis() - startTime < DURATION) {
        uint32_t elapsed = millis() - startTime;
        float progress = (float)elapsed / (float)DURATION;
        if (progress > 1.0f) progress = 1.0f;

        float easeExp = easeOutBack(progress);
        float easeScale = easeOutCubic(progress);

        canvas.fillSprite(COLOR_BG);
        canvas.fillCircle(cx, cy, (int16_t)(centerRadius * easeScale), COLOR_BLUE_ACCENT);

        float curOrbit = targetRadius * easeExp;
        float curDotR = dotRadius * (easeScale > 1.0f ? 1.0f : easeScale);

        for (int i = 0; i < 8; i++) {
            float angle = -3.14159265f / 2.0f + i * (3.14159265f / 4.0f);
            int16_t dx = cx + (int16_t)(cosf(angle) * curOrbit);
            int16_t dy = cy + (int16_t)(sinf(angle) * curOrbit);
            canvas.fillCircle(dx, dy, (int16_t)curDotR, COLOR_BLUE_ACCENT);
        }

        if (progress > 0.40f) {
            canvas.setFont(&fonts::efontCN_24);
            int16_t textY = 175;
            int32_t w1 = canvas.textWidth("凛OS ");
            int32_t w2 = canvas.textWidth("car");
            int16_t startX = 120 - ((w1 + w2) / 2);

            canvas.setTextDatum(middle_left);
            canvas.setTextColor(COLOR_WHITE_TEXT);
            canvas.drawString("凛OS ", startX, textY);
            canvas.setTextColor(COLOR_RED_ACCENT);
            canvas.drawString("car", startX + w1, textY);
        }
        presentCanvas();
        delay(16);
    }
}

// ================= BLE 扫描与连�?=================
// ================= BLE 扫描与多级置�?=================
String toLowerStr(const std::string& str) {
    String s = str.c_str();
    s.toLowerCase();
    return s;
}

// 保护板设备多维识别器 (服务UUID + 广播名称 + 历史MAC + 厂商数据)
bool isAntBmsAdvertised(NimBLEAdvertisedDevice* dev) {
    if (!dev) return false;

    // 1. MAC 地址匹配历史已配对保护板
    if (!pairedBmsMac.isEmpty() && dev->getAddress().toString().c_str() != nullptr) {
        if (pairedBmsMac.equalsIgnoreCase(dev->getAddress().toString().c_str())) {
            return true;
        }
    }

    // 2. 检�?16-bit 专属服务 UUID (蚂蚁保护板透传服务 0xFFE0 / 0xFEE0)
    if (dev->haveServiceUUID()) {
        if (dev->isAdvertisingService(NimBLEUUID((uint16_t)0xFFE0)) ||
            dev->isAdvertisingService(NimBLEUUID((uint16_t)0xFEE0))) {
            return true;
        }
    }

    // 3. 检查广播名称中的关键字 (不区分大小写)
    if (dev->haveName()) {
        std::string name = dev->getName();
        String lowerName = name.c_str();
        lowerName.toLowerCase();
        if (lowerName.indexOf("ant") >= 0 || 
            lowerName.indexOf("bms") >= 0 || 
            lowerName.indexOf("hmsoft") >= 0 ||
            lowerName.indexOf("vbms") >= 0 ||
            lowerName.indexOf("smart") >= 0) {
            return true;
        }
    }

    // 4. 检查厂商自定义数据 (Manufacturer Data)
    if (dev->haveManufacturerData()) {
        std::string mData = dev->getManufacturerData();
        if (mData.length() >= 2) {
            uint8_t b0 = (uint8_t)mData[0];
            uint8_t b1 = (uint8_t)mData[1];
            if ((b0 == 0xAA && b1 == 0x55) || (b0 == 0x7E && b1 == 0xA1)) {
                return true;
            }
        }
    }

    return false;
}

// 智能友好命名解析
std::string getDeviceDisplayName(NimBLEAdvertisedDevice* dev, bool isTarget) {
    std::string devName = dev->haveName() ? dev->getName() : "";
    if (!devName.empty()) {
        return devName;
    }
    std::string macStr = dev->getAddress().toString();
    std::string macTail = (macStr.length() >= 5) ? macStr.substr(macStr.length() - 5) : macStr;
    if (isTarget) {
        if (!pairedBmsMac.isEmpty() && dev->getAddress().toString().c_str() != nullptr &&
            pairedBmsMac.equalsIgnoreCase(dev->getAddress().toString().c_str()) && !pairedBmsName.isEmpty()) {
            return pairedBmsName.c_str();
        }
        return "ANT-BMS [" + macTail + "]";
    }
    return "BLE-" + macTail;
}

// 多级置顶权重：Rank 0(已配�? < Rank 1(ANT/BMS保护�? < Rank 2(普通设�?
int getDeviceRank(const BleDeviceItem& item) {
    if (!pairedBmsMac.isEmpty() && item.address.toString().c_str() != nullptr) {
        if (pairedBmsMac.equalsIgnoreCase(item.address.toString().c_str())) {
            return 0; // 最高优先级：已配对过的保护�?
        }
    }
    if (item.isTarget) {
        return 1; // 第二优先级：ANT / BMS 保护板特征设�?
    }
    return 2;     // 第三优先级：普通周边蓝牙设�?
}

static portMUX_TYPE bleScanMux = portMUX_INITIALIZER_UNLOCKED;

// BLE 回调只做定长 POD 入队，绝不在临界区里分配 vector/string。
struct RawScanEvent {
    uint8_t addr[6];
    uint8_t addrType;
    int8_t rssi;
    bool isTarget;
    char name[32];
};
static constexpr uint8_t kScanQueueSize = 32;
static RawScanEvent s_scanQueue[kScanQueueSize];
static volatile uint8_t s_scanQueueHead = 0;
static volatile uint8_t s_scanQueueTail = 0;

class DeviceScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* dev) override {
        if (!dev) return;

        RawScanEvent ev{};
        NimBLEAddress devAddr = dev->getAddress();
        const uint8_t* native = devAddr.getNative();
        // NimBLEAddress::getNative() 返回的是 NimBLE 内部反序字节；
        // 而 NimBLEAddress(uint8_t[6], type) 构造函数会再反转一次。
        // 这里先转成 ESP/字符串顺序，避免二次反转后连到错误 MAC（表现为连接超时 status=13）。
        if (native) {
            for (size_t i = 0; i < sizeof(ev.addr); i++) {
                ev.addr[i] = native[sizeof(ev.addr) - 1 - i];
            }
        }
        ev.addrType = devAddr.getType();
        ev.rssi = (int8_t)dev->getRSSI();
        ev.isTarget = isAntBmsAdvertised(dev);

        std::string rawName = dev->haveName() ? dev->getName() : "";
        size_t nameLen = rawName.size();
        if (nameLen >= sizeof(ev.name)) nameLen = sizeof(ev.name) - 1;
        memcpy(ev.name, rawName.c_str(), nameLen);
        ev.name[nameLen] = '\0';

        portENTER_CRITICAL(&bleScanMux);
        uint8_t next = (uint8_t)((s_scanQueueHead + 1) % kScanQueueSize);
        if (next != s_scanQueueTail) {
            s_scanQueue[s_scanQueueHead] = ev;
            s_scanQueueHead = next;
        }
        portEXIT_CRITICAL(&bleScanMux);
    }
};

static DeviceScanCallbacks* s_scanCallbacks = nullptr;

void scanCompleteCB(NimBLEScanResults results) {
    isScanning = false;
    // 不在扫描回调任务里读取/修改 scannedDevices（主循环可能正在排序）；
    // 列表为空时由 drawDeviceListUI 按 isScanning 显示提示。
    statusMessage = "";
    Serial.printf("[BLE] 异步扫描结束，共发现 %d 个设备\n", (int)results.getCount());
}

void startBleScan() {
    NimBLEScan* pScan = NimBLEDevice::getScan();
    if (pScan->isScanning()) {
        pScan->stop();
        delay(150);
    }

    portENTER_CRITICAL(&bleScanMux);
    isScanning = true;
    scannedDevices.clear();
    selectedIndex = 0;
    topVisibleIndex = 0;
    statusMessage = "扫描中...";
    // 丢弃上一轮扫描残留事件，避免新一轮列表被旧设备回填
    s_scanQueueHead = 0;
    s_scanQueueTail = 0;
    portEXIT_CRITICAL(&bleScanMux);

    if (s_scanCallbacks == nullptr) {
        s_scanCallbacks = new DeviceScanCallbacks();
    }
    pScan->setAdvertisedDeviceCallbacks(s_scanCallbacks, true); // true = wantDuplicates, 保证接收 SCAN_RSP 补全名称
    pScan->setActiveScan(true);       // 主动扫描，强制请求 SCAN_RSP
    pScan->setDuplicateFilter(false); // 允许接收多包并补全名称
    pScan->setInterval(100);          // 扫描间隔 100ms
    pScan->setWindow(80);             // 扫描窗口 80ms (80% 极高捕获占空比)
    pScan->clearResults();

    // 启动 8 秒非阻塞异步扫描；若刚断开 BMS 连接，第一次可能返回 EBUSY，稍后重试一次。
    bool started = pScan->start(8, scanCompleteCB, false);
    if (!started) {
        Serial.println("[BLE] 首次启动扫描失败，150ms 后重试...");
        delay(150);
        if (pScan->isScanning()) pScan->stop();
        started = pScan->start(8, scanCompleteCB, false);
    }

    if (!started) {
        Serial.println("[BLE] 启动扫描失败");
        isScanning = false;
        statusMessage = "扫描启动失败";
    } else {
        Serial.println("[BLE] 成功启动 8 秒主动异步扫描");
    }
}

// 主循环消费 BLE 回调队列：所有 vector/string 操作都放在这里，不进入临界区。
static void drainScanQueue() {
    bool touched = false;
    for (;;) {
        RawScanEvent ev;
        portENTER_CRITICAL(&bleScanMux);
        if (s_scanQueueTail == s_scanQueueHead) {
            portEXIT_CRITICAL(&bleScanMux);
            break;
        }
        ev = s_scanQueue[s_scanQueueTail];
        s_scanQueueTail = (uint8_t)((s_scanQueueTail + 1) % kScanQueueSize);
        portEXIT_CRITICAL(&bleScanMux);

        NimBLEAddress addr(ev.addr, ev.addrType);
        std::string rawName(ev.name);
        bool found = false;
        for (auto& item : scannedDevices) {
            if (item.address == addr) {
                found = true;
                item.rssi = ev.rssi;
                if (ev.isTarget) item.isTarget = true;
                if (!rawName.empty()) {
                    if (item.name.empty() || item.name.rfind("BLE-", 0) == 0 ||
                        item.name.rfind("ANT-BMS [", 0) == 0) {
                        item.name = rawName;
                    }
                }
                touched = true;
                break;
            }
        }
        if (!found && (ev.isTarget || ev.rssi > -85)) {
            std::string name = rawName;
            if (name.empty()) {
                std::string macStr = addr.toString();
                std::string macTail = (macStr.length() >= 5) ? macStr.substr(macStr.length() - 5) : macStr;
                if (ev.isTarget) {
                    if (!pairedBmsMac.isEmpty() && pairedBmsMac.equalsIgnoreCase(macStr.c_str()) &&
                        !pairedBmsName.isEmpty()) {
                        name = pairedBmsName.c_str();
                    } else {
                        name = "ANT-BMS [" + macTail + "]";
                    }
                } else {
                    name = "BLE-" + macTail;
                }
            }
            scannedDevices.push_back({name, addr, ev.rssi, ev.isTarget});
            touched = true;
        }
    }

    if (touched) {
        std::sort(scannedDevices.begin(), scannedDevices.end(), [](const BleDeviceItem& a, const BleDeviceItem& b) {
            int rankA = getDeviceRank(a);
            int rankB = getDeviceRank(b);
            if (rankA != rankB) return rankA < rankB;
            return a.rssi > b.rssi;
        });
    }
}

void drawSignalBars(int16_t x, int16_t y, int rssi) {
    int bars = 1;
    uint16_t color = COLOR_GRAY_TEXT;
    if (rssi > -60) {
        bars = 4;
        color = COLOR_GREEN_SIGNAL;
    } else if (rssi > -72) {
        bars = 3;
        color = COLOR_GREEN_SIGNAL;
    } else if (rssi > -85) {
        bars = 2;
        color = COLOR_YELLOW_SIGNAL;
    } else {
        bars = 1;
        color = COLOR_RED_ACCENT;
    }

    for (int b = 0; b < 4; b++) {
        int16_t h = (b + 1) * 3;
        uint16_t c = (b < bars) ? color : 0x2124;
        canvas.fillRect(x + b * 4, y + (12 - h), 2, h, c);
    }
}

void drawDeviceListUI(const char* title) {
    canvas.fillSprite(COLOR_BG);
    canvas.drawCircle(120, 120, 119, 0x18C3);

    canvas.setFont(&fonts::efontCN_24);
    canvas.setTextDatum(top_center);
    canvas.setTextColor(COLOR_CYAN_ACCENT);
    canvas.drawString(title, 120, 12);
    canvas.drawFastHLine(50, 40, 140, 0x2945);

    // 线程安全获取设备列表快照
    std::vector<BleDeviceItem> listCopy;
    portENTER_CRITICAL(&bleScanMux);
    listCopy = scannedDevices;
    portEXIT_CRITICAL(&bleScanMux);

    int total = listCopy.size();
    if (selectedIndex >= total) selectedIndex = total - 1;
    if (selectedIndex < 0) selectedIndex = 0;

    if (selectedIndex < topVisibleIndex) {
        topVisibleIndex = selectedIndex;
    } else if (selectedIndex >= topVisibleIndex + 4) {
        topVisibleIndex = selectedIndex - 3;
    }

    const int16_t startY = 46, cardH = 32, gap = 4, cardW = 184;
    const int16_t cardX = 120 - cardW / 2;

    for (int i = 0; i < 4; i++) {
        int itemIdx = topVisibleIndex + i;
        if (itemIdx >= total) break;

        const auto& item = listCopy[itemIdx];
        bool isSelected = (itemIdx == selectedIndex);
        int16_t y = startY + i * (cardH + gap);

        if (isSelected) {
            canvas.fillRoundRect(cardX, y, cardW, cardH, 5, COLOR_CARD_SELECT);
            canvas.drawRoundRect(cardX, y, cardW, cardH, 5, COLOR_CYAN_ACCENT);
            canvas.fillRoundRect(cardX + 2, y + 4, 3, cardH - 8, 2, COLOR_CYAN_ACCENT);
        } else {
            canvas.fillRoundRect(cardX, y, cardW, cardH, 5, COLOR_CARD_NORMAL);
        }

        canvas.setFont(&fonts::FreeSansBold9pt7b);
        canvas.setTextDatum(middle_left);
        if (item.isTarget) {
            canvas.setTextColor(isSelected ? COLOR_GREEN_SIGNAL : 0x47E0);
        } else {
            canvas.setTextColor(isSelected ? COLOR_WHITE_TEXT : COLOR_GRAY_TEXT);
        }

        String displayName = item.name.c_str();
        if (displayName.length() > 12) displayName = displayName.substring(0, 11) + "..";
        canvas.drawString(displayName, cardX + 10, y + cardH / 2);

        drawSignalBars(cardX + cardW - 24, y + 10, item.rssi);
    }

    if (total == 0) {
        canvas.setFont(&fonts::efontCN_14);
        canvas.setTextDatum(top_center);
        canvas.setTextColor(COLOR_GRAY_TEXT);
        canvas.drawString(isScanning ? "正在搜索保护板..." : "未发现保护板", 120, 104);
    }

    if (total > 4) {
        float startAngle = -30.0f, totalSpan = 60.0f;
        float thumbSpan = totalSpan * (4.0f / (float)total);
        float thumbPos = startAngle + (totalSpan - thumbSpan) * ((float)topVisibleIndex / (float)(total - 4));
        canvas.drawArc(120, 120, 117, 115, startAngle, startAngle + totalSpan, 0x18C3);
        canvas.drawArc(120, 120, 117, 115, thumbPos, thumbPos + thumbSpan, COLOR_CYAN_ACCENT);
    }

    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextDatum(top_center);
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.setCursor(105, 184);
    if (total > 0) {
        canvas.printf("%02d / %02d", selectedIndex + 1, total);
    } else {
        canvas.drawString("00 / 00", 120, 184);
    }

    if (!statusMessage.isEmpty()) {
        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextDatum(top_center);
        canvas.setTextColor(statusMessage.indexOf("失败") >= 0 ? COLOR_RED_ACCENT : COLOR_CYAN_ACCENT);
        canvas.drawString(statusMessage, 120, 198);
    } else if (isScanning) {
        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextDatum(top_center);
        canvas.setTextColor(COLOR_CYAN_ACCENT);
        canvas.drawString("搜索中...", 120, 198);
    }

    presentCanvas();
}

// ================= 保护板实车数据解�?=================
uint32_t bmsLastPacketMs = 0;

void bmsNotifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    if (pData == nullptr || length == 0) return;

    if (length >= 2 && pData[0] == 0x7E && pData[1] == 0xA1) {
        bmsRxBuffer.clear();
    } else if (length >= 4 && pData[0] == 0xAA && pData[1] == 0x55 && pData[2] == 0xAA && pData[3] == 0xFF) {
        bmsRxBuffer.clear();
    }

    bmsRxBuffer.insert(bmsRxBuffer.end(), pData, pData + length);

    // 协议 A: 新版 0x7E 0xA1 协议 (�?0xAA 0x55 结尾)
    if (bmsRxBuffer.size() >= 10 && bmsRxBuffer[0] == 0x7E && bmsRxBuffer[1] == 0xA1) {
        if (bmsRxBuffer[bmsRxBuffer.size() - 2] == 0xAA && bmsRxBuffer.back() == 0x55) {
            uint8_t func = bmsRxBuffer[2];
            if (func == 0x11) {
                // 1. 保护板电池运行状�?(Byte 7: 0x01=待机, 0x02=充电�? 0x03=放电�? 0x04=休眠, 0x05=故障)
                // 充电器插枪物理接入充电时，保护板置为 0x02；动能回收制动时仍为 0x03(放电) �?0x01(待机)
                carData.bmsStatus = bmsRxBuffer[7];
                carData.isBmsCharging = (carData.bmsStatus == 0x02);

                uint8_t cells = bmsRxBuffer[9];
                uint8_t tempSensors = bmsRxBuffer[8];
                if (cells > 32) cells = 32;
                if (tempSensors > 6) tempSensors = 6;
                uint8_t offset = cells * 2 + tempSensors * 2;

                if (bmsRxBuffer.size() >= (size_t)(47 + offset)) {
                    carData.chargeMosStatus = bmsRxBuffer[46 + offset];
                }

                if (bmsRxBuffer.size() >= (size_t)(58 + offset)) {
                    uint16_t rawV = (uint16_t)bmsRxBuffer[38 + offset] | ((uint16_t)bmsRxBuffer[39 + offset] << 8);
                    carData.voltage = rawV * 0.01f;

                    int16_t rawI = (int16_t)((uint16_t)bmsRxBuffer[40 + offset] | ((uint16_t)bmsRxBuffer[41 + offset] << 8));
                    carData.currentA = rawI * 0.1f;
                    carData.powerW = carData.voltage * carData.currentA;

                    uint16_t rawSoc = (uint16_t)bmsRxBuffer[42 + offset] | ((uint16_t)bmsRxBuffer[43 + offset] << 8);
                    carData.soc = rawSoc;

                    uint32_t rawTotalCap = (uint32_t)bmsRxBuffer[50 + offset] |
                                           ((uint32_t)bmsRxBuffer[51 + offset] << 8) |
                                           ((uint32_t)bmsRxBuffer[52 + offset] << 16) |
                                           ((uint32_t)bmsRxBuffer[53 + offset] << 24);
                    if (rawTotalCap > 0) {
                        carData.totalAh = rawTotalCap * 0.000001f;
                    }

                    uint32_t rawRemCap = (uint32_t)bmsRxBuffer[54 + offset] |
                                         ((uint32_t)bmsRxBuffer[55 + offset] << 8) |
                                         ((uint32_t)bmsRxBuffer[56 + offset] << 16) |
                                         ((uint32_t)bmsRxBuffer[57 + offset] << 24);
                    carData.currentAh = rawRemCap * 0.000001f;

                    // 1. 电池健康�?SOH (offset + 44)
                    uint16_t rawSoh = (uint16_t)bmsRxBuffer[44 + offset] | ((uint16_t)bmsRxBuffer[45 + offset] << 8);
                    if (rawSoh > 0 && rawSoh <= 100) {
                        carData.soh = rawSoh;
                    } else if (rawSoh == 0) {
                        carData.soh = 100;
                    }

                    // 2. 电池温度 (MOS 温度，位�?offset + 34)
                    int16_t rawMosTemp = (int16_t)((uint16_t)bmsRxBuffer[34 + offset] | ((uint16_t)bmsRxBuffer[35 + offset] << 8));
                    carData.tempC = rawMosTemp;

                    // 3. 压差 (Delta V) 解析与双重校验：
                    if (bmsRxBuffer.size() >= (size_t)(84 + offset)) {
                        carData.deltaV_mV = (uint16_t)bmsRxBuffer[82 + offset] | ((uint16_t)bmsRxBuffer[83 + offset] << 8);
                    }
                    if (cells > 1 && bmsRxBuffer.size() >= (size_t)(34 + cells * 2)) {
                        uint16_t minV = 0xFFFF, maxV = 0;
                        for (uint8_t i = 0; i < cells; i++) {
                            uint16_t cV = (uint16_t)bmsRxBuffer[34 + i * 2] | ((uint16_t)bmsRxBuffer[35 + i * 2] << 8);
                            if (cV >= 1000 && cV <= 5000) {
                                if (cV < minV) minV = cV;
                                if (cV > maxV) maxV = cV;
                            }
                        }
                        if (maxV >= minV && minV != 0xFFFF) {
                            carData.deltaV_mV = maxV - minV;
                            carData.minCell_mV = minV;
                            carData.maxCell_mV = maxV;
                        }
                    }

                    carData.isLiveBms = true;
                    bmsLastPacketMs = millis();
                    carData.bmsPacketCount++;
                }
            }
            bmsRxBuffer.clear();
        }
    }
    // 协议 B: 经典 140 字节协议 (�?0xAA 0x55 0xAA 0xFF 开�?
    else if (bmsRxBuffer.size() >= 140 && bmsRxBuffer[0] == 0xAA && bmsRxBuffer[1] == 0x55 && bmsRxBuffer[2] == 0xAA && bmsRxBuffer[3] == 0xFF) {
        uint16_t rawV = ((uint16_t)bmsRxBuffer[4] << 8) | (uint16_t)bmsRxBuffer[5];
        carData.voltage = rawV * 0.1f;

        // 1. 扫描 32 串单体电压求真实压差与极端电�?
        uint16_t minV = 0xFFFF, maxV = 0;
        for (int i = 0; i < 32; i++) {
            uint16_t cV = ((uint16_t)bmsRxBuffer[6 + i * 2] << 8) | (uint16_t)bmsRxBuffer[7 + i * 2];
            if (cV >= 1000 && cV <= 5000) {
                if (cV < minV) minV = cV;
                if (cV > maxV) maxV = cV;
            }
        }
        if (maxV >= minV && minV != 0xFFFF) {
            carData.deltaV_mV = maxV - minV;
            carData.minCell_mV = minV;
            carData.maxCell_mV = maxV;
        }

        // 2. 电池温度 (MOS 温度，Byte 91..92)
        int16_t rawTemp = (int16_t)(((uint16_t)bmsRxBuffer[91] << 8) | (uint16_t)bmsRxBuffer[92]);
        carData.tempC = rawTemp;

        // 3. 健康�?
        carData.soh = 100;

        int32_t rawI = (int32_t)(((uint32_t)bmsRxBuffer[70] << 24) |
                                 ((uint32_t)bmsRxBuffer[71] << 16) |
                                 ((uint32_t)bmsRxBuffer[72] << 8) |
                                 (uint32_t)bmsRxBuffer[73]);
        carData.currentA = rawI * 0.1f;
        carData.powerW = carData.voltage * carData.currentA;

        carData.soc = bmsRxBuffer[74];

        uint32_t rawTotal = ((uint32_t)bmsRxBuffer[75] << 24) | ((uint32_t)bmsRxBuffer[76] << 16) | ((uint32_t)bmsRxBuffer[77] << 8) | (uint32_t)bmsRxBuffer[78];
        if (rawTotal > 100000) {
            carData.totalAh = rawTotal * 0.000001f;
        } else {
            carData.totalAh = rawTotal * 0.001f;
        }

        uint32_t rawRem = ((uint32_t)bmsRxBuffer[79] << 24) | ((uint32_t)bmsRxBuffer[80] << 16) | ((uint32_t)bmsRxBuffer[81] << 8) | (uint32_t)bmsRxBuffer[82];
        if (rawRem > 100000) {
            carData.currentAh = rawRem * 0.000001f;
        } else {
            carData.currentAh = rawRem * 0.001f;
        }

        if (bmsRxBuffer.size() >= 105) {
            carData.chargeMosStatus = bmsRxBuffer[103];
            if (carData.currentA < -0.2f && carData.chargeMosStatus == 1) {
                carData.bmsStatus = 0x02;
                carData.isBmsCharging = true;
            } else if (carData.currentA > 0.2f) {
                carData.bmsStatus = 0x03;
                carData.isBmsCharging = false;
            } else {
                carData.bmsStatus = 0x01;
                carData.isBmsCharging = false;
            }
        }

        carData.isLiveBms = true;
        bmsLastPacketMs = millis();
        carData.bmsPacketCount++;
        bmsRxBuffer.clear();
    }

    if (bmsRxBuffer.size() > 256) {
        bmsRxBuffer.clear();
    }
}

class BmsClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) override {
    Serial.printf("[BMS-CB] 保护板物理连接成功 ConnId=%d\n", pClient ? pClient->getConnId() : -1);
    }
    void onDisconnect(NimBLEClient* pClient) override {
        int err = pClient ? pClient->getLastError() : -1;
        Serial.printf("[BMS-CB] 保护板断开连接! getLastError=%d\n", err);
        bmsLastPacketMs = 0;
        carData.isLiveBms = false;
        carData.deltaV_mV = 0;
        carData.tempC = 0;
        carData.soh = 0;
        carData.minCell_mV = 0;
        carData.maxCell_mV = 0;
        carData.bmsStatus = 0;
        carData.chargeMosStatus = 0;
        carData.isBmsCharging = false;
    }
};

// 最近一次收到有�?BMS 协议包的时间；用于识别“链路还在但数据已停更”的假在线状�?
bool connectToBms(const BleDeviceItem& target, bool showUi = true, uint8_t timeoutSec = 6) {
    if (showUi) {
        statusMessage = "正在连接...";
        drawDeviceListUI("连接保护板");
    }

    if (NimBLEDevice::getScan()->isScanning()) {
        NimBLEDevice::getScan()->stop();
        delay(100);
    }

    if (pBmsClient == nullptr) {
        pBmsClient = NimBLEDevice::createClient();
        if (pBmsClient == nullptr) {
            Serial.println("[BMS] createClient 失败，BLE 客户端无法分配");
            if (showUi) statusMessage = "蓝牙客户端创建失败";
            return false;
        }
        pBmsClient->setClientCallbacks(new BmsClientCallbacks());
    } else {
        // 复用 client 时先断开上一次残留链路，并清掉属性缓存；
        // 否则连续点连接可能复用旧 GATT 缓存，getService() 永远返回空。
        pBmsClient->disconnect();
        delay(100);
    }
    pBmsClient->setConnectTimeout(timeoutSec);

    Serial.printf("[BMS] 尝试连接 %s [%s]，超时 %us\n",
                  target.name.c_str(), target.address.toString().c_str(), timeoutSec);
    if (pBmsClient->connect(target.address, true)) {
        Serial.println("[BMS] GATT 连接成功，开始发现服务...");
        std::string targetMac = target.address.toString();

        pBmsRxChar = nullptr;
        pBmsTxChar = nullptr;
        NimBLERemoteService* pSvc = pBmsClient->getService(NimBLEUUID((uint16_t)0xFFE0));
        if (!pSvc) {
            pSvc = pBmsClient->getService(NimBLEUUID((uint16_t)0xFEE0));
        }

        if (pSvc) {
            auto pChars = pSvc->getCharacteristics(true);
            if (pChars) {
                for (auto* c : *pChars) {
                    if (c->canNotify() && !pBmsRxChar) pBmsRxChar = c;
                    if ((c->canWrite() || c->canWriteNoResponse()) && !pBmsTxChar) pBmsTxChar = c;
                }
            }
        }

        if (!pBmsRxChar) {
            auto pServices = pBmsClient->getServices(true);
            if (pServices) {
                for (auto* s : *pServices) {
                    auto pChars = s->getCharacteristics(true);
                    if (pChars) {
                        for (auto* c : *pChars) {
                            if (c->canNotify() && !pBmsRxChar) pBmsRxChar = c;
                            if ((c->canWrite() || c->canWriteNoResponse()) && !pBmsTxChar) pBmsTxChar = c;
                        }
                    }
                    if (pBmsRxChar) break;
                }
            }
        }

        if (pBmsRxChar && pBmsRxChar->canNotify()) {
            bool subscribed = pBmsRxChar->subscribe(true, bmsNotifyCallback);
            if (!subscribed) {
                Serial.println("[BMS] 错误: Notify 订阅失败，断开连接！");
                pBmsClient->disconnect();
                delay(100);
                if (showUi) statusMessage = "订阅失败";
                return false;
            }
            Serial.println("[BMS] 成功订阅 Notify 特征值！");
            bmsLastPacketMs = millis(); // 从连接成功开始计算首包新鲜度

            // 只有确认拿到可订阅的 Notify 特征后，才把设备记为已配对；
            // 避免坏 MAC/空连接写进 NVS，导致下次开机反复回连。
            pairedBmsName = target.name.c_str();
            pairedBmsMac  = targetMac.c_str();
            if (prefs.getString("bms_name", "") != pairedBmsName ||
                prefs.getString("bms_mac", "") != pairedBmsMac) {
                prefs.putString("bms_name", pairedBmsName);
                prefs.putString("bms_mac", pairedBmsMac);
            }
        } else {
            // 没有 Notify 就不可能收到遥测，不能把这种“空连接”当作成�?
            Serial.println("[BMS] 错误: 未找到可 Notify 特征值，断开空连接！");
            pBmsClient->disconnect();
            if (showUi) statusMessage = "无遥测特征";
            return false;
        }

        if (showUi) {
            statusMessage = "连接成功";
            drawDeviceListUI("连接保护板");
            delay(400);
        }
        return true;
    } else {
        // connect() 失败后链路可能仍处于 pending/半连接状态，必须主动断开，
        // 否则紧接着 startBleScan() 会被 NimBLE 以 BLE_HS_EBUSY 拒绝。
        Serial.printf("[BMS] 连接失败，lastError=%d\n", pBmsClient ? pBmsClient->getLastError() : -1);
        if (pBmsClient != nullptr) {
            pBmsClient->disconnect();
            delay(150);
        }
        if (showUi) statusMessage = "连接失败";
        return false;
    }
}

#ifndef SIM_BUILD
static void bmsReconnectTask(void*) {
    bool ok = connectToBms(s_bmsReconnectTarget, false, 3);
    s_bmsReconnectOk = ok;
    s_bmsReconnectDone = true;
    s_bmsReconnectInFlight = false;
    vTaskDelete(nullptr);
}
#endif

// ================= ATGM336H / AT6558 CASIC 极速配置引�?=================
void sendCasicCmd(const char* body) {
    uint8_t cs = 0;
    for (const char* p = body; *p; p++) {
        cs ^= (uint8_t)(*p);
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "$%s*%02X\r\n", body, cs);
    GpsSerial.print(buf);
    GpsSerial.flush();
}

void initGpsFast() {
    pinMode(PIN_GPS_RX, INPUT_PULLUP);
    GpsSerial.setRxBufferSize(1024); // 扩展串口缓冲区至 1024 字节，承�?10Hz 突发数据

    // 步骤 1：先以出厂默�?9600 波特率启动，发送指令切�?GPS 模块�?115200 bps
    GpsSerial.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    delay(40);
    sendCasicCmd("PCAS01,5"); // PCAS01,5: 切换�?115200 bps
    delay(40);
    GpsSerial.flush();
    GpsSerial.end();
    delay(30);

    // 步骤 2：ESP32 硬件串口切换�?115200 bps
    GpsSerial.begin(115200, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    delay(40);

    // 步骤 3：再次发�?115200 指令 (防止模块此前已保存在 115200 而在步骤1未被切换)
    sendCasicCmd("PCAS01,5");
    delay(40);

    // 步骤 4：设置输出定位更新率�?10Hz (100ms 刷新周期)
    sendCasicCmd("PCAS02,100");
    delay(40);

    // 步骤 5：精简输出语句 (仅输�?GGA, GSV, RMC；彻底关�?GLL, GSA, VTG, ZDA，消灭芯片负载与串口拥塞)
    sendCasicCmd("PCAS03,1,0,0,1,1,0,0,0,0,0");
    delay(40);

    // 步骤 6：永久保存至内部 Flash / EEPROM (掉电不丢�?
    sendCasicCmd("PCAS00");
    delay(40);

    // 步骤 7：排空握手期间残余杂�?
    while (GpsSerial.available()) {
        GpsSerial.read();
    }
    Serial.println("[GPS] 硬件串口握手完成: 115200 bps @ 10Hz 精简语句模式已激活并存入 Flash");
}

// ================= GPS NMEA 语句解析与自然月跨月解算 =================
bool verifyNmeaChecksum(const char* sentence) {
    if (sentence == nullptr || sentence[0] != '$') return false;
    const char* star = strchr(sentence, '*');
    if (!star) return false; // 没有星号校验符说明报文不完整截断，坚决丢�?    // 确保星号后面至少有两个合法的十六进制字符
    if (!isxdigit(star[1]) || !isxdigit(star[2])) return false;

    uint8_t sum = 0;
    for (const char* p = sentence + 1; p < star; p++) {
        sum ^= (uint8_t)(*p);
    }
    uint8_t expected = (uint8_t)strtoul(star + 1, NULL, 16);
    return (sum == expected);
}

void checkMonthlyRollover(uint32_t currentYM) {
    // 只接受合理的年月，避免 GPS 冷启动/异常报文把月份推到未来导致里程误清零。
    uint32_t year = currentYM / 100;
    uint32_t month = currentYM % 100;
    if (year < 2024 || year > 2035 || month < 1 || month > 12) return;

    static uint32_t s_cachedYM = 0;
    static uint32_t s_pendingYM = 0;
    static uint8_t s_pendingCount = 0;
    if (s_cachedYM == 0) {
        s_cachedYM = prefs.getUInt("month_ym", 0);
    }
    if (s_cachedYM == 0) {
        s_cachedYM = currentYM;
        prefs.putUInt("month_ym", currentYM);
        return;
    }
    if (currentYM == s_cachedYM) {
        s_pendingYM = 0;
        s_pendingCount = 0;
        return;
    }
    if (currentYM < s_cachedYM) return;

    // 连续 3 帧确认新月份，过滤单帧日期跳变；确认后才清零本月里程。
    if (currentYM != s_pendingYM) {
        s_pendingYM = currentYM;
        s_pendingCount = 1;
        return;
    }
    if (s_pendingCount < 3) {
        s_pendingCount++;
        return;
    }

    Serial.printf("[GPS-DATE] 自然月跨月确认 (旧 %u -> 新 %u)，本月大计归零\n", s_cachedYM, currentYM);
    carData.monthKm = 0.0f;
    carData.lastSavedMonthKm = 0.0f;
    prefs.putFloat("month_odo", 0.0f);
    prefs.putUInt("month_ym", currentYM);
    s_cachedYM = currentYM;
    s_pendingYM = 0;
    s_pendingCount = 0;
}

void parseNmeaSentence(const char* sentence) {
    if (sentence == nullptr || sentence[0] != '$') return;

    char buf[160];
    strncpy(buf, sentence, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* tokens[32];
    int tokenCount = 0;
    char* p = buf;
    tokens[tokenCount++] = p;
    while (*p && tokenCount < 32) {
        if (*p == ',' || *p == '*') {
            *p = '\0';
            tokens[tokenCount++] = p + 1;
        }
        p++;
    }

    if (tokenCount < 2) return;

    // 1. 解析 RMC 语句 (包含航速、航向、经纬度、UTC时间、定位状�?—�?唯一高权威基�?
    if (strstr(tokens[0], "RMC") != nullptr && tokenCount >= 10) {
        gpsData.nmeaPacketCount++;
        // 滤波器状态变量：必须�?if/else 外声明，两个分支均可访问
        static float s_win[5] = {0};
        static uint8_t s_winIdx = 0;
        static float s_filteredSpeed = 0.0f;

        if (s_gpsSpeedFilterReset) {
            memset(s_win, 0, sizeof(s_win));
            s_winIdx = 0;
            s_filteredSpeed = 0.0f;
            s_gpsSpeedFilterReset = false;
        }
        if (tokens[2][0] == 'A') {
            gpsData.isFix = true;
            gpsData.lastFixTime = millis();

            float rawKnots = atof(tokens[7]);
            float rawKmH = rawKnots * 1.852f;

            // 死区：静止时低于 2.0 km/h 强制归零
            if (rawKmH < 2.0f) rawKmH = 0.0f;

            gpsData.speedKmH = rawKmH;

            // ── 停车立即归零：flush 整个窗口�?EMA ──────────────────────────
            // 问题根源：若�?flush，窗口里仍有上一秒的历史帧（�?28/30/25），
            // 中位数要等窗口被 0 填满�?�?= 500ms）才能归零，停车后仍显示有速度�?            // 解决：rawKmH==0 时直接全部清零，无延迟�?
            if (rawKmH == 0.0f) {
                memset(s_win, 0, sizeof(s_win));
                s_filteredSpeed = 0.0f;
                carData.speed = 0.0f;
                gpsData.speedKmH = 0.0f;
                // 跳过中位数和 EMA，直�?goto 到航�?坐标解析
                goto rmc_speed_done;
            }

            // ── 5帧滑动窗口中位数滤波 ──────────────────────────────────────
            // 原理：取最�?帧排序后的中位值。对孤立的多径毛刺帧天然免疫�?            // 即使1帧报出离谱的30km/h，中位数仍然稳定在真实值附近�?
            s_win[s_winIdx] = rawKmH;
            s_winIdx = (s_winIdx + 1) % 5;

            {
                // 冒泡排序取中位数�?个数，最�?0次比较）
                float sorted[5];
                memcpy(sorted, s_win, sizeof(sorted));
                for (int i = 0; i < 4; i++) {
                    for (int j = 0; j < 4 - i; j++) {
                        if (sorted[j] > sorted[j+1]) {
                            float tmp = sorted[j]; sorted[j] = sorted[j+1]; sorted[j+1] = tmp;
                        }
                    }
                }
                float medianSpeed = sorted[2]; // 5帧中位数

                // ── 轻量 EMA（alpha=0.25，仅消除整数闪烁，不造成拖尾）───────
                s_filteredSpeed += 0.25f * (medianSpeed - s_filteredSpeed);
                if (s_filteredSpeed < 0.5f) s_filteredSpeed = 0.0f;
            }

            rmc_speed_done:

            carData.speed = s_filteredSpeed;
            gpsData.speedKmH = s_filteredSpeed;  // GPS 面板与主表盘使用同一份滤波速度

            if (strlen(tokens[8]) > 0) {
                gpsData.courseDeg = atof(tokens[8]);
            }

            // 格式化经纬度坐标 (标准度分格式 DD°MM.MM'H)
            if (strlen(tokens[3]) >= 4 && strlen(tokens[4]) >= 1) {
                char deg[3] = { tokens[3][0], tokens[3][1], '\0' };
                const char* minPart = tokens[3] + 2;
                float minVal = atof(minPart);
                snprintf(gpsData.latStr, sizeof(gpsData.latStr), "%s°%05.2f'%s", deg, minVal, tokens[4]);
            }
            if (strlen(tokens[5]) >= 5 && strlen(tokens[6]) >= 1) {
                char deg[4] = { tokens[5][0], tokens[5][1], tokens[5][2], '\0' };
                const char* minPart = tokens[5] + 3;
                float minVal = atof(minPart);
                snprintf(gpsData.lonStr, sizeof(gpsData.lonStr), "%s°%05.2f'%s", deg, minVal, tokens[6]);
            }
        } else {
            // 非有效定位状�?('V' 或尚未定位空�?
            gpsData.isFix = false;
            gpsData.speedKmH = 0.0f;
            memset(s_win, 0, sizeof(s_win));  // 清空滑动窗口，防止重新定位后残留旧速度导致跳变
            s_filteredSpeed = 0.0f;
            carData.speed = 0.0f;
        }

        // 纯整数时间换算：UTC 转北京时�?(UTC+8)
        // 只要卫星时间戳与年月日字段有效，即使处于未定位搜星阶�?('V') 亦可完成时钟秒级精准校时
        if (strlen(tokens[1]) >= 6 && strlen(tokens[9]) >= 6) {
            int utcHour = (tokens[1][0] - '0') * 10 + (tokens[1][1] - '0');
            int utcMin  = (tokens[1][2] - '0') * 10 + (tokens[1][3] - '0');
            int utcSec  = (tokens[1][4] - '0') * 10 + (tokens[1][5] - '0');

            int day   = (tokens[9][0] - '0') * 10 + (tokens[9][1] - '0');
            int month = (tokens[9][2] - '0') * 10 + (tokens[9][3] - '0');
            int year  = 2000 + (tokens[9][4] - '0') * 10 + (tokens[9][5] - '0');

            if (utcHour >= 0 && utcHour <= 23 && utcMin >= 0 && utcMin <= 59 &&
                utcSec >= 0 && utcSec <= 59 &&
                day >= 1 && day <= 31 && month >= 1 && month <= 12 && year >= 2024 && year <= 2099) {
                int bjHour = utcHour + 8;
                int bjDay = day;
                int bjMonth = month;
                int bjYear = year;

                if (bjHour >= 24) {
                    bjHour -= 24;
                    bjDay++;
                    static const uint8_t daysInMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
                    uint8_t dim = daysInMonth[bjMonth - 1];
                    if (bjMonth == 2 && ((bjYear % 4 == 0 && bjYear % 100 != 0) || (bjYear % 400 == 0))) {
                        dim = 29;
                    }
                    if (bjDay > dim) {
                        bjDay = 1;
                        bjMonth++;
                        if (bjMonth > 12) {
                            bjMonth = 1;
                            bjYear++;
                        }
                    }
                }

                gpsData.year = bjYear;
                gpsData.month = bjMonth;
                gpsData.day = bjDay;
                gpsData.hour = bjHour;
                gpsData.minute = utcMin;
                gpsData.second = utcSec;
                gpsData.timeValid = true;

                uint32_t currentYM = gpsData.year * 100 + gpsData.month;
                // 只有有效定位帧才参与跨月判断；V 帧的日期可能尚未稳定。
                if (tokens[2][0] == 'A' && gpsData.isFix) {
                    checkMonthlyRollover(currentYM);
                }
            }
        }
    }
    // 2. 解析 GGA 语句 (包含参与定位解算卫星颗数、定位质量、海拔高�?
    else if (strstr(tokens[0], "GGA") != nullptr && tokenCount >= 10) {
        gpsData.nmeaPacketCount++;
        gpsData.satellites = atoi(tokens[7]);
        // tokens[6] �?Fix Quality�? 为未定位�?/2 为有效定�?
        if (tokens[6][0] != '0' && strlen(tokens[9]) > 0) {
            gpsData.altitudeM = atof(tokens[9]);
        }
    }
    // 3. 解析 GSV 语句 (包含视空可见卫星数、各星载噪比/信号强度 SNR in dB-Hz)
    else if (strstr(tokens[0], "GSV") != nullptr && tokenCount >= 4) {
        gpsData.nmeaPacketCount++;
        gpsData.lastGsvTime = millis();

        // 收到各星座分包的第一包时，更新各星座视空星数
        if (tokens[2][0] == '1') {
            int count = atoi(tokens[3]);
            if (strstr(tokens[0], "GP") != nullptr) {
                gpsData.gpsViewCount = count;
            } else if (strstr(tokens[0], "BD") != nullptr || strstr(tokens[0], "GB") != nullptr) {
                gpsData.bdsViewCount = count;
            }
            gpsData.satellitesInView = gpsData.gpsViewCount + gpsData.bdsViewCount;
        }

        // 循环提取本包最�?4 颗卫星的载噪�?(SNR: tokens 7, 11, 15, 19)
        static uint32_t s_lastGsvCycle = 0;
        static int s_cycleMaxSnr = 0;
        static int s_cycleTracked = 0;

        // 新周期检测：收到任意星座的第 1 分包 (sentence number == '1')
        // 且距上次重置 �?500ms 时，认为新一�?GSV 报告开�?        // 提交上一轮结果并重置累加�?        // 500ms 保护窗口：同一轮内 GPGSV#1 �?BDGSV#1 间隔通常 < 100ms，不会误触发
        // 旧方�?1200ms 定时�?> 1Hz 输出周期 (1000ms)，导致两轮数据叠加到同一累加�?
        if (tokens[2][0] == '1' && (millis() - s_lastGsvCycle >= 500)) {
            gpsData.maxSnr = s_cycleMaxSnr;
            gpsData.trackedWithSnr = s_cycleTracked;
            s_cycleMaxSnr = 0;
            s_cycleTracked = 0;
            s_lastGsvCycle = millis();
        }

        for (int k = 0; k < 4; k++) {
            int prnIdx = 4 + k * 4;
            int snrIdx = 7 + k * 4;
            if (prnIdx < tokenCount && strlen(tokens[prnIdx]) > 0) {
                if (snrIdx < tokenCount && strlen(tokens[snrIdx]) > 0) {
                    int snr = atoi(tokens[snrIdx]);
                    if (snr > 0) {
                        s_cycleTracked++;
                        if (snr > s_cycleMaxSnr) {
                            s_cycleMaxSnr = snr;
                        }
                    }
                }
            }
        }
    }
}

void updateGps() {
    static char nmeaBuf[160];
    static uint8_t nmeaIdx = 0;

    while (GpsSerial.available()) {
        char c = (char)GpsSerial.read();
        if (c == '$') {
            nmeaIdx = 0;
            nmeaBuf[nmeaIdx++] = c;
        } else if (c == '\r' || c == '\n') {
            if (nmeaIdx > 0) {
                nmeaBuf[nmeaIdx] = '\0';
                if (verifyNmeaChecksum(nmeaBuf)) {
                    parseNmeaSentence(nmeaBuf);
                } else {
                    gpsData.nmeaErrorCount++;
                }
                nmeaIdx = 0;
            }
        } else if (nmeaIdx > 0) {
            // 严格过滤：只有在收到 '$' 之后才开始填充缓冲区，防止上电杂波被误统计为校验和错�?
            if (nmeaIdx < sizeof(nmeaBuf) - 1) {
                nmeaBuf[nmeaIdx++] = c;
            } else {
                gpsData.nmeaErrorCount++; // 超长溢出也计入接收错误，避免静默丢帧
                nmeaIdx = 0;
            }
        }
    }

    if (gpsData.isFix && (millis() - gpsData.lastFixTime > 3000)) {
        gpsData.isFix = false;
        gpsData.speedKmH = 0.0f;
        carData.speed = 0.0f;
        // 同步清掉定位相关旧值，避免 GPS 面板显示失锁前的卫星数/海拔/航向。
        gpsData.satellites = 0;
        gpsData.courseDeg = 0.0f;
        gpsData.altitudeM = 0.0f;
        gpsData.timeValid = false;
        s_gpsSpeedFilterReset = true;
    }

    // 若超�?4 秒未收到 GSV 报文，则清空搜星视空与信噪比指标
    if (gpsData.lastGsvTime > 0 && (millis() - gpsData.lastGsvTime > 4000)) {
        gpsData.satellitesInView = 0;
        gpsData.gpsViewCount = 0;
        gpsData.bdsViewCount = 0;
        gpsData.maxSnr = 0;
        gpsData.trackedWithSnr = 0;
    }

    // 航程积分计算：双精度 double 累加，避免 32 位 float 小数值在数万公里时截断与精度损失
    static double s_tripKmAcc = 0.0;
    static double s_monthKmAcc = -1.0;
    if (s_monthKmAcc < 0.0) {
        s_monthKmAcc = (double)carData.monthKm;
        s_tripKmAcc = (double)carData.tripKm;
    }
    // 当跨月清零或用户手动清零本月大计时，同步重置累加�?
    if (carData.monthKm == 0.0f && s_monthKmAcc > 0.01) {
        s_monthKmAcc = 0.0;
    }

    static uint32_t lastDistTime = 0;
    uint32_t now = millis();
    if (lastDistTime == 0) lastDistTime = now;
    uint32_t dt = now - lastDistTime;
    lastDistTime = now;

    if (gpsData.isFix && carData.speed >= 1.0f && dt > 0 && dt < 2000) {
        double dist = (double)carData.speed * ((double)dt / 3600000.0);
        s_tripKmAcc += dist;
        s_monthKmAcc += dist;
        carData.tripKm = (float)s_tripKmAcc;
        carData.monthKm = (float)s_monthKmAcc;

        // 闪存写入保护：每累计增加 1.0 km 保存一次（0.2km 写入过于频繁，长期骑行会加�?Flash 磨损�?
        if (carData.monthKm - carData.lastSavedMonthKm >= 1.0f) {
            carData.lastSavedMonthKm = carData.monthKm;
            prefs.putFloat("month_odo", carData.monthKm);
        Serial.printf("[NVS] 本月大计定期持久化 %.2f km\n", carData.monthKm);
        }
    }

    // 停车 3 秒静止保存检�?
    static bool odoWasMoving = false;
    static uint32_t stopStartTime = 0;
    if (carData.speed >= 1.0f) {
        odoWasMoving = true;
        stopStartTime = 0;
    } else if (odoWasMoving) {
        if (stopStartTime == 0) stopStartTime = millis();
        else if (millis() - stopStartTime >= 3000) {
            odoWasMoving = false;
            if (carData.monthKm != carData.lastSavedMonthKm) {
                carData.lastSavedMonthKm = carData.monthKm;
                prefs.putFloat("month_odo", carData.monthKm);
                Serial.printf("[NVS] 停车同步本月大计: %.2f km\n", carData.monthKm);
            }
        }
    }
}

// ================= 灵动数据：上下副显的情境决策引擎 =================
// 上：手动8s > 刚停车TRIP > 海拔趋势 > 驻车时间 > 骑行轮换
// 下：手动8s > 停车容量 > 大功�?功率突变 > 骑行轮换
void updateSmartSubdata() {
    if (!smartDataEnabled) return;

    const uint32_t now = millis();
    const bool moving = carData.speed >= 2.0f;

    if (moving) {
        hasMoved = true;
        lastMovingMs = now;
        justStoppedUntil = 0;
    } else if (wasMoving) {
        justStoppedUntil = now + 10000; // 刚停�?10 秒内优先显示 TRIP
        topRotateAt = now + 6000;
        bottomRotateAt = now + 5000;
    }
    wasMoving = moving;

    // 海拔�?Hz 采样 + 8 点滑动窗口，连续同向变化超过 3m 视为趋势
    if (gpsData.isFix) {
        if (altSampleMs == 0) altSampleMs = now;
        if (now - altSampleMs >= 1000) {
            altSampleMs = now;
            if (altHistCount == 0) altEma = gpsData.altitudeM;
            else altEma = altEma * 0.7f + gpsData.altitudeM * 0.3f;
            altHist[altHistIdx] = altEma;
            altHistIdx = (altHistIdx + 1) % 8;
            if (altHistCount < 8) altHistCount++;
            if (altHistCount >= 6) {
                uint8_t newest = (altHistIdx + 7) % 8;
                uint8_t oldest = (altHistIdx + 8 - altHistCount) % 8;
                float delta = altHist[newest] - altHist[oldest];
                if (fabsf(delta) >= 3.0f) altTrendUntil = now + 10000;
            }
        }
    } else {
        altHistCount = 0;
        altTrendUntil = 0;
        altSampleMs = now;
    }

    // 上副�?
    if (now >= topManualUntil) {
        int target;
        if (now < justStoppedUntil) {
            target = 0; // 刚停车：TRIP
        } else if (now < altTrendUntil && gpsData.isFix) {
            target = 3; // 海拔持续升降：ALT
        } else if (!hasMoved || now - lastMovingMs > 10000) {
            target = 2; // 持续驻车：卫星时�?
        } else {
            if (now >= topRotateAt) {
                topRotateAt = now + 6000;
                topRotateIdx = (topRotateIdx + 1) % 4;
            }
            target = topRotateIdx;
        }
        if (target != topSubMode) {
            topSubMode = target;
            topSubAnimStart = now;
        }
    }

    // 下副�?
    if (now >= bottomManualUntil) {
        int target;
        const bool parked = !hasMoved || now - lastMovingMs > 10000;
        if (now < justStoppedUntil || parked) {
            target = 0; // 停车：剩余容�?
        } else {
            if (now - powerSampleMs >= 500) {
                if (carData.isLiveBms) {
                    float deltaP = fabsf(carData.powerW - powerPrev);
                    if (deltaP > 180.0f) powerBurstUntil = now + 4000;
                }
                powerPrev = carData.powerW;
                powerSampleMs = now;
            }
            const bool highPower = carData.isLiveBms && fabsf(carData.powerW) > 450.0f;
            const bool rapidPower = now < powerBurstUntil;
            if (highPower || rapidPower) {
                target = 1; // 大功�?功率突变：功率电�?
            } else {
                if (now >= bottomRotateAt) {
                    bottomRotateAt = now + 5000;
                    bottomRotateIdx = (bottomRotateIdx + 1) % 4;
                }
                target = bottomRotateIdx;
            }
        }
        if (target != bottomSubMode) {
            bottomSubMode = target;
            bottomSubAnimStart = now;
        }
    }
}

// ================= 固件内置模拟数据引擎 =================
// 不依�?BLE/GPS 硬件，驱动主表盘、电池面板、GPS 面板和菜单数据�?
void updateSimulation() {
    if (!g_simulationMode) return;

    uint32_t now = millis();

    // PC 预览：F2 进入充电大屏后保持一套稳定的充电数据，直到用户按键退�?
    if (currentState == STATE_CHARGING_DASH) {
        g_simLastUpdateMs = now;
        carData.isLiveBms = true;
        carData.isBmsCharging = true;
        carData.bmsStatus = 0x02;
        carData.speed = 0.0f;
        gpsData.speedKmH = 0.0f;
        carData.soc = 68;
        carData.voltage = 73.8f;
        carData.currentA = -8.5f;
        carData.powerW = carData.voltage * carData.currentA;
        carData.totalAh = 50.0f;
        carData.currentAh = 34.0f;
        carData.deltaV_mV = 18;
        carData.minCell_mV = 3600;
        carData.maxCell_mV = 3618;
        carData.soh = 98;
        carData.tempC = 31;
        carData.chargeMosStatus = 1;
        return;
    }

    if (g_simLastUpdateMs == 0) {
        g_simLastUpdateMs = now;
        return;
    }
    uint32_t dtMs = now - g_simLastUpdateMs;
    if (dtMs < 100) return;
    g_simLastUpdateMs = now;

    float t = (float)(now - g_simStartMs) / 1000.0f;

    // 车速：两种正弦叠加�?~70 km/h 平滑变化
    float speed = 38.0f + 24.0f * sinf(t * 0.18f) + 7.0f * sinf(t * 0.47f);
    if (speed < 0.0f) speed = 0.0f;
    carData.speed = speed;
    gpsData.speedKmH = speed;
    gpsData.courseDeg = fmodf(t * 9.0f, 360.0f);
    gpsData.altitudeM = 35.0f + 2.5f * sinf(t * 0.07f);

    // 里程积分；模拟模式不�?NVS
    float dtH = (float)dtMs / 3600000.0f;
    carData.tripKm += speed * dtH;
    carData.monthKm += speed * dtH;
    carData.lastSavedMonthKm = carData.monthKm;

    // 电池�?2% 缓慢下降，循环演�?
    float soc = 82.0f - fmodf(t * 0.015f, 38.0f);
    if (soc < 0.0f) soc = 0.0f;
    carData.soc = (int)(soc + 0.5f);
    carData.totalAh = 50.0f;
    carData.currentAh = carData.totalAh * (float)carData.soc / 100.0f;
    carData.voltage = 66.0f + 0.08f * (float)carData.soc;
    carData.currentA = 7.0f + 5.0f * fabsf(sinf(t * 0.35f));
    carData.powerW = carData.voltage * carData.currentA;
    carData.deltaV_mV = 16 + (int)(6.0f * sinf(t * 0.23f));
    if (carData.deltaV_mV < 0) carData.deltaV_mV = 0;
    carData.minCell_mV = 3600;
    carData.maxCell_mV = 3600 + carData.deltaV_mV;
    carData.soh = 98;
    carData.tempC = 30 + (int)(3.0f * sinf(t * 0.11f));
    carData.bmsStatus = 0x03;
    carData.chargeMosStatus = 1;
    carData.isBmsCharging = false;
    carData.isLiveBms = true;
    carData.bmsPacketCount++;

    // GPS/时间：固定北京时区演示，随时间递增
    gpsData.isFix = true;
    gpsData.satellites = 13 + (int)(2.0f * sinf(t * 0.09f));
    if (gpsData.satellites < 6) gpsData.satellites = 6;
    gpsData.satellitesInView = 22;
    gpsData.gpsViewCount = 14;
    gpsData.bdsViewCount = 8;
    gpsData.maxSnr = 40 + (int)(4.0f * sinf(t * 0.12f));
    gpsData.trackedWithSnr = 18;
    gpsData.lastFixTime = now;
    gpsData.lastGsvTime = now;
    gpsData.nmeaPacketCount++;
    gpsData.timeValid = true;

    uint32_t totalSec = 13u * 3600u + 39u * 60u + 3u + (now - g_simStartMs) / 1000u;
    gpsData.second = totalSec % 60u;
    gpsData.minute = (totalSec / 60u) % 60u;
    gpsData.hour = (totalSec / 3600u) % 24u;
    gpsData.day = 8 + (int)(totalSec / 86400u);
    gpsData.month = 11;
    gpsData.year = 2026;
}

// ================= 动能回收硬拦截与真实充电状态滤�?=================
bool g_manualExitCharging = false;

bool isBmsChargingCurrent() {
    if (!carData.isLiveBms) return false;
    // 双重硬核校验�?    // 1. 保护板协议层面必须明确上报处于充电中 (carData.isBmsCharging == true，即 bmsStatus == 0x02)
    // 2. 物理电流实测必须为充入电�?(负数，如 -0.2A 及以�?
    // 动能回收制动时，保护板上报状态为放电 (0x03) 或待�?(0x01)，绝对不会误判定为充电！
    return (carData.isBmsCharging && carData.currentA < -0.2f);
}

void checkChargingStateTransitions() {
    // 模拟模式不参与真实充电状态机，避免假电流触发全屏充电动画
    if (g_simulationMode) return;

    static uint32_t chargeCandidateStart = 0;
    static uint32_t dischargeCandidateStart = 0;

    // 息屏待机模式下不主动弹屏打扰，保持关闭直到用户按任意键唤�?
    if (currentState == STATE_SCREEN_OFF) {
        return;
    }

    // 1. 动能回收与骑行硬拦截 (第一道防�?：车�?> 0.5 km/h 坚决禁止进入独占充电大屏�?
    if (carData.speed > 0.5f) {
        chargeCandidateStart = 0;
        dischargeCandidateStart = 0;
        g_manualExitCharging = false; // 正在骑行，解除手动退出锁�?
        if (currentState == STATE_CHARGING_DASH) {
            requestStateTransition(STATE_DASHBOARD_MAIN);
        }
        return;
    }

    // 2. 拧电门放电硬拦截 (第二道防线，0延迟)：只要电流为放电 (正数 > 0.5A)，瞬间强制退出充电大屏！
    // 真实插枪充电时电池绝对不可能处于大电流放电状态！一旦放电说明在骑行拧油门，绝不误跳�?
    if (carData.isLiveBms && carData.currentA > 0.5f) {
        chargeCandidateStart = 0;
        dischargeCandidateStart = 0;
        g_manualExitCharging = false; // 拧油门放电，解除手动退出锁�?
        if (currentState == STATE_CHARGING_DASH) {
        Serial.println("[CHG] 检测到放电电流 (拧油门中)，延迟立即强制退出充电大屏！");
            requestStateTransition(STATE_DASHBOARD_MAIN);
        }
        return;
    }

    // 3. 用户手动退出锁定检查：若用户在充电大屏按键退出了，只要未拔掉充电枪，绝不自动弹回
    if (g_manualExitCharging) {
        chargeCandidateStart = 0;
        // 当物理充入电流完全消失持�?3 秒以上（拔枪），重置锁定状�?
        if (!isBmsChargingCurrent()) {
            if (dischargeCandidateStart == 0) {
                dischargeCandidateStart = millis();
            } else if (millis() - dischargeCandidateStart >= 3000) {
                g_manualExitCharging = false;
                dischargeCandidateStart = 0;
                Serial.println("[CHG] 物理充电电流消失 (已拔枪)，解除手动退出锁定");
            }
        } else {
            dischargeCandidateStart = 0;
        }
        return;
    }

    // 4. 车辆静止下检测保护板充电标志与充入电�?
    if (isBmsChargingCurrent()) {
        dischargeCandidateStart = 0;
        if (chargeCandidateStart == 0) {
            chargeCandidateStart = millis();
        } else if (millis() - chargeCandidateStart >= 3500) {
            // 静止且保护板指示充电持续 3.5 秒以上：确认为真实插枪充�?(彻底过滤动能回收滑行)�?
            if (currentState == STATE_DASHBOARD_MAIN) {
            Serial.println("[CHG] 保护板确认插枪充电 (持续稳定3.5s)！切换至独占全屏充电动画");
                requestStateTransition(STATE_CHARGING_DASH);
                chargeEnterTime = millis();
            }
        }
    } else {
        chargeCandidateStart = 0;
        if (currentState == STATE_CHARGING_DASH) {
            if (dischargeCandidateStart == 0) {
                dischargeCandidateStart = millis();
            } else if (millis() - dischargeCandidateStart >= 1500) {
        Serial.println("[CHG] 退出充电模式 / 拔掉充电枪，恢复主仪表盘");
                requestStateTransition(STATE_DASHBOARD_MAIN);
            }
        }
    }
}

// ================= 全局保护板定时轮询与掉线守护 =================
// 在主仪表盘、充电大屏、功能菜单、参数详情等所有工作状态下均保持后台数据刷�?
void updateBmsPolling() {
    // 模拟模式使用内置数据，不轮询也不自动回连真实保护�?
    if (g_simulationMode) return;

#ifndef SIM_BUILD
    if (s_bmsReconnectDone) {
        s_bmsReconnectDone = false;
        Serial.printf("[BMS] 后台自动回连%s\n", s_bmsReconnectOk ? "成功" : "失败");
    }
    // 后台回连任务运行期间不要让主循环再次进入阻塞式 connect
    if (s_bmsReconnectInFlight) return;
#endif

    // 释放蓝牙模式、配对向导、连接中禁止轮询或自动回�?    // 息屏仍保持轮询；连接过程�?showUi=false 静默执行，绝不刷屏唤�?
    if (currentState == STATE_WIZARD_BMS || currentState == STATE_CONNECTING_BMS ||
        currentState == STATE_BMS_RELEASED) {
        return;
    }

    // 保护板主动定时轮�?(�?1000ms 一次，兼顾新旧双协�?
    if (pBmsClient && pBmsClient->isConnected()) {
        // 链路还在但超过 5 秒没有任何有效协议包：标记离线，避免显示陈旧数据；
        // 超过 8 秒仍无包则主动断开，让后台任务重新建立链路（防止假连接永久挂着）。
        if (bmsLastPacketMs != 0 && millis() - bmsLastPacketMs > 5000) {
            carData.isLiveBms = false;
            if (millis() - bmsLastPacketMs > 8000 && pBmsClient->isConnected()) {
                Serial.println("[BMS] 超过 8 秒无有效报文，主动断开以触发重连");
                pBmsClient->disconnect();
            }
        }

        static uint32_t lastBmsPoll = 0;
        if (millis() - lastBmsPoll >= 1000) {
            lastBmsPoll = millis();
            if (pBmsTxChar && (pBmsTxChar->canWrite() || pBmsTxChar->canWriteNoResponse())) {
                const uint8_t antQueryNew[10] = { 0x7E, 0xA1, 0x01, 0x00, 0x00, 0xBE, 0x18, 0x55, 0xAA, 0x55 };
                pBmsTxChar->writeValue(antQueryNew, sizeof(antQueryNew), false);

                const uint8_t antQueryOld[6] = { 0xDB, 0xDB, 0x00, 0x00, 0x00, 0x00 };
                pBmsTxChar->writeValue(antQueryOld, sizeof(antQueryOld), false);
            }
        }
    } else if (pBmsClient && !pBmsClient->isConnected() && !pairedBmsMac.isEmpty()) {
#ifndef SIM_BUILD
        // 掉线自动回连守护 (每 4 秒)；静默连接，放到后台任务里，绝不阻塞主循环。
        static uint32_t lastBmsReconnect = 0;
        if (millis() - lastBmsReconnect >= 4000) {
            lastBmsReconnect = millis();
            s_bmsReconnectTarget.name = pairedBmsName.c_str();
            s_bmsReconnectTarget.address = NimBLEAddress(pairedBmsMac.c_str());
            s_bmsReconnectInFlight = true;
            s_bmsReconnectDone = false;
            if (xTaskCreate(bmsReconnectTask, "bms_reconnect", 4096, nullptr, 1, nullptr) != pdPASS) {
                s_bmsReconnectInFlight = false;
                Serial.println("[BMS] 后台回连任务创建失败");
            }
        }
#endif
    }
}

// ================= 顶部 GPS 信号强度指数图标 (居中精细化排�? =================
void drawGpsSignalIcon(int16_t cx, int16_t y) {
    int bars = 0;
    uint16_t barColor = COLOR_GRAY_TEXT;

    if (!gpsData.isFix) {
        if (gpsData.satellitesInView > 0) {
            if (gpsData.maxSnr >= 30) {
                bars = 2;
                barColor = COLOR_YELLOW_SIGNAL;
            } else if (gpsData.maxSnr >= 15) {
                bars = 1;
                barColor = COLOR_YELLOW_SIGNAL;
            } else {
                bars = 1;
                barColor = COLOR_RED_ACCENT;
            }
        } else {
            bars = 0;
            barColor = COLOR_GRAY_TEXT;
        }
    } else {
        if (gpsData.satellites < 6) {
            bars = 2;
            barColor = COLOR_YELLOW_SIGNAL;
        } else if (gpsData.satellites < 10) {
            bars = 3;
            barColor = COLOR_GREEN_SIGNAL;
        } else {
            bars = 4;
            barColor = COLOR_CYAN_ACCENT;
        }
    }

    char buf[16];
    if (gpsData.isFix) {
        snprintf(buf, sizeof(buf), "%d SAT", gpsData.satellites);
    } else {
        if (gpsData.satellitesInView > 0) {
            snprintf(buf, sizeof(buf), "%d..", gpsData.satellitesInView);
        } else {
            snprintf(buf, sizeof(buf), "0..");
        }
    }

    canvas.setFont(&fonts::FreeSans9pt7b);
    int32_t textW = canvas.textWidth(buf);
    int16_t totalW = 14 + 5 + textW + 8; // 信号�?4px + 间隙5px + 文字宽度 + 圆点与边�?px
    int16_t startX = cx - totalW / 2;

    // 4 阶梯形信号柱 (宽度 2px, 间隙 2px, 高度 3, 5, 7, 9px)
    const int barHeights[4] = { 3, 5, 7, 9 };
    for (int i = 0; i < 4; i++) {
        int16_t bx = startX + i * 4;
        int16_t bh = barHeights[i];
        int16_t by = y + (9 - bh);
        uint16_t c = (i < bars) ? barColor : 0x2124;
        canvas.fillRect(bx, by, 2, bh, c);
    }

    // 卫星颗数字符
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(gpsData.isFix ? COLOR_WHITE_TEXT : COLOR_YELLOW_SIGNAL);
    canvas.drawString(buf, startX + 18, y + 5);

    // 呼吸定位指示�?
    static bool dotToggle = false;
    static uint32_t lastDot = 0;
    if (millis() - lastDot >= 500) {
        lastDot = millis();
        dotToggle = !dotToggle;
    }
    uint16_t dotColor = gpsData.isFix ? COLOR_GREEN_SIGNAL : (dotToggle ? COLOR_YELLOW_SIGNAL : 0x2124);
    canvas.fillCircle(startX + 18 + textW + 5, y + 5, 2, dotColor);
}

// ================= 等离子流光彗星尾迹渲染算�?=================
void drawCometTailArc(int16_t cx, int16_t cy, int32_t rOut, int32_t rIn, float headAngle, float tailSpanDeg, uint16_t baseColor) {
    const int segments = 12; // 12阶高精度等离子渐变切�?
    float segSpan = tailSpanDeg / (float)segments;

    for (int i = 0; i < segments; i++) {
        float aEnd = headAngle - i * segSpan;
        float aStart = headAngle - (i + 1) * segSpan;

        uint16_t segColor;
        if (i == 0) {
            // 最前端头部：纯白极度刺目高光！
            segColor = COLOR_WHITE_TEXT;
        } else if (i < 4) {
            // 前中段：由纯白向电光青高饱和过渡
            float t = (float)(i - 1) / 3.0f;
            segColor = interpolateColor(COLOR_WHITE_TEXT, baseColor, t);
        } else {
            // 后部尾迹：由电光青向深邃空间暗色消散渐隐
            float t = (float)(i - 4) / 7.0f;
            segColor = interpolateColor(baseColor, 0x0124, t);
        }

        canvas.fillArc(cx, cy, rOut, rIn, aStart, aEnd, segColor);
    }

    // 头部刺目光核聚焦�?
    float rad = headAngle * 0.0174533f;
    float midR = (rOut + rIn) / 2.0f;
    int16_t fx = cx + (int16_t)(cosf(rad) * midR);
    int16_t fy = cy + (int16_t)(sinf(rad) * midR);
    canvas.fillCircle(fx, fy, 4, COLOR_WHITE_TEXT);
}

// 实心圆弧 + 圆角端帽；LovyanGFX �?fillArc 只有平口，端帽需要单独补�?
void fillArcRound(int cx, int cy, int rOut, int rIn, float start, float span, uint16_t color) {
    if (span <= 0.01f) return;
    if (span > 359.99f) span = 359.99f;
    canvas.fillArc(cx, cy, rOut, rIn, start, start + span, color);
    const float midR = (rOut + rIn) * 0.5f;
    const float capR = (rOut - rIn) * 0.5f;
    float a0 = start * 0.0174533f;
    float a1 = (start + span) * 0.0174533f;
    canvas.fillSmoothCircle(cx + (int16_t)(cosf(a0) * midR), cy + (int16_t)(sinf(a0) * midR), (int16_t)capR, color);
    canvas.fillSmoothCircle(cx + (int16_t)(cosf(a1) * midR), cy + (int16_t)(sinf(a1) * midR), (int16_t)capR, color);
}

// 实心渐变弧：按角度切片填色，端帽只补首尾色；2.5° 一段兼顾观感与开销
void fillArcGradient(int cx, int cy, int rOut, int rIn, float start, float span,
                     uint16_t headColor, int soc, bool isSpeed, bool flashOverlay) {
    if (span <= 0.01f) return;
    if (span > 359.99f) span = 359.99f;

    const float stepDeg = 2.5f;
    int steps = (int)ceilf(span / stepDeg);
    if (steps < 1) steps = 1;
    if (steps > 80) steps = 80;

    for (int i = 0; i < steps; i++) {
        float a0 = start + span * (float)i / (float)steps;
        float a1 = start + span * (float)(i + 1) / (float)steps;
        float t = ((float)i + 0.5f) / (float)steps;
        uint16_t c = isSpeed ? speedGradientColor(t, headColor)
                             : batteryGradientColor(t, headColor, soc);
        if (flashOverlay) {
            // 超速仍保留整条闪烁，但叠加在渐变上，不把渐变抹成纯色
            c = interpolateColor(c, COLOR_SPEED_GLOW, 0.55f);
        }
        // 相邻切片轻微重叠，避免浮点取整留下发丝缝
        canvas.fillArc(cx, cy, rOut, rIn, a0, a1 + 0.35f, c);
    }

    const float midR = (rOut + rIn) * 0.5f;
    const float capR = (rOut - rIn) * 0.5f;
    float a0r = start * 0.0174533f;
    float a1r = (start + span) * 0.0174533f;
    uint16_t c0 = isSpeed ? speedGradientColor(0.0f, headColor)
                          : batteryGradientColor(0.0f, headColor, soc);
    uint16_t c1 = isSpeed ? speedGradientColor(1.0f, headColor)
                          : batteryGradientColor(1.0f, headColor, soc);
    if (!isSpeed && soc >= 25) {
        // 电量头部加一点薄荷高光，避免整条翡翠绿显得发闷
        c0 = interpolateColor(c0, COLOR_BATTERY_MINT, 0.55f);
    }
    if (flashOverlay) {
        c0 = interpolateColor(c0, COLOR_SPEED_GLOW, 0.55f);
        c1 = interpolateColor(c1, COLOR_SPEED_GLOW, 0.55f);
    }
    canvas.fillSmoothCircle(cx + (int16_t)(cosf(a0r) * midR), cy + (int16_t)(sinf(a0r) * midR), (int16_t)capR, c0);
    canvas.fillSmoothCircle(cx + (int16_t)(cosf(a1r) * midR), cy + (int16_t)(sinf(a1r) * midR), (int16_t)capR, c1);
}

// 平滑卡片：先画抗锯齿圆角边框，再内缩填充，避�?fillRoundRect 的硬锯齿
void fillSmoothCard(int x, int y, int w, int h, int r, uint16_t fill, uint16_t border) {
    canvas.fillSmoothRoundRect(x, y, w, h, r, border);
    canvas.fillSmoothRoundRect(x + 1, y + 1, w - 2, h - 2, r - 1, fill);
}

// 圆弧边缘解析抗锯齿：只对环带/端帽边缘像素�?4x4 覆盖度混合，避免整屏发糊
void fillArcRoundAA(int cx, int cy, int rOut, int rIn, float start, float span, uint16_t color) {
    fillArcRound(cx, cy, rOut, rIn, start, span, color);
    if (span <= 0.01f) return;
    if (span >= 359.5f) span = 359.5f;

    const float midR = (rOut + rIn) * 0.5f;
    const float capR = (rOut - rIn) * 0.5f;
    const float capR2 = capR * capR;
    const float a0 = start * 0.0174533f;
    const float a1 = (start + span) * 0.0174533f;
    const float c1x = cx + cosf(a0) * midR;
    const float c1y = cy + sinf(a0) * midR;
    const float c2x = cx + cosf(a1) * midR;
    const float c2y = cy + sinf(a1) * midR;
    const float inner2 = (float)(rIn - 2) * (rIn - 2);
    const float outer2 = (float)(rOut + 2) * (rOut + 2);

    auto inside = [&](float px, float py) {
        float dx = px - cx, dy = py - cy;
        float rr2 = dx * dx + dy * dy;
        bool inAnn = rr2 >= (float)rIn * rIn && rr2 <= (float)rOut * rOut;
        bool inSector = true;
        if (span < 359.0f) {
            float ang = atan2f(dy, dx) * 57.2957795f;
            if (ang < 0.0f) ang += 360.0f;
            float rel = fmodf(ang - start + 720.0f, 360.0f);
            inSector = rel <= span;
        }
        if (inAnn && inSector) return true;
        float d1x = px - c1x, d1y = py - c1y;
        float d2x = px - c2x, d2y = py - c2y;
        return (d1x * d1x + d1y * d1y <= capR2) || (d2x * d2x + d2y * d2y <= capR2);
    };

    int x0 = cx - rOut - 2, x1 = cx + rOut + 2;
    int y0 = cy - rOut - 2, y1 = cy + rOut + 2;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float dx = px - cx, dy = py - cy;
            float rr2 = dx * dx + dy * dy;
            bool nearRing = rr2 >= inner2 && rr2 <= outer2;
            float d1x = px - c1x, d1y = py - c1y;
            float d2x = px - c2x, d2y = py - c2y;
            bool nearCap = (d1x * d1x + d1y * d1y <= (capR + 2) * (capR + 2)) ||
                           (d2x * d2x + d2y * d2y <= (capR + 2) * (capR + 2));
            if (!nearRing && !nearCap) continue;

            int covered = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    if (inside(x + (sx + 0.5f) * 0.25f, y + (sy + 0.5f) * 0.25f)) covered++;
                }
            }
            if (covered == 0 || covered == 16) continue;
            uint8_t alpha = (uint8_t)(covered * 255 / 16);
            uint16_t bg = canvas.readPixel(x, y);
            canvas.writePixel(x, y, blend565(bg, color, alpha));
        }
    }
}

// ================= 主表盘渲染核�?(实体实心能量�?+ 严格对称布局 + 丝滑动效 + 扫表自检支持) =================
void drawMainDashboardUI(float overrideSpeed = -1.0f, float overrideSoc = -1.0f, const char* overrideTopText = nullptr, const char* overrideBotText = nullptr, bool isPeakFlash = false) {
    canvas.fillSprite(COLOR_BG);
    canvas.drawCircle(120, 120, 119, COLOR_UI_LINE);

    const int32_t rOut = 114, rIn = 102; // 12px 饱满实体能量弧宽�?
    // -------------------------------------------------------------
    // 1. 上半区：实体速度能量�?(量程 0~100 km/h, 210° ~ 330°)
    // -------------------------------------------------------------
    const float speedStartAngle = 195.0f;
    const float speedTotalSpan  = 150.0f;

    // 实心底槽灰色轨道 (圆角端帽)
    fillArcRound(120, 120, rOut, rIn, speedStartAngle, speedTotalSpan, COLOR_ARC_TRACK);

    float effectiveSpeed = (overrideSpeed >= 0.0f) ? overrideSpeed : carData.speed;
    float speedRatio = effectiveSpeed / 100.0f;
    if (speedRatio > 1.0f) speedRatio = 1.0f;
    if (speedRatio < 0.0f) speedRatio = 0.0f;

    bool isOver100 = (effectiveSpeed > 100.0f);
    static bool flashToggle = false;
    static uint32_t lastFlashTime = 0;
    if (millis() - lastFlashTime >= 90) {
        lastFlashTime = millis();
        flashToggle = !flashToggle;
    }

    uint16_t speedColor = isPeakFlash ? COLOR_WHITE_TEXT : getSpeedArcColor(effectiveSpeed);
    if (!isPeakFlash) {
        // 高速段用暖琥珀呼吸光，不再切回刺眼红色
        if (effectiveSpeed > 85.0f) {
            float pulse = 0.5f + 0.5f * sinf(millis() * 0.0045f);
            speedColor = interpolateColor(COLOR_SPEED_AMBER, COLOR_SPEED_GLOW, 0.12f + 0.22f * pulse);
        }
        if (isOver100) {
            speedColor = flashToggle ? COLOR_SPEED_GLOW : COLOR_SPEED_AMBER;
        }
    }

    if (speedRatio > 0.01f) {
        float activeSpan = speedTotalSpan * speedRatio;
        // 渐变能量弧：沿已填充弧长从冰川青过渡到当前速度端色
        fillArcGradient(120, 120, rOut, rIn, speedStartAngle, activeSpan,
                        speedColor, (int)effectiveSpeed, true,
                        isOver100 && !isPeakFlash && flashToggle);

        // 机车/宝马扫表自检时，在指针尖端绘制高光聚焦点与极光针�?
        if (overrideSpeed >= 0.0f && activeSpan > 2.0f) {
            float headRad = (speedStartAngle + activeSpan) * 0.0174533f;
            int16_t fx = 120 + (int16_t)(cosf(headRad) * 108.0f);
            int16_t fy = 120 + (int16_t)(sinf(headRad) * 108.0f);
            canvas.fillCircle(fx, fy, 4, COLOR_WHITE_TEXT);
        }
    }

    // 2. 速度上方�?1：GPS 信号强度指数 (Y=30, 绝对居中)
    drawGpsSignalIcon(120, 30);

    // 3. 速度上方�?2：行驶数据副�?(带平滑垂直浮现动�?�?扫表自检文字)
    if (overrideTopText != nullptr) {
        canvas.setFont(&fonts::FreeSansBold9pt7b);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(isPeakFlash ? COLOR_WHITE_TEXT : COLOR_CYAN_ACCENT);
        canvas.drawString(overrideTopText, 120, 52);
    } else {
        uint32_t topElapsed = millis() - topSubAnimStart;
        int16_t topYOffset = 0;
        if (topElapsed < 220) {
            float p = (float)topElapsed / 220.0f;
            float ease = easeOutCubic(p);
            topYOffset = (int16_t)((1.0f - ease) * 12.0f);
        }

        canvas.setTextDatum(middle_center);
        int16_t renderTopSubY = 52 + topYOffset;
        if (topSubMode == 0) {
            canvas.setFont(&fonts::FreeSansBold9pt7b);
            canvas.setTextColor(COLOR_SPEED_MINT);
            char tripBuf[24];
            snprintf(tripBuf, sizeof(tripBuf), "TRIP %.2f km", carData.tripKm);
            canvas.drawString(tripBuf, 120, renderTopSubY);
        } else if (topSubMode == 1) {
            canvas.setFont(&fonts::FreeSansBold9pt7b);
            canvas.setTextColor(COLOR_UI_TEXT);
            char monthBuf[24];
            snprintf(monthBuf, sizeof(monthBuf), "MONTH %.1f km", carData.monthKm);
            canvas.drawString(monthBuf, 120, renderTopSubY);
        } else if (topSubMode == 2) {
            canvas.setFont(&fonts::FreeSansBold9pt7b);
            canvas.setTextColor(COLOR_WHITE_TEXT);
            if (gpsData.timeValid) {
                char timeBuf[16];
                snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", gpsData.hour, gpsData.minute, gpsData.second);
                canvas.drawString(timeBuf, 120, renderTopSubY);
            } else {
                canvas.drawString("--:--:--", 120, renderTopSubY);
            }
        } else {
            canvas.setFont(&fonts::FreeSansBold9pt7b);
            canvas.setTextColor(COLOR_SPEED_MINT);
            if (gpsData.isFix) {
                char altBuf[24];
                snprintf(altBuf, sizeof(altBuf), "ALT %.1f m", gpsData.altitudeM);
                canvas.drawString(altBuf, 120, renderTopSubY);
            } else {
                canvas.drawString("ALT --.- m", 120, renderTopSubY);
            }
        }
    }

    // 4. 车速数值与单位 (动态测宽、联合绝对居中，彻底消除偏心不对�?
    char spdStr[16];
    snprintf(spdStr, sizeof(spdStr), "%d", (int)(effectiveSpeed + 0.5f));
    canvas.setFont(&fonts::Font6);
    int32_t spdNumW = canvas.textWidth(spdStr);
    canvas.setFont(&fonts::FreeSans9pt7b);
    int32_t spdUnitW = canvas.textWidth("km/h");
    const int16_t spdSpacing = 4;
    int16_t spdTotalW = spdNumW + spdSpacing + spdUnitW;
    int16_t spdStartX = 120 - spdTotalW / 2;

    canvas.setFont(&fonts::Font6);
    canvas.setTextDatum(middle_left);
    if (isPeakFlash) {
        canvas.setTextColor(COLOR_WHITE_TEXT);
    } else if (isOver100 && !flashToggle) {
        canvas.setTextColor(COLOR_RED_ACCENT);
    } else {
        canvas.setTextColor(COLOR_WHITE_TEXT);
    }
    canvas.drawString(spdStr, spdStartX, 86);

    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextColor(isPeakFlash ? COLOR_WHITE_TEXT : COLOR_SPEED_MINT);
    canvas.drawString("km/h", spdStartX + spdNumW + spdSpacing, 92);

    // -------------------------------------------------------------
    // 5. 中间激光分割线 (关于 X=120 严格左右对称)
    // -------------------------------------------------------------
    uint16_t dividerDotColor = isPeakFlash ? COLOR_WHITE_TEXT : COLOR_UI_LINE;
    canvas.drawFastHLine(32, 120, 176, isPeakFlash ? 0x4B3D : COLOR_UI_LINE);
    canvas.fillCircle(32, 120, 2, dividerDotColor);
    canvas.fillCircle(208, 120, 2, dividerDotColor);

    // -------------------------------------------------------------
    // 6. 下半区：实体电量状态环 (量程 0~100%, 30° ~ 150°)
    // -------------------------------------------------------------
    const float battStartAngle = 15.0f;
    const float battTotalSpan  = 150.0f;

    // 实心底槽灰色轨道 (圆角端帽)
    fillArcRound(120, 120, rOut, rIn, battStartAngle, battTotalSpan, COLOR_ARC_TRACK);

    float effectiveSocRatio = 0.0f;
    uint16_t battColor = COLOR_GRAY_TEXT;
    int displaySoc = 0;
    bool hasSocData = false;

    if (overrideSoc >= 0.0f) {
        displaySoc = (int)overrideSoc;
        effectiveSocRatio = overrideSoc / 100.0f;
        battColor = isPeakFlash ? COLOR_WHITE_TEXT : getBatteryArcColor(displaySoc);
        hasSocData = true;
    } else if (carData.isLiveBms) {
        displaySoc = carData.soc;
        effectiveSocRatio = carData.soc / 100.0f;
        battColor = getBatteryArcColor(carData.soc);
        if (carData.soc < 20) {
            // 低电量（<20%）不再整条硬闪：红色与暖白之间平滑呼吸，弧和数字共用
            float lowPulse = 0.5f + 0.5f * sinf(millis() * 0.0042f);
            battColor = interpolateColor(COLOR_RED_ACCENT, COLOR_SPEED_GLOW,
                                         0.15f + 0.45f * lowPulse);
        }
        hasSocData = true;
    }

    if (effectiveSocRatio > 1.0f) effectiveSocRatio = 1.0f;
    if (effectiveSocRatio < 0.0f) effectiveSocRatio = 0.0f;

    if (effectiveSocRatio > 0.01f) {
        float activeSpan = battTotalSpan * effectiveSocRatio;
        // 电量环固定左端 165°，向右增长到 15°；正常绿色，低于 20% 整段橙红
        float battEndAngle = battStartAngle + battTotalSpan;
        fillArcGradient(120, 120, rOut, rIn, battEndAngle - activeSpan, activeSpan,
                        battColor, displaySoc, false, false);

        // 扫表自检时，在增长端绘制高光聚焦�?
        if (overrideSoc >= 0.0f && activeSpan > 2.0f) {
            float bHeadRad = (battEndAngle - activeSpan) * 0.0174533f;
            int16_t bx = 120 + (int16_t)(cosf(bHeadRad) * 108.0f);
            int16_t by = 120 + (int16_t)(sinf(bHeadRad) * 108.0f);
            canvas.fillCircle(bx, by, 3, COLOR_WHITE_TEXT);
        }
    }

    // 7. 下半区核心指标：电池电量大字 (与上方车速尺寸与规制严格镜像对称，联合绝对居�?
    char socStr[16];
    if (hasSocData) {
        snprintf(socStr, sizeof(socStr), "%d", displaySoc);
    } else {
        snprintf(socStr, sizeof(socStr), "--");
    }
    canvas.setFont(&fonts::Font6);
    int32_t socNumW = canvas.textWidth(socStr);
    canvas.setFont(&fonts::FreeSans9pt7b);
    int32_t socUnitW = canvas.textWidth("%");
    const int16_t socSpacing = 4;
    int16_t socTotalW = socNumW + socSpacing + socUnitW;
    int16_t socStartX = 120 - socTotalW / 2;

    // 电量数字不再整块用绿色：正常电量用白色主数字 + 薄荷单位，低电量才转为红色呼吸。
    uint16_t socNumColor = hasSocData ? COLOR_WHITE_TEXT : COLOR_UI_MUTED;
    uint16_t socUnitColor = hasSocData ? COLOR_BATTERY_MINT : COLOR_UI_MUTED;
    if (hasSocData && displaySoc < 12) {
        socNumColor = battColor;
        socUnitColor = battColor;
    }

    canvas.setFont(&fonts::Font6);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(socNumColor);
    canvas.drawString(socStr, socStartX, 154);

    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextColor(socUnitColor);
    canvas.drawString("%", socStartX + socNumW + socSpacing, 160);

    // 8. 电池总压 (Y=182, 绝对居中)
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COLOR_WHITE_TEXT);
    char voltBuf[16];
    if (overrideSoc >= 0.0f && !carData.isLiveBms) {
        snprintf(voltBuf, sizeof(voltBuf), "%.1f V", 72.0f * (displaySoc / 100.0f));
    } else if (carData.isLiveBms) {
        snprintf(voltBuf, sizeof(voltBuf), "%.1f V", carData.voltage);
    } else {
        snprintf(voltBuf, sizeof(voltBuf), "--.- V");
    }
    canvas.drawString(voltBuf, 120, 182);

    // 9. 下半区副显切�?(带平滑垂直浮现动�?�?扫表自检文字)
    if (overrideBotText != nullptr) {
        canvas.setFont(&fonts::FreeSansBold9pt7b);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(isPeakFlash ? COLOR_WHITE_TEXT : COLOR_SPEED_MINT);
        canvas.drawString(overrideBotText, 120, 202);
    } else {
        uint32_t botElapsed = millis() - bottomSubAnimStart;
        int16_t botYOffset = 0;
        if (botElapsed < 220) {
            float p = (float)botElapsed / 220.0f;
            float ease = easeOutCubic(p);
            botYOffset = (int16_t)((1.0f - ease) * 12.0f);
        }

        int16_t renderBotSubY = 202 + botYOffset;
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextDatum(middle_center);
        if (bottomSubMode == 0) {
            canvas.setTextColor(COLOR_UI_TEXT);
            char ahBuf[24];
            if (carData.isLiveBms) {
                snprintf(ahBuf, sizeof(ahBuf), "%.1f / %.1f Ah", carData.currentAh, carData.totalAh);
            } else {
                snprintf(ahBuf, sizeof(ahBuf), "-- / -- Ah");
            }
            canvas.drawString(ahBuf, 120, renderBotSubY);
        } else if (bottomSubMode == 1) {
            canvas.setTextColor(COLOR_SPEED_AMBER);
            char powerBuf[24];
            if (carData.isLiveBms) {
                snprintf(powerBuf, sizeof(powerBuf), "%.0f W (%.1fA)", carData.powerW, carData.currentA);
            } else {
                snprintf(powerBuf, sizeof(powerBuf), "-- W (--A)");
            }
            canvas.drawString(powerBuf, 120, renderBotSubY);
        } else if (bottomSubMode == 2) {
            char healthBuf[32];
            uint16_t healthColor = COLOR_GRAY_TEXT;
            getBatteryHealthInfo(healthBuf, sizeof(healthBuf), healthColor, true);
            char triBuf[36];
            if (carData.isLiveBms) {
                snprintf(triBuf, sizeof(triBuf), "%dmV  %d°C  %s", carData.deltaV_mV, carData.tempC, healthBuf);
                canvas.setTextColor(healthColor != COLOR_GREEN_SIGNAL ? healthColor : getDeltaVColor(carData.deltaV_mV));
            } else {
                snprintf(triBuf, sizeof(triBuf), "--mV  --°C  --");
                canvas.setTextColor(COLOR_GRAY_TEXT);
            }
            if (canvas.textWidth(triBuf) > 170) {
                canvas.setFont(&fonts::efontCN_14);
            }
            canvas.drawString(triBuf, 120, renderBotSubY);
            canvas.setFont(&fonts::efontCN_16);
        } else {
            char statBuf[24];
            if (carData.isLiveBms) {
                if (carData.isBmsCharging) {
                    canvas.setTextColor(COLOR_CYAN_ACCENT);
                    snprintf(statBuf, sizeof(statBuf), "BMS 充电中");
                } else if (carData.bmsStatus == 3) {
                    canvas.setTextColor(COLOR_GREEN_SIGNAL);
                    snprintf(statBuf, sizeof(statBuf), "BMS 放电中");
                } else {
                    canvas.setTextColor(COLOR_GREEN_SIGNAL);
                    snprintf(statBuf, sizeof(statBuf), "BMS 待机正常");
                }
            } else {
                canvas.setTextColor(COLOR_RED_ACCENT);
                snprintf(statBuf, sizeof(statBuf), "BMS 离线");
            }
            canvas.drawString(statBuf, 120, renderBotSubY);
        }
    }

    // 按键校准调试读数只在开发时用串口查看，不占用主表盘圆屏

    presentCanvas();
}

// ================= 宝马 / 机车顶级开机自检满贯扫表动效 =================
void playDashboardSweepAnimation() {
    const uint32_t SWEEP_DURATION = 1350; // 1.35 秒极致丝滑自检
    uint32_t startT = millis();

    while (millis() - startT < SWEEP_DURATION) {
        uint32_t elapsed = millis() - startT;
        if (!g_simulationMode) updateGps(); // 保证扫表时不丢失 GPS 串口报文与时间同�?
        float curSpeed = 0.0f;
        float curSoc = 0.0f;
        const char* topText = "SYSTEM CHECK";
        const char* botText = "INITIALIZING";
        bool peakFlash = false;

        if (elapsed < 550) {
            // 阶段 1：激情拉转速扫�?0 -> 100 km/h (�?550ms 迅速冲�?
            float p = (float)elapsed / 550.0f;
            float ease = easeOutCubic(p);
            curSpeed = ease * 100.0f;
            curSoc   = ease * 100.0f;
            topText = "SYSTEM CHECK";
            botText = "INITIALIZING";
        } else if (elapsed < 750) {
            // 阶段 2：满表峰值定格自检 100 km/h + 100% 满贯轰鸣高光 (持续 200ms)
            curSpeed = 100.0f;
            curSoc   = 100.0f;
            topText = "SELF-CHECK OK";
            botText = "SYSTEM READY";
            peakFlash = true;
        } else {
            // 阶段 3：平滑回落至实车实测物理数据 (�?600ms 平滑逼近真实�?
            float p = (float)(elapsed - 750) / 600.0f;
            float ease = easeOutCubic(p);
            float targetSpd = carData.speed;
            float targetSoc = carData.isLiveBms ? (float)carData.soc : 0.0f;
            curSpeed = 100.0f - ease * (100.0f - targetSpd);
            curSoc   = 100.0f - ease * (100.0f - targetSoc);
            topText = "RIN-OS CAR";
            botText = "ALL SYSTEMS OK";
        }

        drawMainDashboardUI(curSpeed, curSoc, topText, botText, peakFlash);
        delay(16); // 保证 60 FPS 极度流畅
    }
}

// ================= 模拟模式入口 =================
void enterSimulationMode() {
    if (NimBLEDevice::getScan()->isScanning()) {
        NimBLEDevice::getScan()->stop();
    }
    if (pBmsClient && pBmsClient->isConnected()) {
        pBmsClient->disconnect();
    }

    g_simulationMode = true;
    g_simStartMs = millis();
    g_simLastUpdateMs = 0;
    isScanning = false;
    statusMessage = "";
    g_manualExitCharging = false;
    topSubMode = 0;
    bottomSubMode = 0;

    carData = VehicleData();
    gpsData = GpsInfo();

    // 初始数据：进入后立即呈现一帧完整仪表，再交�?updateSimulation 平滑变化
    carData.isLiveBms = true;
    carData.speed = 45.0f;
    carData.tripKm = 12.6f;
    carData.monthKm = 128.4f;
    carData.lastSavedMonthKm = carData.monthKm;
    carData.voltage = 72.56f;
    carData.soc = 82;
    carData.totalAh = 50.0f;
    carData.currentAh = 41.0f;
    carData.currentA = 8.5f;
    carData.powerW = carData.voltage * carData.currentA;
    carData.deltaV_mV = 16;
    carData.minCell_mV = 3600;
    carData.maxCell_mV = 3616;
    carData.soh = 98;
    carData.tempC = 30;
    carData.bmsStatus = 0x03;
    carData.chargeMosStatus = 1;
    carData.isBmsCharging = false;
    carData.bmsPacketCount = 1;

    gpsData.isFix = true;
    gpsData.speedKmH = carData.speed;
    gpsData.courseDeg = 0.0f;
    gpsData.satellites = 14;
    gpsData.satellitesInView = 22;
    gpsData.gpsViewCount = 14;
    gpsData.bdsViewCount = 8;
    gpsData.maxSnr = 42;
    gpsData.trackedWithSnr = 18;
    gpsData.altitudeM = 35.0f;
    gpsData.timeValid = true;
    gpsData.year = 2026;
    gpsData.month = 11;
    gpsData.day = 8;
    gpsData.hour = 13;
    gpsData.minute = 39;
    gpsData.second = 3;
    gpsData.lastFixTime = millis();
    gpsData.lastGsvTime = millis();
    gpsData.nmeaPacketCount = 1;
    gpsData.nmeaErrorCount = 0;
    snprintf(gpsData.latStr, sizeof(gpsData.latStr), "31°14.52'N");
    snprintf(gpsData.lonStr, sizeof(gpsData.lonStr), "121°28.31'E");

    requestStateTransition(STATE_DASHBOARD_MAIN);
    Serial.println("[SIM] 已进入模拟模式：跳过 BLE 保护板，使用内置电池/GPS 数据");
    playDashboardSweepAnimation();
}

#ifdef SIM_BUILD
// PC 预览专用：直接跳到充电大屏，验证充电 UI / 彗星尾迹 / ETA 布局
void enterChargingPreview() {
    g_simulationMode = true;
    carData.isLiveBms = true;
    carData.isBmsCharging = true;
    carData.bmsStatus = 0x02;
    carData.soc = 68;
    carData.voltage = 73.8f;
    carData.currentA = -8.5f;
    carData.powerW = carData.voltage * carData.currentA;
    carData.totalAh = 50.0f;
    carData.currentAh = 34.0f;
    carData.deltaV_mV = 18;
    carData.minCell_mV = 3600;
    carData.maxCell_mV = 3618;
    carData.soh = 98;
    carData.tempC = 31;
    carData.chargeMosStatus = 1;
    requestStateTransition(STATE_CHARGING_DASH);
    g_manualExitCharging = false;
    chargeEnterTime = millis();
}
#endif

// 退出模拟：清掉假数据，恢复 NVS 里的真实本月里程，避免污染后续真实骑�?
void exitSimulationMode() {
    if (!g_simulationMode) return;

    g_simulationMode = false;
    g_simStartMs = 0;
    g_simLastUpdateMs = 0;
    carData = VehicleData();
    gpsData = GpsInfo();
    carData.monthKm = prefs.getFloat("month_odo", 0.0f);
    carData.lastSavedMonthKm = carData.monthKm;
    carData.isLiveBms = false;
    g_manualExitCharging = false;
}

// ================= 全屏充电仪表（电量主视觉，端压为次级卡片�?=================
void drawChargingUI() {
    canvas.fillSprite(COLOR_UI_BG);
    const int16_t cx = 120, cy = 120;
    const int32_t rOut = 114, rIn = 104;

    float socRatio = carData.isLiveBms ? (carData.soc / 100.0f) : 0.0f;
    if (socRatio > 1.0f) socRatio = 1.0f;
    if (socRatio < 0.0f) socRatio = 0.0f;

    float chgCurrent = fabsf(carData.currentA);
    float chgPower = carData.voltage * chgCurrent;
    uint32_t enterElapsed = millis() - chargeEnterTime;
    const uint32_t ENTER_DURATION = 650;
    float enterP = enterElapsed >= ENTER_DURATION ? 1.0f : (float)enterElapsed / (float)ENTER_DURATION;
    float enterEase = easeOutCubic(enterP);

    // 外圈：SOC 进度�?
    canvas.fillArc(cx, cy, rOut, rIn, 0, 360, COLOR_UI_LINE);
    fillArcRoundAA(cx, cy, rOut, rIn, 0, 359.5f, COLOR_UI_LINE);
    uint16_t socColor = carData.soc >= 80 ? COLOR_SPEED_MINT : COLOR_UI_ACCENT;
    float fillSpan = 360.0f * socRatio * enterEase;
    if (fillSpan > 0.5f) {
        if (fillSpan > 359.5f) fillSpan = 359.5f;
        fillArcRoundAA(cx, cy, rOut, rIn, 270.0f, fillSpan, socColor);
        if (enterP >= 0.98f) {
            float headRad = (270.0f + fillSpan) * 0.0174533f;
            canvas.fillCircle(cx + (int16_t)(cosf(headRad) * 109.0f),
                              cy + (int16_t)(sinf(headRad) * 109.0f), 4, COLOR_WHITE_TEXT);
        }
    }

    // 顶部状�?
    static bool blink = false;
    static uint32_t lastBlink = 0;
    if (millis() - lastBlink >= 420) {
        lastBlink = millis();
        blink = !blink;
    }
    canvas.fillRoundRect(74, 12, 92, 24, 12, COLOR_UI_CARD);
    canvas.fillCircle(88, 24, 3, blink ? COLOR_UI_ACCENT : COLOR_UI_ACCENT_DIM);
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(COLOR_UI_TEXT);
    canvas.drawString(carData.isLiveBms ? "正在充电" : "等待数据", 96, 24);

    // 电量主视觉：巨大的百分比
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextDatum(top_center);
    canvas.setTextColor(COLOR_UI_MUTED);
    canvas.drawString("剩余电量", cx, 42);

    char socBuf[12];
    if (carData.isLiveBms) snprintf(socBuf, sizeof(socBuf), "%d", carData.soc);
    else snprintf(socBuf, sizeof(socBuf), "--");
    canvas.setFont(&fonts::FreeSansBold24pt7b);
    int32_t socNumW = canvas.textWidth(socBuf);
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    int32_t socPctW = canvas.textWidth("%");
    int16_t socTotalW = socNumW + 3 + socPctW;
    int16_t socStartX = cx - socTotalW / 2;
    canvas.setFont(&fonts::FreeSansBold24pt7b);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(COLOR_WHITE_TEXT);
    canvas.drawString(socBuf, socStartX, 78);
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextColor(socColor);
    canvas.drawString("%", socStartX + socNumW + 3, 80);

    char capBuf[32];
    snprintf(capBuf, sizeof(capBuf), "%.1f / %.1f Ah", carData.currentAh, carData.totalAh);
    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COLOR_UI_MUTED);
    canvas.drawString(capBuf, cx, 112);

    // 端压：次级主数据，单独横�?
    canvas.fillRoundRect(50, 118, 140, 36, 18, COLOR_UI_CARD);
    canvas.drawRoundRect(50, 118, 140, 36, 18, COLOR_UI_LINE);
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(COLOR_UI_MUTED);
    canvas.drawString("端压", 62, 136);

    char vBuf[16];
    if (carData.isLiveBms) snprintf(vBuf, sizeof(vBuf), "%.1fV", carData.voltage);
    else snprintf(vBuf, sizeof(vBuf), "--.-V");
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextDatum(middle_right);
    canvas.setTextColor(COLOR_WHITE_TEXT);
    canvas.drawString(vBuf, 178, 136);

    // 压差单独一行，不塞进任何小�?
    char dvBuf[20];
    if (carData.isLiveBms) snprintf(dvBuf, sizeof(dvBuf), "压差  %.3fV", carData.deltaV_mV / 1000.0f);
    else snprintf(dvBuf, sizeof(dvBuf), "压差  --.---V");
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextDatum(top_center);
    canvas.setTextColor(COLOR_UI_MUTED);
    canvas.drawString(dvBuf, cx, 155);

    // 底部一张纵向列表：标签左、数值右，字号统一，避免小卡片压字
    const int16_t cardY = 168, cardH = 46;
    canvas.fillRoundRect(55, cardY, 130, cardH, 18, COLOR_UI_CARD);
    canvas.drawFastHLine(66, cardY + 15, 108, COLOR_UI_LINE);
    canvas.drawFastHLine(66, cardY + 30, 108, COLOR_UI_LINE);
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(COLOR_UI_MUTED);
    canvas.drawString("功率", 66, cardY + 8);
    canvas.drawString("预计", 66, cardY + 23);
    canvas.drawString("温度", 66, cardY + 38);

    char pwrBuf[16];
    snprintf(pwrBuf, sizeof(pwrBuf), "+%.0fW", chgPower);
    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextDatum(middle_right);
    canvas.setTextColor(COLOR_SPEED_AMBER);
    canvas.drawString(pwrBuf, 174, cardY + 8);

    char etaVal[16];
    if (carData.soc >= 100) {
        snprintf(etaVal, sizeof(etaVal), "已满");
    } else if (chgCurrent < 0.25f || carData.totalAh <= 0.0f) {
        snprintf(etaVal, sizeof(etaVal), "--");
    } else {
        float remAh = carData.totalAh * (1.0f - (carData.soc / 100.0f));
        float hours = remAh / (chgCurrent * 0.92f);
        int totalMins = (int)(hours * 60.0f);
        if (totalMins < 1) totalMins = 1;
        int h = totalMins / 60;
        int m = totalMins % 60;
        if (h > 0) snprintf(etaVal, sizeof(etaVal), "%dh%02dm", h, m);
        else snprintf(etaVal, sizeof(etaVal), "%dm", m);
    }
    canvas.setTextColor(COLOR_UI_ACCENT);
    canvas.drawString(etaVal, 174, cardY + 23);

    char tmpBuf[12];
    if (carData.isLiveBms) snprintf(tmpBuf, sizeof(tmpBuf), "%d°C", carData.tempC);
    else snprintf(tmpBuf, sizeof(tmpBuf), "--°C");
    canvas.setTextColor(COLOR_UI_TEXT);
    canvas.drawString(tmpBuf, 174, cardY + 38);

    presentCanvas();
}

// ================= 功能菜单 UI (参�?car_old �?Wear OS 弧形列表) =================
void drawMenuUI() {
    const uint32_t now = millis();
    auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    auto easeOutCubic = [](float t) { float f = 1.0f - t; return 1.0f - f * f * f; };
    auto easeInCubic = [](float t) { return t * t * t; };

    float enterP = menuEnterTime ? clamp01((now - menuEnterTime) / 280.0f) : 1.0f;
    enterP = easeOutCubic(enterP);
    float exitP = menuExiting ? easeInCubic(clamp01((now - menuExitStart) / 200.0f)) : 0.0f;
    const float yOff = (1.0f - enterP) * 30.0f + exitP * 26.0f;

    const uint16_t menuBg = 0x0863;       // color888(11,15,25)
    const uint16_t menuGreen = 0x3BED;    // 雾青绿，保留参考样式但压掉荧光�?
    const uint16_t menuText = 0xCEBD;     // color888(200,215,235)
    canvas.fillSprite(menuBg);

    // 上下磁吸滚动：菜单项�?28px 节距排列，选中项始终落在圆�?y=120
    const uint32_t scrollElapsed = now - menuScrollStart;
    float scrollP = scrollElapsed >= 180 ? 1.0f : easeOutCubic(scrollElapsed / 180.0f);
    menuScrollVisual = menuScrollFrom + (menuScrollTo - menuScrollFrom) * scrollP;

    auto drawMenuIcon = [&](int idx, int x, int y, uint16_t c) {
        switch (idx) {
            case 0: // 释放保护板蓝�?
            case 3: // 重新配对保护�?
                canvas.drawLine(x, y - 6, x, y + 6, c);
                canvas.drawLine(x, y - 6, x + 4, y - 3, c);
                canvas.drawLine(x + 4, y - 3, x - 4, y + 3, c);
                canvas.drawLine(x - 4, y - 3, x + 4, y + 3, c);
                canvas.drawLine(x + 4, y + 3, x, y + 6, c);
                break;
            case 1: // 清除本月大计
                canvas.drawCircle(x, y, 5, c);
                canvas.fillTriangle(x + 1, y - 7, x + 7, y - 4, x + 2, y - 2, c);
                break;
            case 2: // 关闭屏幕显示
                canvas.drawRect(x - 6, y - 5, 12, 10, c);
                canvas.drawLine(x - 2, y + 2, x + 2, y - 2, c);
                break;
            case 4: // 电池遥测
                canvas.drawRect(x - 6, y - 4, 12, 8, c);
                canvas.fillRect(x + 6, y - 2, 2, 4, c);
                canvas.drawLine(x - 3, y, x + 3, y, c);
                break;
            case 5: // GPS 卫星
                canvas.drawCircle(x, y, 6, c);
                canvas.fillCircle(x, y, 2, c);
                canvas.drawLine(x, y - 8, x, y - 6, c);
                canvas.drawLine(x, y + 6, x, y + 8, c);
                break;
            default: // 关于
                canvas.drawCircle(x, y, 6, c);
                canvas.fillCircle(x, y - 3, 1, c);
                canvas.fillRect(x - 1, y - 1, 2, 5, c);
                break;
        }
    };

    for (int idx = 0; idx < MENU_TOTAL_ITEMS; idx++) {
        float yOffset = (float)idx * 32.0f - menuScrollVisual * 32.0f;
        int itemY = 120 + (int)yOffset + (int)yOff;
        if (itemY < 30 || itemY > 210) continue;

        // Wear OS 弧形排版：卡片宽度随圆屏弦长变化
        float dy = fabsf((float)(itemY - 120));
        float dx = sqrtf(fmaxf(0.0f, 14400.0f - dy * dy));
        int startX = 120 - (int)dx + 12;
        int boxW = (int)(2.0f * dx) - 24;
        if (boxW < 60) continue;

        bool selected = fabsf((float)idx - menuScrollVisual) < 0.5f;
        if (selected) {
            canvas.fillRoundRect(startX, itemY - 13, boxW, 26, 13, menuGreen);
        }

        uint16_t fg = selected ? TFT_BLACK : menuText;
        drawMenuIcon(idx, startX + 15, itemY, selected ? TFT_BLACK : menuGreen);

        canvas.setFont(&fonts::efontCN_14);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(fg);
        const char* label = (g_simulationMode && idx == 0) ? "1. 退出模拟模式" : MENU_ITEMS[idx];
        canvas.drawString(label, startX + (boxW / 2) + 10, itemY);
    }

    // 切出时整体压暗，避免硬切
    if (menuExiting) {
        canvas.fillRectAlpha(0, 0, 240, 240, (uint8_t)(exitP * 150.0f), menuBg);
    }
    presentCanvas();
}

// ================= 功能页统一组件 =================
void drawPanelFrame(const char* title, int page, int total) {
    panelCardSeq = 0;
    uint32_t age = millis() - panelAnimStart;
    float p = age >= 220 ? 1.0f : (float)age / 220.0f;
    p = p * p * (3.0f - 2.0f * p);
    int off = (int)((1.0f - p) * 10.0f);

    canvas.fillSprite(COLOR_UI_BG);
    canvas.drawCircle(120, 120, 119, 0x18C3);
    canvas.fillRoundRect(88, 12 + off, 64, 3, 1, COLOR_UI_ACCENT);
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextDatum(top_center);
    canvas.setTextColor(blend565(COLOR_UI_BG, COLOR_UI_TEXT, (uint8_t)(p * 255.0f)));
    canvas.drawString(title, 120, 20 + off);

    if (total > 1) {
        char pageBuf[8];
        snprintf(pageBuf, sizeof(pageBuf), "%d/%d", page, total);
        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextColor(blend565(COLOR_UI_BG, COLOR_UI_ACCENT, (uint8_t)(p * 255.0f)));
        canvas.drawString(pageBuf, 120, 220 + off);
    }
}

void drawInfoCard(int x, int y, int w, int h, const char* label, const char* value,
                  uint16_t valueColor = COLOR_UI_TEXT) {
    uint8_t idx = panelCardSeq++;
    uint32_t age = millis() - panelAnimStart;
    float p = (float)age - (float)idx * 42.0f;
    p = p < 0.0f ? 0.0f : (p > 190.0f ? 1.0f : p / 190.0f);
    p = p * p * (3.0f - 2.0f * p);
    int oy = (int)((1.0f - p) * 9.0f);

    uint16_t cardColor = blend565(COLOR_UI_BG, COLOR_UI_CARD, (uint8_t)(p * 255.0f));
    uint16_t lineColor = blend565(COLOR_UI_BG, COLOR_UI_LINE, (uint8_t)(p * 255.0f));
    canvas.fillRoundRect(x, y + oy, w, h, 10, cardColor);
    canvas.drawRoundRect(x, y + oy, w, h, 10, lineColor);
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(blend565(COLOR_UI_BG, COLOR_UI_MUTED, (uint8_t)(p * 255.0f)));
    canvas.drawString(label, x + 9, y + 5 + oy);
    canvas.setFont(&fonts::efontCN_14);
    if (canvas.textWidth(value) > w - 16) canvas.setFont(&fonts::efontCN_12);
    canvas.setTextColor(blend565(COLOR_UI_BG, valueColor, (uint8_t)(p * 255.0f)));
    canvas.drawString(value, x + 9, y + 19 + oy);
}

void drawArcGauge(int cx, int cy, int rOut, int rIn, float start, float span,
                  float pct, uint16_t color) {
    pct = pct < 0.0f ? 0.0f : (pct > 1.0f ? 1.0f : pct);
    float reveal = (float)(millis() - panelAnimStart) / 500.0f;
    reveal = reveal < 0.0f ? 0.0f : (reveal > 1.0f ? 1.0f : reveal);
    reveal = reveal * reveal * (3.0f - 2.0f * reveal);
    pct *= reveal;
    fillArcRound(cx, cy, rOut, rIn, start, span, COLOR_ARC_TRACK);
    if (pct > 0.01f) {
        fillArcRound(cx, cy, rOut, rIn, start, span * pct, color);
    }
}

void drawClearMonthUI() {
    drawPanelFrame("清除本月大计", 1, 1);

    canvas.fillCircle(120, 58, 21, 0x3000);
    canvas.drawCircle(120, 58, 21, COLOR_RED_ACCENT);
    canvas.fillRoundRect(118, 46, 4, 14, 2, COLOR_RED_ACCENT);
    canvas.fillCircle(120, 66, 2, COLOR_RED_ACCENT);

    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COLOR_UI_MUTED);
    canvas.drawString("本月累计", 120, 92);

    canvas.setFont(&fonts::efontCN_24);
    canvas.setTextColor(COLOR_WHITE_TEXT);
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f km", carData.monthKm);
    canvas.drawString(buf, 120, 112);

    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextColor(COLOR_YELLOW_SIGNAL);
    canvas.drawString("确认将本月里程清零？", 120, 136);

    int16_t btnW = 84, btnH = 34;
    int16_t b1X = 26, b2X = 130, btnY = 156;

    if (clearConfirmCursor == 0) {
        canvas.fillRoundRect(b1X, btnY, btnW, btnH, 17, COLOR_UI_ACCENT);
        canvas.setTextColor(COLOR_UI_BG);
    } else {
        canvas.fillRoundRect(b1X, btnY, btnW, btnH, 17, COLOR_UI_CARD);
        canvas.drawRoundRect(b1X, btnY, btnW, btnH, 17, COLOR_UI_LINE);
        canvas.setTextColor(COLOR_UI_MUTED);
    }
    canvas.setFont(&fonts::efontCN_16);
    canvas.drawString("取消", b1X + btnW / 2, btnY + btnH / 2);

    if (clearConfirmCursor == 1) {
        canvas.fillRoundRect(b2X, btnY, btnW, btnH, 17, COLOR_RED_ACCENT);
        canvas.setTextColor(COLOR_WHITE_TEXT);
    } else {
        canvas.fillRoundRect(b2X, btnY, btnW, btnH, 17, COLOR_UI_CARD);
        canvas.drawRoundRect(b2X, btnY, btnW, btnH, 17, COLOR_UI_LINE);
        canvas.setTextColor(COLOR_UI_MUTED);
    }
    canvas.drawString("确认", b2X + btnW / 2, btnY + btnH / 2);

    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextColor(COLOR_UI_MUTED);
    canvas.drawString("K1/K4 选择   K2 确认   K3 返回", 120, 214);
    presentCanvas();
}

// ================= 电池遥测：三页圆形仪�?=================
void drawBatteryPanelUI() {
    if (batteryPanelPage > 2) batteryPanelPage = 0;
    drawPanelFrame("电池遥测", batteryPanelPage + 1, 3);

    char v[32];
    if (batteryPanelPage == 0) {
        int soc = carData.isLiveBms ?
 carData.soc : 0;
        uint16_t socColor = carData.isLiveBms ? getBatteryArcColor(soc) : COLOR_UI_MUTED;
        drawArcGauge(68, 108, 46, 38, 135.0f, 270.0f, soc / 100.0f, socColor);

        canvas.setFont(&fonts::efontCN_24);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(carData.isLiveBms ? COLOR_WHITE_TEXT : COLOR_UI_MUTED);
        snprintf(v, sizeof(v), "%d", soc);
        canvas.drawString(v, 68, 100);
        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextColor(socColor);
        canvas.drawString("%", 68, 120);
        canvas.setTextColor(COLOR_UI_MUTED);
        canvas.drawString("SOC", 68, 139);

        snprintf(v, sizeof(v), carData.isLiveBms ? "%.2f V" : "--.-- V", carData.voltage);
        drawInfoCard(116, 44, 96, 35, "总压", v);
        snprintf(v, sizeof(v), carData.isLiveBms ? "%.2f A" : "--.-- A", carData.currentA);
        drawInfoCard(116, 83, 96, 35, "电流", v, carData.currentA < -0.2f ? COLOR_UI_ACCENT : COLOR_YELLOW_SIGNAL);
        snprintf(v, sizeof(v), carData.isLiveBms ? "%.0f W" : "-- W", carData.powerW);
        drawInfoCard(116, 122, 96, 35, "功率", v);
        snprintf(v, sizeof(v), carData.isLiveBms ? "%d °C" : "-- °C", carData.tempC);
        drawInfoCard(116, 161, 96, 35, "温度", v, getBatteryTempColor(carData.tempC));

        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(carData.isLiveBms ? COLOR_UI_ACCENT : COLOR_UI_MUTED);
        snprintf(v, sizeof(v), carData.isLiveBms ? "%.1f / %.1f Ah" : "-- / -- Ah",
                 carData.currentAh, carData.totalAh);
        canvas.drawString(v, 68, 174);
        canvas.setTextColor(COLOR_UI_MUTED);
        canvas.drawString("剩余容量", 68, 190);
    } else if (batteryPanelPage == 1) {
        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextDatum(top_center);
        canvas.setTextColor(COLOR_UI_MUTED);
        canvas.drawString("单体压差", 120, 52);

        canvas.setFont(&fonts::efontCN_24);
        canvas.setTextColor(carData.isLiveBms ? getDeltaVColor(carData.deltaV_mV) : COLOR_UI_MUTED);
        snprintf(v, sizeof(v), carData.isLiveBms ? "%d mV" : "-- mV", carData.deltaV_mV);
        canvas.drawString(v, 120, 72);

        float dvPct = carData.isLiveBms ?
 carData.deltaV_mV / 100.0f : 0.0f;
        dvPct = dvPct < 0.0f ? 0.0f : (dvPct > 1.0f ? 1.0f : dvPct);
        canvas.fillRoundRect(35, 108, 170, 10, 5, COLOR_ARC_TRACK);
        if (dvPct > 0.01f) {
            canvas.fillRoundRect(35, 108, (int)(170 * dvPct), 10, 5,
                                 carData.isLiveBms ? getDeltaVColor(carData.deltaV_mV) : COLOR_UI_MUTED);
        }
        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextDatum(top_left);
        canvas.setTextColor(COLOR_UI_MUTED);
        canvas.drawString("0", 36, 122);
        canvas.setTextDatum(top_right);
        canvas.drawString("100 mV", 204, 122);

        snprintf(v, sizeof(v), carData.isLiveBms ? "%.3f V" : "--.--- V", carData.minCell_mV / 1000.0f);
        drawInfoCard(28, 144, 86, 32, "最低单体", v, COLOR_UI_ACCENT);
        snprintf(v, sizeof(v), carData.isLiveBms ? "%.3f V" : "--.--- V", carData.maxCell_mV / 1000.0f);
        drawInfoCard(126, 144, 86, 32, "最高单体", v);

        char health[32];
        uint16_t healthColor = COLOR_UI_MUTED;
        getBatteryHealthInfo(health, sizeof(health), healthColor, false);
        snprintf(v, sizeof(v), "%d%%  %s", carData.isLiveBms ?
 carData.soh : 0, health);
        drawInfoCard(60, 181, 120, 30, "健康度", v, healthColor);
    } else {
        const char* status = !carData.isLiveBms ? "离线" :
                             (carData.isBmsCharging ? "充电中" :
                              (carData.bmsStatus == 3 ? "放电中" : "待机"));
        drawInfoCard(28, 46, 86, 34, "BMS 状态", status,
                     carData.isLiveBms ? COLOR_UI_ACCENT : COLOR_RED_ACCENT);
        drawInfoCard(126, 46, 86, 34, "充电 MOS",
                     carData.chargeMosStatus ? "导通" : "断开",
                     carData.chargeMosStatus ? COLOR_UI_ACCENT : COLOR_UI_MUTED);
        snprintf(v, sizeof(v), carData.isLiveBms ? "%d °C" : "-- °C", carData.tempC);
        drawInfoCard(28, 86, 86, 34, "电池温度", v, getBatteryTempColor(carData.tempC));
        snprintf(v, sizeof(v), "%u 帧", carData.bmsPacketCount);
        drawInfoCard(126, 86, 86, 34, "有效报文", v, COLOR_UI_ACCENT);

        snprintf(v, sizeof(v), carData.isLiveBms ? "%.1f / %.1f Ah" : "-- / -- Ah",
                 carData.currentAh, carData.totalAh);
        drawInfoCard(38, 128, 164, 34, "容量", v);
        snprintf(v, sizeof(v), "%d%%", carData.isLiveBms ?
 carData.soc : 0);
        drawInfoCard(38, 168, 76, 34, "SOC", v, getBatteryArcColor(carData.soc));
        snprintf(v, sizeof(v), "%d%%", carData.isLiveBms ?
 carData.soh : 0);
        drawInfoCard(124, 168, 78, 34, "SOH", v, COLOR_UI_TEXT);

        canvas.setFont(&fonts::FreeSans9pt7b);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(COLOR_UI_MUTED);
        canvas.drawString(pairedBmsMac.isEmpty() ? "MAC: --" : pairedBmsMac.c_str(), 120, 210);
    }
    presentCanvas();
}

// ================= GPS 遥测：三页圆形仪�?=================
void drawGpsPanelUI() {
    if (gpsPanelPage > 2) gpsPanelPage = 0;
    drawPanelFrame("GPS 遥测", gpsPanelPage + 1, 3);

    char v[40];
    if (gpsPanelPage == 0) {
        canvas.fillRoundRect(86, 43, 68, 20, 10,
                             gpsData.isFix ? COLOR_UI_ACCENT_DIM : 0x3000);
        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(gpsData.isFix ? COLOR_UI_ACCENT : COLOR_RED_ACCENT);
        canvas.drawString(gpsData.isFix ? "3D FIX" : "搜星中", 120, 53);

        canvas.setFont(&fonts::efontCN_24);
        canvas.setTextColor(gpsData.isFix ? COLOR_WHITE_TEXT : COLOR_UI_MUTED);
        snprintf(v, sizeof(v), "%d", gpsData.isFix ?
 gpsData.satellites : 0);
        canvas.drawString(v, 54, 92);
        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextColor(COLOR_UI_MUTED);
        canvas.drawString("跟踪卫星", 54, 114);
        snprintf(v, sizeof(v), "视空 %d", gpsData.satellitesInView);
        canvas.setTextColor(COLOR_UI_ACCENT);
        canvas.drawString(v, 54, 130);

        // 圆形星座示意：外圈按 GPS/BDS 颗数分段
        const int cx2 = 174, cy2 = 92;
        canvas.fillArc(cx2, cy2, 39, 35, 0, 360, COLOR_UI_CARD_HI);
        int total = gpsData.gpsViewCount + gpsData.bdsViewCount;
        if (total > 0) {
            float gpsSpan = 360.0f * gpsData.gpsViewCount / total;
            canvas.fillArc(cx2, cy2, 39, 35, 0, gpsSpan, COLOR_UI_ACCENT);
            canvas.fillArc(cx2, cy2, 39, 35, gpsSpan, 360, COLOR_SPEED_AMBER);
        }
        canvas.drawCircle(cx2, cy2, 26, COLOR_UI_LINE);
        canvas.drawCircle(cx2, cy2, 14, COLOR_UI_LINE);
        canvas.fillCircle(cx2, cy2, 3, COLOR_UI_ACCENT);
        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextColor(COLOR_UI_TEXT);
        snprintf(v, sizeof(v), "GPS %d", gpsData.gpsViewCount);
        canvas.drawString(v, cx2, cy2 - 8);
        snprintf(v, sizeof(v), "BDS %d", gpsData.bdsViewCount);
        canvas.setTextColor(COLOR_SPEED_AMBER);
        canvas.drawString(v, cx2, cy2 + 6);

        snprintf(v, sizeof(v), "%s  %s", gpsData.latStr, gpsData.lonStr);
        drawInfoCard(34, 154, 172, 32, "当前位置", v, COLOR_UI_TEXT);

        snprintf(v, sizeof(v), gpsData.isFix ? "海拔 %.1f m" : "等待定位", gpsData.altitudeM);
        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(COLOR_UI_MUTED);
        canvas.drawString(v, 120, 196);
    } else if (gpsPanelPage == 1) {
        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextDatum(top_center);
        canvas.setTextColor(COLOR_UI_MUTED);
        canvas.drawString("最高载噪比 C/N0", 120, 52);
        canvas.setFont(&fonts::efontCN_24);
        canvas.setTextColor(gpsData.maxSnr >= 26 ? COLOR_UI_ACCENT : COLOR_YELLOW_SIGNAL);
        snprintf(v, sizeof(v), "%d dB", gpsData.maxSnr);
        canvas.drawString(v, 120, 70);

        float snrPct = gpsData.maxSnr / 50.0f;
        snrPct = snrPct < 0.0f ? 0.0f : (snrPct > 1.0f ? 1.0f : snrPct);
        canvas.fillRoundRect(36, 100, 168, 9, 4, COLOR_ARC_TRACK);
        if (snrPct > 0.01f) {
            canvas.fillRoundRect(36, 100, (int)(168 * snrPct), 9, 4,
                                 gpsData.maxSnr >= 26 ? COLOR_UI_ACCENT : COLOR_YELLOW_SIGNAL);
        }
        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(COLOR_UI_MUTED);
        const char* quality = gpsData.maxSnr >= 35 ? "信号极佳" :
                              (gpsData.maxSnr >= 26 ? "信号良好" :
                               (gpsData.maxSnr >= 15 ? "信号微弱" : "无信号"));
        canvas.drawString(quality, 120, 118);

        snprintf(v, sizeof(v), "%d", gpsData.gpsViewCount);
        drawInfoCard(28, 132, 86, 30, "GPS 卫星", v, COLOR_UI_ACCENT);
        snprintf(v, sizeof(v), "%d", gpsData.bdsViewCount);
        drawInfoCard(126, 132, 86, 30, "北斗卫星", v, COLOR_SPEED_AMBER);
        snprintf(v, sizeof(v), "%u", gpsData.nmeaPacketCount);
        drawInfoCard(28, 168, 86, 30, "NMEA 帧", v);
        snprintf(v, sizeof(v), "%u", gpsData.nmeaErrorCount);
        drawInfoCard(126, 168, 86, 30, "校验错误", v,
                     gpsData.nmeaErrorCount ? COLOR_YELLOW_SIGNAL : COLOR_UI_MUTED);
    } else {
        canvas.setFont(&fonts::efontCN_24);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(COLOR_WHITE_TEXT);
        snprintf(v, sizeof(v), "%.1f", gpsData.speedKmH);
        canvas.drawString(v, 76, 84);
        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextColor(COLOR_UI_ACCENT);
        canvas.drawString("km/h", 76, 106);

        // 简易罗�?
        const int compassX = 174, compassY = 90;
        canvas.drawCircle(compassX, compassY, 34, COLOR_UI_LINE);
        canvas.drawCircle(compassX, compassY, 27, COLOR_UI_CARD_HI);
        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(COLOR_UI_MUTED);
        canvas.drawString("N", compassX, compassY - 24);
        float rad = (gpsData.courseDeg - 90.0f) * 0.0174533f;
        int ax = compassX + (int)(cosf(rad) * 24);
        int ay = compassY + (int)(sinf(rad) * 24);
        canvas.drawLine(compassX, compassY, ax, ay, COLOR_UI_ACCENT);
        canvas.fillTriangle(ax, ay, ax - 5, ay + 8, ax + 3, ay + 6, COLOR_UI_ACCENT);

        snprintf(v, sizeof(v), "%.1f m", gpsData.altitudeM);
        drawInfoCard(28, 134, 84, 32, "海拔", v);
        snprintf(v, sizeof(v), "%.0f°", gpsData.courseDeg);
        drawInfoCard(126, 134, 84, 32, "航向", v);

        if (gpsData.timeValid) {
            snprintf(v, sizeof(v), "%02d:%02d:%02d", gpsData.hour, gpsData.minute, gpsData.second);
        } else {
            snprintf(v, sizeof(v), "--:--:--");
        }
        drawInfoCard(48, 174, 144, 32, "卫星时间", v, COLOR_UI_ACCENT);
    }
    presentCanvas();
}

// ================= 关于系统：统一功能页风�?=================
void drawAboutPanelUI() {
    drawPanelFrame("关于", 1, 1);

    const int16_t logoCx = 120, logoCy = 54;
    canvas.fillCircle(logoCx, logoCy, 7, COLOR_BLUE_ACCENT);
    for (int i = 0; i < 8; i++) {
        float angle = i * (3.14159265f / 4.0f);
        int16_t dx = logoCx + (int16_t)(cosf(angle) * 16.0f);
        int16_t dy = logoCy + (int16_t)(sinf(angle) * 16.0f);
        canvas.fillCircle(dx, dy, 3, COLOR_BLUE_ACCENT);
    }

    canvas.setFont(&fonts::efontCN_16);
    int32_t w1 = canvas.textWidth("凛OS ");
    int32_t w2 = canvas.textWidth("car");
    int16_t startX = 120 - ((w1 + w2) / 2);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(COLOR_WHITE_TEXT);
    canvas.drawString("凛OS ", startX, 82);
    canvas.setTextColor(COLOR_RED_ACCENT);
    canvas.drawString("car", startX + w1, 82);

    char v[32];
    drawInfoCard(28, 104, 86, 32, "固件版本", "v4.7", COLOR_UI_ACCENT);
    snprintf(v, sizeof(v), "%d MHz", ESP.getCpuFreqMHz());
    drawInfoCard(126, 104, 86, 32, "主控芯片", v);
    snprintf(v, sizeof(v), "%u KB", ESP.getFreeHeap() / 1024);
    drawInfoCard(28, 142, 86, 32, "剩余内存", v, COLOR_UI_ACCENT);
    uint32_t sec = millis() / 1000;
    snprintf(v, sizeof(v), "%02u:%02u:%02u", sec / 3600, (sec % 3600) / 60, sec % 60);
    drawInfoCard(126, 142, 86, 32, "运行时间", v);

    drawInfoCard(48, 180, 144, 30, "GPS 接口", "115200 @ 10Hz", COLOR_UI_ACCENT);
    presentCanvas();
}

// ================= 连接保护板：独立连接动画�?=================
void drawConnectingUI(const char* deviceName) {
    canvas.fillSprite(COLOR_UI_BG);

    // 外圈呼吸�?
    static uint8_t phase = 0;
    phase = (phase + 1) % 8;
    for (int i = 0; i < 8; i++) {
        uint8_t alpha = (uint8_t)(40 + ((i + phase) % 8) * 24);
        uint16_t c = blend565(COLOR_UI_BG, COLOR_UI_ACCENT, alpha);
        float a0 = i * 45.0f;
        canvas.fillArc(120, 112, 92, 89, a0, a0 + 28.0f, c);
    }

    // 中央设备图标
    canvas.fillCircle(120, 96, 26, COLOR_UI_CARD);
    canvas.drawCircle(120, 96, 26, COLOR_UI_ACCENT_DIM);
    canvas.drawLine(120, 82, 120, 110, COLOR_UI_ACCENT);
    canvas.drawLine(120, 82, 131, 89, COLOR_UI_ACCENT);
    canvas.drawLine(131, 89, 111, 104, COLOR_UI_ACCENT);
    canvas.drawLine(111, 89, 131, 104, COLOR_UI_ACCENT);
    canvas.drawLine(131, 104, 120, 110, COLOR_UI_ACCENT);

    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextDatum(top_center);
    canvas.setTextColor(COLOR_UI_TEXT);
    canvas.drawString("正在连接保护板", 120, 132);

    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextColor(COLOR_UI_ACCENT);
    String name = deviceName ? deviceName : "";
    if (name.length() > 14) name = name.substring(0, 14) + "...";
    canvas.drawString(name.c_str(), 120, 158);

    canvas.setFont(&fonts::efontCN_10);
    canvas.setTextColor(COLOR_UI_MUTED);
    canvas.drawString("请保持保护板处于唤醒状态", 120, 182);

    presentCanvas();
}

// ================= 开关设置：功能页内的单开�?=================
void drawSwitchPanelUI() {
    drawPanelFrame("灵动数据开关", 1, 1);

    const int16_t cardX = 22, cardY = 62, cardW = 196, cardH = 86;
    canvas.fillRoundRect(cardX, cardY, cardW, cardH, 20, COLOR_UI_CARD);
    canvas.drawRoundRect(cardX, cardY, cardW, cardH, 20, COLOR_UI_LINE);

    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(COLOR_UI_TEXT);
    canvas.drawString("灵动数据开关", cardX + 18, cardY + 26);

    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextColor(COLOR_UI_MUTED);
    canvas.drawString("按骑行状态自动切换", cardX + 18, cardY + 52);

    // 拨动开�?
    const int16_t swX = cardX + cardW - 60, swY = cardY + 16, swW = 44, swH = 24;
    uint16_t trackColor = smartDataEnabled ? COLOR_UI_ACCENT : 0x3186;
    canvas.fillRoundRect(swX, swY, swW, swH, swH / 2, trackColor);
    canvas.fillCircle(smartDataEnabled ? swX + swW - 12 : swX + 12, swY + swH / 2, 9, COLOR_UI_BG);

    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(smartDataEnabled ? COLOR_UI_ACCENT : COLOR_UI_MUTED);
    canvas.drawString(smartDataEnabled ? "已开启" : "已关闭", cardX + 18, cardY + 72);

    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COLOR_UI_MUTED);
    canvas.drawString("K2 切换    K3 返回", 120, 180);

    canvas.fillRoundRect(44, 194, 152, 26, 13,
                         smartDataEnabled ? COLOR_UI_ACCENT_DIM : COLOR_UI_CARD);
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextColor(smartDataEnabled ? COLOR_UI_ACCENT : COLOR_UI_MUTED);
    canvas.drawString(smartDataEnabled ? "关闭后手动切换" : "开启后自动感知", 120, 207);

    presentCanvas();
}

// ================= 保护板蓝牙释放独立界�?=================
void drawBmsReleasedUI() {
    drawPanelFrame("蓝牙释放", 1, 1);

    static float ringAngle = 0.0f;
    ringAngle += 2.5f;
    if (ringAngle >= 360.0f) ringAngle -= 360.0f;
    canvas.fillArc(120, 120, 117, 114, ringAngle, ringAngle + 50.0f, COLOR_UI_ACCENT);
    canvas.fillArc(120, 120, 117, 114, ringAngle + 180.0f, ringAngle + 230.0f, COLOR_UI_ACCENT);

    // 蓝牙矢量图标：下移，给顶部标题留出呼吸空间
    const int bx = 120, by = 66;
    canvas.fillCircle(bx, by, 23, COLOR_UI_CARD);
    canvas.drawCircle(bx, by, 23, COLOR_UI_ACCENT_DIM);
    canvas.drawLine(bx, by - 12, bx, by + 12, COLOR_UI_ACCENT);
    canvas.drawLine(bx, by - 12, bx + 8, by - 6, COLOR_UI_ACCENT);
    canvas.drawLine(bx + 8, by - 6, bx - 6, by + 6, COLOR_UI_ACCENT);
    canvas.drawLine(bx - 6, by - 6, bx + 8, by + 6, COLOR_UI_ACCENT);
    canvas.drawLine(bx + 8, by + 6, bx, by + 12, COLOR_UI_ACCENT);

    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextDatum(top_center);
    canvas.setTextColor(COLOR_UI_ACCENT);
    canvas.drawString("保护板蓝牙已释放", 120, 94);

    // 独立信息卡：标签和值分两行居中，避免与卡片边框挤在一起
    const int cardX = 34, cardY = 116, cardW = 172, cardH = 48;
    canvas.fillRoundRect(cardX, cardY, cardW, cardH, 10, COLOR_UI_CARD);
    canvas.drawRoundRect(cardX, cardY, cardW, cardH, 10, COLOR_UI_LINE);

    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextColor(COLOR_UI_MUTED);
    canvas.drawString("手机蚂蚁 App", 120, cardY + 7);

    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextColor(COLOR_UI_TEXT);
    canvas.drawString("现在可以连接保护板", 120, cardY + 25);

    // 说明分两行，底部按钮单独占一行
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextColor(COLOR_UI_MUTED);
    canvas.drawString("连接完成后", 120, 172);
    canvas.drawString("按任意键恢复接管", 120, 188);

    canvas.fillRoundRect(70, 202, 100, 24, 12, COLOR_UI_ACCENT);
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COLOR_UI_BG);
    canvas.drawString("按任意键恢复", 120, 214);

    presentCanvas();
}

// ================= 息屏与唤醒控�?=================
void turnScreenOff() {
    canvas.fillSprite(0x0000);
    presentCanvas();
    lcd.sleep();
}

void turnScreenOn() {
    lcd.wakeup();
    lcd.setBrightness(255);
}

// ================= Setup & Loop 主流�?=================
void setup() {
    Serial.begin(115200);

#ifdef SIM_BUILD
    // PC 预览：只初始�?SDL 显示器，不碰 GPS/BLE/NVS 硬件
    lcd.init();
    lcd.setRotation(0);
    lcd.setBrightness(255);
    canvas.setColorDepth(16);
    if (!canvas.createSprite(240, 240) || canvas.getBuffer() == nullptr) {
        Serial.println("[SIM][FATAL] Sprite 分配失败");
        return;
    }
    playBootAnimation();
    enterSimulationMode();
    return;
#else
    // 1. 显式配置模拟引脚为高阻态输�?(消灭 Bootloader 内部上下拉电阻导致的 ADC 键值漂�?
    pinMode(PIN_KEY_ADC, INPUT);
    pinMode(PIN_POWER_ADC, INPUT);
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);

    // 2. GPS 硬件串口初始化、自动协商至 115200 bps 并激活 10Hz (100ms) 极速刷新
    initGpsFast();
    Serial.println("[SYSTEM] 凛OS 车机系统启动");

    // 3. 屏幕驱动�?112.5KB 全屏显存完整性断言
    lcd.init();
    lcd.setRotation(0);
    lcd.setBrightness(255);
    canvas.setColorDepth(16);
    if (!canvas.createSprite(240, 240) || canvas.getBuffer() == nullptr) {
        Serial.println("[FATAL] 112.5KB 显存 Sprite 分配失败！内部 SRAM 堆内存不足！");
        lcd.fillScreen(TFT_RED);
        while (1) delay(1000);
    }

    // 4. NVS 命名空间初始化与异常处理
    if (!prefs.begin("rin_os", false)) {
        Serial.println("[NVS] 警告: NVS 分区打开异常");
    }
    carData.monthKm = prefs.getFloat("month_odo", 0.0f);
    carData.lastSavedMonthKm = carData.monthKm;
    smartDataEnabled = prefs.getBool("smart_data", true);
    Serial.printf("[NVS] 读取到历史本月大计 %.2f km，灵动数据: %s\n", carData.monthKm, smartDataEnabled ? "开" : "关");

    // 5. 蓝牙协议栈与全模式满血射频增益 (+9dBm)
    NimBLEDevice::init("RinOS-Car-Dash");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_DEFAULT);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_ADV);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_SCAN);

    // 6. 开机动画
    playBootAnimation();

    // 7. 检查保护板配对记忆与自愈回�?
    String savedBms = prefs.getString("bms_mac", "");
    if (!savedBms.isEmpty()) {
        pairedBmsName = prefs.getString("bms_name", "ANT-BMS");
        pairedBmsMac  = savedBms;

        Serial.printf("[BLE] 发现记忆保护板 %s [%s]，尝试回连...\n", pairedBmsName.c_str(), pairedBmsMac.c_str());
        BleDeviceItem bmsDev;
        bmsDev.name = pairedBmsName.c_str();
        bmsDev.address = NimBLEAddress(pairedBmsMac.c_str());
        bmsDev.isTarget = true;
        bmsDev.rssi = -60;
        requestStateTransition(STATE_CONNECTING_BMS);
        drawConnectingUI(pairedBmsName.c_str());
        if (connectToBms(bmsDev, false)) {
            requestStateTransition(STATE_DASHBOARD_MAIN);
            playDashboardSweepAnimation();
        } else {
            requestStateTransition(STATE_WIZARD_BMS);
            drawDeviceListUI("连接保护板");
            startBleScan();
        }
    } else {
        requestStateTransition(STATE_WIZARD_BMS);
        drawDeviceListUI("连接保护板");
        startBleScan();
    }
#endif
}

void loop() {
    // 1. 实时读取物理按键
    KeyEvent key = scanKeypad();
    drainScanQueue();

    // App 退场：先让当前功能页淡出 180ms，再切回菜单，避免硬切
    if (appExitActive) {
        if (millis() - appExitStart >= 180) {
            appExitActive = false;
            requestStateTransition(appExitTarget);
            if (appExitTarget == STATE_MENU_MAIN) {
                menuEnterTime = millis();
                menuExiting = false;
                menuScrollVisual = (float)menuCursor;
                menuScrollFrom = (float)menuCursor;
                menuScrollTo = (float)menuCursor;
                menuScrollStart = millis();
            }
        }
        key = KEY_EVENT_NONE;
    } else if (key == KEY_EVENT_K3 && isAppPanelState(currentState)) {
        // 所有功能 app 的 K3 统一走淡出退场
        beginAppExit(STATE_MENU_MAIN);
        key = KEY_EVENT_NONE;
    }

#ifdef SIM_BUILD
    // PC 预览快捷键：F1 回配网向导，F2 直达充电大屏
    if (simHostWizardRequested()) {
        exitSimulationMode();
        requestStateTransition(STATE_WIZARD_BMS);
        isScanning = false;
        statusMessage = "";
        drawDeviceListUI("连接保护板");
        return;
    }
    if (simHostChargingRequested()) {
        enterChargingPreview();
        return;
    }
    if (simHostReleaseRequested()) {
        requestStateTransition(STATE_BMS_RELEASED);
        return;
    }
#endif

    // 2. 模拟模式使用内置假数据，否则实时非阻塞解�?GPS NMEA 与航程积�?
    if (g_simulationMode) {
        updateSimulation();
    } else {
        updateGps();
    }

    // 3. 全局保护板定时轮询与掉线自愈回连守护 (模拟模式不访问真�?BMS)
    if (!g_simulationMode) {
        updateBmsPolling();
        // 4. 实时检查动能回收过滤与真实插枪充电状态转移
        checkChargingStateTransitions();
    }

    // 4. 灵动数据：根据车速、海拔、功率上下文更新上下副显
    updateSmartSubdata();

    // K2 长按 1.8 秒：在任意界面强制重新配对保护板
    if (key == KEY_EVENT_K2_LONG) {
        Serial.println("[SYSTEM] K2 长按触发：清除保护板记忆，重新扫描配对！");
        // 息屏状态下长按也必须先点亮，否则状态已切到向导但屏幕仍黑
        lcd.wakeup();
        lcd.setBrightness(255);
        exitSimulationMode();
        prefs.remove("bms_mac");
        prefs.remove("bms_name");
        pairedBmsMac = "";
        pairedBmsName = "";
        if (pBmsClient && pBmsClient->isConnected()) {
            pBmsClient->disconnect();
        }
        requestStateTransition(STATE_WIZARD_BMS);
        drawDeviceListUI("连接保护板");
        startBleScan();
        return;
    }

    // K3 长按 5 秒：全局卡死紧急强制重�?
    if (key == KEY_EVENT_K3_LONG) {
        Serial.println("[EMERGENCY] K3 长按 5s 触发！全局卡死紧急强制重启系统！");
        if (!g_simulationMode && carData.monthKm != carData.lastSavedMonthKm) {
            prefs.putFloat("month_odo", carData.monthKm);
        }
        lcd.wakeup();
        lcd.setBrightness(255);
        canvas.fillSprite(COLOR_BG);
        canvas.setFont(&fonts::efontCN_24);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(COLOR_RED_ACCENT);
        canvas.drawString("系统强行重启...", 120, 120);
        presentCanvas();
        delay(300);
        esp_restart();
        return;
    }

    switch (currentState) {
        // ================= 步骤 1：连接保护板向导 =================
        case STATE_WIZARD_BMS: {
            if (key == KEY_EVENT_K1) {
                portENTER_CRITICAL(&bleScanMux);
                int count = (int)scannedDevices.size();
                if (count > 0) {
                    selectedIndex = (selectedIndex - 1 + count) % count;
                }
                portEXIT_CRITICAL(&bleScanMux);
            } else if (key == KEY_EVENT_K4) {
                portENTER_CRITICAL(&bleScanMux);
                int count = (int)scannedDevices.size();
                if (count > 0) {
                    selectedIndex = (selectedIndex + 1) % count;
                }
                portEXIT_CRITICAL(&bleScanMux);
            } else if (key == KEY_EVENT_K2) {
                BleDeviceItem targetItem;
                bool hasTarget = false;
                portENTER_CRITICAL(&bleScanMux);
                if (selectedIndex >= 0 && selectedIndex < (int)scannedDevices.size()) {
                    targetItem = scannedDevices[selectedIndex];
                    hasTarget = true;
                }
                portEXIT_CRITICAL(&bleScanMux);

                if (hasTarget) {
                    if (NimBLEDevice::getScan()->isScanning()) {
                        NimBLEDevice::getScan()->stop();
                    }
                    requestStateTransition(STATE_CONNECTING_BMS);
                    drawConnectingUI(targetItem.name.c_str());
                    if (connectToBms(targetItem, false)) {
                        requestStateTransition(STATE_DASHBOARD_MAIN);
                        playDashboardSweepAnimation();
                    } else {
                        requestStateTransition(STATE_WIZARD_BMS);
                        drawDeviceListUI("连接保护板");
                    }
                }
            } else if (key == KEY_EVENT_K3) {
                bool emptyList = false;
                portENTER_CRITICAL(&bleScanMux);
                emptyList = scannedDevices.empty();
                portEXIT_CRITICAL(&bleScanMux);

                if (emptyList) {
                    startBleScan();
                } else {
                    if (NimBLEDevice::getScan()->isScanning()) {
                        NimBLEDevice::getScan()->stop();
                    }
                    requestStateTransition(STATE_DASHBOARD_MAIN);
                }
            }

            if (currentState == STATE_WIZARD_BMS) {
                drawDeviceListUI("连接保护板");
            }
            break;
        }

        // ================= 步骤 2：主仪表�?=================
        case STATE_DASHBOARD_MAIN: {
            // K1: 切换下半区模�?(0=容量Ah, 1=功率电流, 2=压差温度健康, 3=总压状�?
            if (key == KEY_EVENT_K1) {
                bottomSubMode = (bottomSubMode + 1) % 4;
                bottomSubAnimStart = millis();
                bottomManualUntil = millis() + 8000;
            }
            // K4: 切换上半区模�?(0=小计TRIP, 1=大计MONTH, 2=卫星时钟, 3=海拔高度)
            else if (key == KEY_EVENT_K4) {
                topSubMode = (topSubMode + 1) % 4;
                topSubAnimStart = millis();
                topManualUntil = millis() + 8000;
            }
            // K2: 桌面点击唤起功能菜单�?
            else if (key == KEY_EVENT_K2) {
                requestStateTransition(STATE_MENU_MAIN);
                menuCursor = 0;
                menuEnterTime = millis();
                menuExiting = false;
                menuScrollVisual = 0.0f;
                menuScrollFrom = 0.0f;
                menuScrollTo = 0.0f;
                menuScrollStart = millis();
                drawMenuUI();
                break;
            }
            // K3: 如果正处于物理充电中且之前手动退出了充电大屏，按 K3 可再次进入全屏充电大�?
            else if (key == KEY_EVENT_K3 && isBmsChargingCurrent()) {
                Serial.println("[CHG] 用户在主桌面按 K3，重新进入全屏充电大屏");
                requestStateTransition(STATE_CHARGING_DASH);
                g_manualExitCharging = false;
                chargeEnterTime = millis();
                break;
            }

            drawMainDashboardUI();
            break;
        }

        // ================= 步骤 2.2：独占全屏充电动�?=================
        case STATE_CHARGING_DASH: {
            // 在充电大屏按任意按键退出回主仪表盘，并锁定避免再次弹回
            if (key != KEY_EVENT_NONE) {
                Serial.printf("[CHG] 用户在充电大屏按下按键 (Key=%d)，手动退出至主桌面！\n", key);
                requestStateTransition(STATE_DASHBOARD_MAIN);
                g_manualExitCharging = true;
                break;
            }
            drawChargingUI();
            break;
        }

        // ================= 步骤 3：功能菜�?=================
        case STATE_MENU_MAIN: {
            // 切出动画：先让菜单下坠淡出，再回主表�?
            if (menuExiting) {
                if (millis() - menuExitStart >= 210) {
                    menuExiting = false;
                    requestStateTransition(STATE_DASHBOARD_MAIN);
                } else {
                    drawMenuUI();
                }
                break;
            }

            if (key == KEY_EVENT_K1) {
                menuCursor = (menuCursor - 1 + MENU_TOTAL_ITEMS) % MENU_TOTAL_ITEMS;
                menuScrollFrom = menuScrollVisual;
                menuScrollTo = (float)menuCursor;
                menuScrollStart = millis();
            } else if (key == KEY_EVENT_K4) {
                menuCursor = (menuCursor + 1) % MENU_TOTAL_ITEMS;
                menuScrollFrom = menuScrollVisual;
                menuScrollTo = (float)menuCursor;
                menuScrollStart = millis();
            } else if (key == KEY_EVENT_K2) {
                // 确认进入各真实子面板
                switch (menuCursor) {
                    case 0: // 释放保护板蓝�?
                        if (g_simulationMode) {
                            Serial.println("[SIM] 用户选择退出模拟模式，返回配网扫描");
                            exitSimulationMode();
                            requestStateTransition(STATE_WIZARD_BMS);
                            startBleScan();
                            break;
                        }
                        Serial.println("[MENU] 用户选择: 释放保护板蓝牙给手机 App");
                        if (pBmsClient && pBmsClient->isConnected()) {
                            pBmsClient->disconnect();
                        }
                        carData.isLiveBms = false;
                        requestStateTransition(STATE_BMS_RELEASED);
                        break;
                    case 1: // 清除本月大计
                        clearConfirmCursor = 0;
                        requestStateTransition(STATE_PANEL_CLEAR_MONTH);
                        break;
                    case 2: // 关闭屏幕显示
                        Serial.println("[MENU] 用户选择: 关闭屏幕显示 (息屏待机)");
                        requestStateTransition(STATE_SCREEN_OFF);
                        turnScreenOff();
                        break;
                    case 3: // 重新连接保护板
                        exitSimulationMode();
                        prefs.remove("bms_mac");
                        prefs.remove("bms_name");
                        pairedBmsMac = "";
                        pairedBmsName = "";
                        if (pBmsClient && pBmsClient->isConnected()) {
                            pBmsClient->disconnect();
                        }
                        requestStateTransition(STATE_WIZARD_BMS);
                        startBleScan();
                        break;
                    case 4: // 电池面板
                        batteryPanelPage = 0;
                        requestStateTransition(STATE_PANEL_BATTERY);
                        break;
                    case 5: // GPS 参数面板
                        gpsPanelPage = 0;
                        requestStateTransition(STATE_PANEL_GPS);
                        break;
                    case 6: // 开关设�?
                        requestStateTransition(STATE_PANEL_SWITCHES);
                        break;
                    case 7: // 关于系统
                        requestStateTransition(STATE_PANEL_ABOUT);
                        break;
                }
            } else if (key == KEY_EVENT_K3) {
                // 启动菜单切出动画，动画结束后回主桌面
                menuExiting = true;
                menuExitStart = millis();
            }

            if (currentState == STATE_MENU_MAIN) {
                drawMenuUI();
            }
            break;
        }

        // ================= 步骤 4.1：清除本月大计确认面�?=================
        case STATE_PANEL_CLEAR_MONTH: {
            if (key == KEY_EVENT_K1 || key == KEY_EVENT_K4) {
                clearConfirmCursor = 1 - clearConfirmCursor;
            } else if (key == KEY_EVENT_K2) {
                if (clearConfirmCursor == 1) {
                    // 确认清零
                    carData.monthKm = 0.0f;
                    carData.lastSavedMonthKm = 0.0f;
                    prefs.putFloat("month_odo", 0.0f);
                    Serial.println("[NVS] 本月大计已手动清零");
                    requestStateTransition(STATE_DASHBOARD_MAIN);
                } else {
                    // 取消
                    requestStateTransition(STATE_MENU_MAIN);
                    menuEnterTime = millis();
                menuExiting = false;
                menuScrollVisual = 0.0f;
                menuScrollFrom = 0.0f;
                menuScrollTo = 0.0f;
                menuScrollStart = millis();
                }
            } else if (key == KEY_EVENT_K3) {
                // K3 返回菜单
                requestStateTransition(STATE_MENU_MAIN);
                menuEnterTime = millis();
                menuExiting = false;
                menuScrollVisual = 0.0f;
                menuScrollFrom = 0.0f;
                menuScrollTo = 0.0f;
                menuScrollStart = millis();
            }

            if (currentState == STATE_PANEL_CLEAR_MONTH) {
                drawClearMonthUI();
            }
            break;
        }

        // ================= 步骤 4.2：电池参数面�?=================
        case STATE_PANEL_BATTERY: {
            if (key == KEY_EVENT_K1) {
                batteryPanelPage = (batteryPanelPage + 2) % 3;
                panelAnimStart = millis();
            } else if (key == KEY_EVENT_K4) {
                batteryPanelPage = (batteryPanelPage + 1) % 3;
                panelAnimStart = millis();
            } else if (key == KEY_EVENT_K3) {
                requestStateTransition(STATE_MENU_MAIN);
                menuEnterTime = millis();
                menuExiting = false;
                menuScrollVisual = 0.0f;
                menuScrollFrom = 0.0f;
                menuScrollTo = 0.0f;
                menuScrollStart = millis();
            }
            if (currentState == STATE_PANEL_BATTERY) {
                drawBatteryPanelUI();
            }
            break;
        }

        // ================= 步骤 4.3：GPS 参数面板 =================
        case STATE_PANEL_GPS: {
            if (key == KEY_EVENT_K1) {
                gpsPanelPage = (gpsPanelPage + 2) % 3;
                panelAnimStart = millis();
            } else if (key == KEY_EVENT_K4) {
                gpsPanelPage = (gpsPanelPage + 1) % 3;
                panelAnimStart = millis();
            } else if (key == KEY_EVENT_K3) {
                requestStateTransition(STATE_MENU_MAIN);
                menuEnterTime = millis();
                menuExiting = false;
                menuScrollVisual = 0.0f;
                menuScrollFrom = 0.0f;
                menuScrollTo = 0.0f;
                menuScrollStart = millis();
            }
            if (currentState == STATE_PANEL_GPS) {
                drawGpsPanelUI();
            }
            break;
        }

        // ================= 步骤 4.4：关于系统面�?=================
        case STATE_PANEL_ABOUT: {
            if (key == KEY_EVENT_K3) {
                requestStateTransition(STATE_MENU_MAIN);
                menuEnterTime = millis();
                menuExiting = false;
                menuScrollVisual = 0.0f;
                menuScrollFrom = 0.0f;
                menuScrollTo = 0.0f;
                menuScrollStart = millis();
            }
            if (currentState == STATE_PANEL_ABOUT) {
                drawAboutPanelUI();
            }
            break;
        }

        // ================= 步骤 4.5：开关设置面�?=================
        case STATE_PANEL_SWITCHES: {
            if (key == KEY_EVENT_K2) {
                smartDataEnabled = !smartDataEnabled;
                prefs.putBool("smart_data", smartDataEnabled);
                Serial.printf("[SETTINGS] 灵动数据: %s\n", smartDataEnabled ? "开启" : "关闭");
            } else if (key == KEY_EVENT_K3) {
                requestStateTransition(STATE_MENU_MAIN);
                menuEnterTime = millis();
                menuExiting = false;
                menuScrollVisual = 0.0f;
                menuScrollFrom = 0.0f;
                menuScrollTo = 0.0f;
                menuScrollStart = millis();
            }
            if (currentState == STATE_PANEL_SWITCHES) {
                drawSwitchPanelUI();
            }
            break;
        }

        // ================= 步骤 5：释放保护板蓝牙给手�?App =================
        case STATE_BMS_RELEASED: {
            // 按任意按键 (K1, K2, K3, K4) 均退出释放模式并重新连接保护板
            if (key != KEY_EVENT_NONE) {
                Serial.printf("[BMS] 用户按下按键 (Key=%d)，退出释放模式，恢复保护板自动连接！\n", key);
                if (!pairedBmsMac.isEmpty()) {
                    BleDeviceItem bmsDev;
                    bmsDev.name = pairedBmsName.c_str();
                    bmsDev.address = NimBLEAddress(pairedBmsMac.c_str());
                    requestStateTransition(STATE_CONNECTING_BMS);
                    drawConnectingUI(pairedBmsName.c_str());
                    if (connectToBms(bmsDev, false, 3)) {
                        requestStateTransition(STATE_DASHBOARD_MAIN);
                    } else {
                        requestStateTransition(STATE_WIZARD_BMS);
                        drawDeviceListUI("连接保护板");
                        startBleScan();
                    }
                } else {
                    requestStateTransition(STATE_DASHBOARD_MAIN);
                }
                break;
            }
            drawBmsReleasedUI();
            break;
        }

        // ================= 步骤 6：关闭屏幕显�?(息屏待机状�? =================
        case STATE_SCREEN_OFF: {
            // 息屏待机状态下：后�?GPS 航程积分、BMS 轮询保持运行
            // 任意按键按下，立刻点亮唤醒屏幕并返回主仪表盘�?
            if (key != KEY_EVENT_NONE) {
                Serial.printf("[DISPLAY] 任意物理按键按下 (Key=%d)，唤醒点亮屏幕！\n", key);
                turnScreenOn();
                requestStateTransition(STATE_DASHBOARD_MAIN);
                drawMainDashboardUI();
                break;
            }
            break;
        }

        default:
            break;
    }

    // 50 FPS 硬件平滑锁频 (20ms/�?
    static uint32_t lastFrameTick = 0;
    while (millis() - lastFrameTick < 20) {
        delay(1);
    }
    lastFrameTick = millis();
}
