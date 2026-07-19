/*
 * roBa legacy PMW3610 automouse exit listener
 *
 * Behavior:
 * - PMW3610 movement activates layer 4 through the external legacy driver.
 * - Positions used for MB1-MB5 keep layer 4 active.
 * - Pressing any other physical key immediately deactivates layer 4.
 * - Key release does not change the layer.
 *
 * Excluded physical positions:
 *   6 = W
 *   7 = R
 *   8 = Y
 *  18 = T
 *  19 = N
 *
 * These positions cover:
 *   T       -> MB1
 *   N       -> MB2
 *   T + N   -> MB3
 *   W + R   -> MB4
 *   R + Y   -> MB5
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>

LOG_MODULE_REGISTER(roba_automouse_exit, CONFIG_ZMK_LOG_LEVEL);

#define ROBA_MOUSE_LAYER 4

static bool roba_is_mouse_button_position(uint32_t position) {
    switch (position) {
    case 6:
    case 7:
    case 8:
    case 18:
    case 19:
        return true;
    default:
        return false;
    }
}

static int roba_automouse_exit_listener(const zmk_event_t *eh) {
    struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);

    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (!zmk_keymap_layer_active(ROBA_MOUSE_LAYER)) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (roba_is_mouse_button_position(ev->position)) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    LOG_DBG("Deactivate automouse layer on position %u", ev->position);
    zmk_keymap_layer_deactivate(ROBA_MOUSE_LAYER);

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(roba_automouse_exit, roba_automouse_exit_listener);
ZMK_SUBSCRIPTION(roba_automouse_exit, zmk_position_state_changed);
