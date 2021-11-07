#include <Arduino.h>

#include "parport-drv.h"
#include "co-routines.h"

std::list<cr_base *> cr_base::coroutines;
char cr_base::aux_buf[MAX_AUX];

void setup_cr(void)
{
    new cr_mandel_t{"MAND"};  // never freed
    new cr_echo_t{"ECHO"};
    new cr_dump_t{"DUMP"};
    new cr_read_t{"READ"};
}

void loop_cr(void)
{}


// Couroutines


// Calculate & render mandelbrot set into an array layouted for C64 hires gfx

#define NO_THREADS 4
#define PAL_SIZE 4
#define MAX_ITER 128
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
uint8_t *canvas;

#include "mandelbrot.h"
bool cr_mandel_t::setup()
{
    canvas = new uint8_t[CSIZE];
    memset(canvas, 0x0, CSIZE);
    m = (void *) new mandel<double>{-1.5, -1.0, 0.5, 1.0, IMG_W / PIXELW, IMG_H, canvas};

    return true;
}

bool cr_mandel_t::run(pp_drv *drv)
{
    int ret;
    ret = drv->read(aux_buf, 6);
    if (ret != 6)
    {
        log_msg("mandel parm incomplete: %d\n", ret);
        return false;
    }
    point_t ps{aux_buf[0] + aux_buf[1]*256, aux_buf[2]};
    point_t pe{aux_buf[3] + aux_buf[4]*256, aux_buf[5]};
    ps.x /= 2;
    pe.x /= 2;    
    log_msg("mandel screen: {%d,%d} x {%d,%d}\n", ps.x, ps.y, pe.x, pe.y);
    //canvas_dump(canvas);
    memset(canvas, 0x0, CSIZE);
    ((mandel<double> *)m)->select_start(ps);
    ((mandel<double> *)m)->select_end(pe);
    if ((ret = drv->write((const char *)canvas, CSIZE)) != CSIZE)
    {
        log_msg("mandel failed to write %d\n", ret);
        return false;
    }
    return true;
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