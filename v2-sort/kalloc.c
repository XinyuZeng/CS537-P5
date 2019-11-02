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

void merge(int arr[], int arr2[], int l, int m, int r)
{
    int i, j, k;
    int n1 = m - l + 1;
    int n2 =  r - m;

    /* create temp arrays */
    int L[n1], R[n2];
    int L2[n1], R2[n2];

    /* Copy data to temp arrays L[] and R[] */
    for (i = 0; i < n1; i++) {
        L[i] = arr[l + i];
        L2[i] = arr2[l + i];
    }
    for (j = 0; j < n2; j++) {
        R[j] = arr[m + 1 + j];
        R2[j] = arr2[m + 1 + j];
    }

    /* Merge the temp arrays back into arr[l..r]*/
    i = 0; // Initial index of first subarray
    j = 0; // Initial index of second subarray
    k = l; // Initial index of merged subarray
    while (i < n1 && j < n2)
    {
        if (L[i] >= R[j])
        {
            arr[k] = L[i];
            arr2[k] = L2[i];
            i++;
        }
        else
        {
            arr[k] = R[j];
            arr2[k] = R2[j];
            j++;
        }
        k++;
    }

    /* Copy the remaining elements of L[], if there
       are any */
    while (i < n1)
    {
        arr[k] = L[i];
        arr2[k] = L2[i];
        i++;
        k++;
    }

    /* Copy the remaining elements of R[], if there
       are any */
    while (j < n2)
    {
        arr[k] = R[j];
        arr2[k] = R2[j];
        j++;
        k++;
    }
}

/* l is for left index and r is right index of the
   sub-array of arr to be sorted */
void mergeSort(int arr[], int arr2[], int l, int r)
{
    if (l < r)
    {
        // Same as (l+r)/2, but avoids overflow for
        // large l and h
        int m = l+(r-l)/2;

        // Sort first and second halves
        mergeSort(arr, arr2, l, m);
        mergeSort(arr, arr2, m+1, r);

        merge(arr, arr2, l, m, r);
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