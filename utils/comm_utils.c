#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "comm_utils.h"
#include "log_util.h"

void Daemonize() {
  int fd;

  if (fork() != 0) 
    exit(0);  /* parent exits */

  setsid();   /* create a new session */

  if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO) close(fd);
  }
}


//开辟n个BufferListNode节点的链表blist。
BufferList *AllocBufferList(int n) {
  BufferList *blist   = malloc(sizeof(BufferList));
  BufferListNode *buf = malloc(sizeof(BufferListNode));
  BufferListNode *pre;
  

  buf->size = 0;
  buf->next = NULL;
  blist->head = buf;

  //初始写节点write_node为链表的第一个节点。
  pre = blist->head = blist->write_node = buf;
  
  int i;
  for (i = 1; i < n; i++) {
    buf = malloc(sizeof(BufferListNode));
    buf->size = 0;
    buf->next = NULL;
    pre->next = buf;
    pre = buf;
  }

  blist->tail = buf;
  blist->read_pos = 0;

  return blist;
}

void FreeBufferList(BufferList *blist) {
  BufferListNode *cur = blist->head;
  while (cur != NULL) {
    blist->head = cur->next;
    free(cur);
    cur = blist->head;
  }
  free(blist);
}

// get free space from current write node
// len作为传出参数使用，记录了writeNode的剩余可写空间大小。
// 函数返回值为writeNode的可写空间起始地址。
char *BufferListGetSpace(BufferList *blist, int *len) {
  //写指针指向最后一个节点，且最后节点的data[]已经写满，则无空间可写，返回NULL。
  if (blist->write_node == blist->tail && blist->write_node->size == BUFFER_CHUNK_SIZE) {
    *len = 0;
    LogDebug("tail full");
    return NULL;
  }
  //否则计算写指针指向节点的剩余空间：BUFFER_CHUNK_SIZE（data全部空间）- write_node->size（已经写入的size）
  *len = BUFFER_CHUNK_SIZE - blist->write_node->size;
  //返回writeNode的可写空间的起始地址。
  return blist->write_node->data + blist->write_node->size;
}

// push data into buffer
void BufferListPush(BufferList *blist, int len) {
  blist->write_node->size += len;
  LogDebug("head %p tail %p cur %p data %d", blist->head, blist->tail, blist->write_node, blist->head->size - blist->read_pos);
  
  if (blist->write_node->size == BUFFER_CHUNK_SIZE && blist->write_node != blist->tail) {
    // write_node move to next chunk
    blist->write_node = blist->write_node->next;
  }
}

// always get data from head
// len作为传出参数，记录了当前能够从head节点读出的数据量
char *BufferListGetData(BufferList *blist, int *len) {
  //head==writeNode意味着只有一个有数据的节点，但read_pos == size意味着该节点已经没有数据可读
  if (blist->head == blist->write_node && blist->read_pos == blist->head->size) {
    *len = 0;
    LogDebug("head empty");
    return NULL;
  }
  *len = blist->head->size - blist->read_pos;
  return blist->head->data + blist->read_pos;
}

// pop data out from buffer
void BufferListPop(BufferList *blist, int len) {
  blist->read_pos += len;
  LogDebug("head %p tail %p cur %p data %d", blist->head, blist->tail, blist->write_node, blist->head->size - blist->read_pos);
  //head empty, and head is not the node we are writing into, move to tail
  //因为head节点已经没数据可读，需要继续读剩下的其它节点，所以把head节点清空后移动到链表尾部，
  //原第二节点（有数据的节点）变为新的head节点，read_pos归零，。
  if (blist->read_pos == blist->head->size && blist->head != blist->write_node) {
    
    BufferListNode *cur = blist->head;
    blist->head = blist->head->next;
    blist->tail->next = cur;
    blist->tail = cur;
    cur->size = 0;
    cur->next = NULL;
    blist->read_pos = 0;
    if (blist->head == NULL) {
      // there is only one chunk in buffer list
      LogDebug("head null");
      exit(0);
      blist->head = blist->tail;
    }
  }
  // else leave it there, further get data will return NULL
}
