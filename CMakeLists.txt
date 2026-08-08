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


# ------------------------------------------------------------
# Patch C: prevent PMW3610 pointer reporting from blocking forever.
#
# This code is inside if(CONFIG_PMW3610), so the left half does not
# search for or patch the PMW3610 driver at all.
#
# The pinned external driver contains exactly four K_FOREVER timeouts
# in input_report_rel() calls (X, Y, wheel, horizontal wheel).
# If that exact structure changes, stop the build instead of guessing.
# ------------------------------------------------------------
string(REGEX MATCHALL "K_FOREVER" ROBA_PMW3610_FOREVER_MATCHES "${ROBA_PMW3610_TEXT}")
list(LENGTH ROBA_PMW3610_FOREVER_MATCHES ROBA_PMW3610_FOREVER_COUNT)

if(NOT ROBA_PMW3610_FOREVER_COUNT EQUAL 4)
    message(FATAL_ERROR
        "Expected exactly 4 K_FOREVER occurrences in the pinned PMW3610 driver, "
        "but found ${ROBA_PMW3610_FOREVER_COUNT}. Refusing to patch an unknown driver."
    )
endif()

string(REPLACE
    "K_FOREVER"
    "K_NO_WAIT"
    ROBA_PMW3610_TEXT
    "${ROBA_PMW3610_TEXT}"
)

message(STATUS "roBa: PMW3610 input reports changed to K_NO_WAIT")

file(WRITE "${ROBA_PMW3610_SOURCE}" "${ROBA_PMW3610_TEXT}")

message(STATUS "roBa: legacy PMW3610 timeout disabled")
message(STATUS "roBa: typing guard added to automouse activation")

endif() # CONFIG_PMW3610
