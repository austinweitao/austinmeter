#include <stdio.h>
#include "libsocket.h"


enum MsgType
{
	MSG_SBS_FTP_TEST=0,
    MSG_SBS_METER_STATUS,
    MSG_SBS_REBOOT,
    MSG_SBS_TEST,
};

int meterStatus[32]={0};
//CallBack

int SBS_MsgProc(int SrcModuleID,int MsgType,int wParam,int lParam,char* StringParam,int len)
{
	int iret = 0;
	printf("SrcModuleID=%d MessageID=%d wParam=%d lParam=%d StringParam=%s len=%d\n",
		SrcModuleID, MsgType, wParam, lParam, StringParam, len);
    switch(MsgType)
	{
        case MSG_SBS_REBOOT:
            //重启服务
            break;
        default:
            break;
    }

    return iret;
}

int SBS_GetValue_Proc(int SrcModuleID, int MessageID,int *param1,int *param2,char** str, int *len)
{
	printf("SrcModuleID=%d MessageID=%d\n ", SrcModuleID, MessageID);

    switch(MessageID)
	{
        case MSG_SBS_FTP_TEST:
            // 1表示测试成功，0表示测试失败
            *param1 = 1;
            usleep(5*1000*1000);
            break; 
        case MSG_SBS_METER_STATUS:
            {
                char tmp[32]={0};
                int i;
                for(i=0; i<32; i++)
                {
                    tmp[i] = meterStatus[i] ? '1':'0';
                }
    			memcpy(str, tmp, strlen(tmp));
    			*len = 32;
            }
            break;
        case MSG_SBS_TEST://仅测试用
        {
            //char info[256]={0};
            char *info = "1,100,200,300,400,500,600,700,2,1000,2000,3000,4000,5000,6000,7000";
            memcpy(str, info, strlen(info));
            *len = strlen(len);
        }
            break;
        default:
            break;
    }
    
    return 0;
}

void main()
{
	printf("sbs server\n");

	/*init socket file and handle*/
	Init(MODULE_SERVER);
	FuncHostCallback FC;
	FC.pSBS_MsgProc=SBS_MsgProc;
    FC.pSBS_GetValue=SBS_GetValue_Proc;
	/*register call back func*/
	RegisterHostCallBack(FC);

	while(1)
	{
		usleep(10*1000);
	}
	
	Clear(MODULE_SERVER);
	
}



