<div align="center">
  <img src="docs/screenshots/settings-wpf-dark.png" width="960" alt="rapoo-tray 1.16.1 深色设备控制台" />
  <h1>rapoo-tray</h1>
  <p><b>Rimliya fork</b> · 电量、DPI、回报率、配置切换和屏幕 OSD，都从托盘里管。</p>

  <p>
    <a href="https://github.com/Rimliya/rapoo-tray/releases/tag/v1.16.1"><img src="https://img.shields.io/badge/Release-v1.16.1-161616?style=flat-square" alt="Release" /></a>
    <img src="https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-0078D6?style=flat-square&logo=windows" alt="Platform" />
    <img src="https://img.shields.io/badge/Stack-C%2B%2B20%20%7C%20Win32%20%7C%20WPF-00599C?style=flat-square&logo=c%2B%2B" alt="Stack" />
    <img src="https://img.shields.io/badge/License-MIT-green?style=flat-square" alt="License" />
  </p>
</div>

---

> **v1.16.1 没有代码签名。** 安装时 Windows 可能提示“未知发布者”。Release 同时提供 SHA256 校验文件，下载后可以对照。

## 先说这是什么

rapoo-tray 是给雷柏 VT 系列鼠标用的 Windows 托盘工具。

鼠标插上以后，托盘图标显示电量；点一下能看当前状态，右键可以换回报率、调休眠、切配置。要细调 DPI、做前台程序联动、看电量曲线，再打开设置中心。`Ctrl + Alt + M` 会叫出快速面板，不用离开游戏或当前窗口。

它不接管鼠标驱动，也不做宏和按键重映射。管的是雷柏鼠标本来就开放的那部分：电量、DPI、回报率、休眠、性能模式和配置。

## 这个 fork 和上游差在哪

仓库起于 [Iris-0109/rapoo-tray](https://github.com/Iris-0109/rapoo-tray)。上游公开主线停在轻量托盘工具的形态；这边继续往下走了 **48 个提交、102 个文件、16,000 行以上新增代码**。

改的不只是配色和图标。设置中心、配置模型、DPI 编辑器、电量历史、OSD、诊断、应用内更新、安装器以及发布流程都已经单独维护。版本号也不要混着看：这里使用的是 [Rimliya/rapoo-tray](https://github.com/Rimliya/rapoo-tray) 的 `v1.x` Release。

| 场景 | 上游公开主线 | 这里 |
| --- | --- | --- |
| 日常操作 | 托盘看电量、调回报率 | 托盘、快速面板、全局快捷键都能完成 |
| 配置 | 单组鼠标状态 | 多份配置，可按前台程序自动切换 |
| DPI / 性能 | 基础档位和回报率 | 型号限制、六档性能模式、自定义 DPI 档位 |
| 设置保存 | 无完整设置快照 | `.rapoo-snapshot` 保存整套设置，保留最近 20 份 |
| 排查问题 | 看设备状态 | Raw Input 回报率、抖动、最大间隔和脱敏报告 |
| 更新 | 去 Release 手动下载 | 应用内下载、SHA256 校验，失败后可直接重试 |
| 分发 | 基础构建 | 安装版、便携版、TrafficMonitor 插件和 Actions Release |

如果遇到问题，请带上 **Rimliya fork、版本号、鼠标型号和 Windows 版本**。上游 Issue 里不一定能复现这里的行为。

## v1.16.1 改了什么

这一版主要是把 1.16.0 留下的毛边收掉。

- **回报率采样更干净**：Raw Input 现在按当前雷柏设备的 VID/PID 过滤，不会把旁边另一只鼠标的输入混进来。
- **快照变成整套设置**：配置档案、自动切换、OSD、快捷键、电量提醒、自启动和型号覆盖一起保存；恢复前还会先备份当前状态。
- **更新失败有路可走**：下载失败后可以直接打开 Release、清理缓存、重新下载或重新安装，不用自己去翻 `%LOCALAPPDATA%`。
- **设置中心重新处理了文字**：中文正文回到 Regular 字重，字号、对比度和 ClearType 参数重新调过，避免小字发糊。
- **状态 OSD 换了排版**：型号和主读数分层，连接、DPI 档位、性能模式、电量和回报率放在底部分栏里，字段多也不会挤成一串。
- **快速面板标题不再截断**，宽体英文和版本号各归各位。

## 为什么还是原生程序

托盘、HID 通信和 OSD 都在 Windows 原生链路里。设置中心用 WPF，通过本机命名管道和托盘程序通信，没有为了几个设置页再塞一套 WebView2 或 Chromium。

这类工具的启动速度、输入延迟和系统占用比网页技术栈的花样更重要。界面可以做得像控制台，但底层最好别真的背一台浏览器。

## 下载

- [安装版 `rapoo-tray-setup.exe`](https://github.com/Rimliya/rapoo-tray/releases/download/v1.16.1/rapoo-tray-setup.exe)
- [便携版 `rapoo-tray-portable.zip`](https://github.com/Rimliya/rapoo-tray/releases/download/v1.16.1/rapoo-tray-portable.zip)
- [TrafficMonitor 插件 `rapoo-plugin.dll`](https://github.com/Rimliya/rapoo-tray/releases/download/v1.16.1/rapoo-plugin.dll)
- [全部版本](https://github.com/Rimliya/rapoo-tray/releases)

### 运行环境

- Windows 10 / 11 x64
- .NET Framework 4.8（Windows 10/11 一般自带）
- 日常使用不需要管理员权限
- 不要求 WebView2、Electron、Chromium 或额外鼠标驱动

### 数据放在哪

- 配置、快捷键和主题：`HKCU\Software\rapoo-tray`
- 配置快照：`%APPDATA%\rapoo-tray\snapshots`
- 更新缓存：`%LOCALAPPDATA%\rapoo-tray\updates`
- 设置中心与托盘只走本机命名管道，不监听网络端口
- 没有遥测或使用统计；联网只用于检查 GitHub Release、读取校验文件和下载更新
- 卸载不会替你删掉自定义快照

## 目前还做不到

- 同时管理多只鼠标或多个接收器。现在仍以一只活动设备为主。
- 用 USB/HID 抓包测回报率。诊断页测到的是 Windows Raw Input 到达时间，两者不是一回事。
- 自动识别所有雷柏型号的高级能力。未打标机型能走通用模式，但型号数据还要慢慢补。
- 受信任的代码签名。SmartScreen 信誉目前有限。
- 固件更新、宏录制、按键重映射。
- 跨分支配置迁移和自动回滚。覆盖安装前先导出配置，旧安装包也留着。

## 界面

设置中心用的是冷库控制台那一套：冷灰纸面、冰蓝信号、直角、切角按钮和真实计数。不是为了“科技感”堆发光，重点是让当前连接、当前配置和下一步操作一眼能分清。

<a href="docs/screenshots/settings-wpf-dark.png"><img src="docs/screenshots/settings-wpf-dark.png" width="49%" alt="深色配置控制台" /></a>
<a href="docs/screenshots/settings-wpf-fluent.png"><img src="docs/screenshots/settings-wpf-fluent.png" width="49%" alt="浅色配置控制台" /></a>

<a href="docs/screenshots/osd.png"><img src="docs/screenshots/osd.png" width="49%" alt="状态 OSD" /></a>
<a href="docs/screenshots/quick-panel.png"><img src="docs/screenshots/quick-panel.png" width="49%" alt="OSD 快速面板" /></a>

<a href="docs/screenshots/settings-diagnostics.png"><img src="docs/screenshots/settings-diagnostics.png" width="49%" alt="回报率与设备诊断" /></a>
<a href="docs/screenshots/settings-about.png"><img src="docs/screenshots/settings-about.png" width="49%" alt="设置快照与版本信息" /></a>

---

## 鼠标操作

| 操作 | 结果 |
| --- | --- |
| `Ctrl + Alt + M` | 打开快速面板，切换配置、DPI 档位和性能模式；组合键可在设置中心修改 |
| 左键单击 | 显示当前设备状态 OSD |
| 左键双击 | 轮换托盘电池图标样式 |
| 右键单击 | 打开托盘菜单，切换配置、回报率、休眠和启动项 |

## 托盘和硬件

- **托盘图标**按电量变色。低于 30% 显示红色，充电时显示绿色，设备休眠或离线时变成灰色。
- **右键菜单**可以直接改回报率、休眠时间和性能模式。改完就写入鼠标，不需要再进设置页。
- **回报率**支持 125、250、500、1000、2000、4000 和 8000 Hz，具体可选档位由鼠标当前模式和型号决定。
- **DPI 档位**可以增删、排序、分别设置 X/Y，并能直接从托盘切换。
- **配置档案**可以绑定前台程序。切进游戏时应用对应配置，退出后回到默认配置。
- **开机自启**写在当前用户的 `Run` 注册表项里，不会弹 UAC，也不会创建计划任务。

---

## 支持设备

下面这些型号经过实机验证；未列出的型号也可能通过通用模式工作。

| 设备型号 | 连接模式 | VID | PID | 支持状态 |
| :--- | :--- | :--- | :--- | :---: |
| **雷柏 VT7 V2 标准版** | 2.4G 无线接收器 | `0x24AE` | `0x1460` | 开发者实测 |
| **雷柏 VT7 V2 标准版** | USB 有线直连 | `0x24AE` | `0x4660` | 开发者实测 |
| **雷柏 VT3S** | 2.4G 无线接收器 | `0x24AE` | `0x1406` / `0x1410` | 社区实测，数据来自 [@hsb689](https://github.com/hsb689) |
| **雷柏 VT3S** | USB 有线直连 | `0x24AE` | `0x4606` / `0x1411` | 社区实测，数据来自 [@hsb689](https://github.com/hsb689) |
| **雷柏 VT3 MAX** | 2.4G 无线接收器 | `0x24AE` | `0x1417` | 社区实测，数据来自 [@sAchNMN](https://github.com/sAchNMN) |
| **雷柏 VT3 V2 标准版** | 2.4G 无线接收器 | `0x24AE` | `0x1412` / `0x1462` | 社区实测，数据来自 [@lbyisme](https://github.com/lbyisme) |
| **雷柏 VT3 V2 标准版** | USB 有线直连 | `0x24AE` | `0x4612` / `0x4662` | 社区实测，数据来自 [@lbyisme](https://github.com/lbyisme) |
| **雷柏二代游戏鼠标 (未打标机型)** | 2.4G / USB 有线 | `0x24AE` | 通用自动匹配 | 通用模式，待补充 |

### 未打标机型和 PID 提报

雷柏二代 Nordic 54L15 / 3950 方案的鼠标，底层报文基本一致。没收录的型号会先落到 **通用模式**，电量、DPI、回报率和休眠通常仍能使用。

如果型号识别不对，先插入鼠标，然后运行根目录的 `获取鼠标PID.bat`。脚本会把硬件 PID 和可提交的代码片段放进剪贴板；也可以在 PowerShell 中直接查：

```powershell
Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like "*24AE*" } | Select-Object FriendlyName, InstanceId
```

提交 Issue 时带上鼠标型号、连接方式和这段输出。已确认的 PID 会补进 `src/device_manager.cpp` 的 `VERIFIED_MODELS`。

---

## 配置档案与自动切换

1. 右键托盘图标，选择 **配置中心…**。
2. 在左侧选择已有配置，或点击 **新增配置**；可修改名称、回报率、DPI X/Y、休眠时间和是否设为默认配置。
3. 点击 **浏览…** 选择需要联动的程序（例如 `r5apex_dx12.exe`）。游戏切换为无边框或独占全屏后，Windows 仍会报告其前台窗口，因此该规则同样有效。
4. 保存后，进入所选程序时会应用该配置；切换到其他程序时会恢复 **默认配置**。
5. 托盘菜单中的 **快速切换配置** 可立即手动切换；手动切换不会修改默认回退配置。
6. “配置详情”里的 **DPI 参数** 会随档案保存，也可点 **读取当前** 获取鼠标当前 X/Y；旁边的 **打开 DPI 档位编辑器…** 可增删、排序和自定义完整档位列表。
7. “高级设置”中可勾选 **满格射频**（不勾选即智能调节），并选择鼠标型号查看粗略续航估算。
8. DPI 编辑器只会读取当前硬件档位；只有点击“应用并切换”才会写入鼠标，因此重启或更新程序不会把 DPI 恢复为出厂的 800。切换配置档案时则会把该档案保存的 DPI 写入当前档位。

> 配置、默认项和自动切换开关都写在 `HKCU\Software\rapoo-tray`。不需要管理员权限，也不会写进仓库。

---

## TrafficMonitor 插件

Release 里附带 TrafficMonitor 插件 `rapoo-plugin.dll`。把它放进 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 后，可以在任务栏窗口或主悬浮窗里显示鼠标电量和 DPI。

### 显示项目
- **鼠标电量**（例如：`M: 85%`，充电时显示为 `M: ⚡85%`）
- **鼠标 DPI**（例如：`DPI: 800`）
- **悬停信息**：显示型号、连接方式、具体电量和当前 DPI 档位。

### 安装步骤
1. 从 [Releases](https://github.com/Rimliya/rapoo-tray/releases) 下载 `rapoo-plugin.dll`。
2. 将 `rapoo-plugin.dll` 放置于 TrafficMonitor 根目录下的 `plugins` 文件夹中。
3. 重启 TrafficMonitor（或在右键菜单中选择“更多功能” -> “插件管理” -> “重新加载插件”）。
4. 在“选项” -> “任务栏窗口设置”或“主窗口设置”中，勾选“鼠标电量”和“鼠标DPI”。

---

## 安装与卸载

Release 里提供三个文件：

- **便携版 `rapoo-tray-portable.zip`**：解压后保持目录结构，双击 `rapoo-tray.exe` 运行。
- **安装包 `rapoo-tray-setup.exe`**：内含主程序、WPF 设置中心和运行依赖，安装后会创建快捷方式，可在控制面板卸载。
- **TrafficMonitor 插件 `rapoo-plugin.dll`**：给 TrafficMonitor 使用，不需要主程序以外的新依赖。

---

## 协议说明

程序通过 Windows 原生 HID API 与鼠标专有端点建立通信：

> 回报率与性能模式的完整实测矩阵、协议码和截图对照见 [docs/rapoo-performance-modes.md](docs/rapoo-performance-modes.md)。

### 1. 被动状态接收端点 (UsagePage: `0xFF00`, Usage: `0x0002`)
通过中断传输接收 Report ID 为 `0x07` 的 19 字节实时状态包：
- `Byte [0]`: `0x07` (Report ID)
- `Byte [1]`: 设备标识（2.4G 接收器固定为 `0x20`）
- `Byte [2]`: 当前 DPI 档位索引（从 0 开始）
- `Byte [3..4]`: X 轴 DPI 数值（16 位小端序整数）
- `Byte [5..6]`: Y 轴 DPI 数值（16 位小端序整数）
- `Byte [7]`: 活跃状态标志
- `Byte [8]`: 物理电量百分比（`0x00` ~ `0x64`，即 0% ~ 100%）

### 2. 双向控制与心跳检测通道 (UsagePage: `0xFF00`, Usage: `0x000E` / `0x000F`)
通过 33 字节 Command Report (`0x06`) 与 Feature Report (`0x08`) 实现寄存器读写与离线状态判定：
- **写入寄存器**：向 Usage `0x000E` 写入 `[0x06, 0xA5, 0xA5, 0x01, Addr, Bank, Value, ...]`。
- **读取寄存器与心跳**：向 Usage `0x000E` 发送读取命令后，通过 Usage `0x000F` 调用 `HidD_GetFeature` 获取 33 字节应答。若设备休眠或关机，通道立即产生超时应答，程序由此判定设备进入离线状态。

---

## 从源码构建

环境要求：Windows 10 / 11、Visual Studio 2022 C++ 工具链、CMake 3.21+、Ninja、.NET SDK 8+。`build.bat` 会同时编译 Win32 托盘与 WPF 设置中心；本机未安装 MSVC 时，开发工作区可使用 `.tools` 内的 Zig 回退工具链。

### 1. 编译可执行程序
```cmd
build.bat
```
输出产物：`bin\rapoo-tray.exe`、`bin\rapoo-tray-settings.exe` 及 WPF UI 依赖。本地构建只用于测试，不直接上传为正式发布包。

### 2. 编译安装程序
先完成主程序编译，再执行：
```cmd
cd installer
build_installer.bat
```
输出产物：`dist\rapoo-tray-setup.exe`

### 3. 编译 TrafficMonitor 插件
```cmd
cd plugin
build_plugin.bat
```
输出产物：`bin\rapoo-plugin.dll`


### 4. 正式发布流程
1. 本地运行 `build.bat` 做测试，不直接上传本地产物。
2. 提交并推送 `v*` tag 后，由 GitHub Actions 的 `release.yml` 使用 MSVC、.NET 与 MinGW 完成正式构建。
3. Release Step 使用仓库 `GITHUB_TOKEN` / Actions 的 `contents: write` 权限创建 GitHub Release。
4. Git 推送使用本机或 CI 配置的 GitHub token 凭据，禁止把 token 写进源码、脚本或构建产物。

---

## 第三方组件

- [WPF UI 4.3.0](https://github.com/lepoco/wpfui)：底层 WPF 控件、主题资源和 Windows 集成，MIT License。详见 [WPF UI License](licenses/wpf-ui-LICENSE.md) 与 [Third-party notices](licenses/wpf-ui-THIRDPARTY.txt)。
- [Saira](https://github.com/Omnibus-Type/Saira)：设置中心宽体界面拉丁字体，SIL Open Font License 1.1。详见 [Saira License](licenses/saira-OFL.txt)。
- [HarmonyOS Sans SC](https://developer.harmonyos.com/cn/docs/design/des-guides/font-0000001157868583)：设置中心内置中文正文字体，Copyright 2021 Huawei Device Co., Ltd.，按 HarmonyOS Sans Fonts License Agreement 随软件分发。详见 [HarmonyOS Sans License](licenses/harmonyos-sans-LICENSE.txt)。
- .NET Framework 4.8：Windows 10 / 11 内置运行时，设置中心无需额外安装运行时。

---

## 鸣谢

- **[@Nuitfanee](https://github.com/Nuitfanee)** ([ClickSync](https://github.com/Nuitfanee/ClickSync))：雷柏双向控制协议和寄存器地址映射的参考资料。
- **[@dts-dan](https://github.com/dts-dan)** ([logi-tray](https://github.com/dts-dan/logi-tray))：托盘电池图标的设计参考。
- **[@hsb689](https://github.com/hsb689)**：补充了雷柏 VT3S 系列的硬件 PID。
- **[@sAchNMN](https://github.com/sAchNMN)**：补充了雷柏 VT3 MAX 系列的硬件 PID。

---

## 免责声明

1. 这是个人维护的开源项目，与雷柏科技没有隶属关系，也不是雷柏官方软件。
2. “雷柏”与“RAPOO”商标及产品名称所有权归深圳市雷柏科技股份有限公司所有。
3. 软件按 MIT License 和“原样”（AS IS）提供。涉及硬件写入的功能请先备份配置并自行评估风险。

---

## 开发文档

- [开发路线与未完成项](docs/roadmap.md)
- [Windows 代码签名配置](docs/signing.md)

---

## 许可证

本项目基于 [MIT License](LICENSE) 开源。
