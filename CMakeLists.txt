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



# ------------------------------------------------------------
# Patch D: always release PMW3610 chip-select after SPI failure.
#
# The pinned driver has several error paths that return while CS is still
# asserted. Patch the exact known paths only. If any path changes, stop the
# build instead of modifying unknown source.
# ------------------------------------------------------------

set(ROBA_REG_READ_WRITE_OLD
[=[        LOG_ERR("Reg read failed on SPI write");
        return err;]=]
)
set(ROBA_REG_READ_WRITE_NEW
[=[        LOG_ERR("Reg read failed on SPI write");
        spi_cs_ctrl(dev, false);
        return err;]=]
)

set(ROBA_REG_READ_READ_OLD
[=[        LOG_ERR("Reg read failed on SPI read");
        return err;]=]
)
set(ROBA_REG_READ_READ_NEW
[=[        LOG_ERR("Reg read failed on SPI read");
        spi_cs_ctrl(dev, false);
        return err;]=]
)

set(ROBA_REG_WRITE_OLD
[=[        LOG_ERR("Reg write failed on SPI write");
        return err;]=]
)
set(ROBA_REG_WRITE_NEW
[=[        LOG_ERR("Reg write failed on SPI write");
        spi_cs_ctrl(dev, false);
        return err;]=]
)

set(ROBA_MOTION_WRITE_OLD
[=[        LOG_ERR("Motion burst failed on SPI write");
        return err;]=]
)
set(ROBA_MOTION_WRITE_NEW
[=[        LOG_ERR("Motion burst failed on SPI write");
        spi_cs_ctrl(dev, false);
        return err;]=]
)

set(ROBA_MOTION_READ_OLD
[=[        LOG_ERR("Motion burst failed on SPI read");
        return err;]=]
)
set(ROBA_MOTION_READ_NEW
[=[        LOG_ERR("Motion burst failed on SPI read");
        spi_cs_ctrl(dev, false);
        return err;]=]
)

foreach(_pair
    REG_READ_WRITE
    REG_READ_READ
    REG_WRITE
    MOTION_WRITE
    MOTION_READ
)
    set(_old "${ROBA_${_pair}_OLD}")
    set(_new "${ROBA_${_pair}_NEW}")

    string(FIND "${ROBA_PMW3610_TEXT}" "${_new}" _already)
    if(_already EQUAL -1)
        string(FIND "${ROBA_PMW3610_TEXT}" "${_old}" _found)
        if(_found EQUAL -1)
            message(FATAL_ERROR
                "Expected PMW3610 SPI error path ${_pair} was not found. "
                "Refusing to patch an unknown driver."
            )
        endif()

        string(REPLACE
            "${_old}"
            "${_new}"
            ROBA_PMW3610_TEXT
            "${ROBA_PMW3610_TEXT}"
        )
    endif()
endforeach()

message(STATUS "roBa: PMW3610 SPI error paths now release CS")

# ------------------------------------------------------------
# Patch E: automatically restart PMW3610 after a report/SPI failure.
#
# gpio callback disables the sensor IRQ before trigger_work runs.
# On report failure, keep IRQ disabled, release CS, reset the driver's
# asynchronous init state, and start reinitialization after 10 ms.
# The normal init sequence re-enables IRQ after successful initialization.
# ------------------------------------------------------------

set(ROBA_WORK_ORIGINAL
[=[static void pmw3610_work_callback(struct k_work *work) {
    struct pixart_data *data = CONTAINER_OF(work, struct pixart_data, trigger_work);
    const struct device *dev = data->dev;

    pmw3610_report_data(dev);
    set_interrupt(dev, true);
}]=]
)

set(ROBA_WORK_PATCHED
[=[static void pmw3610_work_callback(struct k_work *work) {
    struct pixart_data *data = CONTAINER_OF(work, struct pixart_data, trigger_work);
    const struct device *dev = data->dev;

    int err = pmw3610_report_data(dev);
    if (err) {
        LOG_ERR("PMW3610 report failed (%d); restarting sensor", err);

        spi_cs_ctrl(dev, false);
        data->ready = false;
        data->err = 0;
        data->async_init_step = ASYNC_INIT_STEP_POWER_UP;

        k_work_schedule(&data->init_work, K_MSEC(10));
        return;
    }

    set_interrupt(dev, true);
}]=]
)

string(FIND "${ROBA_PMW3610_TEXT}" "PMW3610 report failed (%d); restarting sensor" ROBA_WORK_ALREADY_PATCHED)

if(ROBA_WORK_ALREADY_PATCHED EQUAL -1)
    string(FIND "${ROBA_PMW3610_TEXT}" "${ROBA_WORK_ORIGINAL}" ROBA_WORK_FOUND)
    if(ROBA_WORK_FOUND EQUAL -1)
        message(FATAL_ERROR
            "Expected PMW3610 work callback was not found. "
            "Refusing to patch an unknown driver."
        )
    endif()

    string(REPLACE
        "${ROBA_WORK_ORIGINAL}"
        "${ROBA_WORK_PATCHED}"
        ROBA_PMW3610_TEXT
        "${ROBA_PMW3610_TEXT}"
    )
endif()

message(STATUS "roBa: PMW3610 automatic recovery enabled")

# ------------------------------------------------------------
# Patch F: retry PMW3610 initialization if recovery itself fails.
# ------------------------------------------------------------

set(ROBA_INIT_FAIL_ORIGINAL
[=[    if (data->err) {
        LOG_ERR("PMW3610 initialization failed");
    } else {]=]
)

set(ROBA_INIT_FAIL_PATCHED
[=[    if (data->err) {
        LOG_ERR("PMW3610 initialization failed; retrying");

        spi_cs_ctrl(dev, false);
        data->ready = false;
        data->err = 0;
        data->async_init_step = ASYNC_INIT_STEP_POWER_UP;

        k_work_schedule(&data->init_work, K_MSEC(100));
    } else {]=]
)

string(FIND "${ROBA_PMW3610_TEXT}" "PMW3610 initialization failed; retrying" ROBA_INIT_RETRY_ALREADY_PATCHED)

if(ROBA_INIT_RETRY_ALREADY_PATCHED EQUAL -1)
    string(FIND "${ROBA_PMW3610_TEXT}" "${ROBA_INIT_FAIL_ORIGINAL}" ROBA_INIT_FAIL_FOUND)
    if(ROBA_INIT_FAIL_FOUND EQUAL -1)
        message(FATAL_ERROR
            "Expected PMW3610 init failure block was not found. "
            "Refusing to patch an unknown driver."
        )
    endif()

    string(REPLACE
        "${ROBA_INIT_FAIL_ORIGINAL}"
        "${ROBA_INIT_FAIL_PATCHED}"
        ROBA_PMW3610_TEXT
        "${ROBA_PMW3610_TEXT}"
    )
endif()

message(STATUS "roBa: PMW3610 initialization retry enabled")


# ------------------------------------------------------------
# Patch G: give PMW3610 motion handling its own workqueue.
#
# The original driver disables the PMW3610 IRQ and submits trigger_work to
# Zephyr's shared system workqueue. If that queue is delayed, the sensor IRQ
# remains disabled until pmw3610_work_callback() finally runs.
#
# Use a dedicated PMW3610 workqueue for trigger_work only.
# The asynchronous sensor initialization remains on the normal delayable
# workqueue, so existing initialization/recovery behavior is preserved.
# ------------------------------------------------------------

set(ROBA_WQ_LOG_ORIGINAL
[=[LOG_MODULE_REGISTER(pmw3610, CONFIG_INPUT_LOG_LEVEL);]=]
)

set(ROBA_WQ_LOG_PATCHED
[=[LOG_MODULE_REGISTER(pmw3610, CONFIG_INPUT_LOG_LEVEL);

/*
 * roBa: dedicated PMW3610 motion workqueue.
 */
#define ROBA_PMW3610_WORKQ_STACK_SIZE 1024
#define ROBA_PMW3610_WORKQ_PRIORITY K_PRIO_PREEMPT(5)

K_THREAD_STACK_DEFINE(roba_pmw3610_workq_stack, ROBA_PMW3610_WORKQ_STACK_SIZE);
static struct k_work_q roba_pmw3610_work_q;
static bool roba_pmw3610_work_q_started;]=]
)

string(FIND
    "${ROBA_PMW3610_TEXT}"
    "K_THREAD_STACK_DEFINE(roba_pmw3610_workq_stack"
    ROBA_WQ_GLOBALS_ALREADY_PATCHED
)

if(ROBA_WQ_GLOBALS_ALREADY_PATCHED EQUAL -1)
    string(FIND
        "${ROBA_PMW3610_TEXT}"
        "${ROBA_WQ_LOG_ORIGINAL}"
        ROBA_WQ_LOG_FOUND
    )
    if(ROBA_WQ_LOG_FOUND EQUAL -1)
        message(FATAL_ERROR
            "Expected PMW3610 LOG_MODULE_REGISTER line was not found. "
            "Refusing to add the dedicated workqueue to an unknown driver."
        )
    endif()

    string(REPLACE
        "${ROBA_WQ_LOG_ORIGINAL}"
        "${ROBA_WQ_LOG_PATCHED}"
        ROBA_PMW3610_TEXT
        "${ROBA_PMW3610_TEXT}"
    )
endif()

set(ROBA_WQ_SUBMIT_ORIGINAL
[=[    k_work_submit(&data->trigger_work);]=]
)

set(ROBA_WQ_SUBMIT_PATCHED
[=[    k_work_submit_to_queue(&roba_pmw3610_work_q, &data->trigger_work);]=]
)

string(FIND
    "${ROBA_PMW3610_TEXT}"
    "${ROBA_WQ_SUBMIT_PATCHED}"
    ROBA_WQ_SUBMIT_ALREADY_PATCHED
)

if(ROBA_WQ_SUBMIT_ALREADY_PATCHED EQUAL -1)
    string(FIND
        "${ROBA_PMW3610_TEXT}"
        "${ROBA_WQ_SUBMIT_ORIGINAL}"
        ROBA_WQ_SUBMIT_FOUND
    )
    if(ROBA_WQ_SUBMIT_FOUND EQUAL -1)
        message(FATAL_ERROR
            "Expected PMW3610 trigger_work submit line was not found. "
            "Refusing to patch an unknown driver."
        )
    endif()

    string(REPLACE
        "${ROBA_WQ_SUBMIT_ORIGINAL}"
        "${ROBA_WQ_SUBMIT_PATCHED}"
        ROBA_PMW3610_TEXT
        "${ROBA_PMW3610_TEXT}"
    )
endif()

set(ROBA_WQ_INIT_ORIGINAL
[=[    // init trigger handler work
    k_work_init(&data->trigger_work, pmw3610_work_callback);]=]
)

set(ROBA_WQ_INIT_PATCHED
[=[    // Start the dedicated PMW3610 motion queue before enabling the IRQ.
    if (!roba_pmw3610_work_q_started) {
        k_work_queue_start(&roba_pmw3610_work_q,
                           roba_pmw3610_workq_stack,
                           K_THREAD_STACK_SIZEOF(roba_pmw3610_workq_stack),
                           ROBA_PMW3610_WORKQ_PRIORITY,
                           NULL);
        roba_pmw3610_work_q_started = true;
    }

    // init trigger handler work
    k_work_init(&data->trigger_work, pmw3610_work_callback);]=]
)

string(FIND
    "${ROBA_PMW3610_TEXT}"
    "k_work_queue_start(&roba_pmw3610_work_q"
    ROBA_WQ_INIT_ALREADY_PATCHED
)

if(ROBA_WQ_INIT_ALREADY_PATCHED EQUAL -1)
    string(FIND
        "${ROBA_PMW3610_TEXT}"
        "${ROBA_WQ_INIT_ORIGINAL}"
        ROBA_WQ_INIT_FOUND
    )
    if(ROBA_WQ_INIT_FOUND EQUAL -1)
        message(FATAL_ERROR
            "Expected PMW3610 trigger_work initialization block was not found. "
            "Refusing to patch an unknown driver."
        )
    endif()

    string(REPLACE
        "${ROBA_WQ_INIT_ORIGINAL}"
        "${ROBA_WQ_INIT_PATCHED}"
        ROBA_PMW3610_TEXT
        "${ROBA_PMW3610_TEXT}"
    )
endif()

message(STATUS "roBa: PMW3610 dedicated motion workqueue enabled")

file(WRITE "${ROBA_PMW3610_SOURCE}" "${ROBA_PMW3610_TEXT}")

message(STATUS "roBa: legacy PMW3610 timeout disabled")
message(STATUS "roBa: typing guard added to automouse activation")

endif() # CONFIG_PMW3610
