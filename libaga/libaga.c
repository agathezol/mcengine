
#include "libaga.h"

char *chomp( char *string )
{
	char *p, *s;
	char *b, *lim;
	
	for( s = string, p = &s[strlen(s) - 1]; p >= s && *p <= ' '; p-- ) *p = 0; // trailing wht
	for( b = string, lim = &b[strlen(b)]; b < lim && *b <= ' '; b++ ); // leading wht
	
	if( strlen( b ) )
	{
		// kill comments
		for( s = b, p = &s[strlen(s)]; s < p ; s++ )
		{
			if( *s == '#' )
			{
				*s = 0;
				break;
			}
		}
	}
	
	return b;
}

int bindSockAddr(int sockfd, char *bindip, int bindport, int pfam )
{
	int retVal = 0;
	struct sockaddr_storage bind_addr;
	struct sockaddr_in *addr4 = (struct sockaddr_in *)&bind_addr;
	struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&bind_addr;
	int val = 0;
	int addr_len = 0;
	int family = AF_INET;

	if( pfam )
		family = pfam;

	memset(&bind_addr, 0, sizeof(bind_addr));
	if( bindip == NULL ) {
		if( family == AF_INET6 )  {
			memcpy(&addr6->sin6_addr, &in6addr_any, 8);
			addr6->sin6_port = htons(bindport);
		}
		else {
			addr4->sin_addr.s_addr = htonl(INADDR_ANY);
			addr4->sin_port = htons(bindport);
		}
	} else {
		if( family == AF_INET ) {
			addr4->sin_family = family;
			addr4->sin_port = htons(bindport);
			inet_pton(addr4->sin_family,bindip,&addr4->sin_addr);
		} else if ( family == AF_INET6 ) {
			addr6->sin6_family = family;
			addr6->sin6_port = htons(bindport);
			inet_pton(addr6->sin6_family,bindip,&addr6->sin6_addr);
		}
	}
	addr_len = sizeof(bind_addr);
	if( bind( sockfd, (const struct sockaddr *)&bind_addr, addr_len ) < 0 ) {
		errLOG( "ERROR: binding local socket: %s", strerror(errno));
		retVal = myErrno;
	} else {
		addr_len = sizeof(int);
		getsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, &addr_len);
		val = 1;
		setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));
	}
	return retVal;
}

/***********************************************************************
 * Open a TCP Connection
 ***********************************************************************/
int openTCPConnection46( char *host, short port, int timeout, int nonblock, int pfam, char *bindip, int bindport )
{
	int retVal = 0;
	struct addrinfo *addr, hints, *caddr;
	struct sockaddr_in *addr4;
	struct sockaddr_in6 *addr6;
	struct sockaddr_storage bind_addr;
	int addr_len = 0;
	char portString[6];
	int sockfd;
	int sockflags = 0;
	int family = 0;
	int val;

	if( pfam )
		family = pfam;
	else
		family = AF_INET;
	
	if( (sockfd = socket( family, SOCK_STREAM, 0 )) < 0 )
	{
		errLOG( "ERROR: creating socket for connection: %s\n", strerror(errno) );
	}
	else
	{
		memset( &hints, 0, sizeof(hints) );
		sprintf( portString, "%d", port );
		hints.ai_family = family;
		if( nonblock )
		{
			sockflags = fcntl( sockfd, F_GETFL, 0 );
			if( fcntl( sockfd, F_SETFL, (sockflags | O_NONBLOCK) ) )
			{
				errLOG( "ERROR: setting socket to nonblocking: %s", strerror(errno) );
			}
		}

		// if we are supposed to bind the socket to a local ip do that
		if( bindip ) {
			retVal = bindSockAddr( sockfd, bindip, bindport, family );
		}

		if( !retVal ) {
			if( getaddrinfo( host, portString, &hints, &addr ) )
			{
				errLOG( "error gettin address information for host %s: %s\n", host, strerror(errno));
			}
			else
			{
				for( caddr = addr; caddr; caddr = caddr->ai_next )
				{
					if( caddr->ai_protocol == PF_PACKET && caddr->ai_family == family )
					{
						if( family == AF_INET ) {
							addr_len = sizeof(struct sockaddr_in);
							addr4 = (struct sockaddr_in *)caddr->ai_addr;
							addr4->sin_port = htons(port);
						} else if( family == AF_INET6 ) {
							addr_len = sizeof(struct sockaddr_in6);
							addr6 = (struct sockaddr_in6 *)caddr->ai_addr;
							addr6->sin6_port = htons(port);
						}

						alarm(timeout);
						if( connect( sockfd, caddr->ai_addr, addr_len ) )
						{
							retVal = sockfd;
							if( errno != EINPROGRESS )
							{
								errLOG( "error connecting to host %s:%s : %s\n", host, 
										portString, strerror(errno));
							}
						}
						else
						{
							retVal = sockfd;
						}
						alarm(0);
						caddr->ai_next = NULL; // exit the for loop
					}
				}
				freeaddrinfo(addr);
			}
		}
	}
	
	return retVal;
}
int openTCPConnection( char *host, short port, int timeout, int nonblock ) 
{
	// use defaults to mimic old non-ipv6 compatible api
	return openTCPConnection46( host, port, timeout, nonblock, 0, NULL, 0 );
}

FILE *openTCPStream46( char *host, short port, char *perm, int *sockfd, int contimeout, int nonblock, int pfam, char *bindip, int bindport )
{
	FILE * retVal = NULL;
	int fd = 0;
	fd = openTCPConnection46( host, port, contimeout, nonblock, pfam, bindip, bindport );
	if( fd > 0 )
	{
		retVal = fdopen( fd, perm );
		if( !retVal )
		{
			errLOG( "error opening connected socket as a stream: %s", strerror(errno) );
		}
		else if( sockfd )
			*sockfd = fd;
	}
	else
		errLOG( "openTCPConnection returned an invalid socket" );
	
	return retVal;
}

FILE *openTCPStream( char *host, short port, char *perm, int *sockfd, int contimeout, int nonblock )
{
	return openTCPStream46( host, port, perm, sockfd, contimeout, nonblock, 0, NULL, 0);
}

int openUDPSocket46( char *ip, int port, int pfam, int nonblock)
{
	int retVal = 0;
	int sockfd;
	struct sockaddr_storage addr;
	struct sockaddr_in *addr4 = (struct sockaddr_in *)&addr;
	struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&addr;
	socklen_t addrlen;
	int family = pfam;
	int val;

	if( family == 0 )
		family = AF_INET;

	memset(&addr, 0, sizeof(addr));
	addr.ss_family = family;

	sockfd = socket( family, SOCK_DGRAM, IPPROTO_UDP );
	if( sockfd < 0 ) {
		errLOG( "ERROR: NET: openUDPSocket46(): could not create socket: %s", strerror(errno));
		retVal = myErrno;
	} else {
		if( nonblock ) {
			val = fcntl(sockfd, F_GETFL, 0);
			fcntl(sockfd, F_SETFL, (val | O_NONBLOCK));
		}
		retVal = bindSockAddr( sockfd, ip, port, pfam );
	}

	return sockfd;
}

/****************************************************************************
 * return the size of the given file
 ***************************************************************************/
size_t fileSize( char *path )
{
	size_t retVal = 0;
	struct stat st;
	
	memset( &st, 0, sizeof(st) );
	if( stat( path, &st ) )
	{
		errLOG( "error statting file %s: %s", path, strerror(errno));
		retVal = ( errno ? -errno : -1 );
	}
	else
	{
		retVal = st.st_size;
	}
	return retVal;
}

/***************************************
 * return true if a socket is connected
 ***************************************/
int socketIsConnected( int fd )
{
	int retVal = 0;
	fd_set writeset;
	struct timeval tv;
	
	memset( &tv, 0, sizeof(tv) );
	memset( &writeset, 0, sizeof(writeset) );
	FD_SET( fd, &writeset );
	
	if( select( fd+1, NULL, &writeset, NULL, &tv ) > 0 )
	{
		if( FD_ISSET( fd, &writeset ) )
			retVal = 1;
	}
	
	return retVal;
}

/***************************************************************************
 * change a character array into a long
 **************************************************************************/
#ifndef NOAGACTOL
long ctol(unsigned char *a, short b)
{
	unsigned char endian_constant[2] = {0,1};
	union { 
		unsigned char ba[4];
    long lv;} work;
	int     ix;
	
	work.lv=0l;
	if (*(short *)endian_constant == 1)
		for(ix = (4 - b);ix < 4;ix++) work.ba[ix] = *(a++);
	else
		while (b > 0)
		{
			b--;
			work.ba[b] = *(a++);
		}
	return work.lv;
}
#endif

char *formatIpFromSS( struct sockaddr_storage *ss )
{
	static char ip[32];
	struct sockaddr_in *a4 = (struct sockaddr_in *)ss;
	struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)ss;

	memset(ip, 0, 32);

	if( ss->ss_family == AF_INET ) {
		inet_ntop(AF_INET, &a4->sin_addr, ip, 31);
	} else if ( ss->ss_family == AF_INET6 ) {
		inet_ntop(AF_INET6, &a6->sin6_addr, ip, 31);
	} else {
		errLOG("ERROR: libaga: formatIpFromSS(): invalid ss_family=%d", ss->ss_family);
	}

	return ip;
}

char *formatIpFromSS_r( struct sockaddr_storage *ss, char *ip, int sz)
{
	struct sockaddr_in *a4 = (struct sockaddr_in *)ss;
	struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)ss;

	memset(ip, 0, sz);

	if( ss->ss_family == AF_INET ) {
		inet_ntop(AF_INET, &a4->sin_addr, ip, sz-1);
	} else if ( ss->ss_family == AF_INET6 ) {
		inet_ntop(AF_INET6, &a6->sin6_addr, ip, sz-1);
	} else {
		errLOG("ERROR: libaga: formatIpFromSS_r(): invalid ss_family=%d", ss->ss_family);
	}


	return ip;
}

int getLocalPort( int sock, unsigned short *port )
{
	struct sockaddr_in name;
	int namelen = sizeof(name);
	unsigned short retVal = 0;
	
	if( getsockname( sock, (struct sockaddr *)&name, &namelen ) )
	{
		errLOG( "error obtaining local socket information: %s", strerror(errno));
		retVal = -1;
	}
	else
	{
		*port = ntohs( name.sin_port );
	}
	
	return retVal;
}

char *getRemoteIP( int sock, char *dest, int len )
{
	struct sockaddr_in name;
	int namelen = sizeof(name);
	char *retVal = NULL;
	
	if( getpeername( sock, (struct sockaddr *)&name, &namelen ) )
	{
		errLOG( "error obtaining peer socket information: %s", strerror(errno));
		retVal = NULL;
	}
	else
	{
		snprintf( dest, len, "%s", inet_ntoa( name.sin_addr ) );
		retVal = dest;
	}
	
	return retVal;
}

void hexLOG(void *_src, int size)
{
	u_char  buf[65],
			bufa[65],
			*lim,
			*p,
			*p1;
	int count;
	unsigned char *src = _src;

	p = buf;
	p1 = bufa;
	lim = &src[size];
	count = 0;
	while (src < lim)
	{
		if (*src < 0x20 || *src > 0x7E)
			*(p1++) = '.';
		else
			*(p1++) = *src;
		*(p1++) = ' ';
		*p1 = 0;
		sprintf(p,"%02X ",*src);
		p += 2;
		if (count < 20)
			count++;
		else
		{
			dbLOG( "DEBUG: HEX: %s", bufa);
			dbLOG( "DEBUG: HEX: %s", buf);
			p = buf;
			p1 = bufa;
			count = 0;
		}
		src++;
	}
	if (count)
	{
		dbLOG("DEBUG: HEX: %s",bufa);
		dbLOG("DEBUG: HEX: %s",buf);
	}
}

uint64_t htonll(uint64_t host_longlong)
{
    int x = 1;
 
    /* little endian */
    if(*(char *)&x == 1)
        return ((((uint64_t)htonl(host_longlong)) << 32) + htonl(host_longlong >> 32));
 
    /* big endian */
    else
        return host_longlong;
}
 
uint64_t ntohll(uint64_t host_longlong)
{
    int x = 1;
 
    /* little endian */
    if(*(char *)&x == 1)
        return ((((uint64_t)ntohl(host_longlong)) << 32) + ntohl(host_longlong >> 32));
 
    /* big endian */
    else
        return host_longlong;
 
}

