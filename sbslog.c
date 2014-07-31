
#ifdef __cplusplus
extern "C"{
#endif

/* Standard Linux headers */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>

#include "sbslog.h"

#define     MAX_LOG_SIZE					(1048576*10) /*大小为字节，默认为10M=1024 * 1024Byte*/
#define     MAX_FILE_CNT					10 /*ui日志最大为5M*/

const char *sg_acLOG_LEV[] =
{
    "ERR",
    "WAN",
    "NOT",
    "DBG",
    "",
};

/* 定义LOG_FILE结构体 */
typedef struct LOG_FILE_s
{
	char    pcFilePath[256];    /* Log当前文件路径 */
	char   pcFilePrefix[10];    /* Log前缀 */
	int     iMaxFileSize;       /* Log大小 */
	int     iMaxFileCount;      /* Log文件个数，主要控制空间大小 */

	FILE *pfFileHandle;         /* 文件句柄 */ 
	int iSeq;                   /* Log文件编号 */
	int iSize;                  /* 当前文件大小 */
    int iLevel;                 /* 打印级别 */
    int iDirect;                /* 是否写日志文件，还是直接输出控制台 */
    pthread_mutex_t sLockMutex; /* 线程锁 */
} LOG_FILE_T;

static LOG_FILE_T   sg_LogArgs;

/******************************************************************************************************
* 函数介绍:     LogGetSeqID函数，读取LOG目录下的ui_log_id.dat文件获取当前LOG日志号
* 输入参数说明: 无  
* 输出参数说明: 无
* 返回值:       成功返回0,失败返回-1
*******************************************************************************************************
*/
static int LogGetSeqID(void)
{
     char        acPath[256],acTemp[64];
     FILE       *pIDFile = NULL;
     
     sprintf(acPath,"%s%s.index",sg_LogArgs.pcFilePath,sg_LogArgs.pcFilePrefix);
     pIDFile = fopen(acPath, "r");
     if(pIDFile != NULL)
     {
         if(fgets(acTemp, 64, pIDFile))
         {
               sg_LogArgs.iSeq = atoi(acTemp);
         }
         fclose(pIDFile);
         //printf("logGetSeqID:%d,%s\n",sg_LogArgs.iSeq,acTemp);
         return 0;
     }
     return -1;
}

/******************************************************************************************************
* 函数介绍:     LogGetFSize函数，得到当前索引日志的大小
* 输入参数说明: 无
* 输出参数说明: 无
* 返回值:       无
*******************************************************************************************************
*/
static void LogGetFSize(void)
{
    char acPath[256];
    struct stat stBuf;
     
    memset(&stBuf, 0, sizeof(stBuf));
    sprintf(acPath,"%s%s%02d.log",sg_LogArgs.pcFilePath,sg_LogArgs.pcFilePrefix,sg_LogArgs.iSeq);
    stat(acPath, &stBuf);//得到指定的文件的信息
    sg_LogArgs.iSize = stBuf.st_size; 
    //printf("logGetFSize:%d\n",sg_LogArgs.iSize);
}

/******************************************************************************************************
* 函数介绍:     LogSetSeqID函数，设置LOG目录下的ui_log_id.dat文件获取当前LOG日志号
* 输入参数说明: 无
* 输出参数说明: 无
* 返回值:       成功返回SEQID，失败返回0
*******************************************************************************************************
*/
static void LogSetSeqID(void)
{
     char        acPath[256],acTemp[64];
     FILE       *pIDFile;
     sprintf(acPath,"%s%s.index",sg_LogArgs.pcFilePath,sg_LogArgs.pcFilePrefix);
     pIDFile = fopen(acPath, "w");
     if(pIDFile != NULL)
     {
        sprintf(acTemp, "%d", sg_LogArgs.iSeq);
        fputs(acTemp, pIDFile);
        fclose(pIDFile);
     }
}

/******************************************************************************************************
* 函数介绍:     LogChangeFile函数，切换日志文件
* 输入参数说明: 无
* 输出参数说明: 无
* 返回值:       无
*******************************************************************************************************
*/
static void LogChangeFile(void)
{
     char acPath[256] = {0};
     
     /*回写旧文件*/
     if(sg_LogArgs.pfFileHandle != NULL)
     {
        fflush(sg_LogArgs.pfFileHandle);
        fclose(sg_LogArgs.pfFileHandle);
     }
     /*创建新文件*/
     sg_LogArgs.iSeq ++;
     if(sg_LogArgs.iSeq > sg_LogArgs.iMaxFileCount)
     {
        sg_LogArgs.iSeq = 1;
     }
     sprintf(acPath,"%s%s%02d.log",sg_LogArgs.pcFilePath,sg_LogArgs.pcFilePrefix,sg_LogArgs.iSeq);
     unlink(acPath);
     sg_LogArgs.pfFileHandle = fopen(acPath, "a+");
     sg_LogArgs.iSize = 0;
     //printf("create new file:%s\n",acPath);
     LogSetSeqID();
}

/******************************************************************************************************
* 函数介绍:     LogInit函数，主要初始化写Log文件中的一些参数
* 输入参数说明: pcLogPath--Log文件路径，如:/home/projects/rtsp/log/,此路径必须带后缀“/”
*               pcLogPrefix--Log文件名前缀
*               iMaxLogSize--Log文件的大小
*               iMaxFileCount--Log文件个数，主要用于控制空间大小，可在规定的文件数里循环
*               iMaxFileSize--iMaxFileCount 都取0时，表示取默认值 5个最大为1M的文件
*               iLevel--打印级别,级别定义见LOG_LEV_EN枚举类型
*               iDirect--打印输出方向,支持的输出方向有LOG_TTY和LOG_FLASH
* 输出参数说明: 无
* 返回值:       成功返回0，失败返回1
*******************************************************************************************************
*/
int LogInit(char *pcFilePath, char *pcFilePrefix, int iMaxFileSize, int iMaxFileCount,
             int iLevel,int iDirect)
{
    char        acPath[128];
    if(pcFilePath != NULL)
    {
        strncpy(sg_LogArgs.pcFilePath,pcFilePath,256);
    }
    else
    {
        return 1;
    }
    /*目录不存，则创建*/
    if (access(pcFilePath,F_OK) < 0)
    {
    	mkdir(pcFilePath,0777);
    }
    /*在目录最后加上/*/
     if(sg_LogArgs.pcFilePath[strlen(sg_LogArgs.pcFilePath) -1] != '/')
    {
        strcat(sg_LogArgs.pcFilePath,"/");
    }
    
    if(pcFilePrefix != NULL)
    {
        strncpy(sg_LogArgs.pcFilePrefix,pcFilePrefix,10);
    }
    else
    {
        strcpy(sg_LogArgs.pcFilePrefix,"xx_log_");
    }
    /*配置文件打印级别*/
    sg_LogArgs.iLevel = iLevel%LOG_LAST;
    /*配置文件日志输出方向*/
    sg_LogArgs.iDirect = !!iDirect;
     
    if(iMaxFileSize > MAX_LOG_SIZE || iMaxFileSize == 0)
    {
        iMaxFileSize = MAX_LOG_SIZE;
    }
    if(iMaxFileCount > MAX_FILE_CNT || iMaxFileCount == 0)
    {
        iMaxFileCount = MAX_FILE_CNT;
    }
    
    sg_LogArgs.iMaxFileSize = iMaxFileSize;
    sg_LogArgs.iMaxFileCount = iMaxFileCount;
    sg_LogArgs.iSeq = 1;
    pthread_mutex_init(&sg_LogArgs.sLockMutex, NULL);
    /* 得到SEQ */
    if(LogGetSeqID())
    {
        LogSetSeqID();
    }
     /* 得到文件大小 */
    LogGetFSize();
     
    sprintf(acPath,"%s%s%02d.log",sg_LogArgs.pcFilePath,sg_LogArgs.pcFilePrefix,sg_LogArgs.iSeq);
    //printf("get isize:%d,path:%s\n",sg_LogArgs.iSize,acPath);
    ///printf("get file:%s\n",acPath);
    if((sg_LogArgs.pfFileHandle = fopen(acPath, "a+")) == NULL)
    {
        return 1;
    }
    else
    {
        return 0;
    }
    
}

/******************************************************************************************************
* 函数介绍:     LogDestroy函数，释放LOG资源
* 输入参数说明: 无
* 输出参数说明: 无
* 返回值:       无
*******************************************************************************************************
*/
void LogDestroy(void)
{
	if(sg_LogArgs.pfFileHandle != NULL)
	{
		pthread_mutex_lock(&sg_LogArgs.sLockMutex);
		/* 清空文件中的缓冲和记录写文件中的位置 */
		fflush(sg_LogArgs.pfFileHandle);
		fclose(sg_LogArgs.pfFileHandle);
		sg_LogArgs.pfFileHandle = NULL;
		pthread_mutex_unlock(&sg_LogArgs.sLockMutex);
		pthread_mutex_destroy(&sg_LogArgs.sLockMutex);
	}
}

/******************************************************************************************************
* 函数介绍:     LOG函数，日志打印函数,打印时如果当前文件写满了，就循环写新的文件
* 输入参数说明: iLevel---打印等级:
*                        LOG_ERR 严重错误
*                        LOG_WAN 警告
*                        LOG_DBG 只是调试用的日志
* 输出参数说明: 无
* 返回值:       无
*******************************************************************************************************
*/
void SBS_LOG(int   iLevel,int  iLine,char *pcFile,const char *pcFunc,const char * fmt, ...)
{
    int     iCnt = 0;
    char    acTime[32];
    struct tm stTime;
    struct timeval stDetailTtime;
    FILE    *pOut = NULL;
   
    /* 打印级别屏蔽 */
    if(iLevel > sg_LogArgs.iLevel)
    {
        return;
    }
    
    acTime[0] = 0;
    pthread_mutex_lock(&sg_LogArgs.sLockMutex);
    
    /* 判断文件是否满 */
    if(sg_LogArgs.iSize >= sg_LogArgs.iMaxFileSize)
    {
        LogChangeFile();
    }
    /* 得到系统时间，主要得到微秒时间 */
    gettimeofday(&stDetailTtime, NULL);
    localtime_r(&stDetailTtime.tv_sec, &stTime);
    /* 每行Log内容前面加上时间 */
    sprintf(acTime, "%02d/%02d %02d:%02d:%02d",
    	stTime.tm_mon + 1,stTime.tm_mday,stTime.tm_hour,stTime.tm_min,stTime.tm_sec);
    
    va_list vap;
    va_start(vap, fmt);
    
    if(sg_LogArgs.iDirect == LOG_TTY)
    {
        pOut = stdout;
    }
    else
    {
        pOut = sg_LogArgs.pfFileHandle;
    }
    
    if (pOut != NULL)
    {
        if (pcFile && pcFunc)
        {
            iCnt = fprintf(pOut, "[%s][%s]-[%s][%s][%d]-", \
                           sg_LogArgs.pcFilePrefix,sg_acLOG_LEV[iLevel%LOG_LAST], acTime, pcFunc, iLine);
        }
        else
        {
            /* 如果文件名和函数名有一项为空,则不将行号,文件名,函数名这三项内容写入日志 */
            iCnt = fprintf(pOut, "[%s][%s]-[%s]-", \
                           sg_LogArgs.pcFilePrefix,sg_acLOG_LEV[iLevel%LOG_LAST], acTime);
        }
        iCnt += vfprintf(pOut, fmt, vap);
        va_end(vap);
        sg_LogArgs.iSize += iCnt;
		fflush(pOut);
    }
    pthread_mutex_unlock(&sg_LogArgs.sLockMutex);
    
}

/******************************************************************************************************
* 函数介绍:     LOG函数，日志打印函数,打印时如果当前文件写满了，就循环写新的文件
* 输入参数说明: iLevel---打印等级:
*                        LOG_ERR 严重错误
*                        LOG_WAN 警告
*                        LOG_DBG 只是调试用的日志
* 输出参数说明: 无
* 返回值:       无
*******************************************************************************************************
*/
void SLOG(int   iLevel,char * plog)
{
    int     iCnt = 0;
    char    acTime[32];
    struct tm stTime;
    struct timeval stDetailTtime;
    FILE    *pOut = NULL;
   
    /* 打印级别屏蔽 */
    if(iLevel > sg_LogArgs.iLevel || plog == NULL)
    {
        return;
    }
    
    acTime[0] = 0;
    pthread_mutex_lock(&sg_LogArgs.sLockMutex);
    
    /* 判断文件是否满 */
    if(sg_LogArgs.iSize >= sg_LogArgs.iMaxFileSize)
    {
        LogChangeFile();
    }
    /* 得到系统时间，主要得到微秒时间 */
    gettimeofday(&stDetailTtime, NULL);
    localtime_r(&stDetailTtime.tv_sec, &stTime);
    /* 每行Log内容前面加上时间 */
    sprintf(acTime, "%02d/%02d %02d:%02d:%02d",
    	stTime.tm_mon + 1,stTime.tm_mday,stTime.tm_hour,stTime.tm_min,stTime.tm_sec);
    
    if(sg_LogArgs.iDirect == LOG_TTY)
    {
        pOut = stdout;
    }
    else
    {
        pOut = sg_LogArgs.pfFileHandle;
    }
    
    if (pOut != NULL)
    {
        /* 如果文件名和函数名有一项为空,则不将行号,文件名,函数名这三项内容写入日志 */
        iCnt = fprintf(pOut, "[%s]-[%s]-%s", \
                       sg_acLOG_LEV[iLevel%LOG_LAST], acTime,plog);
        sg_LogArgs.iSize += iCnt;
		fflush(pOut);
    }
    pthread_mutex_unlock(&sg_LogArgs.sLockMutex);
    
}


#ifdef __cplusplus
}
#endif

