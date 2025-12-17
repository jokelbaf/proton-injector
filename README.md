# Proton DLL Injector

[![Build](https://github.com/jokelbaf/proton-injector/actions/workflows/build.yml/badge.svg)](https://github.com/jokelbaf/proton-injector/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A DLL injector for Windows executables running under Proton with support for multiple injection methods.

## Download

[Latest automated build](https://nightly.link/jokelbaf/proton-injector/workflows/build/master/proton-injector.zip)

## Features

- Multiple injection methods: Standard, APC, NT, and Hook
- Command-line interface
- Detailed logging support
- Proton compatibility for Linux gaming
- Clean, well-structured C codebase
- Process suspension for reliable injection

## Injection Methods

The injector supports four different injection techniques:

| Method | Description | Stealthiness | Compatibility |
|--------|-------------|--------------|---------------|
| **standard** | CreateRemoteThread + LoadLibraryW | Low | Highest |
| **apc** | QueueUserAPC + LoadLibraryA | Medium | High |
| **nt** | NtCreateThreadEx + LoadLibraryA | High | High |
| **hook** | SetWindowsHookExA (WH_CBT) | High | Medium |

### Method Details

- **Standard**: Traditional injection using `CreateRemoteThread`. Most compatible but easily detected.
- **APC**: Queues an Asynchronous Procedure Call to the main thread. More stealthy than standard.
- **NT**: Uses undocumented `NtCreateThreadEx` from ntdll.dll. More stealthy, lower-level approach.
- **Hook**: Installs a Windows hook in the target process. Stealthiest but may have compatibility issues with some games.

## Building

### Requirements

- `x86_64-w64-mingw32-gcc` (MinGW-w64 cross-compiler)
- `make`

Install on Ubuntu/Debian:
```bash
sudo apt install mingw-w64 make
```

Install on Arch Linux:
```bash
sudo pacman -S mingw-w64-gcc make
```

### Compilation

```bash
make clean && make
```

The compiled executable will be available at `bin/injector.exe`.

To build the example test DLL:
```bash
cd example && make
```

## Usage

### Quick Start with Helper Script

The easiest way to use the injector is with the provided helper script:

```bash
# Basic usage (uses standard injection method by default)
./scripts/inject.sh /path/to/game.exe /path/to/your.dll

# With specific injection method
./scripts/inject.sh /path/to/game.exe /path/to/your.dll --method apc

# With custom App ID
APPID=12345 ./scripts/inject.sh /path/to/game.exe /path/to/your.dll --method nt

# Full example with all options
APPID=0 PROTON_PATH=proton-ge ./scripts/inject.sh \
    "$HOME/.local/share/Steam/steamapps/common/YourGame/game.exe" \
    "$HOME/.local/share/Steam/steamapps/common/YourGame/your_mod.dll" \
    --method apc
```

### Direct Usage

```bash
injector.exe <target.exe> <dll.dll> [options]
```

#### Options

- `--method <type>` - Injection method: `standard`, `apc`, `nt`, or `hook` (default: `standard`)
- `--log-file <path>` - Path to log file for detailed injection logs

### Example with Proton

If you prefer to run manually without the script:

```bash
export APPID=0
export STEAM_COMPAT_CLIENT_INSTALL_PATH="$HOME/.local/share/Steam"
export STEAM_COMPAT_DATA_PATH="$HOME/.local/share/Steam/steamapps/compatdata/$APPID"
export PROTON_LOG=1

proton-ge run \
    "Z:\home\user\proton-injector\bin\injector.exe" \
    "Z:\path\to\game.exe" \
    "Z:\path\to\your.dll" \
    --method apc \
    --log-file "Z:\home\user\proton-injector\injector.log"
```

**Note**: Convert all Linux paths to Windows Z: paths when using Proton directly.

### Environment Variables

- `APPID` - Steam App ID (default: 0)
- `STEAM_COMPAT_CLIENT_INSTALL_PATH` - Steam installation path
- `STEAM_COMPAT_DATA_PATH` - Proton compatdata path
- `PROTON_PATH` - Path to Proton executable (default: `proton-ge`)

## How It Works

1. Creates the target process in suspended state
2. Allocates memory in the target process for the DLL path
3. Writes the DLL path to the allocated memory
4. Executes injection using the selected method:
   - Standard: Creates remote thread calling LoadLibraryW
   - APC: Queues APC to main thread with LoadLibraryA
   - NT: Uses NtCreateThreadEx to create thread with LoadLibraryA
   - Hook: Installs Windows hook to trigger DLL load
5. Waits for DLL to load and verifies success
6. Resumes the main thread
7. Waits for the process to exit

## Logging

When a log file is specified with `--log-file`, the injector writes detailed logs including:

- Timestamps for all operations
- Selected injection method
- Process creation details (PID)
- Memory allocation addresses
- Function addresses (LoadLibrary, NtCreateThreadEx, etc.)
- DLL load address on success
- Error messages with error codes

Example log output:
```
[2025-12-18 10:30:45] INFO: Proton DLL Injector v1.0.0
[2025-12-18 10:30:45] INFO: Using injection method: 1
[2025-12-18 10:30:45] INFO: Process created (PID: 12345)
[2025-12-18 10:30:45] DEBUG: Allocated memory at 0x00007FF8A0000000
[2025-12-18 10:30:45] DEBUG: DLL loaded at 0x00007FF8B0000000
```

## Project Structure

```
proton-injector/
├── src/
│   ├── main.c       - Entry point and CLI parsing
│   ├── inject.c     - DLL injection logic
│   └── logger.c     - Logging functionality
├── include/
│   ├── inject.h     - Injection function declarations
│   └── logger.h     - Logger function declarations
├── example/
│   ├── test_dll.c   - Example DLL source
│   └── test.dll     - Compiled example DLL
├── scripts/
│   └── inject.sh    - Helper script for easy injection
├── bin/             - Compiled executables
├── build/           - Object files
├── Makefile         - Build configuration
└── README.md        - This file
```

## Testing

A simple test DLL is provided in the `example/` directory:

```bash
# Build the test DLL
cd example && make

# Test injection with different methods
./scripts/inject.sh /path/to/notepad.exe ./example/test.dll --method standard
./scripts/inject.sh /path/to/notepad.exe ./example/test.dll --method apc
./scripts/inject.sh /path/to/notepad.exe ./example/test.dll --method nt
./scripts/inject.sh /path/to/notepad.exe ./example/test.dll --method hook
```

The test DLL will display a message box when successfully injected.

## Troubleshooting

**Injection fails with standard method**  
Try `apc` or `nt` method. Some anti-cheat systems detect CreateRemoteThread. Verify that the DLL architecture matches the target executable.

**DLL not loading**  
Check the DLL path and verify all dependencies are available. Enable logging for detailed error messages.

**Process crashes after injection**  
The DLL may have initialization issues. Try different injection methods and check logs.

**Hook method not working**  
Hook injection requires a Windows message loop. Try `apc` or `nt` methods instead.

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Disclaimer

This tool is for educational purposes. Use responsibly and only with software you have the right to modify.
