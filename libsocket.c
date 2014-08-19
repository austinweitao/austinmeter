
/* Standard Linux headers */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/un.h>
#include <pthread.h>
#include <arpa/inet.h>


#include "libsocket.h"

#define	SOCKET_VERSION		"1.0.0"

int meterStatus[32]={0};

enum  MessageID
{
	MSG_ECHO = 0,/**/
	MSG_RESPONSE,
	
	/*client function*/
	MSG_HARD_DISK_WRITE_ERROR,/*2*/	
	MSG_GET_PICTURE,
	MSG_GET_VIDEO,
	MSG_SYSTEM_ERROR,

	/*host function*/
	MSG_REGISTER,		//6
	MSG_UNREGISTER,
	MSG_PROC,
	MSG_GET_VALUE,
	MSG_MAX_RESERVED,
};

enum MsgType
{
	MSG_SBS_FTP_TEST=0,
    MSG_SBS_METER_STATUS,
    MSG_SBS_REBOOT,
    MSG_SBS_TEST,
    MSG_SBS_QUERY,
};

/*module operation by showmodule(...) EventNotify*/
enum {
	OP_MODULE_OPEN = 0,
	OP_MODULE_CLOSE,
	OP_MODULE_CHANGE,
	NOTIFY_OPEN,
	NOTIFY_CLOSE,
};

#define RECV_BUF_LEN	1252
/*ui的本地发送socket句柄，在消息接受线程里创建*/
static int sg_hSendFd = -999;
static int sg_moduleid  = -1;
static int sg_quit = 0;
static pthread_t  sg_thread;

static unsigned short	sg_msgid = 0;
static int				sg_sendermsgid[2];//msgid/result
static pthread_mutex_t	sg_sendermutex;/*线程安全锁*/

//增加发送带返回参数的函数，这里是全局变量
static int 	g_iSendResult[2];
char	 g_sSendResult[MAX_LENGTH];
static int g_iSendResultLen;

struct _FuncClientCallback FunctionClientCall;
struct _FuncHostCallback FunctionHostCall;

/******************************************************************************************************
* 函数介绍：    GetTimeTick 获取系统启动或重设时间以来的时钟数 以10ms为单位
* 输入参数说明：flush--刷新起始时间
* 输出参数说明：无
* 返回值：      时钟数 以10ms为单位
*******************************************************************************************************
*/
static unsigned long GetTimeTick(void)
{
	unsigned long uptime = 0;
	double		updata;
	char		genbuf[128];
	FILE		*fp = NULL;
	genbuf[0] = 0;
	if((fp=fopen("/proc/uptime", "r"))!=NULL)
	{
		fgets(genbuf, 128, fp);
		updata = atof(genbuf);
		uptime = updata*100;
		//printf("%lf,%d\n",updata,uptime);
		fclose(fp);
	}
	return uptime;
}

void* MsgReceive(void *pEnv);


char	sg_logname[64];
int Init(int ModuleID)
{
    
	//初始化日志库
	//if (ModuleID == MODULE_HOSTID)
	//{
		if(LogInit( LOG_PATH ,LOG_PREFIX,LOG_SIZE,LOG_FILE_CNT,LOG_LEVERL,LOG_DIRECT))
	    	{
	        		return -1;
		}
	//}
	
	//sprintf(sg_logname,"/var/libsocket%d.log",sg_moduleid);
	//DEBUG("create socket %d\n",ModuleID);
	sg_moduleid = ModuleID;
	pthread_mutex_init(&sg_sendermutex, NULL);
	//SKTDBGMSG("socket lib version:%s\n",SOCKET_VERSION);
	if (pthread_create(&sg_thread,NULL,
			MsgReceive,NULL)== -1) 
	{
		DEBUG("Failed to create receive thread\n");
		return -1;
	}
	/*wait for thread created*/
	while(sg_hSendFd == -999)
	{
		usleep(10000);
	}
	//DEBUG("create socket %d lib success\n",ModuleID);
	sg_quit = 0;
	return 0;
}

int Clear(int ModuleID)
{
	void *thread_return;
	sg_quit = 1;
	pthread_mutex_destroy(&sg_sendermutex);
	pthread_join(sg_thread,&thread_return);

	//if (ModuleID == MODULE_HOSTID)
	//{
		LogDestroy();
	//}
	
	return 0;
}

/******************************************************************************************************
*
* 函数介绍：    PostMessage 发送数据包，不等待返回值，立即返回。
* 输入参数说明：
* 输出参数说明：无
* 返回值：      无
*******************************************************************************************************
*/
int PostMessage(int ToModule,int Appid,int Param1,int Param2,int Param3,int Param4,int Param5,char *Param6,int len)
{
	if (Param6 != NULL)
		DEBUG("dst=%d response=%d, msgid=%d, ret=%d, %d, %d, %d, %s,%d\n", ToModule, Appid, Param1,Param2,Param3,Param4,Param5,Param6,len);
	else
		DEBUG("dst=%d response=%d, msgid=%d, ret=%d, %d, %d, %d\n", ToModule, Appid, Param1,Param2,Param3,Param4,Param5);
	
	struct sockaddr_un address; 
	char szbuffer[RECV_BUF_LEN];
	
	memset(szbuffer,0,sizeof(szbuffer));
	MSG_HEAD	*MsgHead = (MSG_HEAD *)szbuffer;
	MSG_BODY	*MsgBody = (MSG_BODY *)(szbuffer+HEAD_LEN);
	MsgHead->magic = MSG_MAGIC;
	MsgHead->srcmodule = sg_moduleid;
	MsgHead->msgid = ++sg_msgid;
	MsgHead->appid = Appid;
	MsgHead->len = 0;
	
	MsgBody->iParam[0] = Param1;
	MsgBody->iParam[1] = Param2;
	MsgBody->iParam[2] = Param3;
	MsgBody->iParam[3] = Param4;
	MsgBody->iParam[4] = Param5;
	if(Param6 != NULL)
	{
		//char str[128];
		if(len==0)
			len = strlen(Param6);
		if(len > MSG_STRING_LEN)
			len = MSG_STRING_LEN;
		memcpy(MsgBody->sParam,Param6,len);
		MsgHead->len += len;
		/*strcpy(str,MsgBody->sParam);
		printf("PostMessage 0x %x,%x,%x,%x,%x,%x,%x\n",str[0],str[1],str[2],str[3],str[4],str[5],str[6]);*/
	}
	MsgHead->len += BODY_INT_LEN ;
	int	i;
	//sktDebugMsg("post to %d app:%d len:%d\n",ToModule,Appid,len);
	//for(i=0;i<MsgHead->len+HEAD_LEN;i++)
	//	printf("%x.",szbuffer[i]);
	//sktDebugMsg("\n");
	//printf("send len:%d\n",MsgHead->len);
	return UnSendTo(sg_hSendFd,szbuffer,MsgHead->len+HEAD_LEN, ToModule);
}
/******************************************************************************************************
*
* 函数介绍：    SendMessage 发送数据包，并等待返回值,2秒超时。
* 输入参数说明：
* 输出参数说明：无
* 返回值：      执行结果
*******************************************************************************************************
*/
int SendMessage(int ToModule,int Appid,int Param1,int Param2,int Param3,int Param4,int Param5,
					char *Param6,int len)
{
	if (Param6 != NULL)
		DEBUG("dst=%d type=%d, src=%d, cmd=%d, %d, %d, %d, %s\n", ToModule, Appid, Param1,Param2,Param3,Param4,Param5,Param6);
	else
		DEBUG("dst=%d type=%d, src=%d, cmd=%d, %d, %d, %d\n", ToModule, Appid, Param1,Param2,Param3,Param4,Param5);

	struct sockaddr_un address; 
	char szbuffer[RECV_BUF_LEN];
	int		iret = 0;
	memset(szbuffer,0,sizeof(szbuffer));
	MSG_HEAD	*MsgHead = (MSG_HEAD *)szbuffer;
	MSG_BODY	*MsgBody = (MSG_BODY *)(szbuffer+HEAD_LEN);
	MsgHead->magic = MSG_MAGIC;
	MsgHead->srcmodule = sg_moduleid;
	MsgHead->msgid = ++sg_msgid;
	MsgHead->appid = Appid;
	MsgHead->len = 0;
	
	MsgBody->iParam[0] = Param1;
	MsgBody->iParam[1] = Param2;
	MsgBody->iParam[2] = Param3;
	MsgBody->iParam[3] = Param4;
	MsgBody->iParam[4] = Param5;
	if(Param6 != NULL)
	{
		//char str[128];
		if(len==0)
			len = strlen(Param6);
		if(len > MSG_STRING_LEN)
			len = MSG_STRING_LEN;
		memcpy(MsgBody->sParam,Param6,len);
		MsgHead->len += len;
		/*strcpy(str,MsgBody->sParam);
		printf("PostMessage 0x %x,%x,%x,%x,%x,%x,%x\n",str[0],str[1],str[2],str[3],str[4],str[5],str[6]);*/
	}
	//printf("send msg id:%d\n",MsgHead->msgid);
	MsgHead->len += BODY_INT_LEN ;
	pthread_mutex_lock(&sg_sendermutex);
	sg_sendermsgid[0] = MsgHead->msgid;
	//sktDebugMsg("send to %d app:%d len:%d\n",ToModule,Appid,len);
	iret = UnSendTo(sg_hSendFd,szbuffer,MsgHead->len+HEAD_LEN, ToModule);
	if(iret > 0)
	{
		unsigned long iEndClock =0,iCurClock = 0;
		iEndClock= GetTimeTick() + ONE_SECOND*2;
		iCurClock = GetTimeTick();
		while (!sg_quit && TimeAfterEq(iEndClock,iCurClock))
		{
			if(MsgHead->msgid != sg_sendermsgid[0])
				break;
			usleep(1000);
			iCurClock = GetTimeTick();
		}
		iret = sg_sendermsgid[1];
		sg_sendermsgid[0] = -1;
		sg_sendermsgid[1] = -1;
	}
	pthread_mutex_unlock(&sg_sendermutex);
	return iret;
}

/******************************************************************************************************
*
* 函数介绍：    GetValue,从服务端获取值
* 输入参数说明：
* 输出参数说明：无
* 返回值：      执行结果
*******************************************************************************************************
*/
int GetValue(int DstModule,int MsgID,int SrcModule,int MsgType,int *Param1,int *Param2,
			     char **str, int buf_size)
{
	DEBUG("DstModule=%d type=%d, SrcModule=%d, cmd=%d\n", DstModule, MsgID, SrcModule,MsgType);
	
	struct sockaddr_un address; 
	char szbuffer[RECV_BUF_LEN];
	int 	iret = 0;
	memset(szbuffer,0,sizeof(szbuffer));
	MSG_HEAD	*MsgHead = (MSG_HEAD *)szbuffer;
	MSG_BODY	*MsgBody = (MSG_BODY *)(szbuffer+HEAD_LEN);
	MsgHead->magic = MSG_MAGIC;
	MsgHead->srcmodule = sg_moduleid;
	MsgHead->msgid = ++sg_msgid;
	MsgHead->appid = MsgID;
	MsgHead->len = 0;
	
	MsgBody->iParam[0] = SrcModule;
	MsgBody->iParam[1] = MsgType;
	MsgBody->iParam[2] = 0;
	MsgBody->iParam[3] = 0;
	MsgBody->iParam[4] = 0;
	 
	//printf("send msg id:%d\n",MsgHead->msgid);
	MsgHead->len += BODY_INT_LEN ;
	pthread_mutex_lock(&sg_sendermutex);
	sg_sendermsgid[0] = MsgHead->msgid;
	//sktDebugMsg("send to %d app:%d len:%d\n",ToModule,Appid,len);
	iret = UnSendTo(sg_hSendFd,szbuffer,MsgHead->len+HEAD_LEN, DstModule);
	if(iret > 0)
	{
		unsigned long iEndClock =0,iCurClock = 0;
		iEndClock= GetTimeTick() + ONE_SECOND*2;
        
		if (MsgType == MSG_SBS_FTP_TEST)
		{
			iEndClock= GetTimeTick() + ONE_SECOND*20;
		}

        if (MsgType == MSG_SBS_METER_STATUS)
		{
			iEndClock= GetTimeTick() + ONE_SECOND*50;
		}
		
		iCurClock = GetTimeTick();
		while (!sg_quit && TimeAfterEq(iEndClock,iCurClock))
		{
			if(MsgHead->msgid != sg_sendermsgid[0])
			{
				DEBUG("get response\n");
				break;
			}
			usleep(1000);
			iCurClock = GetTimeTick();
		}
		iret = sg_sendermsgid[1];
		
#if 1	//debug
	DEBUG("g_iSendResult[0]=%d g_iSendResult[1]=%d g_sSendResult = %s \n",g_iSendResult[0],g_iSendResult[1], g_sSendResult);
#endif

		//返回参数值
		if (Param1 != NULL){
			*Param1 = g_iSendResult[0];
		}
		if (Param2 != NULL){
			*Param2 = g_iSendResult[1];
		}
		if (str != NULL){
			DEBUG("user buf size = %d, result size = %d\n",buf_size, g_iSendResultLen);
			if (buf_size >= strlen(g_sSendResult))
			{
				memcpy(str, g_sSendResult, g_iSendResultLen);
				iret = g_iSendResultLen;//如果是获取字符串的函数，返回字符串长度
			}
			else
			{
				iret = -1;
			}
		}
	}
	pthread_mutex_unlock(&sg_sendermutex);

	//clear
	sg_sendermsgid[0] = -1;
	sg_sendermsgid[1] = -1;
	g_iSendResult[0] = -1;
	g_iSendResult[1] = -1;
	if (g_sSendResult != NULL)
		memset(g_sSendResult, 0, MAX_LENGTH);
	
	return iret;
}


/******************************************************************************************************
*
* 函数介绍：    getResponse 获取响应包
* 输入参数说明：
* 输出参数说明：无
* 返回值：      执行结果
*******************************************************************************************************
*/
int getResponse(char* buffer,int len)
{
	int	iret = -1;
	if(buffer == NULL)
	{
		return iret;
	}
	MSG_BODY	*MsgBody = (MSG_BODY *)(buffer);
	if(MsgBody == NULL){
		return iret;
	}

	//DEBUG("MsgBody->iParam[2]  = %d MsgBody->iParam[3] =%d MsgBody->iParam[4] = %d, MsgBody->sParam = %s\n", 
		//MsgBody->iParam[2],MsgBody->iParam[3],MsgBody->iParam[4] ,MsgBody->sParam);
	if (MsgBody->iParam[2] == MSG_GET_VALUE)
	{
		g_iSendResult[0]=-1;
		g_iSendResult[1]=-1;
		if (g_sSendResult != NULL)
			memset(g_sSendResult, 0, MAX_LENGTH);
		
		g_iSendResult[0]=MsgBody->iParam[3];
		g_iSendResult[1]=MsgBody->iParam[4];
		g_iSendResultLen = len -BODY_INT_LEN;

		if (MsgBody->sParam != NULL)
			memcpy(g_sSendResult, MsgBody->sParam, (len -BODY_INT_LEN));
	}

	if(MsgBody->iParam[0] == sg_sendermsgid[0])
	{
		sg_sendermsgid[0] = -1;
		sg_sendermsgid[1] = MsgBody->iParam[1];
	}
	
	if(MsgBody->iParam[1] < 0)
	{/*if failed then send rspone to sender*/
		//PostLog(1,"socketlib:%d,op:%d,ret:%d\n",MsgBody->iParam[2],MsgBody->iParam[4],
		//	MsgBody->iParam[1],MsgBody);
		/*if(MsgBody->iParam[2] == MSG_SHOW_MOD){
			if(MsgBody->iParam[4] == OP_MODULE_OPEN)
				FunctionClientCall.pActive(MsgBody->iParam[1]);
			else
				FunctionClientCall.pDeactive(MsgBody->iParam[1]);
		}else if(MsgBody->iParam[2] == MSG_LISTEN_MOD){
			if(MsgBody->iParam[4] == OP_MODULE_OPEN)
				FunctionClientCall.pSwitchOn(MsgBody->iParam[1]);
			else
				FunctionClientCall.pSwitchOff(MsgBody->iParam[1]);
		}*/
	}
	return iret;
}

int Register(int ModuleID)
{
	return PostMessage(MODULE_HOSTID,MSG_REGISTER,ModuleID,0,0,0,0,NULL,0);
}
int UnRegister (int ModuleID)
{
	return PostMessage(MODULE_HOSTID,MSG_UNREGISTER,ModuleID,0,0,0,0,NULL,0);
}

int callClientFunc(int nApiID, MSG_BODY *MsgBody,int len)
{
	int	iret = -1;
	if(MsgBody == NULL){
		return -1;
	}
	/*printf("receive appid:%d,param:%d,%d,%d,%d\n",
		nApiID,MsgBody->iParam[0],MsgBody->iParam[1],MsgBody->iParam[2],MsgBody->iParam[3]);*/
	switch(nApiID)
	{
		case MSG_HARD_DISK_WRITE_ERROR:
		if (FunctionClientCall.pSBS_ReportHarddiskWriteError!= NULL)
		{
			iret = FunctionClientCall.pSBS_ReportHarddiskWriteError(MsgBody->iParam[0]);
		}		
		break;
#if 0			
		case MSG_SYSTEM_ERROR:
		if (FunctionClientCall.pSBS_VideoCoverAlarm!= NULL)
		{
			iret = FunctionClientCall.pSBS_VideoCoverAlarm(MsgBody->iParam[0]);
		}		
		break;	
		case MSG_GET_PICTURE:
		if (FunctionClientCall.pSBS_GetPicture!= NULL)
		{
			iret = FunctionClientCall.pSBS_GetPicture(MsgBody->sParam,len-BODY_INT_LEN);
		}		
		break;
		case MSG_GET_VIDEO:
		if (FunctionClientCall.pSBS_GetVideo!= NULL)
		{
			iret = FunctionClientCall.pSBS_GetVideo(MsgBody->sParam,len-BODY_INT_LEN);
		}		
		break;
#endif
		default:
		break;
		
	}
	return iret;
}

int callHostFunc(int nApiID,int fromModule,MSG_BODY	*MsgBody,int len)
{
	int	iret = -1;
	if(MsgBody == NULL){
		return -1;
	}
	//SKTDBGMSG("receive appid:%d from module:%d,param:%d,%d,%d,%d\n",
	//	nApiID,fromModule,MsgBody->iParam[0],MsgBody->iParam[1],MsgBody->iParam[2],MsgBody->iParam[3]);
	switch(nApiID)
	{
		case MSG_REGISTER:
		if (FunctionHostCall.pSBS_Register!= NULL)
		{
			iret = FunctionHostCall.pSBS_Register(MsgBody->iParam[0]);
		}
		break;
		case MSG_UNREGISTER:
		if (FunctionHostCall.pSBS_UnRegister!= NULL)
		{
			iret = FunctionHostCall.pSBS_UnRegister(MsgBody->iParam[0]);
		}		
		break;
		case MSG_PROC:
		if (FunctionHostCall.pSBS_MsgProc!= NULL)
		{
			iret = FunctionHostCall.pSBS_MsgProc(MsgBody->iParam[0],MsgBody->iParam[1],MsgBody->iParam[2],
				MsgBody->iParam[3],MsgBody->sParam,len-BODY_INT_LEN);
		}		
		break;
		case MSG_GET_VALUE:
		if (FunctionHostCall.pSBS_GetValue != NULL)
		{
			g_iSendResult[0] = -1;
			g_iSendResult[1] = -1;
			g_iSendResultLen = 0;
			if (g_sSendResult != NULL)
				memset(g_sSendResult, 0, MAX_LENGTH);
			
			iret = FunctionHostCall.pSBS_GetValue(MsgBody->iParam[0],MsgBody->iParam[1],
				&g_iSendResult[0],&g_iSendResult[1], &g_sSendResult, &g_iSendResultLen);
		}		
		break;
		default:
		break;
	}
	return iret;
}

/*********************************************************************************
* 函数介绍：   MsgReceive消息处理函数
* 输入参数说明：pEnv-- 线程上下文指针，用于接受到特定消息时进行页面的切换
* 输出参数说明：无
* 返回值：      成功返回0，失败返回-1 
****************************************************************************************
*/
void* MsgReceive(void *pEnv)
{	
	int iRecvFd = -1;
	int iRecvLen = -1,iret = 0;
	MSG_HEAD	*pMsgHead;
	//SKTDBGMSG("create \n");
	/*接受消息buffer，线程结束时销毁*/
	char *pData = (char *)malloc(RECV_BUF_LEN);
	if(NULL == pData)
	{
		//SKTDBGMSG("Fail to malloc pData\n");
		goto cleanup;
	}    
	//pMsgHead->magic = MSG_MAGIC;
	/*创建ui本地接收socket*/
	iRecvFd = UnSocketBind(sg_moduleid, UN_SERVER);
	if(-1 == iRecvFd)
	{
	    //SKTDBGMSG("Faile to recv socket\n");
	    goto cleanup;
	}
	/*创建ui本地发送socket*/
	sg_hSendFd = UnSocketBind(sg_moduleid, UN_CLIENT);
	if(-1 == sg_hSendFd)
	{
	    DEBUG("Faile to send socket\n");
	    goto cleanup;
	}
	
    while(!sg_quit)
    {
		/*清空接受buffer*/
		bzero(pData, RECV_BUF_LEN);
		iRecvLen = UnRecvFrom(iRecvFd, pData, RECV_BUF_LEN);
		if(iRecvLen <= 0)
		{
			if(iRecvLen < 0)
			{
				DEBUG("recvfrom is return < 0\n");
				/*添加失败后重启本地socket的操作*/
				UnSocketDestroy(iRecvFd, sg_moduleid, UN_SERVER);
				iRecvFd = UnSocketBind(sg_moduleid, UN_SERVER);
				if(-1 == iRecvFd)
				{
					DEBUG("Faile to create recv socket\n");
					goto cleanup;
				}
				usleep(100*1000);
			}
	            /*超时了*/
		     	continue;
		}
		/*接受的消息长度必须大于等于IPCMSG_HEAD_LEN，至少要包含一个头部*/
		if(iRecvLen < HEAD_LEN)
		{
			DEBUG("recvfrom is return < HEAD_LEN");
			continue;
		}
		/*int	i;
		printf("receive\n");
		for(i=0;i<iRecvLen;i++)
			printf("%x.",pData[i]);
		printf("\n");*/
		
		/*解析消息头部*/
		pMsgHead = (MSG_HEAD*)pData;
		if(pMsgHead->magic != MSG_MAGIC && iRecvLen != (HEAD_LEN+pMsgHead->len))
		{
			DEBUG("pHead error  magic:%d,len:%d\n",pMsgHead->magic,pMsgHead->len);
			continue;
		}
		if(pMsgHead->appid == MSG_RESPONSE)
		{
			getResponse(pData+HEAD_LEN,pMsgHead->len);
			continue;
		}
		MSG_BODY	*MsgBody = (MSG_BODY *)(pData+HEAD_LEN);
		if(MsgBody == NULL){
			DEBUG("pMsgHead convert failed\n");
			continue;
		}
		/*if(pMsgHead->appid == MSG_ECHO)
		{
			continue;
		}*/
		//printf("=====ui recv , usMsgType:0x%x\n", pMsgHead->appid);
		if(pMsgHead->appid >= MSG_HARD_DISK_WRITE_ERROR  && pMsgHead->appid<=MSG_SYSTEM_ERROR)
		{
			iret = callClientFunc(pMsgHead->appid,MsgBody,pMsgHead->len);
		}
		//else if(pMsgHead->appid >= MSG_REGISTER  && pMsgHead->appid<MSG_MAX_RESERVED) 
		else if(pMsgHead->appid >= MSG_REGISTER  && pMsgHead->appid<MSG_MAX_RESERVED) 
		{
			iret = callHostFunc(pMsgHead->appid,pMsgHead->srcmodule,MsgBody,pMsgHead->len);
		}
		//response to sender module
		//(void)PostMessage(pMsgHead->srcmodule,MSG_RESPONSE,pMsgHead->msgid,iret,
			//pMsgHead->appid,MsgBody->iParam[0],MsgBody->iParam[2],NULL,0);
		(void)PostMessage(pMsgHead->srcmodule,MSG_RESPONSE,pMsgHead->msgid,iret,
			pMsgHead->appid,g_iSendResult[0],g_iSendResult[1], g_sSendResult, g_iSendResultLen);
    }
    
cleanup:
    if(pData != NULL)
    {
        free(pData);
    }
    UnSocketDestroy(iRecvFd, sg_moduleid, UN_SERVER);
    UnSocketDestroy(sg_hSendFd, sg_moduleid, UN_CLIENT);
    sg_hSendFd = -1;
    //SKTDBGMSG("#####################MsgProcManage exit .......\n");
    return NULL;
}




int RegisterClientCallBack(struct _FuncClientCallback FC)
{
	FunctionClientCall = FC;
}

int RegisterHostCallBack(struct _FuncHostCallback FC)
{
	FunctionHostCall = FC;
}

//return value
//-1: failed 0:success
int SBS_Init()
{
	//int ret = Init(MODULE_CLIENT);
	int ret = Init(MODULE_CLIENT);
	return ret;
}

int SBS_Close()
{
	//int ret = Clear(MODULE_CLIENT);
	int ret = Clear(MODULE_CLIENT);
	return ret;
}

int SBS_CaptureReboot()
{
	int ret = SendMessage(MODULE_SERVER, MSG_PROC, MODULE_CLIENT, 
		MSG_SBS_REBOOT, 0, 0, 0, NULL, 0);
	
	usleep(1*1000*1000);// 1 S
	
	if (ret == -1)
		return -1;
	else
		return 0;
}

int SBS_GetFtpTestResult(char** status, int buf_size)
{
	if (status == NULL)
		return -1;
	
	int ret;
	ret = GetValue(MODULE_SERVER,MSG_GET_VALUE,MODULE_CLIENT,
				MSG_SBS_FTP_TEST,NULL,NULL,status, buf_size);

    if (ret == -1)
	{
		return -1;
	}
	else 
	{
		return ret;
	}
}

int SBS_GetMeterStatus(char** status, int buf_size)
{
	if (status == NULL)
		return -1;
	
	int ret;
	ret = GetValue(MODULE_SERVER,MSG_GET_VALUE,MODULE_CLIENT,
				MSG_SBS_METER_STATUS,NULL,NULL,status, buf_size);

    if (ret == -1)
		return -1;
	else 
		return ret;
}

int SBS_GetMeterInfo(char** info, int buf_size)
{
	if (info == NULL)
		return -1;
	
	int ret;
	ret = GetValue(MODULE_SERVER,MSG_GET_VALUE,MODULE_CLIENT,
				MSG_SBS_TEST,NULL,NULL, info, buf_size);
    
	if (ret == -1)
		return -1;
	else 
		return ret;
}

/*
#ifdef __cplusplus
}
#endif
*/

