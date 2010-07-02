#ifndef SHMEM_TEST_H_INCL
#define SHMEM_TEST_H_INCL

#include <arpa/inet.h>
#include <netinet/in.h>

extern struct in_addr shmem_iface1;
extern struct in_addr shmem_iface2;

#define MASTER_SOCKET_NAME "shmem_test_master"

#define TEST_SOCKET_BUFSIZE 32708

#endif
