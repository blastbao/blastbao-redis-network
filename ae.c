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
* ��ʼ���¼�������״̬
* 
* setsizeָ���Ƿŵ�eventloop�������������С��Ҳ���Ǹ��¼�ѭ���п����ж��ٸ�fd��
��aeEventLoop�ṹ�����������飨��ʵ���Ƿ��������������ǰ������ڴ�Ȼ����indexӳ�䵽��Ӧλ�õ���������
����������Ĵ�С��������Ĳ���ֵsetsize�� ae�ᴴ��һ�� setSize*sizeof(aeFileEvent) �Լ�һ�� 
setSize*siezeof(aeFiredEvent) ��С���ڴ棬���ļ���������Ϊ������������Դﵽ0(1)���ٶ��ҵ��¼���������λ�á�
��ô�����С��λ���ٺ����أ���Linux���У��ļ��������Ǹ����޵���Դ������һ���ļ�ʱ�ͻ�����һ���ļ���������
���رո��ļ����������߳������ʱ���ͷŸ��ļ���������Դ���Ӷ��������ļ��򿪲���ʹ�á����ļ��������������ֵ��
���ļ��ͻ������ô������ֵ�Ƕ����أ�����ͨ��/proc/sys/fs/file-max����ϵͳ֧�ֵ������ļ�����������
ͨ�� ulimit -n ���Կ�����ǰ�û��ܴ򿪵������ļ������������������һ̨8g�ڴ�Ļ����ϣ�
ϵͳ֧�������ļ�������365146��������̨64bit�Ļ����� sizeof(aeFiredEvent) + sizeof(aeFileEvent) ��СΪ40byte��
��ϵͳ���֧�ֵ��ļ����������㣬�̶������ڴ�Ϊ14.6M��((365146*40)/(1024*1024))�����������ļ���������Ϊ�����
�±�����������Ȼ�����Ĺ�ϣ�ڽ��������������»��д������˷ѡ��������Ҳ���˷�14M���ڴ棬�������������ǿ�ȡ�ġ�
*/
aeEventLoop *aeCreateEventLoop(int setsize) {
    aeEventLoop *eventLoop;
    int i;

    // �����¼�״̬�ṹ
    if ((eventLoop = malloc(sizeof(*eventLoop))) == NULL) 
        goto err;
    // ��ʼ���ļ��¼��ṹ���Ѿ����ļ��¼��ṹ
    eventLoop->events   = malloc(sizeof(aeFileEvent) *setsize);
    eventLoop->fired    = malloc(sizeof(aeFiredEvent)*setsize);
    if (eventLoop->events == NULL || eventLoop->fired == NULL) 
        goto err;
    eventLoop->setsize  = setsize;
    eventLoop->lastTime = time(NULL);
    // ��ʼ��ʱ���¼��ṹ
    eventLoop->timeEventHead   = NULL;
    eventLoop->timeEventNextId = 0;

    eventLoop->stop         = 0;
    eventLoop->maxfd        = -1;
    eventLoop->beforesleep  = NULL;


    //aeApiCreate()ΪeventLoop����epoll���������epoll_event�¼����顣
    //aeApiCreate()��������ѡ�� ae_epoll.c, ae_select.c, ae_kqueue.c�е�API������
    if (aeApiCreate(eventLoop) == -1) 
        goto err;
    //�����¼��ļ�����ʶmask����Ϊnull��
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
* ɾ���¼�������
*/
void aeDeleteEventLoop(aeEventLoop *eventLoop) {
    aeApiFree(eventLoop);
    free(eventLoop->events);
    free(eventLoop->fired);
    free(eventLoop);
}

/*
* ֹͣ�¼�������
*/
void aeStop(aeEventLoop *eventLoop) {
    eventLoop->stop = 1;    //stopΪ1��ʾֹͣ
}

/*
* aeCreateFileEvent()Ϊfdע��һ���ļ��¼���ʹ��epoll_ctl���뵽ȫ�ֵ�epoll fd ���м�أ�֮����ָ���¼��ɶ�д�������� 
* ���� mask ������ֵ������ fd �ļ���״̬���� fd ����ʱ��ִ�� proc ��������proc�������ݲ���clientData
*/
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask, aeFileProc *proc, void *clientData)
{
    if (fd >= eventLoop->setsize) 
        return AE_ERR;
    aeFileEvent *fe = &eventLoop->events[fd];

    // ����ָ�� fd
    if (aeApiAddEvent(eventLoop, fd, mask) == -1)
        return AE_ERR;

    //�����ļ��¼�����
    fe->mask |= mask;
    if (mask & AE_READABLE) 
        fe->rfileProc = proc;
    if (mask & AE_WRITABLE) 
        fe->wfileProc = proc;

    //���ú�������ָ��
    fe->clientData = clientData;

    //�������Ҫ�������¼������������ fd
    if (fd > eventLoop->maxfd)
        eventLoop->maxfd = fd;

    return AE_OK;
}

/*
* ����mask�޸ģ�ɾ����ĳfd�ϵļ����¼�
* 
* ����ͨ��fd�ҵ�ȥ��aeFileEvent����Ȼ�������е�mask,������м�������
* ����fd���µ�mask�¼����ͣ�����ͨ���޸�epoll����select��ע���IO�¼�����ɡ�
* ������epollΪ��������ݸ��ļ����������Ƿ��д��ȴ����¼����ͷֱ��
* ��epoll_ctr��EPOLL_CTL_MOD����EPOLL_CTL_DEL���
* */
void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask)
{
    if (fd >= eventLoop->setsize) return;
    aeFileEvent *fe = &eventLoop->events[fd];

    // δ���ü������¼����ͣ�ֱ�ӷ���
    if (fe->mask == AE_NONE) return;

    fe->mask = fe->mask & (~mask);
    if (fd == eventLoop->maxfd && fe->mask == AE_NONE) {
        /* Update the max fd */
        int j;

        for (j = eventLoop->maxfd-1; j >= 0; j--)
            if (eventLoop->events[j].mask != AE_NONE) break;
        eventLoop->maxfd = j;
    }

    //����mask����һ��epoll
    aeApiDelEvent(eventLoop, fd, mask);
}

/*
* ��ȡ���� fd ���ڼ������¼�����
*/
int aeGetFileEvents(aeEventLoop *eventLoop, int fd) {
    if (fd >= eventLoop->setsize) 
        return 0;
    aeFileEvent *fe = &eventLoop->events[fd];

    return fe->mask;
}

/*
* ȡ����ǰʱ�����ͺ��룬�ֱ𱣴浽 seconds �� milliseconds ������
*/
static void aeGetTime(long *seconds, long *milliseconds)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    *seconds = tv.tv_sec;
    *milliseconds = tv.tv_usec/1000;
}

/*
* Ϊ��ǰʱ��<�룬����>���� milliseconds ����֮��õ����µ�<�룬����>��
*/
static void aeAddMillisecondsToNow(long long milliseconds, long *sec, long *ms) {
    long cur_sec, cur_ms, when_sec, when_ms;

    // ��ȡ��ǰʱ��
    aeGetTime(&cur_sec, &cur_ms);

    // �������� milliseconds ֮��������ͺ�����
    when_sec = cur_sec + milliseconds/1000;
    when_ms  = cur_ms + milliseconds%1000;

    // ��λ��
    // ��� when_ms ���ڵ��� 1000
    // ��ô�� when_sec ����һ��
    if (when_ms >= 1000) {
        when_sec ++;
        when_ms -= 1000;
    }
    *sec = when_sec;
    *ms  = when_ms;
}

/*
* ����ʱ���¼�
*/
//ae�Ķ�ʱ������һ��������������ģ�����ʱ�����δ�head���뵽�������С�
//����Ĺ����л�ȡ��δ����ǽ��ʱ����Ϊ�䳬ʱ��ʱ�̡�������ǰʱ�������Ӷ�ʱ��ʱ�������ӳ�ʱ�䡣
long long aeCreateTimeEvent(aeEventLoop *eventLoop, 
                            long long milliseconds,
                            aeTimeProc *proc, 
                            void *clientData,
                            aeEventFinalizerProc *finalizerProc)
{
    //��ʱ���¼���timeEventNextId������
    //timeEventNextId�ڴ���ִ�ж�ʱ���¼�ʱ���õ������ڷ�ֹ������ѭ����
    //���ĳ��ʱ���¼����������id�������������ʱ�¼�����ִ�С�������Ϊ���Ǳ�����ѭ��������
    //����¼�һִ�е�ʱ��ע�����¼������¼�һִ����Ϻ��¼����õ�ִ�У�����������¼�һ�ֵõ�ִ�оͻ��Ϊѭ����
    //���ά���� timeEventNextId��ʹ�ñ��δ����������ӵĶ�ʱ���¼�����ִ�С�

    long long id = eventLoop->timeEventNextId++;
    aeTimeEvent *te;

    te = malloc(sizeof(*te));
    if (te == NULL) return AE_ERR;

    te->id = id;

    // �趨�����¼���ʱ��
    aeAddMillisecondsToNow(milliseconds,&te->when_sec,&te->when_ms);
    te->timeProc = proc;
    te->finalizerProc = finalizerProc;
    te->clientData = clientData;

    // �����¼������ͷ
    te->next = eventLoop->timeEventHead;
    eventLoop->timeEventHead = te;

    return id;
}

/*
* ɾ������ id ��ʱ���¼�
* ֱ�ӱ��������ҵ���ʱ��idƥ����ʹ�õ�����ɾ����������ɾ����������ɾ��֮ǰ����ö�ʱ���ϵ�finalizerProc��
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
// Ѱ����Ŀǰʱ�������ʱ���¼�
// ��Ϊ����������ģ����Բ��Ҹ��Ӷ�Ϊ O��N��
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
* ���������ѵ����ʱ���¼�
*/
static int processTimeEvents(aeEventLoop *eventLoop) {
    int processed = 0;
    aeTimeEvent *te;
    long long maxId;
    time_t now = time(NULL);

    //���ϵͳ�¼�����ˣ�Ϊ����������ͽ����еĶ�ʱ���¼�����ʱ����Ϊ0��
    //0һ��С��NOW����ʹ������ʱ���¼��ڱ���ѭ���г�ʱ����ִ�С�
    if (now < eventLoop->lastTime) {
        te = eventLoop->timeEventHead;
        while(te) {
            te->when_sec = 0;
            te = te->next;
        }
    }
    //�������һ�δ���ʱ���¼���ʱ��
    eventLoop->lastTime = now;

    //��һ����ʱ���������ʱ�򣬿��ܻ�����µĶ�ʱʱ�䣬�����ڶ�ʱ���������м����µĶ�ʱ����
    //����ʱ��Ӧ�ô�����һ��ʱ��ε�״̬����Ӧ���ڱ���ѭ����ȥ�����µĶ�ʱ����
    //���ae��EventLoop�м�����һ��timeEventNextId�ĳ�Ա��ʾ�˴�ѭ�������Ķ�ʱ��id+1��
    //�����ڱ�����ʱ���б�ǰ���ȱ������Ķ�ʱ��maxid��Ȼ��������̹��˵��¼���Ķ�ʱ���¼�������maxid����
    //
    //ʱ���¼��ṹ���е�timeEventNextId�ǵ����ģ�ֻ���ڹ��������ӵ�ʱ��ʱ�䣬
    //��ɾ��ʱ���¼�ʱ�������£���С���������Կ����߼��Ƚϼ򵥡�
    te      = eventLoop->timeEventHead;
    maxId   = eventLoop->timeEventNextId-1;
    while(te) {
        long now_sec, now_ms;
        long long id;

        // �����������¼�
        if (te->id > maxId) {
            te = te->next;
            continue;
        }

        // ��ȡ��ǰʱ��
        aeGetTime(&now_sec, &now_ms);

        //���ﶨʱ�����߼������������еĶ�ʱ��ʱ��ȵ�ǰʱ�����ִ�ж�ʱ��ע��Ļص�������
        //����ûص�����������ֵ����ô�͸��¶�ʱ��ʱ��Ϊ��ֵ֮�󣬴Ӷ�����ѭ��ִ�ж�ʱ����
        //����ûص���������AE_NOMORE����ô��ִ����ص�������ע���ö�ʱ����
        if (now_sec > te->when_sec || (now_sec == te->when_sec && now_ms >= te->when_ms))
        {
            int retval;

            id = te->id;
            retval = te->timeProc(eventLoop, id, te->clientData);
            processed++;
            /* ����Ҫע���ˣ��������Ķ�ʱ���ص�����timeProc����ֵΪ��������ô��ʾ�ö�ʱ����һ��ѭ����ʱ����
            ���ڵ�һ��ִ�������Ӷ�ʱ���¼�ʱ�������ӳٺ�ɾ����ʱ�������ӳٸ÷���ֵʱ�䣨��λ�Ǻ��룩��
            �ٴ�ִ�иö�ʱ�������Ծ�Ҫע�⣬����Ҫÿ5��ִ��һ����������ô����Ӷ�ʱ��ʱҪ�����䶨ʱʱ��Ϊ 
            5000���룬ͬʱ�ڸö�ʱ���Ļص�������ҲҪ����5000. */

            // ��¼�Ƿ�����Ҫ�ظ���ѭ����ִ������¼�ʱ��
            if (retval != AE_NOMORE) {
                // �ǵģ� retval ����֮�����ִ�����ʱ���¼�
                aeAddMillisecondsToNow(retval,&te->when_sec,&te->when_ms);
            } else {
                // ����������¼�ɾ��
                aeDeleteTimeEvent(eventLoop, id);
            }


            //���������ʱ���¼����ճ���Ӧ�ü��������Ƿ��е��ڵ��¼�(����ʱ����ͬ)��������Ϊ����������,
            //�ڸ�ʱ���¼���������У�����ʱ���¼���������ıߣ����������������´�ͷ��ʼ������
            //����ae������������⡣�����������Ƚ����׵��Ż������������ʱ������
            //��Ȼ���²���ʱ��ΪO(n)���ֱ�����Ϊ�������Լ�С����ʱ��ΪO(log(n))�����Ǹ�������Ŀռ䡣
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

    //�Ȳ�����ʱ��ʱ��Ҳ�������ļ�ʱ�䣬ֱ�ӷ���0��
    if (!(flags & AE_TIME_EVENTS) && !(flags & AE_FILE_EVENTS)) return 0;

    //һ�������ļ��¼���ʹ��epollʵ�֣�ע�ⳬʱʱ�����ã���������ʱ���ʱ���¼���Ӱ�죩
    //if����:
    //��1��(maxfd!=-1)��ζ�����ļ��¼��ڼ���
    //��2��((flags & AE_TIME_EVENTS) && !(flags & AE_DONT_WAIT))������ʱ���¼���û������AE_DONT_WAIT����������
    //      ��ʶ����������AE_DONT_WAIT��ζ�����е�epoll(select)�������������أ�����������ʽ������ÿ��epoll��
    //      ���������أ����ᷢ����Ϊ��ʱ��epoll���������ʱ���¼�������ʱ����������ԣ���������AE_DONT_WAIT��
    //      Ҳ�㲻����Ҫ������������ʱ���¼������ڵ�ʱ��������epoll�ĳ�ʱ��
    //�����ж��Ƿ��ж�ʱ���¼����������ô��ȥȡ�������һ������ʱ��ʱ����ʱ���ȥ��ǰʱ����Ϊepoll����
    //select�ȴ��ļ��¼��ĳ�ʱʱ�䣬��Ѱ�ҹ��̾���ͨ������ʱ���¼�����������ģ�����ָ����ʱʱ�䣬����IO��
    //��pendingʱ���Դ���IO�¼�����û������Ա�֤�ڸó�ʱʱ�䵽��ʱ��epoll����select�з���ȥ����ʱ���¼���
    if (eventLoop->maxfd != -1 || ((flags & AE_TIME_EVENTS) && !(flags & AE_DONT_WAIT))) {
            int j;
            aeTimeEvent *shortest = NULL;
            struct timeval tv, *tvp;

            //��ȡ�����ʱ���¼�shortest�����м�¼��ʱ���¼��ĵ���ʱ�䡣
            if (flags & AE_TIME_EVENTS && !(flags & AE_DONT_WAIT))
                shortest = aeSearchNearestTimer(eventLoop);
            
            //���ʱ���¼����ڣ����������ִ��ʱ���¼�����ʱ������ڵ�ʱ���洢��timeval�ṹ�У�
            //��Ϊepoll(select)�����ļ�ʱ��ĳ�ʱʱ�䡣
            if (shortest) {
                long now_sec, now_ms;
                //����ʱ�䵽��ʱ��<e_sec,e_msec>�͵�ǰʱ��<n_sec,n_msec>��ʱ��
                //�õ������ʱ���¼���Ҫ��ò��ܹ����ʱ������timeval�ṹ���С�
                aeGetTime(&now_sec, &now_ms);
                tvp = &tv;
                tvp->tv_sec = shortest->when_sec - now_sec;                     //�����
                if (shortest->when_ms < now_ms) {
                    tvp->tv_usec = ((shortest->when_ms+1000) - now_ms)*1000;    //��λ�󣬺���ת΢�����
                    tvp->tv_sec --;
                } else {
                    tvp->tv_usec = (shortest->when_ms - now_ms)*1000;           //����ת΢�������
                }

                // ʱ���С�� 0 ��˵�����ʱ���¼��Ѿ������������ִ�У�����epoll(select)Ӧ���賬ʱΪ0��
                // ���������������ش���ʱ���¼�����������--�������أ�
                if (tvp->tv_sec < 0) 
                    tvp->tv_sec = 0;
                if (tvp->tv_usec < 0) 
                    tvp->tv_usec = 0;
            } else {
                // ִ�е���һ����˵��û��ʱ���¼������߱���������AE_DONT_WAIT������������ʶ��
                // ��ô���� AE_DONT_WAIT�Ƿ������������Ƿ��������Լ�������ʱ�䳤�ȡ�
                /* If we have to check for events but need to return ASAP because of 
                 * AE_DONT_WAIT we need to se the timeout to zero */
                if (flags & AE_DONT_WAIT) {         // �����ļ��¼�����������ʱΪ0--�������أ�
                    tv.tv_sec = tv.tv_usec = 0;
                    tvp = &tv;
                } else {                            // �ļ��¼���������ֱ�����¼�����Ϊֹ����ʱΪNULL--ֱ�������ļ�ʱ�䣩    
                    tvp = NULL; /* wait forever */
                }
            }

            //���ò�ͬ������ģ��poll�¼�,����redis�ļ��¼�
            //����ʱ���� tvp ������tvpΪNULLΪһֱ������tvpΪ<0,0>λ�������������ء�
            numevents = aeApiPoll(eventLoop, tvp);
            for (j = 0; j < numevents; j++) {
                // ���Ѿ�������fired�л�ȡ�����¼�
                aeFileEvent *fe = &eventLoop->events[eventLoop->fired[j].fd];

                int mask    = eventLoop->fired[j].mask;
                int fd      = eventLoop->fired[j].fd;
                int rfired  = 0;

                /* note the fe->mask & mask & ... code: maybe an already processed
                * event removed an element that fired and we still didn't
                * processed, so we check if the event is still valid. */
                if (fe->mask & mask & AE_READABLE) {    // ���¼�
                    rfired = 1;                         // ȷ����/д�¼�ֻ��ִ������һ��
                    fe->rfileProc(eventLoop,fd,fe->clientData,mask);
                }
                if (fe->mask & mask & AE_WRITABLE) {    // д�¼�
                    if (!rfired || fe->wfileProc != fe->rfileProc)
                        fe->wfileProc(eventLoop,fd,fe->clientData,mask);
                }
                processed++;
            }
    }
    //����ִ��ʱ���¼�
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

// �¼�����������ѭ��
// 
void aeMain(aeEventLoop *eventLoop) {

    eventLoop->stop = 0;

    while (!eventLoop->stop) {

        // �������Ҫ���¼�����ǰִ�еĺ�������ô������
        if (eventLoop->beforesleep != NULL)
            eventLoop->beforesleep(eventLoop);
        // ��ʼ�����¼�
        aeProcessEvents(eventLoop, AE_ALL_EVENTS);
    }
}

// ������ʹ�õĶ�·���ÿ������
char *aeGetApiName(void) {
    return aeApiName();
}

// ���ô����¼�ǰ��Ҫ��ִ�еĺ���
// 
void aeSetBeforeSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *beforesleep) {
    eventLoop->beforesleep = beforesleep;
}
