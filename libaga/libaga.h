#ifndef libaga_h
#define libaga_h

#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>

#include "agalog.h"

// Buffers
#define smBUFSZ						128
#ifndef BUFSZ
#define BUFSZ                       4096
#endif

#define myErrno ( errno ? -errno : -1 )

/***************************************
 * chomp
 *   string - the string to chomp
 *
 * removes trailing whitespace and comments from
 * a string
 ****************************************/
char *chomp( char *string );


/****************************************
 * open a generic TCP/UDP connection 
 ***************************************/
int openTCPConnection( char *host, short port, int timeout, int nonblock );
int openTCPConnection46( char *host, short port, int timeout, int nonblock, int pfam, char *bindip, int bindport );

int openUDPSocket46( char *ip, int port, int pfam, int nonblock );

/****************************************
 * open a generic TCP connection
 * as a stream
 ***************************************/
FILE *openTCPStream( char *host, short port, char *perm, int *sockfd, int contimeout, int nonblock );
FILE *openTCPStream46( char *host, short port, char *perm, int *sockfd, int contimeout, int nonblock, int pfam, char *bindip, int bindport );


/***************************************
 * test if a socket is connected
 ***************************************/
int socketIsConnected( int fd );

/***************************************
 * retrieve the local port from a socket
 **************************************/
int getLocalPort( int sock, unsigned short *port );

/***************************************
 * retrive the remote IP from a socket
 ***************************************/
char *getRemoteIP( int sock, char *dest, int len );


/****************************************************************************
 * return the size of the given file
 ***************************************************************************/
size_t fileSize( char *path );

/********************************
 * ss: a sockaddr_storage item
 * returns the sin_addr or sin6_addr from the ss formatted as a string
 * NOTE: this function is not thread safe as it uses a static buffer
 * to store the ip. use the _r variant if you need thread safety.
 */
char *formatIpFromSS( struct sockaddr_storage *ss );
char *formatIpFromSS_r( struct sockaddr_storage *ss, char *dst, int sz );

void hexLOG( void *_src, int sz );

/***************************************************************************
 * change a character array into a long
 **************************************************************************/
#ifndef NOAGACTOL
long ctol(unsigned char *a, short b);
#endif

uint64_t ntohll(uint64_t host_longlong);
uint64_t htonll(uint64_t host_longlong);

#endif

