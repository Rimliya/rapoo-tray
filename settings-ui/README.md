# rapoo-tray Settings UI

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

## 字体

设置中心内置华为 HarmonyOS Sans SC 的 Regular、Medium 与 Bold 字重，不要求用户系统安装该字体。字体文件位于 `Assets/Fonts/`，许可文本同时嵌入程序资源并在“关于”页提供查看入口。

输入控件统一使用 40px 高度和 `VerticalContentAlignment=Center`，避免 WPF UI 默认顶部对齐导致文字偏上。

## 主题

首次启动跟随系统主题。右上角可切换浅色/深色，选择保存在 `HKCU\Software\rapoo-tray\Settings\UiTheme`（1=浅色，2=深色）。
