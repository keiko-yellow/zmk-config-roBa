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

#define ROBA_MOUSE_LAYER 3
#define ROBA_LEFT_CTRL_POSITION 10
#define ROBA_RIGHT_CTRL_POSITION 21
#define ROBA_LEFT_SHIFT_POSITION 38
#define ROBA_RIGHT_SHIFT_POSITION 41
#define ROBA_W_POSITION 6
#define ROBA_R_POSITION 7
#define ROBA_Y_POSITION 8
#define ROBA_T_POSITION 18
#define ROBA_N_POSITION 19
#define ROBA_MINUS_POSITION 14
#define ROBA_MOUSE_COMBO_TIMEOUT_MS 80
#define ROBA_MOUSE_COMBO_EXIT_DELAY_MS (ROBA_MOUSE_COMBO_TIMEOUT_MS + 5)

static atomic_t typing_guard_active = ATOMIC_INIT(0);
static atomic_t last_normal_key_time = ATOMIC_INIT(0);

static bool left_ctrl_candidate, right_ctrl_candidate;
static bool left_shift_candidate, right_shift_candidate;
static bool left_ctrl_resolved_as_hold, right_ctrl_resolved_as_hold;
static bool left_shift_resolved_as_hold, right_shift_resolved_as_hold;

static bool w_down, r_down, y_down;
static bool mouse_combo_active;

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

static bool roba_is_direct_mouse_button_position(uint32_t position) {
    return position == ROBA_T_POSITION || position == ROBA_N_POSITION;
}

static bool roba_is_mouse_combo_candidate(uint32_t position) {
    return position == ROBA_W_POSITION ||
           position == ROBA_R_POSITION ||
           position == ROBA_Y_POSITION;
}

static void roba_set_mouse_combo_candidate_down(uint32_t position, bool state) {
    switch (position) {
    case ROBA_W_POSITION: w_down = state; break;
    case ROBA_R_POSITION: r_down = state; break;
    case ROBA_Y_POSITION: y_down = state; break;
    default: break;
    }
}

static bool roba_mouse_combo_is_pressed(void) {
    return (w_down && r_down) || (r_down && y_down);
}

static void roba_mark_pending_modifiers_as_mouse_hold(void) {
    if (left_ctrl_candidate) left_ctrl_resolved_as_hold = true;
    if (right_ctrl_candidate) right_ctrl_resolved_as_hold = true;
    if (left_shift_candidate) left_shift_resolved_as_hold = true;
    if (right_shift_candidate) right_shift_resolved_as_hold = true;
}

static void roba_combo_candidate_exit_handler(struct k_work *work) {
    ARG_UNUSED(work);
    if (mouse_combo_active) return;
    w_down = r_down = y_down = false;
    roba_exit_mouse_layer_for_normal_key();
}

K_WORK_DELAYABLE_DEFINE(roba_combo_candidate_exit_work,
                        roba_combo_candidate_exit_handler);

static void roba_cancel_combo_candidate_exit(void) {
    (void)k_work_cancel_delayable(&roba_combo_candidate_exit_work);
    w_down = r_down = y_down = false;
    mouse_combo_active = false;
}

bool roba_automouse_allowed(void) {
    if (!atomic_get(&typing_guard_active)) return true;

    uint32_t now = k_uptime_get_32();
    uint32_t last = (uint32_t)atomic_get(&last_normal_key_time);
    if ((now - last) >= CONFIG_ROBA_AUTOMOUSE_TYPING_GUARD_MS) {
        atomic_clear(&typing_guard_active);
        return true;
    }
    return false;
}

static int roba_modifier_listener(const zmk_event_t *eh) {
    struct zmk_modifiers_state_changed *ev = as_zmk_modifiers_state_changed(eh);
    if (ev == NULL || !ev->state) return ZMK_EV_EVENT_BUBBLE;

    if (left_ctrl_candidate && (ev->modifiers & MOD_LCTL)) left_ctrl_resolved_as_hold = true;
    if (right_ctrl_candidate && (ev->modifiers & MOD_RCTL)) right_ctrl_resolved_as_hold = true;
    if (left_shift_candidate && (ev->modifiers & MOD_LSFT)) left_shift_resolved_as_hold = true;
    if (right_shift_candidate && (ev->modifiers & MOD_RSFT)) right_shift_resolved_as_hold = true;

    return ZMK_EV_EVENT_BUBBLE;
}

static int roba_position_listener(const zmk_event_t *eh) {
    struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev == NULL) return ZMK_EV_EVENT_BUBBLE;

    if (ev->state && ev->position == ROBA_LEFT_CTRL_POSITION) {
        left_ctrl_candidate = true; left_ctrl_resolved_as_hold = false; return ZMK_EV_EVENT_BUBBLE;
    }
    if (ev->state && ev->position == ROBA_RIGHT_CTRL_POSITION) {
        right_ctrl_candidate = true; right_ctrl_resolved_as_hold = false; return ZMK_EV_EVENT_BUBBLE;
    }
    if (ev->state && ev->position == ROBA_LEFT_SHIFT_POSITION) {
        left_shift_candidate = true; left_shift_resolved_as_hold = false; return ZMK_EV_EVENT_BUBBLE;
    }
    if (ev->state && ev->position == ROBA_RIGHT_SHIFT_POSITION) {
        right_shift_candidate = true; right_shift_resolved_as_hold = false; return ZMK_EV_EVENT_BUBBLE;
    }

    if (!ev->state && ev->position == ROBA_LEFT_CTRL_POSITION) {
        bool hold = left_ctrl_resolved_as_hold;
        left_ctrl_candidate = left_ctrl_resolved_as_hold = false;
        if (!hold) { roba_cancel_combo_candidate_exit(); roba_exit_mouse_layer_for_normal_key(); }
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (!ev->state && ev->position == ROBA_RIGHT_CTRL_POSITION) {
        bool hold = right_ctrl_resolved_as_hold;
        right_ctrl_candidate = right_ctrl_resolved_as_hold = false;
        if (!hold) { roba_cancel_combo_candidate_exit(); roba_exit_mouse_layer_for_normal_key(); }
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (!ev->state && ev->position == ROBA_LEFT_SHIFT_POSITION) {
        bool hold = left_shift_resolved_as_hold;
        left_shift_candidate = left_shift_resolved_as_hold = false;
        if (!hold) { roba_cancel_combo_candidate_exit(); roba_exit_mouse_layer_for_normal_key(); }
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (!ev->state && ev->position == ROBA_RIGHT_SHIFT_POSITION) {
        bool hold = right_shift_resolved_as_hold;
        right_shift_candidate = right_shift_resolved_as_hold = false;
        if (!hold) { roba_cancel_combo_candidate_exit(); roba_exit_mouse_layer_for_normal_key(); }
        return ZMK_EV_EVENT_BUBBLE;
    }

    bool mouse_layer_active = zmk_keymap_layer_active(ROBA_MOUSE_LAYER);

    if (!ev->state && roba_is_mouse_combo_candidate(ev->position)) {
        roba_set_mouse_combo_candidate_down(ev->position, false);
        if (mouse_combo_active && !w_down && !r_down && !y_down) mouse_combo_active = false;
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (!ev->state) return ZMK_EV_EVENT_BUBBLE;

    if (mouse_layer_active && roba_is_direct_mouse_button_position(ev->position)) {
        roba_mark_pending_modifiers_as_mouse_hold();
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (mouse_layer_active && roba_is_mouse_combo_candidate(ev->position)) {
        roba_set_mouse_combo_candidate_down(ev->position, true);

        if (roba_mouse_combo_is_pressed()) {
            mouse_combo_active = true;
            (void)k_work_cancel_delayable(&roba_combo_candidate_exit_work);
            roba_mark_pending_modifiers_as_mouse_hold();
            return ZMK_EV_EVENT_BUBBLE;
        }

        mouse_combo_active = false;
        (void)k_work_reschedule(&roba_combo_candidate_exit_work,
                                K_MSEC(ROBA_MOUSE_COMBO_EXIT_DELAY_MS));
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (mouse_layer_active && ev->position == ROBA_MINUS_POSITION) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (mouse_layer_active) roba_cancel_combo_candidate_exit();
    roba_exit_mouse_layer_for_normal_key();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(roba_automouse_position, roba_position_listener);
ZMK_SUBSCRIPTION(roba_automouse_position, zmk_position_state_changed);

ZMK_LISTENER(roba_automouse_modifier, roba_modifier_listener);
ZMK_SUBSCRIPTION(roba_automouse_modifier, zmk_modifiers_state_changed);
