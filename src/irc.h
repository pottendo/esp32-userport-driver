#ifndef __IRC_H__
#define __IRC_H__

#include "parport-drv.h"

//#define TEST_IRC

class irc_t
{
#ifdef TEST_IRC
    TaskHandle_t th;
#endif
    void annotate4irc(String &s);
public:
    irc_t();
    ~irc_t();
    bool loop(pp_drv &drv);
    bool get_msg(String &s);
};

#endif