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
#include <sys/wait.h>
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
char			javaCmd[PATH_MAX]		= "java";
char			javaArgs[LINE_MAX]		= "";
char			saveStr[]				= "save-all";
char			shutdownStr[]			= "stop";
int				restartAt				= -1;
struct sigaction sigalarm;
child_t			minecraft;
char			logPath[PATH_MAX];
FILE			*logFD					= NULL;
int				logsToKeep				= 30;
int				lastLogDay				= 0;
int lastRestartDay = 0;
time_t			now;
struct tm		*nowTm;
char			nowString[32];
time_t			lastStartTime = 0;
SEC_USER_LIST	users;

int parseUserLevel( char *l )
{
	int retVal = SEC_MAX;

	if( strcasecmp( l, "normal" ) == 0 )
		retVal = SEC_NORM;
	else if( strcasecmp( l, "priv" ) == 0 )
		retVal = SEC_PRIV;
	else if( strcasecmp( l, "op" ) == 0 || strcasecmp( l, "admin" ) == 0 ) 
		retVal = SEC_ADMIN;

	return retVal;
}

SECURITY_LEVEL getUserSecLevel( char *user ) 
{
	SECURITY_LEVEL retVal = SEC_NORM;
	int i;

	for( i = 0; i < users.count; i++ ) {
		if( strcasecmp( users.user[i].username, user ) == 0 ) {
			retVal = users.user[i].sl; 
			break;
		}
	}

	return retVal;
}

int parseUser( char *v )
{
	int retVal = 0;
	char tuser[32];
	char tlevel[32];
	int level;
	int ix;

	if( v ) {
		memset(tuser, 0, 32);
		if( sscanf( v, "%s:%s", tuser, tlevel ) == 2 ) {
			level = parseUserLevel( tlevel );
			if( level < SEC_MAX ) {
				ix = users.count++;
				users.user = realloc(users.user, users.count * sizeof(SEC_USER) );
				memset( &users.user[ix], 0, sizeof(SEC_USER) );
				strcpy( users.user[ix].username, tuser );
				users.user[ix].sl = level;
			}
			else
				retVal = -1;
		}
		else 
			retVal = -1;
	}
	else
		retVal = -1;

	return retVal;
}

int getNow()
{
	int retVal = 0;
	char *p;

	time(&now);
	nowTm = localtime(&now);
	strcpy( nowString, ctime(&now) );
	for( p = &nowString[strlen(nowString)-1]; *p <= ' '; p-- ) *p = 0;

	return retVal;
}

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

		case SIGCHLD:
			while( waitpid(-1, NULL, WNOHANG) > 0 )
				continue;
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
	char tstr[LINE_MAX];

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
				else if( sscanf(line, "logsToKeep = %d", &logsToKeep) == 1 );
				else if( sscanf(line, "javaCmd = %s", javaCmd) == 1 );
				else if( sscanf(line, "user = %s", tstr ) == 1 ) {
					if( parseUser( tstr ) ) 
						printf("ERROR: invalid user format '%s' on line %d\n",
								tstr, lineNum);
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
		sigaction(SIGCHLD, &sigalarm, NULL );

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
	int result;

	getNow();
	result = now - lastStartTime;
	if( result < 60 ) { 
		printf("mcengine: waiting to restart minecraft for %d seconds\n",
				result);
		sleep(result);
	}

	memset( &minecraft, 0, sizeof(minecraft) );
	strcpy( minecraft.name, "minecraft" );
	strcpy( minecraft.path, javaCmd );

	for( argCount = 0; argCount < 64; argCount++ )
		ps[argCount] = NULL;

	argCount = 0;
	// setup default arguments
	ps[argCount++] = javaCmd;
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

	time(&lastStartTime);
	retVal = dppopenv( &minecraft, ps );
	if( retVal > 0 ) {
		if( logFD )
			fprintf(logFD, "mcengine: server launched with pid %d\n", retVal);
		retVal = 0;
	} 

	return retVal;
}

int handleSTInput() 
{
    int retVal = 0;
	char line[LINE_MAX];
	int sz;

	memset(line, 0, LINE_MAX);
	if( fgets(line, LINE_MAX, stdin) ) {
		sz = strlen(line);
		if( sz > 0 ) {
			if( dpwrite( &minecraft, line, sz ) != sz ) {
				if( logFD ) {
					fprintf(logFD, "mcengine: error writing to server: %s\n", strerror(errno));
				}
				errLOG( "ERROR: could not write to minecraft server: %s", strerror(errno));
				retVal = -1;
			}
		}
	}
	 else 
		retVal = -1;

	return retVal;
}

int parseMCInput( char *line )
{
	int retVal = 0;
	char *p, *lim, *s;

	p = line;
	lim = &line[strlen(line)];

	return retVal;
}

int handleMCInput() 
{
    int retVal = 0;
	char line[LINE_MAX];
	int sz;

	memset(line, 0, LINE_MAX);
	sz = dpread( &minecraft, line, LINE_MAX );
	if( sz > 0 ) {
		if( foreground ) {
			fputs( line, stdout );
			fflush(stdout);
		}
		if( logFD ) {
			fputs( line, logFD );
		}
		parseMCInput(line); // this does destrutive things to the line var and should
		                    // remain under any logging being done with the line
	} else if( sz < 0 )
		retVal = -1;
    
    return retVal;
}

int process() 
{
    int retVal = 0;
    fd_set readSet;
    struct timeval tv;
	int result;
    
    FD_ZERO( &readSet );
    FD_SET ( minecraft.fd_in, &readSet );
    FD_SET ( fileno(stdin), &readSet );
    tv.tv_sec = 60;
    tv.tv_usec = 0;
    
    result = select( FD_SETSIZE, &readSet, NULL, NULL, &tv );
    switch( result )
    {
        case -1:
            printf( "select error: %s\n", strerror(errno) );
			retVal = -1;
            break;
        case 0:
            break;
        default:
            if( FD_ISSET( minecraft.fd_in, &readSet ) )
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
	int nowHM = 0;
	int lcount = logsToKeep;
	char tpath[PATH_MAX];
	char tpath2[PATH_MAX];

	// get times
	getNow();

	if( restartAt >= 0 ) {
		nowHM = nowTm->tm_hour * 60 + nowTm->tm_min;
		if( nowHM < restartAt && nowTm->tm_yday != lastRestartDay ) {
			lastRestartDay = nowTm->tm_yday;
			dpwrite( &minecraft, saveStr, strlen(saveStr) );
			sleep(1);
			dpwrite( &minecraft, shutdownStr, strlen(saveStr) );
		}
	}

	// roll and reopen the logs
	if( nowTm->tm_yday != lastLogDay && logFD && logsToKeep > 1 ) {
		fclose(logFD);
		for( lcount = logsToKeep; lcount > 0; lcount-- ) {
			sprintf(tpath, "%s/%s-%d", basePath, logPath, lcount);
			if( lcount == 1 )
				sprintf(tpath2, "%s/%s", basePath, logPath);
			else
				sprintf(tpath2, "%s/%s-%d", basePath, logPath, lcount-1);
			if( access(tpath2, F_OK) == 0 ) {
				rename(tpath2, tpath);
			}
		}
		logFD = fopen(logPath, "w+");
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
	else if( initMinecraft() )
		retVal = -1;
	else {
		running = 1;
		getNow();
		lastRestartDay = nowTm->tm_yday;
		lastLogDay = nowTm->tm_yday;
		result = 0;
		while( running ) {
			if( result || kill_child(&minecraft, 0) ) {
				kill_child(&minecraft, SIGTERM);
				sleep(5); // wait 5 seconds between kill and restart
				result = initMinecraft();
			} else {
				result = process();
			}
			housekeeping();
		}
	}

	teardown();

	if( restart ) 
		doRestart( argc, argv );

	return retVal;
}

