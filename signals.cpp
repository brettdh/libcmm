#include <pthread.h>
#include <signal.h>
#include <string.h>
#include "libcmm.h"
#include "common.h"
#include "signals.h"

#include "tbb/concurrent_hash_map.h"
using tbb::concurrent_hash_map;

typedef concurrent_hash_map<pthread_t, int, 
                            IntegerHashCompare<pthread_t> > ThreadIdMap;
static ThreadIdMap selecting_threads;

static void
select_signal_handler(int signum)
{
    /* nothin' to do... */
}

void signals_init()
{
    struct sigaction actignore, act;
    memset(&act, 0, sizeof(act));
    memset(&actignore, 0, sizeof(actignore));
    act.sa_handler = select_signal_handler;
    sigaction(CMM_SELECT_SIGNAL, &act, NULL);
}

static void register_selecting_thread()
{
    pthread_t tid = pthread_self();
    (void)selecting_threads.insert(ThreadIdMap::value_type(tid, 0));
}

void unblock_select_signals()
{
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, CMM_SELECT_SIGNAL);
    pthread_sigmask(SIG_UNBLOCK, &sigset, NULL);
    
    register_selecting_thread();
}

void block_select_signals()
{
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, CMM_SELECT_SIGNAL);
    pthread_sigmask(SIG_BLOCK, &sigset, NULL);
}

void signal_selecting_threads()
{
    for (ThreadIdMap::iterator it = selecting_threads.begin();
         it != selecting_threads.end(); it++) {
        pthread_kill(it->first, CMM_SELECT_SIGNAL);
    }
}
