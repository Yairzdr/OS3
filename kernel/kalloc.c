// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define NUM_PYS_PAGES ((PHYSTOP-KERNBASE) / PGSIZE)

int pageReferences[NUM_PYS_PAGES];

void freerange(void *pa_start, void *pa_end);

extern uint64 cas(volatile void* addr, int expected, int newval);
extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  memset(pageReferences, 0, sizeof(int)*NUM_PYS_PAGES); //allocate memory for pageReferences array
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
  {
    pageReferences[((uint64)p - KERNBASE)/PGSIZE] = 1;//initiated with 1 because in kfree the reference count is decreased by 1.
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  r = (struct run*)pa;

  //reduce counter by one. If bigger than 0, return. else, continue with original code to free page
  int indexInList = ((uint64)r - KERNBASE)/ PGSIZE;

  int currentCounter;
  int destCounter;
  do {
    currentCounter = pageReferences[indexInList];
    destCounter = currentCounter - 1;
  }
  while(cas(&(pageReferences[indexInList]), currentCounter, destCounter));
  if (destCounter > 0)
  {
    return;
  }
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);


  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void) //create new PA, set page reference in list to 1
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
  {
    int indexInList = ((uint64)r - KERNBASE)/ PGSIZE;
    pageReferences[indexInList] = 1;
    kmem.freelist = r->next;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

void increaseCounter(uint64 pa)
{ 
  //increaseCounter for references for pa
  int indexInList = (pa - KERNBASE)/ PGSIZE;

  int currentCounter;
  int destCounter;
  do {
    currentCounter = pageReferences[indexInList];
    destCounter = currentCounter + 1;
  }
  while(cas(&(pageReferences[indexInList]), currentCounter, destCounter));
  return;
}