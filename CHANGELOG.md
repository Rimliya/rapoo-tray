# Changelog

## [v1.6.4] - 2026-10-26

### 新增

- 配置档案支持同时保存并切换回报率、DPI X/Y 与休眠时间。
- 设置页新增独立 DPI 参数区域，可读取鼠标当前 DPI，并直接打开完整 DPI 档位编辑器。
- 托盘配置快速切换菜单现在同时显示回报率和 DPI。

### 优化

- 重新整理设置页布局，DPI 设置不再与高级选项挤在同一行。
- 档案列表新增 DPI 列，旧档案会自动显示当前鼠标 DPI，并在下次保存时写入档案。
- 通过托盘切换 DPI 时，会同步更新当前配置档案。

### 修复

- 修复新版 GitHub Actions/MinGW 构建被卡巴斯基误报为 `VHO:HackTool.Win32.Convagent.gen` 的问题；正式构建改用已验证不会触发误报的 Zig C++ 工具链。
- 修复 Zig 构建误用控制台子系统、启动时额外打开命令行窗口的问题；主程序和安装器现在显式使用 Windows GUI 子系统。

### 校验

以下为 `v1.6.4` 实际发布资源的 SHA-256，全部已通过卡巴斯基 21.27 本地扫描：

- `rapoo-tray.exe`：`F5D404AE01B51F038C092D83A79F8958E02696D6CC830474E09EAB15740C1E81`
- `rapoo-tray-setup.exe`：`ABBC34B5837B6AC0F6844BF99C64DB25DE69891784DD4EE71EFD60E130584FD0`
- `rapoo-plugin.dll`：`D55AFC900669D344F4BB23CE9E5FDE3A94D9975F431D3F7A0213C17ADA276270`

**完整变更**：https://github.com/Rimliya/rapoo-tray/compare/v1.6.3...v1.6.4

