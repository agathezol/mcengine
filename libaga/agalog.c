
#include "agalog.h"

/***********************************************************************
 * Log Routine - log when running
 ***********************************************************************/
int LOG( char *format, ... )
{
	va_list ap;
	
	if( running )
    {
		va_start( ap, format );
		vsyslog( syslog_pri, format, ap );
		va_end(ap);
    }
	
	return 0;
}

/***********************************************************************
 * Log Routine - log errors
 ***********************************************************************/
int errLOG( char *format, ... )
{
	va_list ap;
	
	va_start( ap, format );
	vsyslog( syslog_pri, format, ap );
	va_end(ap);
	
	return 0;
}

/***********************************************************************
 * Debug Log Routine - log when in debug
 ***********************************************************************/
int dbLOG( char *format, ... )
{
	va_list ap;
	
	if( debug )
    {
		va_start( ap, format );
		vsyslog( syslog_pri, format, ap );
		va_end(ap);
    }
	
	return 0;
}
