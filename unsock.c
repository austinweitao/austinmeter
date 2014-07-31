#ifdef __cplusplus
extern "C"{
#endif

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdarg.h>

#include "unsock.h"

#define UNSOCK_LOOP_MAX     (3)            /* 循环收发的最大次数 */
#define UNSOCK_TIMEOUT      (0)             /* select超时 */
#define UNSOCK_AGAIN_TV     (200*1000)      /* 重发收发时间间隔 */

/***************************************************************************************
*撤销发送缓冲区，减少一次系统缓冲到socker缓冲的拷贝，
*提高系统性能，数据完全有接受buffer进行缓冲。
****************************************************************************************
*/
#define UNSOCK_SENDBUF_SZ   (0)             
#define UNSOCK_RECVBUF_SZ   (1*1024)

void  DebugMsg(const char * fmt, ...)
{
	//printf("dbgmsg:%s\n",sg_logname);
    va_list vap;
    va_start(vap, fmt);
    //FILE * dbgfile = fopen(DEBUG_FILE_NAME, "a+");
    FILE * dbgfile = fopen("/sdcard/libsocket.log", "a+");
    if (dbgfile != NULL){
        vfprintf(dbgfile, fmt, vap);
        va_end(vap);
        fclose(dbgfile);
    }
}



/***************************************************************************************
*
* 函数介绍：    UnSocketBind，初始化unix域的udp_socket
*               iModuleid    --为模块id
*               iMode   --为0时即接收端，1为发送端
* 返回值：      成功返回套接字句柄，失败返回-1 
****************************************************************************************
*/
int UnSocketBind(int iModuleid, UN_MDL_TYPE_EN iMode)
{
	int	fd = -1;
	struct sockaddr_un saddr;
	int family = AF_LOCAL;
	int type = SOCK_DGRAM;
	int protocol = 0; 
	int flags = 0;

	if ( (fd = socket(family, type, protocol)) < 0) 
	{
		DEBUG("socket error:%s\n", strerror(errno));
		return -1;
	}

    if(UN_SERVER == iMode)
    {
       char pPath[128];
	if(iModuleid == MODULE_HOSTID)
	{
		strcpy(pPath,UN_SERVER_PATH);
	}
	else
	{
		sprintf(pPath,"%s%03d",UN_CLIENT_PATH,iModuleid);
	}
	//printf("create socket service %s\n",pPath);
    	unlink(pPath);
    	bzero(&saddr, sizeof(struct sockaddr_un));
    	saddr.sun_family = family;
    	strcpy(saddr.sun_path, pPath);
    	if (bind(fd, (struct sockaddr *) &saddr, sizeof(saddr)) < 0) 
	{
    		DEBUG("bind path:%s, error:%s\n", pPath, strerror(errno));
			goto CloseSock;
    	}
    }

	/*显式设置socket为非阻塞，不同平台默认是否阻塞也不同*/
	if(0 > (flags=fcntl(fd, F_GETFL, 0)))
	{
	    DEBUG("Failed to F_GETFL, error:%s\n", strerror(errno));
	    goto CloseSock;
	}
	if(0 > fcntl(fd, F_SETFL, flags|O_NONBLOCK))
	{
	    DEBUG("Failed to F_SETFL, error:%s\n", strerror(errno));
	    goto CloseSock;
	}

	return fd;
CloseSock:;
    UnSocketDestroy(fd, iModuleid, iMode);
    return -1;
}
/***************************************************************************************
*
* 函数介绍：    UnSocketDestroy，销毁unix域的udp_socket
* 输入参数说明：
*   		   iFd     --socket的fd
*               iModuleid    --为模块id
*               iMode   --为0时即接收端，1为发送端
* 返回值：      成功返回0，失败返回-1 
****************************************************************************************
*/
void UnSocketDestroy(int iFd, int iModuleid, UN_MDL_TYPE_EN iMode)
{
	if(iFd <= 0)
	{
		return ;
	}
	close(iFd);
	if((UN_SERVER == iMode))
	{
		char pPath[128];
		if(iModuleid == MODULE_HOSTID)
		{
			strcpy(pPath,UN_SERVER_PATH);
		}
		else
		{
			sprintf(pPath,"%s%03d",UN_CLIENT_PATH,iModuleid);
		}
	    unlink(pPath);
	}
}

/***************************************************************************************
*
* 函数介绍：    UnRecvFrom，本地socket接收
* 输入参数说明：
*		   iFd:	为模块id
*               pBuf:	发送数据指针
*               nBytes :	数据长度
* 返回值：      成功返回接收的字节数，超时或者EINTR返回0, 失败返回-1 
****************************************************************************************
*/
int UnRecvFrom(int iFd, void *pBuf, size_t nBytes)
{
    struct sockaddr_un from;
    socklen_t addrlen = sizeof(struct sockaddr_un);
    int iRet = -1;
    int iCnt = 0;       /*重收计数*/
    fd_set iBits;
    struct timeval tout;

recvAgain:
    FD_ZERO(&iBits);
    FD_SET(iFd, &iBits);
    tout.tv_sec = UNSOCK_TIMEOUT;
    tout.tv_usec = 100*1000;
    iRet = select(iFd+1, &iBits, NULL, NULL, &tout);
    if(0 >= iRet)
    {
        return (errno==EINTR) ? 0 : iRet;       /*EINTR错误可接受错误，和超时一样返回0*/
    }

    if(!FD_ISSET(iFd, &iBits))
    {
        return 0;
    }
    
    iRet = recvfrom(iFd, pBuf, nBytes, 0, (struct sockaddr*)&from, &addrlen);
    if(0>iRet)
    {
        if (((errno == EINTR)||(errno == EAGAIN)) && (iCnt<UNSOCK_LOOP_MAX))
        {
            usleep(UNSOCK_AGAIN_TV);
            iCnt ++;
            goto recvAgain;
        }
    }
     //DEBUG("ret:%d,cnt:%d\n",iRet,iCnt);
	return iRet;  
}

/***************************************************************************************
*
* 函数介绍：    UnSendTo，本地socket接收
* 输入参数说明：
*		   iFd :		为模块id
*               pBuf:		发送数据指针
*               nBytes:	数据长度
*               iModuleid:	目标地址索引
* 返回值：      成功返回发送的字节数，失败返回-1，超时或者EINTR返回0 
****************************************************************************************
*/
int UnSendTo(int iFd, const void *pBuf, size_t nbytes, int iDst)
{
    socklen_t addrlen = sizeof(struct sockaddr_un);
    struct sockaddr_un to;
    int iRet = -1;
    int iCnt = 0;       /*重发计数*/

    fd_set oBits;
    struct timeval tout;
	
    if((0>iFd)  ||(NULL==pBuf) || (0>=nbytes))
    {
        DEBUG("Failed to do UnSendTo, input args invalid\n");
        return iRet;
    }

	to.sun_family = AF_LOCAL;
	if(iDst == MODULE_HOSTID)
	{
		strcpy(to.sun_path,UN_SERVER_PATH);
	}
	else
	{
		sprintf(to.sun_path,"%s%03d",UN_CLIENT_PATH,iDst);
	}

sendAgain:;
    FD_ZERO(&oBits);
    FD_SET(iFd, &oBits);
    tout.tv_sec = UNSOCK_TIMEOUT;
    tout.tv_usec = 0;

    iRet = select(iFd+1, NULL, &oBits, NULL, &tout);
    if(0 >= iRet)
    {
    	 DEBUG("send socket select ret:%d\n",iRet);
        return (errno==EINTR) ? 0 : iRet;       /*EINTR错误可接受错误，和超时一样返回0*/
    }
    if(!FD_ISSET(iFd, &oBits))
    {
    	 DEBUG("socket %s can not write\n",to.sun_path);
        return 0;
    }
    iRet = sendto(iFd, pBuf, nbytes, 0, (struct sockaddr *) &to, addrlen);
    if(0>iRet)
    {
        if (((errno == EINTR)||(errno == EAGAIN)) && (iCnt<UNSOCK_LOOP_MAX))
        {
            usleep(UNSOCK_AGAIN_TV);
            iCnt ++;
            goto sendAgain;
        }
	//perror("UnSendTo failed once\n");
    }
    //printf("send result:%d\n",iRet);
    return iRet;
}

#ifdef __cplusplus
}
#endif

