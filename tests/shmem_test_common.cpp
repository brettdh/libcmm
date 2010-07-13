#include "shmem_test_common.h"
#include "../csocket.h"

struct net_interface iface1 = { {0x0}, 0,0,0,0 };
struct net_interface iface2 = { {0xaaaaaaaa}, 0,0,0,0 };
struct net_interface iface3 = { {0xffffffff}, 0,0,0,0 };

CSocketPtr csocket1;
CSocketPtr csocket2;

