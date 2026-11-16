/*
 * xrick/system/sysvid_psx.c -- PlayStation: video
 *
 * xrick renders into a generic 320x200, 8-bit indexed frame buffer. Each
 * frame, that buffer is uploaded unchanged into VRAM as an 8-bit CLUT texture
 * and drawn with two textured sprites (a texture page is at most 256 pixels
 * wide in 8-bit mode), so the GPU does the palette lookup.
 *
 * VRAM layout (16-bit units):
 *   (0,0)   and (0,240)  two 320x240 display buffers
 *   (640,0)              160x200 texture = 320x200 8-bit pixels
 *   (640,256)            256-entry palette (CLUT)
 *
 * The display buffer is swapped in the vblank interrupt, so the game loop
 * never blocks on vsync and keeps its own 40 ms frame timing.
 *
 * PlayStation port 2026.
 */

#include "xrick/system/system.h"
#include "xrick/config.h"
#include "xrick/game.h"
#include "xrick/draw.h"

#include <string.h>
#include <psxapi.h>
#include <psxetc.h>
#include <psxgpu.h>

enum
{
    SCREEN_W = 320,
    SCREEN_H = 240,
    TEX_X = 640,
    TEX_Y = 0,
    CLUT_X = 640,
    CLUT_Y = 256,
    IMG_Y = (SCREEN_H - SYSVID_HEIGHT) / 2
};

U8 *sysvid_fb = NULL;

static U8 framebuffer[SYSVID_WIDTH * SYSVID_HEIGHT] __attribute__((aligned(4)));
static U16 clut[256] __attribute__((aligned(4)));
static bool clutDirty = false;

static DISPENV disp[2];
static DRAWENV draw[2];
static int backBuffer = 0;
static volatile int pendingSwap = -1;

typedef struct
{
    uint32_t ot[1];
    DR_TPAGE tpage[2];
    SPRT sprite[2];
} frame_prims_t;

static frame_prims_t prims[2];

static void vblankHandler(void)
{
    if (pendingSwap >= 0)
    {
        PutDispEnv(&disp[pendingSwap]);
        pendingSwap = -1;
    }
}

void sysvid_setPalette(img_color_t *pal, U16 n)
{
    U16 i;

    for (i = 0; i < n && i < 256; i++)
    {
        U16 c = (U16)((pal[i].r >> 3) | ((pal[i].g >> 3) << 5) | ((pal[i].b >> 3) << 10));
        /* 0x0000 is "transparent" for the GPU; black stays black because the
           background is cleared to black anyway */
        clut[i] = c;
    }
    clutDirty = true;
}

void sysvid_setGamePalette(void)
{
    sysvid_setPalette(game_colors, game_color_count);
}

void sysvid_update(const rect_t *rects)
{
    RECT r;
    frame_prims_t *p;
    int buf;

    if (!rects)
    {
        return;
    }

    /* wait until the previous frame's GPU work is done */
    DrawSync(0);

    if (clutDirty)
    {
        setRECT(&r, CLUT_X, CLUT_Y, 256, 1);
        LoadImage(&r, (const uint32_t *)clut);
        clutDirty = false;
    }

    /* upload the whole frame: 64000 bytes by DMA, cheaper than tracking rects */
    setRECT(&r, TEX_X, TEX_Y, SYSVID_WIDTH / 2, SYSVID_HEIGHT);
    LoadImage(&r, (const uint32_t *)framebuffer);

    /* draw into the back buffer (clears it to black first) */
    buf = backBuffer;
    PutDrawEnv(&draw[buf]);

    p = &prims[buf];
    ClearOTagR(p->ot, 1);

    /* left part: pixels 0..255 */
    setSprt(&p->sprite[0]);
    setShadeTex(&p->sprite[0], 1);
    setXY0(&p->sprite[0], 0, IMG_Y);
    setWH(&p->sprite[0], 256, SYSVID_HEIGHT);
    setUV0(&p->sprite[0], 0, 0);
    setClut(&p->sprite[0], CLUT_X, CLUT_Y);
    addPrim(p->ot, &p->sprite[0]);
    setDrawTPage(&p->tpage[0], 0, 0, getTPage(1, 0, TEX_X, TEX_Y));
    addPrim(p->ot, &p->tpage[0]);

    /* right part: pixels 256..319 (next texture page, 128 VRAM units on) */
    setSprt(&p->sprite[1]);
    setShadeTex(&p->sprite[1], 1);
    setXY0(&p->sprite[1], 256, IMG_Y);
    setWH(&p->sprite[1], SYSVID_WIDTH - 256, SYSVID_HEIGHT);
    setUV0(&p->sprite[1], 0, 0);
    setClut(&p->sprite[1], CLUT_X, CLUT_Y);
    addPrim(p->ot, &p->sprite[1]);
    setDrawTPage(&p->tpage[1], 0, 0, getTPage(1, 0, TEX_X + 128, TEX_Y));
    addPrim(p->ot, &p->tpage[1]);

    DrawOTag(p->ot);

    /* show it at the next vblank */
    DrawSync(0);
    pendingSwap = buf;
    backBuffer = 1 - buf;
}

void sysvid_clear(void)
{
    memset(sysvid_fb, 0, sizeof(framebuffer));
}

void sysvid_zoom(S8 z)
{
    (void)z;
}

void sysvid_toggleFullscreen(void)
{
}

bool sysvid_init(void)
{
    ResetGraph(0);

    SetDefDispEnv(&disp[0], 0, 0, SCREEN_W, SCREEN_H);
    SetDefDispEnv(&disp[1], 0, SCREEN_H, SCREEN_W, SCREEN_H);
    SetDefDrawEnv(&draw[0], 0, SCREEN_H, SCREEN_W, SCREEN_H);
    SetDefDrawEnv(&draw[1], 0, 0, SCREEN_W, SCREEN_H);

    if (GetVideoMode() == MODE_PAL)
    {
        /* centre the 240-line picture in the taller PAL frame */
        disp[0].screen.y = disp[1].screen.y = 24;
    }

    /* draw buffer i is shown by disp i: draw[0] targets y=240 = disp[1] area,
       so swap the pairing to keep "draw buf" and "show buf" the same index */
    {
        DRAWENV t = draw[0];
        draw[0] = draw[1];
        draw[1] = t;
    }

    setRGB0(&draw[0], 0, 0, 0);
    setRGB0(&draw[1], 0, 0, 0);
    draw[0].isbg = 1;
    draw[1].isbg = 1;

    memset(framebuffer, 0, sizeof(framebuffer));
    memset(clut, 0, sizeof(clut));
    sysvid_fb = framebuffer;

    PutDispEnv(&disp[0]);
    PutDrawEnv(&draw[0]);
    DrawSync(0);
    SetDispMask(1);

    backBuffer = 1;
    EnterCriticalSection();
    VSyncCallback(&vblankHandler);
    ExitCriticalSection();

    return true;
}

void sysvid_shutdown(void)
{
    VSyncCallback(NULL);
    sysvid_fb = NULL;
}

/* eof */
