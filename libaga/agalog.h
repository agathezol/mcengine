/***********************************************************************
 * AGASupport Logging Library
 * (c) 2008 AGASupport (Franklin Marmon)
 *    All Rights Reserved
 ***********************************************************************
 *
 * requires a global variables debug and running be defined in the program
 *
 * LOGLEVEL and LOGFACILITY can be compile defined in order to override
 * logging areas at build time
 *
 ***********************************************************************
 *
 * 20080822 - frm
 *   created.
 *
 ***********************************************************************/

#ifndef agalog_h
#define agalog_h

#include <string.h>
#include <syslog.h>
#include <stdarg.h>

#ifndef LOGFACILITY
# define LOGFACILITY LOG_LOCAL0
#endif

#ifndef LOGLEVEL
# define LOGLEVEL LOG_WARNING
#endif

#define  syslog_pri                  LOGFACILITY | LOGLEVEL

extern int running;
extern int debug;

int LOG( char *format, ... );
int errLOG( char *format, ... );
int dbLOG( char *format, ... );

#endif
