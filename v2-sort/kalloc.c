// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

#define LENGTH 32768

void freerange(void *vstart, void *vend);
void freerange2(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct track {
    int frames[LENGTH];
    int pids[LENGTH];
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
  freerange(vstart, vend);
  kmem.use_lock = 1;
    memset(track.frames, -1, LENGTH * sizeof(int));
    memset(track.pids, -1, LENGTH * sizeof(int));
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

int V2FrameNum(void * r) {
    return V2P(r) >> 12;
}

void placeFreePage(struct run* r, struct run* freelist) {
    int frameNum = V2P(r) >> 12;
    if (frameNum > V2P(freelist) >> 12) {
        r->next = kmem.freelist;
        kmem.freelist = r;
        return;
    }
    struct run* p1 = freelist;
    struct run* p2 = freelist->next;
    while (p2 != 0) {
        if (V2FrameNum((void *) p2) > frameNum) {
            p1 = p1->next;
            p2 = p2->next;
            continue;
        }
        p1->next = r;
        r->next = p2;
        return;
    }
    p1->next = r;
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
//  r->next = kmem.freelist;
//  kmem.freelist = r;
    placeFreePage(r, kmem.freelist);
    int framenum = V2P(r) >> 12;
//    cprintf("%x got freed\n", V2P(r) >> 12);
//    cprintf("%x\n", framenum);
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

// return 0 on false, 1 on true
int ableToUse(int frameNum, int pid, int *allocFrameNum, int *pids, int index) {
    if (pid == -2)
        return 1;
    int leftOK = 0, rightOK = 0;
    for (int i = 0; i < index; ++i) {
        if (allocFrameNum[i] == frameNum - 1) {
            if (pids[i] != pid && pids[i] != -2) {
                return 0;
            }
            leftOK = 1;
        } else if (allocFrameNum[i] == frameNum + 1) {
            if (pids[i] != pid && pids[i] != -2) {
                return 0;
            }
            rightOK = 1;
        }
        if (leftOK && rightOK)
            return 1;
    }
    return 1;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
    return kalloc2(-2);
}

char*
kalloc2(int pid)
{
    struct run *r;

    if(kmem.use_lock)
        acquire(&kmem.lock);
    r = kmem.freelist;
    struct run head;
    head.next = kmem.freelist;
    struct run *pre = &head;
    while (r != 0 && !ableToUse(V2P(r) >> 12, pid, track.frames, track.pids, track.index)) {
        pre = pre->next;
        r = r->next;
    }
    if(r) {
        if (pre == &head) {
            kmem.freelist = r->next;
        } else {
            pre->next = r->next;
        }


        track.frames[track.index] = V2P(r) >> 12;
        track.pids[track.index++] = pid;
//        cprintf("%x got alloc, pid: %d\n", V2P(r) >> 12, pid);
    }

    if(kmem.use_lock)
        release(&kmem.lock);
    return (char*)r;
}

void merge(int frames[], int pids[], int lo, int mid, int hi) {
    int i, j, k;
    int n1 = mid - lo + 1;
    int n2 = hi - mid;

    int L1[n1], R1[n2];
    int L2[n1], R2[n2];

    for (i = 0; i < n1; i++) {
        L1[i] = frames[lo + i];
        L2[i] = pids[lo + i];
    }
    for (j = 0; j < n2; j++) {
        R1[j] = frames[mid + 1 + j];
        R2[j] = pids[mid + 1 + j];
    }

    i = 0;
    j = 0;
    k = lo;
    while (i < n1 && j < n2) {
        if (L1[i] >= R1[j]) {
            frames[k] = L1[i];
            pids[k] = L2[i];
            i++;
        } else {
            frames[k] = R1[j];
            pids[k] = R2[j];
            j++;
        }
        k++;
    }

    while (i < n1) {
        frames[k] = L1[i];
        pids[k] = L2[i];
        i++;
        k++;
    }

    while (j < n2) {
        frames[k] = R1[j];
        pids[k] = R2[j];
        j++;
        k++;
    }
}

void mergeSort(int frames[], int pids[], int lo, int hi) {
    if (lo < hi) {
        int mid = lo + (hi - lo) / 2;

        mergeSort(frames, pids, lo, mid);
        mergeSort(frames, pids, mid + 1, hi);

        merge(frames, pids, lo, mid, hi);
    }
}

// This system call is used to find which process owns each frame of physical memory.
int
dump_physmem(int *_frames, int *_pids, int _numframes)
{
    if (_numframes < 0 || _frames == 0 || _pids == 0) {
        return -1;
    }
    mergeSort(track.frames, track.pids, 0, track.index-1);

    for (int i = 0; i < _numframes; ++i) {
        _frames[i] = track.frames[i];
        _pids[i] = track.pids[i];
    }
    return 0;
}