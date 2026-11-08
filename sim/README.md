# RinOS Car PC 模拟器

不接 ESP32 / GPS / 保护板，在 Windows 上直接预览并操作仪表 UI。  
这是开发预览工具，不参与 ESP32 固件编译。

## 启动

先构建，再双击 `run_sim.cmd`，或直接运行：

```text
build\Release\rin_sim.exe
```

窗口为 240×240 的 2 倍缩放（496×519 外框）。

## 键位

| 按键 | 功能 |
|---|---|
| ↑ | K1：切换下半区模式 / 菜单上移 |
| ↓ | K4：切换上半区模式 / 菜单下移 |
| → | K2：确认 / 主表盘打开功能菜单 |
| ← | K3：返回 |
| F1 | 回到“连接保护板”配网界面 |
| F2 | 直接进入充电大屏预览 |
| F3 | 直接进入蓝牙释放页预览 |

长按 K2（1.8 秒）退出模拟并重新配对；长按 K3（5 秒）模拟紧急重启。

## 说明

- 启动后直接进入模拟主表盘，使用 `updateSimulation()` 的假数据。
- 主表盘、菜单、电池遥测、GPS 遥测、关于、充电大屏、蓝牙释放页共用固件真实绘制代码。
- 不访问真实 BLE、GPS、NVS；生产固件的配网列表不再包含“模拟模式”入口。
- 这是 PC 预览，不代表实机 GPIO / BLE / GPS 行为。

## 重新构建

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build 'C:\Users\96943\Desktop\esp32\car\sim\build' --config Release
```

SDL2 2.32.10 静态库位于 `.pio\sdl2\build\Release\SDL2-static.lib`。  
`sim/CMakeLists.txt` 当前使用本机绝对路径；换机器构建时需要同步修改 LovyanGFX、SDL2 和 MSVC 路径。
