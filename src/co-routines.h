#ifndef __CO_ROUTINES_H__
#define __CO_ROUTINES_H__

#include <list>
#include "logger.h"

void setup_cr(void);
void loop_cr(void);

class cr_base
{
protected:
    String name;
    void reg(void)
    {
        if (setup())
        {
            cr_base::coroutines.push_back(this);
            log_msg("Registered command '" + name + "'\n");
        }
        else
            log_msg("Setup of coroutine " + name + " failed.\n");
    }

public:
    cr_base(String n) : name(n)
    {}
    ~cr_base() = default;

    static std::list<cr_base *> coroutines;
    bool match(char *cmd, pp_drv *drv) { 
        if (strncmp(cmd, name.c_str(), 4) == 0)
            return run(drv);
        return false;
    };

    virtual bool setup(void) = 0;
    virtual bool run(pp_drv *drv) = 0;
    //virtual void loop(void) = 0;
};

class cr_mandel_t : public cr_base
{
    uint8_t *canvas;

public:
    cr_mandel_t(const char *n) : cr_base(String{n}) { reg(); }
    ~cr_mandel_t() = default;

    bool setup(void) override;
    bool run(pp_drv *drv) override;
    //void loop(void) override;
};

#endif