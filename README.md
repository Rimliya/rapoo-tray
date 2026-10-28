<div align="center">
  <img src="res/logo.png" width="128" height="128" alt="rapoo-tray logo" />
  <h1>rapoo-tray</h1>
  <p><b>雷柏VT系列游戏鼠标轻量化托盘程序</b></p>

  <p>
    <a href="https://github.com/Rimliya/rapoo-tray/releases"><img src="https://img.shields.io/badge/Release-v1.14.1-blue?style=flat-square" alt="Release" /></a>
    <img src="https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-0078D6?style=flat-square&logo=windows" alt="Platform" />
    <img src="https://img.shields.io/badge/Language-C%2B%2B20%20%2F%20C%23%20WPF%20%2B%20Win32-00599C?style=flat-square&logo=c%2B%2B" alt="Language" />
    <img src="https://img.shields.io/badge/License-MIT-green?style=flat-square" alt="License" />
  </p>
</div>

---

## 💡 项目简介

**rapoo-tray** 是面向雷柏（Rapoo）游戏鼠标用户的轻量化辅助工具。核心托盘、HID 通信和 Fluent 亚克力右键菜单继续使用 C++20 与 Win32；设置中心使用原生 WPF，以冷灰纸面、冰蓝信号、直角切角和机械式控件构成工业风界面，并提供完整明暗主题，不依赖 Chromium、Electron 或 WebView2。程序支持以任务栏系统托盘独立运行，或作为插件集成至 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor)，提供电量监测、DPI 显示、回报率调节、休眠配置等常用功能。

---

## 🚀 新版本特性

- **电量历史与提醒**：后台记录最近 30 天电量曲线，设置中心可按 24 小时 / 7 天 / 30 天查看；低电量、严重低电量与循环充电提醒的阈值和间隔均可配置。
- **OSD 快速面板（Endfield 控制台风格）**：默认按 `Ctrl + Alt + M` 全局呼出，直接切换配置、DPI 档位和性能模式；支持键盘导航、明暗主题与高对比度，快捷键和显示项目均可配置。
- **六档性能模式**：设置页新增“狂暴竞技 / 竞技超核 / 超核 / 火力 / 均衡 / 办公”六档模式，按回报率自动限制可选组合，并随配置档案一起保存、应用和前台自动切换；托盘右键菜单也可直接切换当前模式。
- **DPI 档位编辑器与托盘快速切换**：读取鼠标当前 DPI 档位，以 DPI 寄存器配置为权威来源，避免状态广播中的旧 800 DPI 覆盖实际值；支持按型号限制的 DPI 范围、增删档位、自定义 X/Y、升序排列和独立双轴设置。托盘菜单可直接选择当前型号已配置的 DPI 档位，启动/更新不会重置鼠标当前 DPI。
- **无线射频策略与续航粗估**：设置页可切换“满格射频 / 智能调节”，并按鼠标型号、回报率、射频策略和当前电量估算续航；型号支持自动识别或手动指定。
- **原地更新与强制停机**：安装器会从卸载信息、开机启动项或正在运行的托盘程序定位已有安装目录；安装前强制执行“检测并停止旧程序、确认进程退出、再覆盖文件”的顺序，无法停止时直接中止安装。
- **设备识别增强**：补充 VT3 V2 的 `1462/4662` PID 变体，并增加 HID 产品字符串回退识别。
- **明暗双主题**：设置中心右上角可一键切换浅色/深色主题并持久化；首次启动仍跟随 Windows 系统主题。
- **配置导入导出**：配置中心可生成 `.rapoo` 配置包，携带全部配置、默认项与自动切换开关；导入前自动备份，包内字段经校验后一次性替换，避免半写入。
- **统一安装体验**：安装器改为与设置中心一致的浅色工业风界面，包含部署步骤、方形选项开关、切角操作按钮和实际安装进度，完成状态在窗口内呈现。
- **内置 HarmonyOS Sans SC**：设置中心随包嵌入 Regular / Medium / Bold 三个字重，中文、字段标签与输入内容不再依赖系统中文字体；输入框、下拉框和数字框统一垂直居中，并启用 Display 字形布局与像素对齐。
- **射频策略读取修复**：进入自动化页时主动读取鼠标当前射频策略；读取失败显示可重试状态，不再误报“设备不支持”。
- **工业风 WPF 设置中心**：配置档案、自动化、DPI 档位与关于页使用统一冷灰网格、冰蓝当前位置信号、自绘切角、方形开关和方形滑杆；性能档位改为墨色分段选择，明暗主题与系统主题跟随保持一致。
- **配置档案与快速切换**：可新建、重命名和删除独立配置，每个配置同时保存回报率、性能模式、DPI X/Y 与休眠时间；设置页将“保存”和“应用”拆开，保存只更新本机配置，应用才写入鼠标。托盘菜单和“配置中心”均可快速切换，硬件应用在后台执行。
- **前台程序自动切换**：为配置指定程序后，程序进入前台（包括无边框 / 独占全屏游戏）时自动应用该配置；离开程序后恢复用户指定的默认配置。
- **自动化总开关**：可分别关闭“前台程序自动切换”和“悬浮窗提示 (OSD)”，不影响原有手动回报率、休眠与托盘功能。
- **硬件兼容体系重构**：建立雷柏二代 Nordic 架构通用兼容机制，未打标机型自动归入通用模式，支持使用主要功能。
- **托盘电池个性化**：新增 3 种展示风格：
  - **配置一**：经典电池胶囊外观
  - **配置二**：环形电量进度风格
  - **配置三**：纯数字搭配底部横轨
- **美化信息悬浮窗 (OSD)**：
  - 弃用固定宽框，改为毫秒级文本动态计算卡片宽度（`DT_CALCRECT`）。
  - 三行精简架构，型号、连接、DPI 档位与数值、电量、回报率和性能模式均可独立显示或隐藏。
  - 默认快捷键 `Ctrl + Alt + M` 呼出快速面板，可不进设置中心直接切换配置、DPI 与性能模式。
  - 快速面板顶部电量、回报率、型号信息和三组切换区均可配置。
- **托盘右键面板**：菜单设计成 Fluent 亚克力材质；新增回报率调节、休眠配置功能。
- **关于与在线更新检查**：托盘右键菜单新增“关于 rapoo-tray”，集中显示版本号、项目源码入口，并可异步检查 GitHub Releases 最新版本。

---

## 📸 效果展示 (Showcase)

| 任务栏托盘图标 (双击循环切换 3 种样式) | OSD 屏幕自适应悬浮窗 (支持 X/Y 轴独立显示) |
| :---: | :---: |
| <img src="docs/screenshots/battery_style_1.png" height="42" alt="配置一：经典电池" /> &nbsp;&nbsp; <img src="docs/screenshots/battery_style_2.png" height="42" alt="配置二：状态大圆点" /> &nbsp;&nbsp; <img src="docs/screenshots/battery_style_3.png" height="42" alt="配置三：大号数字" /><br><sub>配置一：经典电池 &nbsp;·&nbsp; 配置二：环形电量 &nbsp;·&nbsp; 配置三：滑轨数字</sub> | <img src="docs/screenshots/osd.png" width="300" alt="OSD 屏幕自适应悬浮窗" /> |

| 工业风设置中心（浅色） | 工业风设置中心（深色） |
| :---: | :---: |
| <img src="docs/screenshots/settings-wpf-fluent.png" width="520" alt="工业风浅色设置中心" /> | <img src="docs/screenshots/settings-wpf-dark.png" width="520" alt="工业风深色设置中心" /> |

| Fluent 亚克力右键菜单 |
| :---: |
| <img src="docs/screenshots/menu.png" width="260" alt="原生 Fluent 亚克力右键控制面板" /> |

| 休眠调节滑块 |
| :---: |
| <img src="docs/screenshots/sleep_slider.png" width="260" alt="2~120 分钟连续平滑休眠滑动条" /> |
---

## 🖱️ 鼠标交互

| 操作手势 | 响应动作 | 视觉反馈 |
| :--- | :--- | :--- |
| **`Ctrl + Alt + M`** | 呼出 OSD 快速面板 | 直接切换配置、DPI 档位与性能模式；快捷键可在设置中心重录 |
| **鼠标左键单击** | 唤起屏幕右下角状态指示窗 | 弹出当前设备型号、DPI、双轴数值、电量与回报率 OSD |
| **鼠标左键双击** | 循环切换托盘电池图标样式 | 在“配置一 (经典电池)”、“配置二 (环形电量)”、“配置三 (滑轨数字)”间轮换，并弹出 OSD 提示 |
| **鼠标右键单击** | 打开 Fluent 亚克力托盘菜单 | 快速切换配置、调节回报率与休眠、打开 OSD 快速面板、切换 OSD / 开机自启 |

---

## 🎯 功能特性

1. **工业风设置与原生托盘**：
   - 系统托盘、HID 通信、自动切换与消息循环保持原生 Win32；设置中心使用原生 WPF，不引入 Chromium、Electron 或 WebView2。
   - 设置窗口使用冷灰网格与冰蓝当前位置信号；主程序和设置中心通过本机命名管道通信，避免两套 HID 轮询同时访问鼠标。
2. **多态电池托盘图标**：
   - 智能四色指示：$\le 30\%$ 红色警示、$> 30\%$ 科技蓝/主题色、充电中翡翠绿、离线/休眠暗灰虚线或横杠。
   - 配置与样式选择自动持久化保存于 Windows 注册表。
3. **硬件级回报率与休眠控制**：
   - 支持 125Hz、250Hz、500Hz、1000Hz（标准）及 2000Hz、4000Hz、8000Hz（电竞高刷）即时切换。
   - 设置页提供六档性能模式；每档回报率只开放固件允许的组合，切换配置时一起应用。
   - 性能模式寄存器采用 4 字节读改写、保留未知字段，并在写入后回读确认。
   - 休眠时间支持从 2 分钟到 120 分钟拖块调节或滚轮调节，且配置即刻下发至硬件。
4. **开机自启**：
   - 基于当前用户注册表 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 实现，无需管理员权限，启动无弹窗干扰。

---

## 📋 支持设备列表 (Supported Devices)

| 设备型号 | 连接模式 | VID | PID | 支持状态 |
| :--- | :--- | :--- | :--- | :---: |
| **雷柏 VT7 V2标准版** | 2.4G 无线接收器 | `0x24AE` | `0x1460` | ✅ 开发者实测支持 |
| **雷柏 VT7 V2标准版** | USB 有线直连 | `0x24AE` | `0x4660` | ✅ 开发者实测支持 |
| **雷柏 VT3S** | 2.4G 无线接收器 | `0x24AE` | `0x1406` / `0x1410` | ✅ 社区实测支持（由社区用户 [@hsb689](https://github.com/hsb689) 提供） |
| **雷柏 VT3S** | USB 有线直连 | `0x24AE` | `0x4606` / `0x1411` | ✅ 社区实测支持（由社区用户 [@hsb689](https://github.com/hsb689) 提供） |
| **雷柏 VT3 MAX** | 2.4G 无线接收器 | `0x24AE` | `0x1417` | ✅ 社区实测支持（由社区用户 [@sAchNMN](https://github.com/sAchNMN) 提供） | 
| **雷柏 VT3 V2标准版** | 2.4G 无线接收器 | `0x24AE` | `0x1412` / `0x1462` | ✅ 社区实测支持（由社区用户 [@lbyisme](https://github.com/lbyisme) 提供） |
| **雷柏 VT3 V2标准版** | USB 有线直连 | `0x24AE` | `0x4612` / `0x4662` | ✅ 社区实测支持（由社区用户 [@lbyisme](https://github.com/lbyisme) 提供） |
| **雷柏二代游戏鼠标 (未打标机型)** | 2.4G / USB 有线 | `0x24AE` | 通用自动匹配 | 🔑 待验证 |

### 💡 通用兼容机制与型号提报指南

1. **二代 Nordic 架构全功能即插即用**：
   雷柏二代游戏鼠标（基于 Nordic 54L15 / 3950 方案）在底层通信报文协议（UsagePage `0xFF00`、Report ID `0x07` 与控制指令集）上完全统一。即使您的鼠标尚未打标，程序也会自动以 **`通用`** 模式挂载运行，**电量读取、DPI 切换、回报率设置与休眠控制均支持使用**。
2. **一键提取鼠标硬件 PID 并提交 PR（仅需 1 行代码）**：
   插入鼠标后，直接双击运行项目根目录下的 **`获取鼠标PID.bat`**（或执行 `tools/get_mouse_pid.ps1`），脚本会自动提取鼠标硬件 PID 并生成代码片段复制到剪贴板；也可在 PowerShell 中手动执行：
   ```powershell
   Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like "*24AE*" } | Select-Object FriendlyName, InstanceId
   ```
   在 `src/device_manager.cpp` 的 `VERIFIED_MODELS` 映射表中添加对应行（如 `{ L"14xx", L"雷柏 VT9 Pro" }`），提交 PR 即可合并！

---

## 🎛️ 配置档案与自动切换

1. 右键托盘图标，选择 **配置中心…**。
2. 在左侧选择已有配置，或点击 **新增配置**；可修改名称、回报率、DPI X/Y、休眠时间和是否设为默认配置。
3. 点击 **浏览…** 选择需要联动的程序（例如 `r5apex_dx12.exe`）。游戏切换为无边框或独占全屏后，Windows 仍会报告其前台窗口，因此该规则同样有效。
4. 保存后，进入所选程序时会应用该配置；切换到其他程序时会恢复 **默认配置**。
5. 托盘菜单中的 **快速切换配置** 可立即手动切换；手动切换不会修改默认回退配置。
6. “配置详情”里的 **DPI 参数** 会随档案保存，也可点 **读取当前** 获取鼠标当前 X/Y；旁边的 **打开 DPI 档位编辑器…** 可增删、排序和自定义完整档位列表。
7. “高级设置”中可勾选 **满格射频**（不勾选即智能调节），并选择鼠标型号查看粗略续航估算。
8. DPI 编辑器只会读取当前硬件档位；只有点击“应用并切换”才会写入鼠标，因此重启或更新程序不会把 DPI 恢复为出厂的 800。切换配置档案时则会把该档案保存的 DPI 写入当前档位。

> 所有配置、默认项和自动化开关均保存在 `HKCU\Software\rapoo-tray`，无需管理员权限，也不会写入仓库。

---

## 🔌 TrafficMonitor 插件使用指南

本项目提供官方适配的 TrafficMonitor 扩展插件（`rapoo-plugin.dll`），可在系统监控软件 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 的任务栏窗口与主悬浮窗中显示鼠标状态。

### 显示项目
- **鼠标电量**（例如：`M: 85%`，充电时显示为 `M: ⚡85%`）
- **鼠标 DPI**（例如：`DPI: 800`）
- **悬停信息 (Tooltip)**：鼠标悬停在监控项上可查看型号、连接模式（2.4G/USB）、详细电量与当前档位。

### 安装步骤
1. 前往 [Releases](https://github.com/Rimliya/rapoo-tray/releases) 下载 `rapoo-plugin.dll`。
2. 将 `rapoo-plugin.dll` 放置于 TrafficMonitor 根目录下的 `plugins` 文件夹中。
3. 重启 TrafficMonitor（或在右键菜单中选择“更多功能” -> “插件管理” -> “重新加载插件”）。
4. 在“选项” -> “任务栏窗口设置”或“主窗口设置”中，勾选“鼠标电量”和“鼠标DPI”即可。

---

## 📥 下载与安装

请前往 [GitHub Releases](https://github.com/Rimliya/rapoo-tray/releases) 下载最新发行版：

- **便携版 (`rapoo-tray-portable.zip`)**：解压后保持目录内文件结构不变，双击 `rapoo-tray.exe` 即可运行。
- **一键安装向导 (`rapoo-tray-setup.exe`)**：约 9 MB，内置主程序、WPF 设置中心与依赖，自动部署至本地目录，创建快捷方式并支持控制面板卸载。
- **TrafficMonitor 插件 (`rapoo-plugin.dll`)**：约 28 KB，供 TrafficMonitor 用户按需下载使用。

---

## 🔬 技术协议解析 (Protocol Reverse Engineering)

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

## 🛠️ 源码编译 (Build from Source)

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

## ⚖️ 第三方组件

- [WPF UI 4.3.0](https://github.com/lepoco/wpfui)：底层 WPF 控件、主题资源和 Windows 集成，MIT License。详见 [WPF UI License](licenses/wpf-ui-LICENSE.md) 与 [Third-party notices](licenses/wpf-ui-THIRDPARTY.txt)。
- [Saira](https://github.com/Omnibus-Type/Saira)：设置中心宽体界面拉丁字体，SIL Open Font License 1.1。详见 [Saira License](licenses/saira-OFL.txt)。
- [HarmonyOS Sans SC](https://developer.harmonyos.com/cn/docs/design/des-guides/font-0000001157868583)：设置中心内置中文正文字体，Copyright 2021 Huawei Device Co., Ltd.，按 HarmonyOS Sans Fonts License Agreement 随软件分发。详见 [HarmonyOS Sans License](licenses/harmonyos-sans-LICENSE.txt)。
- .NET Framework 4.8：Windows 10 / 11 内置运行时，设置中心无需额外安装运行时。

---

## 🤝 鸣谢与致敬 (Credits & Acknowledgments)

- **[@Nuitfanee](https://github.com/Nuitfanee)** ([ClickSync](https://github.com/Nuitfanee/ClickSync))：提供了雷柏双向控制协议逆向与寄存器地址映射的关键技术启发。
- **[@dts-dan](https://github.com/dts-dan)** ([logi-tray](https://github.com/dts-dan/logi-tray))：提供了托盘电池图标的设计灵感与创意启发。
- **[@hsb689](https://github.com/hsb689)**：提供了雷柏 VT3S 系列硬件 PID 数据。
- **[@sAchNMN](https://github.com/sAchNMN)**：提供了雷柏 VT3 MAX 系列硬件 PID 数据。

---

## ⚠️ 免责声明 (Disclaimer)

1. 本项目为个人开源业余作品，旨在为雷柏无线鼠标用户提供轻量、低占用的系统级托盘助手，**非雷柏官方出品**。
2. “雷柏”与“RAPOO”商标及产品名称所有权归深圳市雷柏科技股份有限公司所有。
3. 软件按“原样”（AS IS）提供，作者不对使用过程中可能出现的任何偶发问题承担法律责任。

---

## 📄 开源许可证 (License)

本项目基于 [MIT License](LICENSE) 开源。
