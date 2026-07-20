/*
 * roBa legacy PMW3610 automouse controller
 *
 * Behavior:
 * - PMW3610 movement activates layer 4 through the legacy driver.
 * - Time alone never deactivates layer 4.
 * - MB1-MB5 positions keep layer 4 active.
 * - Any other pressed key immediately deactivates layer 4.
 * - After a normal key press, movement still moves the cursor, but cannot
 *   activate layer 4 until CONFIG_ROBA_AUTOMOUSE_TYPING_GUARD_MS has elapsed.
 *
 * Mouse-button physical positions:
 *   6 = W
 *   7 = R
 *   8 = Y
 *  18 = T
 *  19 = N
 *
 * Mouse bindings:
 *   T       -> MB1
 *   N       -> MB2
 *   T + N   -> MB3
 *   W + R   -> MB4
 *   R + Y   -> MB5
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>

LOG_MODULE_REGISTER(roba_automouse, CONFIG_ZMK_LOG_LEVEL);

#define ROBA_MOUSE_LAYER 4

static atomic_t typing_guard_active = ATOMIC_INIT(0);
static atomic_t last_normal_key_time = ATOMIC_INIT(0);

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

/*
 * Called by the patched PMW3610 driver immediately before layer 4 activation.
 *
 * Cursor movement itself is never blocked. This function controls only
 * whether the movement may activate the automouse layer.
 */
bool roba_automouse_allowed(void) {
    if (!atomic_get(&typing_guard_active)) {
        return true;
    }

    uint32_t now = k_uptime_get_32();
    uint32_t last = (uint32_t)atomic_get(&last_normal_key_time);
    uint32_t elapsed = now - last;

    if (elapsed >= CONFIG_ROBA_AUTOMOUSE_TYPING_GUARD_MS) {
        atomic_clear(&typing_guard_active);
        return true;
    }

    return false;
}

static void roba_start_typing_guard(void) {
    atomic_set(&last_normal_key_time, (atomic_val_t)k_uptime_get_32());
    atomic_set(&typing_guard_active, 1);
}

static int roba_automouse_listener(const zmk_event_t *eh) {
    struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);

    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    bool mouse_layer_active = zmk_keymap_layer_active(ROBA_MOUSE_LAYER);

    /*
     * Only while layer 4 is already active do W/R/Y/T/N count as mouse
     * buttons. On the base layer, those same physical keys are ordinary
     * typing keys and must start the guard.
     */
    if (mouse_layer_active && roba_is_mouse_button_position(ev->position)) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    /*
     * Every normal key press restarts the typing guard. This includes
     * W/R/Y/T/N when layer 4 is not active.
     */
    roba_start_typing_guard();

    if (mouse_layer_active) {
        LOG_DBG("Deactivate automouse layer on position %u", ev->position);
        zmk_keymap_layer_deactivate(ROBA_MOUSE_LAYER);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(roba_automouse, roba_automouse_listener);
ZMK_SUBSCRIPTION(roba_automouse, zmk_position_state_changed);
