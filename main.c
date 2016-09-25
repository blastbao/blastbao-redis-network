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
//��Ŵ�����Ϣ���ַ���
char g_err_string[1024];
//�Ƿ��ػ�ģʽ����
static int run_daemonize = 0;
//�¼�ѭ������
aeEventLoop *g_event_loop = NULL;

//��ʱ������ڣ����һ�仰
int PrintTimer(struct aeEventLoop *eventLoop, long long id, void *clientData)
{
    static int i = 0;
    printf("Test Output: %d\n", i++);

    //10����ٴ�ִ�иú���
    return 10000;
}

//ֹͣ�¼�ѭ��
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
    //���errΪ0����˵���������˳�����������쳣�˳�
    if( 0 == err )
        printf("Client quit: %d\n", fd);
    else if( -1 == err )
        fprintf(stderr, "Client Error: %s\n", strerror(errno));

    //ɾ����㣬�ر��׽����ļ�
    aeDeleteFileEvent(el, fd, AE_READABLE);
    close(fd);
}

//ĳ���������׽���cfd�Ϸ����ɶ��¼�ʱ�����ñ��ص�������
//��1�������������
//��2�������ݳ���Ϊ0�����ظ����ݣ�ֱ�Ӵ��¼������б�ɾ����cfs�϶��¼���
//��3�������ݳ��Ȳ�Ϊ0���ظ�ͬ�������ݣ����ظ�ʧ�ܣ����¼������б�ɾ����cfs�϶��¼������ظ��ɹ���������������
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

//���������ӣ���Ϊ�����׽���listen_fd��IO���¼��Ļص���������listen_fd�Ϸ����ɶ��¼�ʱ��ζ��
//�����������󣬴�ʱ����ô˺���������������д��������������׽��֣���ӵ��¼������б�
//
//����anetTcpAccept���ܿͻ��˵��������󣬻᷵��һ��������socket������cfd�� ����µ���������
//��������IO�¼����������Լ���ʹ��aeCreateFileEvent��cfd�ϵ�IO�¼�����д����ӵ�aeEventLoop�С�
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
    //SIGINT�źŴ�����InterruptKey����������ʱ����StopServer����
    //������ϴ��жϴ����������ж�λ�ü���ִ�С�
    signal(SIGINT, StopServer);

    //��ʼ�������¼�ѭ��
    g_event_loop = aeCreateEventLoop(1024*10);

    //���ü����¼�
    int listen_fd  = anetTcpServer(g_err_string, PORT, NULL);
    if( ANET_ERR == listen_fd )
        fprintf(stderr, "Open port %d error: %s\n", PORT, g_err_string);

    //����aeCreateFileEvent������һ��Event,����Ҫһ��callback,���Event����EventLoop�б������������callback��
    //��������ʵ���Ǵ���һ��aeFileEvent�ṹ���ʵ������ӵ�g_event_loop��events�С�
    if( aeCreateFileEvent(g_event_loop, listen_fd, AE_READABLE, AcceptTcpHandler, NULL) == AE_ERR )
        fprintf(stderr, "Unrecoverable error creating server.ipfd file event.");

    //���ö�ʱ�¼���
    //��1��1�ǳ�ʱʱ�䡣
    //��2��PrintTimer�ǻص��������ú�������10000��10s��,��ζ����ѭ���Ķ�ʱ���¼�������һ��ִ�к�ɾ����
    //��3���ص���������Ϊnull��
    //��4���ö�ʱ���¼���ɾ��ʱ��ִ�����������
    aeCreateTimeEvent(g_event_loop, 1, PrintTimer, NULL, NULL);

    //�����¼�ѭ��
    aeMain(g_event_loop);

    //ɾ���¼�ѭ��
    aeDeleteEventLoop(g_event_loop);
 
    printf("End\n");

    return 0;
}