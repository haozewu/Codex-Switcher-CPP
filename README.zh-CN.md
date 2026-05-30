# CodexSwitcher

CodexSwitcher 是一个 Windows 桌面端 Codex 账号切换工具，使用 C++ 和 Qt Widgets 编写。它把运行数据保存在 exe 同级目录，方便在多个已导入账号之间切换本地或远端的 `~/.codex/auth.json`。

英文文档见 [README.md](README.md)。

## 功能

- 导入包含 Codex/OpenAI token 凭证的账号 JSON。
- 查询账号额度，并直接显示在对应账号卡片上。
- 将查询缓存保存到 exe 同级的 `data/usage_cache.json`。
- 支持按配置间隔自动重新查询，默认间隔为 10 分钟。
- 本地切换时会备份并替换本机 Codex auth 文件。
- 云端切换通过 SSH/SCP 备份并替换远端 auth 文件。
- 最小化后可驻留 Windows 系统托盘。

## 运行数据

打包运行时，数据保存在 exe 同级目录：

- `data/accounts.json`
- `data/settings.json`
- `data/usage_cache.json`

这些文件可能包含敏感凭证或账号状态，请不要提交或分享。

## 构建

安装 Qt 和 C++ 编译工具链后执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Release
```

Release 输出为：

```text
build-release\release\CodexSwitcher.exe
```

## 注意

- 当前项目名为 `CodexSwitcher`。
- `api.openai.com` 等接口域名，以及 Windows 包标识 `OpenAI.Codex_*` 属于外部真实名称，不应随项目改名而替换。
- 云端切换不会自动创建 `~/.codex`；远端必须已经存在 `~/.codex/auth.json`，工具才会先备份再替换。
