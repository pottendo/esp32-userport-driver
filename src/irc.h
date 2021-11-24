#ifndef __IRC_H__
#define __IRC_H__

#include "parport-drv.h"

void setup_irc(void);
void loop_irc(pp_drv &drv);

#endif