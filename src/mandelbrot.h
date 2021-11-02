#ifndef __MANDELBROT_H__
#define __MANDELBROT_H__

#include <Arduino.h>
#include <stdlib.h>
#include <complex>
#include <iostream>

#include "misc.h"
#include "co-routines.h"
#include "logger.h"

template <typename myDOUBLE>
class mandel
{
    struct tparam_t
    {
        int tno, width, height;
        myDOUBLE xl, yl, xh, yh, incx, incy;
        int xoffset, yoffset;
        SemaphoreHandle_t go;
        SemaphoreHandle_t sem;
        mandel<myDOUBLE> *mo;

        tparam_t(int t, int w, int h, myDOUBLE x1, myDOUBLE y1, myDOUBLE x2, myDOUBLE y2, myDOUBLE ix, myDOUBLE iy,
                 int xo, int yo, SemaphoreHandle_t &se, mandel<myDOUBLE> *th)
            : tno(t), width(w), height(h), xl(x1), yl(y1), xh(x2), yh(y2), incx(ix), incy(iy), xoffset(xo), yoffset(yo), mo(th)
        {
            sem = se;
            go = xSemaphoreCreateBinary();

            //std::cout << *this << '\n';
        }
        ~tparam_t()
        {
            //log_msg("cleaning up params\n");
            vSemaphoreDelete(go);
        }
        friend std::ostream &operator<<(std::ostream &ostr, tparam_t &t)
        {
            ostr << "t=" << t.tno << ", w=" << t.width << ",h=" << t.height
                 << ",xl=" << t.xl << ",yl=" << t.yl << ",xh=" << t.xh << ",yh=" << t.yh
                 << ",incx=" << t.incx << ",incy=" << t.incy
                 << ",xo=" << t.xoffset << ",yo=" << t.yoffset << ", go: " << t.go;
            return ostr;
        }
    };

    tparam_t *tp[NO_THREADS];
    int max_iter = MAX_ITER;

    /* class local variables */
    canvas_t *canvas;
    uint16_t xres, yres;
    color_t col_pal[PAL_SIZE];
    coord_t mark_x1, mark_y1, mark_x2, mark_y2;
    myDOUBLE last_xr, last_yr, ssw, ssh, transx, transy;

    SemaphoreHandle_t master_sem, canvas_sem;
    TaskHandle_t worker_tasks[NO_THREADS];

    /* class private functinos */
    // abs() would calc sqr() as well, we don't need that for this fractal
    inline myDOUBLE abs2(std::complex<myDOUBLE> f)
    {
        myDOUBLE r = f.real(), i = f.imag();
        return r * r + i * i;
    }

    int mandel_calc_point(myDOUBLE x, myDOUBLE y)
    {
        const std::complex<myDOUBLE> point{x, y};
        // std::cout << "calc: " << point << '\n';
        std::complex<myDOUBLE> z = point;
        unsigned int nb_iter = 1;
        while (abs2(z) < 4 && nb_iter <= max_iter)
        {
            z = z * z + point;
            nb_iter++;
        }
        if (nb_iter < max_iter)
            return (nb_iter);
        else
            return 0;
    }

    void mandel_helper(myDOUBLE xl, myDOUBLE yl, myDOUBLE xh, myDOUBLE yh, myDOUBLE incx, myDOUBLE incy, int xo, int yo, int width, int height)
    {
        myDOUBLE x, y;
        int xk = xo;
        int yk = yo;
        if ((xl == xh) || (yl == yh))
        {
            log_msg("assertion failed: ");
            std::cout << "xl=" << xl << ",xh=" << xh << ", yl=" << yl << ",yh=" << yh << '\n';
            return;
        }
        x = xl;
        for (xk = 0; xk < width; xk++)
        {
            y = yl;
            for (yk = 0; yk < height; yk++)
            {
                int d = mandel_calc_point(x, y);
                P(canvas_sem);
                //lv_canvas_set_px(canvas, xk + xo, yk + yo, col_pal[d]);
                canvas_setpx(canvas, xk + xo, yk + yo, col_pal[d % PAL_SIZE]);
                V(canvas_sem);
                //mandel_buffer[x][y] = mandel_calc_point(x, y, TFT_WIDTH, TFT_HEIGHT);
                y += incy;
            }
            x += incx;
        }
    }

    static void mandel_wrapper(void *param)
    {
        tparam_t *p = static_cast<tparam_t *>(param);
        p->mo->mandel_wrapper_2(param);
    }

    int mandel_wrapper_2(void *param)
    {
        tparam_t *p = (tparam_t *)param;
        // Wait to be kicked off by mainthread
        //log_msg("thread %d waiting for kickoff\n", p->tno);
        while (true)
        {
            P(p->go);
            //log_msg("starting thread %d\n", p->tno);
            mandel_helper(p->xl, p->yl, p->xh, p->yh, p->incx, p->incy, p->xoffset, p->yoffset, p->width, p->height);
            V(p->sem); // report we've done our job
        }
        return 0;
    }

    void mandel_setup(const int thread_no, myDOUBLE sx, myDOUBLE sy, myDOUBLE tx, myDOUBLE ty)
    {
        int t = 0;
        last_xr = (tx - sx);
        last_yr = (ty - sy);
        ssw = last_xr / xres;
        ssh = last_yr / yres;
        transx = sx;
        transy = sy;
        myDOUBLE stepx = (xres / thread_no) * ssw;
        myDOUBLE stepy = (yres / thread_no) * ssh;
        TaskHandle_t th;
        if (thread_no > 4)
        {
            log_msg("too many threads... giving up.");
            return;
        }
        int w = (xres / thread_no);
        int h = (yres / thread_no);

        for (int tx = 0; tx < thread_no; tx++)
        {
            int xoffset = w * tx;
            for (int ty = 0; ty < thread_no; ty++)
            {
                int yoffset = h * ty;
                tp[t] = new tparam_t(t,
                                     w, h,
                                     tx * stepx + transx,
                                     ty * stepy + transy,
                                     tx * stepx + stepx + transx,
                                     ty * stepy + stepy + transy,
                                     ssw, ssh, xoffset, yoffset,
                                     master_sem, this);
                //th = SDL_CreateThread(mandel_wrapper, "T", tp[t]);
                xTaskCreate(mandel_wrapper, "mandel", 4096, tp[t], uxTaskPriorityGet(nullptr), &th);
                worker_tasks[t] = th;
                t++;
            }
        }
    }

    void go(void)
    {
        for (int i = 0; i < NO_THREADS; i++)
        {
            V(tp[i]->go);
            //usleep(250 * 1000);
        }
        for (int i = 0; i < NO_THREADS; i++)
            P(master_sem); // wait until all workers have finished
        //log_msg("all threads finished.\n");
    }

    void free_ressources(void)
    {
        //log_msg("mandel cleaning up...\n");
        for (int i = 0; i < NO_THREADS; i++)
        {
            vTaskDelete(worker_tasks[i]);
            delete tp[i];
        }
    }

public:
    mandel(myDOUBLE xl, myDOUBLE yl, myDOUBLE xh, myDOUBLE yh, uint16_t xr, uint16_t yr, canvas_t *c)
        : canvas(c), xres(xr), yres(yr)
    {
        //log_msg("mandelbrot set...\n");
        for (int i = 0; i < PAL_SIZE; i++)
            col_pal[i] = i;
        canvas_sem = xSemaphoreCreateMutex();
        master_sem = xSemaphoreCreateCounting(NO_THREADS, 0);
        mandel_setup(sqrt(NO_THREADS), xl, yl, xh, yh);
        go();
    }
    ~mandel()
    {
        free_ressources();
        vSemaphoreDelete(master_sem);
        vSemaphoreDelete(canvas_sem);
    };

    void select_start(point_t &p)
    {
        mark_x1 = p.x; // - ((LV_HOR_RES_MAX - IMG_W) / 2);
        mark_y1 = p.y; // - ((LV_VER_RES_MAX - IMG_H) / 2);
        if (mark_x1 < 0)
            mark_x1 = 0;
        if (mark_y1 < 0)
            mark_y1 = 0;
        log_msg("rect start: %dx%d\n", mark_x1, mark_y1);
    }

    void select_end(point_t &p)
    {
        mark_x2 = p.x; // - ((LV_HOR_RES_MAX - IMG_W) / 2);
        mark_y2 = p.y; // - ((LV_VER_RES_MAX - IMG_H) / 2);
        if (mark_x2 < 0)
            mark_x2 = 0;
        if (mark_y2 < 0)
            mark_y2 = 0;
        log_msg("rect coord: %dx%d - %dx%d\n", mark_x1, mark_y1, mark_x2, mark_y2);
        free_ressources();
        mandel_setup(sqrt(NO_THREADS),
                     mark_x1 * ssw + transx,
                     mark_y1 * ssh + transy,
                     mark_x2 * ssw + transx,
                     mark_y2 * ssh + transy);
        go();

        mark_x1 = -1;
        mark_x2 = mark_x1;
        mark_y2 = mark_y1;
    }

#if 0
    void select_update(point_t &p)
    {
        if (mark_x1 < 0)
            return;
        coord_t tx = mark_x2;
        coord_t ty = mark_y2;
        mark_x2 = p.x; // - ((LV_HOR_RES_MAX - IMG_W) / 2);
        mark_y2 = p.y; // - ((LV_VER_RES_MAX - IMG_H) / 2);
        if (mark_x2 < 0)
            mark_x2 = 0;
        if (mark_y2 < 0)
            mark_y2 = 0;
        if ((tx == mark_x2) && (ty == mark_y2))
            return;
        //log_msg("rect coord: %dx%d - %dx%d\n", mark_x1, mark_y1, mark_x2, mark_y2);
    }
#endif
};

#endif