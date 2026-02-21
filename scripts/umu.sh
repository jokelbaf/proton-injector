#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

if [ $# -lt 2 ]; then
    echo "Usage: $0 <target.exe> <dll.dll> [additional args...]"
    echo ""
    echo "Environment variables:"
    echo "  PROTONPATH   - Path to Proton directory (required)"
    echo "  WINEPREFIX   - Wine prefix path (default: ~/.proton-injector/pfx)"
    echo "  GAMEID       - Game ID for umu-run (default: 0)"
    echo "  SLEEP        - Delay before injection in ms (default: 0)"
    exit 1
fi

if [ -z "${PROTONPATH:-}" ]; then
    echo "Error: PROTONPATH must be set"
    exit 1
fi

command -v umu-run >/dev/null || {
    echo "Error: umu-run not found"
    exit 1
}

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

export PROTONPATH
export WINEPREFIX="${WINEPREFIX:-$HOME/.proton-injector/pfx}"
export GAMEID="${GAMEID:-0}"

to_windows_path() {
    echo "Z:$(realpath "$1" | sed 's/\//\\/g')"
}

WIN_TARGET=$(to_windows_path "$TARGET_EXE")
WIN_DLL=$(to_windows_path "$DLL_PATH")
WIN_LOG=$(to_windows_path "$LOG_FILE")

echo "╔═══════════════════════════════════════════════════════════════════════════╗"
echo "║                      Proton DLL Injector (umu-run)                        ║"
echo "╚═══════════════════════════════════════════════════════════════════════════╝"
echo ""
echo "  Proton:     $PROTONPATH"
echo "  Prefix:     $WINEPREFIX"
echo "  Arch:       $TARGET_ARCH"
echo "  Method:     $METHOD_DISPLAY"
echo "  Sleep:      ${SLEEP:-0} ms"
echo ""
echo "  Target:     $TARGET_EXE"
echo "  DLL:        $DLL_PATH"
echo "  Log:        $LOG_FILE"
echo ""
echo "───────────────────────────────────────────────────────────────────────────"
echo ""

SLEEP_ARGS=()
if [ -n "${SLEEP:-}" ] && [ "$SLEEP" != "0" ]; then
    SLEEP_ARGS=(--sleep "$SLEEP")
fi

umu-run \
    "$INJECTOR_EXE" \
    "$WIN_TARGET" \
    "$WIN_DLL" \
    --log-file "$WIN_LOG" \
    "${SLEEP_ARGS[@]}" \
    "$@"

echo ""
echo "Injection completed. Check $LOG_FILE for details."
