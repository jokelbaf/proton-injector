#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

APPID="${APPID:-0}"
STEAM_PATH="${STEAM_COMPAT_CLIENT_INSTALL_PATH:-$HOME/.local/share/Steam}"

if [ ! -d "$STEAM_PATH" ] && [ -d "$HOME/.var/app/com.valvesoftware.Steam/data/Steam" ]; then
    STEAM_PATH="$HOME/.var/app/com.valvesoftware.Steam/data/Steam"
fi

if [ -n "${STEAM_COMPAT_DATA_PATH:-}" ]; then
    COMPAT_DATA="$STEAM_COMPAT_DATA_PATH"
else
    LIB_PATHS=()
    LIB_PATHS+=("$STEAM_PATH")

    LIB_VDF="$STEAM_PATH/steamapps/libraryfolders.vdf"
    if [ -f "$LIB_VDF" ]; then
        while IFS= read -r line; do
            if [[ $line =~ \"path\"[[:space:]]+\"([^\"]+)\" ]]; then
                LIB_PATHS+=("${BASH_REMATCH[1]}")
            fi
        done < "$LIB_VDF"
    fi

    for lib in "${LIB_PATHS[@]}"; do
        CANDIDATE="$lib/steamapps/compatdata/$APPID"
        if [ -d "$CANDIDATE" ]; then
            COMPAT_DATA="$CANDIDATE"
            break
        fi
    done

    COMPAT_DATA="${COMPAT_DATA:-$STEAM_PATH/steamapps/compatdata/$APPID}"
fi

export STEAM_COMPAT_CLIENT_INSTALL_PATH="$STEAM_PATH"
export STEAM_COMPAT_DATA_PATH="$COMPAT_DATA"
export PROTON_LOG=1

STEAM_LAUNCHER="$STEAM_PATH/ubuntu12_32/steam-launch-wrapper"
STEAM_REAPER="$STEAM_PATH/ubuntu12_32/reaper"

if [ "${APPID}" != "0" ] && [ -n "${APPID}" ]; then
    export SteamAppId="${APPID}"
    export SteamGameId="${APPID}"
fi

if [ $# -lt 2 ]; then
    echo "Usage: $0 <target.exe> <dll.dll> [additional args...]"
    echo ""
    echo "Environment variables:"
    echo "  APPID                                - Steam App ID (default: 0)"
    echo "  STEAM_COMPAT_CLIENT_INSTALL_PATH     - Steam installation path"
    echo "  STEAM_COMPAT_DATA_PATH               - Proton compatdata path"
    echo "  PROTON_PATH                          - Path to Proton (e.g., proton-ge)"
    exit 1
fi

TARGET_EXE="$1"
DLL_PATH="$2"
shift 2

if file "$TARGET_EXE" | grep -q "PE32+"; then
    INJECTOR_EXE="$PROJECT_ROOT/bin/injector64.exe"
    TARGET_ARCH="x64"
else
    INJECTOR_EXE="$PROJECT_ROOT/bin/injector32.exe"
    TARGET_ARCH="x86"
fi

LOG_FILE="$PROJECT_ROOT/injector.log"

INJECTION_METHOD="standard"
PREV=""
for arg in "$@"; do
    if [[ "$PREV" == "--method" ]]; then
        INJECTION_METHOD="$arg"
        break
    fi
    PREV="$arg"
done

case "$INJECTION_METHOD" in
    apc)  METHOD_DISPLAY="APC (QueueUserAPC)" ;;
    nt)   METHOD_DISPLAY="NT (NtCreateThreadEx)" ;;
    hook) METHOD_DISPLAY="Hook (SetWindowsHookExA)" ;;
    *)    METHOD_DISPLAY="Standard (CreateRemoteThread)"; INJECTION_METHOD="standard" ;;
esac

if [ ! -f "$INJECTOR_EXE" ]; then
    echo "Error: $INJECTOR_EXE not found. Build the project first with 'make'"
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

to_windows_path() {
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
echo "  Arch:       $TARGET_ARCH"
echo "  Method:     $METHOD_DISPLAY"
echo ""
echo "  Target:     $TARGET_EXE"
echo "  DLL:        $DLL_PATH"
echo "  Log:        $LOG_FILE"
echo ""
echo "───────────────────────────────────────────────────────────────────────────"
echo ""

PROTON_CMD=("$PROTON" waitforexitandrun "$WIN_INJECTOR" "$WIN_TARGET" "$WIN_DLL" --log-file "$WIN_LOG" "$@")

if [ "${APPID}" != "0" ] && [ -n "${APPID}" ] && [ -x "$STEAM_LAUNCHER" ] && [ -x "$STEAM_REAPER" ]; then
    "$STEAM_LAUNCHER" -- "$STEAM_REAPER" SteamLaunch AppId="$APPID" -- "${PROTON_CMD[@]}"
else
    "${PROTON_CMD[@]}"
fi

echo ""
echo "Injection completed. Check $LOG_FILE for details."
