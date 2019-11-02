#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM 100

int
main(int argc, char *argv[])
{
  int *frames = malloc(sizeof(int) * NUM);
  int *pids = malloc(sizeof(int) * NUM);
  int numframes = NUM;
  dump_physmem(frames, pids, numframes);
    for (int i = 0; i < numframes; ++i) {
        if (pids[i] > 0)
            printf(1, "framnum: %x, pid: %d\n", frames[i], pids[i]);
    }
  exit();
}
