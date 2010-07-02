/***************************************************************************
 * libancillary - black magic on Unix domain sockets
 * (C) Nicolas George
 * test.c - testing and example program
 ***************************************************************************/

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include "ancillary.h"
#include <sys/un.h>

void child_process(int sock, struct sockaddr *dst_addr)
{
    int fd;
    int fds[3], nfds;
    char b[] = "This is on the received fd!\n";

    if(ancil_recv_fd_from(sock, &fd, dst_addr, sizeof(struct sockaddr_un))) {
	perror("ancil_recv_fd_from");
	exit(1);
    } else {
	printf("Received fd: %d\n", fd);
    }
    write(fd, b, sizeof(b));
    close(fd);
    sleep(2);

    nfds = ancil_recv_fds_from(sock, fds, 3, dst_addr, sizeof(struct sockaddr_un));
    if(nfds < 0) {
	perror("ancil_recv_fds_from");
	exit(1);
    } else {
	printf("Received %d/3 fds : %d %d %d.\n", nfds,
	    fds[0], fds[1], fds[2]);
    }
}

void parent_process(int sock, struct sockaddr *dst_addr)
{
    int fds[2] = { 1, 2 };

    if(ancil_send_fd_to(sock, 1, dst_addr, sizeof(struct sockaddr_un))) {
	perror("ancil_send_fd_to");
	exit(1);
    } else {
	printf("Sent fd.\n");
    }
    sleep(1);

    if(ancil_send_fds_to(sock, fds, 2, dst_addr, sizeof(struct sockaddr_un))) {
	perror("ancil_send_fds_to");
	exit(1);
    } else {
	printf("Sent two fds.\n");
    }
}

int main(void)
{
    int sock[2];

    struct sockaddr_un addr0, addr1;
    memset(&addr0, 0, sizeof(addr0));
    addr0.sun_family = AF_UNIX;
    strcpy(&addr0.sun_path[1], "ancil_test_sock0");

    memset(&addr1, 0, sizeof(addr1));
    addr1.sun_family = AF_UNIX;
    strcpy(&addr1.sun_path[1], "ancil_test_sock1");

    sock[0] = socket(PF_UNIX, SOCK_DGRAM, 0);
    if (sock[0] < 0) {
        perror("socket");
        exit(1);
    }
    if (bind(sock[0], (struct sockaddr *)&addr0, sizeof(addr0)) < 0) {
        perror("bind");
        exit(1);
    }
    
    sock[1] = socket(PF_UNIX, SOCK_DGRAM, 0);
    if (sock[1] < 0) {
        perror("socket");
        exit(1);
    }
    if (bind(sock[1], (struct sockaddr *)&addr1, sizeof(addr1)) < 0) {
        perror("bind");
        exit(1);
    }

    printf("Established socket pair: (%d, %d)\n", sock[0], sock[1]);
    
    switch(fork()) {
	case 0:
	    close(sock[0]);
	    child_process(sock[1], (struct sockaddr *)&addr0);
	    break;
	case -1:
	    perror("fork");
	    exit(1);
	default:
	    close(sock[1]);
	    parent_process(sock[0], (struct sockaddr *)&addr1);
	    wait(NULL);
	    break;
    }
    return(0);
}
