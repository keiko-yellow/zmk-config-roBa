# roBa user module
#
# 1. Compile the immediate-exit listener.
# 2. Disable only the legacy PMW3610 automouse timeout start.
#
# The external driver is pinned in config/west.yml. During CMake configure,
# west has already downloaded it into the workspace. We patch the fetched
# workspace copy, not the upstream GitHub repository.

zephyr_library()

# レイヤー管理APIは中央側だけにリンクされる。
# 左側ペリフェラルではこのCファイルをコンパイルしない。
zephyr_library_sources_ifdef(
    CONFIG_ZMK_SPLIT_ROLE_CENTRAL
    src/automouse_key_exit.c
)

# PMW3610ドライバも右側中央だけで使用する。
if(CONFIG_PMW3610)

set(ROBA_PMW3610_SOURCE
    "${ZEPHYR_BASE}/../zmk-pmw3610-driver/src/pmw3610.c"
)

if(NOT EXISTS "${ROBA_PMW3610_SOURCE}")
    # Fallbacks for slightly different west workspace layouts.
    set(_roba_pmw3610_candidates
        "${ZEPHYR_BASE}/../../zmk-pmw3610-driver/src/pmw3610.c"
        "${CMAKE_CURRENT_LIST_DIR}/../zmk-pmw3610-driver/src/pmw3610.c"
        "${CMAKE_CURRENT_LIST_DIR}/../../zmk-pmw3610-driver/src/pmw3610.c"
    )

    foreach(_candidate IN LISTS _roba_pmw3610_candidates)
        if(EXISTS "${_candidate}")
            set(ROBA_PMW3610_SOURCE "${_candidate}")
            break()
        endif()
    endforeach()
endif()

if(NOT EXISTS "${ROBA_PMW3610_SOURCE}")
    message(FATAL_ERROR
        "zmk-pmw3610-driver/src/pmw3610.c was not found. "
        "The legacy automouse timeout cannot be disabled safely."
    )
endif()

file(READ "${ROBA_PMW3610_SOURCE}" ROBA_PMW3610_TEXT)

set(ROBA_TIMEOUT_START
    "k_timer_start(&automouse_layer_timer, K_MSEC(CONFIG_PMW3610_AUTOMOUSE_TIMEOUT_MS), K_NO_WAIT);"
)
set(ROBA_TIMEOUT_DISABLED_MARKER
    "/* roBa: timeout disabled; exit is handled by automouse_key_exit.c */"
)

string(FIND "${ROBA_PMW3610_TEXT}" "${ROBA_TIMEOUT_DISABLED_MARKER}" ROBA_ALREADY_PATCHED)

if(ROBA_ALREADY_PATCHED EQUAL -1)
    string(FIND "${ROBA_PMW3610_TEXT}" "${ROBA_TIMEOUT_START}" ROBA_TIMEOUT_FOUND)

    if(ROBA_TIMEOUT_FOUND EQUAL -1)
        message(FATAL_ERROR
            "The pinned PMW3610 driver no longer contains the expected "
            "automouse timer start line. Refusing to build an unknown behavior."
        )
    endif()

    string(REPLACE
        "${ROBA_TIMEOUT_START}"
        "${ROBA_TIMEOUT_DISABLED_MARKER}"
        ROBA_PMW3610_PATCHED
        "${ROBA_PMW3610_TEXT}"
    )

    file(WRITE "${ROBA_PMW3610_SOURCE}" "${ROBA_PMW3610_PATCHED}")
    message(STATUS "roBa: legacy PMW3610 automouse timeout disabled")
else()
    message(STATUS "roBa: legacy PMW3610 automouse timeout already disabled")
endif()

endif() # CONFIG_PMW3610
