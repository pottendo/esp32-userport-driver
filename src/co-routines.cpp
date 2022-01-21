#include <Arduino.h>

#include "parport-drv.h"
#include "co-routines.h"
#include "cred.h"

std::list<cr_base *> cr_base::coroutines;
char cr_base::aux_buf[MAX_AUX];

void setup_cr(void)
{
    new cr_mandel_t{"MAND"}; // never freed
    new cr_echo_t{"ECHO"};
    new cr_dump_t{"DUM1"};
    new cr_read_t{"READ"};
    new cr_dump2_t{"DUM2"};
#ifdef IRC_CRED
    new cr_irc_t{"IRC_"};
#endif
    new cr_arith_t{"ARIT"};
}

void loop_cr(void)
{
}

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
static uint8_t *canvas;

#include "mandelbrot.h"
bool cr_mandel_t::setup()
{
    canvas = new uint8_t[CSIZE];
    memset(canvas, 0x0, CSIZE);
    m = (void *)new mandel<double>{-1.5, -1.0, 0.5, 1.0, IMG_W / PIXELW, IMG_H, canvas};
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
    point_t ps{aux_buf[0] + aux_buf[1] * 256, aux_buf[2]};
    point_t pe{aux_buf[3] + aux_buf[4] * 256, aux_buf[5]};
    ps.x /= 2;
    pe.x /= 2;
    log_msg("mandel screen: {%d,%d} x {%d,%d}, canvas=%p\n", ps.x, ps.y, pe.x, pe.y, canvas);
    // canvas_dump(canvas);
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

int cmp(uint8_t *s, int len)
{
    // log_msg("compare requested with %d bytes buf=%p, canvas=%p.\n", len, s, canvas);
    return memcmp(canvas, s, len);
}

void canvas_setpx(canvas_t *canvas, coord_t x, coord_t y, color_t c)
{
    const uint lineb = IMG_W / 8 * 8; // line 8 bytes per 8x8 pixel
    const uint colb = 8;              // 8 bytes per 8x8 pixel

    uint h = x % (8 / PIXELW);
    uint shift = (8 / PIXELW - 1) - h;
    uint val = (c << (shift * PIXELW));

    // log_msg("x/y %d/%d offs %d/%d\n", x, y, (x / (8 / PIXELW)) * colb, (y / 8) * lineb  + (y % 8));
    uint cidx = (y / 8) * lineb + (y % 8) + (x / (8 / PIXELW)) * colb;
    if (cidx >= CSIZE)
    {
        // log_msg("Exceeding canvas!! %d, %d/%d\n", cidx, x, y);
        // delay (100 * 1000);
        return;
    }
    char t = canvas[cidx];
    t %= ~val;
    t |= val;
    canvas[cidx] = t;
    vTaskDelay(0 / portTICK_RATE_MS);
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
            // log_msg("idx: %032d ", offs + i);
            dump_bits(c[offs + i]);
        }
        log_msg("\n");
    }
}

// arithmetic functions

bool cr_arith_t::run(pp_drv *drv)
{
    char retbuf[6];
    int arg_len;
    ssize_t ret;
    ret = drv->read(aux_buf, 1);
    if (ret != 1) // minimum 1 byte for arith-function code
    {
        log_msg("coroutine arith, read error %d\n", ret);
        return false;
    }
    if (aux_buf[0] & 0x08)
    {
        log_msg("MFLPT args not yet implemented.\n");
        arg_len = 5;
        return false;
    }
    else
        arg_len = 6;

    int arg_bytes = (aux_buf[0] & 0x7) * arg_len; // number of args
    ret = drv->read(aux_buf + 1, arg_bytes);
    if (ret != arg_bytes) // minimum 1 byte for arith-function code
    {
        log_msg("coroutine arith, read error %d\n", ret);
        return false;
    }

    switch (aux_buf[0])
    {
    case uCFSIN:
    {
        arg1 = cbm62float(aux_buf + 1);
        double s = sin(arg1);
        float2cbm6(s, retbuf);
        break;
    }
    case uCFMUL:
    {
        arg1 = cbm62float(aux_buf + 1);
        arg2 = cbm62float(aux_buf + 1 + arg_len);
        double s = arg1 * arg2;
        float2cbm6(s, retbuf);
        break;
    }
    default:
        log_msg("arthmetic fn '%x' not implemented.\n");
        break;
    }
    ret = drv->write(retbuf, 6);
    if (ret != 6)
        log_msg("coroutine arith, write error %d\n", ret);
    return true;
}

// convert floating point variable to CBM FAC2 format
void cr_arith_t::float2cbm6(float f, char *c)
{
    uint8_t *p = (uint8_t *)&f;

    if (f == 0.0)
        c[0] = 0;
    else
        c[0] = (p[3] & 127) * 2 + (p[2] > 127) + 2;
    c[1] = 128 | (p[2] & 127);
    c[2] = p[1];
    c[3] = p[0];
    c[4] = 0;
    c[5] = p[3] & 128;
}

// convert floating point value to CBM memory format
void cr_arith_t::float2cbm5(float f, char *c)
{
    uint8_t *p = (uint8_t *)&f;

    if (f == 0.0)
        c[0] = 0;
    else
        c[0] = (p[3] & 127) * 2 + (p[2] > 127) + 2;
    c[1] = (p[3] & 128) + (p[2] & 127);
    c[2] = p[1];
    c[3] = p[0];
    c[4] = 0;
}

// convert CBM memory format to floating point variable
float cr_arith_t::cbm52float(char *c)
{
    float f;
    uint8_t *p = (uint8_t *)&f;
    uint8_t e;

    if (c[0] == 0)
        return 0.0;
    e = c[0] - 2;
    p[3] = (c[1] & 128) + (e >> 1);
    p[2] = (e & 1) * 128 + (c[1] & 127);
    p[1] = c[2];
    p[0] = c[3];

    return f;
}

float cr_arith_t::cbm62float(char *c)
{
    float f;
    uint8_t *p = (uint8_t *)&f;
    uint8_t e;

    if (c[0] == 0)
        return 0.0;
    e = c[0] - 2;
    p[3] = (c[5] & 128) + (e >> 1);
    p[2] = (e & 1) * 128 + (c[1] & 127);
    p[1] = c[2];
    p[0] = c[3];

    return f;
}

#if 0
// input C64 FP Format:
// s[0]: exp, s[1-4]: mantissa, s[5]/Bit7 sign (0 => +, 1 = -)
// not used anymore, formula from https://www.c64-wiki.com/wiki/Floating_point_arithmetic#Conversion_example
void cr_arith_t::parse_arg(const char *s)
{
    int sig = ((s[5] & 0b10000000) == 0) ? 1 : -1;
    double exp = s[0] - 128;
    arg1 = sig * (s[1] * pow2s[0] + s[2] * pow2s[1] + s[3] * pow2s[2] + s[4] * pow2s[3]) *
           pow(2, exp);
}
#endif