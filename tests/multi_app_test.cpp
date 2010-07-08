#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <libcmm.h>
#include <libcmm_irob.h>
#include <assert.h>
#include <stdexcept>
#include "multi_app_test_helper.h"
#include "test_common.h"
#include "debug.h"
#include <functional>
using std::min; using std::max;

int open_listening_socket(bool intnw, uint16_t port)
{
    int listen_sock = socket(PF_INET, SOCK_STREAM, 0);
    handle_error(listen_sock < 0, "socket");
    
    int on = 1;
    int rc = setsockopt (listen_sock, SOL_SOCKET, SO_REUSEADDR,
                         (char *) &on, sizeof(on));
    if (rc < 0) {
        dbgprintf_always("Cannot reuse socket address\n");
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    socklen_t addrlen = sizeof(addr);
    rc = bind(listen_sock, (struct sockaddr*)&addr, addrlen);
    handle_error(rc < 0, "bind");
    
    if (intnw) {
        rc = cmm_listen(listen_sock, 5);
    } else {
        rc = listen(listen_sock, 5);
    }
    handle_error(rc < 0, "cmm_listen");
    
    return listen_sock;
}

typedef enum {
    TEST_MODE_SINGLE_SOCKET,
    TEST_MODE_SINGLE_PROCESS,
    TEST_MODE_MULTI_PROCESS,
    TEST_MODE_INVALID
} test_mode_t;


class ServerWorkerThread {
    mc_socket_t sock;
  public:
    ServerWorkerThread(mc_socket_t sock_) : sock(sock_) {}
    void operator()() {
        const size_t chunksize = 64*1024;
        char buf[chunksize];
        struct packet_hdr hdr;
        while (1) {
            u_long labels = 0;
            int rc = cmm_recv(sock, &hdr, sizeof(hdr), MSG_WAITALL, &labels);
            if (rc != sizeof(hdr)) {
                break;
            }
            fprintf(stderr, "Received %s request on socket %d; "
                    "seqno %d len %d\n",
                    ((labels == 0) ? "unlabeled" :
                     (labels & CMM_LABEL_ONDEMAND) ? "FG" : "BG"),
                    sock, ntohl(hdr.seqno), ntohl(hdr.len));
            fprintf(stderr, "About to receive %d bytes of data\n", 
                    ntohl(hdr.len));
            size_t bytes_recvd = 0;
            size_t len = ntohl(hdr.len);
            while (bytes_recvd < len) {
                size_t bytes_to_recv = min(len - bytes_recvd, chunksize);
                rc = cmm_read(sock, buf, bytes_to_recv, NULL);
                if (rc <= 0) {
                    if (rc < 0) {
                        perror("cmm_recv");
                    }
                    fprintf(stderr, "Failed after receiving %zu out of "
                            "%zu data bytes\n", bytes_recvd, len);
                    break;
                }
                bytes_recvd += rc;
            }
            if (bytes_recvd < len) break;
            
            labels &= ~CMM_LABEL_LARGE;
            labels |= CMM_LABEL_SMALL;
            fprintf(stderr, "Sending %s response %d on socket %d\n",
                    ((labels == 0) ? "unlabeled" :
                     (labels & CMM_LABEL_ONDEMAND) ? "FG" : "BG"),
                    ntohl(hdr.seqno), sock);
            rc = cmm_write_with_deps(sock, &hdr, sizeof(hdr), 
                                     0, NULL, labels, NULL, NULL, NULL);
            if (rc != sizeof(hdr)) {
                fprintf(stderr, "Failed to send response %d\n",
                        ntohl(hdr.seqno));
                break;
            }
        }
        fprintf(stderr, "Done with connection %d\n", sock);
        cmm_shutdown(sock, SHUT_RDWR);
        cmm_close(sock);
    }
};

static void spawn_server_worker_thread(int listen_sock)
{
    int sock = cmm_accept(listen_sock, NULL, 0);
    if (sock < 0) {
        perror("cmm_accept");
        fprintf(stderr, "Failed to accept new connection\n");
        return;
    }
    fprintf(stderr, "Accepted connection %d\n", sock);
    
    ServerWorkerThread thread_data(sock);
    boost::thread the_thread(thread_data);
    // detaches the thread upon return (scope destruction)
}

static void run_server()
{
    int vanilla_listener = open_listening_socket(false, MULTI_APP_VANILLA_TEST_PORT);
    int intnw_listener = open_listening_socket(true, MULTI_APP_INTNW_TEST_PORT);
    int maxfd = max(vanilla_listener, intnw_listener);
    
    fd_set listeners;
    FD_ZERO(&listeners);
    FD_SET(vanilla_listener, &listeners);
    FD_SET(intnw_listener, &listeners);
    int rc;
    while (1) {
        rc = cmm_select(maxfd + 1, &listeners, NULL, NULL, NULL);
        if (FD_ISSET(vanilla_listener, &listeners)) {
            fprintf(stderr, "Got connection from vanilla client\n");
            spawn_server_worker_thread(vanilla_listener);
        } else if (FD_ISSET(intnw_listener, &listeners)) {
            fprintf(stderr, "Got connection from intnw client\n");
            spawn_server_worker_thread(intnw_listener);
        }

        FD_ZERO(&listeners);
        FD_SET(vanilla_listener, &listeners);
        FD_SET(intnw_listener, &listeners);
    }
}

#define STR_AND_INT(name, value) \
    const int name = value;      \
    const char *name##_str = #value;

STR_AND_INT(fg_chunksize, 128)
STR_AND_INT(fg_send_period, 2)
STR_AND_INT(fg_start_delay, 20)
STR_AND_INT(fg_sending_duration, 40)

STR_AND_INT(bg_chunksize, 262144)
STR_AND_INT(bg_send_period, 0)
STR_AND_INT(bg_start_delay, 0)
STR_AND_INT(bg_sending_duration, 60)

// returns the socket created, or -1 for failure.
static int connect_client_socket(char *hostname, bool intnw)
{
    int sock;
    if (intnw) {
        sock = cmm_socket(PF_INET, SOCK_STREAM, 0);
    } else {
        sock = socket(PF_INET, SOCK_STREAM, 0);
    }
    if (sock < 0) {
        perror("cmm_socket");
        return sock;
    }
    
    int rc = cmm_connect_to(sock, hostname,
                            intnw ? MULTI_APP_INTNW_TEST_PORT
                            : MULTI_APP_VANILLA_TEST_PORT);
    if (rc < 0) {
        perror("cmm_connect");
        cmm_close(sock);
        return -1;
    }

    if (sock < 0) {
        fprintf(stderr, "Failed to create/connect socket\n");
    }
    return sock;
}

static const char *helper_bin = "/home/brettdh/src/libcmm/tests/multi_app_test_helper";

static void spawn_process(char *hostname, bool intnw, 
                          single_app_test_mode_t mode)
{
    const char *intnw_str = intnw ? "intnw" : "vanilla";
    int rc = fork();
    if (rc == 0) {
        switch (mode) {
        case ONE_SENDER_FOREGROUND:
            rc = execl(helper_bin, helper_bin, hostname, intnw_str, 
                       "foreground", fg_chunksize_str, fg_send_period_str,
                       fg_start_delay_str, fg_sending_duration_str, NULL);
            break;
        case ONE_SENDER_BACKGROUND:
            rc = execl(helper_bin, helper_bin, hostname, intnw_str,
                       "background", bg_chunksize_str, bg_send_period_str,
                       bg_start_delay_str, bg_sending_duration_str, NULL);
            break;
        case TWO_SENDERS:
            rc = execl(helper_bin, helper_bin, hostname, intnw_str, "mix",
                       bg_chunksize_str, bg_send_period_str,
                       bg_start_delay_str, bg_sending_duration_str,
                       bg_chunksize_str, bg_send_period_str,
                       bg_start_delay_str, bg_sending_duration_str, NULL);
            break;
        default:
            assert(0);
        }
        
        if (rc < 0) {
            perror("execl");
            exit(1);
        }
    } else if (rc < 0) {
        perror("fork");
        exit(1);
    }
}

// return the number of processes spawned.
static int run_sanity_check(char *hostname, test_mode_t mode, bool intnw)
{
    if (mode == TEST_MODE_SINGLE_SOCKET ||
        mode == TEST_MODE_SINGLE_PROCESS) {

        AgentData data;
        AgentData bg_data;

        int bg_sock;
        int sock = connect_client_socket(hostname, intnw);
        if (sock < 0) {
            exit(1);
        }
        if (mode == TEST_MODE_SINGLE_PROCESS) {
            bg_sock = connect_client_socket(hostname, intnw);
        } else {
            bg_sock = sock;
        }
        if (bg_sock < 0) {
            exit(1);
        }
            
        boost::thread_group group;
        group.create_thread(ReceiverThread(sock, &data));
        if (bg_sock != sock) {
            group.create_thread(ReceiverThread(bg_sock, &bg_data));
        }
        SenderThread sender(sock, true, fg_chunksize,
                            fg_send_period, fg_start_delay, fg_sending_duration);
        sender.data = &data;
        group.create_thread(sender);

        SenderThread bg_sender(bg_sock, false, bg_chunksize,
                               bg_send_period, bg_start_delay, bg_sending_duration);
        if (bg_sock != sock) {
            bg_sender.data = &bg_data;
        } else {
            bg_sender.data = &data;
        }
        group.create_thread(bg_sender);
        // wait for run to complete
        group.join_all();

        FILE *output = fopen("./single_process_sanity_check.txt", "w");
        if (!output) {
            perror("fopen");
            fprintf(stderr, "printing results to stderr\n");
            output = stderr;
        }
        fprintf(output, "Worker PID %d - %s foreground sender results\n", 
                getpid(), intnw ? "intnw" : "vanilla");
        print_stats(data.fg_results, fg_chunksize, output);
        fprintf(output, "Worker PID %d - %s background sender results\n", 
                getpid(), intnw ? "intnw" : "vanilla");
        print_stats(bg_sender.data->bg_results, bg_chunksize, output);
        return 0;
    } else {
        spawn_process(hostname, intnw, ONE_SENDER_FOREGROUND);
        spawn_process(hostname, intnw, ONE_SENDER_BACKGROUND);
        return 2;
    }
}

//return the number of processes spawned.
static int run_all_tests(char *hostname, test_mode_t mode, 
                          int num_vanilla_agents, int num_intnw_agents)
{
    if (mode == TEST_MODE_SINGLE_SOCKET ||
        mode == TEST_MODE_SINGLE_PROCESS) {
        // XXX: These are not important.  Not working on them right now.
        fprintf(stderr, "NOT IMPLEMENTED\n");
        exit(1);

        if (mode == TEST_MODE_SINGLE_SOCKET) {
            // bool intnw = (num_vanilla_agents == 0);
            //assert(num_vanilla_agents == 0 ||
            //       num_intnw_agents == 0);
            // ...
        } else {
            assert(mode == TEST_MODE_SINGLE_PROCESS);
            // ...
        }
        
        return 0;
    } else {
        assert(mode == TEST_MODE_MULTI_PROCESS);
        int num_procs = 0;
        num_procs = num_vanilla_agents + num_intnw_agents;
        for (int i = 0; i < num_vanilla_agents; ++i) {
            spawn_process(hostname, false, TWO_SENDERS);
        }
        for (int i = 0; i < num_intnw_agents; ++i) {
            spawn_process(hostname, true, TWO_SENDERS);
        }
        return num_procs;
    }
}

#define USAGE_MSG \
"usage: %s [ options ]\n\
\n\
Multi-app test harness\n\
    Use this command to test multi-app interactions in Intentional Networking.\n\
Options:\n\
    -l\n\
         Act as server, accepting connections.\n\
    -h <hostname>\n\
         Connect to the specified hostname to run tests.\n\
\n\
    -s\n\
         Use a single process with a single (multi-)socket.\n\
    -m\n\
         Use a single process with many (multi-)sockets.\n\
    -a\n\
         Use one process per (multi-)socket.\n\
\n\
    -v <number of agents>\n\
         Spawn the specified number of vanilla, non-intnw apps.\n\
    -i <number of agents>\n\
         Spawn the specified number of intnw apps.\n\
\n\
    -q <vanilla|intnw>\n\
         Special flag; ignore -v and -i and instead sanity-check the \n\
         interaction between FG and BG traffic, vanilla or intnw.\n\
         The -s/-m/-a options are taken into account for this test.\n\
"

void usage(char *prog)
{
    fprintf(stderr, USAGE_MSG, prog);
    exit(1);
}

static void check_mode_arg(test_mode_t& target, test_mode_t mode, char *prog)
{
    if (target == TEST_MODE_INVALID) {
        target = mode;
    } else {
        fprintf(stderr, "Error: only one of -s, -m, -a at a time\n");
        usage(prog);
    }
}

int main(int argc, char *argv[])
{
    int ch;
    bool receiver = false;
    int num_vanilla_agents = 0;
    int num_intnw_agents = 0;
    char *hostname = NULL;
    test_mode_t mode = TEST_MODE_INVALID;
    bool sanity_check = false;
    bool sanity_check_intnw = false;

    try {
        while ((ch = getopt(argc, argv, "lh:smav:i:q:")) != -1) {
            switch (ch) {
            case 'l':
                receiver = true;
                break;
            case 'h':
                hostname = optarg;
                break;
            case 's':
                // single multi-socket.
                check_mode_arg(mode, TEST_MODE_SINGLE_SOCKET, argv[0]);
                break;
            case 'm':
                // single process, multiple multi-sockets.
                check_mode_arg(mode, TEST_MODE_SINGLE_PROCESS, argv[0]);
                break;
            case 'a':
                // multiple processes.
                check_mode_arg(mode, TEST_MODE_MULTI_PROCESS, argv[0]);
                break;
            case 'v':
                num_vanilla_agents = get_int_from_string(optarg, "-v");
                break;
            case 'i':
                num_intnw_agents = get_int_from_string(optarg, "-i");            
                break;
            case 'q':
                sanity_check = true;
                if (!strcmp(optarg, "vanilla")) {
                    sanity_check_intnw = false;
                } else if (!strcmp(optarg, "intnw")) {
                    sanity_check_intnw = true;
                } else {
                    fprintf(stderr, "Error: -q requires vanilla or intnw argument\n");
                    usage(argv[0]);
                }
                break;
            case '?':
                usage(argv[0]);
            default:
                break;
            }
        }
    } catch (std::runtime_error& e) {
        fprintf(stderr, e.what());
        usage(argv[0]);
    }

    if (receiver) {
        run_server();
    } else {
        if (mode == TEST_MODE_INVALID) {
            fprintf(stderr, "Error: must specify exactly one of -s/-m/-a\n");
            usage(argv[0]);
        }
        if (num_vanilla_agents < 0 || num_intnw_agents < 0) {
            fprintf(stderr, "Error: must specify positive integers for -v/-i\n");
            usage(argv[0]);
        }
        if (!sanity_check &&
            num_vanilla_agents == 0 && num_intnw_agents == 0) {
            fprintf(stderr, "Error: client must specify at least one agent with -v/-i\n");
            usage(argv[0]);
        }

        if (mode == TEST_MODE_SINGLE_SOCKET &&
            (num_vanilla_agents > 0 ||
             num_intnw_agents > 0)) {
            fprintf(stderr, "Error: cannot have both vanilla and "
                    "intnw agents for single-socket mode\n");
            usage(argv[0]);
        }

        /*
        time_t t = time(NULL);
        struct tm *tmp = localtime(&t);
        if (tmp == NULL) {
            perror("localtime");
            exit(1);
        }
        char dir_str[51];
        memset(dir_str, 0, sizeof(dir_str));
        if (strftime(dir_str, 50, 
                     "./multi_app_test_result_%Y-%m-%d__%H_%M_%S", tmp) == 0) {
            fprintf(stderr, "strftime failed!\n");
            exit(1);
        }
        
        if (mkdir(dir_str, 0777) < 0) {
            perror("mkdir");
            fprintf(stderr, "Couldn't create results directory %s\n",
                    dir_str);
            exit(1);
        }
        if (chdir(dir_str) < 0) {
            perror("chdir");
            exit(1);
        }
        */
        int num_procs = 0;
        if (sanity_check) {
            num_procs = run_sanity_check(hostname, mode, sanity_check_intnw);
        } else {
            num_procs = run_all_tests(hostname, mode, 
                                      num_vanilla_agents, num_intnw_agents);
        }        
        
        for (int i = 0; i < num_procs; ++i) {
            if (wait(NULL) < 0) {
                perror("wait");
                exit(1);
            }
        }
        //chdir("..");
    }

    return 0;
}
