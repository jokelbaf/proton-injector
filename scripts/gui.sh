#!/bin/bash

BASE_DIR="$(cd "$(dirname "$0")" && pwd)"
INJECT_SCRIPT="$BASE_DIR/inject.sh"
UMU_SCRIPT="$BASE_DIR/umu.sh"

command -v zenity >/dev/null || {
    echo "Zenity is required to use the GUI."
    exit 1
}

detect_steam() {
    if [ -d "$HOME/.var/app/com.valvesoftware.Steam/data/Steam" ]; then
        STEAM_TYPE="flatpak"
        STEAM_ROOT="$HOME/.var/app/com.valvesoftware.Steam/data/Steam"
    elif [ -d "$HOME/.steam/steam" ]; then
        STEAM_TYPE="system"
        STEAM_ROOT="$HOME/.steam/steam"
    else
        zenity --error --text="Steam not found!"
        exit 1
    fi
}

get_libraries() {
    declare -A SEEN
    LIBRARIES=()

    VDF="$STEAM_ROOT/steamapps/libraryfolders.vdf"

    if [ -f "$VDF" ]; then
        while IFS= read -r line; do
            if [[ $line =~ \"path\"[[:space:]]+\"([^\"]+)\" ]]; then
                path="${BASH_REMATCH[1]}"
                SEEN["$path"]=1
            fi
        done < "$VDF"
    fi

    SEEN["$STEAM_ROOT"]=1

    for p in "${!SEEN[@]}"; do
        LIBRARIES+=("$p")
    done
}

select_mode() {
    MODE=$(zenity --list --title="Proton DLL Injector" \
        --column="Mode" \
        "Steam Game" \
        "Non-steam Game (requires umu-run)" \
        --width=400 --height=250)
    [ -z "$MODE" ] && exit 0
}

select_appid() {
    declare -A SEEN_APPID
    GAME_LIST=()

    for lib in "${LIBRARIES[@]}"; do
        for manifest in "$lib"/steamapps/appmanifest_*.acf; do
            [ -f "$manifest" ] || continue
            appid="${manifest##*/appmanifest_}"
            appid="${appid%.acf}"
            [ -n "${SEEN_APPID[$appid]:-}" ] && continue
            SEEN_APPID["$appid"]=1
            name=$(grep -m1 '"name"' "$manifest" | sed -E 's/.*"name"[[:space:]]*"([^"]+)".*/\1/')
            GAME_LIST+=("$appid" "$name")
        done
    done

    APPID=$(zenity --list \
        --title="Select Game" \
        --column="APPID" \
        --column="Game Name" \
        "${GAME_LIST[@]}" \
        --width=600 --height=400 \
        --print-column=1)

    [ -z "$APPID" ] && exit 0
}

select_proton() {
    declare -A SEEN
    PROTON_LIST=()

    for lib in "${LIBRARIES[@]}"; do
        for d in "$lib"/steamapps/common/*Proton*; do
            [ -f "$d/proton" ] && SEEN["$d"]=1
        done
    done

    for d in \
        "$HOME/.steam/root/compatibilitytools.d/"* \
        "$HOME/.var/app/com.valvesoftware.Steam/data/Steam/compatibilitytools.d/"*
    do
        [ -f "$d/proton" ] && SEEN["$d"]=1
    done

    for d in "${!SEEN[@]}"; do
        PROTON_LIST+=("$(basename "$d")" "$d/proton")
    done

    PROTON_PATH=$(zenity --list \
        --title="Select Proton Version" \
        --column="Version" \
        --column="Executable" \
        "${PROTON_LIST[@]}" \
        --width=700 --height=400 \
        --print-column=2)

    [ -z "$PROTON_PATH" ] && exit 0
}

select_exe() {
    EXE_PATH=$(zenity --file-selection --title="Select Game EXE" --file-filter="*.exe")
    [ -z "$EXE_PATH" ] && exit 0
}

select_dll() {
    DLL_PATH=$(zenity --file-selection --title="Select DLL" --file-filter="*.dll")
    [ -z "$DLL_PATH" ] && exit 0
}

select_method() {
    METHOD=$(zenity --list --title="Select Injection Method" --column="Method" standard nt apc hook)
    [ -z "$METHOD" ] && exit 0
}

select_wineprefix() {
    local default_prefix="$HOME/.proton-injector/pfx"
    mkdir -p "$default_prefix"
    WINEPREFIX=$(zenity --file-selection --directory \
        --title="Select Wine Prefix" \
        --filename="$default_prefix/")
    [ -z "$WINEPREFIX" ] && exit 0
}

run_steam() {
    APPID="$APPID" \
    PROTON_PATH="$PROTON_PATH" \
        "$INJECT_SCRIPT" "$EXE_PATH" "$DLL_PATH" --method "$METHOD"
}

run_nonsteam() {
    PROTONPATH="$(dirname "$PROTON_PATH")" \
    WINEPREFIX="$WINEPREFIX" \
        "$UMU_SCRIPT" "$EXE_PATH" "$DLL_PATH" --method "$METHOD"
}

detect_steam
get_libraries
select_mode

case "$MODE" in
    "Steam Game")
        select_appid
        select_proton
        select_exe
        select_dll
        select_method
        run_steam
        ;;
    "Non-steam Game"*)
        command -v umu-run >/dev/null || {
            zenity --error --text="umu-run is required for non-steam games.\nInstall it first."
            exit 1
        }
        select_proton
        select_wineprefix
        select_exe
        select_dll
        select_method
        run_nonsteam
        ;;
esac
