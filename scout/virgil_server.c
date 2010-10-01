#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/errno.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#define APTESTS_DOWNBW_PORT	4321
#define APTESTS_UPBW_PORT	(APTESTS_DOWNBW_PORT + 1)
#define APTESTS_LATENCY_PORT	(APTESTS_DOWNBW_PORT + 2)
#define DOWNSTREAM 1
#define UPSTREAM 2

#define MAX_SIMULTANEOUS_CONNS 10

struct thread_arg {
  int cli_sock;
  int port;
};

void sigpipe_handler(int sig) {
  printf("sigpipe_handler(): received signal %d\n", sig);
  signal(SIGPIPE, sigpipe_handler);
}

void * port_server( void * arg ) {
  struct thread_arg * t_arg = (struct thread_arg *)arg;
  int port = t_arg->port;
  int client_d = t_arg->cli_sock;
  int token = 42;
  struct timeval t;
	
  t.tv_sec = 5; t.tv_usec = 0;
  setsockopt(client_d, SOL_SOCKET, SO_SNDTIMEO, &t, sizeof(struct timeval));
  setsockopt(client_d, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(struct timeval));
  if ( 0 > send(client_d, (void *)&token, sizeof(int), 0) ) {
    printf("port_server(): error in send!\n");
  }

  printf("port %d: closing socket.\n", port);	
  close(client_d);

  return(NULL);
}

void * latency_server( void * arg ) {
  struct thread_arg * t_arg = (struct thread_arg *)arg;
  int client_d = t_arg->cli_sock;
  int rc, token, client_closed;

  struct timeval t;
  t.tv_sec = 5; t.tv_usec = 0;
  setsockopt(client_d, SOL_SOCKET, SO_SNDTIMEO, &t, sizeof(struct timeval));
  setsockopt(client_d, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(struct timeval));

  client_closed = 0;
  while ( !client_closed ) {
    rc = recv(client_d, (void *)&token, sizeof(int), 0);
    if ( rc <= 0 ) client_closed = 1;
    else {
      rc = send(client_d, (void *)&token, sizeof(int), 0);
      if ( rc <= 0 ) client_closed = 1;
    }
  }
  close( client_d );
  return( (void *)0 );
}

void * bw_server(void * arg) {
  struct thread_arg * t_arg = (struct thread_arg *)arg;
  int port = t_arg->port;
  int client_d = t_arg->cli_sock;
  int rc, nbytes;
  int client_closed, direction;
  char * buf;

  buf = (char *)malloc(1024*10); /* 1 MB */
  memset(buf, 0x42, 1024*10);
  int buflen = 1024*10;

  printf("port=%d client_d=%d\n", port, client_d);
  if ( port == APTESTS_DOWNBW_PORT ) direction = DOWNSTREAM;
  else if ( port == APTESTS_UPBW_PORT ) direction = UPSTREAM;
  else {
    printf("bw_server(): bad port # %d\n", port);
    return( (void *)-1 );
  }

  struct timeval t;
  t.tv_sec = 5; t.tv_usec = 0;
  setsockopt(client_d, SOL_SOCKET, SO_SNDTIMEO, &t, sizeof(struct timeval));
  setsockopt(client_d, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(struct timeval));

  client_closed = 0;
  nbytes = 0;
  while ( !client_closed ) {
    if ( direction == DOWNSTREAM ) { /* client wants to fetch packets */
      rc = send( client_d, (void *)buf, buflen, 0 );
    }
    else { /* client wants to put packets */
      rc = recv( client_d, (void *)buf, buflen, 0 );
    }
    
    if ( rc <= 0 ) {
      client_closed = 1;
      break;
    }
    else nbytes += rc;
  }

  printf("nbytes = %d %s\n", nbytes,
	 (direction==DOWNSTREAM) ? "sent" : "received");
  close( client_d );
  return( (void *)0 );
}

void * server( void * arg ) {
  int port = (int)arg;
  int serv_d, client_d, opt;
  struct sockaddr_in srv_addr;
  struct thread_arg * t_arg;

  serv_d = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
  if ( 0 > serv_d ) {
    printf("server: port %d error creating socket.\n", port);
    return( (void *)-1 );
  }
  opt = 1;
  if (setsockopt(serv_d,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(int)) < 0) {
    printf("server(): port %d error setting reuse socket option!\n",port);
    return( (void *)-1 );
  }  
  memset(&srv_addr, sizeof(srv_addr), 0);
  srv_addr.sin_family = AF_INET;
  srv_addr.sin_addr.s_addr = htonl( INADDR_ANY );
  srv_addr.sin_port = htons( port );

  if ( 0 > bind(serv_d, (struct sockaddr *)&srv_addr, 
		sizeof(struct sockaddr_in)) ) {
    printf("server(): port %d error binding port.\n", port);
    exit(-1);
  }
  printf("server(): bound to port %d\n", port);
  if (0 > listen( serv_d, MAX_SIMULTANEOUS_CONNS ) ) {
    printf("error putting socket in listen mode!\n");
    exit(-1);
  }
  
  while ( 1 ) {
    client_d = accept(serv_d, NULL, NULL);
    printf("server(): accepted on port %d\n", port);
    t_arg = (struct thread_arg *)malloc(sizeof(struct thread_arg));
    t_arg->cli_sock = client_d;
    t_arg->port = port;
    switch( port ) {
    case APTESTS_DOWNBW_PORT:
      bw_server( (void *)t_arg);
      break;
    case APTESTS_UPBW_PORT:
      bw_server( (void *)t_arg);
      break;
    case APTESTS_LATENCY_PORT:
      latency_server( (void *)t_arg );
      break;
    default:
      port_server( (void *)t_arg );
      break;
    }
  }
  return( (void *)0 );
}

int main() {
  int i;
  pthread_t servers[7];
  //int port_nums[6] = {993,25,5222,445};

  /* ignore broken pipes */
  //signal(SIGPIPE, SIG_IGN);
  signal(SIGPIPE, sigpipe_handler);

  pthread_create( &servers[0], NULL, server, (void *)APTESTS_LATENCY_PORT);
  pthread_create( &servers[1], NULL, server, (void *)APTESTS_DOWNBW_PORT);
  pthread_create( &servers[2], NULL, server, (void *)APTESTS_UPBW_PORT);
  /*
  for (i=0; i<4; i++) 
    pthread_create(&servers[i+3], NULL, server, (void *)port_nums[i]);
  for (i=0; i<7; i++) pthread_join(servers[i], NULL);
  */
  for (i=0; i<4; i++) pthread_join(servers[i], NULL);
  printf("All threads exited, quitting.\n");
  return(0);
}
