<div align="center">
  <img src="docs/screenshots/settings-wpf-dark.png" width="960" alt="rapoo-tray 1.16.1 深色设备控制台" />
  <h1>rapoo-tray · Rimliya fork</h1>
  <p><b>从轻量托盘工具，扩展为完整的雷柏 VT 设备控制中心</b></p>

  <p>
    <a href="https://github.com/Rimliya/rapoo-tray/releases/tag/v1.16.1"><img src="https://img.shields.io/badge/Fork%20Release-v1.16.1-161616?style=flat-square" alt="Fork Release" /></a>
    <a href="https://github.com/Iris-0109/rapoo-tray"><img src="https://img.shields.io/badge/Upstream-Iris--0109-7DE6FF?style=flat-square" alt="Upstream" /></a>
    <img src="https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-0078D6?style=flat-square&logo=windows" alt="Platform" />
    <img src="https://img.shields.io/badge/Stack-C%2B%2B20%20%7C%20Win32%20%7C%20WPF-00599C?style=flat-square&logo=c%2B%2B" alt="Stack" />
    <img src="https://img.shields.io/badge/License-MIT-green?style=flat-square" alt="License" />
  </p>
</div>

---

> **v1.16.1 未签名发布**：Release 不进行代码签名，仅提供 SHA256 校验文件。Windows 可能显示 SmartScreen 或“未知发布者”提示。它不影响程序功能，但下载后请核对 GitHub Release 中的校验值。

## 💡 这个项目是什么

**rapoo-tray · Rimliya fork** 是基于 [Iris-0109/rapoo-tray](https://github.com/Iris-0109/rapoo-tray) 独立演进的雷柏 VT 系列鼠标控制工具。

核心托盘、HID 通信、全局热键和 OSD 使用 C++20 / Win32；设置中心使用原生 WPF，不依赖 Electron、Chromium 或 WebView2。相比只负责电量与回报率的轻量托盘工具，这个分支已经扩展到：

- 配置档案、前台程序自动切换与完整设置快照
- 六档性能模式、DPI 档位编辑器和型号能力限制
- 状态 OSD、OSD 快速面板与五组全局快捷键
- 电量历史、低电量提醒与充电循环提醒
- Raw Input 回报率验证、设备诊断和脱敏报告
- 应用内更新、安装版、便携版和 TrafficMonitor 插件
- Endfield 冷库控制台风格的完整明暗主题与高对比度适配

## 🔀 与上游的关系和差异

这不是只换了一层皮肤的镜像分支。截至 **v1.16.1**，相对上游公开主线累计 **46 个提交、100 个文件、15,000 行以上新增代码**，版本节奏、界面层、配置模型、构建链和发布流程都已独立。

| 范围 | 上游公开主线 | 本分支 v1.16.1 |
| --- | --- | --- |
| 定位 | 轻量托盘与基础硬件控制 | 托盘 + 设置中心 + 诊断 + 自动化的完整控制中心 |
| 界面 | 基础原生界面 | 原生 WPF 设置中心、Endfield 冷库视觉、明暗主题、高对比度 |
| 配置 | 单组硬件状态 | 多配置档案、前台程序绑定、默认配置与自动切换 |
| DPI / 性能 | 基础档位与回报率 | 型号能力限制、六档性能模式、可编辑 DPI 档位与双轴配置 |
| 快照 / 恢复 | 无完整设置快照 | `.rapoo-snapshot` 完整设置快照，最多保留 20 份并兼容旧 `.rapoo` |
| 诊断 | 基础设备信息 | 按设备过滤的 Raw Input 回报率、抖动、最大间隔与诊断导出 |
| 更新 | 手动下载 | 应用内检查、SHA256 校验、失败恢复与可选签名验证 |
| 分发 | 基础构建 | 安装器、便携包、插件、SHA256 资产和 Actions Release |
| 构建 | Windows 原生工具链 | MSVC/CMake/Ninja + .NET，另有 Zig 本地回退构建 |

**版本与 Issue 说明：**

- 本分支使用独立的 `v1.x` 版本号和 [Rimliya/rapoo-tray](https://github.com/Rimliya/rapoo-tray) Release。
- 反馈问题时必须注明“Rimliya fork”、版本号和 Windows 版本；上游 Issue 区不一定能复现这里的行为。
- 上游代码、协议研究和社区设备数据仍是本分支的重要基础，许可证与鸣谢在文末保留。
- 本分支不承诺自动跟随上游合并；涉及注册表、配置格式、界面资源或硬件写入的改动应先备份。

## 🚀 v1.16.1 更新

### 稳定性与数据

- **Raw Input 按设备过滤**：读取当前雷柏 VID/PID，只统计目标设备接口，不再混入其他鼠标输入。
- **完整设置快照**：保存配置档案、自动切换、OSD/快捷键、电量提醒、自启动与型号覆盖；恢复前自动再次备份。
- **更新失败恢复**：支持打开 Release、清理缓存、重新下载和重新安装，不再需要手动删除更新目录。
- **未签名包校验**：强制 SHA256；如果发布资产带 Authenticode 签名，再额外验证发布者。

### UI 与 OSD

- **设置中心可读性第二轮**：Regular 中文正文、14px 基准字号、更高次要文字对比度、`Display + Fixed + ClearType`。
- **双层状态 OSD**：型号与主读数分层，连接、DPI 档位、性能、电量和回报率在底部分栏显示。
- **分层 HUD 抗锯齿**：使用灰阶 `AntiAliasGridFit`，减少透明窗口上的彩色边缘和中文发糊。
- **快速面板标题修复**：宽体英文标题不再被截断，保留切角、真实计数和冰蓝选中状态。

## 📦 下载与安装

生产构建由 GitHub Actions 生成，不直接上传本地测试产物：

- [安装版 `rapoo-tray-setup.exe`](https://github.com/Rimliya/rapoo-tray/releases/download/v1.16.1/rapoo-tray-setup.exe)
- [便携版 `rapoo-tray-portable.zip`](https://github.com/Rimliya/rapoo-tray/releases/download/v1.16.1/rapoo-tray-portable.zip)
- [TrafficMonitor 插件 `rapoo-plugin.dll`](https://github.com/Rimliya/rapoo-tray/releases/download/v1.16.1/rapoo-plugin.dll)
- [全部 Releases](https://github.com/Rimliya/rapoo-tray/releases)

### 系统要求

- Windows 10 / 11 x64
- .NET Framework 4.8（Windows 10/11 通常已内置）
- 无需管理员权限即可运行；安装到受保护目录时由安装器请求权限
- 不依赖 WebView2、Electron、Chromium 或额外鼠标驱动

### 数据与隐私

- 配置、快捷键和界面主题保存在 `HKCU\Software\rapoo-tray`。
- 配置快照位于 `%APPDATA%\rapoo-tray\snapshots`，更新缓存位于 `%LOCALAPPDATA%\rapoo-tray\updates`。
- 设置中心与托盘通过仅本机命名管道通信，不开放网络监听端口。
- 软件不包含遥测或使用统计；网络访问只用于检查 GitHub Release、读取校验文件和下载更新。
- 安装版、便携版都保留本地配置；卸载不会自动删除用户自定义快照。

## ⚠️ 已知限制

- 当前后端仍以单个活动鼠标/接收器为核心，多设备隔离尚未完成。
- 回报率验证测量 Windows Raw Input 到达时间，不等同于 USB/HID 抓包。
- 未打标雷柏二代鼠标可使用通用兼容模式，但高级能力仍需要逐步补充型号数据。
- 发布包尚未进行受信任代码签名，SmartScreen 信誉有限。
- 不提供固件更新、宏录制或按键重映射。
- 从上游或其他改版覆盖安装前，请先导出配置并保留旧安装包，当前不保证自动回滚或跨分支配置迁移。

## 📸 实际界面

<a href="docs/screenshots/settings-wpf-dark.png"><img src="docs/screenshots/settings-wpf-dark.png" width="49%" alt="深色配置控制台" /></a>
<a href="docs/screenshots/settings-wpf-fluent.png"><img src="docs/screenshots/settings-wpf-fluent.png" width="49%" alt="浅色配置控制台" /></a>

<a href="docs/screenshots/osd.png"><img src="docs/screenshots/osd.png" width="49%" alt="状态 OSD" /></a>
<a href="docs/screenshots/quick-panel.png"><img src="docs/screenshots/quick-panel.png" width="49%" alt="OSD 快速面板" /></a>

<a href="docs/screenshots/settings-diagnostics.png"><img src="docs/screenshots/settings-diagnostics.png" width="49%" alt="回报率与设备诊断" /></a>
<a href="docs/screenshots/settings-about.png"><img src="docs/screenshots/settings-about.png" width="49%" alt="设置快照与版本信息" /></a>

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

## 📥 安装方式与卸载

请前往 [GitHub Releases](https://github.com/Rimliya/rapoo-tray/releases) 下载最新发行版：

- **便携版 (`rapoo-tray-portable.zip`)**：解压后保持目录内文件结构不变，双击 `rapoo-tray.exe` 即可运行。
- **一键安装向导 (`rapoo-tray-setup.exe`)**：内置主程序、WPF 设置中心与依赖，自动部署至本地目录，创建快捷方式并支持控制面板卸载；实际大小以 Release 资产为准。
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

## 🧭 开发文档

- [开发路线与未完成项](docs/roadmap.md)
- [Windows 代码签名配置](docs/signing.md)

---

## 📄 开源许可证 (License)

本项目基于 [MIT License](LICENSE) 开源。
