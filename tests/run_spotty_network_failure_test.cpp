#include <sys/wait.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include "test_common.h"

int main()
{
    printf("Starting scout\n");
    pid_t scout_pid = fork();
    switch (scout_pid) {
    case -1:
        perror("fork");
        exit(1);
    case 0:
        if (execlp("conn_scout", 
                   "conn_scout", "socket_control",
                   "rmnet0", "12500", "100",
                   "tiwlan0", "125000", "1",
                   NULL) < 0) {
            perror("execlp");
            exit(1);
        }
    }
    sleep(1);
    
    pid_t test_pid = fork();
    switch (test_pid) {
    case -1:
        perror("fork");
        exit(1);
    case 0:
        if (execlp("fake_intnw_test",
                   "fake_intnw_test", "-h", "141.212.110.132",
                   NULL) < 0) {
            perror("execlp");
            exit(1);
        }
    }
    
    int status = -1;
    if (waitpid(test_pid, &status, 0) != test_pid) {
        perror("test waitpid");
        exit(1);
    }
    if (WEXITSTATUS(status) != 0) {
        printf("Tests failed?\n");
    } else {
        printf("Tests passed.\n");
    }
    kill(scout_pid, SIGINT);
    if (waitpid(scout_pid, NULL, 0) != scout_pid) {
        perror("scout waitpid");
        exit(1);
    }
    
    return 0;
}
