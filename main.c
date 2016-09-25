#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

#include "ae.h"
#include "anet.h"
#include "log_util.h"

#define PORT 4444
#define MAX_LEN 1024
#define VERSION "0.9.2"
//存放错误信息的字符串
char g_err_string[1024];
//是否守护模式运行
static int run_daemonize = 0;
//事件循环机制
aeEventLoop *g_event_loop = NULL;

//定时器的入口，输出一句话
int PrintTimer(struct aeEventLoop *eventLoop, long long id, void *clientData)
{
    static int i = 0;
    printf("Test Output: %d\n", i++);

    //10秒后再次执行该函数
    return 10000;
}

//停止事件循环
void StopServer()
{
    aeStop(g_event_loop);
}

/*
void SignalHandler(int signo) {
  if (signo == SIGINT || signo == SIGTERM) {
    el->stop = 1;
  }
}*/
void Usage() {
  printf("usage:\n"
      "  tcproxy [options] \"proxy policy\"\n"
      "options:\n"
      "  -l file    specify log file\n"
      "  -d         run in background\n"
      "  -v         show detailed log\n"
      "  --version  show version and exit\n"
      "  -h         show help and exit\n\n"
      "examples:\n"
      "  tcproxy \"11212 -> 11211\"\n"
      "  tcproxy \"127.0.0.1:6379 -> rr{192.168.0.100:6379 192.168.0.101:6379}\"\n\n"
      );
  exit(EXIT_SUCCESS);
}

void ParseArgs(int argc, char **argv) {
  int i, j;
  const char *logfile = "logfile";
  int loglevel = kError;
  InitLogger(loglevel, NULL);

  for (i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
        Usage();
      } else if (!strcmp(argv[i], "--version")) {
        printf("tcproxy "VERSION"\n\n");
        exit(EXIT_SUCCESS);
      } else if (!strcmp(argv[i], "-d")) {
        run_daemonize = 1;
      } else if (!strcmp(argv[i], "-l")) {
        if (++i >= argc) 
          LogFatal("file name must be specified");
        logfile = argv[i];
      } else if (!strncmp(argv[i], "-v", 2)) {
        for (j = 1; argv[i][j] != '\0'; j++) {
          if (argv[i][j] == 'v') 
            loglevel++;
          else 
            LogFatal("invalid argument %s", argv[i]);;
        }
      } else {
        LogFatal("unknow option %s\n", argv[i]);
      }
    } 
  }

  InitLogger(loglevel, logfile);
}
void ClientClose(aeEventLoop *el, int fd, int err)
{
    //如果err为0，则说明是正常退出，否则就是异常退出
    if( 0 == err )
        printf("Client quit: %d\n", fd);
    else if( -1 == err )
        fprintf(stderr, "Client Error: %s\n", strerror(errno));

    //删除结点，关闭套接字文件
    aeDeleteFileEvent(el, fd, AE_READABLE);
    close(fd);
}

//某个已连接套接字cfd上发生可读事件时，调用本回调函数：
//（1）读输入的数据
//（2）若数据长度为0，不回复数据，直接从事件监听列表删除该cfs上读事件。
//（3）若数据长度不为0，回复同样的数据，若回复失败，从事件监听列表删除该cfs上读事件，若回复成功不做其它操作。
void ReadFromClient(aeEventLoop *el, int fd, void *privdata, int mask)
{
    char buffer[MAX_LEN] = { 0 };
    int res;
    res = read(fd, buffer, MAX_LEN);
    if( res <= 0 )
    {
        ClientClose(el, fd, res);
    }
    else
    {
        res = write(fd, buffer, MAX_LEN);
        if( -1 == res )
            ClientClose(el, fd, res);
    }
}

//接受新连接：作为监听套接字listen_fd上IO读事件的回调函数，当listen_fd上发生可读事件时意味着
//有新连接请求，此时会调用此函数对连接请求进行处理：创建已连接套接字，添加到事件监听列表。
//
//调用anetTcpAccept接受客户端的连接请求，会返回一个已连接socket描述符cfd， 这个新的描述符上
//会有网络IO事件发生，可以继续使用aeCreateFileEvent把cfd上的IO事件（读写）添加到aeEventLoop中。
void AcceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask)
{
    int cfd, cport;
    char ip_addr[128] = { 0 };
    cfd = anetTcpAccept(g_err_string, fd, ip_addr, &cport);
    if (cfd == AE_ERR) {
        LogError("Accept client connection failed: %s", g_err_string);
        return;
    }
    LogInfo("Accepted client from %s:%d", cfd, cport);

    /*Client *c = AllocClient(cfd);
    if (c == NULL || aeCreateFileEvent(el, cfd, AE_READABLE, ReadIncome, c) == AE_ERR) {
        LogError("Create event failed");
        FreeClient(c);
    }*/
    if( aeCreateFileEvent(el, cfd, AE_READABLE, ReadFromClient, NULL) == AE_ERR )
    {
        fprintf(stderr, "client connect fail: %d\n", fd);
        close(fd);
    }
}

int main(int argc, char **argv)
{
    ParseArgs(argc, argv);

    printf("Start\n");
    if (run_daemonize) 
        Daemonize();
    //SIGINT信号代表由InterruptKey产生，产生时调用StopServer处理，
    //处理完毕从中断处理函数返回中断位置继续执行。
    signal(SIGINT, StopServer);

    //初始化网络事件循环
    g_event_loop = aeCreateEventLoop(1024*10);

    //设置监听事件
    int listen_fd  = anetTcpServer(g_err_string, PORT, NULL);
    if( ANET_ERR == listen_fd )
        fprintf(stderr, "Open port %d error: %s\n", PORT, g_err_string);

    //调用aeCreateFileEvent来创建一个Event,它需要一个callback,这个Event会在EventLoop中被处理并调用这个callback。
    //在这里其实就是创建一个aeFileEvent结构体的实例并添加到g_event_loop的events中。
    if( aeCreateFileEvent(g_event_loop, listen_fd, AE_READABLE, AcceptTcpHandler, NULL) == AE_ERR )
        fprintf(stderr, "Unrecoverable error creating server.ipfd file event.");

    //设置定时事件：
    //（1）1是超时时间。
    //（2）PrintTimer是回调函数，该函数返回10000（10s）,意味着是循环的定时器事件，不会一次执行后被删除。
    //（3）回调函数参数为null。
    //（4）该定时器事件被删除时不执行清理操作。
    aeCreateTimeEvent(g_event_loop, 1, PrintTimer, NULL, NULL);

    //开启事件循环
    aeMain(g_event_loop);

    //删除事件循环
    aeDeleteEventLoop(g_event_loop);
 
    printf("End\n");

    return 0;
}