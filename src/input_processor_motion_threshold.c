/*
 * Pointer motion threshold gate for ZMK.
 *
 * Events are blocked until cumulative |X| + |Y| reaches the threshold.
 * The crossing event is then passed to the next processor, which should be
 * ZMK's official zmk,input-processor-temp-layer.
 *
 * Once the target layer is active, all motion passes through immediately.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_motion_threshold

#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <drivers/input_processor.h>
#include <zmk/keymap.h>

struct motion_threshold_config {
    int32_t threshold;
    int32_t reset_timeout_ms;
};

struct motion_threshold_data {
    struct k_mutex lock;
    int64_t last_movement_timestamp;
    int32_t accumulated_movement;
};

static int motion_threshold_handle_event(const struct device *dev,
                                         struct input_event *event,
                                         uint32_t target_layer,
                                         uint32_t param2,
                                         struct zmk_input_processor_state *state) {
    ARG_UNUSED(param2);
    ARG_UNUSED(state);

    /*
     * Only relative X/Y movement participates in the threshold.
     * Buttons and wheel events continue normally.
     */
    if (event->type != INPUT_EV_REL ||
        (event->code != INPUT_REL_X && event->code != INPUT_REL_Y)) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    struct motion_threshold_data *data = dev->data;
    const struct motion_threshold_config *cfg = dev->config;

    /*
     * The parameter supplied from the keymap is a layer index.
     * ZMK's state query expects the internal layer ID.
     */
    if (zmk_keymap_layer_active(zmk_keymap_layer_index_to_id(target_layer))) {
        if (k_mutex_lock(&data->lock, K_FOREVER) == 0) {
            data->accumulated_movement = 0;
            data->last_movement_timestamp = k_uptime_get();
            k_mutex_unlock(&data->lock);
        }

        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (k_mutex_lock(&data->lock, K_FOREVER) < 0) {
        return ZMK_INPUT_PROC_STOP;
    }

    int64_t now = k_uptime_get();

    if ((now - data->last_movement_timestamp) > cfg->reset_timeout_ms) {
        data->accumulated_movement = 0;
    }

    data->last_movement_timestamp = now;
    data->accumulated_movement += abs(event->value);

    bool reached = data->accumulated_movement >= cfg->threshold;

    if (reached) {
        /*
         * Reset before passing the crossing event.
         * The next processor receives this event and activates the layer.
         */
        data->accumulated_movement = 0;
    }

    k_mutex_unlock(&data->lock);

    return reached ? ZMK_INPUT_PROC_CONTINUE : ZMK_INPUT_PROC_STOP;
}

static int motion_threshold_init(const struct device *dev) {
    struct motion_threshold_data *data = dev->data;

    k_mutex_init(&data->lock);
    data->last_movement_timestamp = 0;
    data->accumulated_movement = 0;

    return 0;
}

static const struct zmk_input_processor_driver_api motion_threshold_driver_api = {
    .handle_event = motion_threshold_handle_event,
};

#define MOTION_THRESHOLD_INST(n)                                                   \
    static struct motion_threshold_data motion_threshold_data_##n = {};            \
    static const struct motion_threshold_config motion_threshold_config_##n = {    \
        .threshold = DT_INST_PROP(n, threshold),                                   \
        .reset_timeout_ms = DT_INST_PROP_OR(n, reset_timeout_ms, 200),             \
    };                                                                              \
    DEVICE_DT_INST_DEFINE(n, motion_threshold_init, NULL,                          \
                          &motion_threshold_data_##n,                               \
                          &motion_threshold_config_##n, POST_KERNEL,                \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                      \
                          &motion_threshold_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MOTION_THRESHOLD_INST)
