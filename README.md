# Proton DLL Injector

[![Build](https://github.com/jokelbaf/proton-injector/actions/workflows/build.yml/badge.svg)](https://github.com/jokelbaf/proton-injector/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A DLL injector for Windows executables running under Proton/Wine on Linux. Supports both 32-bit and 64-bit targets, multiple injection methods, and Steam runtime integration for games that use the Steam API.

## Download

[Latest automated build](https://nightly.link/jokelbaf/proton-injector/workflows/build/master/proton-injector.zip)

## Features

- 32-bit and 64-bit injection
- Five injection methods: Standard, APC, NT, Hook, Manual Map
- Multiple DLL injection (inject several DLLs in sequence)
- Child process injection: follow launcher processes or skip parent entirely
- Multi-step injection with automatic fallbacks (PEB walk, VM query, toolhelp)
- Steam runtime integration (reaper/launch wrapper) for Steam API authentication
- umu-run support for non-Steam games
- Zenity-based GUI for both Steam and non-Steam workflows
- Detailed logging

## Injection Methods

| Method | Function | Stealth | Compatibility |
|--------|----------|---------|---------------|
| `standard` | CreateRemoteThread | Low | Highest |
| `apc` | QueueUserAPC | Medium | High |
| `nt` | NtCreateThreadEx | High | High |
| `hook` | SetWindowsHookExA | High | Medium |
| `manual_map` | Manual PE mapping | Highest | Medium |

`standard`, `apc`, `nt`, and `hook` all call `LoadLibraryA` from `kernel32.dll`. The address is resolved in the target process via three stages: PEB walk, virtual memory query (`NtQueryVirtualMemory`), and toolhelp snapshot, falling back automatically if earlier stages fail.

`manual_map` bypasses `LoadLibraryA` entirely. It reads the DLL from disk, maps it into the target process, resolves imports, applies relocations, registers exception handlers (x64), and calls `DllMain` directly via shellcode injected into the target.

## Building

Requires MinGW-w64 cross-compilers and `make`.

```bash
# Ubuntu/Debian
sudo apt install mingw-w64 make

# Arch Linux
sudo pacman -S mingw-w64-gcc make
```

```bash
make
```

Produces `bin/injector32.exe` and `bin/injector64.exe`. The correct binary is selected automatically by the helper scripts based on the target executable.

## Usage

### GUI

The GUI requires [Zenity](https://gitlab.gnome.org/GNOME/zenity). It auto-detects Steam libraries, installed games, and available Proton versions.

```bash
./scripts/gui.sh
```

Select "Steam Game" for games with Steam API, or "Non-steam Game" for anything else (requires [umu-run](https://github.com/Open-Wine-Components/umu-launcher)).

### Steam Games (`inject.sh`)

For games installed through Steam. Handles compatdata detection across multiple Steam library folders, sets up Steam environment variables, and wraps the launch with Steam's `reaper` for proper API authentication.

```bash
APPID=945360 ./scripts/inject.sh \
    "$HOME/.local/share/Steam/steamapps/common/Among Us/Among Us.exe" \
    /path/to/your.dll

# With multiple DLLs (injected in order)
APPID=945360 ./scripts/inject.sh \
    /path/to/game.exe /path/to/mod1.dll /path/to/mod2.dll

# With custom Proton path and injection method
APPID=945360 PROTON_PATH=/path/to/GE-Proton/proton ./scripts/inject.sh \
    /path/to/game.exe /path/to/mod.dll --method apc

# With target launch options
APPID=945360 ./scripts/inject.sh \
    /path/to/game.exe /path/to/mod.dll -- --your -target=options
```

| Variable | Description | Default |
|----------|-------------|---------|
| `APPID` | Steam App ID | `0` |
| `PROTON_PATH` | Path to Proton executable | `proton-ge` |
| `STEAM_COMPAT_CLIENT_INSTALL_PATH` | Steam installation path | Auto-detected |
| `STEAM_COMPAT_DATA_PATH` | Proton compatdata path | Auto-detected from `APPID` |
| `SLEEP` | Delay before injection in ms | `0` |
| `FOLLOW_SLEEP` | Delay before injecting into the followed child process in ms | `0` |
| `FOLLOW_PROCESS` | Inject into child process when parent exits with `0` | `false` |
| `FOLLOW_PROCESS_NAME` | Preferred child process executable name | — |
| `NO_PARENT` | Skip parent injection; inject into child process instead | `false` |

### Non-Steam Games (`umu.sh`)

For games outside of Steam, [umu-run](https://github.com/Open-Wine-Components/umu-launcher) is used to manage the Proton runtime.

```bash
PROTONPATH=/path/to/GE-Proton ./scripts/umu.sh \
    /path/to/game.exe /path/to/mod.dll

# With multiple DLLs
PROTONPATH=/path/to/GE-Proton ./scripts/umu.sh \
    /path/to/game.exe /path/to/mod1.dll /path/to/mod2.dll

# With custom Wine prefix
PROTONPATH=/path/to/GE-Proton WINEPREFIX=~/.my-prefix ./scripts/umu.sh \
    /path/to/game.exe /path/to/mod.dll --method nt

# With target launch options
PROTONPATH=/path/to/GE-Proton ./scripts/umu.sh \
    /path/to/game.exe /path/to/mod.dll -- --your -target=options
```

| Variable | Description | Default |
|----------|-------------|---------|
| `PROTONPATH` | Path to Proton directory (required) | — |
| `WINEPREFIX` | Wine prefix path | `~/.proton-injector/pfx` |
| `GAMEID` | Game ID for umu-run | `0` |
| `SLEEP` | Delay before injection in ms | `0` |
| `FOLLOW_SLEEP` | Delay before injecting into the followed child process in ms | `0` |
| `FOLLOW_PROCESS` | Inject into child process when parent exits with `0` | `false` |
| `FOLLOW_PROCESS_NAME` | Preferred child process executable name | — |
| `NO_PARENT` | Skip parent injection; inject into child process instead | `false` |

### Direct Usage

```bash
# Inside a Proton/Wine environment
injector64.exe "Z:\path\to\game.exe" "Z:\path\to\mod.dll" --method apc --log-file "Z:\path\to\injector.log"

# With multiple DLLs (injected in order)
injector64.exe "Z:\path\to\game.exe" "Z:\path\to\mod1.dll" "Z:\path\to\mod2.dll" --method apc

# With target launch options
injector64.exe "Z:\path\to\game.exe" "Z:\path\to\mod.dll" -- --your -target=options
```

Options:
- `--method <type>` — Injection method: `standard`, `apc`, `nt`, `hook`, `manual_map` (default: `standard`)
- `--log-file <path>` — Log file path (Windows-style `Z:` paths when under Proton)
- `--sleep <ms>` — Delay in milliseconds before injection (default: `0`)
- `--follow-sleep <ms>` — Delay in milliseconds before injecting into the followed child process (default: `0`)
- `--follow-process` — After the target exits with code `0`, inject into the best-matching child process
- `--follow-process-name <name>` — Preferred child process executable name when using `--follow-process`
- `--no-parent` — Skip injecting into the target; inject into its child process instead
- `--` — End of injector options; pass remaining args to the target executable

Multiple DLLs can be specified by listing them consecutively: `injector64.exe game.exe dll1.dll dll2.dll dll3.dll`

## How It Works

1. Creates the target process in a suspended state, then resumes it
2. Polls the target's PEB until `kernel32.dll` is loaded
3. Resolves `LoadLibraryA` in the remote `kernel32.dll` via multi-step module lookup (PEB walk > VM query > toolhelp snapshot)
4. Writes the DLL path into the target process's memory
5. Executes injection using the selected method
6. Waits for the process to exit

For `manual_map`, steps 3–5 are replaced by a full manual PE mapping routine: the DLL is read from disk, mapped into the target, imports resolved, relocations applied, exception handlers registered, and `DllMain` called via injected shellcode.

If `--follow-process` is set and the parent exits with code `0` (or `--no-parent` is used), the injector scans for child processes and selects the best candidate by name match, start time proximity to parent exit, and shared path components. Injection is then performed on that child.

## Acknowledgements

Special thanks to the following people for their contributions:
- [CrayzyEyezz](https://github.com/CrayzyEyezz) for the GUI design and help with testing.

## License

MIT License - see [LICENSE](LICENSE) for details.

## Disclaimer

This tool is intended for educational purposes and legitimate modding. Use responsibly and only with software you have the right to modify.
