#ifndef SHMEM_TEST_COMMON_H_INCL
#define SHMEM_TEST_COMMON_H_INCL

#include <arpa/inet.h>
#include <netinet/in.h>

#include <boost/shared_ptr.hpp>
class CSocket;

extern struct net_interface iface1;
extern struct net_interface iface2;
extern struct net_interface iface3;

extern boost::shared_ptr<CSocket> csocket1;
extern boost::shared_ptr<CSocket> csocket2;

#define MASTER_SOCKET_NAME "shmem_test_master"

#define TEST_SOCKET_BUFSIZE 32708

#endif
