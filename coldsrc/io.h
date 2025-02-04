/* io.h: Declarations for input/output management. */

#ifndef IO_H
#define IO_H

typedef struct connection Connection;
typedef struct server Server;
typedef struct pending Pending;

#include "cmstring.h"
#include "data.h"

struct connection {
    int fd;			/* File descriptor for input and output. */
    Buffer *write_buf;		/* Buffer for network output. */
    Dbref dbref;		/* The player, usually. */
    struct {
      char readable;		/* Connection has new data pending. */
      char writable;		/* Connection can be written to. */
      char dead;		/* Connection is defunct. */
      char pipe;		/* Connection is a pipe */
      char writecallback;	/* Connection wants notification on write */
    } flags;
    Connection *next;
};

struct server {
    int server_socket;
    unsigned short port;
    Dbref dbref;
    int dead;
    int client_socket;
    char client_addr[20];
    unsigned short client_port;
    Server *next;
};

struct pending {
    int fd;
    long task_id;
    Dbref dbref;
    long error;
    char pipe;
    int finished;
    Pending *next;
};

void init_io(void);
void flush_defunct(void);
void handle_io_events(long sec);
void tell(long dbref, Buffer *buf);
int boot(long dbref);
int add_server(int port, long dbref);
int remove_server(int port);
long make_connection(char *addr, int port, Dbref receiver);
void flush_output(void);

#endif

