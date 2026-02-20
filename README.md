# Proton DLL Injector

[![Build](https://github.com/jokelbaf/proton-injector/actions/workflows/build.yml/badge.svg)](https://github.com/jokelbaf/proton-injector/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A DLL injector for Windows executables running under Proton/Wine on Linux. Supports both 32-bit and 64-bit targets, multiple injection methods, and Steam runtime integration for games that use the Steam API.

## Download

[Latest automated build](https://nightly.link/jokelbaf/proton-injector/workflows/build/master/proton-injector.zip)

## Features

- 32-bit and 64-bit injection
- Four injection methods: Standard, APC, NT, Hook
- Multi-step injection with automatic fallbacks (PEB walk, VM query, toolhelp)
- Steam runtime integration (reaper/launch wrapper) for Steam API authentication
- umu-run support for non-Steam games
- Zenity-based GUI for both Steam and non-Steam workflows
- Process suspension during injection for reliability
- Detailed logging

## Injection Methods

| Method | Function | Stealth | Compatibility |
|--------|----------|---------|---------------|
| `standard` | CreateRemoteThread | Low | Highest |
| `apc` | QueueUserAPC | Medium | High |
| `nt` | NtCreateThreadEx | High | High |
| `hook` | SetWindowsHookExA | High | Medium |

All methods use `LdrLoadDll` with custom shellcode rather than plain `LoadLibrary` calls. Module resolution in the target process goes through three stages: PEB walk, virtual memory query (`NtQueryVirtualMemory`), and toolhelp snapshot, falling back automatically if earlier methods fail.

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

# With custom Proton path and injection method
APPID=945360 PROTON_PATH=/path/to/GE-Proton/proton ./scripts/inject.sh \
    /path/to/game.exe /path/to/mod.dll --method apc
```

| Variable | Description | Default |
|----------|-------------|---------|
| `APPID` | Steam App ID | `0` |
| `PROTON_PATH` | Path to Proton executable | `proton-ge` |
| `STEAM_COMPAT_CLIENT_INSTALL_PATH` | Steam installation path | Auto-detected |
| `STEAM_COMPAT_DATA_PATH` | Proton compatdata path | Auto-detected from `APPID` |

### Non-Steam Games (`umu.sh`)

For games outside of Steam, [umu-run](https://github.com/Open-Wine-Components/umu-launcher) is used to manage the Proton runtime.

```bash
PROTONPATH=/path/to/GE-Proton ./scripts/umu.sh \
    /path/to/game.exe /path/to/mod.dll

# With custom Wine prefix
PROTONPATH=/path/to/GE-Proton WINEPREFIX=~/.my-prefix ./scripts/umu.sh \
    /path/to/game.exe /path/to/mod.dll --method nt
```

| Variable | Description | Default |
|----------|-------------|---------|
| `PROTONPATH` | Path to Proton directory (required) | - |
| `WINEPREFIX` | Wine prefix path | `~/.proton-injector/pfx` |
| `GAMEID` | Game ID for umu-run | `0` |

### Direct Usage

```bash
# Inside a Proton/Wine environment
injector64.exe "Z:\path\to\game.exe" "Z:\path\to\mod.dll" --method apc --log-file "Z:\path\to\injector.log"
```

Options:
- `--method <type>` -- Injection method: `standard`, `apc`, `nt`, `hook` (default: `standard`)
- `--log-file <path>` -- Log file path (Windows-style Z: paths when under Proton)

## How It Works

1. Creates the target process in a suspended state
2. Waits for `kernel32.dll` to load in the target (PEB polling)
3. Resolves `LdrLoadDll` in the remote process via multi-step module lookup
4. Writes position-independent shellcode + data block to the target
5. Executes injection using the selected method
6. Resumes the process and waits for exit

## Acknowledgements

Special thanks to the following people for their contributions:
- [CrayzyEyezz](https://github.com/CrayzyEyezz) for the GUI design and help with testing.

## License

MIT License - see [LICENSE](LICENSE) for details.

## Disclaimer

This tool is intended for educational purposes and legitimate modding. Use responsibly and only with software you have the right to modify.
