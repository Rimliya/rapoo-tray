# rapoo-tray Settings UI

视觉变体：冷库（ice / cool / tight / grid / standard / calm）
主视觉：真实鼠标状态读数与配置记录列表
页面类型：配置页为控制台，其余设置页为阅读页

设置中心使用原生 WPF 与 WPF UI 4.3，目标框架为 .NET Framework 4.8。它只负责呈现和编辑界面；HID、配置持久化、托盘菜单和任务栏图标继续由 `rapoo-tray.exe` 的 C++ 后端负责。

## 进程结构

- `rapoo-tray.exe`：Win32 托盘、任务栏电池图标、Fluent 亚克力右键菜单、HID 与自动化。
- `rapoo-tray-settings.exe`：WPF UI 设置中心，支持 Mica、系统主题、单实例跳页。
- `\\.\pipe\rapoo-tray-ui-v1`：仅限本机的命名管道，WPF UI 与托盘后端通过它交换状态和执行命令。
- 设置进程不会自己启动第二套 HID 轮询，避免同时访问鼠标造成卡顿。

## 本地预览

```powershell
dotnet run --project settings-ui/RapooTray.Settings.csproj -- --demo
```

`--demo` 使用内置示例数据显示配置、自动化和 DPI 页面；正式启动不带这个参数。

## 构建约定

- `build.bat`：本地测试构建，同时生成 C++ 托盘与 WPF 设置中心。
- `installer/build_installer.bat`：用当前 `bin` 产物生成安装包。
- 正式发版只由推送 `v*` tag 触发的 GitHub Actions `release.yml` 完成。
- Actions 的 Release 步骤使用 `GITHUB_TOKEN`；不要把个人 token 写入源码、脚本或提交记录。

## 配置包

配置页支持导入和导出 `.rapoo` 文件。配置包包含配置、默认配置索引与自动切换开关；格式字段为 `format=rapoo-tray-profiles`、`schemaVersion=1`。

- 导入前自动备份到 `%APPDATA%\rapoo-tray\backups`。
- 文件大小上限为 2 MB，最多 16 个配置，名称、回报率和性能模式会在替换前校验。
- 实际替换由托盘进程一次完成，避免逐条删除导致半写入。

## 字体

设置中心内置华为 HarmonyOS Sans SC 的 Regular、Medium 与 Bold 字重，不要求用户系统安装该字体。字体文件位于 `Assets/Fonts/`，许可文本同时嵌入程序资源并在“关于”页提供查看入口。

输入控件统一使用 40px 高度和 `VerticalContentAlignment=Center`，避免 WPF UI 默认顶部对齐导致文字偏上。

正文使用 Regular 字重和 14px 基准字号，小标签为 12px；窗口使用 `TextFormattingMode=Display`、`TextHintingMode=Fixed` 与 ClearType，避免 Medium 字体叠加伪粗体后出现发糊和彩边。状态 OSD 在分层窗口上使用灰阶 `AntiAliasGridFit`，以得到更稳定的中文边缘。

## 主题

首次启动跟随系统主题。右上角可切换浅色/深色，选择保存在 `HKCU\Software\rapoo-tray\Settings\UiTheme`（1=浅色，2=深色）。

## v1.16.3 应用图标

- 新增简化版应用图标，用于标题栏、侧栏、任务栏和窗口图标。
- `app.ico` 扩展到 10 个尺寸帧，小尺寸不绘制外框，避免任务栏中发糊。
- 关于页继续展示完整八角框 Logo，设置中心其余位置使用简化图标。
- 安装程序、DPI 窗口、`rapoo-tray.exe` 和快捷方式复用同一份 ICO 资源。

## v1.16.2 界面文案补丁

- 统一导航、设置页、托盘菜单、状态 OSD、快速面板和提醒窗口的术语。
- “配置”专指 profile；“状态 OSD”专指瞬态提示；“快速面板”专指可交互 HUD。
- 将“安全写入”“设置已即时生效”“高优先级卡片”等表达改为直接说明结果。
- 状态页按钮按上下文收敛为“预览”“恢复默认”“保存设置”。
- 更新截图与 README，使展示文案和实际界面一致。

签名与发布配置见仓库根目录 `docs/signing.md`，未完成项见 `docs/roadmap.md`。
