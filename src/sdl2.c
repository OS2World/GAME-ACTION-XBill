/* sdl2.c -- SDL2 UI backend for xBill (ArcaOS port)
 *
 * Replaces x11.c + x11-athena.c entirely.
 * Implements all UI_methods function pointers.
 *
 * Strategy:
 *  - All XPM pixmaps included as C arrays; loaded via IMG_ReadXPMFromArray.
 *  - XBM cursor bitmaps embedded directly; cursor bits reversed (XBM=LSB-first,
 *    SDL=MSB-first).
 *  - Menus: clicking "Game" or "Info" label shows a native PM SDL_ShowMessageBox
 *    so no SDL-drawn dropdown is needed.
 *  - Dialogs: SDL_ShowSimpleMessageBox / SDL_ShowMessageBox (native PM).
 *  - Text input (warp level, enter name): SDL text-input event sub-loop drawn
 *    over the renderer.
 *  - Timer: SDL_AddTimer one-shot callback pushes a user event; main loop
 *    re-arms before calling Game_update (mirrors X11 timer_tick behaviour).
 */

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "config.h"
#include "types.h"
#include "util.h"
#include "strings.h"
#include "Game.h"
#include "UI.h"
#include "sdl2.h"

/* -------------------------------------------------------------------------
 * Struct definitions (opaque in types.h)
 * ---------------------------------------------------------------------- */

struct Picture {
    SDL_Texture *tex;
    int width, height;
};

struct MCursor {
    SDL_Texture *tex;
    int w, h, hot_x, hot_y;
};

/* -------------------------------------------------------------------------
 * XPM includes -- all pixmaps embedded as static C arrays
 * ---------------------------------------------------------------------- */

#include "pixmaps/about.xpm"
#include "pixmaps/apple.xpm"
#include "pixmaps/billA_0.xpm"
#include "pixmaps/billA_1.xpm"
#include "pixmaps/billA_2.xpm"
#include "pixmaps/billA_3.xpm"
#include "pixmaps/billA_4.xpm"
#include "pixmaps/billA_5.xpm"
#include "pixmaps/billA_6.xpm"
#include "pixmaps/billA_7.xpm"
#include "pixmaps/billA_8.xpm"
#include "pixmaps/billA_9.xpm"
#include "pixmaps/billA_10.xpm"
#include "pixmaps/billA_11.xpm"
#include "pixmaps/billA_12.xpm"
#include "pixmaps/billD_0.xpm"
#include "pixmaps/billD_1.xpm"
#include "pixmaps/billD_2.xpm"
#include "pixmaps/billD_3.xpm"
#include "pixmaps/billD_4.xpm"
#include "pixmaps/billL_0.xpm"
#include "pixmaps/billL_1.xpm"
#include "pixmaps/billL_2.xpm"
#include "pixmaps/billR_0.xpm"
#include "pixmaps/billR_1.xpm"
#include "pixmaps/billR_2.xpm"
#include "pixmaps/bsd.xpm"
#include "pixmaps/bsdcpu.xpm"
#include "pixmaps/bucket.xpm"
#include "pixmaps/hurd.xpm"
#include "pixmaps/icon.xpm"
#include "pixmaps/linux.xpm"
#include "pixmaps/logo.xpm"
#include "pixmaps/maccpu.xpm"
#include "pixmaps/next.xpm"
#include "pixmaps/nextcpu.xpm"
#include "pixmaps/os2.xpm"
#include "pixmaps/os2cpu.xpm"
#include "pixmaps/palm.xpm"
#include "pixmaps/palmcpu.xpm"
#include "pixmaps/redhat.xpm"
#include "pixmaps/sgi.xpm"
#include "pixmaps/sgicpu.xpm"
#include "pixmaps/spark_0.xpm"
#include "pixmaps/spark_1.xpm"
#include "pixmaps/sun.xpm"
#include "pixmaps/suncpu.xpm"
#include "pixmaps/toaster.xpm"
#include "pixmaps/wingdows.xpm"

/* Name → xpm data lookup table */
typedef struct { const char *name; char **data; } XpmEntry;
static XpmEntry xpm_table[] = {
    {"about",    about_xpm},
    {"apple",    apple_xpm},
    {"billA_0",  billA_0_xpm},
    {"billA_1",  billA_1_xpm},
    {"billA_2",  billA_2_xpm},
    {"billA_3",  billA_3_xpm},
    {"billA_4",  billA_4_xpm},
    {"billA_5",  billA_5_xpm},
    {"billA_6",  billA_6_xpm},
    {"billA_7",  billA_7_xpm},
    {"billA_8",  billA_8_xpm},
    {"billA_9",  billA_9_xpm},
    {"billA_10", billA_10_xpm},
    {"billA_11", billA_11_xpm},
    {"billA_12", billA_12_xpm},
    {"billD_0",  billD_0_xpm},
    {"billD_1",  billD_1_xpm},
    {"billD_2",  billD_2_xpm},
    {"billD_3",  billD_3_xpm},
    {"billD_4",  billD_4_xpm},
    {"billL_0",  billL_0_xpm},
    {"billL_1",  billL_1_xpm},
    {"billL_2",  billL_2_xpm},
    {"billR_0",  billR_0_xpm},
    {"billR_1",  billR_1_xpm},
    {"billR_2",  billR_2_xpm},
    {"bsd",      bsd_xpm},
    {"bsdcpu",   bsdcpu_xpm},
    {"bucket",   bucket_xpm},
    {"hurd",     hurd_xpm},
    {"icon",     icon_xpm},
    {"linux",    linux_xpm},
    {"logo",     logo_xpm},
    {"maccpu",   maccpu_xpm},
    {"next",     next_xpm},
    {"nextcpu",  nextcpu_xpm},
    {"os2",      os2_xpm},
    {"os2cpu",   os2cpu_xpm},
    {"palm",     palm_xpm},
    {"palmcpu",  palmcpu_xpm},
    {"redhat",   redhat_xpm},
    {"sgi",      sgi_xpm},
    {"sgicpu",   sgicpu_xpm},
    {"spark_0",  spark_0_xpm},
    {"spark_1",  spark_1_xpm},
    {"sun",      sun_xpm},
    {"suncpu",   suncpu_xpm},
    {"toaster",  toaster_xpm},
    {"wingdows", wingdows_xpm},
    {NULL, NULL}
};

/* -------------------------------------------------------------------------
 * XBM cursor data (embedded; bit order reversed below for SDL)
 * ---------------------------------------------------------------------- */

#include "bitmaps/hand_up.xbm"
#include "bitmaps/hand_up_mask.xbm"
#include "bitmaps/hand_down.xbm"
#include "bitmaps/hand_down_mask.xbm"
#include "bitmaps/bucket.xbm"

/* -------------------------------------------------------------------------
 * Embedded 8×8 bitmap font (public domain, IBM CP437 glyphs 32-127)
 * ---------------------------------------------------------------------- */

static const Uint8 font8x8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 32 ' ' */
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, /* 33 '!' */
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, /* 34 '"' */
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, /* 35 '#' */
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, /* 36 '$' */
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, /* 37 '%' */
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, /* 38 '&' */
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, /* 39 ''' */
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, /* 40 '(' */
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, /* 41 ')' */
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, /* 42 '*' */
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, /* 43 '+' */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, /* 44 ',' */
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, /* 45 '-' */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, /* 46 '.' */
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, /* 47 '/' */
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, /* 48 '0' */
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, /* 49 '1' */
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, /* 50 '2' */
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, /* 51 '3' */
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, /* 52 '4' */
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, /* 53 '5' */
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, /* 54 '6' */
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, /* 55 '7' */
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, /* 56 '8' */
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, /* 57 '9' */
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, /* 58 ':' */
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, /* 59 ';' */
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, /* 60 '<' */
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, /* 61 '=' */
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, /* 62 '>' */
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, /* 63 '?' */
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, /* 64 '@' */
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, /* 65 'A' */
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, /* 66 'B' */
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, /* 67 'C' */
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, /* 68 'D' */
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, /* 69 'E' */
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, /* 70 'F' */
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, /* 71 'G' */
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, /* 72 'H' */
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 73 'I' */
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, /* 74 'J' */
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, /* 75 'K' */
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, /* 76 'L' */
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, /* 77 'M' */
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, /* 78 'N' */
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, /* 79 'O' */
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, /* 80 'P' */
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, /* 81 'Q' */
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, /* 82 'R' */
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, /* 83 'S' */
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 84 'T' */
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, /* 85 'U' */
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, /* 86 'V' */
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, /* 87 'W' */
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, /* 88 'X' */
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, /* 89 'Y' */
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, /* 90 'Z' */
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, /* 91 '[' */
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, /* 92 '\' */
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, /* 93 ']' */
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, /* 94 '^' */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, /* 95 '_' */
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, /* 96 '`' */
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, /* 97 'a' */
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, /* 98 'b' */
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, /* 99 'c' */
    {0x38,0x30,0x30,0x3e,0x33,0x33,0x6E,0x00}, /* 100 'd' */
    {0x00,0x00,0x1E,0x33,0x3f,0x03,0x1E,0x00}, /* 101 'e' */
    {0x1C,0x36,0x06,0x0f,0x06,0x06,0x0F,0x00}, /* 102 'f' */
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, /* 103 'g' */
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, /* 104 'h' */
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, /* 105 'i' */
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, /* 106 'j' */
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, /* 107 'k' */
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 108 'l' */
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, /* 109 'm' */
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, /* 110 'n' */
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, /* 111 'o' */
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, /* 112 'p' */
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, /* 113 'q' */
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, /* 114 'r' */
    {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, /* 115 's' */
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, /* 116 't' */
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, /* 117 'u' */
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, /* 118 'v' */
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, /* 119 'w' */
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, /* 120 'x' */
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, /* 121 'y' */
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, /* 122 'z' */
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, /* 123 '{' */
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, /* 124 '|' */
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, /* 125 '}' */
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, /* 126 '~' */
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, /* 127 DEL (block) */
};

/* -------------------------------------------------------------------------
 * Global state
 * ---------------------------------------------------------------------- */

#define MENU_H 22

static SDL_Window   *gWin        = NULL;
static SDL_Renderer *gRen        = NULL;
static int           gScreensize = 0; /* game logical size (SCREENSIZE) */
static int           gDispSize   = 0; /* game display pixels (gScreensize * scale) */
static int           gWinW       = 0; /* actual window width */
static SDL_Texture  *gGameTex    = NULL; /* render target for game area */
static int           gTimerActive = 0;
static SDL_TimerID   gTimer;
static Uint32        gTimerEvent;
static int           gPauseEnabled = 0;
static int           gPaused       = 0; /* 1 while game is paused by user */

/* Software cursor state */
static SDL_Texture  *gCursorTex  = NULL;
static int           gCursorW    = 0;
static int           gCursorH    = 0;
static int           gCursorHotX = 0;
static int           gCursorHotY = 0;
static int           gMouseX     = 0;
static int           gMouseY     = 0;

/* Menu layout: two clickable labels */
static struct { const char *label; int x, w; } gMenuBar[2];

/* Dialog text updated by UI_update_dialog */
static char gDialogText[DIALOG_MAX + 1][2048];

/* -------------------------------------------------------------------------
 * Viewport helpers
 * ---------------------------------------------------------------------- */

/* Draw subsequent calls into the game texture */
static void set_game_vp(void) {
    SDL_SetRenderTarget(gRen, gGameTex);
    SDL_RenderSetViewport(gRen, NULL);
}

/* Draw subsequent calls directly to the screen */
static void clear_vp(void) {
    SDL_SetRenderTarget(gRen, NULL);
    SDL_RenderSetViewport(gRen, NULL);
}

/* -------------------------------------------------------------------------
 * 8×8 font rendering (draws within whatever viewport is active)
 * ---------------------------------------------------------------------- */

static void blit_char(int cx, int cy, char ch, Uint8 r, Uint8 g, Uint8 b)
{
    int idx = (unsigned char)ch - 32;
    if (idx < 0 || idx >= 96) return;
    SDL_SetRenderDrawColor(gRen, r, g, b, 255);
    for (int row = 0; row < 8; row++) {
        Uint8 bits = font8x8[idx][row];
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << col)) {
                SDL_RenderDrawPoint(gRen, cx + col, cy + row);
            }
        }
    }
}

static void blit_str(int x, int y, const char *s, Uint8 r, Uint8 g, Uint8 b)
{
    int cx = x;
    for (; *s; s++) {
        if (*s == '\n') { cx = x; y += 10; continue; }
        blit_char(cx, y, *s, r, g, b);
        cx += 8;
    }
}

/* Scaled font rendering: each font pixel becomes an s×s block */
static void blit_char_s(int cx, int cy, char ch, Uint8 r, Uint8 g, Uint8 b, int s)
{
    int idx = (unsigned char)ch - 32;
    if (idx < 0 || idx >= 96) return;
    SDL_SetRenderDrawColor(gRen, r, g, b, 255);
    for (int row = 0; row < 8; row++) {
        Uint8 bits = font8x8[idx][row];
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << col)) {
                SDL_Rect px = {cx + col * s, cy + row * s, s, s};
                SDL_RenderFillRect(gRen, &px);
            }
        }
    }
}

static void blit_str_s(int x, int y, const char *str,
                       Uint8 r, Uint8 g, Uint8 b, int s)
{
    int cx = x;
    for (; *str; str++) {
        if (*str == '\n') { cx = x; y += 10 * s; continue; }
        blit_char_s(cx, y, *str, r, g, b, s);
        cx += 8 * s;
    }
}

/* -------------------------------------------------------------------------
 * Dropdown menu data and helpers
 * ---------------------------------------------------------------------- */

#define DROPDOWN_W      280
#define DROPDOWN_ITEM_H 22
#define DROPDOWN_PAD_X  10
#define DROPDOWN_SEP_H  6
#define DROPDOWN_FSCALE 2   /* font scale inside dropdown items */

typedef struct { const char *label; int dialog; int is_sep; } DropItem;

static DropItem gGameItems[] = {
    {"New Game",          DIALOG_NEWGAME,    0},
    {"Warp to level...",  DIALOG_WARPLEVEL,  0},
    {"View High Scores",  DIALOG_HIGHSCORE,  0},
    {"",                  -1,                1},
    {"Pause",             DIALOG_PAUSEGAME,  0},
    {"",                  -1,                1},
    {"Quit Game",         DIALOG_QUITGAME,   0},
};
#define GAME_ITEMS_N 7

static DropItem gInfoItems[] = {
    {"Story", DIALOG_STORY, 0},
    {"Rules", DIALOG_RULES, 0},
    {"About", DIALOG_ABOUT, 0},
};
#define INFO_ITEMS_N 3

static int dropdown_height(DropItem *items, int n)
{
    int h = 2; /* top/bottom padding */
    for (int i = 0; i < n; i++)
        h += items[i].is_sep ? DROPDOWN_SEP_H : DROPDOWN_ITEM_H;
    return h;
}

/* Returns item index under (mx,my), or -1 if outside */
static int item_at(int dx, int dy, DropItem *items, int n, int mx, int my)
{
    if (mx < dx || mx >= dx + DROPDOWN_W) return -1;
    int y = dy + 1;
    for (int i = 0; i < n; i++) {
        int ih = items[i].is_sep ? DROPDOWN_SEP_H : DROPDOWN_ITEM_H;
        if (my >= y && my < y + ih) return i;
        y += ih;
    }
    return -1;
}

static void draw_dropdown(int dx, int dy, DropItem *items, int n, int hover)
{
    clear_vp();
    int total_h = dropdown_height(items, n);

    /* Shadow */
    SDL_SetRenderDrawColor(gRen, 100, 100, 100, 255);
    SDL_Rect shadow = {dx + 2, dy + 2, DROPDOWN_W, total_h};
    SDL_RenderFillRect(gRen, &shadow);

    /* Background */
    SDL_SetRenderDrawColor(gRen, 240, 240, 240, 255);
    SDL_Rect bg = {dx, dy, DROPDOWN_W, total_h};
    SDL_RenderFillRect(gRen, &bg);

    /* Border */
    SDL_SetRenderDrawColor(gRen, 100, 100, 100, 255);
    SDL_RenderDrawRect(gRen, &bg);

    int y = dy + 1;
    for (int i = 0; i < n; i++) {
        if (items[i].is_sep) {
            SDL_SetRenderDrawColor(gRen, 180, 180, 180, 255);
            SDL_RenderDrawLine(gRen, dx + 4, y + 2,
                               dx + DROPDOWN_W - 4, y + 2);
            y += DROPDOWN_SEP_H;
        } else {
            int disabled = (items[i].dialog == DIALOG_PAUSEGAME && !gPauseEnabled);
            int fh = 8 * DROPDOWN_FSCALE;
            int ty = y + (DROPDOWN_ITEM_H - fh) / 2;
            if (i == hover && !disabled) {
                SDL_SetRenderDrawColor(gRen, 10, 36, 106, 255);
                SDL_Rect hi = {dx + 1, y, DROPDOWN_W - 2, DROPDOWN_ITEM_H};
                SDL_RenderFillRect(gRen, &hi);
                blit_str_s(dx + DROPDOWN_PAD_X, ty, items[i].label,
                           255, 255, 255, DROPDOWN_FSCALE);
            } else {
                Uint8 tc = disabled ? 130 : 0;
                blit_str_s(dx + DROPDOWN_PAD_X, ty, items[i].label,
                           tc, tc, tc, DROPDOWN_FSCALE);
            }
            y += DROPDOWN_ITEM_H;
        }
    }
}

/* -------------------------------------------------------------------------
 * Menu bar drawing
 * ---------------------------------------------------------------------- */

static void draw_menu_bar_open(int open_idx)
{
    clear_vp();

    SDL_SetRenderDrawColor(gRen, 212, 208, 200, 255);
    SDL_Rect bg = {0, 0, gWinW, MENU_H};
    SDL_RenderFillRect(gRen, &bg);

    SDL_SetRenderDrawColor(gRen, 128, 128, 128, 255);
    SDL_RenderDrawLine(gRen, 0, MENU_H - 1, gWinW, MENU_H - 1);

    const char *labels[] = {"Game", "Info"};
    int x = 4;
    int fscale = 2;                        /* 2× font for menu bar labels */
    int fh = 8 * fscale;                   /* 16px glyph height */
    for (int i = 0; i < 2; i++) {
        int tw = (int)strlen(labels[i]) * 8 * fscale;
        int padx = 6;
        gMenuBar[i].x = x;
        gMenuBar[i].w = tw + padx * 2;
        gMenuBar[i].label = labels[i];

        if (i == open_idx) {
            SDL_SetRenderDrawColor(gRen, 10, 36, 106, 255);
            SDL_Rect sel = {x, 0, gMenuBar[i].w, MENU_H - 1};
            SDL_RenderFillRect(gRen, &sel);
            blit_str_s(x + padx, (MENU_H - fh) / 2, labels[i],
                       255, 255, 255, fscale);
        } else {
            blit_str_s(x + padx, (MENU_H - fh) / 2, labels[i],
                       0, 0, 0, fscale);
        }
        x += gMenuBar[i].w + 4;
    }
}

static void draw_menu_bar(void)
{
    draw_menu_bar_open(-1);
}

/* -------------------------------------------------------------------------
 * Timer
 * ---------------------------------------------------------------------- */

static Uint32 timer_cb(Uint32 interval, void *param)
{
    UNUSED(interval);
    UNUSED(param);
    SDL_Event e;
    SDL_memset(&e, 0, sizeof(e));
    e.type = gTimerEvent;
    SDL_PushEvent(&e);
    return 0; /* one-shot */
}

static void sdl2_start_timer(int ms)
{
    if (!gTimerActive) {
        gTimerActive = 1;
        gTimer = SDL_AddTimer((Uint32)ms, timer_cb, NULL);
    }
}

static void sdl2_stop_timer(void)
{
    if (gTimerActive) {
        SDL_RemoveTimer(gTimer);
        gTimerActive = 0;
    }
}

static int sdl2_timer_active(void)
{
    return gTimerActive;
}

/* -------------------------------------------------------------------------
 * XBM → SDL_Texture (software cursor, bypasses SDL cursor API)
 * XBM uses LSB-first bit order within each byte.
 * ---------------------------------------------------------------------- */

static SDL_Texture *xbm_to_texture(const char *data_bits,
                                   const char *mask_bits,
                                   int w, int h)
{
    SDL_Surface *surf = SDL_CreateRGBSurface(0, w, h, 32,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (!surf) return NULL;

    SDL_FillRect(surf, NULL, 0x00000000); /* fully transparent */

    int stride = (w + 7) / 8;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int byte_idx = row * stride + col / 8;
            int bit      = 1 << (col % 8); /* XBM: LSB = leftmost */
            int is_data  = ((unsigned char)data_bits[byte_idx] & bit) != 0;
            int is_mask  = ((unsigned char)mask_bits[byte_idx] & bit) != 0;
            if (!is_mask) continue; /* transparent */
            Uint32 color = is_data
                ? SDL_MapRGBA(surf->format, 0,   0,   0,   255) /* black */
                : SDL_MapRGBA(surf->format, 255, 255, 255, 255); /* white */
            Uint32 *px = (Uint32 *)((Uint8 *)surf->pixels
                                    + row * surf->pitch + col * 4);
            *px = color;
        }
    }

    SDL_Texture *tex = SDL_CreateTextureFromSurface(gRen, surf);
    SDL_FreeSurface(surf);
    if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    return tex;
}

/* -------------------------------------------------------------------------
 * Picture loading
 * ---------------------------------------------------------------------- */

static char **find_xpm(const char *name)
{
    for (int i = 0; xpm_table[i].name != NULL; i++) {
        if (strcmp(xpm_table[i].name, name) == 0)
            return xpm_table[i].data;
    }
    return NULL;
}

static void sdl2_load_picture(const char *name, int trans, Picture **pictp)
{
    char **xpm = find_xpm(name);
    if (!xpm) fatal("sdl2_load_picture: no XPM for '%s'", name);

    SDL_Surface *surf = IMG_ReadXPMFromArray(xpm);
    if (!surf) fatal("sdl2_load_picture: IMG_ReadXPMFromArray failed for '%s': %s",
                     name, IMG_GetError());

    /* For non-transparent pictures, blit onto a white surface to fill "None" */
    if (!trans) {
        SDL_Surface *bg = SDL_CreateRGBSurface(0, surf->w, surf->h, 32,
                                               0xFF000000, 0x00FF0000,
                                               0x0000FF00, 0x000000FF);
        SDL_FillRect(bg, NULL, SDL_MapRGB(bg->format, 255, 255, 255));
        SDL_BlitSurface(surf, NULL, bg, NULL);
        SDL_FreeSurface(surf);
        surf = bg;
    }

    SDL_Texture *tex = SDL_CreateTextureFromSurface(gRen, surf);
    if (!tex) fatal("sdl2_load_picture: SDL_CreateTextureFromSurface failed for '%s'", name);

    SDL_SetTextureBlendMode(tex, trans ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);

    Picture *pict = (Picture *)xalloc(sizeof *pict);
    pict->tex    = tex;
    pict->width  = surf->w;
    pict->height = surf->h;
    SDL_FreeSurface(surf);

    *pictp = pict;
}

static void sdl2_load_picture_indexed(const char *name, int index, int trans,
                                      Picture **pictp)
{
    char key[128];
    snprintf(key, sizeof(key), "%s_%d", name, index);
    sdl2_load_picture(key, trans, pictp);
}

static int sdl2_picture_width(Picture *pict)  { return pict->width;  }
static int sdl2_picture_height(Picture *pict) { return pict->height; }

/* -------------------------------------------------------------------------
 * Cursor loading
 * ---------------------------------------------------------------------- */

static void sdl2_load_cursor(const char *name, int masked, MCursor **cursorp)
{
    SDL_Texture *tex = NULL;
    int w = 16, h = 16, hot_x = 8, hot_y = 8;

    if (strcmp(name, "hand_up") == 0) {
        tex = xbm_to_texture(hand_up_bits, hand_up_mask_bits,
                             hand_up_width, hand_up_height);
        w = hand_up_width; h = hand_up_height;
        hot_x = 5; hot_y = 0; /* fingertip */
    } else if (strcmp(name, "hand_down") == 0) {
        tex = xbm_to_texture(hand_down_bits, hand_down_mask_bits,
                             hand_down_width, hand_down_height);
        w = hand_down_width; h = hand_down_height;
        hot_x = 5; hot_y = 0;
    } else if (strcmp(name, "bucket") == 0) {
        tex = xbm_to_texture(bucket_bits, bucket_bits,
                             bucket_width, bucket_height);
        w = bucket_width; h = bucket_height;
        hot_x = w / 2; hot_y = h / 2;
    } else {
        /* OS logo cursors — use the color XPM sprite */
        char **xpm = find_xpm(name);
        if (xpm) {
            SDL_Surface *surf = IMG_ReadXPMFromArray(xpm);
            if (surf) {
                tex = SDL_CreateTextureFromSurface(gRen, surf);
                if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                w = surf->w; h = surf->h;
                hot_x = w / 2; hot_y = h / 2;
                SDL_FreeSurface(surf);
            }
        }
        if (!tex)
            fprintf(stderr, "sdl2_load_cursor: unknown cursor '%s'\n", name);
    }

    UNUSED(masked);

    MCursor *cur = (MCursor *)xalloc(sizeof *cur);
    cur->tex   = tex;
    cur->w     = w;
    cur->h     = h;
    cur->hot_x = hot_x;
    cur->hot_y = hot_y;
    *cursorp = cur;
}

static void sdl2_set_cursor(MCursor *cursor)
{
    if (!cursor) return;
    gCursorTex  = cursor->tex;
    gCursorW    = cursor->w;
    gCursorH    = cursor->h;
    gCursorHotX = cursor->hot_x;
    gCursorHotY = cursor->hot_y;
}

static void sdl2_set_icon(Picture *icon)
{
    UNUSED(icon);
    /* SDL_SetWindowIcon requires an SDL_Surface; skip for now */
}

/* -------------------------------------------------------------------------
 * Graphics operations
 * ---------------------------------------------------------------------- */

static void sdl2_graphics_init(void)
{
    SDL_SetRenderDrawColor(gRen, 255, 255, 255, 255);
    SDL_RenderClear(gRen);
}

static void sdl2_clear_window(void)
{
    SDL_SetRenderTarget(gRen, gGameTex);
    SDL_SetRenderDrawColor(gRen, 255, 255, 255, 255);
    SDL_RenderClear(gRen);
}

static void sdl2_refresh_window(void)
{
    /* Composite onto screen: background, game texture (scaled), cursor, menu bar */
    clear_vp();

    /* Gray background fills the right strip (beyond game area) */
    SDL_SetRenderDrawColor(gRen, 180, 180, 180, 255);
    SDL_RenderClear(gRen);

    /* Game texture scaled 2x */
    SDL_Rect gameDst = {0, MENU_H, gDispSize, gDispSize};
    SDL_RenderCopy(gRen, gGameTex, NULL, &gameDst);

    /* PAUSED overlay */
    if (gPaused) {
        SDL_SetRenderDrawBlendMode(gRen, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(gRen, 0, 0, 0, 110);
        SDL_Rect over = {0, MENU_H, gDispSize, gDispSize};
        SDL_RenderFillRect(gRen, &over);
        SDL_SetRenderDrawBlendMode(gRen, SDL_BLENDMODE_NONE);
        const char *ptxt = "PAUSED";
        int tw = (int)(strlen(ptxt)) * 8 * 3;
        blit_str_s((gDispSize - tw) / 2,
                   gDispSize / 2 - 12 + MENU_H,
                   ptxt, 255, 220, 60, 3);
    }

    /* Software cursor — scale hotspot and size by display scale */
    if (gCursorTex && gMouseY >= MENU_H && gMouseX < gDispSize) {
        int scale = gDispSize / gScreensize;
        int cx = gMouseX - gCursorHotX * scale;
        int cy = gMouseY - gCursorHotY * scale;
        SDL_Rect dst = {cx, cy, gCursorW * scale, gCursorH * scale};
        SDL_RenderCopy(gRen, gCursorTex, NULL, &dst);
    }

    draw_menu_bar();
    SDL_RenderPresent(gRen);

    /* Leave render target on game texture for subsequent draw calls */
    SDL_SetRenderTarget(gRen, gGameTex);
}

static void sdl2_draw_image(Picture *pict, int x, int y)
{
    SDL_Rect dst = {x, y, pict->width, pict->height};
    SDL_RenderCopy(gRen, pict->tex, NULL, &dst);
}

static void sdl2_draw_line(int x1, int y1, int x2, int y2)
{
    SDL_SetRenderDrawColor(gRen, 0, 0, 0, 255);
    SDL_RenderDrawLine(gRen, x1, y1, x2, y2);
}

static void sdl2_draw_string(const char *str, int x, int y)
{
    /* y in xbill is baseline; shift up by font height */
    blit_str(x, y - 8, str, 0, 0, 0);
}

/* -------------------------------------------------------------------------
 * Text-input overlay (for DIALOG_WARPLEVEL and DIALOG_ENTERNAME)
 * Returns 1 on Enter (text in buf), 0 on Escape.
 * ---------------------------------------------------------------------- */

static void draw_input_box(const char *prompt, const char *buf)
{
    int W = gScreensize;
    int bw = (W * 3) / 4, bh = 54;
    int bx = (W - bw) / 2, by = (gScreensize - bh) / 2;

    set_game_vp();
    /* Dark overlay over game area */
    SDL_SetRenderDrawBlendMode(gRen, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(gRen, 0, 0, 0, 160);
    SDL_Rect overlay = {0, 0, W, gScreensize};
    SDL_RenderFillRect(gRen, &overlay);
    SDL_SetRenderDrawBlendMode(gRen, SDL_BLENDMODE_NONE);

    /* Box background */
    SDL_SetRenderDrawColor(gRen, 40, 40, 80, 255);
    SDL_Rect box = {bx, by, bw, bh};
    SDL_RenderFillRect(gRen, &box);

    /* Box border */
    SDL_SetRenderDrawColor(gRen, 200, 200, 255, 255);
    SDL_RenderDrawRect(gRen, &box);

    /* Prompt and input */
    blit_str(bx + 8, by + 8, prompt, 255, 255, 200);
    char display[256];
    snprintf(display, sizeof(display), "> %s_", buf);
    blit_str(bx + 8, by + 28, display, 255, 255, 80);

    draw_menu_bar();
    SDL_RenderPresent(gRen);
    set_game_vp();
}

static int sdl2_get_text(const char *prompt, char *buf, int maxlen)
{
    buf[0] = '\0';
    int len = 0;

    SDL_StartTextInput();
    draw_input_box(prompt, buf);

    SDL_Event e;
    while (1) {
        if (!SDL_WaitEventTimeout(&e, 10000)) continue;
        if (e.type == SDL_QUIT) { SDL_StopTextInput(); Game_quit(); return 0; }
        if (e.type == SDL_KEYDOWN) {
            SDL_Keycode k = e.key.keysym.sym;
            if (k == SDLK_RETURN)  { SDL_StopTextInput(); return 1; }
            if (k == SDLK_ESCAPE)  { buf[0] = '\0'; SDL_StopTextInput(); return 0; }
            if (k == SDLK_BACKSPACE && len > 0) { buf[--len] = '\0'; }
        } else if (e.type == SDL_TEXTINPUT) {
            int tlen = (int)strlen(e.text.text);
            if (len + tlen < maxlen - 1) {
                strcat(buf, e.text.text);
                len += tlen;
            }
        } else {
            /* ignore other events (including timer) while input box is open */
            continue;
        }
        draw_input_box(prompt, buf);
    }
}

/* -------------------------------------------------------------------------
 * Dialog helpers
 * ---------------------------------------------------------------------- */

static void sdl2_msgbox(const char *title, const char *msg)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title, msg, gWin);
}

static int sdl2_yesno(const char *title, const char *msg)
{
    SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes"},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No"},
    };
    SDL_MessageBoxData mbd = {
        SDL_MESSAGEBOX_WARNING, gWin, title, msg, 2, buttons, NULL
    };
    int btn = 0;
    SDL_ShowMessageBox(&mbd, &btn);
    return btn;
}

/* -------------------------------------------------------------------------
 * Dialog system
 * ---------------------------------------------------------------------- */

static void sdl2_create_dialogs(Picture *logo, Picture *icon, Picture *about)
{
    /* Nothing to create: dialogs are generated on demand via SDL_ShowMessageBox */
    UNUSED(logo); UNUSED(icon); UNUSED(about);
    memset(gDialogText, 0, sizeof(gDialogText));
}

static void sdl2_update_dialog(int index, const char *str)
{
    if (index >= 0 && index <= DIALOG_MAX)
        strncpy(gDialogText[index], str, sizeof(gDialogText[0]) - 1);
}

/* Score overlay: shows level score, auto-dismisses after 2 seconds */
static void sdl2_score_popup(const char *msg)
{
    Uint32 start   = SDL_GetTicks();
    Uint32 wait_ms = 2000;
    SDL_Event e;

    for (;;) {
        Uint32 elapsed = SDL_GetTicks() - start;
        if (elapsed >= wait_ms) break;

        clear_vp();
        SDL_SetRenderDrawColor(gRen, 180, 180, 180, 255);
        SDL_RenderClear(gRen);

        SDL_Rect gameDst = {0, MENU_H, gDispSize, gDispSize};
        SDL_RenderCopy(gRen, gGameTex, NULL, &gameDst);

        SDL_SetRenderDrawBlendMode(gRen, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(gRen, 0, 0, 0, 150);
        SDL_RenderFillRect(gRen, &gameDst);
        SDL_SetRenderDrawBlendMode(gRen, SDL_BLENDMODE_NONE);

        int bw = 220, bh = 70;
        int bx = (gDispSize - bw) / 2;
        int by = (gDispSize - bh) / 2 + MENU_H;
        SDL_Rect box = {bx, by, bw, bh};
        SDL_SetRenderDrawColor(gRen, 40, 40, 80, 255);
        SDL_RenderFillRect(gRen, &box);
        SDL_SetRenderDrawColor(gRen, 200, 200, 255, 255);
        SDL_RenderDrawRect(gRen, &box);

        blit_str(bx + 8, by + 8, msg, 255, 255, 200);

        int secs = (int)((wait_ms - elapsed + 999) / 1000);
        char cstr[32];
        snprintf(cstr, sizeof(cstr), "Next level in %ds...", secs);
        blit_str(bx + 8, by + 52, cstr, 180, 180, 180);

        if (gCursorTex) {
            int scale = gDispSize / gScreensize;
            SDL_Rect cdst = {gMouseX - gCursorHotX * scale,
                             gMouseY - gCursorHotY * scale,
                             gCursorW * scale, gCursorH * scale};
            SDL_RenderCopy(gRen, gCursorTex, NULL, &cdst);
        }

        draw_menu_bar();
        SDL_RenderPresent(gRen);

        Uint32 remaining = wait_ms - elapsed;
        if (!SDL_WaitEventTimeout(&e, (remaining < 50) ? (int)remaining : 50))
            continue;

        if (e.type == SDL_QUIT) { Game_quit(); return; }
        if (e.type == SDL_MOUSEMOTION) {
            gMouseX = e.motion.x;
            gMouseY = e.motion.y;
        } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_KEYDOWN) {
            break;
        }
    }

    SDL_SetRenderTarget(gRen, gGameTex);
}

static void toggle_pause(void); /* forward declaration */

static void sdl2_popup_dialog(int index)
{
    /* Drain stale timer events accumulated while we are in the dialog */
    SDL_FlushEvent(gTimerEvent);

    switch (index) {

    case DIALOG_NEWGAME:
        if (sdl2_yesno("New Game", newgame_dialog_str))
            Game_start(1);
        break;

    case DIALOG_PAUSEGAME:
        toggle_pause();
        break;

    case DIALOG_QUITGAME:
        if (sdl2_yesno("Quit Game", quit_dialog_str))
            Game_quit();
        break;

    case DIALOG_WARPLEVEL: {
        char buf[32] = "";
        if (sdl2_get_text("Warp to level? (Enter number):", buf, sizeof(buf))) {
            int lev = atoi(buf);
            if (lev > 0) Game_warp_to_level(lev);
        }
        break;
    }

    case DIALOG_HIGHSCORE:
        sdl2_msgbox("High Scores", gDialogText[DIALOG_HIGHSCORE]);
        break;

    case DIALOG_STORY:
        sdl2_msgbox("Story of xBill", story_dialog_str);
        break;

    case DIALOG_RULES:
        sdl2_msgbox("Rules", rules_dialog_str);
        break;

    case DIALOG_ABOUT:
        sdl2_msgbox("About xBill",
            "XBill version 2.1\n\n"
            "Original game by Brian Wellington\n"
            "and Teemu Paukamainen\n\n"
            "ArcaOS SDL2 port");
        break;

    case DIALOG_SCORE:
        sdl2_score_popup(gDialogText[DIALOG_SCORE]);
        break;

    case DIALOG_ENDGAME:
        sdl2_msgbox("Game Over", endgame_dialog_str);
        break;

    case DIALOG_ENTERNAME: {
        char buf[64] = "";
        sdl2_get_text("High Score!  Enter your name:", buf, sizeof(buf));
        Game_add_high_score(buf);
        break;
    }

    default:
        break;
    }

    SDL_FlushEvent(gTimerEvent);

    /* DIALOG_SCORE runs its own 2-second event loop which consumes the
       game timer event before the main loop can see it.  gTimerActive is
       still 1 but the underlying SDL_AddTimer already fired (one-shot) and
       will never push another event, so the game loop would freeze.
       Inject a synthetic timer event so the main loop re-arms normally. */
    if (index == DIALOG_SCORE) {
        gTimerActive = 0;
        SDL_Event te;
        SDL_memset(&te, 0, sizeof(te));
        te.type = gTimerEvent;
        SDL_PushEvent(&te);
    }
}

/* -------------------------------------------------------------------------
 * Pause toggle
 * ---------------------------------------------------------------------- */

static void toggle_pause(void)
{
    if (!gPauseEnabled) return; /* no game running */
    if (gTimerActive) {
        UI_pause_game();
        gPaused = 1;
    } else {
        UI_resume_game();
        gPaused = 0;
    }
    sdl2_refresh_window();
}

/* -------------------------------------------------------------------------
 * Set pause button enable/disable
 * ---------------------------------------------------------------------- */

static void sdl2_set_pausebutton(int active)
{
    gPauseEnabled = active;
    if (!active) gPaused = 0; /* game ended — clear paused state */
}

/* -------------------------------------------------------------------------
 * Menu click handling (top bar)
 * ---------------------------------------------------------------------- */

static void handle_menu_click(int bar_mx)
{
    int mi = -1;
    for (int i = 0; i < 2; i++) {
        if (bar_mx >= gMenuBar[i].x && bar_mx < gMenuBar[i].x + gMenuBar[i].w) {
            mi = i;
            break;
        }
    }
    if (mi < 0) return;

    DropItem *items = (mi == 0) ? gGameItems : gInfoItems;
    int n = (mi == 0) ? GAME_ITEMS_N : INFO_ITEMS_N;
    int drop_x = gMenuBar[mi].x;
    int drop_y = MENU_H;
    int hover = -1;
    int action = -1;

    SDL_Event e;
    while (1) {
        /* Composite game texture first so the game shows behind the menu */
        clear_vp();
        SDL_SetRenderDrawColor(gRen, 180, 180, 180, 255);
        SDL_RenderClear(gRen);
        SDL_Rect gameDst = {0, MENU_H, gDispSize, gDispSize};
        SDL_RenderCopy(gRen, gGameTex, NULL, &gameDst);

        draw_menu_bar_open(mi);
        draw_dropdown(drop_x, drop_y, items, n, hover);
        SDL_RenderPresent(gRen);

        if (!SDL_WaitEventTimeout(&e, 5000)) continue;

        if (e.type == SDL_QUIT) { Game_quit(); return; }

        if (e.type == SDL_MOUSEMOTION) {
            hover = item_at(drop_x, drop_y, items, n,
                            e.motion.x, e.motion.y);
        } else if (e.type == SDL_MOUSEBUTTONDOWN) {
            int item = item_at(drop_x, drop_y, items, n,
                               e.button.x, e.button.y);
            if (item >= 0 && !items[item].is_sep) {
                int disabled = (items[item].dialog == DIALOG_PAUSEGAME
                                && !gPauseEnabled);
                if (!disabled)
                    action = items[item].dialog;
            }
            break;
        } else if (e.type == SDL_KEYDOWN &&
                   e.key.keysym.sym == SDLK_ESCAPE) {
            break;
        }
        /* ignore timer events while menu is open */
    }

    draw_menu_bar();
    set_game_vp();

    if (action >= 0)
        sdl2_popup_dialog(action);
}

/* -------------------------------------------------------------------------
 * Main loop
 * ---------------------------------------------------------------------- */

static void sdl2_main_loop(void)
{
    SDL_Event e;
    while (1) {
        if (!SDL_WaitEventTimeout(&e, 5000)) continue;

        if (e.type == SDL_QUIT) {
            Game_quit();

        } else if (e.type == gTimerEvent) {
            gTimerActive = 0;
            UI_restart_timer();   /* re-arm before update (mirrors X11 behaviour) */
            Game_update();

        } else if (e.type == SDL_MOUSEMOTION) {
            gMouseX = e.motion.x;
            gMouseY = e.motion.y;
            sdl2_refresh_window(); /* re-composite cursor without corrupting game tex */

        } else if (e.type == SDL_MOUSEBUTTONDOWN) {
            int mx = e.button.x, my = e.button.y;
            gMouseX = mx; gMouseY = my;
            if (my < MENU_H) {
                handle_menu_click(mx);
            } else if (mx < gDispSize) {
                int scale = gDispSize / gScreensize;
                Game_button_press(mx / scale, (my - MENU_H) / scale);
            }

        } else if (e.type == SDL_MOUSEBUTTONUP) {
            int mx = e.button.x, my = e.button.y;
            gMouseX = mx; gMouseY = my;
            if (my >= MENU_H && mx < gDispSize) {
                int scale = gDispSize / gScreensize;
                Game_button_release(mx / scale, (my - MENU_H) / scale);
            }

        } else if (e.type == SDL_WINDOWEVENT) {
            switch (e.window.event) {
            case SDL_WINDOWEVENT_LEAVE:
                UI_pause_game();
                break;
            case SDL_WINDOWEVENT_ENTER:
                UI_resume_game();
                break;
            case SDL_WINDOWEVENT_EXPOSED:
                UI_refresh();
                break;
            }

        } else if (e.type == SDL_KEYDOWN) {
            SDL_Keycode k   = e.key.keysym.sym;
            SDL_Keymod  mod = e.key.keysym.mod;
            if ((mod & KMOD_CTRL) && k == SDLK_x) {
                Game_quit();
            } else if ((mod & KMOD_CTRL) && k == SDLK_p) {
                toggle_pause();
            } else {
                switch (k) {
                case SDLK_ESCAPE: sdl2_popup_dialog(DIALOG_QUITGAME);   break;
                case SDLK_n:      sdl2_popup_dialog(DIALOG_NEWGAME);     break;
                case SDLK_w:      sdl2_popup_dialog(DIALOG_WARPLEVEL);   break;
                case SDLK_h:      sdl2_popup_dialog(DIALOG_HIGHSCORE);   break;
                default: break;
                }
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * Initialization and window creation
 * ---------------------------------------------------------------------- */

static void sdl2_initialize(int *argc, char **argv)
{
    UNUSED(argc); UNUSED(argv);

    freopen("xbill_err.log", "w", stderr);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
        fatal("SDL_Init failed: %s", SDL_GetError());

    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
        /* PNG not strictly needed; XPM loading is built-in */
    }

    gTimerEvent = SDL_RegisterEvents(1);
    if (gTimerEvent == (Uint32)-1)
        fatal("SDL_RegisterEvents failed");

    SDL_ShowCursor(SDL_DISABLE); /* software cursor draws it instead */
}

static void sdl2_make_main_window(int size)
{
    gScreensize = size;              /* logical game size (375) */
    gDispSize   = size * 2;          /* displayed size on screen (750) */
    gWinW       = gDispSize;         /* window matches game width exactly */
    int winH    = gDispSize + MENU_H;

    SDL_Rect disp = {0, 0, 1024, 768};
    SDL_GetDisplayBounds(0, &disp);
    int wx = disp.x + (disp.w - gWinW) / 2;
    int wy = disp.y + (disp.h - winH)  / 2;

    gWin = SDL_CreateWindow("xBill", wx, wy, gWinW, winH, 0);
    if (!gWin) fatal("SDL_CreateWindow failed: %s", SDL_GetError());

    gRen = SDL_CreateRenderer(gWin, -1,
                              SDL_RENDERER_ACCELERATED |
                              SDL_RENDERER_PRESENTVSYNC);
    if (!gRen) {
        gRen = SDL_CreateRenderer(gWin, -1, 0);
        if (!gRen) fatal("SDL_CreateRenderer failed: %s", SDL_GetError());
    }

    /* Game render texture (SCREENSIZE × SCREENSIZE, displayed 2× on screen) */
    gGameTex = SDL_CreateTexture(gRen, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_TARGET,
                                 gScreensize, gScreensize);
    if (!gGameTex) fatal("SDL_CreateTexture failed: %s", SDL_GetError());

    /* Start with render target on game texture */
    SDL_SetRenderTarget(gRen, gGameTex);

    /* Initialize menu bar layout (labels drawn later in draw_menu_bar) */
    gMenuBar[0].label = "Game"; gMenuBar[0].x = 0; gMenuBar[0].w = 0;
    gMenuBar[1].label = "Info"; gMenuBar[1].x = 0; gMenuBar[1].w = 0;
}

/* -------------------------------------------------------------------------
 * UI_methods table and setmethods
 * ---------------------------------------------------------------------- */

static UI_methods sdl2_methods = {
    sdl2_set_cursor,
    sdl2_load_cursor,
    sdl2_load_picture,
    sdl2_set_icon,
    sdl2_picture_width,
    sdl2_picture_height,
    sdl2_graphics_init,
    sdl2_clear_window,
    sdl2_refresh_window,
    sdl2_draw_image,
    sdl2_draw_line,
    sdl2_draw_string,
    sdl2_start_timer,
    sdl2_stop_timer,
    sdl2_timer_active,
    sdl2_popup_dialog,
    sdl2_main_loop,
    sdl2_initialize,
    sdl2_make_main_window,
    sdl2_create_dialogs,
    sdl2_set_pausebutton,
    sdl2_update_dialog,
};

void sdl2_setmethods(UI_methods **methodsp)
{
    *methodsp = &sdl2_methods;
}
