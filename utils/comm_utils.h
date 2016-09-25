#ifndef _COMM_UTILS_H_
#define _COMM_UTILS_H_

#define BUFFER_CHUNK_SIZE 1024*1024*2

typedef struct BufferListNode {
  char data[BUFFER_CHUNK_SIZE];
  int size;
  struct BufferListNode *next;
} BufferListNode;

typedef struct BufferList {
  BufferListNode *head;
  BufferListNode *tail;
  int read_pos;
  BufferListNode *write_node;
} BufferList;

BufferList *AllocBufferList(int n);

void FreeBufferList(BufferList *blist);
char *BufferListGetData(BufferList *blist, int *len);
char *BufferListGetSpace(BufferList *blist, int *len);
void BufferListPop(BufferList *blist, int len);
void BufferListPush(BufferList *blist, int len);

void Daemonize();

#endif /* _COMM_UTILS_H_ */
