#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  int *frames = 0;
  int *pids = 0;
  int numframes = 10;
  dump_physmem(frames, pids, numframes);
    for (int i = 0; i < numframes; ++i) {
        printf(1, "framnum: %d, pid: %d\n", frames[i], pids[i]);
    }
  exit();
}
