#ifndef net_stats_h_incl
#define net_stats_h_incl

#define NET_STATS_LATENCY 0
#define NET_STATS_BW_UP   1

// unlikely that sender will need this, but it is here for completeness
#define NET_STATS_BW_DOWN 2

// each CSocket will include an object of this type, since the stats
// are kept for each (local,remote) interface pair.
class NetStats {
  public:
    u_long get_estimate(short type);
    
  private:
    struct in_addr local_addr;
    struct in_addr remote_addr;
};

#endif
