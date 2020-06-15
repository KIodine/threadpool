#ifndef STHP_DBG_H
#define STHP_DBG_H

#include <stdio.h>

#define NL "\n"

#ifndef NDEBUG
    #define MSG_PREFIX "[ThreadPool] "
    #define debug_printf(fmt, ...) printf(MSG_PREFIX fmt, ##__VA_ARGS__)
#else
    #define MSG_PREFIX
    #define debug_printf(ignore, ...) ((void)0)
#endif


#endif /* STHP_DBG_H */