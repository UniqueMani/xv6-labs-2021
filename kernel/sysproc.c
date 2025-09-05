#include "types.h"
#include "param.h"
#include "date.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"
#include "fcntl.h"
#include "file.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

extern struct vma *vma_alloc();
void *sys_mmap(void)
{
  uint64 addr;
  struct proc *p = myproc();
  int length, prot, flags, fd, offset;

  if (argaddr(0, &addr) < 0)
    return (void *)-1;
  if (argint(1, &length) < 0)
    return (void *)-1;
  if (argint(2, &prot) < 0)
    return (void *)-1;
  if (argint(3, &flags) < 0)
    return (void *)-1;
  if (argint(4, &fd) < 0)
    return (void *)-1;
  if (argint(5, &offset) < 0)
    return (void *)-1;
  if (addr != 0)
    addr = 0;
  if (offset != 0)
    offset = 0;

  struct file *f = p->ofile[fd];

  // Check flags
  int pte_flag = PTE_U;
  if (prot & PROT_READ)
  {
    if (!f->readable)
      return (void *)-1;
    pte_flag |= PTE_R;
  }
  if (prot & PROT_WRITE)
  {
    if (!f->writable && !(flags & MAP_PRIVATE))
      return (void *)-1;
    pte_flag |= PTE_W;
  }

  // Setting up vma
  struct vma *v = vma_alloc();
  v->perm = pte_flag;
  v->length = length;
  v->off = offset;
  v->file = myproc()->ofile[fd];
  v->flags = flags;
  filedup(f);
  struct vma *pv = p->vma;
  if (pv == 0)
  {
    v->start = VMA_START;
    v->end = length + v->start;
    p->vma = v;
  }
  else
  {
    while (pv->next)
      pv = pv->next;
    v->start = PGROUNDUP(pv->end);
    v->end = v->start + length;
    pv->next = v;
    v->next = 0;
  }
  addr = v->start;
  release(&v->lock);
  return (void *)(addr);
}

int sys_munmap(void)
{
  uint64 addr;
  int length;
  if (argaddr(0, &addr) < 0)
    return -1;
  if (argint(1, &length) < 0)
    return -1;
  struct proc *p = myproc();
  struct vma *v = p->vma;
  struct vma *pre = 0;
  while (v != 0)
  {
    if (addr >= v->start && addr < v->end)
      break;
    pre = v;
    v = v->next;
  }
  // not mapped
  if (v == 0)
    return -1;
  if (addr != v->start && addr + length != v->end)
    panic("munmap: middle of vma");
  if (addr == v->start)
  {
    write_back(v, addr, length);
    uvmunmap(p->pagetable, addr, length / PGSIZE, 1);
    if (length == v->length)
    {
      // free all
      fileclose(v->file);
      if (pre == 0)
      {
        p->vma = v->next;
      }
      else
      {
        pre->next = v->next;
        v->next = 0;
      }
      acquire(&v->lock);
      v->length = 0;
      release(&v->lock);
    }
    else
    {
      // head
      v->start -= length;
      v->off += length;
      v->length -= length;
    }
  }
  else
  {
    // tail
    v->length -= length;
    v->end -= length;
  }
  return 0;
}
