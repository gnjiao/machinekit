/********************************************************************
* Description:  rtapi_io.c
*
*               This file, 'rtapi_io.c', implements the i/o- related
*               functions for realtime modules.  See rtapi.h for more
*               info.
********************************************************************/

#include "config.h"		// build configuration
#include "rtapi.h"		// these functions
#include "rtapi_common.h"	// RTAPI macros and decls

#ifdef MODULE
#include <linux/module.h>	// EXPORT_SYMBOL
#else
#include <sys/io.h>		/* inb(), outb(), inw(), outw() */
#endif

#ifdef HAVE_RTAPI_OUTB_HOOK
void rtapi_outb_hook(unsigned char byte, unsigned int port);
#endif

void rtapi_outb(unsigned char byte, unsigned int port) {
#ifdef HAVE_RTAPI_OUTB_HOOK
    rtapi_outb_hook(byte,port);
#endif
}


#ifdef HAVE_RTAPI_INB_HOOK
unsigned char rtapi_inb_hook(unsigned int port);
#endif

unsigned char rtapi_inb(unsigned int port) {
#ifdef HAVE_RTAPI_INB_HOOK
    return rtapi_inb_hook(port);
#else
    return 0;
#endif
}


#ifdef HAVE_RTAPI_OUTW_HOOK
void rtapi_outw_hook(unsigned short word, unsigned int port);
#endif

void rtapi_outw(unsigned short word, unsigned int port) {
#ifdef HAVE_RTAPI_OUTW_HOOK
    rtapi_outw_hook(word,port);
#endif
}


#ifdef HAVE_RTAPI_INW_HOOK
unsigned short rtapi_inw_hook(unsigned int port);
#endif

unsigned short rtapi_inw(unsigned int port)
{
#ifdef HAVE_RTAPI_INW_HOOK
    return rtapi_inw_hook(port);
#else
    return inw(port);
#endif
}
