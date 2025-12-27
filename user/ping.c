//
// ping - Simple network test utility
//

#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    printf("ping: Sending echo request to 10.0.2.2 (gateway)...\n");
    
    // Send ARP first to get gateway MAC
    printf("ping: Resolving gateway MAC...\n");
    netping();  // First call triggers ARP
    
    // Wait for ARP reply
    for(int i = 0; i < 10; i++) {
        sleep(10);
        netpoll();
    }
    
    // Now try again with MAC resolved
    printf("ping: Sending ICMP echo...\n");
    int ret = netping();
    if(ret < 0) {
        printf("ping: Gateway MAC not resolved yet, try again\n");
    }
    
    // Poll for reply
    for(int i = 0; i < 50; i++) {
        netpoll();
        sleep(10);
    }
    
    printf("ping: Done\n");
    exit(0);
}
