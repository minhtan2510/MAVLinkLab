#!/usr/bin/env bash
# CLI workbench management tool for MAVLinkLab.
# Supports both interactive menu mode and direct command arguments.

set -eo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${MAVLINK_BUILD_DIR:-$project_dir/build}"
if [[ "$build_dir" != /* ]]; then
    build_dir="$project_dir/$build_dir"
fi
build_core_dir="$project_dir/build-core"

# Colors for terminal output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Retain each menu's highlighted option for the current CLI session.
declare -A menu_selections=()

# Check if terminal supports color
if [[ ! -t 1 ]]; then
    RED=''
    GREEN=''
    BLUE=''
    YELLOW=''
    CYAN=''
    BOLD=''
    NC=''
fi

# Developer mode flag: activated via -dev or --dev argument.
IS_DEV_MODE=0
_filtered_args=()
for _arg in "$@"; do
    if [[ "$_arg" == "-dev" || "$_arg" == "--dev" ]]; then
        IS_DEV_MODE=1
    else
        _filtered_args+=("$_arg")
    fi
done
set -- "${_filtered_args[@]}"
unset _filtered_args _arg

##
# @brief Safely clear terminal screen without failing if TERM is unset.
#
safe_clear()
{
    if [[ -t 1 && -n "${TERM:-}" && "$TERM" != "dumb" ]]; then
        clear 2>/dev/null || true
    fi
}

##
# @brief Configure Qt runtime environment variables based on CMakeCache.txt.
#
setup_qt_env()
{
    local cache="$build_dir/CMakeCache.txt"
    local qt_cmake_dir=""
    if [[ -r "$cache" ]]; then
        qt_cmake_dir="$(sed -n 's/^Qt6Core_DIR:PATH=//p' "$cache")"
    fi

    if [[ -z "$qt_cmake_dir" && -d "$project_dir/.deps/usr/lib/x86_64-linux-gnu" ]]; then
        qt_cmake_dir="$project_dir/.deps/usr/lib/x86_64-linux-gnu/cmake/Qt6Core"
    fi

    if [[ -n "$qt_cmake_dir" ]]; then
        if [[ ! -d "$qt_cmake_dir/../.." ]]; then
            printf "%bConfigured Qt installation is missing. Reconfigure and rebuild with an available Qt installation.%b\n" "$RED" "$NC" >&2
            return 1
        fi
        local qt_lib_dir
        qt_lib_dir="$(cd -- "$qt_cmake_dir/../.." && pwd)"
        if [[ ! -r "$qt_lib_dir/libQt6Core.so.6" ]]; then
            printf "%bQt runtime not found in %s. Reconfigure and rebuild with a complete Qt installation.%b\n" "$RED" "$qt_lib_dir" "$NC" >&2
            return 1
        fi
        export LD_LIBRARY_PATH="$qt_lib_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
        for plugins in "$qt_lib_dir/qt6/plugins" "$qt_lib_dir/../plugins"; do
            if [[ -d "$plugins/platforms" ]]; then
                export QT_PLUGIN_PATH="$plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
                break
            fi
        done
    fi
}

##
# @brief Discover Qt installation prefix if available.
#
# @return Prints path to Qt prefix directory if found.
#
detect_qt_prefix()
{
    if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
        printf '%s' "$CMAKE_PREFIX_PATH"
        return 0
    fi

    if [[ -d "$project_dir/.deps/usr" ]]; then
        printf '%s' "$project_dir/.deps/usr"
        return 0
    fi

    local cache="$build_dir/CMakeCache.txt"
    if [[ -r "$cache" ]]; then
        local cached_prefix
        cached_prefix="$(sed -n 's/^CMAKE_PREFIX_PATH:[^=]*=//p' "$cache")"
        if [[ -n "$cached_prefix" && -d "$cached_prefix" ]]; then
            printf '%s' "$cached_prefix"
            return 0
        fi
    fi

    # Fallback to local Qt installation prefix if present on this machine
    if [[ -d "/tmp/mavlink-qt-prefix/usr" ]]; then
        printf '%s' "/tmp/mavlink-qt-prefix/usr"
        return 0
    fi
}

##
# @brief Install required dependencies from requirement.txt into project .deps directory.
#
# @return 0 on success, non-zero on failure.
#
install_requirements()
{
    local req_file="$project_dir/requirement.txt"
    if [[ ! -f "$req_file" && -f "$project_dir/requirements.txt" ]]; then
        req_file="$project_dir/requirements.txt"
    fi

    if [[ ! -f "$req_file" ]]; then
        return 0
    fi

    local deps_dir="$project_dir/.deps"
    local stamp_file="$deps_dir/.installed_stamp"
    local req_hash
    req_hash="$(sha256sum "$req_file" | awk '{print $1}')"

    if [[ -f "$stamp_file" && -r "$deps_dir/usr/lib/x86_64-linux-gnu/libQt6Core.so.6" ]]; then
        local saved_hash
        saved_hash="$(cat "$stamp_file" 2>/dev/null || true)"
        if [[ "$saved_hash" == "$req_hash" ]]; then
            return 0
        fi
    fi

    printf "\n%b=== Installing Dependencies from %s ===%b\n" "$BLUE" "$(basename "$req_file")" "$NC"
    printf "Target directory: %b%s%b\n" "$CYAN" "$deps_dir" "$NC"

    local packages=()
    while IFS= read -r line || [[ -n "$line" ]]; do
        line="$(echo "$line" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"
        if [[ -n "$line" && ! "$line" =~ ^# ]]; then
            packages+=("$line")
        fi
    done < "$req_file"

    if [[ "${#packages[@]}" -eq 0 ]]; then
        printf "%bNo packages specified in %s.%b\n" "$YELLOW" "$(basename "$req_file")" "$NC"
        return 0
    fi

    local temp_download_dir
    temp_download_dir="$(mktemp -d /tmp/mavlink_req_XXXXXX)"

    cleanup_temp()
    {
        rm -rf "$temp_download_dir"
    }
    trap cleanup_temp RETURN

    printf "%bDownloading %d package(s) via apt-get download...%b\n" "$CYAN" "${#packages[@]}" "$NC"
    if ! (cd "$temp_download_dir" && apt-get download "${packages[@]}"); then
        printf "%bFailed to download requirement packages!%b\n" "$RED" "$NC" >&2
        return 1
    fi

    printf "%bExtracting packages into %s...%b\n" "$CYAN" "$deps_dir" "$NC"
    mkdir -p "$deps_dir"
    local deb_count=0
    for deb in "$temp_download_dir"/*.deb; do
        if [[ -f "$deb" ]]; then
            dpkg-deb -x "$deb" "$deps_dir"
            deb_count=$((deb_count + 1))
        fi
    done

    printf "%s" "$req_hash" > "$stamp_file"
    printf "%bSuccessfully installed %d package(s) into %s!%b\n" "$GREEN" "$deb_count" "$deps_dir" "$NC"
    return 0
}

##
# @brief Update git submodules recursively.
#
# @return 0 on success, non-zero on failure.
#
update_submodules()
{
    printf "\n%b=== Updating Git Submodules ===%b\n" "$BLUE" "$NC"
    if git -C "$project_dir" submodule update --init --recursive; then
        printf "%bGit submodules updated successfully.%b\n" "$GREEN" "$NC"
        return 0
    else
        printf "%bFailed to update git submodules!%b\n" "$RED" "$NC" >&2
        return 1
    fi
}

##
# @brief Build the full project including Qt GUI and test suites.
#
build_full()
{
    printf "\n%b=== Building Full Project (Desktop & Tests) ===%b\n" "$BLUE" "$NC"
    if ! update_submodules; then
        return 1
    fi
    if ! update_enums; then
        return 1
    fi
    if ! install_requirements; then
        return 1
    fi

    local need_config=0
    if [[ ! -f "$build_dir/CMakeCache.txt" || (! -f "$build_dir/Makefile" && ! -f "$build_dir/build.ninja") ]]; then
        need_config=1
    fi

    if [[ "$need_config" -eq 0 && -f "$build_dir/CMakeCache.txt" ]]; then
        if grep -q "Qt6_DIR:PATH=Qt6_DIR-NOTFOUND" "$build_dir/CMakeCache.txt"; then
            need_config=1
        fi
    fi

    if [[ "$need_config" -eq 1 ]]; then
        printf "%bConfiguring CMake in %s...%b\n" "$CYAN" "$build_dir" "$NC"
        local cmake_args=("-S" "$project_dir" "-B" "$build_dir")
        local qt_prefix
        qt_prefix="$(detect_qt_prefix)"
        if [[ -n "$qt_prefix" ]]; then
            cmake_args+=("-DCMAKE_PREFIX_PATH=$qt_prefix")
        fi

        if ! cmake "${cmake_args[@]}"; then
            printf "%bCMake configuration failed!%b\n" "$RED" "$NC" >&2
            return 1
        fi
    fi

    printf "%bCompiling...%b\n" "$CYAN" "$NC"
    if ! cmake --build "$build_dir" -j"$(nproc)"; then
        printf "%bBuild failed!%b\n" "$RED" "$NC" >&2
        return 1
    fi

    printf "%bBuild completed successfully!%b\n" "$GREEN" "$NC"
    return 0
}

##
# @brief Build only core library and tests without Qt dependencies.
#
build_core()
{
    printf "\n%b=== Building Core Only (No GUI) ===%b\n" "$BLUE" "$NC"
    if ! update_submodules; then
        return 1
    fi
    if ! update_enums; then
        return 1
    fi

    local need_config=0
    if [[ ! -f "$build_core_dir/CMakeCache.txt" || (! -f "$build_core_dir/Makefile" && ! -f "$build_core_dir/build.ninja") ]]; then
        need_config=1
    fi

    if [[ "$need_config" -eq 1 ]]; then
        printf "%bConfiguring CMake in %s (BUILD_GUI=OFF)...%b\n" "$CYAN" "$build_core_dir" "$NC"
        if ! cmake -S "$project_dir" -B "$build_core_dir" -DBUILD_GUI=OFF; then
            printf "%bCMake configuration failed!%b\n" "$RED" "$NC" >&2
            return 1
        fi
    fi

    printf "%bCompiling...%b\n" "$CYAN" "$NC"
    if ! cmake --build "$build_core_dir" -j"$(nproc)"; then
        printf "%bCore build failed!%b\n" "$RED" "$NC" >&2
        return 1
    fi

    printf "%bCore build completed successfully!%b\n" "$GREEN" "$NC"
    return 0
}

##
# @brief Launch N instances of the simulator application as detached background processes.
#
# @param count Number of instances to launch (default: 1).
# @param ... Additional arguments passed to the Qt application.
#
launch_instances()
{
    local count="${1:-1}"
    shift || true

    if ! [[ "$count" =~ ^[1-9][0-9]*$ ]]; then
        printf "%bInvalid instance count: '%s'. Must be a positive integer.%b\n" "$RED" "$count" "$NC" >&2
        return 1
    fi

    local app="$build_dir/mavlink-lab"

    if [[ ! -x "$app" ]]; then
        printf "%bApplication not built yet. Building first...%b\n" "$YELLOW" "$NC"
        build_full
    elif [[ -f "$build_dir/CMakeCache.txt" ]]; then
        cmake --build "$build_dir" -j"$(nproc)" --target mavlink-lab >/dev/null 2>&1 || true
    fi

    if [[ ! -x "$app" ]]; then
        printf "%bApplication not found: %s\nBuild it with cmake first; see README.md.%b\n" "$RED" "$app" "$NC" >&2
        return 1
    fi

    setup_qt_env

    if [[ "$count" -eq 1 ]]; then
        printf "\n%b=== Launching MAVLinkLab (Detached Process) ===%b\n" "$GREEN" "$NC"
        nohup "$app" "$@" >/dev/null 2>&1 &
        local pid=$!
        disown "$pid" 2>/dev/null || true
        printf "  Started MAVLinkLab in background (PID: %d)\n" "$pid"
        return 0
    fi

    printf "\n%b=== Launching %d MAVLinkLab Windows (Detached) ===%b\n" "$GREEN" "$count" "$NC"
    for ((i = 1; i <= count; i++)); do
        nohup "$app" "$@" >/dev/null 2>&1 &
        local pid=$!
        disown "$pid" 2>/dev/null || true
        printf "  Started instance #%d (PID: %d)\n" "$i" "$pid"
    done
    return 0
}

##
# @brief Launch single instance of MAVLinkLab.
#
run_single()
{
    launch_instances 1 "$@"
}

##
# @brief Prompt user for N and launch N instances.
#
run_n_interactive()
{
    printf "\n%b=== Launch Multiple MAVLinkLab Instances ===%b\n" "$CYAN" "$NC"
    local n_input
    read -rp "Enter number of windows to launch [default: 2]: " n_input
    n_input="${n_input:-2}"
    launch_instances "$n_input" "$@"
}

##
# @brief Run all automated tests (Core + Desktop PTY & UI tests) with visual tracking.
#
run_tests()
{
    if [[ ! -d "$build_dir" ]]; then
        printf "%bBuild directory not found. Building project first...%b\n" "$YELLOW" "$NC"
        build_full
    elif [[ -f "$build_dir/CMakeCache.txt" ]]; then
        cmake --build "$build_dir" -j"$(nproc)"
    fi

    setup_qt_env

    local runner="$project_dir/tools/test_runner.py"
    if command -v python3 &>/dev/null && [[ -f "$runner" ]]; then
        local suite_arg=()
        local filter_arg=()
        if [[ $# -gt 0 && "$1" != -* ]]; then
            case "$1" in
                core|desktop|profile|control-lines)
                    suite_arg=(--suite "$1")
                    ;;
                *)
                    filter_arg=(--filter "$1")
                    ;;
            esac
            shift
        fi
        python3 "$runner" --build-dir "$build_dir" "${suite_arg[@]}" "${filter_arg[@]}" "$@"
    else
        printf "\n%b=== Running All Tests (Core + Desktop) ===%b\n" "$BLUE" "$NC"
        ctest --test-dir "$build_dir" --output-on-failure "$@"
    fi
}

##
# @brief Run core unit tests only.
#
run_core_tests()
{
    if [[ ! -d "$build_dir" ]]; then
        printf "%bBuild directory not found. Building project first...%b\n" "$YELLOW" "$NC"
        build_full
    elif [[ -f "$build_dir/CMakeCache.txt" ]]; then
        cmake --build "$build_dir" -j"$(nproc)"
    fi

    setup_qt_env

    local runner="$project_dir/tools/test_runner.py"
    if command -v python3 &>/dev/null && [[ -f "$runner" ]]; then
        python3 "$runner" --build-dir "$build_dir" --suite core "$@"
    elif [[ -d "$build_core_dir" && -x "$build_core_dir/core-tests" ]]; then
        printf "\n%b=== Running Core Tests (build-core) ===%b\n" "$BLUE" "$NC"
        ctest --test-dir "$build_core_dir" --output-on-failure "$@"
    elif [[ -d "$build_dir" && -x "$build_dir/core-tests" ]]; then
        printf "\n%b=== Running Core Tests (build) ===%b\n" "$BLUE" "$NC"
        ctest --test-dir "$build_dir" -R core --output-on-failure "$@"
    else
        printf "%bCore tests not built yet. Building core...%b\n" "$YELLOW" "$NC"
        build_core
        ctest --test-dir "$build_core_dir" --output-on-failure "$@"
    fi
}

##
# @brief Clean build artifacts.
#
clean_artifacts()
{
    printf "\n%b=== Cleaning Build Artifacts ===%b\n" "$YELLOW" "$NC"
    read -rp "Are you sure you want to delete build directories? [y/n]: " confirm
    case "$confirm" in
        [yY]|[yY][eE][sS])
            rm -rf "$build_dir" "$build_core_dir" "$project_dir/build-sanitize"
            printf "%bCleaned build directories.%b\n" "$GREEN" "$NC"
            ;;
        *)
            printf "Clean cancelled.\n"
            ;;
    esac
}

##
# @brief Rebuild the project from scratch.
#
rebuild_project()
{
    printf "\n%b=== Rebuilding Project ===%b\n" "$YELLOW" "$NC"
    local saved_prefix
    saved_prefix="$(detect_qt_prefix)"
    rm -rf "$build_dir"
    CMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH:-$saved_prefix}" build_full
}

##
# @brief Synchronize CodeGraph index if codegraph command is present.
#
sync_codegraph()
{
    printf "\n%b=== Syncing CodeGraph ===%b\n" "$BLUE" "$NC"
    if command -v codegraph &>/dev/null; then
        codegraph sync
        codegraph status
    else
        printf "%bCodeGraph CLI is not installed in PATH.%b\n" "$YELLOW" "$NC"
    fi
}

##
# @brief Regenerate MAVLink enum tables from XML definitions.
#
update_enums()
{
    printf "\n%b=== Updating MAVLink Enums from XML Definitions ===%b\n" "$BLUE" "$NC"
    local generator="$project_dir/tools/generate_mavlink_enums.py"
    if [[ ! -f "$generator" ]]; then
        printf "%bGenerator script not found: %s%b\n" "$RED" "$generator" "$NC" >&2
        return 1
    fi
    if command -v python3 &>/dev/null; then
        python3 "$generator"
        printf "%bMAVLink enum tables updated successfully.%b\n" "$GREEN" "$NC"
        return 0
    else
        printf "%bpython3 is required to regenerate MAVLink enums.%b\n" "$RED" "$NC" >&2
        return 1
    fi
}

##
# @brief Get the current application version from CMakeLists.txt (Single Source of Truth).
#
get_project_version()
{
    sed -n 's/^project(MAVLinkLab VERSION \([0-9.]*\) LANGUAGES CXX)/\1/p' "$project_dir/CMakeLists.txt"
}

##
# @brief Compare two semantic version strings (X.Y.Z).
#
# @param 1 First version string.
# @param 2 Second version string.
# @return Prints 1 if v1 > v2, -1 if v1 < v2, 0 if equal.
#
semver_compare()
{
    local v1="${1#v}"
    local v2="${2#v}"
    v1="${v1%%-*}"
    v2="${v2%%-*}"
    if [[ "$v1" == "$v2" ]]; then
        printf "0\n"
        return 0
    fi
    if command -v python3 &>/dev/null; then
        python3 -c "
try:
    p1 = [int(x) for x in '${v1}'.split('.')]
    p2 = [int(x) for x in '${v2}'.split('.')]
    print(1 if p1 > p2 else (-1 if p1 < p2 else 0))
except Exception:
    print(0)
"
    else
        local IFS=.
        local i ver1=($v1) ver2=($v2)
        for ((i = 0; i < 3; i++)); do
            local n1=${ver1[i]:-0}
            local n2=${ver2[i]:-0}
            if (( n1 > n2 )); then
                printf "1\n"
                return 0
            elif (( n1 < n2 )); then
                printf "-1\n"
                return 0
            fi
        done
        printf "0\n"
    fi
}

##
# @brief Bump the project version in CMakeLists.txt.
#
# @param 1 New version string (e.g. 1.3.0 or v1.3.0).
#
bump_project_version()
{
    local new_ver="${1:-}"
    if [[ -z "$new_ver" ]]; then
        printf "%bUsage: %s bump <new-version> (e.g. ./cli.sh bump 1.3.0)%b\n" "$RED" "$0" "$NC" >&2
        return 1
    fi
    new_ver="${new_ver#v}"
    if ! [[ "$new_ver" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        printf "%bInvalid SemVer format: '%s'. Expected X.Y.Z%b\n" "$RED" "$new_ver" "$NC" >&2
        return 1
    fi
    local old_ver
    old_ver="$(get_project_version)"

    local cmp
    cmp="$(semver_compare "$new_ver" "$old_ver")"
    if [[ "$cmp" -eq 0 ]]; then
        printf "%bProject version is already %s.%b\n" "$YELLOW" "$old_ver" "$NC"
        return 0
    elif [[ "$cmp" -lt 0 ]]; then
        printf "%bWarning: New version (%s) is lower than current version (%s).%b\n" "$YELLOW" "$new_ver" "$old_ver" "$NC"
        local confirm
        read -rp "Are you sure you want to change to a lower version? [Y/n]: " confirm
        confirm="${confirm:-Y}"
        if ! [[ "$confirm" =~ ^[Yy]$ ]]; then
            printf "%bVersion update cancelled.%b\n" "$YELLOW" "$NC"
            return 0
        fi
    fi

    sed -i "s/^project(MAVLinkLab VERSION [0-9.]* LANGUAGES CXX)/project(MAVLinkLab VERSION ${new_ver} LANGUAGES CXX)/" "$project_dir/CMakeLists.txt"
    printf "%bUpdated project version: %s -> %s%b\n" "$GREEN" "$old_ver" "$new_ver" "$NC"
}

##
# @brief Interactive prompt to bump the project version.
#
bump_version_interactive()
{
    local current_ver
    current_ver="$(get_project_version)"
    printf "\n%bCurrent project version: %s%b\n" "$CYAN" "$current_ver" "$NC"
    local input_ver
    read -rp "Enter new version (X.Y.Z) or leave empty to cancel: " input_ver
    if [[ -z "$input_ver" ]]; then
        printf "%bOperation cancelled.%b\n" "$YELLOW" "$NC"
        return 0
    fi
    bump_project_version "$input_ver"
}

##
# @brief Install or update the Linux desktop launcher (.desktop) and application icons.
#
# @return 0 on success, 1 on error.
#
install_desktop_entry()
{
    printf "\n%b=== Installing MAVLinkLab Desktop Launcher ===%b\n" "$CYAN" "$NC"

    local app_bin="$build_dir/mavlink-lab"
    local apps_dir="$HOME/.local/share/applications"
    local icons_base="$HOME/.local/share/icons/hicolor"
    local desktop_file="$apps_dir/mavlink-lab.desktop"

    if [[ ! -x "$app_bin" ]]; then
        printf "%bApplication binary not found at: %s%b\n" "$YELLOW" "$app_bin" "$NC"
        printf "Building application first...\n"
        build_full
        if [[ ! -x "$app_bin" ]]; then
            printf "%bFailed to build application binary. Aborting desktop install.%b\n" "$RED" "$NC" >&2
            return 1
        fi
    fi

    # 1. Create target directories
    mkdir -p "$apps_dir"
    mkdir -p "$icons_base/scalable/apps"

    # 2. Install application icons
    printf "Installing application icons to %s...\n" "$icons_base"
    local sizes=(16 24 32 48 64 128 256 512)
    for size in "${sizes[@]}"; do
        local src_png="$project_dir/resources/icons/mavlink-lab-${size}.png"
        local dest_dir="$icons_base/${size}x${size}/apps"
        if [[ -f "$src_png" ]]; then
            mkdir -p "$dest_dir"
            cp -f "$src_png" "$dest_dir/mavlink-lab.png"
        fi
    done

    local src_svg="$project_dir/resources/icons/mavlink-lab.svg"
    if [[ -f "$src_svg" ]]; then
        cp -f "$src_svg" "$icons_base/scalable/apps/mavlink-lab.svg"
    fi

    # 3. Generate .desktop file
    printf "Generating desktop entry at %s...\n" "$desktop_file"
    cat > "$desktop_file" <<EOF
[Desktop Entry]
Name=MAVLinkLab
GenericName=Serial Workbench for MAVLink
Comment=Desktop serial workbench for sending, receiving and inspecting MAVLink telemetry
Exec=$app_bin %f
Path=$project_dir
Icon=mavlink-lab
Terminal=false
Type=Application
Categories=Development;Engineering;Electronics;
Keywords=mavlink;serial;uart;telemetry;drone;uav;robotics;ardupilot;px4;
StartupWMClass=mavlink-lab
EOF

    chmod 644 "$desktop_file"

    # 4. Update system caches if tools exist
    if command -v update-desktop-database &>/dev/null; then
        update-desktop-database "$apps_dir" 2>/dev/null || true
    fi
    if command -v gtk-update-icon-cache &>/dev/null; then
        gtk-update-icon-cache -f -t "$icons_base" 2>/dev/null || true
    fi

    printf "%b✔ Desktop launcher installed successfully!%b\n" "$GREEN" "$NC"
    printf "  • Desktop entry: %s\n" "$desktop_file"
    printf "  • Binary target: %s\n" "$app_bin"
    printf "  • Icon installed: %s/scalable/apps/mavlink-lab.svg\n" "$icons_base"
    printf "You can now launch MAVLinkLab from your application menu or dash.\n\n"
    return 0
}

##
# @brief Uninstall and remove the Linux desktop launcher (.desktop) and installed icons.
#
# @return 0 on success.
#
uninstall_desktop_entry()
{
    printf "\n%b=== Removing MAVLinkLab Desktop Launcher ===%b\n" "$CYAN" "$NC"

    local apps_dir="$HOME/.local/share/applications"
    local icons_base="$HOME/.local/share/icons/hicolor"
    local desktop_file="$apps_dir/mavlink-lab.desktop"
    local removed_any=0

    # 1. Remove .desktop file
    if [[ -f "$desktop_file" ]]; then
        rm -f "$desktop_file"
        printf "  • Removed desktop entry: %s\n" "$desktop_file"
        removed_any=1
    fi

    # 2. Remove icons
    local sizes=(16 24 32 48 64 128 256 512)
    for size in "${sizes[@]}"; do
        local icon_path="$icons_base/${size}x${size}/apps/mavlink-lab.png"
        if [[ -f "$icon_path" ]]; then
            rm -f "$icon_path"
            removed_any=1
        fi
    done
    if [[ -f "$icons_base/scalable/apps/mavlink-lab.svg" ]]; then
        rm -f "$icons_base/scalable/apps/mavlink-lab.svg"
        removed_any=1
    fi

    # 3. Update system caches
    if command -v update-desktop-database &>/dev/null; then
        update-desktop-database "$apps_dir" 2>/dev/null || true
    fi
    if command -v gtk-update-icon-cache &>/dev/null; then
        gtk-update-icon-cache -f -t "$icons_base" 2>/dev/null || true
    fi

    if [[ "$removed_any" -eq 1 ]]; then
        printf "%b✔ Desktop launcher and icons removed successfully!%b\n\n" "$GREEN" "$NC"
    else
        printf "%bDesktop launcher was not found on your system.%b\n\n" "$YELLOW" "$NC"
    fi
    return 0
}

##
# @brief Query GitHub Releases API for the latest version and prompt to update if newer.
#
# @return 0 on success, 1 on network or parse failure.
#
check_for_updates()
{
    printf "\n%b=== Checking for MAVLinkLab Updates ===%b\n" "$CYAN" "$NC"

    local current_ver
    current_ver="$(get_project_version)"
    local current_clean="${current_ver#v}"

    if ! command -v curl &>/dev/null; then
        printf "%bError: 'curl' is required to check for updates.%b\n" "$RED" "$NC" >&2
        return 1
    fi

    printf "Querying GitHub Releases (minhtan2510/MAVLinkLab)...\n"
    local api_url="https://api.github.com/repos/minhtan2510/MAVLinkLab/releases/latest"
    local response
    response="$(curl -sSfL -m 8 -H "Accept: application/vnd.github.v3+json" "$api_url" 2>/dev/null || true)"

    if [[ -z "$response" ]]; then
        printf "%bUnable to connect to GitHub API. Please check your internet connection.%b\n" "$RED" "$NC" >&2
        return 1
    fi

    local latest_tag=""
    local release_name=""
    local release_url=""

    if command -v python3 &>/dev/null; then
        {
            read -r latest_tag
            read -r release_name
            read -r release_url
        } < <(python3 -c '
import sys, json
try:
    data = json.loads(sys.stdin.read())
    print(data.get("tag_name", "").strip())
    print(data.get("name", "").strip())
    print(data.get("html_url", "").strip())
except Exception:
    pass
' <<< "$response")
    fi

    if [[ -z "$latest_tag" ]]; then
        latest_tag="$(sed -n 's/.*"tag_name":[[:space:]]*"\([^"]*\)".*/\1/p' <<< "$response" | head -n 1)"
        release_name="$(sed -n 's/.*"name":[[:space:]]*"\([^"]*\)".*/\1/p' <<< "$response" | head -n 1)"
        release_url="$(sed -n 's/.*"html_url":[[:space:]]*"\([^"]*\)".*/\1/p' <<< "$response" | head -n 1)"
    fi

    if [[ -z "$latest_tag" ]]; then
        printf "%bCould not parse release information from GitHub response.%b\n" "$RED" "$NC" >&2
        return 1
    fi

    local latest_clean="${latest_tag#v}"
    local cmp
    cmp="$(semver_compare "$latest_clean" "$current_clean")"

    printf "  • Current version: %bv%s%b\n" "$BOLD" "$current_clean" "$NC"
    printf "  • Latest release:  %bv%s%b (%s)\n" "$BOLD" "$latest_clean" "$NC" "${release_name:-$latest_tag}"
    printf "  • Release page:    %s\n\n" "$release_url"

    if [[ "$cmp" -eq 1 ]]; then
        printf "%b★ A new version is available: v%s -> v%s%b\n\n" "$YELLOW" "$current_clean" "$latest_clean" "$NC"

        local confirm
        read -rp "Do you want to update to ${latest_tag} now? [Y/n]: " confirm
        confirm="${confirm:-Y}"
        if [[ "$confirm" =~ ^[Yy]$ ]]; then
            perform_update "$latest_tag"
            return $?
        else
            printf "%bUpdate skipped.%b\n\n" "$YELLOW" "$NC"
        fi
    elif [[ "$cmp" -eq 0 ]]; then
        printf "%b✔ MAVLinkLab is up to date (v%s).%b\n\n" "$GREEN" "$current_clean" "$NC"
    else
        printf "%bYou are running a newer/pre-release build (v%s) than the latest GitHub release (v%s).%b\n\n" "$CYAN" "$current_clean" "$latest_clean" "$NC"
    fi
    return 0
}

##
# @brief Perform update by fetching the target tag or remote branch, updating submodules, and rebuilding.
#
# @param 1 Target release tag (e.g. v1.4.0).
# @return 0 on success, 1 on error.
#
perform_update()
{
    local target_tag="${1:-}"
    printf "\n%b=== Updating MAVLinkLab to %s ===%b\n" "$CYAN" "${target_tag:-latest}" "$NC"

    # Check for uncommitted working tree changes
    if [[ -d "$project_dir/.git" ]]; then
        if [[ -n "$(git -C "$project_dir" status --porcelain 2>/dev/null)" ]]; then
            printf "%bWarning: You have uncommitted changes in your repository.%b\n" "$YELLOW" "$NC"
            read -rp "Discard uncommitted changes and continue with update? [y/N]: " force_update
            if ! [[ "$force_update" =~ ^[Yy]$ ]]; then
                printf "%bUpdate aborted. Please commit or stash your changes first.%b\n" "$RED" "$NC"
                return 1
            fi
            git -C "$project_dir" reset --hard HEAD
        fi

        printf "Fetching latest tags and commits from origin...\n"
        git -C "$project_dir" fetch origin --tags

        local current_branch
        current_branch="$(git -C "$project_dir" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "main")"

        if [[ -n "$target_tag" ]]; then
            if git -C "$project_dir" rev-parse "refs/tags/$target_tag" &>/dev/null; then
                printf "Checking out release tag: %s...\n" "$target_tag"
                git -C "$project_dir" checkout "$target_tag"
            else
                printf "Pulling latest changes on branch '%s'...\n" "$current_branch"
                git -C "$project_dir" pull --ff-only origin "$current_branch" || git -C "$project_dir" pull origin "$current_branch"
            fi
        else
            printf "Pulling latest changes on branch '%s'...\n" "$current_branch"
            git -C "$project_dir" pull --ff-only origin "$current_branch" || git -C "$project_dir" pull origin "$current_branch"
        fi

        printf "Updating submodules...\n"
        git -C "$project_dir" submodule update --init --recursive
    fi

    printf "Rebuilding application...\n"
    build_full

    # Re-apply desktop launcher if it was already installed
    local desktop_installed="$HOME/.local/share/applications/mavlink-lab.desktop"
    if [[ -f "$desktop_installed" ]]; then
        printf "Updating installed desktop launcher...\n"
        install_desktop_entry >/dev/null 2>&1 || true
    fi

    local new_ver
    new_ver="$(get_project_version)"
    printf "\n%b✔ Successfully updated MAVLinkLab to v%s!%b\n\n" "$GREEN" "$new_ver" "$NC"
    return 0
}

##
# @brief Interactive update entry point checking GitHub release and prompting user.
#
auto_update()
{
    check_for_updates 1
}

##
# @brief Print CLI command usage help.
#
show_help()
{
    if [[ "$IS_DEV_MODE" -eq 1 ]]; then
        printf "Usage: %s -dev [group/command] [options...]\n\n" "$0"
        printf "Groups & Commands (Developer Mode):\n"
        printf "  Build:\n"
        printf "    build [all]         Build full desktop app & test suites (default)\n"
        printf "    build core          Build core-only library & tests (no Qt)\n"
        printf "    build rebuild       Clean and rebuild full project\n"
        printf "    build clean         Delete build directories\n\n"
        printf "  Run:\n"
        printf "    run                 Launch single MAVLinkLab window\n"
        printf "    run <N>             Launch N MAVLinkLab windows (e.g. ./cli.sh run 3)\n"
        printf "    run -n <N>          Launch N MAVLinkLab windows\n"
        printf "    run --two           Launch two MAVLinkLab windows\n"
        printf "    run-two             Alias for 2 MAVLinkLab windows\n\n"
        printf "  Test:\n"
        printf "    test [all]          Run all tests (Core & Desktop offscreen)\n"
        printf "    test core           Run core tests only\n\n"
        printf "  Utilities:\n"
        printf "    check-update        Check for latest version from GitHub Releases\n"
        printf "    update              Check and update to latest GitHub release\n"
        printf "    clean               Delete build directories\n"
        printf "    rebuild             Clean and rebuild full project\n"
        printf "    submodules          Update git submodules\n"
        printf "    update-enums        Regenerate MAVLink enum tables from XML\n"
        printf "    sync-graph          Synchronize CodeGraph index\n"
        printf "    install-desktop     Install desktop launcher (.desktop & icons)\n"
        printf "    uninstall-desktop   Remove desktop launcher and icons\n"
        printf "    version             Display current project version\n"
        printf "    bump <version>      Update project version in CMakeLists.txt\n"
        printf "    menu                Open interactive menu (default when no args)\n"
        printf "    help, -h, --help    Show this help message\n\n"
        printf "Environment Variables:\n"
        printf "  MAVLINK_BUILD_DIR     Select custom build directory (default: build)\n"
    else
        printf "Usage: %s [command] [options...]\n\n" "$0"
        printf "Commands:\n"
        printf "  Build:\n"
        printf "    build [all]         Build full desktop app (default)\n"
        printf "    build core          Build core-only library (no Qt)\n"
        printf "    build clean         Delete build directories\n"
        printf "    build rebuild       Clean and rebuild full project\n\n"
        printf "  Run:\n"
        printf "    run                 Launch single MAVLinkLab window\n"
        printf "    run <N>             Launch N MAVLinkLab windows (e.g. ./cli.sh run 3)\n"
        printf "    run --two           Launch two MAVLinkLab windows\n\n"
        printf "  General:\n"
        printf "    check-update        Check for latest version from GitHub Releases\n"
        printf "    update              Check and update to latest GitHub release\n"
        printf "    clean               Delete build directories\n"
        printf "    rebuild             Clean and rebuild full project\n"
        printf "    install-desktop     Install desktop launcher (.desktop & icons)\n"
        printf "    uninstall-desktop   Remove desktop launcher and icons\n"
        printf "    menu                Open interactive menu (default when no args)\n"
        printf "    help, -h, --help    Show this help message\n\n"
        printf "Developer Mode:\n"
        printf "    Pass '-dev' flag for development workbench (testing, CodeGraph, MAVLink tools, change version).\n"
        printf "    Example: %s -dev\n\n" "$0"
    fi
}

##
# @brief Show an arrow-key menu, retaining its last highlight within this session.
#
# @param title Title of the menu.
# @param return_var Variable name to receive the selected 0-based index (-1 if cancelled/exit).
# @param ... List of option strings.
#
prompt_menu()
{
    local title="$1"
    local return_var="$2"
    shift 2
    local options=("$@")
    local num_options=${#options[@]}
    local selected="${menu_selections["$title"]:-0}"
    if (( selected < 0 || selected >= num_options )); then
        selected=0
    fi
    local total_lines=$((num_options + 6))

    # Fallback to simple line-based prompt if stdin is not a TTY
    if [[ ! -t 0 ]]; then
        printf "\n%s:\n" "$title"
        for i in "${!options[@]}"; do
            printf "  %2d) %s\n" "$((i + 1))" "${options[$i]}"
        done
        local fallback_choice
        read -rp "Select option [1-$num_options]: " fallback_choice || true
        local choice_idx=$((fallback_choice - 1))
        if [[ "$choice_idx" -ge 0 && "$choice_idx" -lt "$num_options" ]]; then
            eval "$return_var=$choice_idx"
        else
            eval "$return_var=-1"
        fi
        return 0
    fi

    # Ensure cursor is restored if interrupted
    trap 'printf "\033[?25h"; exit 0' INT TERM

    local first_draw=1

    while true; do
        printf "\033[?25l" # Hide cursor

        if [[ "$first_draw" -eq 0 ]]; then
            printf "\033[%dA" "$total_lines"
        else
            first_draw=0
        fi

        printf "\033[1;36m====================================================\033[0m\033[K\n"
        printf "\033[1;36m  %-48s  \033[0m\033[K\n" "$title"
        printf "\033[1;36m====================================================\033[0m\033[K\n"
        printf "\033[90mUse \xE2\x86\x91/\xE2\x86\x93 (or j/k) to navigate, Enter to select, Esc/Backspace/q to back\033[0m\033[K\n"
        printf "\033[K\n"

        for i in "${!options[@]}"; do
            if [[ "$i" -eq "$selected" ]]; then
                printf "  \033[1;32m\xE2\x9D\xAF \033[1m%s\033[0m\033[K\n" "${options[$i]}"
            else
                printf "    \033[2m%s\033[0m\033[K\n" "${options[$i]}"
            fi
        done
        printf "\033[1;36m====================================================\033[0m\033[K\n"

        local key=""
        local rest=""
        IFS= read -rsn1 key || true

        if [[ "$key" == $'\e' ]]; then
            IFS= read -rsn2 -t 0.05 rest || true
            key+="$rest"
        fi

        case "$key" in
            $'\e[A'|k|K)
                ((selected--)) || true
                if [[ "$selected" -lt 0 ]]; then
                    selected=$((num_options - 1))
                fi
                ;;
            $'\e[B'|j|J)
                ((selected++)) || true
                if [[ "$selected" -ge "$num_options" ]]; then
                    selected=0
                fi
                ;;
            "")
                # Enter key pressed
                printf "\033[?25h" # Restore cursor
                menu_selections["$title"]=$selected
                eval "$return_var=$selected"
                return 0
                ;;
            q|Q|$'\e'|$'\177'|$'\b')
                printf "\033[?25h"
                menu_selections["$title"]=$selected
                eval "$return_var=-1"
                return 0
                ;;
        esac
    done
}

##
# @brief Interactive submenu for Build actions.
#
menu_build()
{
    while true; do
        local choice
        if [[ "$IS_DEV_MODE" -eq 1 ]]; then
            prompt_menu "Build Management (Dev Mode)" choice \
                "Build all (Full Desktop + Tests)" \
                "Install / update dependencies (requirement.txt)" \
                "Build core only (No GUI)" \
                "Rebuild project (Clean + Build)" \
                "Clean build artifacts" \
                "← Back to Main Menu"

            case "$choice" in
                0)
                    build_full || true
                    read -rp "Press Enter to continue..." _dummy
                    safe_clear
                    ;;
                1)
                    install_requirements || true
                    read -rp "Press Enter to continue..." _dummy
                    safe_clear
                    ;;
                2)
                    build_core || true
                    read -rp "Press Enter to continue..." _dummy
                    safe_clear
                    ;;
                3)
                    rebuild_project || true
                    read -rp "Press Enter to continue..." _dummy
                    safe_clear
                    ;;
                4)
                    clean_artifacts || true
                    read -rp "Press Enter to continue..." _dummy
                    safe_clear
                    ;;
                *)
                    safe_clear
                    return 0
                    ;;
            esac
        else
            prompt_menu "Build Management" choice \
                "Build all (Full Desktop)" \
                "Build core only (No GUI)" \
                "Clean build artifacts" \
                "Rebuild project (Clean + Build)" \
                "← Back to Main Menu"

            case "$choice" in
                0)
                    build_full || true
                    read -rp "Press Enter to continue..." _dummy
                    safe_clear
                    ;;
                1)
                    build_core || true
                    read -rp "Press Enter to continue..." _dummy
                    safe_clear
                    ;;
                2)
                    clean_artifacts || true
                    read -rp "Press Enter to continue..." _dummy
                    safe_clear
                    ;;
                3)
                    rebuild_project || true
                    read -rp "Press Enter to continue..." _dummy
                    safe_clear
                    ;;
                *)
                    safe_clear
                    return 0
                    ;;
            esac
        fi
    done
}

##
# @brief Interactive submenu for Run actions.
#
menu_run()
{
    while true; do
        local choice
        prompt_menu "Run MAVLinkLab" choice \
            "Run single window" \
            "Run multi windows" \
            "← Back to Main Menu"

        case "$choice" in
            0)
                run_single || true
                read -rp "Press Enter to continue..." _dummy
                safe_clear
                ;;
            1)
                run_n_interactive || true
                read -rp "Press Enter to continue..." _dummy
                safe_clear
                ;;
            *)
                safe_clear
                return 0
                ;;
        esac
    done
}

##
# @brief Interactive submenu for Test actions.
#
menu_test()
{
    while true; do
        local choice
        prompt_menu "Test Suite" choice \
            "Run all tests (Core + Desktop offscreen)" \
            "Run core tests only" \
            "← Back to Main Menu"

        case "$choice" in
            0)
                run_tests || true
                read -rp "Press Enter to continue..." _dummy
                safe_clear
                ;;
            1)
                run_core_tests || true
                read -rp "Press Enter to continue..." _dummy
                safe_clear
                ;;
            *)
                safe_clear
                return 0
                ;;
        esac
    done
}

##
# @brief Interactive submenu for Utility actions.
#
menu_tools()
{
    while true; do
        local choice
        local cur_ver
        cur_ver="$(get_project_version)"
        prompt_menu "Tools & Utilities" choice \
            "Change project version (Current: ${cur_ver})" \
            "Check for updates (GitHub Releases)" \
            "Update git submodules" \
            "Regenerate MAVLink enums from XML" \
            "Sync CodeGraph" \
            "Install desktop launcher (.desktop & icons)" \
            "Uninstall desktop launcher (remove .desktop & icons)" \
            "← Back to Main Menu"

        case "$choice" in
            0)
                bump_version_interactive || true
                read -rp "Press Enter to continue..." _dummy
                safe_clear
                ;;
            1)
                check_for_updates || true
                read -rp "Press Enter to continue..." _dummy
                safe_clear
                ;;
            2)
                update_submodules || true
                read -rp "Press Enter to continue..." _dummy
                safe_clear
                ;;
            3)
                update_enums || true
                read -rp "Press Enter to continue..." _dummy
                safe_clear
                ;;
            4)
                sync_codegraph || true
                read -rp "Press Enter to continue..." _dummy
                safe_clear
                ;;
            5)
                install_desktop_entry || true
                read -rp "Press Enter to continue..." _dummy
                safe_clear
                ;;
            6)
                uninstall_desktop_entry || true
                read -rp "Press Enter to continue..." _dummy
                safe_clear
                ;;
            *)
                safe_clear
                return 0
                ;;
        esac
    done
}

##
# @brief Display interactive main menu with grouped navigation.
#
interactive_menu()
{
    safe_clear
    while true; do
        local choice
        if [[ "$IS_DEV_MODE" -eq 1 ]]; then
            prompt_menu "MAVLinkLab CLI Workbench (Dev Mode)" choice \
                "[Build]  Build all, rebuild, or clean project" \
                "[Run]    Run project" \
                "[Test]   Run all tests or core tests" \
                "[Tools]  Change version, submodules, CodeGraph, desktop entry" \
                "[Exit]   Exit CLI"

            case "$choice" in
                0)
                    safe_clear
                    menu_build
                    ;;
                1)
                    safe_clear
                    menu_run
                    ;;
                2)
                    safe_clear
                    menu_test
                    ;;
                3)
                    safe_clear
                    menu_tools
                    ;;
                4|-1)
                    printf "\033[?25h"
                    printf "\nExiting MAVLinkLab CLI. Goodbye!\n"
                    exit 0
                    ;;
            esac
        else
            prompt_menu "MAVLinkLab CLI Workbench" choice \
                "[Build]  Build all, core, clean, or rebuild" \
                "[Run]    Run MAVLinkLab" \
                "[Exit]   Exit CLI"

            case "$choice" in
                0)
                    safe_clear
                    menu_build
                    ;;
                1)
                    safe_clear
                    menu_run
                    ;;
                2|-1)
                    printf "\033[?25h"
                    printf "\nExiting MAVLinkLab CLI. Goodbye!\n"
                    exit 0
                    ;;
            esac
        fi
    done
}

# Entry point logic
cmd="${1:-}"

case "$cmd" in
    build)
        shift
        subcmd="${1:-all}"
        case "$subcmd" in
            all|full)
                shift || true
                build_full "$@"
                ;;
            core)
                shift || true
                build_core "$@"
                ;;
            rebuild)
                shift || true
                rebuild_project "$@"
                ;;
            clean)
                shift || true
                clean_artifacts "$@"
                ;;
            *)
                build_full "$subcmd" "$@"
                ;;
        esac
        ;;
    build-full)
        shift
        build_full "$@"
        ;;
    build-core)
        shift
        build_core "$@"
        ;;
    deps|requirements|install-deps)
        shift
        install_requirements "$@"
        ;;
    run)
        shift
        if [[ "${1:-}" == "--two" ]]; then
            shift
            launch_instances 2 "$@"
        elif [[ "${1:-}" == "-n" ]]; then
            shift
            count="${1:-1}"
            shift || true
            launch_instances "$count" "$@"
        elif [[ "${1:-}" =~ ^[1-9][0-9]*$ ]]; then
            count="$1"
            shift
            launch_instances "$count" "$@"
        else
            launch_instances 1 "$@"
        fi
        ;;
    run-two|--two)
        shift
        launch_instances 2 "$@"
        ;;
    test)
        shift
        if [[ "${1:-}" == "core" ]]; then
            shift
            run_core_tests "$@"
        else
            run_tests "$@"
        fi
        ;;
    test-core)
        shift
        run_core_tests "$@"
        ;;
    clean)
        shift
        clean_artifacts "$@"
        ;;
    rebuild)
        shift
        rebuild_project "$@"
        ;;
    submodules)
        shift
        update_submodules "$@"
        ;;
    update-enums|enums)
        shift
        update_enums "$@"
        ;;
    sync-graph)
        shift
        sync_codegraph "$@"
        ;;
    check-update|check-updates|update|self-update)
        shift
        check_for_updates "$@"
        ;;
    install-desktop|desktop)
        shift
        install_desktop_entry "$@"
        ;;
    uninstall-desktop|remove-desktop)
        shift
        uninstall_desktop_entry "$@"
        ;;
    version)
        printf "%s\n" "$(get_project_version)"
        ;;
    bump)
        shift
        bump_project_version "$@"
        ;;
    menu)
        interactive_menu
        ;;
    help|-h|--help)
        show_help
        ;;
    "")
        interactive_menu
        ;;
    *)
        printf "%bUnknown command: %s%b\n\n" "$RED" "$cmd" "$NC" >&2
        show_help >&2
        exit 1
        ;;
esac

