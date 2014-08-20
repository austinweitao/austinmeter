#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>

#include "sll.h"
#include <errno.h>
#include <modbus/modbus.h>

#include "unit-test.h"
#include <curl/curl.h>
#include <uci.h>
#include "libsocket.h"
#include "zlog.h"

#define	    xml_header		"<?xml_version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\"?>\n<XML>\n  <action type=\"update\">"
#define	    xml_ender		"  </action>\n</XML>"
#define	    csv_header		"###ACTION:UPDATE ENTITY:MeterData SCHEMA:Default VERSION:1.0.0"
#define	    METER_FILE_LOCATION	"/meters/"
#define	    UPLOAD_FILE_AS	"while-uploading.txt"
//#define	    REMOTE_URL		"ftp://cwt:110weitao660@192.168.5.247:990/"  UPLOAD_FILE_AS
#define	    REMOTE_URL		"ftp://austin:weitao@192.168.5.247:990/"  UPLOAD_FILE_AS
#define	    CLOCKID CLOCK_REALTIME

#define	    USER_UCI_SAMPLE_INTERVAL	(10)
#define	    USER_UCI_UPLOAD_INTERVAL	(80)
#define	    SECSPERHOUR			 3600
#define	    SECSPERMIN			60	
#define	    INTERVAL_CMEP		"00000005"

#define	    METER_UCI_CONFIG_FILE		"/etc/config/meter"
#define	    GATEWAY_UCI_CONFIG_FILE		"/etc/config/gateway"
#define	    FTP_UCI_CONFIG_FILE			"/etc/config/ftp"
#define	    METER_LASTVALUE_UCI_CONFIG_FILE	"/etc/config/meter_lastvalue"

#define	    UCI_SAMPLE_INTERVAL		"gateway.general.sample_interval"
#define	    CUSTOM_UCI_SAMPLE_INTERVAL  "gateway.general.custom_sample_interval"
#define	    UCI_GATEWAY_TYPE		"gateway.general.gw_type"
#define	    UCI_FILE_PREFIX		"gateway.general.file_prefix"
#define	    UCI_V2_TYPE			"gateway.general.v2_type"
#define	    UCI_UPLOAD_INTERVAL		"ftp.general.ftp_upload_interval"
#define	    CUSTOM_UCI_UPLOAD_INTERVAL	"ftp.general.ftp_custom_upload_interval"
#define	    UCI_FTP_ADDR		"ftp.general.ftp_addr"
#define	    UCI_FTP_PORT		"ftp.general.ftp_port"
#define	    UCI_FTP_USER_NAME		"ftp.general.ftp_user_name"
#define	    UCI_FTP_PASSWORD		"ftp.general.ftp_password"
#define	    UCI_FTP_FILE_PATH		"ftp.general.ftp_file_path"
#define	    UCI_FTP_SSL			"ftp.general.ftp_ssl"
#define	    UCI_UART_DATABITS		"uart.general.data_bit"
#define	    UCI_UART_STOPBITS		"uart.general.stop_bit"
#define	    UCI_UART_BAUDRATE		"uart.general.baud"
#define	    UCI_UART_PARITY		"uart.general.parity"

#define SECSPERHOUR 3600
#define SECSPERMIN 60	
/*=====================defined struct================================*/
enum MsgType
{
    MSG_SBS_FTP_TEST=0,
    MSG_SBS_METER_STATUS,
    MSG_SBS_REBOOT,
    MSG_SBS_TEST,
    MSG_SBS_SET_QUERY_REG_CFG,
    MSG_SBS_GET_QUERY_REG_VALUE,
    MSG_SBS_FTP_UPLOAD,
};

typedef struct{
    char *file_path;
    char *file_name;
}Upload_Args;

union ATTRValue
{
    uint32_t value_uint32;
    int32_t value_int32_t;
    float value_float;
    int64_t value_int64_t;
};

typedef struct{
    int addr;	// meter attribute register start address
    int reg_num;	//meter attribute register number
    int constant;	//attribute value constant
    char* value_type;	//typde of the value: float,int,long{0,1,2}	
    char* value_unit;  //type of the value unit: KWh,KVarh,Volt
    char* total_diff;
    char* tagid;
}Meter_Attribute;

typedef struct{
    char *name;
    int modbus_id;
    int attr_num;
    Meter_Attribute *attribute;
    Meter_Attribute *current_attr;
    char *sender_id;
    char *receiver_id;
    char *customer_id;
    char *customer_name;
    char *account_id;
    char *account_name;
    char *meter_id;
    char *commodity;
    char *file_path;	//file path for sampling data file
    char *file_tmp_path;
    char *file_name;
    FILE * file;		//file descriptor for sampling data file
    FILE * file_tmp;		//file descriptor for sampling data file
}Meter;

typedef struct{
    int databits;
    int stopbits;
    int baudrate;
    char parity;
}UART_config;


typedef struct{
    char *ftp_addr;
    char *ftp_port;
    char *ftp_user_name;
    char *ftp_password;
    char *ftp_file_path;
    char *ftp_ssl;
}FTP_config;

typedef struct {
	int sample_interval;
	int upload_interval;
}Interval;

enum output_format
{
    cmep,
    xml,
    csv
};
/*===========================end======================================*/

/*=====================global shared resources=========================*/

static pid_t mainpid;
static struct uci_context * ctx = NULL; 
Sll *head = NULL;

enum output_format meter_opfm;
char output_file_prefix[64] = {0};

UART_config *uart_config;
FTP_config *ftp_config;
FTP_REMOTE_URL[128] = {0};
Interval interval;

pthread_mutex_t uart_mutex;

zlog_category_t *zlogcat;
/*===========================end=======================================*/


float modbus_get_float_cdab(uint16_t* value)
{
   float f;
   uint32_t i;
   uint16_t tmp[2];

   tmp[1] = value[0];
   tmp[0] = value[1];

   i = (((uint32_t)tmp[1]) << 16) + tmp[0];
   memcpy(&f,&i,sizeof(float));

   return f;
}

static int ftp_uci_get_option(char *option, char *value)
{
	int ret = UCI_OK;
	struct uci_ptr ptr_uci;
	struct uci_context *ctx_uci = NULL;

	ctx_uci = uci_alloc_context();

	if(!ctx_uci)
	{
	    zlog_error(zlogcat,"in ftp_uci_get_option:failed to alloc uci context:%s\n",option);
	    return -1;	    
	}

	if ((ret = uci_lookup_ptr(ctx_uci,&ptr_uci,option,true)) != UCI_OK) 
	{ 
	    zlog_error(zlogcat,"in ftp_uci_get_option:lookup_ptr failed:%s\n",option);
	    goto cleanup;
	}

	strcpy(value,ptr_uci.o->v.string);

cleanup:
    uci_free_context(ctx_uci);
    ctx_uci = NULL;
    return ret;
}


static void freeData(void **data)
{

    (void) fprintf(stderr,"in free data function.\n");
    
    Meter
        **addr=(Meter **) data;

    static int
        n=0;

    n++;
    if (*addr)
    {
	(void) fprintf(stderr,"Freeing Meter Attributes First.\n");
    int i;
	for(i = 0; i< (*addr)->attr_num; i++){
		if( (*addr)->attribute)
		{
			(void)fprintf(stderr,"Freeing value_type and value unit.\n");
			if((*addr)->attribute->value_type)
			{
				(void)free((char *)((*addr)->attribute->value_type));
			}
			if((*addr)->attribute->value_unit)
			{
				(void)free((char *)((*addr)->attribute->value_unit));
			}
			(void)fprintf(stderr,"Freeing attribute itself.\n");
			(void)free((Meter_Attribute *)(*addr)->attribute);
		}
	}
		(void) fprintf(stderr,"Freeing Meter.\n");
        (void) free((Meter *) (*addr));
        (*addr)=NULL;
    }
	printf("\n");
}



static int uci_set_option(struct uci_context *ctx_uci, char *string)
{
	int ret = UCI_OK;
	struct uci_ptr ptr;

	zlog_debug(zlogcat,"uci_set_option:string is %s.\n",string);

	if ((ret = uci_lookup_ptr(ctx_uci, &ptr,string, true)) != UCI_OK) 
	{ 
	    zlog_error(zlogcat,"in uci_set_option:lookup_ptr failed:%s\n",string);
	    return ret;
	}
	ret = uci_set(ctx_uci, &ptr);
	if (ret != UCI_OK){
		zlog_error(zlogcat,"in uci_set_option:uci_set failed:%s\n",string);
		return ret;
	}
	if ( UCI_OK != (ret = uci_commit(ctx_uci, &ptr.p, false))) {
	    zlog_error(zlogcat,"in uci_set_option:uci_commit failed:%s\n",string);
	    return ret;
	}
	return ret;

}

static int uci_do_add(struct uci_context *ctx,struct uci_package *pkg,char *section_type,Meter *meter)
{
	struct uci_section *s = NULL;
	int ret = UCI_OK;
	struct uci_ptr ptr;
	struct uci_element *e = NULL;

	ret = uci_add_section(ctx, pkg, section_type, &s);

	if (ret != UCI_OK){
		zlog_error(zlogcat,"add  section failed.\n");
		return ret;
	}
	zlog_debug(zlogcat,"section's name is %s.\n",s->e.name);
	char ptr_str[64] = {0};
	sprintf(ptr_str,"meter_lastvalue.%s",s->e.name);

	if ( (ret = uci_lookup_ptr(ctx, &ptr, ptr_str, true)) != UCI_OK) 
	{ 
	    zlog_error(zlogcat,"in uci_do_add:uci_lookup_ptr failed:%s.\n",ptr_str);
	    return ret;
	}
	zlog_debug(zlogcat,"ptr's package is %s.\n",ptr.package);
	zlog_debug(zlogcat,"ptr's section is %s.\n",ptr.section);
	zlog_debug(zlogcat,"ptr's option is %s.\n",ptr.option);

	ptr.value = strdup(meter->name);

	if ((ret = uci_rename(ctx,&ptr) != UCI_OK))
	{
	    zlog_error(zlogcat,"in uci_do_add uci_rename failed.\n");
	    return ret;
	}

	sprintf(ptr_str,"meter_lastvalue.%s.modbus_id=%d",s->e.name,meter->modbus_id);
	zlog_debug(zlogcat,"ptr_str is %s.\n",ptr_str);

	if( ret = uci_set_option(ctx,ptr_str) != UCI_OK){
		zlog_error(zlogcat,"in uci_do_add: uci_set_option failed:%s.\n",ptr_str);
		return ret;
	}

	int i;
	Meter_Attribute *attribute = meter->attribute;

	for(i = 0; i < meter->attr_num; i++,attribute++)
	{
	    sprintf(ptr_str,"meter_lastvalue.%s.%d=%d",s->e.name,attribute->addr,0);
	    zlog_debug(zlogcat,"ptr_str is %s.\n",ptr_str);

	    if( ret = uci_set_option(ctx,ptr_str) != UCI_OK){
		zlog_error(stderr,"uci_set_option failed:%s.\n",ptr_str);
		return ret;
	    }
	
	}

	return ret;
}
int load_meter_lastvalue_config()
{
    Sll
        *lp,
        *new=NULL;
    Meter
        *meter;

    int
        n=0;


    struct uci_package * pkg = NULL;
    struct uci_element *e;
    struct uci_context *ctx_uci = NULL;
    char *tmp;
    const char *value;
    int modbus_id;
    struct uci_section *s;
    int ret = UCI_OK;


    ctx_uci = uci_alloc_context(); 
    if (UCI_OK != (ret = uci_load(ctx_uci, METER_LASTVALUE_UCI_CONFIG_FILE, &pkg)))
        goto cleanup; 

    for(lp=head;lp;lp=lp->next)
    {
	meter = (Meter *)lp->data;
	int flag = 0;
	uci_foreach_element(&pkg->sections, e)
	{
	    s = uci_to_section(e);
	

	    if(!strcmp("meter_lastvalue",s->type))
	    {
		zlog_debug(zlogcat,"section s's type is meter_lastvalue\n");
		if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "modbus_id"))) 
		{
		    modbus_id = atoi(value);
		    if ( meter->modbus_id ==  modbus_id){
			flag = 1; 
			zlog_debug(zlogcat,"found a section belonging to the meter.\n");
		    }
		}
					
	    }
	}
	if(flag == 0)   //no seciton belongs to the current meter
	{
	    zlog_debug(zlogcat,"currently no section belongs to the meter, create a new section.\n");
	    if ( UCI_OK != (ret = uci_do_add(ctx_uci,pkg,"meter_lastvalue",meter)))
	    {
		zlog_error(zlogcat,"in load_meter_lastvalue: uci do add failed.\n");
		goto cleanup;
	    }
	}
    }

cleanup:
    uci_free_context(ctx_uci);
    ctx_uci = NULL;
    return ret;
}
int load_attr_config(char *meter_uci_config_file)
{
    struct uci_package * pkg = NULL;
    struct uci_element *e;
    struct uci_context *ctx_uci = NULL;
    const char *value;
    int modbus_id;
    Sll *lp;
    Meter *meter;
    int ret = UCI_OK;


    ctx_uci = uci_alloc_context(); 
    if (UCI_OK != uci_load(ctx_uci, meter_uci_config_file, &pkg)){
	ret = -1;
        goto cleanup; 
    }
	

    uci_foreach_element(&pkg->sections, e)
    {
    	struct uci_section *s = uci_to_section(e);
	
	zlog_debug(zlogcat,"section s's type is %s.\n",s->type);

	if(!strcmp("attr",s->type))
	{
        	if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "name")))
        	{
		    int meter_flag = 0;
		    for(lp=head;lp;lp=lp->next)
		    {
			    meter = (Meter *)lp->data;
			    if(!strcmp(meter->name,value)){
				meter_flag =1; 
				break;
			    }
		    }
		    if(meter_flag == 1)
		    {
			if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "addr")))
			{
				meter->current_attr->addr = atoi(value); 
				zlog_debug(zlogcat," attr addr is  %d.\n",meter->current_attr->addr);
			}
			if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "reg_num")))
			{
				meter->current_attr->reg_num = atoi(value); 
				zlog_debug(zlogcat," reg num  is  %d.\n",meter->current_attr->reg_num);
			}
			if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "constant")))
			{
				meter->current_attr->constant = atoi(value); 
				zlog_debug(zlogcat," constant  is  %d.\n",meter->current_attr->constant);
			}
			if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "value_type")))
			{
				meter->current_attr->value_type = strdup(value); 
				zlog_debug(zlogcat," value_type  is  %s.\n",meter->current_attr->value_type);
			}
			if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "value_unit")))
			{
				meter->current_attr->value_unit = strdup(value); 
				zlog_debug(zlogcat," value_unit  is  %s.\n",meter->current_attr->value_unit);
			}
			if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "total_diff")))
			{
				meter->current_attr->total_diff = strdup(value); 
				zlog_debug(zlogcat," total_diff is %s.\n",meter->current_attr->total_diff);
			}
			if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "tagid")))
			{
				meter->current_attr->tagid = strdup(value); 
				zlog_debug(zlogcat," tagid is %s.\n",meter->current_attr->tagid);
			}
						
			meter->current_attr++;
		    }
				
		}

	}

    }
    uci_unload(ctx_uci, pkg); // 释放 pkg 
cleanup:
    uci_free_context(ctx_uci);
    ctx_uci = NULL;
    return ret;
}

int load_meter_config(char *meter_uci_config_file)
{
    Sll *l,*new=NULL;
    Meter *meter;
    int ret = UCI_OK;

    struct uci_context *ctx_uci;
    struct uci_package *pkg = NULL;
    struct uci_element *e;
    const char *value;
    int meter_enable_flag = 0;


    ctx_uci = uci_alloc_context(); 
    if (UCI_OK != (ret = uci_load(ctx_uci, meter_uci_config_file, &pkg))){
	zlog_error(zlogcat,"failed to load meter_uci_config_file.\n");
        goto cleanup; 
    }


    uci_foreach_element(&pkg->sections, e)
    {
        struct uci_section *s = uci_to_section(e);
	
	zlog_debug(zlogcat,"section s's type is %s.\n",s->type);

	if(!strcmp("meter",s->type)) //this section is a meter
	{

	    //find a meter section, allocate a new meter;
	    zlog_debug(zlogcat,"this seciton is a meter.\n");
	    if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "status")))
	    {
		if(!strcmp(value,"enable"))
		{
		    meter_enable_flag = 1;
		    zlog_debug(zlogcat,"this meter seciton is enabled.\n");
		
		    meter = (Meter*) malloc(sizeof(Meter));
		    if (meter == NULL)
		    {
			zlog_error(zlogcat,"in load_meter_config: meter malloc failed\n");
			ret = -1;
			goto cleanup;
		    }

		    zlog_debug(zlogcat,"\n---------------[ appending ]----------\n");
		    new=allocateNode((void *) meter);
		    appendNode(&head,&new);


		    if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "modbus_id")))
		    {
			char metername[32] = {0};
			meter->modbus_id = atoi(value); 
			zlog_debug(zlogcat," modbus_id is %d.\n",meter->modbus_id);

			sprintf(metername,"meter_%d",meter->modbus_id);
			meter->name = strdup(metername);
			zlog_debug(zlogcat," meter name is %s.\n",meter->name);
		    }
		    if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "attr_num")))
		    {
			meter->attr_num = atoi(value); 
			zlog_debug(zlogcat," attr_num is %d.\n",meter->attr_num);

			//should verify the existance of option attr_num before alloc memory for attr
			meter->attribute = (Meter_Attribute *) malloc(meter->attr_num * sizeof(Meter_Attribute));
			if(meter->attribute == NULL)
			{
			    zlog_error(zlogcat,"in load_meter_config: Attribute:malloc failed\n");
			    destroyNodes(&head,freeData);
			    ret = -1;
			    goto cleanup;
			}
		    }
		    if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "sender_id")))
		    {
			meter->sender_id = strdup(value); 
			zlog_debug(zlogcat," sender_id is %s.\n",meter->sender_id);
		    }
		    if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "receiver_id")))
		    {
			meter->receiver_id = strdup(value); 
			zlog_debug(zlogcat," receiver_id is %s.\n",meter->receiver_id);
		    }
		    if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "customer_id")))
		    {
			meter->customer_id = strdup(value); 
			zlog_debug(zlogcat," customer_id is %s.\n",meter->customer_id);
		    }
		    if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "customer_name")))
		    {
			meter->customer_name = strdup(value); 
			zlog_debug(zlogcat," customer_name is %s.\n",meter->customer_name);
		    }
		    if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "account_id")))
		    {
			meter->account_id = strdup(value); 
			zlog_debug(zlogcat," account_id is %s.\n",meter->account_id);
		    }
		    if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "account_name")))
		    {
			meter->account_name = strdup(value); 
			zlog_debug(zlogcat," account_name is %s.\n",meter->account_name);
		    }
		    if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "meter_id")))
		    {
			meter->meter_id = strdup(value); 
			zlog_debug(zlogcat," meter_id is %s.\n",meter->meter_id);
		    }
		    if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "commodity")))
		    {
			meter->commodity = strdup(value); 
			zlog_debug(zlogcat," commodity is %s.\n",meter->commodity);
		    }

		    meter->current_attr = meter->attribute;
		}
		else
		    zlog_debug(zlogcat,"this meter section is disabled.\n");
	    }

	}
    }



    //try to get meter attribute only when this meter is enabled
    if(meter_enable_flag)
    {
	uci_foreach_element(&pkg->sections, e)
	{
	    struct uci_section *s = uci_to_section(e);
	    
	    zlog_debug(zlogcat,"section s's type is %s.\n",s->type);

	    if(!strcmp("attr",s->type))
	    {
		if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "addr")))
		{
			meter->current_attr->addr = atoi(value); 
			zlog_debug(zlogcat," attr addr is  %d.\n",meter->current_attr->addr);
		}
		if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "reg_num")))
		{
			meter->current_attr->reg_num = atoi(value); 
			zlog_debug(zlogcat," reg num  is  %d.\n",meter->current_attr->reg_num);
		}
		if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "constant")))
		{
			meter->current_attr->constant = atoi(value); 
			zlog_debug(zlogcat," constant  is  %d.\n",meter->current_attr->constant);
		}
		if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "value_type")))
		{
			meter->current_attr->value_type = strdup(value); 
			zlog_debug(zlogcat," value_type  is  %s.\n",meter->current_attr->value_type);
		}
		if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "value_unit")))
		{
			meter->current_attr->value_unit = strdup(value); 
			zlog_debug(zlogcat," value_unit  is  %s.\n",meter->current_attr->value_unit);
		}
		if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "total_diff")))
		{
			meter->current_attr->total_diff = strdup(value); 
			zlog_debug(zlogcat," total_diff is %s.\n",meter->current_attr->total_diff);
		}
		if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "tagid")))
		{
			meter->current_attr->tagid = strdup(value); 
			zlog_debug(zlogcat," tagid is %s.\n",meter->current_attr->tagid);
		}
					
		meter->current_attr++;
		

	    }

	}
    }
    uci_unload(ctx_uci, pkg); // 释放 pkg 
cleanup:
    uci_free_context(ctx_uci);
    ctx_uci = NULL;
    return ret;
}

int meter_init(void)
{
    int ret = UCI_OK;

    char meter_config_file[64] = {0};
    int i = 32;

    for(i = 1; i <=32; i++)
    {
	sprintf(meter_config_file,"/etc/config/meter%d",i);
	zlog_debug(zlogcat,"meter config file is %s.\n",meter_config_file);

	if (UCI_OK != (ret = load_meter_config(meter_config_file)))
	{
	    zlog_error(zlogcat,"load_meter_config failed:%s.\n",meter_config_file);
	    return ret;
	}
    }
    
    if (UCI_OK != (ret = load_meter_lastvalue_config()))
	return ret;
    return ret;
}

int meter_output_prefix(void){

    char value[32] = {0};
    char string[64] = {0};
    int ret = UCI_OK;

    sprintf(string,"%s",UCI_FILE_PREFIX);
    if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
    {
	strcpy(output_file_prefix,value);
	zlog_debug(zlogcat,"output_file_prefix is %s.\n",value);
	return ret;
    }
    else
    {
	zlog_error(zlogcat,"failed to get output file prefix.\n");
	return ret;
    }

}

int meter_output_format(void){

    zlog_info(zlogcat,"about to init meter output format.\n");

    char value[32] = {0};
    char string[64] = {0};
    int ret = UCI_OK;

    sprintf(string,"%s",UCI_GATEWAY_TYPE);
    if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
    {
	zlog_debug(zlogcat,"gateway type is %s.\n",value);
	if(!strcmp(value,"cmep")){
	    meter_opfm = cmep;
	    return ret;
	}
	else{
		sprintf(string,"%s",UCI_V2_TYPE);
		if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
		{
		    zlog_debug(zlogcat,"v2 type is %s.\n",value);
		    if(!strcmp(value,"xml"))
		    {
			zlog_debug(zlogcat,"v2 type is xml\n");
			meter_opfm = xml;
			return ret;
		    }
		    else if(!strcmp(value,"csv"))
		    {
			zlog_debug(zlogcat,"v2 type is csv\n");
			meter_opfm = csv;
			return ret;
		    }
		}
		else
		{
		    zlog_error(zlogcat,"failed to get v2 type.\n");
		    return ret;
		}
	}
    }
    else
    {
	zlog_error(zlogcat,"failed to get gateway type.\n");
	return ret;
    }
}

int uart_init(void)
{
    int ret = 0;
    zlog_info(zlogcat,"about to init uart.\n");

    uart_config = (UART_config *)malloc(sizeof(UART_config));
    if(uart_config == NULL){
	zlog_error(zlogcat,"uart_init: failed to alloc memeory.\n");
	ret = -1;
	//goto cleanup;
    }

    char value[32] = {0};
    char string[64] = {0};


    //get uart baudrate
    sprintf(string,"%s",UCI_UART_BAUDRATE);
    if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
    {
	uart_config->baudrate = atoi(value);
	zlog_debug(zlogcat,"uart_config->baudrate is %d.\n",uart_config->baudrate);
    }
    else
    {
	zlog_error(zlogcat,"failed to get option:%s.\n",string);
	return ret;
    }
    //get uart databits 
    sprintf(string,"%s",UCI_UART_DATABITS);
    if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
    {
	uart_config->databits = atoi(value);
	zlog_debug(zlogcat,"uart_config->databits is %d.\n",uart_config->databits);
    }
    else
    {
	zlog_error(zlogcat,"failed to get option:%s.\n",string);
	return ret;
    }
    //get uart stopbits 
    sprintf(string,"%s",UCI_UART_STOPBITS);
    if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
    {
	uart_config->stopbits = atoi(value);
	zlog_debug(zlogcat,"uart_config->stopbits is %d.\n",uart_config->stopbits);
    }
    else
    {
	zlog_error(zlogcat, "failed to get option:%s.\n",string);
	return ret;
    }
    //get uart parity 
    sprintf(string,"%s",UCI_UART_PARITY);
    if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
    {
	if(!strcmp(value,"None"))
	    uart_config->parity = 'N';
	if(!strcmp(value,"Even"))
	    uart_config->parity = 'E';
	if(!strcmp(value,"Odd"))
	    uart_config->parity = 'O';

	zlog_debug(zlogcat,"uart_config->parity is %c.\n",uart_config->parity);
    }
    else
    {
	zlog_error(zlogcat,"failed to get option:%s.\n",string);
	return ret;
    }
	

    return ret;
}


int ftp_init(void)
{
    int ret = 0;


    ftp_config = (FTP_config *)malloc(sizeof(FTP_config));
    if(ftp_config == NULL){
	zlog_error(zlogcat,"ftp_init: failed to alloc memeory.\n");
	ret = -1;
	//goto cleanup;
    }

    char value[32] = {0};
    char string[64] = {0};


    //get ftp server addr
    sprintf(string,"%s",UCI_FTP_ADDR);
    if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
    {
	ftp_config->ftp_addr = strdup(value);
	zlog_debug(zlogcat,"ftp_config->ftp_addr is %s.\n",ftp_config->ftp_addr);
    }
    else
    {
	zlog_error("failed to get option:%s.\n",string);
	return ret;
    }
	

    //get ftp server port
    sprintf(string,"%s",UCI_FTP_PORT);
    if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
    {
	ftp_config->ftp_port = strdup(value);
	zlog_debug(zlogcat,"ftp_config->ftp_port is %s.\n",ftp_config->ftp_port);
    }
    else
    {
	zlog_error(zlogcat,"failed to get option:%s.\n",string);
	return ret;
    }

    //get ftp user name
    sprintf(string,"%s",UCI_FTP_USER_NAME);
    if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
    {
	ftp_config->ftp_user_name = strdup(value);
	zlog_debug(zlogcat,"ftp_config->ftp_user_name is %s.\n",ftp_config->ftp_user_name);
    }
    else
    {
	zlog_error(zlogcat,"failed to get option:%s.\n",string);
	return ret;
    }

    //get ftp user password
    sprintf(string,"%s",UCI_FTP_PASSWORD);
    if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
    {
	ftp_config->ftp_password = strdup(value);
	zlog_debug(zlogcat,"ftp_config->ftp_password is %s.\n",ftp_config->ftp_password);
    }
    else
    {
	zlog_error(zlogcat,"failed to get option:%s.\n",string);
	return ret;
    }

    //get ftp file path
    sprintf(string,"%s",UCI_FTP_FILE_PATH);
    if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
    {
	ftp_config->ftp_file_path = strdup(value);
	zlog_debug(zlogcat,"ftp_config->ftp_file_path is %s.\n",ftp_config->ftp_file_path);
    }
    else
    {
	zlog_error(zlogcat,"failed to get option:%s.\n",string);
	return ret;
    }
    
    //get ftp ssl
    sprintf(string,"%s",UCI_FTP_SSL);
    if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
    {
	ftp_config->ftp_ssl = strdup(value);
	zlog_debug(zlogcat,"ftp_config->ftp_ssl is %s.\n",ftp_config->ftp_ssl);
    }
    else
    {
	zlog_error(zlogcat,"failed to get option:%s.\n",string);
	return ret;
    }

    sprintf(FTP_REMOTE_URL,"ftp://%s:%s@%s:%s/%s/",ftp_config->ftp_user_name,
			ftp_config->ftp_password,ftp_config->ftp_addr,ftp_config->ftp_port,ftp_config->ftp_file_path);
    zlog_info(zlogcat,"FTP_REMOTE_URL is %s.\n",FTP_REMOTE_URL);

    return ret;
}
/*  
	user defined sample interval and upload interval
	super important, be aware of mutual exclusion
	global shared resources
*/
int get_interval(char *interval_name, char *custom_interval_name)
{
    int interval;
    int ret = -1;

    char value[32] = {0};
    char string[64] = {0};
    //get ftp server addr
    sprintf(string,"%s",interval_name);
    if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
    {
	zlog_debug(zlogcat,"%s is %s.\n",string,value);
    }
    else
    {
	zlog_error("failed to get option:%s.\n",string);
	return ret;
    }

    interval = atoi(value);

    if(interval){
	return interval;
    }
    else{
	sprintf(string,"%s",custom_interval_name);
	if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
	{
	    zlog_debug(zlogcat,"%s is %s.\n",string,value);
	}
	else
	{
	    zlog_debug(zlogcat,"failed to get option:%s.\n",string);
	    return ret;
	}

	interval = atoi(value);
	return interval;
    }

}
// returned values in minutes
int get_sample_interval()
{
    char sample_interval[] = UCI_SAMPLE_INTERVAL;
    char custom_sample_interval[] = CUSTOM_UCI_SAMPLE_INTERVAL;
    return get_interval(sample_interval,custom_sample_interval);
}
//returned values in minutes
int get_upload_interval()
{
    char upload_interval[] = UCI_UPLOAD_INTERVAL;
    char custom_upload_interval[] = CUSTOM_UCI_UPLOAD_INTERVAL;
    return get_interval(upload_interval,custom_upload_interval);
}

int interval_init(Interval *interval)
{
    zlog_info(zlogcat,"about to init interval:sample,upload.\n");

    //before the create the interval timer, we need to initialize the interval first
    int ret = 0;
    int value;

    if((value = get_sample_interval()) == -1)
    {
	zlog_error(zlogcat,"get_sample_interval failed.\n");
	return -1;
    }
    else
	interval->sample_interval = value;   //minutes to seconds

    if((value = get_upload_interval()) == -1)
    {
	zlog_error(zlogcat,"get_upload_interval failed.\n");
	return -1;
    }
    else
	interval->upload_interval = value;  //minutes to seconds

    zlog_info(zlogcat,"sample interval:%d\n",interval->sample_interval);
    zlog_info(zlogcat,"upload interval:%d\n\n",interval->upload_interval);
    return ret;
}

// interval in seconds to MON:DAY:HH:MM string
void second_trans(int seconds,char *time)
{
    int day,hour,min;
    day = seconds / (60 * 60 * 24);
    hour = seconds / (60 * 60) % 24;
    min = seconds / 60 % 60;
    sprintf(time,"%02d%02d%02d%02d",0,day,hour,min);
}

// initialize interval timer according to user defined sample and upload interval
void itimer_init(struct tm *info, struct itimerspec *it_spec,int it_interval)
{
    //int secs_interval = it_interval * SECSPERMIN;

    zlog_debug(zlogcat,"about to call itimer_init.\n");

    int secs_interval = it_interval;
    int secs_left = 0;	
    //sample interval less than 1 hour, unit in seconds
    if(secs_interval < SECSPERHOUR){
		
    	if((info->tm_min * SECSPERMIN + info->tm_sec) % secs_interval == 0)
	    secs_left = 0;
	else
	    secs_left = secs_interval - (info->tm_min * SECSPERMIN + info->tm_sec) % secs_interval;
    }
    //sample interval equal or greater than 1 hour, unit in hours
    else
	secs_left = SECSPERHOUR - (info->tm_min * SECSPERMIN + info->tm_sec);
	
    zlog_debug(zlogcat,"secs_left is %d.\n",secs_left);	
    if(secs_left == 0)	
    {
	it_spec->it_interval.tv_sec = secs_interval;
	it_spec->it_interval.tv_nsec = 0;
	it_spec->it_value.tv_sec = 0;
	it_spec->it_value.tv_nsec = 1;  //a workaround to start the thread immediately
    }
    else	
    {
	it_spec->it_interval.tv_sec = secs_interval;
	it_spec->it_interval.tv_nsec = 0;
	it_spec->it_value.tv_sec = secs_left;
	it_spec->it_value.tv_nsec = 0;  

    }
	
}


// callback function which gets called when uploading a file to ftp server
static size_t read_callback(void *ptr, size_t size, size_t nmemb, void *stream)
{
    curl_off_t nread;
    /* in real-world cases, this would probably get this data differently
     as this fread() stuff is exactly what the library already would do
     by default internally */
    size_t retcode = fread(ptr, size, nmemb, stream);

    nread = (curl_off_t)retcode;

    fprintf(stderr, "*** We read %" CURL_FORMAT_CURL_OFF_T
          " bytes from file\n", nread);
    return retcode;
}

//upload file funciton: upload file to user specified ftp server
static int upload_file(char *file_to_upload, char *rename_to, char *error_str)
{

    zlog_info(zlogcat,"about to upload file:%s.\n",file_to_upload);

    CURL *curl;
    CURLcode res;
    FILE *hd_src;
    struct stat file_info;
    curl_off_t fsize;
    int ret = 0;
    char remote_url[128] = {0};

    sprintf(remote_url,"%s%s",FTP_REMOTE_URL,rename_to);
    zlog_debug(zlogcat,"remote_url is %s.\n",remote_url);
	
    struct curl_slist *headerlist=NULL;

    /* get the file size of the local file */
    if(stat(file_to_upload, &file_info)) {
	zlog_error(zlogcat,"Couldnt open '%s': %s\n", file_to_upload, strerror(errno));
	sprintf(error_str,"Couldnt open '%s': %s\n", file_to_upload, strerror(errno));
	ret = -1;
	zlog_error(zlogcat,"upload file:%s FAILED.\n",file_to_upload);
	return ret;
    }
    fsize = (curl_off_t)file_info.st_size;
				  
	
    zlog_debug(zlogcat,"Local file size: %" CURL_FORMAT_CURL_OFF_T " bytes.\n", fsize);
				
    /* get a FILE * of the same file */
    hd_src = fopen(file_to_upload, "rb");
				
    /* In windows, this will init the winsock stuff */
    curl_global_init(CURL_GLOBAL_ALL);
				
    /* get a curl handle */
    curl = curl_easy_init();
    if(curl) {
	/* build a list of commands to pass to libcurl */
#if 1
	/* We activate SSL and we require it for both control and data */
	if(!strcmp(ftp_config->ftp_ssl,"yes")){
	    curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
	    curl_easy_setopt (curl, CURLOPT_SSL_VERIFYPEER, 0L); 
	    curl_easy_setopt (curl, CURLOPT_SSL_VERIFYHOST, 0L); 
	}
#endif
	
	/* Switch on full protocol/debug output */
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
	
	/* enable uploading */
	curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
	
	/* specify target */
	//curl_easy_setopt(curl,CURLOPT_URL, REMOTE_URL);
	curl_easy_setopt(curl,CURLOPT_URL, remote_url);
			
	/* now specify which file to upload */
	curl_easy_setopt(curl, CURLOPT_READDATA, hd_src);
			
	/* Set the size of the file to upload (optional).  If you give a *_LARGE
	option you MUST make sure that the type of the passed-in argument is a
	curl_off_t. If you use CURLOPT_INFILESIZE (without _LARGE) you must
	make sure that to pass in a type 'long' argument. */
	curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE,
		(curl_off_t)fsize);
			
	/* Now run off and do what you've been told! */
	res = curl_easy_perform(curl);
	/* Check for errors */
	if(res != CURLE_OK){
	    zlog_error(zlogcat, "curl_easy_perform() failed: %s\n",
		curl_easy_strerror(res));
	    sprintf(error_str, "curl_easy_perform() failed: %s",
		curl_easy_strerror(res));
	    ret =  -1;
	}
			
	/* clean up the FTP commands list */
	curl_slist_free_all (headerlist);
			
	/* always cleanup */
	curl_easy_cleanup(curl);
    }
    fclose(hd_src); /* close the local file */
    curl_global_cleanup();
    return ret;
}

// sample thread start function which runs when sampling interval expires

void timer_thread_sample(union sigval v)
{

    /*  thread shared resources
	super important, be aware of mutual exclusion
	counter: how many times the interval timer expires 
    */
    bool upload = false;
    static int counter = 0;
    counter++;

    if(counter == interval.upload_interval / interval.sample_interval)
	upload = true;

    char time_utc[64] = {0};
    char time_local[64] = {0};
    char time_local_file[64] = {0};
    char time_utc_xml[64] = {0};
    char time_utc_csv[64] = {0};

    time_t rawtime;
    struct tm *info, *info_local;

    zlog_info(zlogcat,"sampling timer expire, about to sample meter datas.\n");

    time(&rawtime);
    /* Get GMT time */
    info = gmtime(&rawtime );

    sprintf(time_utc,"%4d%02d%02d%02d%02d",(1900 + info->tm_year),
			    (info->tm_mon + 1),info->tm_mday,info->tm_hour,info->tm_min);

    sprintf(time_utc_xml,"%4d-%02d-%02dT%02d:%02d:%02d.0000000Z",(1900 + info->tm_year),
			    (info->tm_mon + 1),info->tm_mday,info->tm_hour,info->tm_min,info->tm_sec);

    sprintf(time_utc_csv,"%4d-%02d-%02dT%02d:%02d:%02d.00Z",(1900 + info->tm_year),
			    (info->tm_mon + 1),info->tm_mday,info->tm_hour,info->tm_min,info->tm_sec);
    /* Get LOCAL time */
    info_local = localtime(&rawtime);

    sprintf(time_local,"%4d%02d%02d%02d%02d",(1900 + info_local->tm_year),
			(info_local->tm_mon + 1),info_local->tm_mday,info_local->tm_hour,
			info_local->tm_min);

    sprintf(time_local_file,"%4d%02d%02d%02d%02d%02d",(1900 + info_local->tm_year),
			(info_local->tm_mon + 1),info_local->tm_mday,info_local->tm_hour,
			info_local->tm_min,info_local->tm_sec);

    zlog_debug(zlogcat,
"=========================================================================\n");
    zlog_debug(zlogcat," Sampling timer expired\n");
    zlog_debug(zlogcat," UTC: %s.\n", time_utc);
    zlog_debug(zlogcat," LOCAL: %s.\n", time_local);
    zlog_debug(zlogcat," LOCAL FILE: %s.\n", time_local_file);
    zlog_debug(zlogcat,
"=========================================================================\n");

//*****************************sampling data**********************//
    Sll *l;
    Meter *meter;

    int n=0;

    //define as static for consistency between different sampling
    static char file_path[64];
    static char file_tmp_path[64];
    static char file_name[64];

    int ret = UCI_OK;
    struct uci_context *ctx_uci = NULL;
    ctx_uci = uci_alloc_context();

    
    uint8_t *tab_rp_bits;
    uint16_t *tab_rp_registers;
    modbus_t *ctx_modbus;
    int i;
    uint8_t value;
    int nb_points;
    int rc;
    char interval_string[16];

    zlog_debug(zlogcat,"thread_sample: tring to lock uart_mutex .\n");

    pthread_mutex_lock(&uart_mutex);

    zlog_debug(zlogcat,"thread_sample: get the uart_mutex lock.\n");

    ctx_modbus = modbus_new_rtu("/dev/ttyUSB0", uart_config->baudrate, uart_config->parity, 
						uart_config->databits, uart_config->stopbits);
    if (ctx_modbus == NULL) {
	zlog_error(zlogcat,"thread_sample: modbus_new_rtu--Unable to allocate libmodbus context\n");
	exit -1;
    }

    modbus_set_debug(ctx_modbus, TRUE);
    modbus_set_error_recovery(ctx_modbus,MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);

    if (modbus_connect(ctx_modbus) == -1) {
        zlog_error(zlogcat,"thread_sample: modbus_connect--Connection failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx_modbus);
        exit -1;
    }

    /* Allocate and initialize the memory to store the registers */
    nb_points = 64; //max register number
    tab_rp_registers = (uint16_t *) malloc(nb_points * sizeof(uint16_t));
    memset(tab_rp_registers, 0, nb_points * sizeof(uint16_t));

    //head: the global meter linked list head
    for (l=head; l; l=l->next)
    {
	//get a meter
        meter = (Meter*) l->data;

	//sample data file gets created the first time sample interval timer expires
	if(counter == 1){

	    char postfix[8] = {0};
	    switch(meter_opfm){
		case cmep: strcpy(postfix,"cmep");break;
		case xml : strcpy(postfix,"xml");break;
		case csv : strcpy(postfix,"csv");break;
	    }

	    sprintf(file_name,"%s_%s_001.%s",output_file_prefix,time_local_file,postfix);
	    sprintf(file_path,"%s%s",METER_FILE_LOCATION,file_name);
	    sprintf(file_tmp_path,"%s_%s_tmp",file_path,meter->meter_id);

	    meter->file_path = strdup(file_path);
	    meter->file_tmp_path = strdup(file_tmp_path);
	    meter->file_name = strdup(file_name);


	    if (meter->file_path == NULL || meter->file_tmp_path == NULL || meter->file_name == NULL) 
	    {
		zlog_error(zlogcat,"meter->file_(path,file_tmp_path,file_name) malloc failed\n");
       		exit(-1);
	    }
	    zlog_debug(zlogcat,"the file name is %s.\n",meter->file_name);
	    zlog_debug(zlogcat,"the file path is %s.\n",meter->file_path);
	    zlog_debug(zlogcat,"the tmp file path is %s.\n",meter->file_tmp_path);
	}

	zlog_debug(zlogcat,"opening file %s.\n",meter->file_tmp_path);

	meter->file = fopen(meter->file_tmp_path,"a");			
	if(meter->file == NULL){
	    zlog_error(zlogcat,"failed to fopen %s.\n",meter->file_tmp_path);
	    exit(-1);
	}



    	modbus_set_slave(ctx_modbus, meter->modbus_id);

	Meter_Attribute *attribute = meter->attribute;
	zlog_debug(zlogcat,"attr_num is %d.\n",meter->attr_num);

	for(i = 0; i < meter->attr_num && attribute; i++,attribute++){

	    char attr_option[64] = {0};
	    char option_value[64] = {0};
	    float attr_lastvalue;

	    sprintf(attr_option,"meter_lastvalue.%s.%d",meter->name,attribute->addr);
	    zlog_debug(zlogcat,"attr_option is %s.\n",attr_option);
	    ftp_uci_get_option(attr_option,option_value);

	    attr_lastvalue = atof(option_value);
	    zlog_debug(zlogcat,"attr_lastvalue is %f.\n",attr_lastvalue);

    	    /* Single register */
	    zlog_debug(zlogcat,"reading register.\n");
	    zlog_debug(zlogcat,"addr is %d.\n",attribute->addr);

	    rc = modbus_read_registers(ctx_modbus,attribute->addr,attribute->reg_num,tab_rp_registers);

	    float attr_value;
    	    if (rc == attribute->reg_num) 
	    {

		if(!strcmp(attribute->value_type,"float")){
		    zlog_debug(zlogcat,"attribute value_type is float.\n");
		    attr_value = modbus_get_float(tab_rp_registers);
		}

		if(!strcmp(attribute->value_type,"float swap")){
		    zlog_debug(zlogcat,"attribute value_type is float swap.\n");
		    attr_value = modbus_get_float_cdab(tab_rp_registers);
		}


		sprintf(attr_option,"meter_lastvalue.%s.%d=%f",meter->name,attribute->addr,attr_value);
		zlog_debug(zlogcat,"attr_option is %s.\n",attr_option);

		if( ret = uci_set_option(ctx_uci,attr_option) != UCI_OK){
		   zlog_error(zlogcat,"uci_set_option failed: option is %s.\n",attr_option);
		}

		if(!strcmp(attribute->total_diff,"diff")){
		    zlog_debug(zlogcat,"this attribute is diff.\n");
		    zlog_debug(zlogcat,"attr_value is %f.\n",attr_value);
		    zlog_debug(zlogcat,"attr_lastvalue is %f.\n",attr_lastvalue);
		    
		    attr_value = attr_value - attr_lastvalue;

		    zlog_debug(zlogcat,"attr_value is %f.\n",attr_value);
		}
	    }
	    else   //failed to read the register value, use the last value
	    {
		zlog_error(zlogcat,"failed to read attribute register %d\n",attribute->addr);
		attr_value = attr_lastvalue;

	    }
		
	        //xml
	    if(meter_opfm == xml){

		 fprintf(meter->file,"    <MeterData schema=\"Default\" version=\"1.0.0\">\n      <AcquistionDateTime>%s</AcquisitionDateTime>\n      <Value>%f</Value>\n      <MeterLocalId>%s</MeterLocalId>\n    </MeterData>\n",time_utc_xml,attr_value,attribute->tagid);

		 fflush(meter->file);
	    }
		//csv
	    else if(meter_opfm == csv){
		
		fprintf(meter->file,"%s,%f,%s\n",time_utc_csv,attr_value,attribute->tagid);
		fflush(meter->file);

	    }
	    else{

		second_trans(interval.sample_interval,interval_string);

		if(counter == 1){

		    fprintf(meter->file,"%s,%s,,%s,\"%s\\%s|%s\\%s\",%s,%s,%s,%s,%s,%d,%s,%d,%s,,%f#",
			"MEPMD01,19970819",meter->sender_id,meter->receiver_id,meter->customer_id,
			meter->customer_name,meter->account_id,meter->account_name,time_local,
			meter->meter_id,"OK",meter->commodity,attribute->value_unit,
			attribute->constant,interval_string,
			get_upload_interval() / get_sample_interval(),time_utc,attr_value);

		}
		else{

		    if( i == meter->attr_num -1){	
			fprintf(meter->file,",,,%f",attr_value);
		    }

		    else{
			fprintf(meter->file,",,,%f#",attr_value);
		    }

		    fflush(meter->file);
		}
	    }

	}

	if(meter_opfm == cmep)
	    fprintf(meter->file,"\n");

	zlog_debug(zlogcat,"closing file %s.\n",meter->file_tmp_path);
	fclose(meter->file);
   


//******************************end of sampling data******************//


	zlog_debug(zlogcat,"counter is %d.\n",counter);
	//upload is true
	if(upload && meter_opfm == cmep)
	{
	    char system_arguments[128];

	    //sprintf(system_arguments,"awk '{for(i=1;i<=NF;i++){a[FNR,i]=$i}}END{for(i=1;i<=NF;i++){for(j=1;j<=FNR;j++){printf a[j,i]\" \"}print \"\"}}' %s | sed s/[[:space:]]//g > %s_new",meter->file_path,meter->file_path);
	    sprintf(system_arguments,"awk 'BEGIN{FS=\"#\"}{for(i=1;i<=NF;i++){a[FNR,i]=$i}}END{for(i=1;i<=NF;i++){for(j=1;j<=FNR;j++){printf a[j,i]\"#\"}print \"\"}}' \"%s\" | sed s/#//g >> \"%s\"",meter->file_tmp_path,file_path);
	    if( system(system_arguments) != 0) 
		zlog_error(zlogcat,"system call error:%s.\n",system_arguments); 

	}
    }
    /* Free the memory */

    zlog_debug(zlogcat,"closing the modbus\n");
    free(tab_rp_registers);
    /* Close the connection */
    modbus_close(ctx_modbus);
    modbus_free(ctx_modbus);

    zlog_debug(zlogcat,"thread_sample: about to unlock uart_mutex .\n");
    pthread_mutex_unlock(&uart_mutex);

    if(upload)
    {
	zlog_debug(zlogcat,"######upload time######.\n");
	//setting counter back to 0
	counter = 0;

	if(meter_opfm == xml){
	
	    char args[128];
	    sprintf(args,"echo \"%s\" > \"%s\"",xml_header,file_path);

	    if(system(args) != 0) 
		zlog_error(zlogcat,"system call error:%s.\n",args); 
	 }
	
	if(meter_opfm == csv){

	    char args[128];
	    sprintf(args,"echo \"%s\" > \"%s\"",csv_header,file_path);

	    if(system(args) != 0) 
		zlog_error(zlogcat,"system call error:%s.\n",args); 
	}

	if(meter_opfm == xml || meter_opfm == csv){
	    for (l=head; l; l=l->next)
	    {
		char args[64];
		meter = (Meter*) l->data;
		sprintf(args,"cat \"%s\" >> \"%s\"",meter->file_tmp_path,file_path);

		if(system(args) != 0) 
		    zlog_error(zlogcat,"system call error:%s.\n",args); 
	    }
	}

	if(meter_opfm == xml){
	    char args[128];
	    sprintf(args,"echo \"%s\" >> \"%s\"",xml_ender,file_path);

	    if(system(args) != 0) 
		zlog_error(zlogcat,"system call error:%s.\n",args); 
	 }


	 char error_str[128] = {0};
	 if ( 0 != upload_file(file_path,file_name,error_str) )
	    zlog_error(zlogcat,"failed to upload file:%s.\n",file_path);
    }
}
//CallBack
static int req_modbusid, req_regaddr, req_regnum;

int SBS_MsgProc(int SrcModuleID,int MsgType,int param1,int param2, int param3,char* StringParam,int len)
{
    int iret = 0;
    zlog_info(zlogcat,"Received: SrcModuleID=%d MessageID=%d param1=%d Param2=%d Param3=%d StringParam=%s len=%d\n",
		SrcModuleID, MsgType, param1, param2,param3, StringParam, len);
    switch(MsgType)
	{
        case MSG_SBS_REBOOT:
	    zlog_debug(zlogcat,"about to send SIGINT to %d.\n",mainpid);
	    kill(mainpid,SIGINT);
            break;
	case MSG_SBS_FTP_UPLOAD:
	    zlog_debug(zlogcat,"MSG_SBS_FTP_UPLOAD.\n");
	    break;
	case MSG_SBS_SET_QUERY_REG_CFG:
	    zlog_debug(zlogcat,"MSG_SBS_SET_QUERY_REG_CFG,modbusID=%d,regAddr=%d,regNum=%d.\n",param1,param2,param3);   
	    req_modbusid = param1;
	    req_regaddr = param2;
	    req_regnum = param3;
	    break;
	    
        default:
            break;
    }

    return iret;
}

int SBS_GetValue_Proc(int SrcModuleID, int MessageID,int *param1,int *param2,char** str, int *len)
{
    zlog_info(zlogcat,"Received: SrcModuleID=%d MessageID=%d\n ", SrcModuleID, MessageID);

    FILE *ftp_file = NULL;
    struct tm *info;
    time_t raw;
    char error_str[128] = {0};
    
    switch(MessageID)
	{
        case MSG_SBS_FTP_TEST:
	    zlog_info(zlogcat,"receive ftp test request.\n");

	    time(&raw);
	    info = localtime(&raw);
	    ftp_file = fopen("/tmp/ftp_test","w");
	    fprintf(ftp_file,"ftp_test:%s\n",asctime(info));
	    fflush(ftp_file);
	    fclose(ftp_file);
	    if (0 == upload_file("/tmp/ftp_test","ftp_test",error_str))
	    {
		sprintf(str,"1");
		*len = 1;
	    }
		
	    else 
	    {
		sprintf(str,"0|%s",error_str);
	    }
            break; 
        case MSG_SBS_METER_STATUS:
            {
		zlog_info(zlogcat,"receive meter status request.\n");
		zlog_debug(zlogcat,"meter_status:tring to lock uart_mutex .\n");

		pthread_mutex_lock(&uart_mutex);

		zlog_debug(zlogcat,"meter_status:get the uart_mutex lock.\n");

		uint8_t *tab_rp_bits;
		uint16_t *tab_rp_registers;
		modbus_t *ctx_modbus;
		int i;
		uint8_t value;
		int nb_points;
		int rc;
		char interval_string[16];

		Sll *l;
		Meter *meter;
		Meter_Attribute *attribute;
		char meter_status[32] = {0};
	    
		ctx_modbus = modbus_new_rtu("/dev/ttyUSB0", uart_config->baudrate, uart_config->parity, 
							uart_config->databits, uart_config->stopbits);
		if (ctx_modbus == NULL) {
		    sprintf(str, "0|Unable to allocate libmodbus context");
		    return 0;
		}
		modbus_set_debug(ctx_modbus, TRUE);
		modbus_set_error_recovery(ctx_modbus,
					    MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);
		if (modbus_connect(ctx_modbus) == -1) {
		    sprintf(str, "0|modbus Connection failed: %s", modbus_strerror(errno));
		    modbus_free(ctx_modbus);
		    return 0;
		}
		strcat(str,"1,");
		/* Allocate and initialize the memory to store the bits */
		nb_points = 64; //maximum reading registers number is 64
		tab_rp_registers = (uint16_t *) malloc(nb_points * sizeof(uint16_t));
		memset(tab_rp_registers, 0, nb_points * sizeof(uint16_t));

		for (l=head; l; l=l->next)
		{
		    //get a meter
		    meter = (Meter*) l->data;
		    attribute = meter->attribute;

		    modbus_set_slave(ctx_modbus, meter->modbus_id);

		    rc = modbus_read_registers(ctx_modbus,
				attribute->addr,attribute->reg_num,tab_rp_registers);

		    if (rc == attribute->reg_num) 
		    {
			if(l->next)
			    sprintf(meter_status,"%s|1,",meter->name);
			else
			    sprintf(meter_status,"%s|1",meter->name);
			strcat(str,meter_status);
		    }

		    else {
			if(l->next)
			    sprintf(meter_status,"%s|0,",meter->name);
			else
			    sprintf(meter_status,"%s|0",meter->name);
			strcat(str,meter_status);
		    }

		    zlog_debug(zlogcat,"returned str is %s.\n",str);
		}
		free(tab_rp_bits);
		free(tab_rp_registers);
		modbus_close(ctx_modbus);
		modbus_free(ctx_modbus);
		printf("tring to unlock uart_mutex .\n");
		pthread_mutex_unlock(&uart_mutex);

            }
            break;
        case MSG_SBS_TEST://½ö²âÊÔÓÃ
        {

	    FILE *tmp_file = fopen("/tmp/meter_values","w");
	    if(tmp_file == NULL)
		fprintf(stderr,"failed to open meter_values file.\n");
	    
	    Sll *l;
	    Meter *meter;
	    for (l=head; l; l=l->next)
	    {
		//get a meter
		meter = (Meter*) l->data;
		fprintf(tmp_file,"%s ",meter->name);

		//sample data file gets created the first time sample interval timer expires

		uint8_t *tab_rp_bits;
		uint16_t *tab_rp_registers;
		modbus_t *ctx;
		int i;
		uint8_t value;
		int nb_points;
		int rc;
		char interval_string[16];

		ctx = modbus_new_rtu("/dev/ttyUSB0", 19200, 'N', 8, 1);
		if (ctx == NULL) {
		    fprintf(stderr, "Unable to allocate libmodbus context\n");
		    exit -1;
		}
		modbus_set_debug(ctx, TRUE);
		modbus_set_error_recovery(ctx,MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);
		modbus_set_slave(ctx, meter->modbus_id);

		if (modbus_connect(ctx) == -1) {
		    fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
		    modbus_free(ctx);
		    exit -1;
		}


		/* Allocate and initialize the memory to store the registers */
		nb_points = 16; //max register number
		tab_rp_registers = (uint16_t *) malloc(nb_points * sizeof(uint16_t));
		memset(tab_rp_registers, 0, nb_points * sizeof(uint16_t));

		Meter_Attribute *attribute = meter->attribute;
		printf("attr_num is %d.\n",meter->attr_num);
		fprintf(tmp_file,"%d ",meter->attr_num);
		for(i = 0; i < meter->attr_num && attribute; i++,attribute++){

		    /* Single register */
		    printf("reading register.\n");
		    printf("addr is %d.\n",attribute->addr);
		    rc = modbus_read_registers(ctx,attribute->addr,attribute->reg_num,tab_rp_registers);
		    if (rc == attribute->reg_num) 
		    {
			if(!strcmp(meter->name,"PM700"))
			    fprintf(tmp_file,"%.3f %s ",modbus_get_float_cdab(tab_rp_registers),attribute->value_unit);
			else 
			    fprintf(tmp_file,"%.3f %s ",modbus_get_float(tab_rp_registers),attribute->value_unit);

		    }
		    else
			fprintf(tmp_file,"%.3f ",0);

		}
		fprintf(tmp_file,"\n");
		free(tab_rp_registers);
		/* Close the connection */
		modbus_close(ctx);
		modbus_free(ctx);
	    }
	    fclose(tmp_file);

	    tmp_file = fopen("/tmp/meter_values","r");
	    if(tmp_file == NULL)
		fprintf(stderr,"failed to open file meter_values.\n");

            char info[256]={0};
	    char *p;

	    while (fgets(info,256,tmp_file) != NULL){
		//p = strchr(info,'\n');
		//*p = '\0';
		strcat(str,info);
	    }
	    printf("str is %s.\n",str);
            *len = strlen(str);
        }
	break;
	case MSG_SBS_GET_QUERY_REG_VALUE:
	{

	    zlog_info(zlogcat,"receive meter reading register request.\n");
	    zlog_debug(zlogcat,"req_modbusid:%d.\n",req_modbusid);
	    zlog_debug(zlogcat,"req_regaddr:%d.\n",req_regaddr);
	    zlog_debug(zlogcat,"req_regnum:%d.\n",req_regnum);

	    zlog_debug(zlogcat,"meter_status:tring to lock uart_mutex .\n");

	    pthread_mutex_lock(&uart_mutex);

	    zlog_debug(zlogcat,"meter_status:get the uart_mutex lock.\n");

	    uint8_t *tab_rp_bits;
	    uint16_t *tab_rp_registers;
	    modbus_t *ctx_modbus;
	    int i;
	    uint8_t value;
	    int nb_points;
	    int rc;
	    char interval_string[16];

	    char meter_status[32] = {0};
	
	    ctx_modbus = modbus_new_rtu("/dev/ttyUSB0", uart_config->baudrate, uart_config->parity, 
						    uart_config->databits, uart_config->stopbits);
	    if (ctx_modbus == NULL) {
		sprintf(str, "0|Unable to allocate libmodbus context");
		return 0;
	    }
	    modbus_set_debug(ctx_modbus, TRUE);
	    modbus_set_error_recovery(ctx_modbus,
					MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);
	    if (modbus_connect(ctx_modbus) == -1) {
		sprintf(str, "0|modbus Connection failed: %s", modbus_strerror(errno));
		modbus_free(ctx_modbus);
		return 0;
	    }
	    /* Allocate and initialize the memory to store the bits */
	    nb_points = 64; //maximum reading registers number is 64
	    tab_rp_registers = (uint16_t *) malloc(nb_points * sizeof(uint16_t));
	    memset(tab_rp_registers, 0, nb_points * sizeof(uint16_t));


	    modbus_set_slave(ctx_modbus, req_modbusid);

	    rc = modbus_read_registers(ctx_modbus,
			    req_regaddr,req_regnum,tab_rp_registers);

	    if (rc == req_regnum)  
	    {
		for(i = 0; i < req_regnum; i++){
		    sprintf(meter_status,"<%04X>",tab_rp_registers[i]);
		    strcat(str,meter_status);
		}
	    }

	    else {
		strcat(str,meter_status);
	    }

	    zlog_debug(zlogcat,"returned str is %s.\n",str);
	    free(tab_rp_bits);
	    free(tab_rp_registers);
	    modbus_close(ctx_modbus);
	    modbus_free(ctx_modbus);
	    printf("tring to unlock uart_mutex .\n");
	    pthread_mutex_unlock(&uart_mutex);

	}
        break;
        default:
            break;
    }
    
    return 0;
}
void sig_handler(int sig_no)
{
    if(sig_no == SIGINT)
	zlog_info(zlogcat,"main thread catch signal interrupt,about to restart\n");
}

int main(int argc,char *argv[])
{
    //save the current pid;
    mainpid = getpid();

    int zlogrc;

    zlogrc = zlog_init("/etc/test_level.conf");
    if(zlogrc)
    {
	printf("zlog init failed.\n");
	return -1;
    }
    zlogcat = zlog_get_category("my_cat");
    if(!zlogcat)
    {
	printf("failed to get zlog category.\n");
	zlog_fini();
	return -1;
    }

    zlog_debug(zlogcat,"hello,zlog,sampling2 - debug.\n");
    zlog_info(zlogcat,"hello,zlog -info.\n");



    //register signal SIGINT handler
    if(0 != pthread_mutex_init(&uart_mutex,NULL))
    {
	zlog_error(zlogcat,"uart_mutext init failed.\n");
	return -1;
    }

    if( signal(SIGINT,sig_handler) == SIG_ERR)
	zlog_error(zlogcat,"failed to register SIGINT handler.\n");

    if(UCI_OK != uart_init())
    {
	zlog_error(zlogcat,"uart init failed.\n");
	return -1;
    }
    
    if(UCI_OK != ftp_init())
    {
	zlog_error(zlogcat,"ftp init failed.\n");
	return -1;
    }
	
    if(UCI_OK != meter_init())
    {
	zlog_error(zlogcat,"meter init failed.\n");
	return -1;
    }


    Sll *l;
    Meter *addr;
    zlog_debug(zlogcat,"\n---------------[ Meters printing ]----------\n");
    int n=0;
    for (l=head; l; l=l->next)
    {
        addr=(Meter*) l->data;
    	zlog_debug(zlogcat,"Node: %d\n", ++n);
    	zlog_debug(zlogcat,"modbus_id:  %d\n",addr->modbus_id);
    	zlog_debug(zlogcat,"attr_num:  %d\n",addr->attr_num);

    	Meter_Attribute *attribute = addr->attribute;
    	int i;
    	for(i = 0; i < addr->attr_num && attribute; i++,attribute++){
	   zlog_debug(zlogcat,"attribute->addr:  %d\n",attribute->addr);
	   zlog_debug(zlogcat,"attribute->reg_num:  %d\n",attribute->reg_num);
	   zlog_debug(zlogcat,"attribute->constant:  %d\n",attribute->constant);
	   zlog_debug(zlogcat,"attribute->value_type:  %s\n",attribute->value_type);
	   zlog_debug(zlogcat,"attribute->value_unit  %s\n",attribute->value_unit);
	   zlog_debug(zlogcat,"\n");
   	 }
    }

    if(UCI_OK != meter_output_format())
    {
	zlog_error(zlogcat,"failed to get meter_output_format.\n");
	return -1;
    }
    if(UCI_OK != meter_output_prefix())
    {
	zlog_error(zlogcat,"failed to get meter_output_prefix.\n");
	return -1;
    }

    if ( 0 != interval_init(&interval) )
    {
	zlog_error(zlogcat,"failed to get interval_init.\n");
	return -1;
    }

#if 1

    time_t rawtime;
    struct tm *info, *info_local;

    timer_t timerid_sample;
    struct sigevent evp_sample;
    memset(&evp_sample, 0, sizeof(struct sigevent));		

    evp_sample.sigev_value.sival_int = 100;			
    evp_sample.sigev_notify = SIGEV_THREAD;		
    evp_sample.sigev_notify_function = timer_thread_sample;		

    if (timer_create(CLOCKID, &evp_sample, &timerid_sample) == -1) {
	zlog_error(zlogcat,"fail to sample timer_create");
	exit(-1);
    }

    time(&rawtime);
    /* Get GMT time */
    info = gmtime(&rawtime );
    zlog_debug(zlogcat,"Current world clock:\n");
    zlog_debug(zlogcat,"UTC: %4d%02d%02d%02d%02d%02d\n\n", (info->tm_year + 1900),
			(info->tm_mon + 1),info->tm_mday,info->tm_hour, info->tm_min,info->tm_sec);

    info_local = localtime(&rawtime);
    zlog_debug(zlogcat,"Current local clock:\n");
    zlog_debug(zlogcat,"LOCAL: %4d%02d%02d%02d%02d%02d\n\n", (info_local->tm_year + 1900),
			(info_local->tm_mon + 1),info_local->tm_mday,info_local->tm_hour, 
			info_local->tm_min,info_local->tm_sec);
	
	   
    struct itimerspec it_sample;

    itimer_init(info,&it_sample,interval.sample_interval);
	
    if (timer_settime(timerid_sample, 0, &it_sample, NULL) == -1) {
	zlog_error(zlogcat,"fail to sample timer_settime");
	exit(-1);
    }
#endif

    zlog_debug(zlogcat,"sbs communication server\n");

    /*init socket file and handle*/
    Init(MODULE_SERVER);
    FuncHostCallback FC;
    FC.pSBS_MsgProc=SBS_MsgProc;
    FC.pSBS_GetValue=SBS_GetValue_Proc;
    /*register call back func*/
    RegisterHostCallBack(FC);
    pause();

    zlog_debug(zlogcat,"pause return");

//free memory section
    zlog_fini();

//end 
    execv(argv[0],argv);
    Clear(MODULE_SERVER);
	
    return 0;
}

