#ifndef _SBS_SOCK_INTERFACE_

#define _SBS_SOCK_INTERFACE_
#include <stdarg.h>
#include "unsock.h"


#define MAX_LENGTH	1024

#define MODULE_SERVER 	0
#define MODULE_CLIENT	1
#define MODULE_TEST	2

int Register(int ModuleID);	
int UnRegister(int ModuleID);
int Init(int ModuleID);
int Clear(int ModuleID);
int SBS_Init();
int SBS_Close();

int SBS_CaptureReboot();
int SBS_GetFtpTestResult(int *result);
int SBS_GetMeterStatus(char** status, int buf_size);
int SBS_GetMeterInfo(char** info, int buf_size);

#define CALLBACK 

typedef int (CALLBACK* pFuncSBS_GetHarddiskStorageStatus)(int total, int use);
typedef int (CALLBACK* pFuncSBS_VideoSignalAlarm)(int channel);
typedef int (CALLBACK* pFuncSBS_VideoCoverAlarm)(int channel);
typedef int (CALLBACK* pFuncSBS_GetPicture)(char* path, int len);
typedef int (CALLBACK* pFuncSBS_GetVideo)(char* path, int len);


typedef int (CALLBACK* pFuncSBS_MsgProc)(int SrcModuleID,int MessageID,int wParam,int lParam,char* StringParam,int len);

typedef int (CALLBACK* pFuncSBS_GetValue)(int SrcModuleID,int MessageID,int *param1,int *param2,char** StringParam,int *len);

typedef int (CALLBACK* pFuncSBS_ReportHarddiskWriteError)(int errorType);

typedef struct _FuncClientCallback
{
	//pFuncSBS_GetPicture pSBS_GetPicture;
	//pFuncSBS_GetVideo pSBS_GetVideo;
	pFuncSBS_ReportHarddiskWriteError pSBS_ReportHarddiskWriteError;
}FuncClientCallback;
int RegisterClientCallBack(FuncClientCallback FC);

/*******************************below just for service*************************************/ 


typedef int (CALLBACK* pFuncSBS_Register)(int ModuleID);
typedef int (CALLBACK* pFuncSBS_UnRegister)(int ModuleID);

typedef struct _FuncHostCallback
{
	pFuncSBS_Register pSBS_Register;
	pFuncSBS_UnRegister pSBS_UnRegister;
	pFuncSBS_MsgProc pSBS_MsgProc;
	pFuncSBS_GetValue pSBS_GetValue;
}FuncHostCallback;
int RegisterHostCallBack(FuncHostCallback FC);

int PostMessage(int DstModule,int MsgID,int Param1_SrcModule,int Param2_MsgType,int Param3_wParam,int Param4_lParam,int Param5,char *Param6,int len);
int SendMessage(int DstModule,int  MsgID,int Param1_SrcModule,int Param2_MsgType,int Param3_wParam,int Param4_lParam,int Param5,char *Param6,int len);

//可以返回参数，客户端直接获取返回值
int GetValue(int DstModule,int  MsgID,int SrcModule,int MsgType,int *Param1,int *Param2,char **str, int buf_size);

#endif 

