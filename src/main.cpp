#include <Arduino.h>
#include <vector>
#include <algorithm>
#include <Preferences.h>
#include <time.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <NimBLEDevice.h>

// ================= 引脚与硬件配置 =================
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

LGFX lcd;
LGFX_Sprite canvas(&lcd);
Preferences prefs;

// ================= 现代化机能风配色定义 (RGB565) =================
#define COLOR_BG            0x0000  // 纯黑底色
#define COLOR_CARD_NORMAL   0x10A2  // 默认暗灰深色卡片
#define COLOR_CARD_SELECT   0x116A  // 选中项幽蓝高亮底色
#define COLOR_CYAN_ACCENT   0x07FF  // 霓虹电光青
#define COLOR_BLUE_ACCENT   0x24BD  // 凛OS经典天青蓝
#define COLOR_WHITE_TEXT    0xFFFF  // 高对比纯白
#define COLOR_GRAY_TEXT     0x8410  // 次级灰
#define COLOR_RED_ACCENT    0xFA04  // 极速赛道红
#define COLOR_GREEN_SIGNAL  0x07E0  // 强信号绿
#define COLOR_YELLOW_SIGNAL 0xFDE0  // 中信号黄
#define COLOR_ARC_TRACK     0x18C3  // 弧线底槽灰

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
    if (speed <= 35.0f) {
        return interpolateColor(0x05BF, 0x07E0, speed / 35.0f);
    } else if (speed <= 65.0f) {
        return interpolateColor(0x07E0, 0xFDE0, (speed - 35.0f) / 30.0f);
    } else if (speed <= 85.0f) {
        return interpolateColor(0xFDE0, 0xFD20, (speed - 65.0f) / 20.0f);
    } else {
        return interpolateColor(0xFD20, COLOR_RED_ACCENT, (speed - 85.0f) / 15.0f);
    }
}

uint16_t getBatteryArcColor(int soc) {
    if (soc >= 60) {
        return COLOR_GREEN_SIGNAL;
    } else if (soc >= 25) {
        return interpolateColor(COLOR_YELLOW_SIGNAL, COLOR_GREEN_SIGNAL, (soc - 25) / 35.0f);
    } else if (soc >= 12) {
        return interpolateColor(COLOR_RED_ACCENT, COLOR_YELLOW_SIGNAL, (soc - 12) / 13.0f);
    } else {
        return COLOR_RED_ACCENT;
    }
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
    KEY_EVENT_K1,      // 向上 / 切换下半区模式
    KEY_EVENT_K4,      // 向下 / 切换上半区模式
    KEY_EVENT_K2,      // 确认 / 桌面唤起功能菜单
    KEY_EVENT_K3,      // 全局返回 / 退出
    KEY_EVENT_K2_LONG  // K2 长按 1.8 秒：重新进入蓝牙配对向导
};

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

    uint8_t currentKey = 0;
    if (raw < 600) {
        currentKey = 0;
    } else if (raw < 1550) {
        currentKey = 1; // K1
    } else if (raw < 2350) {
        currentKey = 2; // K2
    } else if (raw < 3350) {
        currentKey = 3; // K3
    } else {
        currentKey = 4; // K4
    }

    if (currentKey != 0) {
        releaseCounter = 0;
        if (!isPressing) {
            isPressing = true;
            activeKey = currentKey;
            pressStartTime = millis();
            longTriggered = false;
        } else {
            if (activeKey == 2 && !longTriggered && (millis() - pressStartTime >= 1800)) {
                longTriggered = true;
                return KEY_EVENT_K2_LONG;
            }
        }
    } else {
        if (isPressing) {
            releaseCounter++;
            if (releaseCounter >= 2) {
                isPressing = false;
                uint32_t duration = millis() - pressStartTime;
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
    STATE_PANEL_ABOUT         // 关于系统面板
};

SystemState currentState = STATE_BOOT_ANIM;

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

// ================= 实时车机与电池数据模型 =================
struct VehicleData {
    float speed = 0.0f;            // 实时车速 km/h (来自 GPS NMEA)
    float tripKm = 0.0f;           // 本次开机小计里程 km (来自 GPS 航程积分)
    float monthKm = 0.0f;          // 本月大计总里程 km (来自 GPS 航程积分并存入 NVS)
    float lastSavedMonthKm = 0.0f; // 上次存入 NVS 的本月里程

    float voltage = 0.0f;          // 电池总压 V (来自 ANT-BMS 实测)
    int soc = 0;                   // 实时电量 0~100% (来自 ANT-BMS 实测)
    float currentAh = 0.0f;        // 剩余容量 Ah (来自 ANT-BMS 实测)
    float totalAh = 0.0f;          // 额定总容量 Ah (来自 ANT-BMS 实测)
    float currentA = 0.0f;         // 瞬时放电/充电电流 A (来自 ANT-BMS 实测，充电为负值)
    float powerW = 0.0f;           // 瞬时功率 W

    int deltaV_mV = 0;             // 单体最大压差 mV (来自 ANT-BMS 实测)
    uint16_t minCell_mV = 0;       // 单体最低电压 mV (来自 ANT-BMS 实测)
    uint16_t maxCell_mV = 0;       // 单体最高电压 mV (来自 ANT-BMS 实测)
    int soh = 100;                 // 电池健康度 % (来自 ANT-BMS 实测)
    int tempC = 0;                 // 电池温度 °C (来自 ANT-BMS 实测 MOS 温度)

    uint8_t bmsStatus = 0;         // 电池运行状态 (0:未知, 1:待机, 2:充电中, 3:放电中, 4:休眠, 5:故障)
    uint8_t chargeMosStatus = 0;   // 充电 MOS 状态 (0:关, 1:开, 2:过充保护, 4:已充满...)
    bool isBmsCharging = false;    // 保护板是否明确判定为外部充电中 (bmsStatus == 0x02)

    bool isLiveBms = false;        // 是否有真实 BMS 数据接入
    uint32_t bmsPacketCount = 0;   // 真实接收到的 BMS 协议包计数
};

VehicleData carData;

// ================= 电池健康度与多重 Buff 综合估算引擎 =================
// 依据单体压差、MOS/电池温度、极限电压进行动力电池健康状态动态解算，并支持多重 Buff 叠加
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

    // 1. 压差检测
    if (carData.deltaV_mV >= 150) {
        isCritDelta = true;
        isDanger = true;
    } else if (carData.deltaV_mV >= 60) {
        isHighDelta = true;
    }

    // 2. 温度检测 (MOS/电芯温度)
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

    // 3. 电压检测 (单体电压优先，整包电压兜底)
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

    // 6. 无异常 Buff 则判定为全绿健康
    if (buffs.empty()) {
        snprintf(outBuf, maxLen, "健康");
        outColor = COLOR_GREEN_SIGNAL;
        return;
    }

    // 7. 颜色分级：欠压/过压/温度过高属于高风险红，一般温高/压差高为警示黄
    if (isOverV || isUnderV || isOverTemp) {
        outColor = COLOR_RED_ACCENT;
    } else {
        outColor = COLOR_YELLOW_SIGNAL;
    }

    // 8. 叠加拼接 (如 "温高+压差高" 或 "欠压+压差过高")
    outBuf[0] = '\0';
    for (size_t i = 0; i < buffs.size(); i++) {
        if (i > 0) strncat(outBuf, "+", maxLen - strlen(outBuf) - 1);
        strncat(outBuf, buffs[i], maxLen - strlen(outBuf) - 1);
    }
}

// ================= GPS 硬件解析数据模型 =================
struct GpsInfo {
    bool isFix = false;            // 是否获得有效 3D 卫星定位
    float speedKmH = 0.0f;         // 实时地面航速 (km/h)
    float courseDeg = 0.0f;        // 地面航向角度 (0~360°)
    int satellites = 0;            // 参与定位解算卫星数 (GGA)
    int satellitesInView = 0;      // 视空/搜星可见卫星总数 (GSV)
    int gpsViewCount = 0;          // GPS 可见卫星数 (GPGSV)
    int bdsViewCount = 0;          // 北斗可见卫星数 (BDGSV/GBGSV)
    int maxSnr = 0;                // 当前搜星最高载噪比/信号强度 (dB-Hz)
    int trackedWithSnr = 0;        // 当前有实际接收信号(SNR>0)的卫星数
    float altitudeM = 0.0f;        // 海拔高度米 (GGA)
    
    char latStr[18] = "--°--.--' -"; // 格式化纬度字符串
    char lonStr[18] = "---°--.--' -";// 格式化经度字符串

    int year = 0;                  // 北京时间年 (如 2026)
    int month = 0;                 // 北京时间月 (1~12)
    int day = 0;                   // 北京时间日 (1~31)
    int hour = 0;                  // 北京时间时 (0~23)
    int minute = 0;                // 北京时间分 (0~59)
    int second = 0;                // 北京时间秒 (0~59)
    bool timeValid = false;        // 卫星授时是否有效
    uint32_t lastFixTime = 0;      // 上次有效定位时间戳 (millis)
    uint32_t lastGsvTime = 0;      // 上次接收 GSV 报文时间戳 (millis)

    uint32_t nmeaPacketCount = 0;  // 真实接收到的 NMEA 报文总数
    uint32_t nmeaErrorCount = 0;   // 校验和错误总数
};

GpsInfo gpsData;

// 副显示模式切换：
// 上半区副显 (K4切换)：0=本次小计TRIP，1=本月大计MONTH，2=GPS卫星时钟，3=GPS海拔高度
int topSubMode = 0;
// 下半区副显 (K1切换)：0=剩余容量Ah/总容量Ah，1=瞬时放电功率/电流，2=电池总压与状态
int bottomSubMode = 0;

// 丝滑物理动效时间戳
uint32_t chargeEnterTime = 0;
uint32_t menuEnterTime = 0;
uint32_t topSubAnimStart = 0;
uint32_t bottomSubAnimStart = 0;

// 功能菜单项光标与选项
int menuCursor = 0;
const int MENU_TOTAL_ITEMS = 5;
const char* MENU_ITEMS[MENU_TOTAL_ITEMS] = {
    "1. 清除本月大计",
    "2. 重新连接保护板",
    "3. 电池面板",
    "4. GPS 参数面板",
    "5. 关于系统"
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

// 开机动画
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
        canvas.pushSprite(0, 0);
        delay(16);
    }
}

// ================= BLE 扫描与连接 =================
// ================= BLE 扫描与多级置顶 =================
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

    // 2. 检查 16-bit 专属服务 UUID (蚂蚁保护板透传服务 0xFFE0 / 0xFEE0)
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

// 多级置顶权重：Rank 0(已配对) < Rank 1(ANT/BMS保护板) < Rank 2(普通设备)
int getDeviceRank(const BleDeviceItem& item) {
    if (!pairedBmsMac.isEmpty() && item.address.toString().c_str() != nullptr) {
        if (pairedBmsMac.equalsIgnoreCase(item.address.toString().c_str())) {
            return 0; // 最高优先级：已配对过的保护板
        }
    }
    if (item.isTarget) {
        return 1; // 第二优先级：ANT / BMS 保护板特征设备
    }
    return 2;     // 第三优先级：普通周边蓝牙设备
}

static portMUX_TYPE bleScanMux = portMUX_INITIALIZER_UNLOCKED;

class DeviceScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* dev) override {
        if (!dev) return;

        NimBLEAddress devAddr = dev->getAddress();
        int devRssi = dev->getRSSI();
        bool devIsTarget = isAntBmsAdvertised(dev);
        std::string rawName = dev->haveName() ? dev->getName() : "";

        portENTER_CRITICAL(&bleScanMux);

        // 1. 查重并更新
        bool found = false;
        for (auto& item : scannedDevices) {
            if (item.address == devAddr) {
                found = true;
                item.rssi = devRssi;
                if (devIsTarget) item.isTarget = true;
                // 若之前没有名称或属于占位名称，此次有真实广播名称则立即更新
                if (!rawName.empty()) {
                    if (item.name.empty() || 
                        item.name.rfind("BLE-", 0) == 0 || 
                        item.name.rfind("ANT-BMS [", 0) == 0) {
                        item.name = rawName;
                    }
                }
                break;
            }
        }

        // 2. 新设备入队
        if (!found) {
            std::string finalName = getDeviceDisplayName(dev, devIsTarget);
            // 过滤极弱信号且非目标的噪音设备 (低于 -85dBm)
            if (devIsTarget || devRssi > -85) {
                scannedDevices.push_back({finalName, devAddr, devRssi, devIsTarget});
            }
        }

        // 3. 严格多级置顶排序：已配对(Rank 0) > ANT保护板(Rank 1) > 普通设备(Rank 2)，同级按 RSSI 降序
        std::sort(scannedDevices.begin(), scannedDevices.end(), [](const BleDeviceItem& a, const BleDeviceItem& b) {
            int rankA = getDeviceRank(a);
            int rankB = getDeviceRank(b);
            if (rankA != rankB) return rankA < rankB;
            return a.rssi > b.rssi;
        });

        portEXIT_CRITICAL(&bleScanMux);
    }
};

static DeviceScanCallbacks* s_scanCallbacks = nullptr;

void scanCompleteCB(NimBLEScanResults results) {
    isScanning = false;
    portENTER_CRITICAL(&bleScanMux);
    statusMessage = scannedDevices.empty() ? "未发现设备" : "";
    portEXIT_CRITICAL(&bleScanMux);
    Serial.printf("[BLE] 异步扫描结束，共发现 %d 个设备\n", (int)results.getCount());
}

void startBleScan() {
    NimBLEScan* pScan = NimBLEDevice::getScan();
    if (pScan->isScanning()) {
        pScan->stop();
    }

    portENTER_CRITICAL(&bleScanMux);
    isScanning = true;
    scannedDevices.clear();
    selectedIndex = 0;
    topVisibleIndex = 0;
    statusMessage = "扫描中...";
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

    // 启动 8 秒非阻塞异步扫描
    if (!pScan->start(8, scanCompleteCB, false)) {
        Serial.println("[BLE] 启动扫描失败！");
        isScanning = false;
        statusMessage = "扫描启动失败";
    } else {
        Serial.println("[BLE] 成功启动 8 秒主动异步扫描");
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

    if (isScanning && listCopy.empty()) {
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(COLOR_WHITE_TEXT);
        canvas.drawString("正在搜索保护板...", 120, 105);

        static float scanAngle = 0;
        scanAngle += 15.0f;
        if (scanAngle >= 360.0f) scanAngle -= 360.0f;
        canvas.drawArc(120, 105, 36, 32, scanAngle, scanAngle + 90, COLOR_CYAN_ACCENT);

        canvas.setFont(&fonts::FreeSans9pt7b);
        canvas.setTextColor(COLOR_GRAY_TEXT);
        canvas.drawString("Searching BLE Devices...", 120, 155);
    } else if (listCopy.empty()) {
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(COLOR_RED_ACCENT);
        canvas.drawString("未发现可用设备", 120, 100);
        canvas.setFont(&fonts::FreeSans9pt7b);
        canvas.setTextColor(COLOR_GRAY_TEXT);
        canvas.drawString("K3: Rescan", 120, 126);
    } else {
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
        canvas.setCursor(105, 194);
        canvas.printf("%02d / %02d", selectedIndex + 1, total);
    }

    if (!statusMessage.isEmpty()) {
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextDatum(top_center);
        canvas.setTextColor(statusMessage.indexOf("失败") >= 0 ? COLOR_RED_ACCENT : COLOR_CYAN_ACCENT);
        canvas.drawString(statusMessage, 120, 192);
    } else if (isScanning) {
        canvas.setFont(&fonts::efontCN_14);
        canvas.setTextDatum(top_center);
        canvas.setTextColor(COLOR_CYAN_ACCENT);
        canvas.drawString("搜索中...", 120, 194);
    }

    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextDatum(top_center);
    canvas.setTextColor(0x52AA);
    canvas.drawString("K1:^  K4:v  K2:OK  K3:Back", 120, 212);
    canvas.pushSprite(0, 0);
}

// ================= 保护板实车数据解析 =================
void bmsNotifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    if (pData == nullptr || length == 0) return;

    if (length >= 2 && pData[0] == 0x7E && pData[1] == 0xA1) {
        bmsRxBuffer.clear();
    } else if (length >= 4 && pData[0] == 0xAA && pData[1] == 0x55 && pData[2] == 0xAA && pData[3] == 0xFF) {
        bmsRxBuffer.clear();
    }

    bmsRxBuffer.insert(bmsRxBuffer.end(), pData, pData + length);

    // 协议 A: 新版 0x7E 0xA1 协议 (以 0xAA 0x55 结尾)
    if (bmsRxBuffer.size() >= 10 && bmsRxBuffer[0] == 0x7E && bmsRxBuffer[1] == 0xA1) {
        if (bmsRxBuffer[bmsRxBuffer.size() - 2] == 0xAA && bmsRxBuffer.back() == 0x55) {
            uint8_t func = bmsRxBuffer[2];
            if (func == 0x11) {
                // 1. 保护板电池运行状态 (Byte 7: 0x01=待机, 0x02=充电中, 0x03=放电中, 0x04=休眠, 0x05=故障)
                // 充电器插枪物理接入充电时，保护板置为 0x02；动能回收制动时仍为 0x03(放电) 或 0x01(待机)
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

                    // 1. 电池健康度 SOH (offset + 44)
                    uint16_t rawSoh = (uint16_t)bmsRxBuffer[44 + offset] | ((uint16_t)bmsRxBuffer[45 + offset] << 8);
                    if (rawSoh > 0 && rawSoh <= 100) {
                        carData.soh = rawSoh;
                    } else if (rawSoh == 0) {
                        carData.soh = 100;
                    }

                    // 2. 电池温度 (MOS 温度，位于 offset + 34)
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
                    carData.bmsPacketCount++;
                }
            }
            bmsRxBuffer.clear();
        }
    }
    // 协议 B: 经典 140 字节协议 (以 0xAA 0x55 0xAA 0xFF 开头)
    else if (bmsRxBuffer.size() >= 140 && bmsRxBuffer[0] == 0xAA && bmsRxBuffer[1] == 0x55 && bmsRxBuffer[2] == 0xAA && bmsRxBuffer[3] == 0xFF) {
        uint16_t rawV = ((uint16_t)bmsRxBuffer[4] << 8) | (uint16_t)bmsRxBuffer[5];
        carData.voltage = rawV * 0.1f;

        // 1. 扫描 32 串单体电压求真实压差与极端电芯
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

        // 3. 健康度
        carData.soh = 100;

        int32_t rawI = ((int32_t)bmsRxBuffer[70] << 24) | ((int32_t)bmsRxBuffer[71] << 16) | ((int32_t)bmsRxBuffer[72] << 8) | (int32_t)bmsRxBuffer[73];
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
        if (rawTotal > 100000) {
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
        carData.bmsPacketCount++;
        bmsRxBuffer.clear();
    }

    if (bmsRxBuffer.size() > 256) {
        bmsRxBuffer.clear();
    }
}

class BmsClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) override {
        Serial.printf("[BMS-CB] 保护板物理连接成功! ConnId=%d\n", pClient ? pClient->getConnId() : -1);
    }
    void onDisconnect(NimBLEClient* pClient) override {
        int err = pClient ? pClient->getLastError() : -1;
        Serial.printf("[BMS-CB] 保护板断开连接! getLastError=%d\n", err);
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

bool connectToBms(const BleDeviceItem& target) {
    statusMessage = "正在连接...";
    drawDeviceListUI("连接保护板");

    if (NimBLEDevice::getScan()->isScanning()) {
        NimBLEDevice::getScan()->stop();
        delay(100);
    }

    if (pBmsClient == nullptr) {
        pBmsClient = NimBLEDevice::createClient();
        pBmsClient->setClientCallbacks(new BmsClientCallbacks());
    }
    pBmsClient->setConnectTimeout(6);

    if (pBmsClient->connect(target.address, false)) {
        pairedBmsName = target.name.c_str();
        pairedBmsMac  = target.address.toString().c_str();

        prefs.putString("bms_name", pairedBmsName);
        prefs.putString("bms_mac", pairedBmsMac);

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
            pBmsRxChar->subscribe(true, bmsNotifyCallback);
            Serial.println("[BMS] 成功订阅 Notify 特征值！");
        } else {
            Serial.println("[BMS] 警告: 未找到可 Notify 特征值！");
        }

        statusMessage = "连接成功";
        drawDeviceListUI("连接保护板");
        delay(400);
        return true;
    } else {
        statusMessage = "连接失败";
        return false;
    }
}

// ================= ATGM336H / AT6558 CASIC 极速配置引擎 =================
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
    GpsSerial.setRxBufferSize(1024); // 扩展串口缓冲区至 1024 字节，承载 10Hz 突发数据

    // 步骤 1：先以出厂默认 9600 波特率启动，发送指令切换 GPS 模块至 115200 bps
    GpsSerial.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    delay(40);
    sendCasicCmd("PCAS01,5"); // PCAS01,5: 切换到 115200 bps
    delay(40);
    GpsSerial.flush();
    GpsSerial.end();
    delay(30);

    // 步骤 2：ESP32 硬件串口切换至 115200 bps
    GpsSerial.begin(115200, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    delay(40);

    // 步骤 3：再次发送 115200 指令 (防止模块此前已保存在 115200 而在步骤1未被切换)
    sendCasicCmd("PCAS01,5");
    delay(40);

    // 步骤 4：设置输出定位更新率为 10Hz (100ms 刷新周期)
    sendCasicCmd("PCAS02,100");
    delay(40);

    // 步骤 5：精简输出语句 (仅输出 GGA, GSV, RMC；彻底关闭 GLL, GSA, VTG, ZDA，消灭芯片负载与串口拥塞)
    sendCasicCmd("PCAS03,1,0,0,1,1,0,0,0,0,0");
    delay(40);

    // 步骤 6：永久保存至内部 Flash / EEPROM (掉电不丢失)
    sendCasicCmd("PCAS00");
    delay(40);

    // 步骤 7：排空握手期间残余杂波
    while (GpsSerial.available()) {
        GpsSerial.read();
    }
    Serial.println("[GPS] 硬件串口握手完成: 115200 bps @ 10Hz 精简语句模式已激活并存入 Flash");
}

// ================= GPS NMEA 语句解析与自然月跨月解算 =================
bool verifyNmeaChecksum(const char* sentence) {
    if (sentence == nullptr || sentence[0] != '$') return false;
    const char* star = strchr(sentence, '*');
    if (!star) return false; // 没有星号校验符说明报文不完整截断，坚决丢弃
    // 确保星号后面至少有两个合法的十六进制字符
    if (!isxdigit(star[1]) || !isxdigit(star[2])) return false;

    uint8_t sum = 0;
    for (const char* p = sentence + 1; p < star; p++) {
        sum ^= (uint8_t)(*p);
    }
    uint8_t expected = (uint8_t)strtoul(star + 1, NULL, 16);
    return (sum == expected);
}

void checkMonthlyRollover(uint32_t currentYM) {
    if (currentYM < 202401) return; // 过滤 GPS 冷启动出厂默认初始年份 (如 1980 或 2000 年)
    static uint32_t s_cachedYM = 0;
    if (s_cachedYM == 0) {
        s_cachedYM = prefs.getUInt("month_ym", 0);
    }
    if (s_cachedYM == 0) {
        s_cachedYM = currentYM;
        prefs.putUInt("month_ym", currentYM);
    } else if (currentYM > s_cachedYM) {
        Serial.printf("[GPS-DATE] 自然月跨月检测触发 (原:%u -> 新:%u)，本月大计归零\n", s_cachedYM, currentYM);
        carData.monthKm = 0.0f;
        carData.lastSavedMonthKm = 0.0f;
        prefs.putFloat("month_odo", 0.0f);
        prefs.putUInt("month_ym", currentYM);
        s_cachedYM = currentYM;
    }
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

    // 1. 解析 RMC 语句 (包含航速、航向、经纬度、UTC时间、定位状态 —— 唯一高权威基准)
    if (strstr(tokens[0], "RMC") != nullptr && tokenCount >= 10) {
        gpsData.nmeaPacketCount++;
        // 滤波器状态变量：必须在 if/else 外声明，两个分支均可访问
        static float s_win[5] = {0};
        static uint8_t s_winIdx = 0;
        static float s_filteredSpeed = 0.0f;

        if (tokens[2][0] == 'A') {
            gpsData.isFix = true;
            gpsData.lastFixTime = millis();

            float rawKnots = atof(tokens[7]);
            float rawKmH = rawKnots * 1.852f;

            // 死区：静止时低于 2.0 km/h 强制归零
            if (rawKmH < 2.0f) rawKmH = 0.0f;

            gpsData.speedKmH = rawKmH;

            // ── 停车立即归零：flush 整个窗口和 EMA ──────────────────────────
            // 问题根源：若不 flush，窗口里仍有上一秒的历史帧（如 28/30/25），
            // 中位数要等窗口被 0 填满（5帧 = 500ms）才能归零，停车后仍显示有速度。
            // 解决：rawKmH==0 时直接全部清零，无延迟。
            if (rawKmH == 0.0f) {
                memset(s_win, 0, sizeof(s_win));
                s_filteredSpeed = 0.0f;
                carData.speed = 0.0f;
                gpsData.speedKmH = 0.0f;
                // 跳过中位数和 EMA，直接 goto 到航向/坐标解析
                goto rmc_speed_done;
            }

            // ── 5帧滑动窗口中位数滤波 ──────────────────────────────────────
            // 原理：取最近5帧排序后的中位值。对孤立的多径毛刺帧天然免疫：
            // 即使1帧报出离谱的30km/h，中位数仍然稳定在真实值附近。
            s_win[s_winIdx] = rawKmH;
            s_winIdx = (s_winIdx + 1) % 5;

            {
                // 冒泡排序取中位数（5个数，最多10次比较）
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
            // 非有效定位状态 ('V' 或尚未定位空包)
            gpsData.isFix = false;
            gpsData.speedKmH = 0.0f;
            s_filteredSpeed = 0.0f;
            carData.speed = 0.0f;
        }

        // 纯整数时间换算：UTC 转北京时间 (UTC+8)
        // 只要卫星时间戳与年月日字段有效，即使处于未定位搜星阶段 ('V') 亦可完成时钟秒级精准校时
        if (strlen(tokens[1]) >= 6 && strlen(tokens[9]) >= 6) {
            int utcHour = (tokens[1][0] - '0') * 10 + (tokens[1][1] - '0');
            int utcMin  = (tokens[1][2] - '0') * 10 + (tokens[1][3] - '0');
            int utcSec  = (tokens[1][4] - '0') * 10 + (tokens[1][5] - '0');

            int day   = (tokens[9][0] - '0') * 10 + (tokens[9][1] - '0');
            int month = (tokens[9][2] - '0') * 10 + (tokens[9][3] - '0');
            int year  = 2000 + (tokens[9][4] - '0') * 10 + (tokens[9][5] - '0');

            if (day >= 1 && day <= 31 && month >= 1 && month <= 12 && year >= 2024 && year <= 2099) {
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
                checkMonthlyRollover(currentYM);
            }
        }
    }
    // 2. 解析 GGA 语句 (包含参与定位解算卫星颗数、定位质量、海拔高度)
    else if (strstr(tokens[0], "GGA") != nullptr && tokenCount >= 10) {
        gpsData.nmeaPacketCount++;
        gpsData.satellites = atoi(tokens[7]);
        // tokens[6] 为 Fix Quality：0 为未定位，1/2 为有效定位
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

        // 循环提取本包最多 4 颗卫星的载噪比 (SNR: tokens 7, 11, 15, 19)
        static uint32_t s_lastGsvCycle = 0;
        static int s_cycleMaxSnr = 0;
        static int s_cycleTracked = 0;

        // 每隔 1200ms (即一个完整 NMEA 输出周期) 结算上一周期的最高信噪比与有效信号星数
        if (millis() - s_lastGsvCycle >= 1200) {
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
        if (s_cycleMaxSnr > gpsData.maxSnr) {
            gpsData.maxSnr = s_cycleMaxSnr;
        }
        if (s_cycleTracked > gpsData.trackedWithSnr) {
            gpsData.trackedWithSnr = s_cycleTracked;
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
            // 严格过滤：只有在收到 '$' 之后才开始填充缓冲区，防止上电杂波被误统计为校验和错误
            if (nmeaIdx < sizeof(nmeaBuf) - 1) {
                nmeaBuf[nmeaIdx++] = c;
            } else {
                nmeaIdx = 0; // 超长溢出保护，丢弃非法长帧
            }
        }
    }

    if (gpsData.isFix && (millis() - gpsData.lastFixTime > 3000)) {
        gpsData.isFix = false;
        gpsData.speedKmH = 0.0f;
        carData.speed = 0.0f;
    }

    // 若超过 4 秒未收到 GSV 报文，则清空搜星视空与信噪比指标
    if (gpsData.lastGsvTime > 0 && (millis() - gpsData.lastGsvTime > 4000)) {
        gpsData.satellitesInView = 0;
        gpsData.gpsViewCount = 0;
        gpsData.bdsViewCount = 0;
        gpsData.maxSnr = 0;
        gpsData.trackedWithSnr = 0;
    }

    // 航程积分计算 (双精度 double 累加，彻底消除 32 位 float 小数值在数万公里时累加截断与精度损失)
    static double s_tripKmAcc = 0.0;
    static double s_monthKmAcc = -1.0;
    if (s_monthKmAcc < 0.0) {
        s_monthKmAcc = (double)carData.monthKm;
        s_tripKmAcc = (double)carData.tripKm;
    }
    // 当跨月清零或用户手动清零本月大计时，同步重置累加器
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

        // 闪存写入保护：每累计增加 0.2 km 保存一次
        if (carData.monthKm - carData.lastSavedMonthKm >= 0.2f) {
            carData.lastSavedMonthKm = carData.monthKm;
            prefs.putFloat("month_odo", carData.monthKm);
            Serial.printf("[NVS] 本月大计定期持久化: %.2f km\n", carData.monthKm);
        }
    }

    // 停车 3 秒静止保存检测
    static bool wasMoving = false;
    static uint32_t stopStartTime = 0;
    if (carData.speed >= 1.0f) {
        wasMoving = true;
        stopStartTime = 0;
    } else if (wasMoving) {
        if (stopStartTime == 0) stopStartTime = millis();
        else if (millis() - stopStartTime >= 3000) {
            wasMoving = false;
            if (carData.monthKm != carData.lastSavedMonthKm) {
                carData.lastSavedMonthKm = carData.monthKm;
                prefs.putFloat("month_odo", carData.monthKm);
                Serial.printf("[NVS] 停车同步本月大计: %.2f km\n", carData.monthKm);
            }
        }
    }
}

// ================= 动能回收硬拦截与真实充电状态滤波 =================
bool isBmsChargingCurrent() {
    if (!carData.isLiveBms) return false;
    // 双重硬核校验：
    // 1. 保护板协议层面必须明确上报处于充电中 (carData.isBmsCharging == true，即 bmsStatus == 0x02)
    // 2. 物理电流实测必须为充入电流 (负数，如 -0.2A 及以下)
    // 动能回收制动时，保护板上报状态为放电 (0x03) 或待机 (0x01)，绝对不会误判定为充电！
    return (carData.isBmsCharging && carData.currentA < -0.2f);
}

void checkChargingStateTransitions() {
    static uint32_t chargeCandidateStart = 0;
    static uint32_t dischargeCandidateStart = 0;

    // 1. 动能回收与行驶硬拦截：车速 > 0.5 km/h 坚决禁止进入独占充电大屏！
    if (carData.speed > 0.5f) {
        chargeCandidateStart = 0;
        if (currentState == STATE_CHARGING_DASH) {
            currentState = STATE_DASHBOARD_MAIN;
        }
        return;
    }

    // 2. 车辆静止下检测保护板充电标志与充入电流
    if (isBmsChargingCurrent()) {
        dischargeCandidateStart = 0;
        if (chargeCandidateStart == 0) {
            chargeCandidateStart = millis();
        } else if (millis() - chargeCandidateStart >= 2000) {
            // 静止且保护板指示充电持续 2 秒：确认为真实插枪充电！
            if (currentState == STATE_DASHBOARD_MAIN) {
                Serial.println("[CHG] 保护板确认插枪充电！切换至独占全屏充电动画");
                currentState = STATE_CHARGING_DASH;
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
                currentState = STATE_DASHBOARD_MAIN;
            }
        }
    }
}

// ================= 顶部 GPS 信号强度指数图标 (居中精细化排版) =================
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
    int16_t totalW = 14 + 5 + textW + 8; // 信号柱14px + 间隙5px + 文字宽度 + 圆点与边距8px
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

    // 呼吸定位指示灯
    static bool dotToggle = false;
    static uint32_t lastDot = 0;
    if (millis() - lastDot >= 500) {
        lastDot = millis();
        dotToggle = !dotToggle;
    }
    uint16_t dotColor = gpsData.isFix ? COLOR_GREEN_SIGNAL : (dotToggle ? COLOR_YELLOW_SIGNAL : 0x2124);
    canvas.fillCircle(startX + 18 + textW + 5, y + 5, 2, dotColor);
}

// ================= 等离子流光彗星尾迹渲染算法 =================
void drawCometTailArc(int16_t cx, int16_t cy, int32_t rOut, int32_t rIn, float headAngle, float tailSpanDeg, uint16_t baseColor) {
    const int segments = 12; // 12阶高精度等离子渐变切片
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

    // 头部刺目光核聚焦点
    float rad = headAngle * 0.0174533f;
    float midR = (rOut + rIn) / 2.0f;
    int16_t fx = cx + (int16_t)(cosf(rad) * midR);
    int16_t fy = cy + (int16_t)(sinf(rad) * midR);
    canvas.fillCircle(fx, fy, 4, COLOR_WHITE_TEXT);
}

// ================= 主表盘渲染核心 (实体实心能量环 + 严格对称布局 + 丝滑动效 + 扫表自检支持) =================
void drawMainDashboardUI(float overrideSpeed = -1.0f, float overrideSoc = -1.0f, const char* overrideTopText = nullptr, const char* overrideBotText = nullptr, bool isPeakFlash = false) {
    canvas.fillSprite(COLOR_BG);
    canvas.drawCircle(120, 120, 119, 0x18C3);

    const int32_t rOut = 114, rIn = 102; // 12px 饱满实体能量弧宽度

    // -------------------------------------------------------------
    // 1. 上半区：实体速度能量环 (量程 0~100 km/h, 210° ~ 330°)
    // -------------------------------------------------------------
    const float speedStartAngle = 210.0f;
    const float speedTotalSpan  = 120.0f;

    // 实心底槽灰色轨道 (fillArc 实体填充)
    canvas.fillArc(120, 120, rOut, rIn, speedStartAngle, speedStartAngle + speedTotalSpan, COLOR_ARC_TRACK);

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
    if (isOver100 && !isPeakFlash) {
        speedColor = flashToggle ? COLOR_RED_ACCENT : COLOR_WHITE_TEXT;
    }

    if (speedRatio > 0.01f) {
        float activeSpan = speedTotalSpan * speedRatio;
        // 实心速度能量环 (fillArc 实体高亮)
        canvas.fillArc(120, 120, rOut, rIn, speedStartAngle, speedStartAngle + activeSpan, speedColor);

        // 机车/宝马扫表自检时，在指针尖端绘制高光聚焦点与极光针尖
        if (overrideSpeed >= 0.0f && activeSpan > 2.0f) {
            float headRad = (speedStartAngle + activeSpan) * 0.0174533f;
            int16_t fx = 120 + (int16_t)(cosf(headRad) * 108.0f);
            int16_t fy = 120 + (int16_t)(sinf(headRad) * 108.0f);
            canvas.fillCircle(fx, fy, 4, COLOR_WHITE_TEXT);
        }
    }

    // 2. 速度上方行 1：GPS 信号强度指数 (Y=30, 绝对居中)
    drawGpsSignalIcon(120, 30);

    // 3. 速度上方行 2：行驶数据副显 (带平滑垂直浮现动效 或 扫表自检文字)
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
            canvas.setTextColor(COLOR_CYAN_ACCENT);
            char tripBuf[24];
            snprintf(tripBuf, sizeof(tripBuf), "TRIP %.2f km", carData.tripKm);
            canvas.drawString(tripBuf, 120, renderTopSubY);
        } else if (topSubMode == 1) {
            canvas.setFont(&fonts::FreeSansBold9pt7b);
            canvas.setTextColor(COLOR_YELLOW_SIGNAL);
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
            canvas.setTextColor(COLOR_CYAN_ACCENT);
            if (gpsData.isFix) {
                char altBuf[24];
                snprintf(altBuf, sizeof(altBuf), "ALT %.1f m", gpsData.altitudeM);
                canvas.drawString(altBuf, 120, renderTopSubY);
            } else {
                canvas.drawString("ALT --.- m", 120, renderTopSubY);
            }
        }
    }

    // 4. 车速数值与单位 (动态测宽、联合绝对居中，彻底消除偏心不对称)
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
    canvas.setTextColor(isPeakFlash ? COLOR_WHITE_TEXT : COLOR_CYAN_ACCENT);
    canvas.drawString("km/h", spdStartX + spdNumW + spdSpacing, 92);

    // -------------------------------------------------------------
    // 5. 中间激光分割线 (关于 X=120 严格左右对称)
    // -------------------------------------------------------------
    uint16_t dividerDotColor = isPeakFlash ? COLOR_WHITE_TEXT : COLOR_CYAN_ACCENT;
    canvas.drawFastHLine(32, 120, 176, isPeakFlash ? 0x4B3D : 0x2945);
    canvas.fillCircle(32, 120, 2, dividerDotColor);
    canvas.fillCircle(208, 120, 2, dividerDotColor);

    // -------------------------------------------------------------
    // 6. 下半区：实体电量状态环 (量程 0~100%, 30° ~ 150°)
    // -------------------------------------------------------------
    const float battStartAngle = 30.0f;
    const float battTotalSpan  = 120.0f;

    // 实心底槽灰色轨道 (fillArc 实体填充)
    canvas.fillArc(120, 120, rOut, rIn, battStartAngle, battStartAngle + battTotalSpan, COLOR_ARC_TRACK);

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
        if (carData.soc < 12 && flashToggle) {
            battColor = 0x6000;
        }
        hasSocData = true;
    }

    if (effectiveSocRatio > 1.0f) effectiveSocRatio = 1.0f;
    if (effectiveSocRatio < 0.0f) effectiveSocRatio = 0.0f;

    if (effectiveSocRatio > 0.01f) {
        float activeSpan = battTotalSpan * effectiveSocRatio;
        // 实心电量能量环 (fillArc 实体高亮)
        canvas.fillArc(120, 120, rOut, rIn, battStartAngle, battStartAngle + activeSpan, battColor);

        // 扫表自检时，在指针尖端绘制高光聚焦点
        if (overrideSoc >= 0.0f && activeSpan > 2.0f) {
            float bHeadRad = (battStartAngle + activeSpan) * 0.0174533f;
            int16_t bx = 120 + (int16_t)(cosf(bHeadRad) * 108.0f);
            int16_t by = 120 + (int16_t)(sinf(bHeadRad) * 108.0f);
            canvas.fillCircle(bx, by, 3, COLOR_WHITE_TEXT);
        }
    }

    // 7. 下半区核心指标：电池电量大字 (与上方车速尺寸与规制严格镜像对称，联合绝对居中)
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

    canvas.setFont(&fonts::Font6);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(battColor);
    canvas.drawString(socStr, socStartX, 154);

    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextColor(battColor);
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

    // 9. 下半区副显切换 (带平滑垂直浮现动效 或 扫表自检文字)
    if (overrideBotText != nullptr) {
        canvas.setFont(&fonts::FreeSansBold9pt7b);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(isPeakFlash ? COLOR_GREEN_SIGNAL : COLOR_CYAN_ACCENT);
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
            canvas.setTextColor(COLOR_GRAY_TEXT);
            char ahBuf[24];
            if (carData.isLiveBms) {
                snprintf(ahBuf, sizeof(ahBuf), "%.1f / %.1f Ah", carData.currentAh, carData.totalAh);
            } else {
                snprintf(ahBuf, sizeof(ahBuf), "-- / -- Ah");
            }
            canvas.drawString(ahBuf, 120, renderBotSubY);
        } else if (bottomSubMode == 1) {
            canvas.setTextColor(COLOR_YELLOW_SIGNAL);
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

    canvas.pushSprite(0, 0);
}

// ================= 宝马 / 机车顶级开机自检满贯扫表动效 =================
void playDashboardSweepAnimation() {
    const uint32_t SWEEP_DURATION = 1350; // 1.35 秒极致丝滑自检
    uint32_t startT = millis();

    while (millis() - startT < SWEEP_DURATION) {
        uint32_t elapsed = millis() - startT;
        updateGps(); // 保证扫表时不丢失 GPS 串口报文与时间同步

        float curSpeed = 0.0f;
        float curSoc = 0.0f;
        const char* topText = "SYSTEM CHECK";
        const char* botText = "INITIALIZING";
        bool peakFlash = false;

        if (elapsed < 550) {
            // 阶段 1：激情拉转速扫表 0 -> 100 km/h (前 550ms 迅速冲顶)
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
            // 阶段 3：平滑回落至实车实测物理数据 (后 600ms 平滑逼近真实值)
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

// ================= 独占全屏充电动画 (插枪入场仪式 + 等离子彗星流光 + 电流物理联动) =================
void drawChargingUI() {
    canvas.fillSprite(COLOR_BG);
    const int16_t cx = 120, cy = 120;
    const int32_t rOut = 114, rIn = 102; // 实体能量环

    float socRatio = carData.isLiveBms ? (carData.soc / 100.0f) : 0.0f;
    if (socRatio > 1.0f) socRatio = 1.0f;
    if (socRatio < 0.0f) socRatio = 0.0f;
    float fillSpan = 360.0f * socRatio;
    uint16_t socColor = getBatteryArcColor(carData.soc);

    float chgCurrent = abs(carData.currentA);
    float chgPower = carData.voltage * chgCurrent;

    // ---------------------------------------------------------------------
    // 动效 1：插枪入场仪式动效 (前 650ms 极速环形扫光与数字回弹)
    // ---------------------------------------------------------------------
    uint32_t enterElapsed = millis() - chargeEnterTime;
    const uint32_t ENTER_DURATION = 650;

    if (enterElapsed < ENTER_DURATION) {
        float p = (float)enterElapsed / (float)ENTER_DURATION;
        float expandEase = easeOutCubic(p);
        float bounceEase = easeOutBack(p > 1.0f ? 1.0f : p);

        // 实心底槽快速两向展开
        canvas.fillArc(cx, cy, rOut, rIn, 270.0f - 180.0f * expandEase, 270.0f + 180.0f * expandEase, COLOR_ARC_TRACK);

        // 真实电量环从 0° 极速扩张并带光爆
        float currentFillSpan = fillSpan * expandEase;
        if (currentFillSpan > 0.5f) {
            canvas.fillArc(cx, cy, rOut, rIn, 270.0f, 270.0f + currentFillSpan, socColor);
        }

        // 头部扫光光爆彗星
        float sparkHead = 270.0f + currentFillSpan;
        drawCometTailArc(cx, cy, rOut, rIn, sparkHead, 45.0f * (1.0f - p * 0.4f), COLOR_CYAN_ACCENT);

        // 顶部连接提示
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextDatum(top_center);
        canvas.setTextColor(COLOR_GREEN_SIGNAL);
        canvas.drawString("充电准备中", cx, 28);

        // 屏幕中央大字带有弹性浮升
        int16_t bounceY = (int16_t)((1.0f - bounceEase) * 20.0f);
        char socStr[16];
        snprintf(socStr, sizeof(socStr), "%d", carData.soc);
        canvas.setFont(&fonts::Font7);
        int32_t sNumW = canvas.textWidth(socStr);
        canvas.setFont(&fonts::FreeSansBold9pt7b);
        int32_t sUnitW = canvas.textWidth("%");
        int16_t sTotalW = sNumW + 4 + sUnitW;
        int16_t sStartX = cx - sTotalW / 2;

        canvas.setFont(&fonts::Font7);
        canvas.setTextDatum(middle_left);
        canvas.setTextColor(COLOR_WHITE_TEXT);
        canvas.drawString(socStr, sStartX, 85 + bounceY);

        canvas.setFont(&fonts::FreeSansBold9pt7b);
        canvas.setTextColor(COLOR_CYAN_ACCENT);
        canvas.drawString("%", sStartX + sNumW + 4, 96 + bounceY);

        canvas.pushSprite(0, 0);
        return;
    }

    // ---------------------------------------------------------------------
    // 动效 2：常态稳态充电 (实心底槽 + 实心电量 + 等离子彗星物理转速流光)
    // ---------------------------------------------------------------------
    // 1. 实心底槽 360° 完整圆环
    canvas.fillArc(cx, cy, rOut, rIn, 0, 360, COLOR_ARC_TRACK);

    // 2. 实心动态电量进度弧
    if (fillSpan > 0.5f) {
        canvas.fillArc(cx, cy, rOut, rIn, 270.0f, 270.0f + fillSpan, socColor);
    }

    // 3. 真实充电电流物理联动速率的等离子彗星流光 (电流越大转越快)
    static float flowAngle = 0.0f;
    float rotSpeed = 3.2f + chgCurrent * 0.9f;
    if (rotSpeed > 14.0f) rotSpeed = 14.0f;
    flowAngle += rotSpeed;
    if (flowAngle >= 360.0f) flowAngle -= 360.0f;

    drawCometTailArc(cx, cy, rOut, rIn, flowAngle, 48.0f, COLOR_CYAN_ACCENT);

    // 4. 屏幕顶部：呼吸闪烁充电状态
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextDatum(top_center);
    static bool blink = false;
    static uint32_t lastBlink = 0;
    if (millis() - lastBlink >= 400) {
        lastBlink = millis();
        blink = !blink;
    }
    canvas.setTextColor(blink ? COLOR_GREEN_SIGNAL : COLOR_CYAN_ACCENT);
    canvas.drawString("正在充电...", cx, 28);

    // 5. 屏幕中央：大字当前 SOC (%) 联合绝对居中
    char socStr[16];
    snprintf(socStr, sizeof(socStr), "%d", carData.soc);
    canvas.setFont(&fonts::Font7);
    int32_t sNumW = canvas.textWidth(socStr);
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    int32_t sUnitW = canvas.textWidth("%");
    int16_t sTotalW = sNumW + 4 + sUnitW;
    int16_t sStartX = cx - sTotalW / 2;

    canvas.setFont(&fonts::Font7);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(COLOR_WHITE_TEXT);
    canvas.drawString(socStr, sStartX, 85);

    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextColor(COLOR_CYAN_ACCENT);
    canvas.drawString("%", sStartX + sNumW + 4, 96);

    // 6. 当前容量 / 额定总容量 (居中)
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COLOR_WHITE_TEXT);
    char capBuf[32];
    snprintf(capBuf, sizeof(capBuf), "%.1f / %.1f Ah", carData.currentAh, carData.totalAh);
    canvas.drawString(capBuf, cx, 124);

    // 7. 当前充电功率与电流 (居中)
    canvas.setTextColor(COLOR_YELLOW_SIGNAL);
    char pwrBuf[32];
    snprintf(pwrBuf, sizeof(pwrBuf), "+%.0f W (%.1f A)", chgPower, chgCurrent);
    canvas.drawString(pwrBuf, cx, 145);

    // 8. 预计充满剩余时间算法 (居中)
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextColor(COLOR_CYAN_ACCENT);
    if (carData.soc >= 100 || (carData.totalAh > 0 && carData.currentAh >= carData.totalAh)) {
        canvas.setTextColor(COLOR_GREEN_SIGNAL);
        canvas.drawString("已充满 (浮充中)", cx, 166);
    } else if (chgCurrent < 0.25f || carData.totalAh <= 0.0f) {
        canvas.setTextColor(COLOR_GRAY_TEXT);
        canvas.drawString("计算剩余时间中...", cx, 166);
    } else {
        float remAh = 0.0f;
        if (carData.totalAh > 0 && carData.currentAh > 0 && carData.totalAh > carData.currentAh) {
            remAh = carData.totalAh - carData.currentAh;
        } else {
            remAh = carData.totalAh * (1.0f - (carData.soc / 100.0f));
        }
        float hours = remAh / (chgCurrent * 0.92f);
        int totalMins = (int)(hours * 60.0f);
        if (totalMins < 1) totalMins = 1;

        int h = totalMins / 60;
        int m = totalMins % 60;
        char etaBuf[32];
        if (h > 0) {
            snprintf(etaBuf, sizeof(etaBuf), "预计 %d小时%d分 充满", h, m);
        } else {
            snprintf(etaBuf, sizeof(etaBuf), "预计 %d分钟 充满", m);
        }
        canvas.drawString(etaBuf, cx, 166);
    }

    // 9. 充电界面：单体压差与电池温度
    canvas.setFont(&fonts::efontCN_16);
    char chgSubBuf1[32];
    if (carData.isLiveBms) {
        snprintf(chgSubBuf1, sizeof(chgSubBuf1), "压差: %dmV   温度: %d°C", carData.deltaV_mV, carData.tempC);
        canvas.setTextColor(getDeltaVColor(carData.deltaV_mV));
    } else {
        snprintf(chgSubBuf1, sizeof(chgSubBuf1), "压差: --mV   温度: --°C");
        canvas.setTextColor(COLOR_GRAY_TEXT);
    }
    canvas.drawString(chgSubBuf1, cx, 187);

    // 10. 充电界面：电池健康状态与端压 (支持多重 Buff 叠加与边界保护)
    char healthBuf[32];
    uint16_t healthColor = COLOR_GRAY_TEXT;
    getBatteryHealthInfo(healthBuf, sizeof(healthBuf), healthColor, true);
    char chgSubBuf2[32];
    if (carData.isLiveBms) {
        snprintf(chgSubBuf2, sizeof(chgSubBuf2), "%s   端压: %.1fV", healthBuf, carData.voltage);
        canvas.setTextColor(healthColor);
    } else {
        snprintf(chgSubBuf2, sizeof(chgSubBuf2), "--   端压: --.-V");
        canvas.setTextColor(COLOR_GRAY_TEXT);
    }
    if (canvas.textWidth(chgSubBuf2) > 165) {
        canvas.setFont(&fonts::efontCN_14);
    }
    canvas.drawString(chgSubBuf2, cx, 206);
    canvas.setFont(&fonts::efontCN_16);

    canvas.pushSprite(0, 0);
}

// ================= 功能菜单 UI (卡片入场交错滑入 + 光标阻尼丝滑平移) =================
void drawMenuUI() {
    canvas.fillSprite(COLOR_BG);
    canvas.drawCircle(120, 120, 119, 0x18C3);

    // 标题
    canvas.setFont(&fonts::efontCN_24);
    canvas.setTextDatum(top_center);
    canvas.setTextColor(COLOR_CYAN_ACCENT);
    canvas.drawString("功能设置", 120, 12);
    canvas.drawFastHLine(50, 40, 140, 0x2945);

    const int16_t startY = 46, cardH = 28, gap = 4, cardW = 184;
    const int16_t cardX = 120 - cardW / 2;

    // 动效 3：菜单入场交错滑入 (前 320ms)
    uint32_t menuElapsed = millis() - menuEnterTime;
    const uint32_t MENU_ANIM_DUR = 320;
    float menuProgress = (menuElapsed < MENU_ANIM_DUR) ? ((float)menuElapsed / (float)MENU_ANIM_DUR) : 1.0f;

    // 动效 4：光标阻尼平滑逼近 (物理弹簧插值 Lerp)
    static float cursorCurrentY = 46.0f;
    float cursorTargetY = startY + menuCursor * (cardH + gap);
    if (menuElapsed < 30 && menuCursor == 0) {
        cursorCurrentY = cursorTargetY;
    } else {
        cursorCurrentY += (cursorTargetY - cursorCurrentY) * 0.35f;
    }
    int16_t renderCursorY = (int16_t)(cursorCurrentY + 0.5f);

    // 先画所有普通底卡片 (带交错横向滑入)
    for (int i = 0; i < MENU_TOTAL_ITEMS; i++) {
        int16_t y = startY + i * (cardH + gap);
        float itemDelay = i * 0.08f;
        float itemP = (menuProgress - itemDelay) / (1.0f - itemDelay);
        if (itemP < 0.0f) itemP = 0.0f;
        if (itemP > 1.0f) itemP = 1.0f;
        float ease = easeOutCubic(itemP);
        int16_t slideX = cardX + (int16_t)((1.0f - ease) * (i % 2 == 0 ? -30.0f : 30.0f));

        canvas.fillRoundRect(slideX, y, cardW, cardH, 5, COLOR_CARD_NORMAL);
    }

    // 绘制高亮浮动光标 (物理丝滑跟随)
    if (menuProgress >= 0.5f) {
        canvas.fillRoundRect(cardX, renderCursorY, cardW, cardH, 5, COLOR_CARD_SELECT);
        canvas.drawRoundRect(cardX, renderCursorY, cardW, cardH, 5, COLOR_CYAN_ACCENT);
        canvas.fillRoundRect(cardX + 2, renderCursorY + 4, 3, cardH - 8, 2, COLOR_CYAN_ACCENT);
    }

    // 绘制卡片文字
    for (int i = 0; i < MENU_TOTAL_ITEMS; i++) {
        int16_t y = startY + i * (cardH + gap);
        float itemDelay = i * 0.08f;
        float itemP = (menuProgress - itemDelay) / (1.0f - itemDelay);
        if (itemP < 0.0f) itemP = 0.0f;
        if (itemP > 1.0f) itemP = 1.0f;
        float ease = easeOutCubic(itemP);
        int16_t slideX = cardX + (int16_t)((1.0f - ease) * (i % 2 == 0 ? -30.0f : 30.0f));

        bool isSelected = (i == menuCursor);
        canvas.setFont(&fonts::efontCN_16);
        canvas.setTextDatum(middle_left);
        canvas.setTextColor(isSelected ? COLOR_WHITE_TEXT : COLOR_GRAY_TEXT);
        canvas.drawString(MENU_ITEMS[i], slideX + 10, y + cardH / 2);
    }

    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextDatum(top_center);
    canvas.setTextColor(0x52AA);
    canvas.drawString("K1:^  K4:v  K2:OK  K3:Back", 120, 212);
    canvas.pushSprite(0, 0);
}

// ================= 清除本月大计确认面板 =================
void drawClearMonthUI() {
    canvas.fillSprite(COLOR_BG);
    canvas.drawCircle(120, 120, 119, 0x18C3);

    canvas.setFont(&fonts::efontCN_24);
    canvas.setTextDatum(top_center);
    canvas.setTextColor(COLOR_RED_ACCENT);
    canvas.drawString("清除本月大计", 120, 16);
    canvas.drawFastHLine(40, 46, 160, 0x2945);

    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COLOR_WHITE_TEXT);
    char buf[32];
    snprintf(buf, sizeof(buf), "本月累计: %.2f km", carData.monthKm);
    canvas.drawString(buf, 120, 80);

    canvas.setTextColor(COLOR_YELLOW_SIGNAL);
    canvas.drawString("确认将本月里程清零？", 120, 108);

    // 选项按钮
    int16_t btnW = 76, btnH = 30;
    int16_t b1X = 120 - btnW - 8, b2X = 120 + 8, btnY = 142;

    // 取消按钮
    if (clearConfirmCursor == 0) {
        canvas.fillRoundRect(b1X, btnY, btnW, btnH, 5, COLOR_CARD_SELECT);
        canvas.drawRoundRect(b1X, btnY, btnW, btnH, 5, COLOR_CYAN_ACCENT);
        canvas.setTextColor(COLOR_WHITE_TEXT);
    } else {
        canvas.fillRoundRect(b1X, btnY, btnW, btnH, 5, COLOR_CARD_NORMAL);
        canvas.setTextColor(COLOR_GRAY_TEXT);
    }
    canvas.drawString("取消", b1X + btnW / 2, btnY + btnH / 2);

    // 确认清零按钮
    if (clearConfirmCursor == 1) {
        canvas.fillRoundRect(b2X, btnY, btnW, btnH, 5, 0x8000);
        canvas.drawRoundRect(b2X, btnY, btnW, btnH, 5, COLOR_RED_ACCENT);
        canvas.setTextColor(COLOR_WHITE_TEXT);
    } else {
        canvas.fillRoundRect(b2X, btnY, btnW, btnH, 5, COLOR_CARD_NORMAL);
        canvas.setTextColor(COLOR_GRAY_TEXT);
    }
    canvas.drawString("确认", b2X + btnW / 2, btnY + btnH / 2);

    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextColor(0x52AA);
    canvas.drawString("K1/K4:Select  K2:OK  K3:Back", 120, 212);
    canvas.pushSprite(0, 0);
}

// ================= 电池参数详情面板 =================
void drawBatteryPanelUI() {
    canvas.fillSprite(COLOR_BG);
    canvas.drawCircle(120, 120, 119, 0x18C3);

    canvas.setFont(&fonts::efontCN_24);
    canvas.setTextDatum(top_center);
    canvas.setTextColor(COLOR_CYAN_ACCENT);
    canvas.drawString("电池遥测监控", 120, 10);
    canvas.drawFastHLine(45, 34, 150, 0x2945);

    const int16_t startY = 38, rowH = 16, leftX = 26, valX = leftX + 80;
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextDatum(middle_left);

    // 1. 连接状态
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.drawString("连接状态:", leftX, startY);
    canvas.setTextColor(carData.isLiveBms ? (carData.isBmsCharging ? COLOR_CYAN_ACCENT : COLOR_GREEN_SIGNAL) : COLOR_RED_ACCENT);
    canvas.drawString(carData.isLiveBms ? (carData.isBmsCharging ? "充电中 (Charging)" : (carData.bmsStatus == 3 ? "放电中 (Discharge)" : "在线 (Standby)")) : "离线 (Offline)", valX, startY);

    // 2. 电池总压
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.drawString("电池总压:", leftX, startY + rowH);
    canvas.setTextColor(COLOR_WHITE_TEXT);
    char bufV[24];
    if (carData.isLiveBms) snprintf(bufV, sizeof(bufV), "%.2f V", carData.voltage);
    else snprintf(bufV, sizeof(bufV), "--.- V");
    canvas.drawString(bufV, valX, startY + rowH);

    // 3. 实时电流
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.drawString("实时电流:", leftX, startY + rowH * 2);
    canvas.setTextColor(carData.currentA < -0.2f ? COLOR_GREEN_SIGNAL : COLOR_YELLOW_SIGNAL);
    char bufI[24];
    if (carData.isLiveBms) snprintf(bufI, sizeof(bufI), "%.2f A", carData.currentA);
    else snprintf(bufI, sizeof(bufI), "--.- A");
    canvas.drawString(bufI, valX, startY + rowH * 2);

    // 4. 单体压差
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.drawString("单体压差:", leftX, startY + rowH * 3);
    char bufDV[24];
    if (carData.isLiveBms) {
        snprintf(bufDV, sizeof(bufDV), "%d mV", carData.deltaV_mV);
        canvas.setTextColor(getDeltaVColor(carData.deltaV_mV));
    } else {
        snprintf(bufDV, sizeof(bufDV), "-- mV");
        canvas.setTextColor(COLOR_GRAY_TEXT);
    }
    canvas.drawString(bufDV, valX, startY + rowH * 3);

    // 5. 电池温度 (严禁写出括号或mos字样)
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.drawString("电池温度:", leftX, startY + rowH * 4);
    char bufT[24];
    if (carData.isLiveBms) {
        snprintf(bufT, sizeof(bufT), "%d °C", carData.tempC);
        canvas.setTextColor(getBatteryTempColor(carData.tempC));
    } else {
        snprintf(bufT, sizeof(bufT), "-- °C");
        canvas.setTextColor(COLOR_GRAY_TEXT);
    }
    canvas.drawString(bufT, valX, startY + rowH * 4);

    // 6. 电池健康度 (动态多重 Buff 综合估算，支持多重叠加)
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.drawString("电池健康:", leftX, startY + rowH * 5);
    char bufH[36];
    uint16_t healthColor = COLOR_GRAY_TEXT;
    getBatteryHealthInfo(bufH, sizeof(bufH), healthColor, false);
    canvas.setTextColor(healthColor);
    if (canvas.textWidth(bufH) > 118) {
        canvas.setFont(&fonts::efontCN_14);
    }
    canvas.drawString(bufH, valX, startY + rowH * 5);
    canvas.setFont(&fonts::efontCN_16);

    // 7. 瞬时功率
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.drawString("瞬时功率:", leftX, startY + rowH * 6);
    canvas.setTextColor(COLOR_WHITE_TEXT);
    char bufP[24];
    if (carData.isLiveBms) snprintf(bufP, sizeof(bufP), "%.1f W", carData.powerW);
    else snprintf(bufP, sizeof(bufP), "-- W");
    canvas.drawString(bufP, valX, startY + rowH * 6);

    // 8. 剩余 SOC
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.drawString("剩余 SOC:", leftX, startY + rowH * 7);
    canvas.setTextColor(carData.isLiveBms ? getBatteryArcColor(carData.soc) : COLOR_GRAY_TEXT);
    char bufS[24];
    if (carData.isLiveBms) snprintf(bufS, sizeof(bufS), "%d %%", carData.soc);
    else snprintf(bufS, sizeof(bufS), "-- %%");
    canvas.drawString(bufS, valX, startY + rowH * 7);

    // 9. 容量明细
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.drawString("容量明细:", leftX, startY + rowH * 8);
    canvas.setTextColor(COLOR_WHITE_TEXT);
    char bufC[32];
    if (carData.isLiveBms) snprintf(bufC, sizeof(bufC), "%.1f / %.1f Ah", carData.currentAh, carData.totalAh);
    else snprintf(bufC, sizeof(bufC), "-- / -- Ah");
    canvas.drawString(bufC, valX, startY + rowH * 8);

    // 10. 有效报文
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.drawString("有效报文:", leftX, startY + rowH * 9);
    canvas.setTextColor(COLOR_CYAN_ACCENT);
    char bufCnt[24];
    snprintf(bufCnt, sizeof(bufCnt), "%u 帧", carData.bmsPacketCount);
    canvas.drawString(bufCnt, valX, startY + rowH * 9);

    // 11. 保护板 MAC
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextDatum(top_center);
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.drawString(pairedBmsMac.isEmpty() ? "未绑定 MAC" : pairedBmsMac.c_str(), 120, 198);

    canvas.setTextColor(0x52AA);
    canvas.drawString("K3: Return", 120, 214);
    canvas.pushSprite(0, 0);
}

// ================= GPS 参数详情面板 =================
void drawGpsPanelUI() {
    canvas.fillSprite(COLOR_BG);
    canvas.drawCircle(120, 120, 119, 0x18C3);

    canvas.setFont(&fonts::efontCN_24);
    canvas.setTextDatum(top_center);
    canvas.setTextColor(COLOR_CYAN_ACCENT);
    canvas.drawString("GPS 卫星遥测", 120, 10);
    canvas.drawFastHLine(45, 34, 150, 0x2945);

    const int16_t startY = 38, rowH = 16, leftX = 35;
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextDatum(middle_left);

    // 1. 定位状态
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.drawString("定位状态:", leftX, startY);
    canvas.setTextColor(gpsData.isFix ? COLOR_GREEN_SIGNAL : COLOR_YELLOW_SIGNAL);
    canvas.drawString(gpsData.isFix ? "有效 3D 定位" : "搜星中 (No Fix)", leftX + 70, startY);

    // 2. 跟踪卫星 (区分参与定位解算星数与视空搜星总数)
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.drawString("跟踪卫星:", leftX, startY + rowH);
    char bufSat[32];
    if (gpsData.isFix) {
        snprintf(bufSat, sizeof(bufSat), "%d 颗 (视空 %d)", gpsData.satellites, gpsData.satellitesInView);
        canvas.setTextColor(COLOR_GREEN_SIGNAL);
    } else {
        snprintf(bufSat, sizeof(bufSat), "0 颗 (视空 %d)", gpsData.satellitesInView);
        canvas.setTextColor(COLOR_YELLOW_SIGNAL);
    }
    canvas.drawString(bufSat, leftX + 70, startY + rowH);

    // 3. 信号强度 (载噪比 C/N0 与信号质量评估)
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.drawString("信号强度:", leftX, startY + rowH * 2);
    char bufSnr[32];
    uint16_t snrColor;
    const char* snrDesc;
    if (gpsData.maxSnr >= 35) {
        snrColor = COLOR_GREEN_SIGNAL;
        snrDesc = "极佳";
    } else if (gpsData.maxSnr >= 26) {
        snrColor = COLOR_CYAN_ACCENT;
        snrDesc = "良好";
    } else if (gpsData.maxSnr >= 15) {
        snrColor = COLOR_YELLOW_SIGNAL;
        snrDesc = "微弱";
    } else {
        snrColor = COLOR_RED_ACCENT;
        snrDesc = "无信号";
    }
    snprintf(bufSnr, sizeof(bufSnr), "%d dB (%s)", gpsData.maxSnr, snrDesc);
    canvas.setTextColor(snrColor);
    canvas.drawString(bufSnr, leftX + 70, startY + rowH * 2);

    // 4. 地面航速
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.drawString("地面航速:", leftX, startY + rowH * 3);
    canvas.setTextColor(COLOR_CYAN_ACCENT);
    char bufSpd[24];
    snprintf(bufSpd, sizeof(bufSpd), "%.1f km/h", gpsData.speedKmH);
    canvas.drawString(bufSpd, leftX + 70, startY + rowH * 3);

    // 5. 航向方位
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.drawString("航向角度:", leftX, startY + rowH * 4);
    canvas.setTextColor(COLOR_WHITE_TEXT);
    char bufCrs[24];
    snprintf(bufCrs, sizeof(bufCrs), "%.1f °", gpsData.courseDeg);
    canvas.drawString(bufCrs, leftX + 70, startY + rowH * 4);

    // 6. 海拔高度
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.drawString("海拔高度:", leftX, startY + rowH * 5);
    canvas.setTextColor(COLOR_WHITE_TEXT);
    char bufAlt[24];
    snprintf(bufAlt, sizeof(bufAlt), "%.1f m", gpsData.altitudeM);
    canvas.drawString(bufAlt, leftX + 70, startY + rowH * 5);

    // 7. 经纬度坐标
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COLOR_WHITE_TEXT);
    char coordBuf[36];
    snprintf(coordBuf, sizeof(coordBuf), "%s  %s", gpsData.latStr, gpsData.lonStr);
    canvas.drawString(coordBuf, 120, 140);

    // 8. 卫星授时北京时间
    canvas.setTextColor(COLOR_CYAN_ACCENT);
    if (gpsData.timeValid) {
        canvas.setFont(&fonts::FreeSans9pt7b);
        char timeFullBuf[32];
        snprintf(timeFullBuf, sizeof(timeFullBuf), "%04d-%02d-%02d %02d:%02d:%02d",
                 gpsData.year, gpsData.month, gpsData.day, gpsData.hour, gpsData.minute, gpsData.second);
        canvas.drawString(timeFullBuf, 120, 160);
    } else {
        canvas.setFont(&fonts::efontCN_14);
        canvas.drawString("卫星时间同步中...", 120, 160);
    }

    // 9. 报文统计 (包含中文字符，使用 efontCN_14)
    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextColor(COLOR_GRAY_TEXT);
    char nmeBuf[40];
    snprintf(nmeBuf, sizeof(nmeBuf), "NMEA: %u 帧 (校验误:%u)", gpsData.nmeaPacketCount, gpsData.nmeaErrorCount);
    canvas.drawString(nmeBuf, 120, 180);

    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextColor(0x52AA);
    canvas.drawString("K3: Return", 120, 212);
    canvas.pushSprite(0, 0);
}

// ================= 关于系统面板 (系统Logo + 凛OS car + 真实参数) =================
void drawAboutPanelUI() {
    canvas.fillSprite(COLOR_BG);
    canvas.drawCircle(120, 120, 119, 0x18C3);

    // 1. 顶部绘制系统 Logo (开机同款中心圆+8环绕圆)
    const int16_t logoCx = 120, logoCy = 32;
    canvas.fillCircle(logoCx, logoCy, 7, COLOR_BLUE_ACCENT);
    for (int i = 0; i < 8; i++) {
        float angle = i * (3.14159265f / 4.0f);
        int16_t dx = logoCx + (int16_t)(cosf(angle) * 16.0f);
        int16_t dy = logoCy + (int16_t)(sinf(angle) * 16.0f);
        canvas.fillCircle(dx, dy, 3, COLOR_BLUE_ACCENT);
    }

    // 2. 居中绘制系统名称 (car字与开机同款红色 COLOR_RED_ACCENT)
    canvas.setFont(&fonts::efontCN_24);
    int32_t w1 = canvas.textWidth("凛OS ");
    int32_t w2 = canvas.textWidth("car");
    int16_t startX = 120 - ((w1 + w2) / 2);
    int16_t textY = 62;

    canvas.setTextDatum(middle_left);
    canvas.setTextColor(COLOR_WHITE_TEXT);
    canvas.drawString("凛OS ", startX, textY);
    canvas.setTextColor(COLOR_RED_ACCENT);
    canvas.drawString("car", startX + w1, textY);

    canvas.drawFastHLine(45, 78, 150, 0x2945);

    // 3. 真实硬件与系统参数展示
    const int16_t startY = 92, rowH = 19, leftX = 35;
    canvas.setFont(&fonts::efontCN_16);
    canvas.setTextDatum(middle_left);

    // 固件版本
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.drawString("固件版本:", leftX, startY);
    canvas.setTextColor(COLOR_CYAN_ACCENT);
    canvas.drawString("v2.4.0-GPS", leftX + 70, startY);

    // 芯片平台与主频
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.drawString("主控芯片:", leftX, startY + rowH);
    canvas.setTextColor(COLOR_WHITE_TEXT);
    char cpuBuf[24];
    snprintf(cpuBuf, sizeof(cpuBuf), "ESP32-S3 (%dM)", ESP.getCpuFreqMHz());
    canvas.drawString(cpuBuf, leftX + 70, startY + rowH);

    // 动态空闲堆内存 (Free Heap)
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.drawString("剩余内存:", leftX, startY + rowH * 2);
    canvas.setTextColor(COLOR_GREEN_SIGNAL);
    char heapBuf[24];
    snprintf(heapBuf, sizeof(heapBuf), "%u KB", ESP.getFreeHeap() / 1024);
    canvas.drawString(heapBuf, leftX + 70, startY + rowH * 2);

    // 真实开机运行时间 (Uptime)
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.drawString("运行时间:", leftX, startY + rowH * 3);
    canvas.setTextColor(COLOR_WHITE_TEXT);
    uint32_t sec = millis() / 1000;
    char upBuf[24];
    snprintf(upBuf, sizeof(upBuf), "%02u:%02u:%02u", sec / 3600, (sec % 3600) / 60, sec % 60);
    canvas.drawString(upBuf, leftX + 70, startY + rowH * 3);

    // GPS 串口配置
    canvas.setTextColor(COLOR_GRAY_TEXT);
    canvas.drawString("GPS 接口:", leftX, startY + rowH * 4);
    canvas.setTextColor(COLOR_CYAN_ACCENT);
    canvas.drawString("115200@10Hz", leftX + 70, startY + rowH * 4);

    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextDatum(top_center);
    canvas.setTextColor(0x52AA);
    canvas.drawString("K3: Return", 120, 212);
    canvas.pushSprite(0, 0);
}

// ================= Setup & Loop 主流程 =================
void setup() {
    Serial.begin(115200);

    // 1. 显式配置模拟引脚为高阻态输入 (消灭 Bootloader 内部上下拉电阻导致的 ADC 键值漂移)
    pinMode(PIN_KEY_ADC, INPUT);
    pinMode(PIN_POWER_ADC, INPUT);
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);

    // 2. GPS 硬件串口初始化、自动协商至 115200 bps 并激活 10Hz (100ms) 极速刷新
    initGpsFast();
    Serial.println("[SYSTEM] 凛OS 车机系统启动");

    // 3. 屏幕驱动与 112.5KB 全屏显存完整性断言
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
        Serial.println("[NVS] 警告: NVS 分区打开异常！");
    }
    carData.monthKm = prefs.getFloat("month_odo", 0.0f);
    carData.lastSavedMonthKm = carData.monthKm;
    Serial.printf("[NVS] 读取到历史本月大计: %.2f km\n", carData.monthKm);

    // 5. 蓝牙协议栈与全模式满血射频增益 (+9dBm)
    NimBLEDevice::init("RinOS-Car-Dash");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_DEFAULT);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_ADV);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_SCAN);

    // 6. 开机动画
    playBootAnimation();

    // 7. 检查保护板配对记忆与自愈回连
    String savedBms = prefs.getString("bms_mac", "");
    if (!savedBms.isEmpty()) {
        pairedBmsName = prefs.getString("bms_name", "ANT-BMS");
        pairedBmsMac  = savedBms;

        Serial.printf("[BLE] 发现记忆保护板: %s [%s]，尝试回连...\n", pairedBmsName.c_str(), pairedBmsMac.c_str());
        BleDeviceItem bmsDev;
        bmsDev.name = pairedBmsName.c_str();
        bmsDev.address = NimBLEAddress(pairedBmsMac.c_str());
        bmsDev.isTarget = true;
        bmsDev.rssi = -60;
        if (connectToBms(bmsDev)) {
            currentState = STATE_DASHBOARD_MAIN;
            playDashboardSweepAnimation();
        } else {
            currentState = STATE_WIZARD_BMS;
            drawDeviceListUI("连接保护板");
            startBleScan();
        }
    } else {
        currentState = STATE_WIZARD_BMS;
        drawDeviceListUI("连接保护板");
        startBleScan();
    }
}

void loop() {
    // 1. 实时读取物理按键
    KeyEvent key = scanKeypad();

    // 2. 实时非阻塞解析 GPS NMEA 报文与航程积分
    updateGps();

    // 3. 实时检查动能回收过滤与真实插枪充电状态转移
    checkChargingStateTransitions();

    // K2 长按 1.8 秒：在任意界面强制重新配对保护板
    if (key == KEY_EVENT_K2_LONG) {
        Serial.println("[SYSTEM] K2 长按触发：清除保护板记忆，重新扫描配对！");
        prefs.remove("bms_mac");
        prefs.remove("bms_name");
        pairedBmsMac = "";
        pairedBmsName = "";
        if (pBmsClient && pBmsClient->isConnected()) {
            pBmsClient->disconnect();
        }
        currentState = STATE_WIZARD_BMS;
        drawDeviceListUI("连接保护板");
        startBleScan();
        return;
    }

    switch (currentState) {
        // ================= 步骤 1：连接保护板向导 =================
        case STATE_WIZARD_BMS: {
            if (key == KEY_EVENT_K1) {
                portENTER_CRITICAL(&bleScanMux);
                int count = scannedDevices.size();
                if (count > 0) {
                    selectedIndex = (selectedIndex - 1 + count) % count;
                }
                portEXIT_CRITICAL(&bleScanMux);
            } else if (key == KEY_EVENT_K4) {
                portENTER_CRITICAL(&bleScanMux);
                int count = scannedDevices.size();
                if (count > 0) {
                    selectedIndex = (selectedIndex + 1) % count;
                }
                portEXIT_CRITICAL(&bleScanMux);
            } else if (key == KEY_EVENT_K2) {
                BleDeviceItem targetItem;
                bool hasTarget = false;
                portENTER_CRITICAL(&bleScanMux);
                if (!scannedDevices.empty() && selectedIndex < (int)scannedDevices.size()) {
                    targetItem = scannedDevices[selectedIndex];
                    hasTarget = true;
                }
                portEXIT_CRITICAL(&bleScanMux);

                if (hasTarget) {
                    if (NimBLEDevice::getScan()->isScanning()) {
                        NimBLEDevice::getScan()->stop();
                    }
                    currentState = STATE_CONNECTING_BMS;
                    if (connectToBms(targetItem)) {
                        currentState = STATE_DASHBOARD_MAIN;
                        playDashboardSweepAnimation();
                    } else {
                        currentState = STATE_WIZARD_BMS;
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
                    currentState = STATE_DASHBOARD_MAIN;
                }
            }

            if (currentState == STATE_WIZARD_BMS) {
                drawDeviceListUI("连接保护板");
            }
            break;
        }

        // ================= 步骤 2：主仪表盘 =================
        case STATE_DASHBOARD_MAIN: {
            // K1: 切换下半区模式 (0=容量Ah, 1=功率电流, 2=压差温度健康, 3=总压状态)
            if (key == KEY_EVENT_K1) {
                bottomSubMode = (bottomSubMode + 1) % 4;
                bottomSubAnimStart = millis();
            }
            // K4: 切换上半区模式 (0=小计TRIP, 1=大计MONTH, 2=卫星时钟, 3=海拔高度)
            else if (key == KEY_EVENT_K4) {
                topSubMode = (topSubMode + 1) % 4;
                topSubAnimStart = millis();
            }
            // K2: 桌面点击唤起功能菜单！
            else if (key == KEY_EVENT_K2) {
                currentState = STATE_MENU_MAIN;
                menuCursor = 0;
                menuEnterTime = millis();
                drawMenuUI();
                break;
            }

            // 保护板主动定时轮询 (每 1000ms 一次，兼顾新旧双协议)
            if (pBmsClient && pBmsClient->isConnected()) {
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
                // 掉线自动回连守护 (每 4 秒)
                static uint32_t lastBmsReconnect = 0;
                if (millis() - lastBmsReconnect >= 4000) {
                    lastBmsReconnect = millis();
                    BleDeviceItem bmsDev;
                    bmsDev.name = pairedBmsName.c_str();
                    bmsDev.address = NimBLEAddress(pairedBmsMac.c_str());
                    connectToBms(bmsDev);
                }
            }

            drawMainDashboardUI();
            break;
        }

        // ================= 步骤 2.2：独占全屏充电动画 =================
        case STATE_CHARGING_DASH: {
            // 在充电大屏按 K3 可临时返回桌面
            if (key == KEY_EVENT_K3) {
                currentState = STATE_DASHBOARD_MAIN;
            }
            drawChargingUI();
            break;
        }

        // ================= 步骤 3：功能菜单 =================
        case STATE_MENU_MAIN: {
            if (key == KEY_EVENT_K1) {
                menuCursor = (menuCursor - 1 + MENU_TOTAL_ITEMS) % MENU_TOTAL_ITEMS;
            } else if (key == KEY_EVENT_K4) {
                menuCursor = (menuCursor + 1) % MENU_TOTAL_ITEMS;
            } else if (key == KEY_EVENT_K2) {
                // 确认进入各真实子面板
                switch (menuCursor) {
                    case 0: // 清除本月大计
                        clearConfirmCursor = 0;
                        currentState = STATE_PANEL_CLEAR_MONTH;
                        break;
                    case 1: // 重新连接保护板
                        prefs.remove("bms_mac");
                        prefs.remove("bms_name");
                        pairedBmsMac = "";
                        pairedBmsName = "";
                        if (pBmsClient && pBmsClient->isConnected()) {
                            pBmsClient->disconnect();
                        }
                        currentState = STATE_WIZARD_BMS;
                        startBleScan();
                        break;
                    case 2: // 电池面板
                        currentState = STATE_PANEL_BATTERY;
                        break;
                    case 3: // GPS 参数面板
                        currentState = STATE_PANEL_GPS;
                        break;
                    case 4: // 关于系统
                        currentState = STATE_PANEL_ABOUT;
                        break;
                }
            } else if (key == KEY_EVENT_K3) {
                // K3 全局返回主桌面
                currentState = STATE_DASHBOARD_MAIN;
            }

            if (currentState == STATE_MENU_MAIN) {
                drawMenuUI();
            }
            break;
        }

        // ================= 步骤 4.1：清除本月大计确认面板 =================
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
                    currentState = STATE_DASHBOARD_MAIN;
                } else {
                    // 取消
                    currentState = STATE_MENU_MAIN;
                    menuEnterTime = millis();
                }
            } else if (key == KEY_EVENT_K3) {
                // K3 返回菜单
                currentState = STATE_MENU_MAIN;
                menuEnterTime = millis();
            }

            if (currentState == STATE_PANEL_CLEAR_MONTH) {
                drawClearMonthUI();
            }
            break;
        }

        // ================= 步骤 4.2：电池参数面板 =================
        case STATE_PANEL_BATTERY: {
            if (key == KEY_EVENT_K3) {
                currentState = STATE_MENU_MAIN;
                menuEnterTime = millis();
            }
            if (currentState == STATE_PANEL_BATTERY) {
                drawBatteryPanelUI();
            }
            break;
        }

        // ================= 步骤 4.3：GPS 参数面板 =================
        case STATE_PANEL_GPS: {
            if (key == KEY_EVENT_K3) {
                currentState = STATE_MENU_MAIN;
                menuEnterTime = millis();
            }
            if (currentState == STATE_PANEL_GPS) {
                drawGpsPanelUI();
            }
            break;
        }

        // ================= 步骤 4.4：关于系统面板 =================
        case STATE_PANEL_ABOUT: {
            if (key == KEY_EVENT_K3) {
                currentState = STATE_MENU_MAIN;
                menuEnterTime = millis();
            }
            if (currentState == STATE_PANEL_ABOUT) {
                drawAboutPanelUI();
            }
            break;
        }

        default:
            break;
    }

    // 50 FPS 硬件平滑锁频 (20ms/帧)
    static uint32_t lastFrameTick = 0;
    while (millis() - lastFrameTick < 20) {
        delay(1);
    }
    lastFrameTick = millis();
}
