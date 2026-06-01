# CodexSwitcher

CodexSwitcher 是一个 Windows 桌面端 Codex 账号切换工具，使用 C++ 和 Qt Widgets 编写。运行数据保存在 exe 同级目录，方便在多个已导入账号之间切换本地或远端的 `~/.codex/auth.json` 和 `~/.codex/installation_id`。

## 功能

- 导入包含 Codex/OpenAI token 凭证的账号 JSON。
- 可将当前本机 `~/.codex/auth.json` 导入到 `data/accounts/<编码后的账号名>/auth.json`，并为该账号生成新的 UUID v4 installation id。
- 当前配置导入的账号在切换时会直接使用保存下来的原始 `auth.json`；手动文本导入的账号才会根据 credentials 构建 auth JSON。
- 导入账号后会对实际新增账号查询一次额度。
- 点击单个账号卡片下方的“查询”只查询该账号；“全部查询”按钮才会查询全部账号。
- 查询账号额度，并直接显示在对应账号卡片上。
- 可在设置中配置配额提醒阈值；成功查询后如 5h 或周限额剩余比例低于阈值，会弹窗提醒。
- 查询失败也会记录为一次已查询结果，卡片会保留失败时间和错误原因；如已有成功额度信息，会继续保留额度条用于参考。
- 查询失败若为 HTTP 401，会停止该账号的后台自动查询；仍可手动点击该账号卡片重新查询。
- 将查询缓存保存到 exe 同级的 `data/usage_cache.json`。
- 支持按配置间隔在后台自动重新查询已有查询记录的账号，默认间隔为 10 分钟；自动查询会按账号轮询节流，不会在打开窗口时一次性全量触发。
- 如果软件关闭期间错过了自动查询时间，重新打开后会滚动到下一个未来周期再查，不会立即补查；每次自动查询时间会加入 1-8 秒抖动。
- 最近切换成功的当前账号会使用单独的自动查询间隔，默认 1 分钟，可在设置中调整。
- 当前使用中的账号会固定排在最前面；自动排序后的账号顺序会写回 `accounts.json`，下次启动沿用上次关闭前的顺序。
- 每个账号生成并绑定一个标准小写 UUID v4 `installation_id`。
- 账号卡片使用一个“切换”入口：未启用云端时只执行本地切换；启用云端时会同时执行本地和云端切换，并用一个结果弹窗汇总。
- 本地切换时会备份并替换本机 Codex auth 文件和 installation id，然后重启 Codex。Windows 端会使用开始菜单中的 Codex 应用入口重启，因为 CLI daemon 生命周期命令只支持 Unix。
- 云端切换默认不启用；启用后通过 SSH/SCP 备份并替换远端 auth 文件和 installation id。
- 最小化后可驻留 Windows 系统托盘。

## 运行数据

打包运行时，数据保存在 exe 同级目录：

- `data/accounts.json`
- `data/accounts/<编码后的账号名>/installation_id`
- `data/accounts/<编码后的账号名>/auth.json`，用于从当前本机 Codex 配置导入的账号
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
- `installation_id` 只使用标准 UUID v4 生成，没有 salt、硬件绑定或特殊编码。
- 云端切换不会自动创建 `~/.codex`；远端必须已经存在 `~/.codex/auth.json`，工具才会先备份再替换。
