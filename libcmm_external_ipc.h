#ifndef LIBCMM_EXTERNAL_IPC_H_D69CUJHG
#define LIBCMM_EXTERNAL_IPC_H_D69CUJHG

#include <vector>

int open_scout_socket();
bool get_local_interfaces(std::vector<struct net_interface>& ifaces);
bool is_ip_connected(int ipAddr);

#endif /* end of include guard: LIBCMM_EXTERNAL_IPC_H_D69CUJHG */
