# roBa user module
#
# This module:
# 1. Compiles the automouse controller only on the split central.
# 2. Disables the legacy PMW3610 time-based layer timeout.
# 3. Adds the typing-idle guard before automouse layer activation.
#
# The external driver is pinned in config/west.yml. The fetched workspace
# copy is patched during CMake configuration. If the expected source text is
# missing, the build fails instead of silently producing different behavior.

zephyr_library()

zephyr_library_sources_ifdef(
    CONFIG_ZMK_SPLIT_ROLE_CENTRAL
    src/automouse_key_exit.c
)

if(CONFIG_PMW3610)

set(ROBA_PMW3610_SOURCE
    "${ZEPHYR_BASE}/../zmk-pmw3610-driver/src/pmw3610.c"
)

if(NOT EXISTS "${ROBA_PMW3610_SOURCE}")
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
        "The custom automouse behavior cannot be applied safely."
    )
endif()

file(READ "${ROBA_PMW3610_SOURCE}" ROBA_PMW3610_TEXT)

# ------------------------------------------------------------
# Patch A: disable time-based automouse layer deactivation.
# ------------------------------------------------------------
set(ROBA_TIMEOUT_START
    "k_timer_start(&automouse_layer_timer, K_MSEC(CONFIG_PMW3610_AUTOMOUSE_TIMEOUT_MS), K_NO_WAIT);"
)
set(ROBA_TIMEOUT_DISABLED_MARKER
    "/* roBa: timeout disabled; exit is handled by automouse_key_exit.c */"
)

string(FIND "${ROBA_PMW3610_TEXT}" "${ROBA_TIMEOUT_DISABLED_MARKER}" ROBA_TIMEOUT_ALREADY_PATCHED)

if(ROBA_TIMEOUT_ALREADY_PATCHED EQUAL -1)
    string(FIND "${ROBA_PMW3610_TEXT}" "${ROBA_TIMEOUT_START}" ROBA_TIMEOUT_FOUND)

    if(ROBA_TIMEOUT_FOUND EQUAL -1)
        message(FATAL_ERROR
            "Expected PMW3610 automouse timer start line was not found. "
            "Refusing to build unknown timeout behavior."
        )
    endif()

    string(REPLACE
        "${ROBA_TIMEOUT_START}"
        "${ROBA_TIMEOUT_DISABLED_MARKER}"
        ROBA_PMW3610_TEXT
        "${ROBA_PMW3610_TEXT}"
    )
endif()

# ------------------------------------------------------------
# Patch B: block layer activation during the typing guard.
#
# All pointer reports continue normally. Only layer activation is gated.
# ------------------------------------------------------------
set(ROBA_ACTIVATE_ORIGINAL
"static void activate_automouse_layer() {
    automouse_triggered = true;"
)

set(ROBA_ACTIVATE_PATCHED
"extern bool roba_automouse_allowed(void);

static void activate_automouse_layer() {
    if (!roba_automouse_allowed()) {
        return;
    }

    automouse_triggered = true;"
)

string(FIND "${ROBA_PMW3610_TEXT}" "extern bool roba_automouse_allowed(void);" ROBA_GUARD_ALREADY_PATCHED)

if(ROBA_GUARD_ALREADY_PATCHED EQUAL -1)
    string(FIND "${ROBA_PMW3610_TEXT}" "${ROBA_ACTIVATE_ORIGINAL}" ROBA_ACTIVATE_FOUND)

    if(ROBA_ACTIVATE_FOUND EQUAL -1)
        message(FATAL_ERROR
            "Expected PMW3610 activate_automouse_layer function was not found. "
            "Refusing to build without the typing guard."
        )
    endif()

    string(REPLACE
        "${ROBA_ACTIVATE_ORIGINAL}"
        "${ROBA_ACTIVATE_PATCHED}"
        ROBA_PMW3610_TEXT
        "${ROBA_PMW3610_TEXT}"
    )
endif()

file(WRITE "${ROBA_PMW3610_SOURCE}" "${ROBA_PMW3610_TEXT}")

message(STATUS "roBa: legacy PMW3610 timeout disabled")
message(STATUS "roBa: typing guard added to automouse activation")

endif() # CONFIG_PMW3610

# ------------------------------------------------------------
# Native Zephyr PMW3610 hardening
#
# Current "now" uses the Zephyr built-in PMW3610 driver:
#   ${ZEPHYR_BASE}/drivers/input/input_pmw3610.c
#
# That driver reports X/Y from a system work item with K_FOREVER.
# If the Zephyr input queue is temporarily full, the PMW3610 work item
# can wait indefinitely. Replace ONLY those two PMW3610 report timeouts
# with K_NO_WAIT. No overlay, SPI, CPI, automouse, or keymap behavior is changed.
# ------------------------------------------------------------

set(ROBA_NATIVE_PMW3610_SOURCE
    "${ZEPHYR_BASE}/drivers/input/input_pmw3610.c"
)

if(EXISTS "${ROBA_NATIVE_PMW3610_SOURCE}")
    file(READ "${ROBA_NATIVE_PMW3610_SOURCE}" ROBA_NATIVE_PMW3610_TEXT)

    set(ROBA_NATIVE_X_REPORT_OLD
        "input_report_rel(dev, cfg->axis_x, x, false, K_FOREVER);"
    )
    set(ROBA_NATIVE_X_REPORT_NEW
        "input_report_rel(dev, cfg->axis_x, x, false, K_NO_WAIT);"
    )

    set(ROBA_NATIVE_Y_REPORT_OLD
        "input_report_rel(dev, cfg->axis_y, y, true, K_FOREVER);"
    )
    set(ROBA_NATIVE_Y_REPORT_NEW
        "input_report_rel(dev, cfg->axis_y, y, true, K_NO_WAIT);"
    )

    string(FIND
        "${ROBA_NATIVE_PMW3610_TEXT}"
        "${ROBA_NATIVE_X_REPORT_NEW}"
        ROBA_NATIVE_X_ALREADY_PATCHED
    )
    if(ROBA_NATIVE_X_ALREADY_PATCHED EQUAL -1)
        string(FIND
            "${ROBA_NATIVE_PMW3610_TEXT}"
            "${ROBA_NATIVE_X_REPORT_OLD}"
            ROBA_NATIVE_X_FOUND
        )
        if(ROBA_NATIVE_X_FOUND EQUAL -1)
            message(FATAL_ERROR
                "Zephyr native PMW3610 X report line was not found. "
                "Refusing to patch an unknown driver version."
            )
        endif()
        string(REPLACE
            "${ROBA_NATIVE_X_REPORT_OLD}"
            "${ROBA_NATIVE_X_REPORT_NEW}"
            ROBA_NATIVE_PMW3610_TEXT
            "${ROBA_NATIVE_PMW3610_TEXT}"
        )
    endif()

    string(FIND
        "${ROBA_NATIVE_PMW3610_TEXT}"
        "${ROBA_NATIVE_Y_REPORT_NEW}"
        ROBA_NATIVE_Y_ALREADY_PATCHED
    )
    if(ROBA_NATIVE_Y_ALREADY_PATCHED EQUAL -1)
        string(FIND
            "${ROBA_NATIVE_PMW3610_TEXT}"
            "${ROBA_NATIVE_Y_REPORT_OLD}"
            ROBA_NATIVE_Y_FOUND
        )
        if(ROBA_NATIVE_Y_FOUND EQUAL -1)
            message(FATAL_ERROR
                "Zephyr native PMW3610 Y report line was not found. "
                "Refusing to patch an unknown driver version."
            )
        endif()
        string(REPLACE
            "${ROBA_NATIVE_Y_REPORT_OLD}"
            "${ROBA_NATIVE_Y_REPORT_NEW}"
            ROBA_NATIVE_PMW3610_TEXT
            "${ROBA_NATIVE_PMW3610_TEXT}"
        )
    endif()

    file(WRITE
        "${ROBA_NATIVE_PMW3610_SOURCE}"
        "${ROBA_NATIVE_PMW3610_TEXT}"
    )

    message(STATUS "roBa: Zephyr native PMW3610 input reports changed to K_NO_WAIT")
else()
    message(FATAL_ERROR
        "Zephyr native PMW3610 driver was not found at "
        "${ROBA_NATIVE_PMW3610_SOURCE}"
    )
endif()

