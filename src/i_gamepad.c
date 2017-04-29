/*
========================================================================

                           D O O M  R e t r o
         The classic, refined DOOM source port. For Windows PC.

========================================================================

  Copyright © 1993-2012 id Software LLC, a ZeniMax Media company.
  Copyright © 2013-2017 Brad Harding.

  DOOM Retro is a fork of Chocolate DOOM.
  For a list of credits, see <http://wiki.doomretro.com/credits>.

  This file is part of DOOM Retro.

  DOOM Retro is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation, either version 3 of the License, or (at your
  option) any later version.

  DOOM Retro is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with DOOM Retro. If not, see <https://www.gnu.org/licenses/>.

  DOOM is a registered trademark of id Software LLC, a ZeniMax Media
  company, in the US and/or other countries and is used without
  permission. All other trademarks are the property of their respective
  holders. DOOM Retro is in no way affiliated with nor endorsed by
  id Software.

========================================================================
*/

#if defined(_WIN32)
#include <Windows.h>
#include <XInput.h>

typedef DWORD(WINAPI *XINPUTGETSTATE)(DWORD, XINPUT_STATE *);
typedef DWORD(WINAPI *XINPUTSETSTATE)(DWORD, XINPUT_VIBRATION *);

static XINPUTGETSTATE pXInputGetState;
static XINPUTSETSTATE pXInputSetState;
#endif

#include "c_console.h"
#include "d_main.h"
#include "hu_stuff.h"
#include "i_gamepad.h"
#include "m_config.h"
#include "m_controls.h"
#include "m_fixed.h"
#include "m_misc.h"

float                   gp_deadzone_left = gp_deadzone_left_default;
float                   gp_deadzone_right = gp_deadzone_right_default;

static SDL_Joystick     *gamepad;

int                     gamepadbuttons;
short                   gamepadthumbLX;
short                   gamepadthumbLY;
short                   gamepadthumbRX;
short                   gamepadthumbRY;
float                   gamepadsensitivity;
short                   gamepadleftdeadzone;
short                   gamepadrightdeadzone;

dboolean                vibrate;
int                     damagevibrationtics;
int                     weaponvibrationtics;
int                     currentmotorspeed;
int                     idlemotorspeed;
int                     restoremotorspeed;

extern dboolean         idclev;
extern dboolean         idmus;
extern dboolean         idbehold;
extern dboolean         menuactive;
extern dboolean         message_clearable;

#if defined(_WIN32)
HMODULE                 pXInputDLL;
#endif

void (*gamepadfunc)(void);
void (*gamepadthumbsfunc)(short, short, short, short);

void I_InitGamepad(void)
{
    gamepadfunc = I_PollDirectInputGamepad;
    gamepadthumbsfunc = (gp_swapthumbsticks ? I_PollThumbs_DirectInput_LeftHanded :
        I_PollThumbs_DirectInput_RightHanded);

    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) < 0)
        C_Warning("Gamepad support couldn't be initialized.");
    else
    {
        int     i;
        int     numgamepads = SDL_NumJoysticks();

        for (i = 0; i < numgamepads; i++)
            if ((gamepad = SDL_JoystickOpen(i)))
                break;

        if (!gamepad)
            SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
        else
        {
#if defined(_WIN32)
            char        *XInputDLL;
            static int  initcount;

            if ((pXInputDLL = LoadLibrary("XInput1_4.dll")))
                XInputDLL = "XInput1_4.dll";
            else if ((pXInputDLL = LoadLibrary("XInput9_1_0.dll")))
                XInputDLL = "XInput9_1_0.dll";
            else if ((pXInputDLL = LoadLibrary("XInput1_3.dll")))
                XInputDLL = "XInput1_3.dll";

            initcount++;

            if (pXInputDLL)
            {
                pXInputGetState = (XINPUTGETSTATE)GetProcAddress(pXInputDLL, "XInputGetState");
                pXInputSetState = (XINPUTSETSTATE)GetProcAddress(pXInputDLL, "XInputSetState");

                if (pXInputGetState && pXInputSetState)
                {
                    XINPUT_STATE        state;

                    ZeroMemory(&state, sizeof(XINPUT_STATE));

                    if (pXInputGetState(0, &state) == ERROR_SUCCESS)
                    {
                        gamepadfunc = I_PollXInputGamepad;
                        gamepadthumbsfunc = (gp_swapthumbsticks ? I_PollThumbs_XInput_LeftHanded :
                            I_PollThumbs_XInput_RightHanded);
                        if (initcount++ == 1)
                            C_Output("An <i><b>XInput</b></i> gamepad is connected. Using "
                                "<b>%s</b>.", XInputDLL);
                    }
                }
                else
                    FreeLibrary(pXInputDLL);
            }

            if (initcount == 1)
            {
                const char      *name = SDL_JoystickName(gamepad);

                if (*name)
                    C_Output("A <i><b>DirectInput</b></i> gamepad called \"%s\" is connected.",
                        name);
                else
                    C_Output("A <i><b>DirectInput</b></i> gamepad is connected.");
            }
#else
            const char  *name = SDL_JoystickName(gamepad);

            if (*name)
                C_Output("A <i><b>DirectInput</b></i> gamepad called \"%s\" is connected.", name);
            else
                C_Output("A <i><b>DirectInput</b></i> gamepad is connected.");
#endif

            SDL_JoystickEventState(SDL_ENABLE);
        }
    }
}

void I_ShutdownGamepad(void)
{
#if defined(_WIN32)
    if (pXInputDLL)
        FreeLibrary(pXInputDLL);
#endif

    if (gamepad)
    {
        SDL_JoystickClose(gamepad);
        gamepad = NULL;
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    }
}

static short __inline clamp(short value, short deadzone)
{
    return (ABS(value) < deadzone ? 0 : MAX(-SHRT_MAX, value));
}

void I_PollThumbs_DirectInput_RightHanded(short LX, short LY, short RX, short RY)
{
    gamepadthumbLX = clamp(SDL_JoystickGetAxis(gamepad, LX), gamepadleftdeadzone);
    gamepadthumbLY = clamp(SDL_JoystickGetAxis(gamepad, LY), gamepadleftdeadzone);
    gamepadthumbRX = clamp(SDL_JoystickGetAxis(gamepad, RX), gamepadrightdeadzone);
    gamepadthumbRY = clamp(SDL_JoystickGetAxis(gamepad, RY), gamepadrightdeadzone);
}

void I_PollThumbs_DirectInput_LeftHanded(short LX, short LY, short RX, short RY)
{
    gamepadthumbLX = clamp(SDL_JoystickGetAxis(gamepad, RX), gamepadrightdeadzone);
    gamepadthumbLY = clamp(SDL_JoystickGetAxis(gamepad, RY), gamepadrightdeadzone);
    gamepadthumbRX = clamp(SDL_JoystickGetAxis(gamepad, LX), gamepadleftdeadzone);
    gamepadthumbRY = clamp(SDL_JoystickGetAxis(gamepad, LY), gamepadleftdeadzone);
}

void I_PollDirectInputGamepad(void)
{
    if (gamepad && !noinput)
    {
        int     hat = SDL_JoystickGetHat(gamepad, 0);

        gamepadbuttons = (SDL_JoystickGetButton(gamepad, 0) << 14)
            | (SDL_JoystickGetButton(gamepad, 1) << 12)
            | (SDL_JoystickGetButton(gamepad, 2) << 13)
            | (SDL_JoystickGetButton(gamepad, 3) << 15)
            | (SDL_JoystickGetButton(gamepad, 4) << 8)
            | (SDL_JoystickGetButton(gamepad, 5) << 9)
            | (SDL_JoystickGetButton(gamepad, 6) << 10)
            | (SDL_JoystickGetButton(gamepad, 7) << 11)
            | (SDL_JoystickGetButton(gamepad, 8) << 5)
            | (SDL_JoystickGetButton(gamepad, 9) << 4)
            | (SDL_JoystickGetButton(gamepad, 10) << 6)
            | (SDL_JoystickGetButton(gamepad, 11) << 7);

        if (hat)
            gamepadbuttons |= !!(hat & SDL_HAT_UP)
                | (!!(hat & SDL_HAT_RIGHT) << 3)
                | (!!(hat & SDL_HAT_DOWN) << 1)
                | (!!(hat & SDL_HAT_LEFT) << 2);

        if (gamepadbuttons)
        {
            idclev = false;
            idmus = false;
            if (idbehold)
            {
                message_clearable = true;
                HU_ClearMessages();
                idbehold = false;
            }
        }

        if (gp_sensitivity || menuactive || (gamepadbuttons & gamepadmenu))
        {
            event_t     ev;

            ev.type = ev_gamepad;
            D_PostEvent(&ev);

            gamepadthumbsfunc(0, 1, 2, 3);
        }
        else
        {
            gamepadbuttons = 0;
            gamepadthumbLX = 0;
            gamepadthumbLY = 0;
            gamepadthumbRX = 0;
            gamepadthumbRY = 0;
        }
    }
}

void XInputVibration(int motorspeed)
{
#if defined(_WIN32)
    motorspeed = MIN(motorspeed, 65535);
    if (motorspeed > currentmotorspeed || motorspeed == idlemotorspeed)
    {
        XINPUT_VIBRATION    vibration;

        ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));
        vibration.wLeftMotorSpeed = motorspeed;
        currentmotorspeed = motorspeed;
        pXInputSetState(0, &vibration);
    }
#endif
}

void I_PollThumbs_XInput_RightHanded(short LX, short LY, short RX, short RY)
{
    gamepadthumbLX = clamp(LX, gamepadleftdeadzone);
    gamepadthumbLY = -clamp(LY, gamepadleftdeadzone);
    gamepadthumbRX = clamp(RX, gamepadrightdeadzone);
    gamepadthumbRY = -clamp(RY, gamepadrightdeadzone);
}

void I_PollThumbs_XInput_LeftHanded(short LX, short LY, short RX, short RY)
{
    gamepadthumbLX = clamp(RX, gamepadrightdeadzone);
    gamepadthumbLY = -clamp(RY, gamepadrightdeadzone);
    gamepadthumbRX = clamp(LX, gamepadleftdeadzone);
    gamepadthumbRY = -clamp(LY, gamepadleftdeadzone);
}

void I_PollXInputGamepad(void)
{
#if defined(_WIN32)
    if (gamepad && !noinput)
    {
        XINPUT_STATE    state;
        XINPUT_GAMEPAD  Gamepad;

        ZeroMemory(&state, sizeof(XINPUT_STATE));
        pXInputGetState(0, &state);
        Gamepad = state.Gamepad;

        gamepadbuttons = Gamepad.wButtons
            | ((Gamepad.bLeftTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) << 10)
            | ((Gamepad.bRightTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) << 11);

        if (damagevibrationtics)
            if (!--damagevibrationtics && !weaponvibrationtics)
                XInputVibration(idlemotorspeed);

        if (weaponvibrationtics)
            if (!--weaponvibrationtics && !damagevibrationtics)
                XInputVibration(idlemotorspeed);

        if (gamepadbuttons)
        {
            vibrate = true;
            idclev = false;
            idmus = false;
            if (idbehold)
            {
                message_clearable = true;
                HU_ClearMessages();
                idbehold = false;
            }
        }

        if (gp_sensitivity || menuactive || (gamepadbuttons & gamepadmenu))
        {
            event_t      ev;

            ev.type = ev_gamepad;
            D_PostEvent(&ev);

            gamepadthumbsfunc(Gamepad.sThumbLX, Gamepad.sThumbLY, Gamepad.sThumbRX,
                Gamepad.sThumbRY);
        }
        else
        {
            gamepadbuttons = 0;
            gamepadthumbLX = 0;
            gamepadthumbLY = 0;
            gamepadthumbRX = 0;
            gamepadthumbRY = 0;
        }
    }
#endif
}

void I_SetGamepadSensitivity(int value)
{
    gamepadsensitivity = (!value ? 0.0f : GP_SENSITIVITY_OFFSET
        + GP_SENSITIVITY_FACTOR * value / gp_sensitivity_max);
}

void I_SetGamepadLeftDeadZone(float value)
{
    gamepadleftdeadzone = (short)(value * SHRT_MAX / 100.0f);
}

void I_SetGamepadRightDeadZone(float value)
{
    gamepadrightdeadzone = (short)(value * SHRT_MAX / 100.0f);
}
