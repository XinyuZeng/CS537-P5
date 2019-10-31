#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  int *frames = malloc(sizeof(int) * 10);
  int *pids = malloc(sizeof(int) * 10);
  int numframes = 10;
  dump_physmem(frames, pids, numframes);
    for (int i = 0; i < numframes; ++i) {
        printf(1, "framnum: %x, pid: %d\n", frames[i], pids[i]);
    }
  exit();
}
