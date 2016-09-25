#ifndef __AE_H__
#define __AE_H__

#include <time.h>
#include <stdlib.h>

/*
* 事件执行状态
*/
#define AE_OK 0     // 成功
#define AE_ERR -1   // 出错

/*
* 文件事件状态
*/
#define AE_NONE 0       // 未设置
#define AE_READABLE 1   // 可读
#define AE_WRITABLE 2   // 可写

/*
* 时间处理器的执行 flags
*/
#define AE_FILE_EVENTS 1
#define AE_TIME_EVENTS 2
#define AE_ALL_EVENTS (AE_FILE_EVENTS|AE_TIME_EVENTS)
#define AE_DONT_WAIT 4

/*
* 决定时间事件是否要持续执行的 flag
*/
#define AE_NOMORE -1

/* Macros */
#define AE_NOTUSED(V) ((void) V)

/*
* 事件处理器状态
*/
struct aeEventLoop;

/* Types and data structures */
typedef void aeFileProc(struct aeEventLoop *eventLoop, int fd, void *clientData, int mask);
typedef int aeTimeProc(struct aeEventLoop *eventLoop, long long id, void *clientData);
typedef void aeEventFinalizerProc(struct aeEventLoop *eventLoop, void *clientData);
typedef void aeBeforeSleepProc(struct aeEventLoop *eventLoop);


/*  aeFileEvent 是文件事件结构体*/
typedef struct aeFileEvent {
    int mask;               // 要捕获的事件的掩码（读|写），值可以是AE_READABLE或AE_WRITABLE,或者两者的或*/
    aeFileProc *rfileProc;  // 写事件函数
    aeFileProc *wfileProc;  // 读事件函数
    void *clientData;       // clientData是函数用的参数
} aeFileEvent;

/* Time event structure 时间事件结构体*/
typedef struct aeTimeEvent {
    long long id;                           // 时间事件的唯一标识符  time event identifier. 
    
    long when_sec;                          // 事件的到达时间 秒
    long when_ms;                           // 事件的到达时间 微秒
    
    aeTimeProc *timeProc;                   // 时间事件处理函数
    aeEventFinalizerProc *finalizerProc;    // 定时事件清理函数，当删除定时事件的时候会被调用
    void *clientData;                       // 多路复用库的私有数据
    struct aeTimeEvent *next;               // 指向下个时间事件结构，形成的无序链表

} aeTimeEvent;

/* A fired event (epollwait)已就绪事件 */
typedef struct aeFiredEvent {
    int fd;     // 已就绪文件描述符
    int mask;   // 事件类型掩码，可以是 AE_READABLE 或 AE_WRITABLE
} aeFiredEvent;

/* State of an event based program 事件处理器的状态 */
typedef struct aeEventLoop {

    int maxfd;                      //记录已注册的最大文件描述符,初始化为-1，
    int setsize;                    //目前已追踪的最大描述符      
    long long timeEventNextId;      //定时器事件的ID编号管理，用于生成下一个时间事件的timer_id
    time_t lastTime;                //最后一次执行时间事件的时间，用来诊断系统时间偏差，以矫正系统时间的

    aeFileEvent *events;            //用于保存epoll需要关注的文件事件的fd、触发条件、注册函数 
    aeFiredEvent *fired;            //已触发的文件事件，poll_wait之后获得可读或者可写的fd数组，通过aeFiredEvent->fd再定位到events

    aeTimeEvent *timeEventHead;     //定时器事件，以链表形式保存多个时间事件，每隔一段时间就会触发注册的函数（复杂度O(n),故不宜过多）

    int stop;                       //事件循环结束标识,1表示停止(时间轮询是否结束?)

                                    //这个是处理底层特定API的数据，对于epoll来说，该结构体包含了epoll fd和epoll_event
    void *apidata;                  //多路复用库的私有数据---文件事件的轮询数据和结果数据：poll； 三种轮询方式：epoll（linux），select（windows），kqueue  
                                    //对于不同的 I/O 多路复用技术，有不同的数据，详见各自实现。
                                    
    aeBeforeSleepProc *beforesleep; //在每轮事件循环阻塞前执行这个callback。

} aeEventLoop;


/* Prototypes */
aeEventLoop *aeCreateEventLoop(int setsize);
void aeDeleteEventLoop(aeEventLoop *eventLoop);
void aeStop(aeEventLoop *eventLoop);
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,aeFileProc *proc, void *clientData);
void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask);
int aeGetFileEvents(aeEventLoop *eventLoop, int fd);
long long aeCreateTimeEvent(aeEventLoop *eventLoop, 
                            long long milliseconds,
                            aeTimeProc *proc, 
                            void *clientData,
                            aeEventFinalizerProc *finalizerProc);
int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id);
int aeProcessEvents(aeEventLoop *eventLoop, int flags);
int aeWait(int fd, int mask, long long milliseconds);
void aeMain(aeEventLoop *eventLoop);
char *aeGetApiName(void);
void aeSetBeforeSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *beforesleep);
int aeGetSetSize(aeEventLoop *eventLoop);
int aeResizeSetSize(aeEventLoop *eventLoop, int setsize);

#endif
