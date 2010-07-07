#include "../cmm_socket_control.h"
#include <stdio.h>

int main()
{
    printf("sizeof(struct CMMSocketControlHdr) = %zu\n",
           sizeof(struct CMMSocketControlHdr));
    return 0;
}
