/*
 * Library routines for the dpfork package.
 */

#include "dpfork.h"
#include <sys/select.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

/*
 * dppopen: returns the pid of the child process.
 *
 * parent                               child
 *  \                                     /
 *   \                                   /
 *    \---->--| pipe1[1] |--->------>---/
 *     \                               /
 *      \-<-----<-| pipe2[0] |--<--<--/
 *
 * pipes in this diagram are relative to the parent
 * the childs are reversed.
 */
pid_t dppopen( child_t * child )
{
  int ret = 0;
  int pipe1[2];
  int pipe2[2];
  int i;
  char *argv[3];

  if( pipe( pipe1 ) )
    {
      ret = -errno;
    }
  else if( pipe( pipe2 ) )
    {
      ret = -errno;
    }
  else
    {
      child->pid = fork(  );
      if( child->pid == 0 )
        {                       /* in child */
          close( pipe1[0] );
          close( pipe2[1] );
          dup2( pipe1[1], 1 );
          dup2( pipe2[0], 0 );
          for( i = 3; i < 1024; i++ )
            close( i );
          argv[0] = child->path;
          if( debug )
            {
              argv[1] = "-debug";
              argv[2] = 0;
            }
          else
            argv[1] = 0;
          if( execv( child->path, argv ) )
            {
              exit( errno );    /* if execv fails we return a good
                                   status to wait pid */
            }
          exit( 0 );            /* we should never get here, but just in case */
        }
      else if( child->pid > 0 )
        {
          close( pipe1[1] );
          close( pipe2[0] );
          child->fd_in = pipe1[0];
          child->fd_out = pipe2[1];
          ret = child->pid;
        }
    }

  return ret;
}

pid_t dppopenv( child_t *child, char **args ) 
{
    int ret = 0;
    int pipe1[2];
    int pipe2[2];
    int i;
    
    if( pipe( pipe1 ) )
    {
        ret = -errno;
    }
    else if( pipe( pipe2 ) )
    {
        ret = -errno;
    }
    else
    {
        child->pid = fork(  );
        if( child->pid == 0 )
        {                       /* in child */
            close( pipe1[0] );
            close( pipe2[1] );
            dup2( pipe1[1], 1 );
            dup2( pipe2[0], 0 );
            for( i = 3; i < 1024; i++ )
                close( i );
            args[0] = child->path;
            
            if( execvp( child->path, args ) )
            {
                exit( errno );    /* if execv fails we return a good
                                   status to wait pid */
            }
            exit( 0 );            /* we should never get here, but just in case */
        }
        else if( child->pid > 0 )
        {
            close( pipe1[1] );
            close( pipe2[0] );
            child->fd_in = pipe1[0];
            child->fd_out = pipe2[1];
            ret = child->pid;
        }
    }
    
    return ret;
}


/*
 * dpwrite
 * - write to a child_t
 */
int dpwrite( child_t * child, void *buff, int sz )
{
  int ret = 0;

  ret = write( child->fd_out, buff, sz );

  return ret;
}

/*
 * dpread
 * - read from a child_t
 */
int dpread( child_t * child, void *buff, int sz )
{
  int ret = 0;
  fd_set readset;
  struct timeval tv;

  memset(&tv, 0, sizeof(tv));
  FD_ZERO(&readset);
  FD_SET(child->fd_in, &readset);
  tv.tv_sec = 5;
  tv.tv_usec = 0;
  switch( select(child->fd_in + 1, &readset, NULL, NULL, &tv) )
    {
    case 0:
    case -1:
      ret = -errno;
      break;
    default:
      memset( buff, 0, sz );
      ret = read( child->fd_in, buff, sz );
      break;
    }

  return ret;
}

/* 
 * terminate a child
 */
int kill_child( child_t * child, int sig )
{
  int ret = 0;

  if( child->pid > 0 )
    {
      if( kill( child->pid, sig ) )
        {
          ret = -errno;
        }
      else
        {
          if( child->fd_out )
            close( child->fd_out );
          if( child->fd_in )
            close( child->fd_in );
          child->pid = 0;
        }
    }

  return ret;
}
