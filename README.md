# RinOS-Car：ESP32-S3 智能圆形车载液晶仪表盘

> 当前固件版本：**v4.7**
> 实车状态：**2026-11-08 已上车验证基础功能可用**
> 目前只支持**蚂蚁保护板**的 BLE 遥测协议。

<div align="center">

![Platform](https://img.shields.io/badge/Platform-ESP32--S3-blue.svg)
![Framework](https://img.shields.io/badge/Framework-Arduino%20%2F%20PlatformIO-orange.svg)
![Screen](https://img.shields.io/badge/Screen-GC9A01%20240x240%20IPS-green.svg)
![Version](https://img.shields.io/badge/Version-v4.7-cyan.svg)
![License](https://img.shields.io/badge/License-MIT-purple.svg)

**专为电摩打造的开源智能圆形彩色液晶仪表系统**
高精度 GPS/北斗测速 · 蓝牙 BMS 电池遥测 · 情境化灵动数据

**作者主页 / 博客**：[https://miqwq.com](https://miqwq.com)

</div>

---

## ✨ 功能概览

### 仪表与交互

- 1.28 寸 GC9A01 240×240 圆形 IPS 屏，LovyanGFX 全屏 Sprite 渲染。
- 4 键模拟 ADC 按键：K1/K4 切换上下副显，K2 确认/菜单，K3 返回。
- 主表盘：速度弧、电量弧、速度/电量大字、GPS 信号、上下情境副显。
- 功能菜单：全屏圆屏应用列表，支持上下滚动、进入/退出动画。
- 功能页：电池遥测 3 页、GPS 遥测 3 页、充电大屏、蓝牙释放、清除本月、灵动数据开关、关于。
- 充电页以 SOC 为主视觉，显示端压、容量、功率、预计充满时间、压差、温度。
- 支持息屏、K3 长按 5 秒紧急重启、K2 长按 1.8 秒重新配对。

### 灵动数据

主表盘上下副显不再固定，而是按骑行情境自动切换；K1/K4 手动切换后 8 秒恢复自动。

**上副显优先级：**

1. 手动选择（8 秒）
2. 刚停车：TRIP
3. 海拔持续上升/下降：海拔
4. 驻车：时间
5. 骑行中：TRIP / MONTH / 时间 / 海拔轮换

**下副显优先级：**

1. 手动选择（8 秒）
2. 停车：剩余容量
3. 大功率或功率快速变化：功率
4. 骑行中：容量 / 功率 / 压差温度健康 / BMS 状态轮换

可在功能菜单的“灵动数据开关”中关闭自动切换。

### GPS / 北斗

- ATGM336H / AT6558，Serial1 115200，10Hz 输出。
- RMC 速度 5 帧中位数 + EMA 滤波，低速死区处理。
- TRIP / MONTH 里程积分，跨月自动清零并写 NVS。
- GGA 卫星数/海拔，GSV 载噪比/可见卫星统计。
- 失锁 3 秒清速度与旧定位数据；重新定位时重置速度滤波窗口。
- 跨月判断使用连续 3 帧确认，避免 GPS 日期跳变误清里程。

### BMS 蓝牙遥测

- NimBLE-Arduino 低功耗蓝牙，支持蚂蚁保护板新/旧协议解析。
- 总压、电流、SOC、SOH、容量、温度、压差、单体电压、充电 MOS 状态。
- 扫描结果使用 POD 环形队列交给主循环，避免 BLE 回调临界区堆操作。
- 自动重连后台任务化，不阻塞主循环；连接成功后校验 Notify 特征并订阅。
- BMS 数据 5 秒新鲜度检查，8 秒无包主动断开重连。
- 只有确认可订阅 Notify 后才写入配对信息，避免坏 MAC 污染 NVS。
- 息屏期间仍保持 BMS 轮询与后台重连。

### 数据持久化

NVS namespace：`rin_os`

| Key | 类型 | 说明 |
| :--- | :--- | :--- |
| `month_odo` | float | 本月累计里程（km） |
| `month_ym` | uint | 当前月份（YYYYMM） |
| `bms_name` | string | 已配对保护板名称 |
| `bms_mac` | string | 已配对保护板 MAC |
| `smart_data` | bool | 灵动数据开关 |

---

## 🛒 硬件清单与购买关键词

| 部件名称 | 推荐规格 / 芯片型号 | 作用 | 购买关键词 | 参考价 |
| :--- | :--- | :--- | :--- | :--- |
| **主控开发板** | **ESP32-S3-DevKitC-1**（N8 / 8MB Flash，无 PSRAM 即可） | 蓝牙、GPS 解析、UI 渲染 | `ESP32-S3 开发板 N8` | ￥18 ~ ￥25 |
| **圆形彩屏** | **1.28 寸 GC9A01 圆形 IPS**（240×240，SPI，8P） | 仪表显示 | `GC9A01 圆屏 1.28寸 SPI` | ￥15 ~ ￥22 |
| **卫星定位模块** | **ATGM336H**（成品模块 + 陶瓷天线） | GPS/北斗定位测速 | `ATGM336H 双模模块+天线` | ￥15 ~ ￥19 |
| **按键模块** | **4 位模拟量 AD 按键**（VCC/GND/OUT） | 菜单与页面交互 | `4位 AD按键模块` | ￥3 ~ ￥6 |
| **杜邦线 / 热缩管** | 20cm 母对母、公对母 | 接线与绝缘 | `杜邦线 20cm` | ￥3 ~ ￥5 |

> [!IMPORTANT]
> 购买 ATGM336H 时务必选择**带陶瓷天线的套餐**。推荐 18×18mm 或 25×25mm 方形陶瓷天线，陶瓷面朝天安装，搜星与抗干扰效果最好。

---

## 🔌 接线引脚

### 1. GC9A01 圆形屏（SPI）

| 屏幕引脚 | ESP32-S3 | 说明 |
| :--- | :--- | :--- |
| **SCL / CLK** | **GPIO 12** | SPI 时钟 |
| **SDA / MOSI / DIN** | **GPIO 11** | SPI 数据 |
| **DC / RS** | **GPIO 9** | 数据/命令选择 |
| **CS** | **GPIO 10** | 片选 |
| **RES / RST** | **GPIO 13** | 复位 |
| **BLK / LED** | **3.3V** | 背光常亮 |
| **VCC** | **3.3V**（或 5V） | 电源正 |
| **GND** | **GND** | 电源地 |

### 2. ATGM336H GPS/北斗（UART）

| GPS 引脚 | ESP32-S3 | 说明 |
| :--- | :--- | :--- |
| **TX** | **GPIO 17** | GPS 发送 → ESP32 RX1 |
| **RX** | **GPIO 18** | ESP32 TX1 → GPS 接收配置命令 |
| **VCC** | **3.3V** | 模块电源，严禁超过 3.6V（未加 LDO 时） |
| **GND** | **GND** | 电源地 |
| **PPS** | 悬空 | 1Hz 秒脉冲，不使用 |

### 3. 4 位 AD 按键模块

| 按键模块 | ESP32-S3 | 说明 |
| :--- | :--- | :--- |
| **OUT / AD** | **GPIO 8** | ADC1 模拟电压采样 |
| **VCC** | **3.3V** | 上拉基准 |
| **GND** | **GND** | 电源地 |

---

## 🚀 编译与刷机

项目基于 **PlatformIO + Arduino**，依赖已在 `platformio.ini` 中配置。

### 编译

```powershell
pio run
```

当前 v4.7 构建结果：

- RAM：34,604 / 327,680（10.6%）
- Flash：2,238,181 / 3,342,336（67.0%）

### 电脑 USB 刷写（推荐）

```powershell
# 编译并烧录
pio run -t upload

# 串口监视
pio device monitor -b 115200
```

### 整包刷写

`.pio/build/esp32-s3-devkitc-1/firmware_full.bin` 是 ESP32-S3 整包，包含：

| 偏移 | 内容 |
| :--- | :--- |
| `0x0` | bootloader |
| `0x8000` | partition table |
| `0xe000` | boot_app0 |
| `0x10000` | application |

> [!WARNING]
> 整包从 `0x0` 刷写会覆盖 NVS，导致保护板配对、本月里程、灵动数据开关丢失。
> 如需保留数据，只刷 `firmware.bin` 到 `0x10000`；或刷完整包后重新配对保护板并设置里程。

> [!NOTE]
> 推荐使用电脑 USB 刷写。部分手机 OTG 工具在刷整包时可能因线材、供电或工具兼容性导致 boot loop；如遇黑屏，优先改用电脑 `pio run -t upload` 恢复。

---

## 🖥️ PC 模拟器（开发预览）

`sim/` 是独立的 Windows SDL2 预览程序，复用固件真实 UI 绘制代码，不接 ESP32、GPS、保护板。

- 启动：双击 `sim\run_sim.cmd`，或运行 `sim\build\Release\rin_sim.exe`
- 窗口：240×240，2× 缩放（496×519 外框）
- 键位：
  - ↑ = K1，↓ = K4，→ = K2，← = K3
  - F1 = 配网向导
  - F2 = 充电大屏
  - F3 = 蓝牙释放页
- 模拟器只用于 UI 调试，不代表实机 GPIO / BLE / GPS 行为。

详细说明见 [`sim/README.md`](sim/README.md)。

---

## 📁 目录结构

```text
.
├── src/
│   └── main.cpp          # 固件主程序（UI、GPS、BMS、状态机）
├── sim/                  # Windows SDL2 交互预览（非固件）
├── platformio.ini        # PlatformIO 配置
├── README.md
├── CHANGELOG.md
└── LICENSE
```

---

## 📝 更新日志

完整版本记录见 [`CHANGELOG.md`](CHANGELOG.md)。

当前版本 **v4.7** 的主要变化：

- 统一圆屏视觉体系：菜单、电池遥测、GPS 遥测、充电、蓝牙释放、关于、清除本月。
- 新增灵动数据：上下副显按停车/骑行/海拔/功率情境自动切换。
- 新增灵动数据开关页。
- BMS 扫描队列、地址字节序、订阅校验、后台重连、数据新鲜度修复。
- GPS 失锁滤波重置、跨月 3 帧确认、时间校验、NMEA 溢出统计。
- 修复配网扫描启动失败、蓝牙释放页文字错位、充电页排版等问题。

---

## 📄 开源许可证

本项目遵循 [MIT License](LICENSE)。欢迎个人 DIY、车友改装交流与二次开发。

**官方网站与技术博客**：[https://miqwq.com](https://miqwq.com)
