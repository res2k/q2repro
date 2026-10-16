/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

//
// these are the key numbers that should be passed to Key_Event
//
typedef enum
{
    K_BACKSPACE = 8,
    K_TAB = 9,
    K_ENTER = 13,
    K_PAUSE = 19,
    K_ESCAPE = 27,
    K_SPACE = 32,
    K_DEL = 127,

    // normal keys should be passed as lowercased ascii
    K_ASCIIFIRST = 32,
    K_ASCIILAST = 127,

    K_UPARROW,
    K_DOWNARROW,
    K_LEFTARROW,
    K_RIGHTARROW,

    K_ALT,
    K_CTRL,
    K_SHIFT,
    K_F1,
    K_F2,
    K_F3,
    K_F4,
    K_F5,
    K_F6,
    K_F7,
    K_F8,
    K_F9,
    K_F10,
    K_F11,
    K_F12,
    K_INS,
    K_PGDN,
    K_PGUP,
    K_HOME,
    K_END,

    K_102ND,

    K_NUMLOCK,
    K_CAPSLOCK,
    K_SCROLLOCK,
    K_LWINKEY,
    K_RWINKEY,
    K_MENU,
    K_PRINTSCREEN,

    K_KP_HOME,
    K_KP_UPARROW,
    K_KP_PGUP,
    K_KP_LEFTARROW,
    K_KP_5,
    K_KP_RIGHTARROW,
    K_KP_END,
    K_KP_DOWNARROW,
    K_KP_PGDN,
    K_KP_ENTER,
    K_KP_INS,
    K_KP_DEL,
    K_KP_SLASH,
    K_KP_MINUS,
    K_KP_PLUS,
    K_KP_MULTIPLY,

    // these come paired with legacy K_ALT/K_CTRL/K_SHIFT events
    K_LALT = 180,
    K_RALT,
    K_LCTRL,
    K_RCTRL,
    K_LSHIFT,
    K_RSHIFT,

    // mouse buttons generate virtual keys
    K_MOUSEFIRST = 200,
    K_MOUSE1,
    K_MOUSE2,
    K_MOUSE3,
    K_MOUSE4,
    K_MOUSE5,
    K_MOUSE6,
    K_MOUSE7,
    K_MOUSE8,

    // mouse wheel generates virtual keys
    K_MWHEELDOWN = 210,
    K_MWHEELUP,
    K_MWHEELRIGHT,
    K_MWHEELLEFT,
    K_MOUSELAST,

    // SDL game controller keys
    K_GAMEPAD_FIRST = 300,
    K_LEFT_STICK = K_GAMEPAD_FIRST,
    K_RIGHT_STICK,
    K_LEFT_SHOULDER,
    K_RIGHT_SHOULDER,
    K_DPAD_UP,
    K_DPAD_DOWN,
    K_DPAD_LEFT,
    K_DPAD_RIGHT,
    K_A_BUTTON,
    K_B_BUTTON,
    K_X_BUTTON,
    K_Y_BUTTON,
    K_LEFT_TRIGGER,
    K_RIGHT_TRIGGER,
    K_MISC1,
    K_RIGHT_PADDLE1,
    K_LEFT_PADDLE1,
    K_RIGHT_PADDLE2,
    K_LEFT_PADDLE2,
    K_TOUCHPAD,
    K_MISC2,
    K_PAD_VIEW,
    K_PAD_MENU,

    K_GAMEPAD_ALT_FIRST,
    K_LEFT_STICK_ALT = K_GAMEPAD_ALT_FIRST,
    K_RIGHT_STICK_ALT,
    K_LEFT_SHOULDER_ALT,
    K_RIGHT_SHOULDER_ALT,
    K_DPAD_UP_ALT,
    K_DPAD_DOWN_ALT,
    K_DPAD_LEFT_ALT,
    K_DPAD_RIGHT_ALT,
    K_A_BUTTON_ALT,
    K_B_BUTTON_ALT,
    K_X_BUTTON_ALT,
    K_Y_BUTTON_ALT,
    K_LEFT_TRIGGER_ALT,
    K_RIGHT_TRIGGER_ALT,
    K_MISC1_ALT,
    K_RIGHT_PADDLE1_ALT,
    K_LEFT_PADDLE1_ALT,
    K_RIGHT_PADDLE2_ALT,
    K_LEFT_PADDLE2_ALT,
    K_TOUCHPAD_ALT,
    K_MISC2_ALT,
    K_PAD_VIEW_ALT,
    K_PAD_MENU_ALT,

    _NUM_KEY_CODES
} keycode_t;

typedef enum {
    KEY_GAME    = 0,
    KEY_CONSOLE = BIT(0),
    KEY_MESSAGE = BIT(1),
    KEY_MENU    = BIT(2)
} keydest_t;

typedef bool (*keywaitcb_t)(void *arg, int key);

void    Key_Init(void);

void    Key_Event(unsigned key, bool down, unsigned time);
void    Key_Event2(unsigned key, bool down, unsigned time);
void    Key_CharEvent(int key);

bool        Key_GetOverstrikeMode(void);
void        Key_SetOverstrikeMode(bool overstrike);
keydest_t   Key_GetDest(void);
void        Key_SetDest(keydest_t dest);

int         Key_IsDown(int key);
int         Key_AnyKeyDown(void);
void        Key_ClearStates(void);

const char  *Key_KeynumToString(int keynum);
const char  *Key_KeynumToLabel(int keynum);
int     Key_StringToKeynum(const char *str);
void    Key_SetBinding(int keynum, const char *binding);
const char  *Key_LabelForBinding(const char *binding, unsigned sources);
const char *Key_BindingForKey(int key);
int Key_EnumBindings(int key, const char *binding, unsigned sources);
void    Key_WriteBindings(qhandle_t f);

void    Key_WaitKey(keywaitcb_t wait, void *arg);
