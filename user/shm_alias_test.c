#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void main() {
    printf("SHM Aliasing Test\n");
    
    // Alloc 1
    int id1 = shmget(0, 4096);
    printf("ID1: %d\n", id1);
    
    // Alloc 2
    int id2 = shmget(0, 4096);
    printf("ID2: %d\n", id2);
    
    if(id1 == id2) {
        printf("FAIL: IDs are identical (%d)\n", id1);
        exit(1);
    }
    
    char *p1 = shmat(id1, 0);
    char *p2 = shmat(id2, 0);
    
    printf("Addr1: %p\n", p1);
    printf("Addr2: %p\n", p2);
    
    if(p1 == p2) {
         printf("FAIL: Addresses are identical\n");
         exit(1);
    }
    
    // Write test
    p1[0] = 'A';
    p2[0] = 'B';
    
    if(p1[0] == 'B') {
        printf("FAIL: Memory is aliased! p1[0] became 'B'\n");
    } else {
        printf("PASS: Memory is unique. p1[0]=%c p2[0]=%c\n", p1[0], p2[0]);
    }
    
    shmdt(id1, p1);
    shmdt(id2, p2);
    exit(0);
}
