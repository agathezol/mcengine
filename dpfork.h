/*
 * Double pipe opens for read/write to a client process.
 */

#include <sys/types.h>
#include <errno.h>

#ifndef BUFSZ
#  define BUFSZ 65536
#endif

extern int debug;

typedef struct _child_t {
  pid_t           pid;
  int             fd_in;  /* into the child */
  int             fd_out; /* out from the child */
  char            name[64];
  char            path[BUFSZ];
} child_t;


/* Open a child */
pid_t dpopen( child_t *child );
pid_t dppopenv( child_t *child, char **args );

/* Write a buffer to a child */
int   dpwrite( child_t *child, void *buf, int size);

/* Read a buffer from a child */
int   dpread( child_t *child, void *buf, int size);

/* Send a signal to a child */
int   kill_child( child_t *child, int sig );
