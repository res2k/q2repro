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
// input.h -- external (non-keyboard) input devices
//

void IN_Frame(void);
void IN_Activate(void);
void IN_WarpMouse(int x, int y);

// Input source types
typedef enum {
    INSRC_KEYMOUSE = 0,
    INSRC_GAMEPAD = 1
} input_source_t;

// Input source flags
enum {
    INSRCFLAG_KEYMOUSE = BIT(INSRC_KEYMOUSE),
    INSRCFLAG_GAMEPAD = BIT(INSRC_GAMEPAD),
};

// Input source tracking
void IN_SetSource(input_source_t source);
input_source_t IN_GetSource(void);

//
// Gamepad support functions
//

// Gamepad move/look values
typedef struct {
    float x;
    float y;
} gamepad_2dinput_t;

/* The wheel input coordinate can come from different sources;
 * most of the time from the "look" stick, however, when the
 * wheel was activated by pushing down on a touchpad, use the
 * respective touchpad as source of the input. */
typedef enum {
    WHEEL_LOOK_STICK,
    WHEEL_LOOK_TOUCHPAD0,
    WHEEL_LOOK_TOUCHPAD1,
} gamepad_wheel_look_source_t;

#if USE_GAMEPAD
void Gamepad_Init(void);
void Gamepad_Shutdown(void);
void Gamepad_Frame(void);

bool Gamepad_GetMoveLook(gamepad_2dinput_t *move, gamepad_2dinput_t *look);
bool Gamepad_GetWheelLook(gamepad_wheel_look_source_t source, gamepad_2dinput_t *look);

const char *Gamepad_Name(void);
bool Gamepad_HasTouchpad(void);
#else // USE_GAMEPAD
#define Gamepad_Init()             (void)0
#define Gamepad_Shutdown()         (void)0
#define Gamepad_Frame()            (void)0
#define Gamepad_GetMoveLook(M, L)  (false)
#define Gamepad_GetWheelLook(S, L) (false)
#define Gamepad_Name()             (NULL)
#define Gamepad_HasTouchpad()      (false)
#endif // USE_GAMEPAD
