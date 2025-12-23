// size_test.c - Check sizeof sulu_window_shm
#include "kernel/types.h"
#include "user/user.h"
#include "user/sulu_client.h"

int main() {
  int shm_size = sizeof(struct sulu_window_shm);
  int cmd_size = sizeof(struct sulu_cmd);
  int ev_size = sizeof(struct sulu_event);
  int ring_size = sizeof(struct sulu_ring);
  
  printf("sizeof(struct sulu_window_shm) = %d bytes\n", shm_size);
  printf("sizeof(struct sulu_cmd) = %d bytes\n", cmd_size);
  printf("sizeof(struct sulu_event) = %d bytes\n", ev_size);
  printf("sizeof(struct sulu_ring) = %d bytes\n", ring_size);
  
  int width = 25, height = 30;
  int total = shm_size + (width * height * 4);
  printf("Total for 25x30 window: %d bytes\n", total);
  printf("Fits in 4KB? %s\n", total <= 4096 ? "YES" : "NO");
  
  exit(0);
}
