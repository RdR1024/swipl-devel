/*  $Id$

    Part of XPCE
    Designed and implemented by Anjo Anjewierden and Jan Wielemaker
    E-mail: jan@swi.psy.uva.nl

    Copyright (C) 1997 University of Amsterdam. All rights reserved.
*/

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SYNOPSIS:
	install [-v] [-c] [-m mode] from to
	install [-v] [-c] [-m mode] [-p] file file ... dir

Simple install replacement  to  be  used   for  XPCE  installation.  The
configure install.sh script is very slow,   while  all operating systems
appear to be using different versions of install. This should solve this
problem.

This installer is also used to install the system under windows to avoid
the limitiations and  portability  issues   around  the  Windows command
interpreter and copy command.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define bool int
#define shift (argc--, argv++)

#define CPBUFSIZE 8192

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifdef WIN32
#define IsDirSep(c) ((c) == '/' || (c) == '\\')
#define DIRSEP '\\'
#else
#define IsDirSep(c) ((c) == '/')
#define DIRSEP '/'
#endif

		 /*******************************
		 *	       SETTINGS		*
		 *******************************/

unsigned short mode;
bool	 set_mode;
bool	 copy;
char *	 program;
int	 verbose=0;
bool	 makedirs;
bool	 strippath = -1;		/* i.e. use basename() */
bool	 installdirs = FALSE;		/* -d */
bool	 newer_only = FALSE;

static int
get_mode(const char *s, unsigned short *m)
{ unsigned short mode = 0;

  while(*s >= '0' && *s <= '7')
  { mode = (mode << 3) + *s++ - '0';
  }
  
  if ( *s )
    return FALSE;
  
  *m = mode;
  return TRUE;
}


static int
isdir(const char *path)
{ struct stat buf;

  if ( stat(path, &buf) < 0 ||
       (buf.st_mode & S_IFMT) != S_IFDIR )
    return FALSE;

  return TRUE;
}


static void
get_file_mode(const char *name, int fd, mode_t *m)
{ struct stat buf;

#ifdef HAVE_FSTAT
  if ( fstat(fd, &buf) == 0 )
#else
  if ( stat(name, &buf) == 0 )
#endif
    *m = buf.st_mode;
  else
    *m = 0755;				/* default (hack) */
}


static int
last_modified(const char *name, time_t *t)
{ struct stat buf;

  if ( stat(name, &buf) == 0 )
  { *t = buf.st_mtime;
    return 0;
  } else
    return -1;
}


const char *
basename(const char *path)
{ const char *base;

  for(base=path; *path; path++)
  { if ( IsDirSep(*path) )
      base = path+1;
  }

  return base;
}


char *
dirname(const char *path)		/* returns malloced directory name */
{ const char *base = basename(path);
  char *rval;

  while(base > path && IsDirSep(base[-1]) )
    base--;
  
  if ( !(rval = malloc(base-path+1)) )
  { perror("malloc");
    exit(1);
  }
  memcpy(rval, path, base-path);
  rval[base-path] = '\0';

  return rval;
}


static int
makedir(char *path)
{ again:

  if ( mkdir(path, 0777) == 0 )
  { free(path);
    return TRUE;
  }

  if ( errno == ENOENT )
  { if ( makedir(dirname(path)) )
      goto again;
  }

  free(path);
  return FALSE;
}


static char *
str_store(const char *in)
{ char *rval;

  if ( (rval = malloc(strlen(in)+1)) )
  { strcpy(rval, in);
    return rval;
  }

  perror("malloc");
  exit(1);
}


static int
install_dir(const char *name)
{ if ( !isdir(name) )
    return makedir(str_store(name));

  return TRUE; 
}


static int
install_file(const char *from, const char *to)
{ int fdfrom, fdto;
  char buf[CPBUFSIZE];
  int rval;
  int n;
  mode_t m;

  if ( newer_only )
  { time_t to_time;

    if ( last_modified(to, &to_time) == 0 )
    { time_t from_time;

      if ( !last_modified(from, &from_time) == 0 )
      { perror(from);
	return FALSE;
      }

      if ( difftime(from_time, to_time) < 0.0 )
      { if ( verbose )
	  fprintf(stderr, "Skipped %s (not modified)\n", to);
	return TRUE;
      }
    }
  }

  if ( (fdfrom = open(from, O_RDONLY|O_BINARY)) < 0 )
  { perror(from);
    return FALSE;
  }

  if ( set_mode )
    m = mode;
  else
    get_file_mode(from, fdfrom, &m);

  unlink(to);
  again:
  if ( (fdto = open(to, O_WRONLY|O_BINARY|O_CREAT|O_TRUNC, m)) < 0 )
  { if ( errno == ENOENT && makedir(dirname(to)) )
      goto again;
    perror(to);
    close(fdfrom);
    return FALSE;
  }

  while( (n = read(fdfrom, buf, CPBUFSIZE)) > 0 )
  { char *b = buf;

    while(n > 0)
    { int n2;

      if ( (n2=write(fdto, b, n)) < 0 )
      { perror(to);
	rval = FALSE;
	goto out;
      }
      b += n2;
      n -= n2;
    }
  }
  if ( n < 0 )
  { perror(from);
    rval = FALSE;
  } else
    rval = TRUE;

out:
  close(fdfrom);
  close(fdto);

  return rval;
}


const char *
strip_path(const char *path, int strip)
{ if ( strip < 0 )
    return basename(path);
  else
  { const char *base = path;

    while(strip-- > 0)
    { while( IsDirSep(*base) )
	base++;
      while( *base && !IsDirSep(*base) )
	base++;
      while( IsDirSep(*base) )
	base++;
    }

    return base;
  }
}


static int
install_file_in_dir(const char *file, const char *dir)
{ char path[MAXPATHLEN];
  const char *base = strip_path(file, strippath);
  int rval;

  sprintf(path, "%s%c%s", dir, DIRSEP, base);

  if ( (rval = install_file(file, path)) && verbose )
    printf("%s\n", base);
    
  return rval;
}


void
usage()
{ fprintf(stderr, "  Usage: %s options file ... directory\n", program);
  fprintf(stderr, "     or: %s options from to\n", program);
  fprintf(stderr, "     or: %s [-C dir] -d dir ...\n", program);
  fprintf(stderr, "options: [-v[N]] [-n] [-c] [-p[N]] [-C dir] [-m mode]\n");
  exit(1);
}


int
main(int argc, char **argv)
{ char *out;
  int errors = 0;

  program = argv[0];
  shift;
  while(argc > 0 && argv[0][0] == '-')
  { char *opts = &argv[0][1];

    shift;
    for( ; *opts; opts++ )
    { switch( *opts )
      { case 'c':
	  copy = TRUE;
	  break;
	case 'p':
	  strippath = 0;
	  makedirs = TRUE;
	  if ( isdigit(opts[1]&0xff) )
	  { opts++;
	    strippath = *opts - '0';
	  }
	  break;
	case 'd':
	  installdirs = TRUE;
	  break;
	case 'n':
	  newer_only = TRUE;
	  break;
	case 'v':
	  verbose++;
	  if ( isdigit(opts[1]&0xff) )
	  { opts++;
	    verbose = *opts - '0';
	  }
	  break;
	case 'C':
	{ char *dir = argv[0];
	  
	  shift;
	  if ( chdir(dir) != 0 )
	  { perror(dir);
	    exit(1);
	  }
	  break;
	}
	case 'm':
	  if ( argc > 0 && get_mode(argv[0], &mode) )
	  { shift;
	    set_mode = TRUE;
	  }
	  break;
	default:
	  usage();
      }
    }
  }
  
  if ( argc == 0 )
    usage();
  out = argv[argc-1];

  if ( installdirs )
  { int i;

    for(i=0; i<argc; i++)
    { if ( !install_dir(argv[i]) )
	errors++;
    }
  } else
  { if ( isdir(out) )
    { int i;
  
      for(i=0; i<argc-1; i++)
      { if ( !install_file_in_dir(argv[i], out) )
	  errors++;
      }
    } else
    { if ( !install_file(argv[0], out) )
	errors++;
    }
  }

  return errors ? 1 : 0;
}
