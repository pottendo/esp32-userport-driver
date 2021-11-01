#include <Arduino.h>

#include "parport-drv.h"
#include "co-routines.h"

// mandelbrot set
#define NO_THREADS 4
#define PAL_SIZE 4
#define MAX_ITER 64
#define IMG_W 320 // 320
#define IMG_H 200 // 200
#define CSIZE (IMG_W * IMG_H) / 8
#define PIXELW 2 // 2

typedef uint8_t canvas_t;
typedef int coord_t;
typedef uint8_t color_t;
typedef struct
{
    coord_t x;
    coord_t y;
} point_t;

void canvas_setpx(canvas_t *canvas, coord_t x, coord_t y, color_t c);
static void canvas_dump(canvas_t *c);

#include "mandelbrot.h"

mandel<float> *mo;

uint8_t *canvas;

void init_mandel(void)
{
    canvas = new uint8_t[CSIZE];
    memset(canvas, 0x0, CSIZE);
}

void cr_mandel(pp_drv *drv)
{
    mo = new mandel<float>(-1.5, -1.0, 0.5, 1.0, IMG_W / PIXELW, IMG_H, canvas);
    //canvas_dump(canvas);
    int ret;
    if ((ret = drv->write((const char *)canvas, CSIZE)) != CSIZE)
        log_msg("mandel failed to write %d\n", ret);
    delete mo;
}

void canvas_setpx(canvas_t *canvas, coord_t x, coord_t y, color_t c)
{
    const uint lineb = IMG_W / 8 * 8; // line 8 bytes per 8x8 pixel
    const uint colb = 8;              // 8 bytes per 8x8 pixel

    uint h = x % (8 / PIXELW);
    uint shift = (8 / PIXELW - 1) - h;
    uint val = (c << (shift * PIXELW));
    
    //log_msg("x/y %d/%d offs %d/%d\n", x, y, (x / (8 / PIXELW)) * colb, (y / 8) * lineb  + (y % 8));
    uint cidx = (y / 8) * lineb  + (y % 8) + (x / (8 / PIXELW)) * colb;
    if (cidx >= CSIZE)
    {
        //log_msg("Exceeding canvas!! %d, %d/%d\n", cidx, x, y);
        //delay (100 * 1000);
        return;
    }
    char t = canvas[cidx];
    t %= ~val;
    t |= val;
    canvas[cidx] = t;
}

static void dump_bits(uint8_t c)
{
    for (int i = 7; i >= 0; i--)
    {
        log_msg("%c", (c & (1 << i)) ? '*' : '.');
    }
}

static void canvas_dump(canvas_t *c)
{
    for (int y = 0; y < IMG_H; y++)
    {
        log_msg("%02d: ", y);
        int offs = (y / 8) * IMG_W + (y % 8);
        for (auto i = 0; i < IMG_W; i += 8)
        {
            //log_msg("idx: %032d ", offs + i);
            dump_bits(c[offs + i]);
        }
        log_msg("\n");
    }
}