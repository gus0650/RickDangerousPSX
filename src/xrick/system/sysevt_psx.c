/*
 * xrick/system/sysevt_psx.c -- PlayStation: controller input
 *
 * Mapping (controller port 1):
 *   D-pad        move / climb / crouch
 *   Cross        fire (with a direction: poke stick, drop dynamite, shoot)
 *   Square       fire, too
 *   Circle       jump (same as Up)
 *   Start        pause / resume
 *   Select+Start end the current game
 *
 * PlayStation port 2026.
 */

#include "xrick/system/system.h"
#include "xrick/config.h"
#include "xrick/control.h"
#include "xrick/game.h"

#include <psxapi.h>
#include <psxgpu.h>
#include <psxpad.h>

U8 syskbd_up, syskbd_down, syskbd_left, syskbd_right;
U8 syskbd_pause, syskbd_end, syskbd_xtra, syskbd_fire;

static uint8_t padBuffer[2][34];
static bool padStarted = false;
static U16 previousButtons = 0;

static void startPad(void)
{
    InitPAD(padBuffer[0], 34, padBuffer[1], 34);
    StartPAD();
    ChangeClearPAD(0);
    padStarted = true;
}

/* returns pressed buttons as set bits (the hardware reports them as 0) */
static U16 readButtons(void)
{
    PADTYPE *pad = (PADTYPE *)padBuffer[0];

    if (!padStarted)
    {
        startPad();
    }
    if (pad->stat != 0)
    {
        return 0;
    }
    if (pad->type != PAD_ID_DIGITAL &&
        pad->type != PAD_ID_ANALOG_STICK &&
        pad->type != PAD_ID_ANALOG)
    {
        return 0;
    }
    return (U16)~pad->btn;
}

static void setControl(bool on, control_t c)
{
    if (on)
    {
        control_set(c);
    }
    else
    {
        control_clear(c);
    }
}

static void applyButtons(U16 b, U16 prev)
{
    bool start = (b & PAD_START) != 0;
    bool select = (b & PAD_SELECT) != 0;

    (void)prev;
    setControl((b & (PAD_UP | PAD_CIRCLE)) != 0, Control_UP);
    setControl((b & PAD_DOWN) != 0, Control_DOWN);
    setControl((b & PAD_LEFT) != 0, Control_LEFT);
    setControl((b & PAD_RIGHT) != 0, Control_RIGHT);
    setControl((b & (PAD_CROSS | PAD_SQUARE)) != 0, Control_FIRE);

    /* like keyboard keys, these are "held" states; the game itself
       looks for press and release */
    setControl(start && !select, Control_PAUSE);
    setControl(start && select, Control_END);
}

void sysevt_poll(void)
{
    U16 b = readButtons();

    applyButtons(b, previousButtons);
    previousButtons = b;
}

void sysevt_wait(void)
{
    U16 b;

    /* block until the controller state changes */
    do
    {
        VSync(0);
        b = readButtons();
    }
    while (b == previousButtons);

    applyButtons(b, previousButtons);
    previousButtons = b;
}

/* eof */
