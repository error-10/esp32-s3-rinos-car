# RinOS-Car: ESP32-S3 智能圆形车载液晶仪表盘系统

<div align="center">

![Platform](https://img.shields.io/badge/Platform-ESP32--S3-blue.svg)
![Framework](https://img.shields.io/badge/Framework-Arduino%20%2F%20PlatformIO-orange.svg)
![Screen](https://img.shields.io/badge/Screen-GC9A01%20240x240%20IPS-green.svg)
![License](https://img.shields.io/badge/License-MIT-purple.svg)

**专为电动车、电动摩托、机车量身打造的开源智能圆形彩色液晶仪表系统**  
高精度 GPS/北斗测速 · 蓝牙 BMS 电池遥测 · 独占流光充电大屏 · 宝马扫表开机动效 · 60FPS 丝滑渲染

**作者主页 / 博客**：[https://miqwq.com](https://miqwq.com)

</div>

---

## 🌟 系统核心功能与设计亮点

1. **宝马 / 机车自检开机动效**
   - 开机启动完成并连接保护板后，自动触发机车/宝马 M 风格的 $0 \to 100 \text{ km/h}$ 极速扫表与满表高光轰鸣自检，兼具科幻感与机械仪式感。
2. **自研 5 帧滑动窗口中位数滤波 + 轻量 EMA 测速算法**
   - **静态零漂死区截断**：车速 $< 2.0\text{ km/h}$ 强制定零，彻底消除红绿灯停车时的微小多径漂移。
   - **停车瞬间归零（Zero Latency Flush）**：检测到静止瞬间立即清空历史窗口，彻底杜绝普通 GPS 速度表停车后读数迟迟不归零的拖尾问题。
   - **多径脉冲毛刺全过滤**：高架、桥洞、楼宇多径反射偶尔出现的单帧离谱高速毛刺（如静止时误报 30+ km/h）被中位数天然吸收，数字绝对不会乱跳。
   - **动态 EMA 平滑微阻尼**：巡航时个位数绝不高频闪烁，急起急刹响应极速。
3. **BLE 保护板（BMS）无线遥测**
   - 自动扫描并低功耗蓝牙接入保护板，实时提取剩余电量百分比（SOC）、实时总电压、实时充放电电流、单体最大压差、电池温度等关键数据。
   - **动能回收与行驶硬拦截**：车速 $> 0.5\text{ km/h}$ 坚决屏蔽充电屏，彻底解决滑行或刹车动能回收反充时误跳充电大屏的问题。
4. **独占智能充电流光大屏**
   - 插入充电器自动唤醒专属全屏充电 UI。
   - **等离子彗星流光**：流光环转速与真实物理充电电流严格线性联动，电流越大流速越快。
   - 动态计算并显示充入电量、实时充电功率、以及智能预估充满剩余时间。
5. **多功能按键交互**
   - 采用梯形电阻分压单引脚 ADC 读取，支持 4 按键单击、长按复位单次里程等。

---

## 🛒 硬件清单与购买关键词推荐

| 部件名称 | 推荐规格 / 芯片型号 | 作用 | 购买关键词（淘宝/拼多多/立创） | 预估参考价 |
| :--- | :--- | :--- | :--- | :--- |
| **主控开发板** | **ESP32-S3-DevKitC-1** (N8 / 8MB Flash，无 PSRAM 即可) | 核心主控，负责蓝牙、GPS 解析、UI 渲染 | `ESP32-S3 开发板 N8` 或 `ESP32-S3 DevKitC-1` | ￥18 ~ ￥25 |
| **圆形彩屏** | **1.28 寸 GC9A01 圆形 IPS 模块** (240x240 分辨率，SPI 接口，8P 排针) | 仪表盘高清显示，全贴合高透视角度 | `GC9A01 圆屏 1.28寸 SPI` 或 `240*240 圆形彩屏` | ￥15 ~ ￥22 |
| **卫星定位模块** | **ATGM336H 成品模块 + 陶瓷天线** (已焊好 5P 排针，带陶瓷天线) | 北斗+GPS 双模卫星定位测速 | `ATGM336H 双模模块+天线 已焊排针` | ￥15 ~ ￥19 |
| **按键模块** | **4 位模拟量 AD 按键模块** (3 线制：VCC, GND, OUT/AD) | 仪表菜单切页、里程清零等交互 | `4位 AD按键模块` 或 `单总线电阻梯按键` | ￥3 ~ ￥6 |
| **电池保护板** | **蚂蚁 BMS / 极空 BMS / 达锂 BMS** (带 BLE 蓝牙) | 电池数据来源（无线传输） | 现有车辆电池自带保护板即可 | - |
| **杜邦线 / 导线** | 20cm 母对母 / 公对母杜邦线、热缩管 | 电路连接与绝缘加固 | `杜邦线 20cm`、`绝缘热缩管` | ￥3 ~ ￥5 |

> [!IMPORTANT]
> **关于 GPS 天线选购**：
> 购买 ATGM336H 时**务必勾选带天线的套餐**（不要只买单板）！推荐搭配 $18\times 18\text{mm}$ 或 $25\times 25\text{mm}$ 方形陶瓷天线，陶瓷面朝天安装，搜星速度与抗干扰能力最佳。

---

## 🔌 硬件接线引脚对照表 (Wiring Pinout)

### 1. GC9A01 圆形显示屏 (SPI 接口)
| 屏幕引脚标注 | 对应 ESP32-S3 引脚 | 说明 |
| :--- | :--- | :--- |
| **SCL** (或 CLK) | **GPIO 12** | SPI 时钟线 (SCLK) |
| **SDA** (或 MOSI/DIN) | **GPIO 11** | SPI 数据输出线 (MOSI) |
| **DC** (或 RS) | **GPIO 9** | 数据 / 命令选择线 |
| **CS** | **GPIO 10** | 片选引脚 |
| **RES** (或 RST) | **GPIO 13** | 复位引脚 |
| **BLK** (或 LED) | **3.3V** | 背光常亮 (也可悬空或接 3.3V) |
| **VCC** | **3.3V** (或 5V) | 电源正极 |
| **GND** | **GND** | 电源地 |

---

### 2. ATGM336H GPS/北斗模块 (UART 交叉串口)
| GPS 模块引脚 | 对应 ESP32-S3 引脚 | 说明 |
| :--- | :--- | :--- |
| **TX** (或 TXD) | **GPIO 17** | GPS 发送 $\to$ ESP32 接收 (RX1) |
| **RX** (或 RXD) | **GPIO 18** | ESP32 发送 (TX1) $\to$ GPS 接收配置命令 |
| **VCC** | **3.3V** | 模块电源（严禁超过 3.6V 如果未经过 LDO） |
| **GND** | **GND** | 电源地 |
| **PPS** | 悬空 | 1Hz 秒脉冲指示（不用接） |

---

### 3. 4 位 AD 按键模块 (模拟分压)
| 按键模块引脚 | 对应 ESP32-S3 引脚 | 说明 |
| :--- | :--- | :--- |
| **OUT / AD** | **GPIO 8** | 模拟电压采样引脚 (ADC1) |
| **VCC** | **3.3V** | 按键上拉基准电源 |
| **GND** | **GND** | 电源地 |

---

## 🚀 快速上手与刷机指南

本项目基于标准的 **PlatformIO** 开发环境，所有第三方库均已在 `platformio.ini` 中配置好自动化依赖管理，任何人下载源码后均可**一键编译、一键上传**。

### 方法一：VS Code + PlatformIO 插件（最推荐）

1. 在电脑上安装 [VS Code](https://code.visualstudio.com/)。
2. 在 VS Code 扩展商店中搜索并安装 **PlatformIO IDE** 插件。
3. 克隆或下载本仓库：
   ```bash
   git clone https://github.com/error-10/esp32-s3-rinos-car.git
   ```
4. 在 VS Code 中点击 `File` $\to$ `Open Folder...`，直接打开解压后的 `esp32-s3-rinos-car` 根目录。
5. 用 Type-C 数据线将 ESP32-S3 开发板连接到电脑。
6. 点击 VS Code 底部状态栏的 **对勾图标 (Build)** 编译，或点击 **向右箭头图标 (Upload)** 一键刷入！

---

### 方法二：命令行一键编译上传

如果你习惯使用命令行或终端：

```powershell
# 1. 编译固件
pio run

# 2. 编译并烧录至 ESP32-S3
pio run -t upload

# 3. 打开串口监视器查看实时调试信息
pio device monitor -b 115200
```

---

## 📦 依赖库列表 (已在 platformio.ini 自动集成)

- [LovyanGFX](https://github.com/lovyan03/LovyanGFX) `@ ^1.2.30`：极其强大的 ESP32 跨平台高性能图形渲染驱动，支持 DMA 与超高速局部刷新。
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) `@ ^1.4.3`：比原版 Bluedroid 节省 60% 内存的超轻量蓝牙协议栈。
- [Preferences](https://github.com/espressif/arduino-esp32) `@ ^2.0.0`：断电保存历史里程与累计统计。

---

## 📄 开源许可证

本项目遵循 [MIT License](LICENSE) 开源许可协议。欢迎个人 DIY、车友改装交流与二次开发。

**官方网站与技术博客**：[https://miqwq.com](https://miqwq.com)
