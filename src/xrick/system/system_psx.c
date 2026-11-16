/*
 * xrick/system/system_psx.c -- PlayStation: main, timing, args, messages
 *
 * Copyright (C) 1998-2002 BigOrno (bigorno@bigorno.net).
 * Copyright (C) 2008-2014 Pierluigi Vicinanza.
 * PlayStation port 2026.
 * All rights reserved.
 *
 * The use and distribution terms for this software are contained in the file
 * named README, which can be found in the root of this distribution. By
 * using this software in any fashion, you are agreeing to be bound by the
 * terms of this license.
 *
 * You must not remove this notice, or any other, from this software.
 */

#include "xrick/system/system.h"
#include "xrick/config.h"
#include "xrick/game.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <psxapi.h>
#include <psxetc.h>
#include <psxgpu.h>
#include <hwregs_c.h>

/*
 * millisecond clock, driven by root counter 2 (system clock / 8)
 */
static volatile U32 msTicks = 0;

static void timer2Handler(void)
{
    msTicks++;
}

static void startClock(void)
{
    /* 33.8688 MHz / 8 = 4233600 Hz -> 4234 counts per millisecond */
    TIMER_CTRL(2)   = 0x0258;   /* clk/8, reset at target, repeated IRQ at target */
    TIMER_RELOAD(2) = 4234;
    EnterCriticalSection();
    InterruptCallback(IRQ_TIMER2, &timer2Handler);
    ExitCriticalSection();
}

U32 sys_gettime(void)
{
    return msTicks;
}

void sys_yield(void)
{
    /* single threaded: nothing to yield to */
}

/*
 * messages (go to the TTY / emulator log)
 */
void sys_error(const char *err, ...)
{
    va_list argptr;
    char s[128];

    va_start(argptr, err);
    vsnprintf(s, sizeof(s), err, argptr);
    va_end(argptr);
    printf("xrick error: %s\n", s);
}

void sys_printf(const char *msg, ...)
{
    va_list argptr;
    char s[128];

    va_start(argptr, msg);
    vsnprintf(s, sizeof(s), msg, argptr);
    va_end(argptr);
    printf("%s", s);
}

void sys_snprintf(char *buf, size_t size, const char *msg, ...)
{
    va_list argptr;

    va_start(argptr, msg);
    vsnprintf(buf, size, msg, argptr);
    va_end(argptr);
}

size_t sys_strlen(const char * str)
{
    return strlen(str);
}

/*
 * arguments (none on the PlayStation)
 */
int sysarg_args_period = 0;
int sysarg_args_map = 0;
int sysarg_args_submap = 0;
int sysarg_args_fullscreen = 0;
int sysarg_args_zoom = 0;
#ifdef ENABLE_SOUND
bool sysarg_args_nosound = false;
int sysarg_args_vol = 0;
#endif
const char *sysarg_args_data = NULL;

bool sysarg_init(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return true;
}

/*
 * init / shutdown
 */
bool sys_init(int argc, char **argv)
{
    if (!sysarg_init(argc, argv)) return false;
    if (!sysmem_init()) return false;
    if (!sysvid_init()) return false;
    startClock();
#ifdef ENABLE_SOUND
    if (!syssnd_init()) return false;
#endif
    if (!sysfile_setRootPath(sysfile_defaultPath)) return false;
    return true;
}

void sys_shutdown(void)
{
    sysfile_clearRootPath();
#ifdef ENABLE_SOUND
    syssnd_shutdown();
#endif
    sysvid_shutdown();
    sysmem_shutdown();
}

/* sound effects live in SPU RAM from the start, music on CD: nothing to cache */
bool sys_cacheData(void)
{
    return true;
}

void sys_uncacheData(void)
{
}

int main(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    if (sys_init(0, NULL))
    {
        game_run();
    }
    sys_shutdown();

    /* there is nowhere to exit to: idle forever */
    for (;;)
    {
        VSync(0);
    }
    return 0;
}

/* eof */
