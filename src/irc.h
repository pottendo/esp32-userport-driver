#ifndef __IRC_H__
#define __IRC_H__

#include "parport-drv.h"
#include "cred.h"

#ifdef IRC_CRED
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

    #define num_cols 4
    static int cols[num_cols]; 
    static int next_colidx;
    int get_nextcol(void) { return cols[(++next_colidx) % num_cols]; }
};

#endif /* IRC_CRED */
#endif