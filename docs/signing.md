# Windows 代码签名

## 当前状态

签名工具链、时间戳和验证脚本已经实现，但 **v1.16.1 正式发布策略选择不进行代码签名**。

因此：

- `v1.16.1` 发布包只提供 SHA256 校验文件。
- GitHub Actions 不再强制要求证书 secrets，缺少证书时仍能发布未签名资产。
- Windows SmartScreen 可能提示“未知发布者”。
- 自签名证书只用于本机测试，不能当作正式分发签名使用。

## 生产证书要求

建议使用受信任的代码签名证书：

- OV / EV Code Signing Certificate；或
- Azure Trusted Signing 等受信任签名服务（当前工作流使用 PFX 方案）。
- 证书必须具备 Code Signing EKU 和私钥导出能力。
- 建议使用 SHA256 文件摘要，并配置 RFC3161 时间戳。

## GitHub Actions secrets

在 `Rimliya/rapoo-tray` 仓库添加：

- `WINDOWS_CERTIFICATE_BASE64`
- `WINDOWS_CERTIFICATE_PASSWORD`

生成 Base64（在可信且离线的环境执行）：

```powershell
[Convert]::ToBase64String(
  [IO.File]::ReadAllBytes("C:\secure\rapoo-tray-code-signing.pfx")
) | Set-Clipboard
```

不要把 PFX、密码或 Base64 内容提交到仓库、Issue、Release 或聊天记录。

## 签名流程

`tools/sign-artifacts.ps1` 可以选择性签名：

1. `bin/rapoo-tray.exe`
2. `bin/rapoo-tray-settings.exe`
3. `bin/rapoo-plugin.dll`
4. `dist/rapoo-tray-setup.exe`

脚本使用 `Get-AuthenticodeSignature` 验证状态必须为 `Valid`。

**v1.16.1 的正式工作流不调用该脚本。** 工作流仍然生成：

- `dist/rapoo-tray-setup.exe.sha256`
- `dist/rapoo-tray-portable.zip.sha256`

## 本地测试签名

本机存在测试证书时，可以只对测试副本签名：

```powershell
Copy-Item bin\rapoo-tray.exe .tools\sign-test.exe -Force
.\tools\sign-artifacts.ps1 `
  -Paths .tools\sign-test.exe `
  -CertificateThumbprint <THUMBPRINT> `
  -MachineStore
```

测试签名仅适用于本机验证：

- 证书必须被当前系统信任；
- 不要把这个签名结果描述为“受信任发布”；
- 其他用户仍可能看到未知发布者提示。

## 应用内更新校验

设置中心更新器会：

1. 通过 GitHub API 获取最新正式 Release；
2. 下载 `rapoo-tray-setup.exe` 与对应的 `.sha256`；
3. 强制校验 SHA256；
4. 如果文件带 Authenticode 签名，则额外调用 `WinVerifyTrust` 验证发布者；
5. SHA256 校验通过后允许启动安装器，未签名包会显示“仅 SHA256 校验”。

如果 Release 缺少 `.sha256` 或哈希不匹配，自动安装仍会被拒绝。