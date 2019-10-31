// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
void freerange2(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct track {
    int frames[16385];
    int pids[16385];
    int index;
}track;

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange2(vstart, vend);
  kmem.use_lock = 1;
    memset(track.frames, -1, 16384 * sizeof(int));
    memset(track.pids, -1, 16384 * sizeof(int));
    track.index = 0;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree2(p);
}

void
freerange2(void *vstart, void *vend)
{
    char *p;
    p = (char*)PGROUNDUP((uint)vstart) + PGSIZE;        // TODO: modified
    for(; p + PGSIZE <= (char*)vend; p += PGSIZE*2)       // TODO: modified
        kfree2(p);                                           // TODO: modified
}
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
    int framenum = V2P(r) >> 12;
    int flag = 0;
    for (int i = 0; i < track.index; ++i) {
        if (track.frames[i] == framenum)
            flag = 1;
        if (flag) {
            track.frames[i] = track.frames[i+1];
            track.pids[i] = track.pids[i+1];
        }
    }
    track.index--;
  if(kmem.use_lock)
    release(&kmem.lock);
}

void
kfree2(char *v)
{
    struct run *r;

    if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
        panic("kfree");

    // Fill with junk to catch dangling refs.
    memset(v, 1, PGSIZE);

    if(kmem.use_lock)
        acquire(&kmem.lock);
    r = (struct run*)v;
    r->next = kmem.freelist;
    kmem.freelist = r;
    if(kmem.use_lock)
        release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;

  track.frames[track.index] = V2P(r) >> 12;
  track.pids[track.index++] = -2;

  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
}

char*
kalloc2(int pid)
{
    struct run *r;

    if(kmem.use_lock)
        acquire(&kmem.lock);
    r = kmem.freelist;
    if(r)
        kmem.freelist = r->next;
    track.frames[track.index] = V2P(r) >> 12;
    track.pids[track.index++] = pid;
    if(kmem.use_lock)
        release(&kmem.lock);
    return (char*)r;
}

// This system call is used to find which process owns each frame of physical memory.
int
dump_physmem(int *_frames, int *_pids, int _numframes)
{
    if (_numframes < 0 || _frames == 0 || _pids == 0) {
        return -1;
    }

    for (int i = 0; i < _numframes; ++i) {
        _frames[i] = track.frames[i];
        _pids[i] = track.pids[i];
    }
    return 0;
}