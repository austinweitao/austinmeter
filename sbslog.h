/******************************************************************************************************
*******************************************************************************************************
*/

#ifndef __LOG_H__
#define __LOG_H__

#include <stdarg.h>

typedef enum 
{
    LOG_ERR = 0,/* 严重错误 */
    LOG_WAR,    /* 警告 */
    LOG_NOT,    /* 通知 */
    LOG_DBG,    /* 只是调试用的日志 */
    LOG_LAST,
}LOG_LEV_EN;

#define     LOG_TTY             0x00       /*串口*/
#define     LOG_FLASH           0x01       /*FLASH*/

/* 本地默认的日志输出方向和级别，当读配置文件失败时，取此值 */
//#define     LOG_DIRECT          LOG_TTY
//#define     LOG_LEVEL           LOG_DBG

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
void SBS_LOG(int   iLevel,int  iLine,char *pcFile,const char *pcFunc,const char * fmt, ...);

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
              int iLevel,int iDirect);

/******************************************************************************************************
* 函数介绍:     LogDestroy函数，释放LOG资源
* 输入参数说明: 无
* 输出参数说明: 无
* 返回值:       无
*******************************************************************************************************
*/
void LogDestroy(void);

#define     LOGERR(fmt,args...)   LOG(LOG_ERR, __LINE__,__FILE__,__FUNCTION__,fmt,## args)
#define     LOGWAR(fmt,args...)   LOG(LOG_WAR,__LINE__,__FILE__,__FUNCTION__,fmt,## args)
#define     LOGNOT(fmt,args...)   LOG(LOG_NOT,__LINE__,__FILE__,__FUNCTION__,fmt,## args)
#define     LOGDBG(fmt,args...)   LOG(LOG_DBG,__LINE__,__FILE__,__FUNCTION__,fmt,## args)

/*简单LOG*/
void SLOG(int   iLevel,char * plog);

//#define     LOGFTPDBG(fmt,args...)   LOG(LOG_DBG,"FTP",__LINE__,__FILE__,__FUNCTION__,fmt,## args)
#endif
