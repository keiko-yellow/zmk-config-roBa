/*
 * roBa legacy PMW3610 automouse controller
 *
 * Behavior:
 * - PMW3610 movement activates layer 4 through the legacy driver.
 * - Time alone never deactivates layer 4.
 * - MB1-MB5 positions keep layer 4 active.
 * - Held Ctrl and Shift mod-taps keep layer 4 active.
 * - Tapped Ctrl/Shift mod-taps (E/H/Space/Enter) are treated as normal keys
 *   and deactivate layer 4 on physical key release.
 * - Any other normal key immediately deactivates layer 4.
 * - After a normal key press, pointer movement continues, but automouse
 *   activation is blocked for CONFIG_ROBA_AUTOMOUSE_TYPING_GUARD_MS.
 *
 * Modifier mod-tap positions:
 * - Position 10 = &ctrl_mt LCTRL E
 * - Position 21 = &ctrl_mt RCTRL H
 * - Position 38 = &shift_mt LSHIFT SPACE
 * - Position 41 = &shift_mt RIGHT_SHIFT ENTER
 *
 * A modifier mod-tap press is not treated as a normal key immediately.
 * If ZMK resolves it as a held modifier, layer 4 stays active so
 * Ctrl+click/drag and Shift+click work.
 *
 * For very fast modifier + mouse-button chords, the physical mouse-button
 * press also marks every pending Ctrl/Shift candidate as a hold. This avoids
 * a race where layer 4 is deactivated before the delayed T/N combo decision
 * has finished.
 *
 * If a modifier key is tapped without a mouse-button press, the tap
 * (E/H/Space/Enter) is treated as normal input and exits layer 4.
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <dt-bindings/zmk/modifiers.h>

#include <zmk/event_manager.h>
#include <zmk/events/modifiers_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>

LOG_MODULE_REGISTER(roba_automouse, CONFIG_ZMK_LOG_LEVEL);

#define ROBA_MOUSE_LAYER 4

#define ROBA_LEFT_CTRL_POSITION 10
#define ROBA_RIGHT_CTRL_POSITION 21
#define ROBA_LEFT_SHIFT_POSITION 38
#define ROBA_RIGHT_SHIFT_POSITION 41

static atomic_t typing_guard_active = ATOMIC_INIT(0);
static atomic_t last_normal_key_time = ATOMIC_INIT(0);

static bool left_ctrl_candidate;
static bool right_ctrl_candidate;
static bool left_shift_candidate;
static bool right_shift_candidate;

static bool left_ctrl_resolved_as_hold;
static bool right_ctrl_resolved_as_hold;
static bool left_shift_resolved_as_hold;
static bool right_shift_resolved_as_hold;

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

static void roba_mark_pending_modifiers_as_mouse_hold(void) {
    if (left_ctrl_candidate) {
        left_ctrl_resolved_as_hold = true;
    }

    if (right_ctrl_candidate) {
        right_ctrl_resolved_as_hold = true;
    }

    if (left_shift_candidate) {
        left_shift_resolved_as_hold = true;
    }

    if (right_shift_candidate) {
        right_shift_resolved_as_hold = true;
    }
}

/*
 * Called by the patched PMW3610 driver immediately before layer 4 activation.
 * Cursor movement itself is never blocked.
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

static void roba_exit_mouse_layer_for_normal_key(void) {
    roba_start_typing_guard();

    if (zmk_keymap_layer_active(ROBA_MOUSE_LAYER)) {
        zmk_keymap_layer_deactivate(ROBA_MOUSE_LAYER);
    }
}

/*
 * Record when Ctrl/Shift mod-taps have actually resolved to held modifiers.
 * Merely pressing their physical positions does not set these flags.
 */
static int roba_modifier_listener(const zmk_event_t *eh) {
    struct zmk_modifiers_state_changed *ev = as_zmk_modifiers_state_changed(eh);

    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (left_ctrl_candidate && (ev->modifiers & MOD_LCTL)) {
        left_ctrl_resolved_as_hold = true;
    }

    if (right_ctrl_candidate && (ev->modifiers & MOD_RCTL)) {
        right_ctrl_resolved_as_hold = true;
    }

    if (left_shift_candidate && (ev->modifiers & MOD_LSFT)) {
        left_shift_resolved_as_hold = true;
    }

    if (right_shift_candidate && (ev->modifiers & MOD_RSFT)) {
        right_shift_resolved_as_hold = true;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

static int roba_position_listener(const zmk_event_t *eh) {
    struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);

    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    /*
     * Modifier mod-tap press:
     * Do not start the typing guard and do not exit layer 4 yet.
     */
    if (ev->state && ev->position == ROBA_LEFT_CTRL_POSITION) {
        left_ctrl_candidate = true;
        left_ctrl_resolved_as_hold = false;
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->state && ev->position == ROBA_RIGHT_CTRL_POSITION) {
        right_ctrl_candidate = true;
        right_ctrl_resolved_as_hold = false;
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->state && ev->position == ROBA_LEFT_SHIFT_POSITION) {
        left_shift_candidate = true;
        left_shift_resolved_as_hold = false;
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->state && ev->position == ROBA_RIGHT_SHIFT_POSITION) {
        right_shift_candidate = true;
        right_shift_resolved_as_hold = false;
        return ZMK_EV_EVENT_BUBBLE;
    }

    /*
     * Modifier mod-tap release:
     * - held modifier: preserve automouse layer 4
     * - tapped E/H/Space/Enter: treat as a normal typed key
     */
    if (!ev->state && ev->position == ROBA_LEFT_CTRL_POSITION) {
        bool was_hold = left_ctrl_resolved_as_hold;
        left_ctrl_candidate = false;
        left_ctrl_resolved_as_hold = false;

        if (!was_hold) {
            roba_exit_mouse_layer_for_normal_key();
        }

        return ZMK_EV_EVENT_BUBBLE;
    }

    if (!ev->state && ev->position == ROBA_RIGHT_CTRL_POSITION) {
        bool was_hold = right_ctrl_resolved_as_hold;
        right_ctrl_candidate = false;
        right_ctrl_resolved_as_hold = false;

        if (!was_hold) {
            roba_exit_mouse_layer_for_normal_key();
        }

        return ZMK_EV_EVENT_BUBBLE;
    }

    if (!ev->state && ev->position == ROBA_LEFT_SHIFT_POSITION) {
        bool was_hold = left_shift_resolved_as_hold;
        left_shift_candidate = false;
        left_shift_resolved_as_hold = false;

        if (!was_hold) {
            roba_exit_mouse_layer_for_normal_key();
        }

        return ZMK_EV_EVENT_BUBBLE;
    }

    if (!ev->state && ev->position == ROBA_RIGHT_SHIFT_POSITION) {
        bool was_hold = right_shift_resolved_as_hold;
        right_shift_candidate = false;
        right_shift_resolved_as_hold = false;

        if (!was_hold) {
            roba_exit_mouse_layer_for_normal_key();
        }

        return ZMK_EV_EVENT_BUBBLE;
    }

    if (!ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    bool mouse_layer_active = zmk_keymap_layer_active(ROBA_MOUSE_LAYER);

    /*
     * W/R/Y/T/N are mouse-button positions only while layer 4 is active.
     * On the base layer they remain normal typing keys.
     */
    if (mouse_layer_active && roba_is_mouse_button_position(ev->position)) {
        roba_mark_pending_modifiers_as_mouse_hold();
        return ZMK_EV_EVENT_BUBBLE;
    }

    roba_exit_mouse_layer_for_normal_key();

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(roba_automouse_position, roba_position_listener);
ZMK_SUBSCRIPTION(roba_automouse_position, zmk_position_state_changed);

ZMK_LISTENER(roba_automouse_modifier, roba_modifier_listener);
ZMK_SUBSCRIPTION(roba_automouse_modifier, zmk_modifiers_state_changed);
