//#define HAVE_EPOLL
#define HAVE_SELECT
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include <time.h>

#include "ae.h"

/* Include the best multiplexing layer supported by this system.
* The following should be ordered by performances, descending. */
#ifdef HAVE_EVPORT
#include "ae_evport.c"
#else
#ifdef HAVE_EPOLL
#include "ae_epoll.c"
#else
#ifdef HAVE_SELECT
#include "ae_select.c"
#else
#include "ae_kqueue.c"
#endif
#endif
#endif

/*
* 初始化事件处理器状态
* 
* setsize指的是放到eventloop中最大描述符大小，也就是该事件循环中可能有多少个fd。
在aeEventLoop结构中有两个数组（其实就是服务器程序惯用提前分配好内存然后用index映射到相应位置的做法），
这两个数组的大小就是这里的参数值setsize。 ae会创建一个 setSize*sizeof(aeFileEvent) 以及一个 
setSize*siezeof(aeFiredEvent) 大小的内存，用文件描述符作为其索引，这可以达到0(1)的速度找到事件数据所在位置。
那么这个大小定位多少合适呢？在Linux个中，文件描述符是个有限的资源，当打开一个文件时就会消耗一个文件描述符，
当关闭该文件描述符或者程序结束时会释放该文件描述符资源，从而供其他文件打开操作使用。当文件描述符超过最大值后，
打开文件就会出错。那么这个最大值是多少呢？可以通过/proc/sys/fs/file-max看到系统支持的最大的文件描述符数。
通过 ulimit -n 可以看到当前用户能打开的最大的文件描述符。在我这里的一台8g内存的机器上，
系统支持最大的文件描述是365146。而在这台64bit的机器上 sizeof(aeFiredEvent) + sizeof(aeFileEvent) 大小为40byte。
按系统最大支持的文件描述符来算，固定消耗内存为14.6M【((365146*40)/(1024*1024))】。这样以文件描述符作为数组的
下标来索引，虽然这样的哈希在接入量不大的情况下会有大量的浪费。但是最多也就浪费14M的内存，因此这样的设计是可取的。
*/
aeEventLoop *aeCreateEventLoop(int setsize) {
    aeEventLoop *eventLoop;
    int i;

    // 创建事件状态结构
    if ((eventLoop = malloc(sizeof(*eventLoop))) == NULL) 
        goto err;
    // 初始化文件事件结构和已就绪文件事件结构
    eventLoop->events   = malloc(sizeof(aeFileEvent) *setsize);
    eventLoop->fired    = malloc(sizeof(aeFiredEvent)*setsize);
    if (eventLoop->events == NULL || eventLoop->fired == NULL) 
        goto err;
    eventLoop->setsize  = setsize;
    eventLoop->lastTime = time(NULL);
    // 初始化时间事件结构
    eventLoop->timeEventHead   = NULL;
    eventLoop->timeEventNextId = 0;

    eventLoop->stop         = 0;
    eventLoop->maxfd        = -1;
    eventLoop->beforesleep  = NULL;


    //aeApiCreate()为eventLoop创建epoll句柄并开辟epoll_event事件数组。
    //aeApiCreate()会根据情况选择 ae_epoll.c, ae_select.c, ae_kqueue.c中的API函数。
    if (aeApiCreate(eventLoop) == -1) 
        goto err;
    //各个事件的监听标识mask设置为null。
    for (i = 0; i < setsize; i++)
        eventLoop->events[i].mask = AE_NONE;
    return eventLoop;

err:
    if (eventLoop) {
        free(eventLoop->events);
        free(eventLoop->fired);
        free(eventLoop);
    }
    return NULL;
}

/* Return the current set size. */
int aeGetSetSize(aeEventLoop *eventLoop) {
    return eventLoop->setsize;
}

/* Resize the maximum set size of the event loop.
 * If the requested set size is smaller than the current set size, but
 * there is already a file descriptor in use that is >= the requested
 * set size minus one, AE_ERR is returned and the operation is not
 * performed at all.
 *
 * Otherwise AE_OK is returned and the operation is successful. */
int aeResizeSetSize(aeEventLoop *eventLoop, int setsize) {
    int i;

    if (setsize == eventLoop->setsize) return AE_OK;
    if (eventLoop->maxfd >= setsize) return AE_ERR;
    if (aeApiResize(eventLoop,setsize) == -1) return AE_ERR;

    eventLoop->events = realloc(eventLoop->events,sizeof(aeFileEvent)*setsize);
    eventLoop->fired  = realloc(eventLoop->fired,sizeof(aeFiredEvent)*setsize);
    eventLoop->setsize = setsize;

    /* Make sure that if we created new slots, they are initialized with
     * an AE_NONE mask. */
    for (i = eventLoop->maxfd+1; i < setsize; i++)
        eventLoop->events[i].mask = AE_NONE;
    return AE_OK;
}



/*
* 删除事件处理器
*/
void aeDeleteEventLoop(aeEventLoop *eventLoop) {
    aeApiFree(eventLoop);
    free(eventLoop->events);
    free(eventLoop->fired);
    free(eventLoop);
}

/*
* 停止事件处理器
*/
void aeStop(aeEventLoop *eventLoop) {
    eventLoop->stop = 1;    //stop为1表示停止
}

/*
* aeCreateFileEvent()为fd注册一个文件事件，使用epoll_ctl加入到全局的epoll fd 进行监控，之后再指定事件可读写处理函数。 
* 根据 mask 参数的值，监听 fd 文件的状态，当 fd 可用时，执行 proc 函数，给proc函数传递参数clientData
*/
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask, aeFileProc *proc, void *clientData)
{
    if (fd >= eventLoop->setsize) 
        return AE_ERR;
    aeFileEvent *fe = &eventLoop->events[fd];

    // 监听指定 fd
    if (aeApiAddEvent(eventLoop, fd, mask) == -1)
        return AE_ERR;

    //设置文件事件类型
    fe->mask |= mask;
    if (mask & AE_READABLE) 
        fe->rfileProc = proc;
    if (mask & AE_WRITABLE) 
        fe->wfileProc = proc;

    //设置函数参数指针
    fe->clientData = clientData;

    //如果有需要，更新事件处理器的最大 fd
    if (fd > eventLoop->maxfd)
        eventLoop->maxfd = fd;

    return AE_OK;
}

/*
* 根据mask修改（删除）某fd上的监听事件
* 
* 首先通过fd找到去掉aeFileEvent对象，然后获得已有的mask,对其进行减操作后，
* 构成fd上新的mask事件类型，接着通过修改epoll或者select中注册的IO事件来完成。
* 这里以epoll为例，会根据该文件描述符上是否还有待等待的事件类型分别调
* 用epoll_ctr的EPOLL_CTL_MOD或者EPOLL_CTL_DEL命令。
* */
void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask)
{
    if (fd >= eventLoop->setsize) return;
    aeFileEvent *fe = &eventLoop->events[fd];

    // 未设置监听的事件类型，直接返回
    if (fe->mask == AE_NONE) return;

    fe->mask = fe->mask & (~mask);
    if (fd == eventLoop->maxfd && fe->mask == AE_NONE) {
        /* Update the max fd */
        int j;

        for (j = eventLoop->maxfd-1; j >= 0; j--)
            if (eventLoop->events[j].mask != AE_NONE) break;
        eventLoop->maxfd = j;
    }

    //根据mask设置一下epoll
    aeApiDelEvent(eventLoop, fd, mask);
}

/*
* 获取给定 fd 正在监听的事件类型
*/
int aeGetFileEvents(aeEventLoop *eventLoop, int fd) {
    if (fd >= eventLoop->setsize) 
        return 0;
    aeFileEvent *fe = &eventLoop->events[fd];

    return fe->mask;
}

/*
* 取出当前时间的秒和毫秒，分别保存到 seconds 和 milliseconds 参数中
*/
static void aeGetTime(long *seconds, long *milliseconds)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    *seconds = tv.tv_sec;
    *milliseconds = tv.tv_usec/1000;
}

/*
* 为当前时间<秒，毫秒>加上 milliseconds 毫秒之后得到的新的<秒，毫秒>。
*/
static void aeAddMillisecondsToNow(long long milliseconds, long *sec, long *ms) {
    long cur_sec, cur_ms, when_sec, when_ms;

    // 获取当前时间
    aeGetTime(&cur_sec, &cur_ms);

    // 计算增加 milliseconds 之后的秒数和毫秒数
    when_sec = cur_sec + milliseconds/1000;
    when_ms  = cur_ms + milliseconds%1000;

    // 进位：
    // 如果 when_ms 大于等于 1000
    // 那么将 when_sec 增大一秒
    if (when_ms >= 1000) {
        when_sec ++;
        when_ms -= 1000;
    }
    *sec = when_sec;
    *ms  = when_ms;
}

/*
* 创建时间事件
*/
//ae的定时器是用一个单链表来管理的，将定时器依次从head插入到单链表中。
//插入的过程中会取得未来的墙上时间作为其超时的时刻。即将当前时间加上添加定时器时给定的延迟时间。
long long aeCreateTimeEvent(aeEventLoop *eventLoop, 
                            long long milliseconds,
                            aeTimeProc *proc, 
                            void *clientData,
                            aeEventFinalizerProc *finalizerProc)
{
    //定时器事件的timeEventNextId自增。
    //timeEventNextId在处理执行定时器事件时会用到，用于防止出现死循环。
    //如果某定时器事件超过了最大id，则跳过这个定时事件，不执行。这样做为的是避免死循环，即：
    //如果事件一执行的时候注册了事件二，事件一执行完毕后事件二得到执行，紧接着如果事件一又得到执行就会成为循环，
    //因此维护了 timeEventNextId，使得本次处理中新增加的定时器事件不被执行。

    long long id = eventLoop->timeEventNextId++;
    aeTimeEvent *te;

    te = malloc(sizeof(*te));
    if (te == NULL) return AE_ERR;

    te->id = id;

    // 设定处理事件的时间
    aeAddMillisecondsToNow(milliseconds,&te->when_sec,&te->when_ms);
    te->timeProc = proc;
    te->finalizerProc = finalizerProc;
    te->clientData = clientData;

    // 将新事件放入表头
    te->next = eventLoop->timeEventHead;
    eventLoop->timeEventHead = te;

    return id;
}

/*
* 删除给定 id 的时间事件
* 直接遍历链表，找到定时器id匹配的项，使用单链表删除操作进行删除。这里再删除之前会调用定时器上的finalizerProc。
*/
int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id)
{
    aeTimeEvent *te, *prev = NULL;

    te = eventLoop->timeEventHead;
    while(te) {
        if (te->id == id) {

            if (prev == NULL)
                eventLoop->timeEventHead = te->next;
            else
                prev->next = te->next;

            if (te->finalizerProc)
                te->finalizerProc(eventLoop, te->clientData);

            free(te);

            return AE_OK;
        }
        prev = te;
        te = te->next;
    }

    return AE_ERR; /* NO event with the specified ID found */
}

/* Search the first timer to fire.
* This operation is useful to know how many time the select can be
* put in sleep without to delay any event.
* If there are no timers NULL is returned.
*
* Note that's O(N) since time events are unsorted.
* Possible optimizations (not needed by Redis so far, but...):
* 1) Insert the event in order, so that the nearest is just the head.
*    Much better but still insertion or deletion of timers is O(N).
* 2) Use a skiplist to have this operation as O(1) and insertion as O(log(N)).
*/
// 寻找里目前时间最近的时间事件
// 因为链表是乱序的，所以查找复杂度为 O（N）
static aeTimeEvent *aeSearchNearestTimer(aeEventLoop *eventLoop)
{
    aeTimeEvent *te = eventLoop->timeEventHead;
    aeTimeEvent *nearest = NULL;

    while(te) {
        if (!nearest || te->when_sec < nearest->when_sec ||
            (te->when_sec == nearest->when_sec &&
            te->when_ms < nearest->when_ms))
            nearest = te;
        te = te->next;
    }
    return nearest;
}

/* Process time events
*
* 处理所有已到达的时间事件
*/
static int processTimeEvents(aeEventLoop *eventLoop) {
    int processed = 0;
    aeTimeEvent *te;
    long long maxId;
    time_t now = time(NULL);

    //如果系统事件变更了，为保险起见，就将所有的定时器事件到达时间设为0，
    //0一定小于NOW，这使得所有时间事件在本次循环中超时并被执行。
    if (now < eventLoop->lastTime) {
        te = eventLoop->timeEventHead;
        while(te) {
            te->when_sec = 0;
            te = te->next;
        }
    }
    //更新最后一次处理时间事件的时间
    eventLoop->lastTime = now;

    //当一个定时器被处理的时候，可能会加入新的定时时间，比如在定时器处理函数中加入新的定时器。
    //而此时仅应该处理上一个时间段的状态，不应该在本次循环中去处理新的定时器。
    //因此ae在EventLoop中加入了一个timeEventNextId的成员表示此次循环中最大的定时器id+1，
    //这样在遍历定时器列表前，先保存最大的定时器maxid，然后遍历过程过滤掉新加入的定时器事件（大于maxid）。
    //
    //时间事件结构体中的timeEventNextId是递增的，只用于过滤新增加的时间时间，
    //当删除时间事件时并不更新（减小）它，所以控制逻辑比较简单。
    te      = eventLoop->timeEventHead;
    maxId   = eventLoop->timeEventNextId-1;
    while(te) {
        long now_sec, now_ms;
        long long id;

        // 跳过新增加事件
        if (te->id > maxId) {
            te = te->next;
            continue;
        }

        // 获取当前时间
        aeGetTime(&now_sec, &now_ms);

        //这里定时器的逻辑是若单链表中的定时器时间比当前时间早就执行定时器注册的回调函数。
        //如果该回调函数返回正值，那么就更新定时器时间为该值之后，从而可以循环执行定时器。
        //如果该回调函数返回AE_NOMORE，那么在执行完回调函数后注销该定时器。
        if (now_sec > te->when_sec || (now_sec == te->when_sec && now_ms >= te->when_ms))
        {
            int retval;

            id = te->id;
            retval = te->timeProc(eventLoop, id, te->clientData);
            processed++;
            /* 这里要注意了，如果定义的定时器回调函数timeProc返回值为正数，那么表示该定时器是一个循环定时器，
            即在第一次执行完后添加定时器事件时给定的延迟后不删除定时器，在延迟该返回值时间（单位是毫秒）后
            再次执行该定时器。所以就要注意，比如要每5秒执行一个操作，那么在添加定时器时要给定其定时时间为 
            5000毫秒，同时在该定时器的回调函数中也要返回5000. */

            // 记录是否有需要重复（循环）执行这个事件时间
            if (retval != AE_NOMORE) {
                // 是的， retval 毫秒之后继续执行这个时间事件
                aeAddMillisecondsToNow(retval,&te->when_sec,&te->when_ms);
            } else {
                // 不，将这个事件删除
                aeDeleteTimeEvent(eventLoop, id);
            }


            //处理完这个时间事件，照常理应该继续检索是否有到期的事件(到期时间相同)，但是因为是无序链表,
            //在该时间事件处理过程中，可能时间事件链表或发生改边（新增），所以重新从头开始检索。
            //这是ae网络库最大的问题。在这点可以做比较容易的优化，如进行有序时间链表，
            //虽然导致插入时间为O(n)；又比如设为跳表，可以减小插入时间为O(log(n))，但是付出更多的空间。
            te = eventLoop->timeEventHead;
        } else {
            te = te->next;
        }
    }
    return processed;
}

int aeProcessEvents(aeEventLoop *eventLoop, int flags)
{
    int processed = 0, numevents;

    //既不处理时间时间也不处理文件时间，直接返回0。
    if (!(flags & AE_TIME_EVENTS) && !(flags & AE_FILE_EVENTS)) return 0;

    //一、处理文件事件，使用epoll实现，注意超时时间设置（减少阻塞时间对时间事件的影响）
    //if条件:
    //（1）(maxfd!=-1)意味着有文件事件在监听
    //（2）((flags & AE_TIME_EVENTS) && !(flags & AE_DONT_WAIT))：处理时间事件且没有设置AE_DONT_WAIT（不阻塞）
    //      标识。若设置了AE_DONT_WAIT意味着所有的epoll(select)都采用立即返回（不阻塞）方式，这样每次epoll都
    //      能立即返回，不会发生因为长时间epoll阻塞造成了时间事件处理不及时的情况。所以，若设置了AE_DONT_WAIT，
    //      也便不再需要计算最近到达的时间事件距现在的时间差，来设置epoll的超时。
    //首先判断是否有定时器事件，如果有那么就去取得最近的一个将超时定时器的时间减去当前时间作为epoll或者
    //select等待文件事件的超时时间，该寻找过程就是通过遍历时间事件单链表得来的，这样指定超时时间，在有IO事
    //件pending时可以处理IO事件，若没有则可以保证在该超时时间到达时从epoll或者select中返回去处理定时器事件。
    if (eventLoop->maxfd != -1 || ((flags & AE_TIME_EVENTS) && !(flags & AE_DONT_WAIT))) {
            int j;
            aeTimeEvent *shortest = NULL;
            struct timeval tv, *tvp;

            //获取最近的时间事件shortest，其中记录了时间事件的到达时间。
            if (flags & AE_TIME_EVENTS && !(flags & AE_DONT_WAIT))
                shortest = aeSearchNearestTimer(eventLoop);
            
            //如果时间事件存在，计算最近可执行时间事件到达时间和现在的时间差，存储到timeval结构中，
            //作为epoll(select)监听文件时间的超时时间。
            if (shortest) {
                long now_sec, now_ms;
                //根据时间到达时间<e_sec,e_msec>和当前时间<n_sec,n_msec>求时间差，
                //得到最近的时间事件还要多久才能够到达，时间差保存在timeval结构体中。
                aeGetTime(&now_sec, &now_ms);
                tvp = &tv;
                tvp->tv_sec = shortest->when_sec - now_sec;                     //秒相减
                if (shortest->when_ms < now_ms) {
                    tvp->tv_usec = ((shortest->when_ms+1000) - now_ms)*1000;    //借位后，毫秒转微秒相减
                    tvp->tv_sec --;
                } else {
                    tvp->tv_usec = (shortest->when_ms - now_ms)*1000;           //毫秒转微秒相减，
                }

                // 时间差小于 0 ，说明最近时间事件已经到达，可以立即执行，所以epoll(select)应该设超时为0，
                // 这样可以立即返回处理时间事件。（不阻塞--立即返回）
                if (tvp->tv_sec < 0) 
                    tvp->tv_sec = 0;
                if (tvp->tv_usec < 0) 
                    tvp->tv_usec = 0;
            } else {
                // 执行到这一步，说明没有时间事件，或者本身设置了AE_DONT_WAIT（不阻塞）标识。
                // 那么根据 AE_DONT_WAIT是否设置来决定是否阻塞，以及阻塞的时间长度。
                /* If we have to check for events but need to return ASAP because of 
                 * AE_DONT_WAIT we need to se the timeout to zero */
                if (flags & AE_DONT_WAIT) {         // 设置文件事件不阻塞（超时为0--立即返回）
                    tv.tv_sec = tv.tv_usec = 0;
                    tvp = &tv;
                } else {                            // 文件事件可以阻塞直到有事件到达为止（超时为NULL--直到发生文件时间）    
                    tvp = NULL; /* wait forever */
                }
            }

            //调用不同的网络模型poll事件,处理redis文件事件
            //阻塞时间由 tvp 决定，tvp为NULL为一直阻塞，tvp为<0,0>位不阻塞立即返回。
            numevents = aeApiPoll(eventLoop, tvp);
            for (j = 0; j < numevents; j++) {
                // 从已就绪数组fired中获取发生事件
                aeFileEvent *fe = &eventLoop->events[eventLoop->fired[j].fd];

                int mask    = eventLoop->fired[j].mask;
                int fd      = eventLoop->fired[j].fd;
                int rfired  = 0;

                /* note the fe->mask & mask & ... code: maybe an already processed
                * event removed an element that fired and we still didn't
                * processed, so we check if the event is still valid. */
                if (fe->mask & mask & AE_READABLE) {    // 读事件
                    rfired = 1;                         // 确保读/写事件只能执行其中一个
                    fe->rfileProc(eventLoop,fd,fe->clientData,mask);
                }
                if (fe->mask & mask & AE_WRITABLE) {    // 写事件
                    if (!rfired || fe->wfileProc != fe->rfileProc)
                        fe->wfileProc(eventLoop,fd,fe->clientData,mask);
                }
                processed++;
            }
    }
    //二、执行时间事件
    if (flags & AE_TIME_EVENTS)
        processed += processTimeEvents(eventLoop);

    return processed; /* return the number of processed file/time events */
}

/* Wait for millseconds until the given file descriptor becomes
* writable/readable/exception */
int aeWait(int fd, int mask, long long milliseconds) {
    struct pollfd pfd;
    int retmask = 0, retval;

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    if (mask & AE_READABLE) pfd.events |= POLLIN;
    if (mask & AE_WRITABLE) pfd.events |= POLLOUT;

    if ((retval = poll(&pfd, 1, milliseconds))== 1) {
        if (pfd.revents & POLLIN) retmask |= AE_READABLE;
        if (pfd.revents & POLLOUT) retmask |= AE_WRITABLE;
        if (pfd.revents & POLLERR) retmask |= AE_WRITABLE;
        if (pfd.revents & POLLHUP) retmask |= AE_WRITABLE;
        return retmask;
    } else {
        return retval;
    }
}

// 事件处理器的主循环
// 
void aeMain(aeEventLoop *eventLoop) {

    eventLoop->stop = 0;

    while (!eventLoop->stop) {

        // 如果有需要在事件处理前执行的函数，那么运行它
        if (eventLoop->beforesleep != NULL)
            eventLoop->beforesleep(eventLoop);
        // 开始处理事件
        aeProcessEvents(eventLoop, AE_ALL_EVENTS);
    }
}

// 返回所使用的多路复用库的名称
char *aeGetApiName(void) {
    return aeApiName();
}

// 设置处理事件前需要被执行的函数
// 
void aeSetBeforeSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *beforesleep) {
    eventLoop->beforesleep = beforesleep;
}
