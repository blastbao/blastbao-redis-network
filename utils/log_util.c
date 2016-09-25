#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

#include "log_util.h"

static LogLevel log_level = kDebug;
static FILE *log_file     = NULL;
static char now_str[sizeof("2011/11/11 11:11:11")];
static const char *LevelName[] = {"NONE","FATAL","CRITICAL","ERROR","WARNING","INFO","DEBUG",};


static void UpdateTime() {
  static time_t now = 0;
  //printf("DEBUG PRINT Variable [%s] of UpdateTime Func %d\n", "now",now);
  time_t t = time(NULL);

  //update time every second
  if (t - now == 0) return;
  now = t;

  struct tm tm;
  //localtime_r是线程安全的,可重入:将时间戳转换为日期结构体tm。
  localtime_r(&now, &tm);
  sprintf(now_str, "%04d/%02d/%02d %02d:%02d:%02d",
      1900 + tm.tm_year, tm.tm_mon + 1, tm.tm_mday, 
      tm.tm_hour, tm.tm_min, tm.tm_sec);
}

//输出日志信息到文件。
//
//vfprintf()会根据参数format字符串来转换并格式化数据，
//然后将结果输出到参数stream指定的文件中，直到出现字符串结束（‘\0’）为止。
//fflush()通常用于处理磁盘文件，清除读写缓冲区，立即把输出缓冲区的数据进行物理写入。
void LogPrint(LogLevel level, const char *fmt, ...) {
  va_list  args;
  if (level > log_level) //数值越高对应日志级别越低，若数值高于指定级别log_level，不打印。
    return;
  va_start(args, fmt);
  if (log_file) 
    vfprintf(log_file, fmt, args);
  va_end(args);
  fflush(log_file);
}

//先调用UpdateTime()获取格式化的日志字符串(时间+日志级别)，再输出其他日志信息到文件。
//与fprintf相比，vfprintf适合可变参数列表传递。
void LogInternal(LogLevel level, const char *fmt, ...) {
  va_list  args;
  if (level > log_level) 
    return;
  UpdateTime();
  if (log_file) 
    fprintf(log_file, "%s [%s] ", now_str, LevelName[level]);
  va_start(args, fmt);
  if (log_file) 
    vfprintf(log_file, fmt, args);
  va_end(args);
  fflush(log_file);
}

void InitLogger(LogLevel level, const char *filename) {
  log_level = level;
  //若果没设置日志文件名称，就把日志输出到标准错误输出中。
  if (filename == NULL || strcmp(filename, "stderr") == 0 || strcmp(filename, "") == 0) {
    log_file = stderr;
  } else if (strcmp(filename, "stdout") == 0) {
    log_file = stdout;
  } else {
    //否则，以追加写的方式打开日志文件。
    log_file = fopen(filename, "a+");
  }
}
