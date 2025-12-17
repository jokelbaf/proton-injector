#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

APPID="${APPID:-0}"
STEAM_PATH="${STEAM_COMPAT_CLIENT_INSTALL_PATH:-$HOME/.local/share/Steam}"
COMPAT_DATA="${STEAM_COMPAT_DATA_PATH:-$STEAM_PATH/steamapps/compatdata/$APPID}"

export STEAM_COMPAT_CLIENT_INSTALL_PATH="$STEAM_PATH"
export STEAM_COMPAT_DATA_PATH="$COMPAT_DATA"
export PROTON_LOG=1

if [ $# -lt 2 ]; then
    echo "Usage: $0 <target.exe> <dll.dll> [additional args...]"
    echo ""
    echo "Environment variables:"
    echo "  APPID                                - Steam App ID (default: 0)"
    echo "  STEAM_COMPAT_CLIENT_INSTALL_PATH     - Steam installation path"
    echo "  STEAM_COMPAT_DATA_PATH               - Proton compatdata path"
    echo "  PROTON_PATH                          - Path to Proton (e.g., proton-ge)"
    echo ""
    echo "Example:"
    echo "  APPID=0 PROTON_PATH=proton-ge $0 /path/to/game.exe /path/to/mod.dll"
    exit 1
fi

TARGET_EXE="$1"
DLL_PATH="$2"
shift 2

INJECTOR_EXE="$PROJECT_ROOT/bin/injector.exe"
LOG_FILE="$PROJECT_ROOT/injector.log"

INJECTION_METHOD="standard"
for arg in "$@"; do
    if [[ "$arg" == --method ]]; then
        INJECTION_METHOD="${@:$((OPTIND+1)):1}"
    fi
done

case "$INJECTION_METHOD" in
    apc)
        METHOD_DISPLAY="APC (QueueUserAPC)"
        ;;
    nt)
        METHOD_DISPLAY="NT (NtCreateThreadEx)"
        ;;
    hook)
        METHOD_DISPLAY="Hook (SetWindowsHookExA)"
        ;;
    *)
        METHOD_DISPLAY="Standard (CreateRemoteThread)"
        INJECTION_METHOD="standard"
        ;;
esac

if [ ! -f "$INJECTOR_EXE" ]; then
    echo "Error: injector.exe not found. Build the project first with 'make'"
    exit 1
fi

if [ ! -f "$TARGET_EXE" ]; then
    echo "Error: Target executable not found: $TARGET_EXE"
    exit 1
fi

if [ ! -f "$DLL_PATH" ]; then
    echo "Error: DLL not found: $DLL_PATH"
    exit 1
fi

PROTON="${PROTON_PATH:-proton-ge}"

function to_windows_path() {
    echo "Z:$(realpath "$1" | sed 's/\//\\/g')"
}

WIN_INJECTOR=$(to_windows_path "$INJECTOR_EXE")
WIN_TARGET=$(to_windows_path "$TARGET_EXE")
WIN_DLL=$(to_windows_path "$DLL_PATH")
WIN_LOG=$(to_windows_path "$LOG_FILE")

echo "╔═══════════════════════════════════════════════════════════════════════════╗"
echo "║                          Proton DLL Injector                              ║"
echo "╚═══════════════════════════════════════════════════════════════════════════╝"
echo ""
echo "  Proton:     $PROTON"
echo "  App ID:     $APPID"
echo "  Method:     $METHOD_DISPLAY"
echo ""
echo "  Target:     $TARGET_EXE"
echo "  DLL:        $DLL_PATH"
echo "  Log:        $LOG_FILE"
echo ""
echo "───────────────────────────────────────────────────────────────────────────"
echo ""

"$PROTON" run \
    "$WIN_INJECTOR" \
    "$WIN_TARGET" \
    "$WIN_DLL" \
    --log-file "$WIN_LOG" \
    "$@"

echo ""
echo "Injection completed. Check $LOG_FILE for details."
