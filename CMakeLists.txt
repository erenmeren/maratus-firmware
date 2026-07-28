cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ditto-firmware)

# --- Guard: tools/patch-deps.sh must have been applied to managed_components ---
# managed_components/ is gitignored and re-fetched by the component manager, which
# silently drops our source patches. The lodepng PSRAM patch is the dangerous one:
# without it, intermediate decode buffers land in internal DRAM, a >=512px image
# needs ~1MB the ESP32-P4 doesn't have, and lodepng_decode fails INVISIBLY
# (CONFIG_LV_USE_LOG is off) — branding images just never appear on device. This
# regressed once already when the deps were re-fetched during an IDF upgrade, so
# fail the build rather than ship a firmware that cannot draw images.
set(DITTO_LODEPNG "${CMAKE_CURRENT_LIST_DIR}/managed_components/lvgl__lvgl/src/libs/lodepng/lodepng.c")
if(EXISTS "${DITTO_LODEPNG}")
    file(STRINGS "${DITTO_LODEPNG}" DITTO_LODEPNG_PATCHED
         REGEX "PATCH \\(Ditto\\) lodepng PSRAM" LIMIT_COUNT 1)
    if(NOT DITTO_LODEPNG_PATCHED)
        message(FATAL_ERROR
            "lodepng PSRAM patch is MISSING from managed_components.\n"
            "Images would silently fail to decode on device (no error is logged).\n"
            "Fix: run  tools/patch-deps.sh  then rebuild.")
    endif()
else()
    # Not fatal (the path could legitimately move), but never let the guard go
    # quietly dead — a silent no-op guard is how this bug came back.
    message(WARNING
        "Cannot verify the lodepng PSRAM patch: ${DITTO_LODEPNG} not found. "
        "If lvgl moved, update the guard path in CMakeLists.txt.")
endif()
