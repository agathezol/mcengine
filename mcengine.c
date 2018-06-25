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
#include "dpfork.h"

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
int				foreground				= 1;
int				alarmRaised				= 0;
int				termsig					= 0;
int				restart					= 0;
int				lockFD					= 0;
int				minMem					= 1024;
int 			maxMem					= 2048;
char			mcJar[PATH_MAX]			= "minecraft_server.jar";
char			javaArgs[LINE_MAX]		= "";
char			saveStr[]				= "save-all";
char			shutdownStr[]			= "stop";
int				restartAt				= -1;
struct sigaction sigalarm;
child_t			minecraft;
char			logPath[PATH_MAX];
FILE			*logFD					= NULL;

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
    char *p, *d;
    int tint, tint2;
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
				else if( sscanf( line, "minMem = %d", &minMem ) == 1);
				else if( sscanf( line, "maxMem = %d", &maxMem ) == 1);
				else if( sscanf( line, "serverJar = %s", mcJar ) == 1);
				else if( strncmp( line, "javaArgs = \"", 12 ) == 0 ) {
					memset(javaArgs, 0, LINE_MAX);
					for( p = &line[12], d = javaArgs; *p ; ) {
						if( *p == '"' && *(p-1) != '\\' ) {
							*p = 0;
						} else {
							*(d++) = *(p++);
						}
					}
				}
				else if( sscanf( line, "restartAt = %d:%d", &tint, &tint2 ) == 2 ) {
					restartAt = tint * 60 + tint2;
				}
				else if( sscanf(line, "logfile = %s", logPath ) == 1 ) {
					logFD = fopen(logPath, "w+");
					if( logFD == NULL ) {
						printf("ERROR: could not open logfile '%s': %s\n", logPath,
								strerror(errno));
						retVal = -1;
					}
				}
				else {
					printf( "WARNING: invalid config entry at line %d: %s\n", lineNum, line );
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
					"(c) 2016-%d Stirge Gaming\n"
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

	if( logFD ) {
		fclose(logFD);
		logFD = NULL;
	}

	return retVal;
}

int initMinecraft()
{
	int retVal = 0;
	char *ps[64];
	char *p, *s;
	int argCount = 0;

	memset( &minecraft, 0, sizeof(minecraft) );
	strcpy( minecraft.name, "minecraft" );
	strcpy( minecraft.path, "java" );

	for( argCount = 0; argCount < 64; argCount++ )
		ps[argCount] = NULL;

	argCount = 0;
	// setup default arguments
	ps[argCount++] = "java";
	ps[argCount++] = "-jar";
	ps[argCount++] = mcJar;
	ps[argCount++] = "nogui";
	// parse additional arguments
	for( p = javaArgs, s = p; *p && argCount < 62; ) {
		p++;
		if( *p == ' ' || *p == 0 ) {
			*p = 0;
			while( *p == ' ' ) p++;
			ps[argCount++] = s;
			s = p;
		}
	}
	ps[argCount] = NULL;

	retVal = (dppopenv( &minecraft, ps ) < 0);

	return retVal;
}

int handleSTInput() 
{
    int retVal = 0;
	char line[LINE_MAX];
	int sz;

	if( fgets(line, LINE_MAX, stdin) ) {
		sz = strlen(line);
		if( sz > 0 ) {
			retVal = (dpwrite( &minecraft, line, sz ) != sz);
		}
	}
	 else 
		retVal = -1;

	return retVal;
}

int handleMCInput() 
{
    int retVal = 0;
	char line[LINE_MAX];
	int sz;

	sz = dpread( &minecraft, line, LINE_MAX );
	if( sz > 0 ) {
		fputs( line, stdout );
		fflush(stdout);
		if( logFD ) {
			fputs( line, logFD );
		}
	} else if( sz < 0 )
		retVal = -1;
    
    return retVal;
}

int process() 
{
    int retVal = 0;
    fd_set readSet;
    struct timeval tv;
    
    FD_ZERO( &readSet );
    FD_SET ( minecraft.fd_out, &readSet );
    FD_SET ( fileno(stdin), &readSet );
    tv.tv_sec = 60;
    tv.tv_usec = 0;
    
    retVal = select( FD_SETSIZE, &readSet, NULL, NULL, &tv );
    switch( retVal )
    {
        case -1:
            printf( "select error: %s\n", strerror(errno) );
            break;
        case 0:
            break;
        default:
            if( FD_ISSET( minecraft.fd_out, &readSet ) )
                retVal = handleMCInput();
			if( !retVal && FD_ISSET( fileno(stdin), &readSet ) )
				retVal = handleSTInput();
            break;
    }
    
    return retVal;
}

int housekeeping()
{
	int retVal = 0;
	time_t now;
	struct tm *lt;
	int nowHM = 0;
	static int lastRestartDay = 0;

	if( restartAt >= 0 ) {
		time(&now);
		lt = localtime(&now);
		nowHM = lt->tm_hour * 60 + lt->tm_min;
		if( nowHM < restartAt && lt->tm_yday != lastRestartDay ) {
			lastRestartDay = lt->tm_yday;
			dpwrite( &minecraft, saveStr, strlen(saveStr) );
			sleep(1);
			dpwrite( &minecraft, shutdownStr, strlen(saveStr) );
		}
	}

	return retVal;
}

int main( int argc, char *argv[] )
{
	int retVal = 0;
	int result;

	if( setup( argc, argv ) )
		retVal = -1; 
	else if( parseConfig() )
		retVal = -1;
	else {
		running = 1;
		while( running ) {
			if( initMinecraft() == 0 ) {
				do { 
					result = process();
					housekeeping();
				} while ( result == 0 );
			} else {
				printf("error initing minecraft server\n");
				sleep(5);
			}
		}
	}

	teardown();

	if( restart ) 
		doRestart( argc, argv );

	return retVal;
}

