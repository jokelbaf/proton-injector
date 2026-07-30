#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

if [ $# -lt 2 ]; then
    echo "Usage: $0 <target.exe> <dll.dll> [dll2.dll ...] [injector args...] [-- target args...]"
    echo ""
    echo "Environment variables:"
    echo "  PROTONPATH           - Path to Proton directory (required)"
    echo "  WINEPREFIX           - Wine prefix path (default: ~/.proton-injector/pfx)"
    echo "  GAMEID               - Game ID for umu-run (default: 0)"
    echo "  SLEEP                - Delay before injection in ms (default: 0)"
    echo "  FOLLOW_SLEEP         - Delay before injecting into followed child process in ms (default: 0)"
    echo "  FOLLOW_PROCESS       - Inject into child process when parent exits with 0 (default: false)"
    echo "  FOLLOW_PROCESS_NAME  - Preferred child process executable name (optional)"
    echo "  NO_PARENT            - Skip parent injection; inject into child process instead (default: false)"
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
shift

DLL_PATHS=()
INJECTOR_ARGS=()
COLLECTING_DLLS=1
for arg in "$@"; do
    if [ "$COLLECTING_DLLS" -eq 1 ]; then
        case "$arg" in
            --)       COLLECTING_DLLS=0; INJECTOR_ARGS+=("$arg") ;;
            --*)      COLLECTING_DLLS=0; INJECTOR_ARGS+=("$arg") ;;
            *)        DLL_PATHS+=("$arg") ;;
        esac
    else
        INJECTOR_ARGS+=("$arg")
    fi
done

if [ ${#DLL_PATHS[@]} -eq 0 ]; then
    echo "Error: No DLL specified"
    exit 1
fi

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
for arg in "${INJECTOR_ARGS[@]}"; do
    if [ "$arg" = "--" ]; then
        break
    fi
    case "$PREV" in
        --method)              INJECTION_METHOD="$arg" ;;
        --follow-process-name) FOLLOW_PROCESS_NAME="$arg" ;;
    esac
    case "$arg" in
        --follow-process) FOLLOW_PROCESS=true ;;
        --no-parent)      NO_PARENT=true ;;
    esac
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

for dll in "${DLL_PATHS[@]}"; do
    if [ ! -f "$dll" ]; then
        echo "Error: DLL not found: $dll"
        exit 1
    fi
done

export PROTONPATH
export WINEPREFIX="${WINEPREFIX:-$HOME/.proton-injector/pfx}"
export GAMEID="${GAMEID:-0}"

to_windows_path() {
    echo "Z:$(realpath "$1" | sed 's/\//\\/g')"
}

WIN_TARGET=$(to_windows_path "$TARGET_EXE")
WIN_DLLS=()
for dll in "${DLL_PATHS[@]}"; do
    WIN_DLLS+=("$(to_windows_path "$dll")")
done
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
echo "  Follow sleep: ${FOLLOW_SLEEP:-0} ms"
echo "  No-parent:  ${NO_PARENT:-false}"
echo "  Follow:     ${FOLLOW_PROCESS:-false}"
if [ "${FOLLOW_PROCESS:-false}" = "true" ] && [ -n "${FOLLOW_PROCESS_NAME:-}" ]; then
    echo "  Follow name: $FOLLOW_PROCESS_NAME"
fi
echo ""
echo "  Target:     $TARGET_EXE"
for i in "${!DLL_PATHS[@]}"; do
    echo "  DLL $((i+1))/${#DLL_PATHS[@]}:     ${DLL_PATHS[$i]}"
done
echo "  Log:        $LOG_FILE"
echo ""
echo "───────────────────────────────────────────────────────────────────────────"
echo ""

SLEEP_ARGS=()
if [ -n "${SLEEP:-}" ] && [ "$SLEEP" != "0" ]; then
    SLEEP_ARGS=(--sleep "$SLEEP")
fi

FOLLOW_SLEEP_ARGS=()
if [ -n "${FOLLOW_SLEEP:-}" ] && [ "$FOLLOW_SLEEP" != "0" ]; then
    FOLLOW_SLEEP_ARGS=(--follow-sleep "$FOLLOW_SLEEP")
fi

NO_PARENT_ARGS=()
if [ "${NO_PARENT:-false}" = "true" ]; then
    NO_PARENT_ARGS=(--no-parent)
fi

FOLLOW_ARGS=()
if [ "${FOLLOW_PROCESS:-false}" = "true" ]; then
    FOLLOW_ARGS=(--follow-process)
    if [ -n "${FOLLOW_PROCESS_NAME:-}" ]; then
        FOLLOW_ARGS+=(--follow-process-name "$FOLLOW_PROCESS_NAME")
    fi
fi

umu-run \
    "$INJECTOR_EXE" \
    "$WIN_TARGET" \
    "${WIN_DLLS[@]}" \
    --log-file "$WIN_LOG" \
    "${SLEEP_ARGS[@]}" \
    "${FOLLOW_SLEEP_ARGS[@]}" \
    "${NO_PARENT_ARGS[@]}" \
    "${FOLLOW_ARGS[@]}" \
    "${INJECTOR_ARGS[@]}"

echo ""
echo "Injection completed. Check $LOG_FILE for details."
