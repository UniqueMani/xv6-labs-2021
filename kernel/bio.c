// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
#define NBUCKET 13

struct {
  struct spinlock lock[NBUCKET];   // 每个桶一个锁
  struct buf buf[NBUF];            // 缓冲区数组
  struct buf bucket[NBUCKET];
} bcache;

void
binit(void)
{
  struct buf *b;

  for (int i = 0; i < NBUCKET; i++) {
    initlock(&bcache.lock[i], "bcache.bucket");
    bcache.bucket[i].next = 0;
  }

  b = &bcache.buf[0];
  for (int i = 0; i < NBUF; i++, b++) {
    initsleeplock(&b->lock, "buffer");
    int bucket_id = i % NBUCKET;   // 初始分配
    b->next = bcache.bucket[bucket_id].next;
    bcache.bucket[bucket_id].next = b;
  }
}

int can_lock(int cur_idx, int req_idx)
{
  int mid = NBUCKET / 2;
  if (cur_idx == req_idx) return 0;
  else if (cur_idx < req_idx) {
    if (req_idx <= (cur_idx + mid)) return 0;
  } else {
    if (cur_idx >= (req_idx + mid)) return 0;
  }
  return 1;
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
  int bucket_id = blockno % NBUCKET;
  struct buf *b;

  acquire(&bcache.lock[bucket_id]);
  b = bcache.bucket[bucket_id].next;
  while (b)
  {
    if (b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++;
      release(&bcache.lock[bucket_id]);
      acquiresleep(&b->lock);
      return b;
    }
    b = b->next;
  }
  int index = -1;
  uint min_tick = 0xffffffff;
  for (int j = 0; j < NBUCKET; j++)
  {
    if (!can_lock(bucket_id, j))
    {
      continue;
    }
    else
    {
      acquire(&bcache.lock[j]);
    }
    b = bcache.bucket[j].next;
    while (b)
    {
      if (b->refcnt == 0)
      {
        if (b->time < min_tick)
        {
          min_tick = b->time;
          if (index != -1 && index != j && holding(&bcache.lock[index]))
          {
            release(&bcache.lock[index]);
          }
          index = j;
        }
      }
      b = b->next;
    }
    if (j != index && holding(&bcache.lock[j]))
    {
      release(&bcache.lock[j]);
    }
  }

  if (index == -1)
  {
    panic("bget: no buffers");
  }

  struct buf *move = 0;
  b = &bcache.bucket[index];
  while (b->next)
  {
    if (b->next->refcnt == 0 && b->next->time == min_tick)
    {
      b->next->dev = dev;
      b->next->blockno = blockno;
      b->next->valid = 0;
      b->next->refcnt = 1;
      // remove buf from the old bucket
      move = b->next;
      b->next = b->next->next;
      release(&bcache.lock[index]);
      break;
    }
    b = b->next;
  }
  b = &bcache.bucket[bucket_id];
  while (b->next)
  {
    b = b->next;
  }
  move->next = 0;
  b->next = move;
  release(&bcache.lock[bucket_id]);
  acquiresleep(&move->lock);
  return move;
}
// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int bucket_id = b->blockno % NBUCKET;
  acquire(&bcache.lock[bucket_id]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->time = ticks;
  }
  
  release(&bcache.lock[bucket_id]);
}

void bpin(struct buf *b)
{
  int bucket_id = b->blockno % NBUCKET;
  acquire(&bcache.lock[bucket_id]);
  b->refcnt++;
  release(&bcache.lock[bucket_id]);
}

void bunpin(struct buf *b)
{
  int bucket_id = b->blockno % NBUCKET;
  acquire(&bcache.lock[bucket_id]);
  b->refcnt--;
  release(&bcache.lock[bucket_id]);
}


