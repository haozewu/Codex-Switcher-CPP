# CodexSwitcher

CodexSwitcher is a Windows desktop account switcher for Codex. It is built with C++ and Qt Widgets, stores its runtime data next to the executable, and helps switch local or remote `~/.codex/auth.json` credentials between imported accounts.

对于中文用户，看 [README.zh-CN.md](README.zh-CN.md).

## Features

- Import account JSON files that contain Codex/OpenAI token credentials.
- Query account quota status and show it directly on each account card.
- Cache query results under `data/usage_cache.json` next to the executable.
- Automatically re-query accounts after the configured interval. The default is 10 minutes.
- Switch the local Codex auth file with backup protection.
- Switch a remote Codex auth file over SSH/SCP after validating that the remote `~/.codex/auth.json` already exists.
- Minimize to the Windows system tray.

## Runtime Files

When running from a packaged executable, runtime files are stored beside the executable:

- `data/accounts.json`
- `data/settings.json`
- `data/usage_cache.json`

These files may contain sensitive credentials or account status. Do not commit or share them.

## Build

Install Qt with a C++ toolchain, then run:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Release
```

The release executable is generated as:

```text
build-release\release\CodexSwitcher.exe
```

## Notes

- The project name is `CodexSwitcher`.
- OpenAI API endpoint names and the Windows `OpenAI.Codex_*` package identifier are external names and are intentionally not renamed.
- Remote switching does not create `~/.codex`; it requires an existing remote `~/.codex/auth.json` so the tool can back it up before replacement.
