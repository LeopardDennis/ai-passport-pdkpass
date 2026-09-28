#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware|--battery-diagnostics]" >&2
}

run_static_checks() {
    local actionlint_bin
    local test_dir

    python3 tools/check_repo.py
    python3 tools/check_circuit_assets.py

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_pdkpass_model.c main/pdkpass_model.c \
        -o "${test_dir}/test_pdkpass_model"
    "${test_dir}/test_pdkpass_model"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_pdkpass_schedule.c main/pdkpass_schedule.c main/pdkpass_data.c \
        -o "${test_dir}/test_pdkpass_schedule"
    "${test_dir}/test_pdkpass_schedule"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain -Itools/pdkpass-simulator/stubs \
        tests/test_pdkpass_calendar.c main/pdkpass_calendar.c main/pdkpass_data.c \
        main/pdkpass_tracks.c main/pdkpass_schedule.c main/pdkpass_season_core.c \
        -o "${test_dir}/test_pdkpass_calendar"
    "${test_dir}/test_pdkpass_calendar"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_pdkpass_tracks.c main/pdkpass_tracks.c \
        -o "${test_dir}/test_pdkpass_tracks"
    "${test_dir}/test_pdkpass_tracks"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_pdkpass_theme.c main/pdkpass_theme.c main/pdkpass_tracks.c \
        -o "${test_dir}/test_pdkpass_theme"
    "${test_dir}/test_pdkpass_theme"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_pdkpass_results_core.c main/pdkpass_results_core.c \
        -o "${test_dir}/test_pdkpass_results_core"
    "${test_dir}/test_pdkpass_results_core"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_pdkpass_sound_core.c main/pdkpass_sound_core.c \
        -o "${test_dir}/test_pdkpass_sound_core"
    "${test_dir}/test_pdkpass_sound_core"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_pdkpass_season_core.c main/pdkpass_season_core.c \
        -o "${test_dir}/test_pdkpass_season_core"
    "${test_dir}/test_pdkpass_season_core"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_pdkpass_wifi_form.c main/pdkpass_wifi_form.c \
        -o "${test_dir}/test_pdkpass_wifi_form"
    "${test_dir}/test_pdkpass_wifi_form"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_pdkpass_wifi_qr.c main/pdkpass_wifi_qr.c \
        -o "${test_dir}/test_pdkpass_wifi_qr"
    "${test_dir}/test_pdkpass_wifi_qr"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_pdkpass_wifi_profiles.c main/pdkpass_wifi_profiles.c \
        -o "${test_dir}/test_pdkpass_wifi_profiles"
    "${test_dir}/test_pdkpass_wifi_profiles"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_pdkpass_json_stream.c main/pdkpass_json_stream.c \
        -o "${test_dir}/test_pdkpass_json_stream"
    "${test_dir}/test_pdkpass_json_stream"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_pdkpass_reminder_core.c main/pdkpass_reminder_core.c \
        -o "${test_dir}/test_pdkpass_reminder_core"
    "${test_dir}/test_pdkpass_reminder_core"
    python3 tests/test_pdkpass_services.py
    python3 tests/test_verify_firmware.py
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

run_firmware_checks() (
    local validation_build_dir
    local variant="${1:-normal}"
    local defaults_file="${repo_root}/sdkconfig.defaults"
    local artifact="FoloToy-AI-Passport-full.bin"

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    if [[ "${variant}" == "battery-diagnostics" ]]; then
        defaults_file="${validation_build_dir}/sdkconfig.defaults"
        cp "${repo_root}/sdkconfig.defaults" "${defaults_file}"
        printf '\nCONFIG_PDKPASS_BATTERY_DIAGNOSTICS=y\n' >> "${defaults_file}"
        artifact="FoloToy-AI-Passport-battery-diagnostics-full.bin"
    fi
    SDKCONFIG_DEFAULTS="${defaults_file}" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    if [[ "${variant}" == "battery-diagnostics" ]]; then
        grep -qx 'CONFIG_PDKPASS_BATTERY_DIAGNOSTICS=y' "${validation_build_dir}/sdkconfig"
        echo "Battery diagnostics: enabled (60-second read-only sampling)"
    fi
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/${artifact}"
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --battery-diagnostics)
        run_static_checks
        run_firmware_checks battery-diagnostics
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
