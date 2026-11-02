#!/usr/bin/env bash
# Launch the existing desktop build using its configured Qt installation.
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${MAVLINK_BUILD_DIR:-$project_dir/build}"
if [[ "$build_dir" != /* ]]; then
    build_dir="$project_dir/$build_dir"
fi
app="$build_dir/mavlink-simulator"
two_instances=false

case "${1:-}" in
    --help|-h)
        printf 'Usage: %s [--two] [Qt application arguments...]\n' "$0"
        printf '  --two  Open two independent application windows (no virtual UART setup).\n'
        printf '  MAVLINK_BUILD_DIR can select another build directory.\n'
        exit 0
        ;;
    --two)
        two_instances=true
        shift
        ;;
esac

if [[ ! -x "$app" ]]; then
    printf 'Application not found: %s\nBuild it with cmake first; see README.md.\n' "$app" >&2
    exit 1
fi

# Derive the matching runtime from CMake instead of embedding machine-specific paths.
cache="$build_dir/CMakeCache.txt"
if [[ -r "$cache" ]]; then
    qt_cmake_dir="$(sed -n 's/^Qt6Core_DIR:PATH=//p' "$cache")"
    if [[ -n "$qt_cmake_dir" ]]; then
        if [[ ! -d "$qt_cmake_dir/../.." ]]; then
            printf 'Configured Qt installation is missing. Reconfigure and rebuild with an available Qt installation.\n' >&2
            exit 1
        fi
        qt_lib_dir="$(cd -- "$qt_cmake_dir/../.." && pwd)"
        if [[ ! -r "$qt_lib_dir/libQt6Core.so.6" ]]; then
            printf 'Qt runtime not found in %s. Reconfigure and rebuild with a complete Qt installation.\n' "$qt_lib_dir" >&2
            exit 1
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

if ! "$two_instances"; then
    exec "$app" "$@"
fi

"$app" "$@" &
first_pid=$!
"$app" "$@" &
second_pid=$!

# @brief Stop only this launcher's children when the terminal interrupts the pair.
stop_instances()
{
    trap - INT TERM
    kill "$first_pid" "$second_pid" 2>/dev/null || true
    wait "$first_pid" "$second_pid" 2>/dev/null || true
    exit 130
}

trap stop_instances INT TERM
status=0
wait "$first_pid" || status=$?
wait "$second_pid" || status=$?
exit "$status"
