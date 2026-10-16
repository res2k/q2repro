/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2005 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "client.h"
#include "client/input.h"

#include <SDL3/SDL.h>

extern const vid_driver_t vid_sdl;

/* Although internally we call it a "gamepad", use "joy(stick)" in cvar names -
 * since we borrowed swaths of code from vkQuake anyway, this allows joystick
 * related settings to be copy pasted, if so desired. */
static cvar_t            *in_joystick;

static cvar_t            *joy_deadzone_look;
static cvar_t            *joy_deadzone_move;
static cvar_t            *joy_outer_threshold_look;
static cvar_t            *joy_outer_threshold_move;
static cvar_t            *joy_deadzone_trigger;
static cvar_t            *joy_sensitivity_yaw;
static cvar_t            *joy_sensitivity_pitch;
static cvar_t            *joy_invert;
static cvar_t            *joy_exponent;
static cvar_t            *joy_exponent_move;
static cvar_t            *joy_swapmovelook;

static cvar_t            *cl_joyinvertturn;   // Look axis X invert
static cvar_t            *cl_joyinvertstrafe; // Move axis X invert
static cvar_t            *cl_joyinvertmove;   // Move axis Y invert

static cvar_t            *cl_joytouchpadpressure; // touchpad pressure threshold to register it as "down"
static cvar_t            *cl_joytouchpadsplit;    // if only a single touchpad is present, split it down the middle
                                                  // and pretend we have a left and right touchpad

extern bool               joy_altmodifier_pressed;

#define MAX_TOUCHPADS 2

static const keycode_t touchpad_button[MAX_TOUCHPADS] = {K_TOUCHPAD, K_MISC2};

typedef struct axisstate_s {
    float value[SDL_GAMEPAD_AXIS_COUNT]; // normalized to +-1
} gamepad_axisstate_t;

static struct {
    bool         initialized;
    SDL_Gamepad *ptr;

    struct {
        size_t   available[BC_COUNT(SDL_GAMEPAD_BUTTON_COUNT)];
        size_t   state[BC_COUNT(SDL_GAMEPAD_BUTTON_COUNT)];
        unsigned timer[SDL_GAMEPAD_BUTTON_COUNT];
    } button;

    struct {
        size_t available[BC_COUNT(SDL_GAMEPAD_AXIS_COUNT)];
    } axis;
    gamepad_axisstate_t axisstate;

    unsigned            emulatedkeytimer[6];

    int                 reported_key[_NUM_KEY_CODES];

    struct {
        int               num;
        int               fingers[MAX_TOUCHPADS];
        bool              split;
        size_t            down[BC_COUNT(MAX_TOUCHPADS)];
        gamepad_2dinput_t touch_pos[MAX_TOUCHPADS];
        unsigned          timer[MAX_TOUCHPADS];
    } touchpad;
} gamepad;

static void in_joystick_changed(cvar_t *cvar)
{
    if (in_joystick->integer && !gamepad.initialized)
        Gamepad_Init();
    else if (!in_joystick->integer && gamepad.initialized)
        Gamepad_Shutdown();
}

static void init_touchpad_split(void)
{
    gamepad.touchpad.split = gamepad.touchpad.num == 1 && cl_joytouchpadsplit->integer != 0;
}

static void cl_joytouchpadsplit_changed(cvar_t *cvar)
{
    if (!gamepad.ptr)
        return;
    init_touchpad_split();
}

static void init_gamepad_state(void)
{
    memset(&gamepad.button, 0, sizeof(gamepad.button));
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) {
        if (SDL_GamepadHasButton(gamepad.ptr, b))
            Q_SetBit(gamepad.button.available, b);
    }

    memset(&gamepad.axis, 0, sizeof(gamepad.axis));
    for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++) {
        if (SDL_GamepadHasAxis(gamepad.ptr, a))
            Q_SetBit(gamepad.axis.available, a);
    }

    memset(&gamepad.axisstate, 0, sizeof(gamepad.axisstate));
    memset(&gamepad.emulatedkeytimer, 0, sizeof(gamepad.emulatedkeytimer));
    memset(&gamepad.reported_key, 0, sizeof(gamepad.reported_key));

    memset(&gamepad.touchpad, 0, sizeof(gamepad.touchpad));
    gamepad.touchpad.num = SDL_GetNumGamepadTouchpads(gamepad.ptr);
    gamepad.touchpad.num = min(gamepad.touchpad.num, MAX_TOUCHPADS);
    for (int t = 0; t < gamepad.touchpad.num; t++)
        gamepad.touchpad.fingers[t] = SDL_GetNumGamepadTouchpadFingers(gamepad.ptr, t);
    init_touchpad_split();
}

void Gamepad_Init(void)
{
    in_joystick = Cvar_Get("in_joystick", "0", CVAR_ARCHIVE);
    in_joystick->changed = in_joystick_changed;

    joy_deadzone_look = Cvar_Get("joy_deadzone_look", "0.175", CVAR_ARCHIVE);
    joy_deadzone_move = Cvar_Get("joy_deadzone_move", "0.175", CVAR_ARCHIVE);
    joy_outer_threshold_look = Cvar_Get("joy_outer_threshold_look", "0.02", CVAR_ARCHIVE);
    joy_outer_threshold_move = Cvar_Get("joy_outer_threshold_move", "0.02", CVAR_ARCHIVE);
    joy_sensitivity_yaw = Cvar_Get("joy_sensitivity_yaw", "240", CVAR_ARCHIVE);
    joy_sensitivity_pitch = Cvar_Get("joy_sensitivity_pitch", "130", CVAR_ARCHIVE);
    joy_invert = Cvar_Get("joy_invert", "0", CVAR_ARCHIVE);
    joy_deadzone_trigger = Cvar_Get("joy_deadzone_trigger", "0.2", CVAR_ARCHIVE);
    joy_exponent = Cvar_Get("joy_exponent", "2", CVAR_ARCHIVE);
    joy_exponent_move = Cvar_Get("joy_exponent_move", "2", CVAR_ARCHIVE);
    joy_swapmovelook = Cvar_Get("joy_swapmovelook", "0", CVAR_ARCHIVE);

    cl_joyinvertturn = Cvar_Get("cl_joyinvertturn", "0", CVAR_ARCHIVE);
    cl_joyinvertstrafe = Cvar_Get("cl_joyinvertstrafe", "0", CVAR_ARCHIVE);
    cl_joyinvertmove = Cvar_Get("cl_joyinvertmove", "0", CVAR_ARCHIVE);

    cl_joytouchpadpressure = Cvar_Get("cl_joytouchpadpressure", "0.01", CVAR_ARCHIVE);
    cl_joytouchpadsplit = Cvar_Get("cl_joytouchpadsplit", "1", CVAR_ARCHIVE);
    cl_joytouchpadsplit->changed = cl_joytouchpadsplit_changed;

    if (gamepad.initialized)
        return;

    if (!in_joystick->integer)
        return;

    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        Com_WPrintf("could not initialize SDL Gamepad\n");
        return;
    }

    // TODO: read additional gamecontrollerdb.txt files?

    gamepad.initialized = true;
}

void Gamepad_Shutdown(void)
{
    if (!gamepad.initialized)
        return;

    if (gamepad.ptr) {
        SDL_CloseGamepad(gamepad.ptr);
        gamepad.ptr = NULL;
    }

    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    gamepad.initialized = false;
}

static void check_gamepad_connected(void)
{
    if (gamepad.ptr && !SDL_GamepadConnected(gamepad.ptr)) {
        Com_WPrintf("gamepad disconnected: %s\n", SDL_GetGamepadName(gamepad.ptr));
        SDL_CloseGamepad(gamepad.ptr);
        gamepad.ptr = NULL;
    }

    if (!gamepad.ptr && SDL_HasJoystick()) // prefer SDL_HasJoystick() as it
                                           // doesn't allocate memory
    {
        int             num_gamepads = 0;
        SDL_JoystickID *gamepads = SDL_GetGamepads(&num_gamepads);
        for (int i = 0; i < num_gamepads; i++) {
            gamepad.ptr = SDL_OpenGamepad(gamepads[i]);
            if (gamepad.ptr) {
                Com_NPrintf("using gamepad: %s\n", SDL_GetGamepadName(gamepad.ptr));
                init_gamepad_state();
                break;
            } else {
                Com_DPrintf("failed to open controller %s: %s\n", SDL_GetGamepadNameForID(gamepads[i]), SDL_GetError());
            }
        }
        SDL_free(gamepads);
    }
}

/*
================
IN_KeyForControllerButton
================
*/
static int IN_KeyForControllerButton(SDL_GamepadButton button)
{
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return K_A_BUTTON;
    case SDL_GAMEPAD_BUTTON_EAST:
        return K_B_BUTTON;
    case SDL_GAMEPAD_BUTTON_WEST:
        return K_X_BUTTON;
    case SDL_GAMEPAD_BUTTON_NORTH:
        return K_Y_BUTTON;
    case SDL_GAMEPAD_BUTTON_BACK:
        return K_PAD_VIEW;
    case SDL_GAMEPAD_BUTTON_START:
        return K_PAD_MENU;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        return K_LEFT_STICK;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return K_RIGHT_STICK;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return K_LEFT_SHOULDER;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return K_RIGHT_SHOULDER;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return K_DPAD_UP;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return K_DPAD_DOWN;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return K_DPAD_LEFT;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return K_DPAD_RIGHT;
    case SDL_GAMEPAD_BUTTON_MISC1:
        return K_MISC1;
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1:
        return K_RIGHT_PADDLE1;
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1:
        return K_LEFT_PADDLE1;
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2:
        return K_RIGHT_PADDLE2;
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2:
        return K_LEFT_PADDLE2;
    case SDL_GAMEPAD_BUTTON_TOUCHPAD:
        return K_TOUCHPAD;
    case SDL_GAMEPAD_BUTTON_MISC2:
        return K_MISC2;
    default:
        return 0;
    }
}

/*
================
IN_JoyKeyEvent

Sends a Key_Event if a unpressed -> pressed or pressed -> unpressed transition
occurred, and generates key repeats if the button is held down.

Adapted from DarkPlaces by lordhavoc
================
*/
static void IN_JoyKeyEvent(bool wasdown, bool isdown, int key, unsigned *timer)
{
    const unsigned currenttime = com_localTime3;

    if (wasdown) {
        // alt modifier may mean a different key code was sent, fetch that...
        int realkey = key;
        key = gamepad.reported_key[key];
        if (!key)
            key = realkey; // shouldn't really happen...

        if (isdown) {
            if (currenttime >= *timer) {
                *timer = currenttime + 100;
                Key_Event(key, true, currenttime);
            }
        } else {
            *timer = 0;
            Key_Event(key, false, currenttime);

            // Clear any translation due to the alt modifier
            gamepad.reported_key[realkey] = 0;
        }
    } else {
        if (isdown) {
            // When pressing down a button, apply the alt modifier if pressed, and record what key we emitted
            int realkey = key;
            if (joy_altmodifier_pressed && key >= K_GAMEPAD_FIRST && key < K_GAMEPAD_ALT_FIRST)
                key += K_GAMEPAD_ALT_FIRST - K_GAMEPAD_FIRST;
            gamepad.reported_key[realkey] = key;

            *timer = currenttime + 500;
            Key_Event(key, true, currenttime);
        }
    }
}

static int get_touchpad_index(int key)
{
    switch (key) {
    case K_TOUCHPAD:
        return 0;
    case K_MISC2:
        return 1;
    }

    return -1;
}

/* Touchpad handling:
 * - customizable pressure threshold
 * - split touchpad emulation
 */
static unsigned get_pushed_touchpads(void)
{
    unsigned pushes = 0;
    if (gamepad.touchpad.split) {
        // We have a single touchpad, and depending on the side we have touch(es), simulate touchpad 0 or 1
        for (int f = 0; f < gamepad.touchpad.fingers[0]; f++) {
            gamepad_2dinput_t pos = {0.5f, 0.5f};
            float             pressure = 0;
            SDL_GetGamepadTouchpadFinger(gamepad.ptr, 0, f, NULL, &pos.x, &pos.y, &pressure);

            if (pressure > 0)
                Com_DDDPrintf("%s: touchpad 0 finger %d pressure %g\n", __func__, f, pressure);

            int touchpad_idx = 0;
            if (pos.x >= 0.5f) {
                pos.x -= 0.5f;
                touchpad_idx = 1;
            }
            pos.x *= 2;

            if (pushes & BIT(touchpad_idx))
                continue; // another finger is pushing down in this half already...

            if (pressure >= cl_joytouchpadpressure->value) {
                pushes |= BIT(touchpad_idx);
                memcpy(&gamepad.touchpad.touch_pos[touchpad_idx], &pos, sizeof(gamepad_2dinput_t));
            }
        }
    } else {
        // Just look at the first finger of each touchpad
        for (int t = 0; t < gamepad.touchpad.num; t++) {
            gamepad_2dinput_t pos = {0.5f, 0.5f};
            float             pressure = 0;
            SDL_GetGamepadTouchpadFinger(gamepad.ptr, t, 0, NULL, &pos.x, &pos.y, &pressure);

            if (pressure >= cl_joytouchpadpressure->value) {
                pushes |= BIT(t);
                memcpy(&gamepad.touchpad.touch_pos[t], &pos, sizeof(gamepad_2dinput_t));
            }
        }
    }
    return pushes;
}

void Gamepad_Frame(void)
{
    // If we don't run an event loop, update gamepad explicitly
    if (vid != &vid_sdl)
        SDL_UpdateGamepads();

    check_gamepad_connected();

    if (!gamepad.ptr)
        return;

    // emit key events for controller buttons
    for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; i++) {
        if (!Q_IsBitSet(gamepad.button.available, i))
            continue;

        bool newstate = SDL_GetGamepadButton(gamepad.ptr, (SDL_GamepadButton)i);
        bool oldstate = Q_IsBitSet(gamepad.button.state, i);

        if (newstate)
            Q_SetBit(gamepad.button.state, i);
        else
            Q_ClearBit(gamepad.button.state, i);
        int key = IN_KeyForControllerButton((SDL_GamepadButton)i);
        if (!key)
            continue;

        int touchpad = get_touchpad_index(key);
        if (touchpad >= 0)
            continue;

        IN_JoyKeyEvent(oldstate, newstate, key, &gamepad.button.timer[i]);
    }

    // Compute touchpad buttons we tell the game
    unsigned pressed_touchpads = get_pushed_touchpads();
    int      virtual_touchpads = gamepad.touchpad.split ? 2 : gamepad.touchpad.num;
    for (int t = 0; t < virtual_touchpads; t++) {
        bool newstate = (pressed_touchpads & BIT(t)) != 0;
        bool oldstate = Q_IsBitSet(gamepad.touchpad.down, t);

        if (newstate)
            Q_SetBit(gamepad.touchpad.down, t);
        else
            Q_ClearBit(gamepad.touchpad.down, t);

        int key = touchpad_button[t];

        IN_JoyKeyEvent(oldstate, newstate, key, &gamepad.touchpad.timer[t]);
    }

    gamepad_axisstate_t newaxisstate;
    for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; i++) {
        if (!Q_IsBitSet(gamepad.axis.available, i))
            continue;

        newaxisstate.value[i] = SDL_GetGamepadAxis(gamepad.ptr, (SDL_GamepadAxis)i) / 32768.0f;
    }

    // emit emulated arrow keys so the analog sticks can be used in the menu
    const float stickthreshold = 0.9f;
    if (Key_GetDest() != KEY_GAME) {
        IN_JoyKeyEvent(gamepad.axisstate.value[SDL_GAMEPAD_AXIS_LEFTX] < -stickthreshold,
                       newaxisstate.value[SDL_GAMEPAD_AXIS_LEFTX] < -stickthreshold, K_LEFTARROW,
                       &gamepad.emulatedkeytimer[0]);
        IN_JoyKeyEvent(gamepad.axisstate.value[SDL_GAMEPAD_AXIS_LEFTX] > stickthreshold,
                       newaxisstate.value[SDL_GAMEPAD_AXIS_LEFTX] > stickthreshold, K_RIGHTARROW,
                       &gamepad.emulatedkeytimer[1]);
        IN_JoyKeyEvent(gamepad.axisstate.value[SDL_GAMEPAD_AXIS_LEFTY] < -stickthreshold,
                       newaxisstate.value[SDL_GAMEPAD_AXIS_LEFTY] < -stickthreshold, K_UPARROW,
                       &gamepad.emulatedkeytimer[2]);
        IN_JoyKeyEvent(gamepad.axisstate.value[SDL_GAMEPAD_AXIS_LEFTY] > stickthreshold,
                       newaxisstate.value[SDL_GAMEPAD_AXIS_LEFTY] > stickthreshold, K_DOWNARROW,
                       &gamepad.emulatedkeytimer[3]);
    }

    // emit emulated keys for the analog triggers
    const float triggerthreshold = joy_deadzone_trigger->value;
    IN_JoyKeyEvent(gamepad.axisstate.value[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] > triggerthreshold,
                   newaxisstate.value[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] > triggerthreshold, K_LEFT_TRIGGER,
                   &gamepad.emulatedkeytimer[4]);
    IN_JoyKeyEvent(gamepad.axisstate.value[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] > triggerthreshold,
                   newaxisstate.value[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] > triggerthreshold, K_RIGHT_TRIGGER,
                   &gamepad.emulatedkeytimer[5]);

    gamepad.axisstate = newaxisstate;
}

/*
================
IN_AxisMagnitude

Returns the vector length of the given joystick axis
================
*/
static vec_t IN_AxisMagnitude(const gamepad_2dinput_t axis)
{
    vec_t magnitude = sqrtf((axis.x * axis.x) + (axis.y * axis.y));
    return magnitude;
}

/*
================
IN_ApplyEasing

assumes axis values are in [-1, 1] and the vector magnitude has been clamped
at 1. Raises the axis values to the given exponent, keeping signs.
================
*/
static gamepad_2dinput_t IN_ApplyEasing(const gamepad_2dinput_t axis, float exponent)
{
    gamepad_2dinput_t result = {0};
    vec_t             eased_magnitude;
    vec_t             magnitude = IN_AxisMagnitude(axis);

    if (magnitude == 0)
        return result;

    eased_magnitude = powf(magnitude, exponent);

    result.x = axis.x * (eased_magnitude / magnitude);
    result.y = axis.y * (eased_magnitude / magnitude);
    return result;
}

/*
================

IN_ApplyDeadzone

in: raw joystick axis values converted to floats in +-1
out: applies a circular inner deadzone and a circular outer threshold and clamps
the magnitude at 1 (my 360 controller is slightly non-circular and the stick
travels further on the diagonals)

deadzone is expected to satisfy 0 < deadzone < 1 - outer_threshold
outer_threshold is expected to satisfy 0 < outer_threshold < 1 - deadzone

from https://github.com/jeremiah-sypult/Quakespasm-Rift
and adapted from
http://www.third-helix.com/2013/04/12/doing-thumbstick-dead-zones-right.html
================
*/
static gamepad_2dinput_t IN_ApplyDeadzone(const gamepad_2dinput_t axis, float deadzone, float outer_threshold)
{
    gamepad_2dinput_t result = {0};
    vec_t             magnitude = IN_AxisMagnitude(axis);

    if (magnitude > deadzone) {
        const vec_t new_magnitude = min(1.0, (magnitude - deadzone) / (1.0 - deadzone - outer_threshold));
        const vec_t scale = new_magnitude / magnitude;
        result.x = axis.x * scale;
        result.y = axis.y * scale;
    }

    return result;
}

static bool get_raw_movelook(gamepad_2dinput_t *move, gamepad_2dinput_t *look)
{
    if (!gamepad.ptr)
        return false;

    gamepad_2dinput_t moveRaw, moveDeadzone;
    gamepad_2dinput_t lookRaw, lookDeadzone;

    moveRaw.x = gamepad.axisstate.value[SDL_GAMEPAD_AXIS_LEFTX];
    moveRaw.y = gamepad.axisstate.value[SDL_GAMEPAD_AXIS_LEFTY];
    lookRaw.x = gamepad.axisstate.value[SDL_GAMEPAD_AXIS_RIGHTX];
    lookRaw.y = gamepad.axisstate.value[SDL_GAMEPAD_AXIS_RIGHTY];

    if (joy_swapmovelook->integer) {
        const gamepad_2dinput_t temp = moveRaw;
        moveRaw = lookRaw;
        lookRaw = temp;
    }

    moveDeadzone = IN_ApplyDeadzone(moveRaw, joy_deadzone_move->value, joy_outer_threshold_move->value);
    lookDeadzone = IN_ApplyDeadzone(lookRaw, joy_deadzone_look->value, joy_outer_threshold_look->value);

    *move = IN_ApplyEasing(moveDeadzone, joy_exponent_move->value);
    *look = IN_ApplyEasing(lookDeadzone, joy_exponent->value);

    return true;
}

bool Gamepad_GetMoveLook(gamepad_2dinput_t *move, gamepad_2dinput_t *look)
{
    gamepad_2dinput_t moveEased, lookEased;

    if (!get_raw_movelook(&moveEased, &lookEased))
        return false;

    if (cl_joyinvertmove->integer)
        moveEased.y = -moveEased.y;
    if (cl_joyinvertstrafe->integer)
        moveEased.x = -moveEased.x;

    if (joy_invert->integer)
        lookEased.y = -lookEased.y;
    if (cl_joyinvertturn->integer)
        lookEased.x = -lookEased.x;

    lookEased.x *= joy_sensitivity_yaw->value;
    lookEased.y *= joy_sensitivity_pitch->value;

    *move = moveEased;
    *look = lookEased;

    return true;
}

bool Gamepad_GetWheelLook(gamepad_wheel_look_source_t source, gamepad_2dinput_t *look)
{
    if (source == WHEEL_LOOK_STICK) {
        gamepad_2dinput_t moveEased;

        if (!get_raw_movelook(&moveEased, look))
            return false;

        return true;
    }

    int touchpad = source - WHEEL_LOOK_TOUCHPAD0;
    if (touchpad < 0)
        return false;

    gamepad_2dinput_t current_pos;
    memcpy(&current_pos, &gamepad.touchpad.touch_pos[touchpad], sizeof(gamepad_2dinput_t));

    look->x = 2 * (current_pos.x - 0.5f);
    look->y = 2 * (current_pos.y - 0.5f);

    return true;
}

const char *Gamepad_Name(void)
{
    if (!gamepad.ptr)
        return NULL;
    return SDL_GetGamepadName(gamepad.ptr);
}

bool Gamepad_HasTouchpad(void)
{
    if (!gamepad.ptr)
        return false;
    return SDL_GetNumGamepadTouchpads(gamepad.ptr) > 0;
}
