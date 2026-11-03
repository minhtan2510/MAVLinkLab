#!/usr/bin/env bash
# CLI workbench management tool for MAVLinkSimulator.
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
    if [[ -r "$cache" ]]; then
        local qt_cmake_dir
        qt_cmake_dir="$(sed -n 's/^Qt6Core_DIR:PATH=//p' "$cache")"
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
# @brief Update git submodules recursively.
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

    local need_config=0
    if [[ ! -f "$build_dir/CMakeCache.txt" || (! -f "$build_dir/Makefile" && ! -f "$build_dir/build.ninja") ]]; then
        need_config=1
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
# @brief Launch N instances of the simulator application.
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

    local app="$build_dir/mavlink-simulator"

    if [[ ! -x "$app" ]]; then
        printf "%bApplication not built yet. Building first...%b\n" "$YELLOW" "$NC"
        build_full
    fi

    if [[ ! -x "$app" ]]; then
        printf "%bApplication not found: %s\nBuild it with cmake first; see README.md.%b\n" "$RED" "$app" "$NC" >&2
        return 1
    fi

    setup_qt_env

    if [[ "$count" -eq 1 ]]; then
        printf "\n%b=== Launching MAVLink Simulator (Single Window) ===%b\n" "$GREEN" "$NC"
        "$app" "$@"
        return $?
    fi

    printf "\n%b=== Launching %d MAVLink Simulator Windows ===%b\n" "$GREEN" "$count" "$NC"
    local pids=()
    for ((i = 1; i <= count; i++)); do
        "$app" "$@" &
        local pid=$!
        pids+=("$pid")
        printf "  Started instance #%d (PID: %d)\n" "$i" "$pid"
    done
    printf "%bPress Ctrl+C to terminate all %d instances.%b\n" "$YELLOW" "$count" "$NC"

    # @brief Stop child instances when interrupted.
    stop_instances()
    {
        trap - INT TERM
        printf "\n%bStopping %d child instances...%b\n" "$YELLOW" "${#pids[@]}" "$NC"
        kill "${pids[@]}" 2>/dev/null || true
        wait "${pids[@]}" 2>/dev/null || true
    }

    trap stop_instances INT TERM
    local status=0
    for pid in "${pids[@]}"; do
        wait "$pid" || status=$?
    done
    trap - INT TERM
    return "$status"
}

##
# @brief Launch single instance of MAVLinkSimulator.
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
    printf "\n%b=== Launch Multiple Simulator Instances ===%b\n" "$CYAN" "$NC"
    local n_input
    read -rp "Enter number of windows to launch [default: 2]: " n_input
    n_input="${n_input:-2}"
    launch_instances "$n_input" "$@"
}

##
# @brief Run all automated tests (Core + Desktop PTY & UI tests).
#
run_tests()
{
    if [[ ! -d "$build_dir" ]]; then
        printf "%bBuild directory not found. Building project first...%b\n" "$YELLOW" "$NC"
        build_full
    fi

    printf "\n%b=== Running All Tests (Core + Desktop) ===%b\n" "$BLUE" "$NC"
    setup_qt_env
    ctest --test-dir "$build_dir" --output-on-failure "$@"
}

##
# @brief Run core unit tests only.
#
run_core_tests()
{
    if [[ -d "$build_core_dir" && -x "$build_core_dir/core-tests" ]]; then
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
# @brief Print CLI command usage help.
#
show_help()
{
    printf "Usage: %s [group/command] [options...]\n\n" "$0"
    printf "Groups & Commands:\n"
    printf "  Build:\n"
    printf "    build [all]         Build full desktop app & test suites (default)\n"
    printf "    build core          Build core-only library & tests (no Qt)\n"
    printf "    build rebuild       Clean and rebuild full project\n"
    printf "    build clean         Delete build directories\n\n"
    printf "  Run:\n"
    printf "    run                 Launch single MAVLink Simulator window\n"
    printf "    run <N>             Launch N simulator windows (e.g. ./cli.sh run 3)\n"
    printf "    run -n <N>          Launch N simulator windows\n"
    printf "    run --two           Launch two simulator windows\n"
    printf "    run-two             Alias for 2 simulator windows\n\n"
    printf "  Test:\n"
    printf "    test [all]          Run all tests (Core & Desktop offscreen)\n"
    printf "    test core           Run core tests only\n\n"
    printf "  Utilities:\n"
    printf "    clean               Delete build directories\n"
    printf "    rebuild             Clean and rebuild full project\n"
    printf "    submodules          Update git submodules\n"
    printf "    sync-graph          Synchronize CodeGraph index\n"
    printf "    menu                Open interactive menu (default when no args)\n"
    printf "    help, -h, --help    Show this help message\n\n"
    printf "Environment Variables:\n"
    printf "  MAVLINK_BUILD_DIR     Select custom build directory (default: build)\n"
}

##
# @brief Generic interactive menu using arrow key navigation.
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
    local selected=0
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
        printf "\033[90mUse \xE2\x86\x91/\xE2\x86\x93 (or j/k) to navigate, Enter to select, q to back\033[0m\033[K\n"
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
            IFS= read -rsn2 -t 0.1 rest || true
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
                eval "$return_var=$selected"
                return 0
                ;;
            q|Q|$'\e')
                printf "\033[?25h"
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
        prompt_menu "Build Management" choice \
            "Build all (Full Desktop + Tests)" \
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
                build_core || true
                read -rp "Press Enter to continue..." _dummy
                safe_clear
                ;;
            2)
                rebuild_project || true
                read -rp "Press Enter to continue..." _dummy
                safe_clear
                ;;
            3)
                clean_artifacts || true
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
# @brief Interactive submenu for Run actions.
#
menu_run()
{
    while true; do
        local choice
        prompt_menu "Run Simulator" choice \
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
        prompt_menu "Tools & Utilities" choice \
            "Update git submodules" \
            "Sync CodeGraph" \
            "← Back to Main Menu"

        case "$choice" in
            0)
                update_submodules || true
                read -rp "Press Enter to continue..." _dummy
                safe_clear
                ;;
            1)
                sync_codegraph || true
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
        prompt_menu "MAVLink Simulator CLI Workbench" choice \
            "[Build]  Build all, rebuild, or clean project" \
            "[Run]    Run project" \
            "[Test]   Run all tests or core tests" \
            "[Tools]  Update submodules or sync CodeGraph" \
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
                printf "\nExiting MAVLink Simulator CLI. Goodbye!\n"
                exit 0
                ;;
        esac
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
    sync-graph)
        shift
        sync_codegraph "$@"
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

