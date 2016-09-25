#ifndef __AE_H__
#define __AE_H__

#include <time.h>
#include <stdlib.h>

/*
* �¼�ִ��״̬
*/
#define AE_OK 0     // �ɹ�
#define AE_ERR -1   // ����

/*
* �ļ��¼�״̬
*/
#define AE_NONE 0       // δ����
#define AE_READABLE 1   // �ɶ�
#define AE_WRITABLE 2   // ��д

/*
* ʱ�䴦������ִ�� flags
*/
#define AE_FILE_EVENTS 1
#define AE_TIME_EVENTS 2
#define AE_ALL_EVENTS (AE_FILE_EVENTS|AE_TIME_EVENTS)
#define AE_DONT_WAIT 4

/*
* ����ʱ���¼��Ƿ�Ҫ����ִ�е� flag
*/
#define AE_NOMORE -1

/* Macros */
#define AE_NOTUSED(V) ((void) V)

/*
* �¼�������״̬
*/
struct aeEventLoop;

/* Types and data structures */
typedef void aeFileProc(struct aeEventLoop *eventLoop, int fd, void *clientData, int mask);
typedef int aeTimeProc(struct aeEventLoop *eventLoop, long long id, void *clientData);
typedef void aeEventFinalizerProc(struct aeEventLoop *eventLoop, void *clientData);
typedef void aeBeforeSleepProc(struct aeEventLoop *eventLoop);


/*  aeFileEvent ���ļ��¼��ṹ��*/
typedef struct aeFileEvent {
    int mask;               // Ҫ������¼������루��|д����ֵ������AE_READABLE��AE_WRITABLE,�������ߵĻ�*/
    aeFileProc *rfileProc;  // д�¼�����
    aeFileProc *wfileProc;  // ���¼�����
    void *clientData;       // clientData�Ǻ����õĲ���
} aeFileEvent;

/* Time event structure ʱ���¼��ṹ��*/
typedef struct aeTimeEvent {
    long long id;                           // ʱ���¼���Ψһ��ʶ��  time event identifier. 
    
    long when_sec;                          // �¼��ĵ���ʱ�� ��
    long when_ms;                           // �¼��ĵ���ʱ�� ΢��
    
    aeTimeProc *timeProc;                   // ʱ���¼�������
    aeEventFinalizerProc *finalizerProc;    // ��ʱ�¼�����������ɾ����ʱ�¼���ʱ��ᱻ����
    void *clientData;                       // ��·���ÿ��˽������
    struct aeTimeEvent *next;               // ָ���¸�ʱ���¼��ṹ���γɵ���������

} aeTimeEvent;

/* A fired event (epollwait)�Ѿ����¼� */
typedef struct aeFiredEvent {
    int fd;     // �Ѿ����ļ�������
    int mask;   // �¼��������룬������ AE_READABLE �� AE_WRITABLE
} aeFiredEvent;

/* State of an event based program �¼���������״̬ */
typedef struct aeEventLoop {

    int maxfd;                      //��¼��ע�������ļ�������,��ʼ��Ϊ-1��
    int setsize;                    //Ŀǰ��׷�ٵ����������      
    long long timeEventNextId;      //��ʱ���¼���ID��Ź�������������һ��ʱ���¼���timer_id
    time_t lastTime;                //���һ��ִ��ʱ���¼���ʱ�䣬�������ϵͳʱ��ƫ��Խ���ϵͳʱ���

    aeFileEvent *events;            //���ڱ���epoll��Ҫ��ע���ļ��¼���fd������������ע�ắ�� 
    aeFiredEvent *fired;            //�Ѵ������ļ��¼���poll_wait֮���ÿɶ����߿�д��fd���飬ͨ��aeFiredEvent->fd�ٶ�λ��events

    aeTimeEvent *timeEventHead;     //��ʱ���¼�����������ʽ������ʱ���¼���ÿ��һ��ʱ��ͻᴥ��ע��ĺ��������Ӷ�O(n),�ʲ��˹��ࣩ

    int stop;                       //�¼�ѭ��������ʶ,1��ʾֹͣ(ʱ����ѯ�Ƿ����?)

                                    //����Ǵ���ײ��ض�API�����ݣ�����epoll��˵���ýṹ�������epoll fd��epoll_event
    void *apidata;                  //��·���ÿ��˽������---�ļ��¼�����ѯ���ݺͽ�����ݣ�poll�� ������ѯ��ʽ��epoll��linux����select��windows����kqueue  
                                    //���ڲ�ͬ�� I/O ��·���ü������в�ͬ�����ݣ��������ʵ�֡�
                                    
    aeBeforeSleepProc *beforesleep; //��ÿ���¼�ѭ������ǰִ�����callback��

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
