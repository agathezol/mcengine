// system includes
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <syslog.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/file.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

// libaga has my logging system in it
#include <libaga.h>
#include <agalog.h>


#include "mcengine.h"

char			slash					= '/';
int				running 				= 0;
int				debug					= 0;
char			basePath[PATH_MAX]		= {0};
char			programName[PATH_MAX]	= {0};
char			configPath[PATH_MAX]	= {0};
char			hostname[PATH_MAX]		= "localhost";
char			lockfilePath[PATH_MAX]	= {0};
int				takeSnapshots			= 0;
int				hexDumpEnabled			= 0;
int				foreground				= 0;
int				alarmRaised				= 0;
int				termsig					= 0;
int				restart					= 0;
int				lockFD					= 0;
struct sigaction sigalarm;

void sig_handler( int sig )
{
	switch( sig )
	{
		case SIGALRM:
			alarmRaised = 1;
			break;

		case SIGPIPE:
			break;

		case SIGUSR1:
			if( debug )
			{
				dbLOG("exiting debug mode\n");
				debug = 0;
			}
			else
			{
				debug = 1;
				dbLOG("entered debug mode\n");
			}
			break;

		case SIGUSR2:
			if( hexDumpEnabled )
			{
				dbLOG("disabling hexdump\n");
				hexDumpEnabled = 0;
			}
			else
			{
				if( !debug )
					debug = 1;
				hexDumpEnabled = 1;
				dbLOG("enabling hexdump\n");
			}
			break;

		case SIGHUP:
			running = 0;
			restart = 1;
			termsig = sig;
			break;

		default:
			running = 0;
			termsig = sig;
			break;
	}

	return;
}

int lockProgram()
{
	int retVal = 0;

	lockFD = open( lockfilePath, O_CREAT | O_RDWR, S_IRUSR |S_IWUSR );
	if( lockFD < 0 )
	{
		errLOG( "ERROR: opening lock file %s: %s", lockfilePath, strerror(errno));
		retVal = myErrno;
	}
	else if( flock( lockFD, LOCK_EX | LOCK_NB ) )
	{
		errLOG( "ERROR: locking lock file %s: %s", lockfilePath, strerror(errno));
		retVal = myErrno;
	}

	return retVal;
}

int unlockProgram()
{
	if( lockFD > 0 )
	{
		flock(lockFD, LOCK_UN);
		close(lockFD);
		unlink(lockfilePath);
	}
	return 0;
}

/******************************************************************************
 * parse config
 *****************************************************************************/
int parseConfig( )
{
    int retVal = 0;
    char line[BUFSZ];
    FILE *fp = NULL;
    char *p;
    int tint;
	int lineNum = 0;

    if( (fp = fopen( configPath, "r" )) )
    {
        memset( line, 0, BUFSZ );
        while( !retVal && fgets( line, BUFSZ, fp ) )
        {
			lineNum++;

            /* normalize line */
            if( (p = strchr( line, '#' ) ) ) *p = 0;
            if( strlen(line) )
            {
                for( p = &line[strlen(line)]; p >= line && *p <= ' '; p-- )
                    *p = 0;
            }

            /* if we still have a line, parse it */
            if( strlen(line) )
            {
                if( sscanf( line, "debug = %d", &tint ) == 1 ) 
					debug = tint;
				else {
					LOG( "WARNING: invalid config entry at line %d: %s", lineNum, line );
					retVal = -1;
				}
			}
		}
	}

	return retVal;
}


int setup( int argc, char *argv[] )
{
	int retVal = 0;
	int i;
	char *lim;
	char tp[PATH_MAX] = {0};
	time_t t = time(NULL);
	struct tm *lt = localtime(&t);

	strncpy(basePath, argv[0], PATH_MAX);
	for( lim = &basePath[strlen(basePath)-1];
			(lim >= basePath) && (*lim != slash); lim-- ) *lim = 0;

	if( !strlen(basePath) )
		basePath[0] = '.';

	strncpy(programName, argv[0], PATH_MAX);
	for( lim = &programName[strlen(programName)-1];
			(lim >= programName) && (*lim != slash); lim-- );
	if( *lim == slash )
	{
		lim++;
		strcpy(tp, lim);
		memset(programName, 0, 256);
		strcpy(programName, tp);
	}

	for( i = 1; i < argc; i++ )
	{
		if( strcmp( argv[i], "-debug" ) == 0 )
		{
			debug = 1;
		}
		else if( strcmp( argv[i], "-snapshot" ) == 0 )
		{
			takeSnapshots = 1;
		}
		else if( strcmp(argv[i], "-hexdump" ) == 0 )
		{
			hexDumpEnabled = 1;
		}
		else if( strcmp( argv[i], "-f" ) == 0 )
		{
			foreground = 1;
		}
		else if( strcmp( argv[i], "-v") == 0 )
		{
			fprintf( stdout,
					"%s - mcengine Program\n"
					"(c) 2016-%d The Hyde Company\n"
					"   All Rights Reserved\n"
					"Build Time: %s\n"
					"Version: %s\n",
					programName,
					lt->tm_year + 1900,
					buildtime,
					MCENGINE_VERSION
				   );
			retVal = 1;
		}
		else
		{
			fprintf( stdout, "unknown option: %s\n", argv[i] );
		}
	}

	openlog( programName, LOG_NDELAY | LOG_NOWAIT | LOG_PID, LOG_LOCAL1 );
	dbLOG( "boj\n" );

	sprintf(lockfilePath, "/dev/shm/%s.lck", programName );
	if( lockProgram() )
	{
		errLOG( "%s is already running, aborting", programName );
		retVal = -1;
	}
	else
	{
		memset(&sigalarm, 0, sizeof(sigalarm));
		sigalarm.sa_handler = sig_handler;
		sigaction(SIGALRM, &sigalarm, NULL);
		sigaction(SIGPIPE, &sigalarm, NULL);
		sigaction(SIGTERM, &sigalarm, NULL);
		sigaction(SIGHUP, &sigalarm, NULL);			   /* restart this process */
		sigaction(SIGINT, &sigalarm, NULL);
		sigaction(SIGUSR1, &sigalarm, NULL);           /* change debug mode */
		sigaction(SIGUSR2, &sigalarm, NULL);		   /* change hexdump mode */
		sigaction(SIGTTIN, &sigalarm, NULL);
		sigaction(SIGTTOU, &sigalarm, NULL);
		sigaction(SIGIO, &sigalarm, NULL);

		// make setuid dumpable
		prctl(PR_SET_DUMPABLE, 1, 1, 1, 1);

		/* obtain our local ip address */
		gethostname(hostname, PATH_MAX);

		/* build our config file path */
		sprintf(configPath, "%s/%s.cfg", basePath, programName );

		if( !foreground )
		{
			daemon(1, 0);
		}
	}

	return retVal;
}

/**
 * doRestart();
 *   - create another copy of us using a potentially new binary
 */
void doRestart(int argc, char *argv[])
{
	char **argl;
	int i;

	argl = malloc( (argc+1) * sizeof( char *) );
	for( i = 0; i < argc; i++ )
		argl[i] = argv[i];
	argl[argc] = NULL;
	execv( argv[0], argl );
}


/**
 * Snapshot();
 *   - create a coredup if takeSnapshots is enabled
 */
void snapshot()
{
	static int last = 0;

	if( takeSnapshots ) {
		if( last < 10 ) {
			last++;
			if( !fork() ) {
				abort();
			}
		}
	}
}


/**
 * Teardown()
 *   - called right before restarting the progarm. any socket closes, db resets, etc
 *     should happen here.
 */
int teardown()
{
	int retVal = 0;

	return retVal;
}

int main( int argc, char *argv[] )
{
	int retVal = 0;

	if( setup( argc, argv ) )
		retVal = -1; 
	else if( parseConfig() )
		retVal = -1;
	else {
		running = 1;
		while( running ) {
			sleep(1); /* this is where we do things, but as we are a mcengine there is
			             nothing to do */
		}
	}

	teardown();

	if( restart ) 
		doRestart( argc, argv );

	return retVal;
}

