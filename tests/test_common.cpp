#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include "test_common.h"

#include <cppunit/Message.h>
#include <cppunit/Asserter.h>
#include <sstream>
#include <cmath>
using std::fabs;

void nowake_nanosleep(const struct timespec *duration)
{
    struct timespec time_left;
    struct timespec sleep_time = *duration;
    while (nanosleep(&sleep_time, &time_left) < 0) {
        if (errno != EINTR) {
            perror("nanosleep");
            exit(-1);
        }
        sleep_time = time_left;
    }
}

void print_on_error(bool err, const char *str)
{
    if (err) {
        perror(str);
    }
}

void handle_error(bool condition, const char *msg)
{
    if (condition) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

void assertEqWithin(const std::string& actual_str, 
                    const std::string& expected_str, 
                    const std::string& message, 
                    double expected, double actual, double alpha,
                    CppUnit::SourceLine line)
{
    double val = fabs(expected - actual);
    double window = fabs(alpha * expected);
    
    const int precision = 15;
    char expected_buf[32];
    char actual_buf[32];
    char alpha_buf[32];
    char window_buf[32];
    char diff_buf[32];
    sprintf(expected_buf, "%.*g", precision, expected);
    sprintf(actual_buf, "%.*g", precision, actual);
    sprintf(alpha_buf, "%.*g", precision, alpha);
    sprintf(window_buf, "%.*g", precision, window);
    sprintf(diff_buf, "%.*g", precision, val);
    
    std::ostringstream expr;
    expr << "Expression: abs("
         << actual_str << " - " << expected_str << ") <= " << window_buf
         << "(" << expected_str << "*" << alpha_buf << ")";

    std::ostringstream values;
    values << "Values    : abs("
           << actual_buf << " - " << expected_buf << ") = " << diff_buf;

    CppUnit::Message msg("assertion failed", 
                         expr.str(), values.str(), message);
    CppUnit::Asserter::failIf(!(val <= window), msg, line);
}

int get_int_from_string(const char *str, const char *name, char *prog)
{
    errno = 0;
    int val = strtol(str, NULL, 10);
    if (errno != 0) {
        dbgprintf_always("Error: %s argument must be an integer\n", name);
        usage(prog);
    }
    return val;
}

int open_listening_socket(bool intnw, uint16_t port)
{
    listen_sock = socket(PF_INET, SOCK_STREAM, 0);
    handle_error(listen_sock < 0, "socket");
    
    int on = 1;
    int rc = setsockopt (listen_sock, SOL_SOCKET, SO_REUSEADDR,
                         (char *) &on, sizeof(on));
    if (rc < 0) {
        fprintf(stderr, "Cannot reuse socket address\n");
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
