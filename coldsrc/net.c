/* net.c: Network routines. */
/* This stuff is not POSIX, and thus must be ported separately to each
 * network interface.  This code is for a BSD interface. */

/* RFC references: inverse name resolution--1293, 903
 * 1035 - domain name system */

#define _BSD 44 /* For RS6000s. */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <grp.h>

#include "net.h"
#include "io.h"
#include "log.h"
#include "util.h"
#include "ident.h"

extern int socket(), bind(), listen(), getdtablesize(), select(), accept();
extern int connect(), getpeername(), getsockopt(), setsockopt();
extern void bzero();

static long translate_connect_error(int error);

static struct sockaddr_in sockin;		/* An internet address. */
static int addr_size = sizeof(sockin);	/* Size of sockin. */

long server_failure_reason;

int get_server_socket(int port)
{
    int fd, one;

    /* Create a socket. */
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      write_log("get_server_socket - socket failed: %s.", strerror(errno));
      server_failure_reason = socket_id;
      return -1;
    }

    /* Set SO_REUSEADDR option to avoid restart problems. */
    one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof(int));

    /* Bind the socket to port. */
    memset(&sockin, 0, sizeof(sockin));
    sockin.sin_family = AF_INET;
    sockin.sin_port = htons((unsigned short) port);
    if (bind(fd, (struct sockaddr *) &sockin, sizeof(sockin)) < 0) {
      close(fd);
      server_failure_reason = bind_id;
      return -1;
    }

    /* Start listening on port.  This shouldn't return an error under any
     * circumstances. */
    listen(fd, 8);

    return fd;
}

/* Wait for I/O events.  sec is the number of seconds we can wait before
 * returning, or -1 if we can wait forever.  Returns nonzero if an I/O event
 * happened. */
int io_event_wait(long sec, Connection *connections, Server *servers,
		  Pending *pendings)
{
    struct timeval tv, *tvp;
    Connection *conn;
    Server *serv;
    Pending *pend;
    fd_set read_fds, write_fds;
    int flags, nfds, count, result, error, dummy = sizeof(int);

    /* Set time structure according to sec. */
    if (sec == -1) {
	tvp = NULL;
        /* this is a rather odd thing to happen for me */
        write_log("select:  forever wait");
    } else {
	tv.tv_sec = sec;
	tv.tv_usec = 0;
	tvp = &tv;
    }

    /* Begin with blank file descriptor masks and an nfds of 0. */
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    nfds = 0;

    /* Listen for new data on connections, and also check for ability to write
     * to them if we have data to write. */
    for (conn = connections; conn; conn = conn->next) {
      int fd = conn->fd;
      if (!conn->flags.dead)
	FD_SET(fd, &read_fds);
      if (conn->write_buf && conn->write_buf->len) {
	FD_SET(fd, &write_fds);
      }

      if (fd >= nfds)
	nfds = fd + 1;
    }

    /* Listen for connections on the server sockets. */
    for (serv = servers; serv; serv = serv->next) {
	FD_SET(serv->server_socket, &read_fds);
	if (serv->server_socket >= nfds)
	    nfds = serv->server_socket + 1;
    }

    /* Check pending connections for ability to write. */
    for (pend = pendings; pend; pend = pend->next) {
	if (pend->error != NOT_AN_IDENT) {
	    /* The connect has already failed; just set the finished bit. */
	    pend->finished = 1;
	} else {
	    FD_SET(pend->fd, &write_fds);
	    if (pend->fd >= nfds)
		nfds = pend->fd + 1;
	}
    }

    /* Call select(). */
    count = rand();
    count = select(nfds, &read_fds, &write_fds, NULL, tvp);

    /* Lose horribly if select() fails on anything but an interrupted system
     * call.  On EINTR, we'll return 0. */
    if (count == -1 && errno != EINTR)
	panic("select() failed");

    /* Stop and return zero if no I/O events occurred. */
    if (count <= 0)
	return 0;

    /* Check if any connections are readable or writable. */
    for (conn = connections; conn; conn = conn->next) {
	if (FD_ISSET(conn->fd, &read_fds))
	    conn->flags.readable = 1;
	if (FD_ISSET(conn->fd, &write_fds))
	    conn->flags.writable = 1;
    }

    /* Check if any server sockets have new connections. */
    for (serv = servers; serv; serv = serv->next) {
	if (FD_ISSET(serv->server_socket, &read_fds)) {
	    serv->client_socket = accept(serv->server_socket,
					 (struct sockaddr *) &sockin, &addr_size);
	    if (serv->client_socket < 0)
		continue;

	    /* Get address and local port of client. */
	    strcpy(serv->client_addr, inet_ntoa(sockin.sin_addr));
	    serv->client_port = ntohs(sockin.sin_port);

	    /* Set the CLOEXEC flag on socket so that it will be closed for a
	     * run_script() operation. */
#ifdef FD_CLOEXEC
	    flags = fcntl(serv->client_socket, F_GETFD);
	    flags |= FD_CLOEXEC;
	    fcntl(serv->client_socket, F_SETFD, flags);
#endif
	}
    }

    /* Check if any pending connections have succeeded or failed. */
    for (pend = pendings; pend; pend = pend->next) {
	if (FD_ISSET(pend->fd, &write_fds)) {
	    result = getpeername(pend->fd, (struct sockaddr *) &sockin,
				 &addr_size);
	    if (result == 0) {
		pend->error = NOT_AN_IDENT;
	    } else {
		error = EOPNOTSUPP;  /* in case this is a unix domain */
		getsockopt(pend->fd, SOL_SOCKET, SO_ERROR, (char *) &error,
			   &dummy);
		pend->error = translate_connect_error(error);
	    }
	    pend->finished = 1;
	}
    }

    /* Return nonzero, indicating that at least one I/O event occurred. */
    return 1;
}

long non_blocking_iconnect(char *addr, int port, int *socket_return)
{
  int fd, result, flags;
  struct in_addr inaddr;
  struct sockaddr_in saddr;

  *socket_return = -1;

  /* Convert address to struct in_addr. */
  inaddr.s_addr = inet_addr(addr);
  if (inaddr.s_addr == -1)
    return address_id;

  /* Get a socket for the connection. */
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    write_log("non_blocking_iconnect - socket failed: %s.", strerror(errno));
    return socket_id;
  }

  /* Set the socket non-blocking. */
  flags = fcntl(fd, F_GETFL);
#ifdef FNDELAY
  flags |= FNDELAY;
#else
#ifdef O_NDELAY
  flags |= O_NDELAY;
#endif
#endif
  fcntl(fd, F_SETFL, flags);

  /* Make the connection. */
  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_port = htons((unsigned short) port);
  saddr.sin_addr = inaddr;
  do {
    result = connect(fd, (struct sockaddr *) &saddr, sizeof(saddr));
  } while (result == -1 && errno == EINTR);

  *socket_return = fd;
  if (result != -1 || errno == EINPROGRESS)
    return NOT_AN_IDENT;
  else {
    close(fd);
    return translate_connect_error(errno);
  }
}

static long translate_connect_error(int error)
{
    switch (error) {

      case ECONNREFUSED:
	return refused_id;

      case ENETUNREACH:
	return net_id;

      case ETIMEDOUT:
	return timeout_id;

    case EOPNOTSUPP:
	return NOT_AN_IDENT;

      default:
	return other_id;
    }
}

String *hostname(char *chaddr)
{
   unsigned addr;
   register struct hostent *hp;

   addr = inet_addr(chaddr);
   if (addr == -1)
     return string_from_chars(chaddr, strlen(chaddr));

   hp = gethostbyaddr((char *) &addr, 4, AF_INET);
   if (hp)
     return string_from_chars(hp->h_name, strlen(hp->h_name));
   else
     return string_from_chars(chaddr, strlen(chaddr));
}

String *ip(char *chaddr)
{
   unsigned addr;
   register struct hostent *hp;

   addr = inet_addr(chaddr);
   if (addr == -1) {
     hp = gethostbyname(chaddr);
     if (hp)
       return string_from_chars(inet_ntoa(*(struct in_addr *)hp->h_addr), strlen(inet_ntoa(*(struct in_addr *)hp->h_addr)));
     else
       return string_from_chars("-1", 2);
   } else
       return string_from_chars(chaddr, strlen(chaddr));
}

#if 0
int get_server_usocket(char *path) 
{
  struct sockaddr_un sock_un;
  int s;

  s = socket(AF_UNIX, SOCK_STREAM, 0 );
  if (s < 0) {
    write_log("get_server_usocket - socket failed: %s.", strerror(errno));
    server_failure_reason = socket_id;
    return -1;
  }

  /* Bind the socket to port. */
  sock_un.sun_family = AF_UNIX;
  strcpy(sock_un.sun_path, path);
  unlink(sock_un.sun_path);

  if (bind(s,(struct sockaddr *)&sock_un, strlen(sock_un.sun_path) + 2) < 0) {
    close(s);
    server_failure_reason = bind_id;
    return -1;
  }

  /* Start listening on port.  This shouldn't return an error under any
   * circumstances. */
  listen(s, 8);

  return s;
}
#endif

long non_blocking_uconnect(char *p, int *socket_return)
{
  struct sockaddr_un sock_un;
  int s, result, flags;

  *socket_return = -1;

  /* Get a socket for the connection. */
  s = socket(AF_UNIX, SOCK_STREAM, 0);
  if (s < 0) {
    write_log("non_blocking_uconnect - socket failed: %s - %s.", p, strerror(errno));
    return socket_id;
  }

  /* Set the socket non-blocking. */
  flags = fcntl(s, F_GETFL);
#ifdef FNDELAY
  flags |= FNDELAY;
#else
#ifdef O_NDELAY
  flags |= O_NDELAY;
#endif
#endif
  fcntl(s, F_SETFL, flags);

  /* Make the connection. */
  sock_un.sun_family =  AF_UNIX; 
  strcpy(sock_un.sun_path,p);
  result = connect(s,
		   (struct sockaddr *)&sock_un,
		   strlen(sock_un.sun_path) + 2);

  *socket_return = s;

  if (result) {
    close(s);
    return translate_connect_error(errno);
  } else
    return NOT_AN_IDENT;
}

static int openpty(amaster, aslave, name, termp, winp)
     int *amaster, *aslave;
     char *name;
     struct termios *termp;
     struct winsize *winp;
{
  static char line[] = "/dev/ptyXX";
  register const char *cp1, *cp2;
  register int master, slave, ttygid;
  struct group *gr;

  if ((gr = getgrnam("tty")) != NULL)
    ttygid = gr->gr_gid;
  else
    ttygid = -1;
  
  for (cp1 = "pqrs"; *cp1; cp1++) {
    line[8] = *cp1;
    for (cp2 = "0123456789abcdef"; *cp2; cp2++) {
      struct stat buf;
      line[9] = *cp2;

      if (stat(line, &buf) < 0) {
	if (errno == ENOENT)
	  return (-1);	/* out of ptys */
      } 

      master = open(line, O_RDWR | O_EXCL, 0);
      if ((master >= 0) && (buf.st_mode & 4)) {
	line[5] = 't';
	if ((slave = open(line, O_RDWR, 0)) != -1) {

	  (void) chown(line, getuid(), ttygid);
	  (void) chmod(line, S_IRUSR|S_IWUSR|S_IWGRP);

	  *amaster = master;
	  *aslave = slave;

	  if (name)
	    strcpy(name, line);
	  if (termp)
	    (void) tcsetattr(slave, TCSAFLUSH, termp);
	  if (winp)
	    (void) ioctl(slave, TIOCSWINSZ, (char *)winp);

	  return (0);
	}
      }
      (void) close(master);
      line[5] = 'p';
    }
  }
  errno = ENOENT;	/* out of ptys */
  return (-1);
}


#if 0
 /* non-pty exec */
 /* usage is more like rsh, shell can handle compound command */
 long non_blocking_pconnect(char *prog, int *socket_return)
 {
   int master;
   int slave;
   int pid;

   pid = openpty(&master,
		 &slave,
		 (char *)0,
		 (struct termios *)0,
		 (struct winsize *)0);
   if (pid < 0) {
     write_log("non_blocking_pconnect - socket failed: %s - %s.", prog, strerror(errno));
     return socket_id;
   }

   pid = fork();

   if (pid < 0) {
    write_log("non_blocking_pconnect - socket failed: %s - %s.", prog, strerror(errno));
     return socket_id;
   } else if (pid > 0) {
     /* master is coldmud's end.  Set it non-blocking. */
     int flags;

     flags = fcntl(master, F_GETFL);
 #ifdef FNDELAY
     flags |= FNDELAY;
 #else
 #ifdef O_NDELAY
     flags |= O_NDELAY;
 #endif
 #endif
     fcntl(master, F_SETFL, flags);

     /* parent */
     close(slave);

     *socket_return = master;
     return NOT_AN_IDENT;

   } else {
     /* child */
     int i,j;
     char **argv;

     /* count args */
     for (j = 0, i = 1; prog[j]; j++)
       if (prog[j] == ' ')
	 i++;
     argv = calloc(sizeof(char *), i+1);

     /* unpack args */
     argv[0] = prog;
     for (j = 0, i = 0; prog[j]; j++) {
       if (prog[j] == ' ') {
	 prog[j++] = '\0';
	 while (prog[j] && prog[j] == ' ');
	 if (prog[j])
	   argv[++i] = &prog[j];
       }
     }
     argv[++i]=(char *)0;
 
    (void) close(master);
    (void) setsid();
    (void) ioctl(slave, TIOCSCTTY, (char *)1);

    (void) dup2(slave, 0);
    (void) dup2(slave, 1);
    (void) dup2(slave, 2);

    /* close redundant file descriptors */
     for (i = 3; i < 64;++i)
       close(i);

    /* setbuf(stdout, NULL); */
    
    setuid(getuid());
    setgid(getgid());
    
    execvp(argv[0], argv);

    exit(1);
  }
  
}
#endif

/* non-pty exec */
/* usage is more like rsh, shell can handle compound command */
long non_blocking_pconnect(char *prog, int *socket_return)
{
  int i=0,j=0;
  int soc[2];
  int pid;
  int flags;

  if (socketpair(AF_UNIX, SOCK_STREAM, 0, soc) < 0) {
    write_log("non_blocking_pconnect - socketpair failed: %s - %s.", prog, strerror(errno));
    return socket_id;
  }

  /* soc[0] is coldmud's end.  Set it non-blocking. */
  flags = fcntl(soc[0], F_GETFL);
#ifdef FNDELAY
  flags |= FNDELAY;
#else
#ifdef O_NDELAY
  flags |= O_NDELAY;
#endif
#endif
  fcntl(soc[0], F_SETFL, flags);

  if ((pid = fork ()) < 0) {
    write_log("non_blocking_pconnect - fork failed: %s.", strerror(errno));
    return bind_id;
  }

  if (pid == 0) /* child. */
  { 
    char **argv;

    /* count args */
    for (j = 0, i = 1; prog[j]; j++)
      if (prog[j] == ' ')
	i++;
    argv = calloc(sizeof(char *), i+1);

    /* unpack args */
    argv[0] = prog;
    for (j = 0, i = 0; prog[j]; j++) {
      if (prog[j] == ' ') {
	prog[j++] = '\0';
	while (prog[j] && prog[j] == ' ');
	if (prog[j])
	  argv[++i] = &prog[j];
      }
    }
    argv[++i]=(char *)0;

    close (0);
    close (1);
    close (2);

    /* soc[1] is child's end */
    dup2 (soc[1], 0);
    dup2 (soc[1], 1);
    dup2 (soc[1], 2);
    for (i = 3; i < 64;++i)
      close(i);
    
    setbuf(stdout, NULL);
    
    setuid(getuid());
    setgid(getgid());
    
    execvp(argv[0], argv);

    write_log("non_blocking_pconnect: Failed to exec \"%s\": %s.", argv[0], strerror(errno));
    exit(1);
  }

  /* parent */
  close(soc[1]);

  *socket_return = soc[0];
  return NOT_AN_IDENT;
}

