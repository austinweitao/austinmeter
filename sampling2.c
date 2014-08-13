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
	    printf("failed to alloc uci context.\n");
	    return -1;	    
	}

	if ((ret = uci_lookup_ptr(ctx_uci,&ptr_uci,option,true)) != UCI_OK) 
	{ 
	    printf("lookup_ptr failed.\n");
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

	printf("string is %s.\n",string);

	if ((ret = uci_lookup_ptr(ctx_uci, &ptr,string, true)) != UCI_OK) 
	{ 
	    printf("lookup_ptr failed.\n");
	    return ret;
	}
	ret = uci_set(ctx_uci, &ptr);
	if (ret != UCI_OK){
		fprintf(stderr,"uci_set failed.\n");
		return ret;
	}
	if ( UCI_OK != (ret = uci_commit(ctx_uci, &ptr.p, false))) {
	    printf("uci_set_option:uci commit failed.\n");
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
		fprintf(stderr,"add  section failed.\n");
		return ret;
	}
	printf("section's name is %s.\n",s->e.name);
	char ptr_str[64] = {0};
	sprintf(ptr_str,"meter_lastvalue.%s",s->e.name);

	if ( (ret = uci_lookup_ptr(ctx, &ptr, ptr_str, true)) != UCI_OK) 
	{ 
	    printf("uci_lookup_ptr failed:%s.\n",ptr_str);
	    return ret;
	}
	printf("ptr's package is %s.\n",ptr.package);
	printf("ptr's section is %s.\n",ptr.section);
	printf("ptr's option is %s.\n",ptr.option);

	ptr.value = strdup(meter->name);

	if ((ret = uci_rename(ctx,&ptr) != UCI_OK))
	{
	    fprintf(stderr,"uci_rename failed.\n");
	    return ret;
	}

	sprintf(ptr_str,"meter_lastvalue.%s.modbus_id=%d",s->e.name,meter->modbus_id);
	printf("ptr_str is %s.\n",ptr_str);

	if( ret = uci_set_option(ctx,ptr_str) != UCI_OK){
		fprintf(stderr,"uci_set_option failed:%s.\n",ptr_str);
		return ret;
	}

	int i;
	Meter_Attribute *attribute = meter->attribute;

	for(i = 0; i < meter->attr_num; i++,attribute++)
	{
	    sprintf(ptr_str,"meter_lastvalue.%s.%s=%d",s->e.name,attribute->value_unit,0);
	    printf("ptr_str is %s.\n",ptr_str);

	    if( ret = uci_set_option(ctx,ptr_str) != UCI_OK){
		fprintf(stderr,"uci_set_option failed:%s.\n",ptr_str);
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
		printf("section s's type is meter_lastvalue\n");
		if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "modbus_id"))) 
		{
		    modbus_id = atoi(value);
		    if ( meter->modbus_id ==  modbus_id){
			flag = 1; 
			printf("found a section belonging to the meter.\n");
		    }
		}
					
	    }
	}
	if(flag == 0)   //no seciton belongs to the current meter
	{
	    printf("currently no section belongs to the meter, create a new section.\n");
	    if ( UCI_OK != (ret = uci_do_add(ctx_uci,pkg,"meter_lastvalue",meter)))
	    {
		fprintf(stderr,"uci do add failed.\n");
		goto cleanup;
	    }
	}
    }

cleanup:
    uci_free_context(ctx_uci);
    ctx_uci = NULL;
    return ret;
}
int load_attr_config()
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
    if (UCI_OK != uci_load(ctx_uci, METER_UCI_CONFIG_FILE, &pkg)){
	ret = -1;
        goto cleanup; 
    }
	

    uci_foreach_element(&pkg->sections, e)
    {
    	struct uci_section *s = uci_to_section(e);
	
	printf("section s's type is %s.\n",s->type);

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
				printf(" attr addr is  %d.\n",meter->current_attr->addr);
			}
			if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "reg_num")))
			{
				meter->current_attr->reg_num = atoi(value); 
				printf(" reg num  is  %d.\n",meter->current_attr->reg_num);
			}
			if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "constant")))
			{
				meter->current_attr->constant = atoi(value); 
				printf(" constant  is  %d.\n",meter->current_attr->constant);
			}
			if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "value_type")))
			{
				meter->current_attr->value_type = strdup(value); 
				printf(" value_type  is  %s.\n",meter->current_attr->value_type);
			}
			if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "value_unit")))
			{
				meter->current_attr->value_unit = strdup(value); 
				printf(" value_unit  is  %s.\n",meter->current_attr->value_unit);
			}
			if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "total_diff")))
			{
				meter->current_attr->total_diff = strdup(value); 
				printf(" total_diff is %s.\n",meter->current_attr->total_diff);
			}
			if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "tagid")))
			{
				meter->current_attr->tagid = strdup(value); 
				printf(" tagid is %s.\n",meter->current_attr->tagid);
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

int load_meter_config()
{
    Sll *l,*new=NULL;
    Meter *meter;
    int ret = UCI_OK;

    struct uci_context *ctx_uci;
    struct uci_package *pkg = NULL;
    struct uci_element *e;
    const char *value;


    ctx_uci = uci_alloc_context(); 
    if (UCI_OK != (ret = uci_load(ctx_uci, METER_UCI_CONFIG_FILE, &pkg))){
	printf("meter_config error.\n");
        goto cleanup; 
    }
    printf("############meter_config#############.\n");


    uci_foreach_element(&pkg->sections, e)
    {
        struct uci_section *s = uci_to_section(e);
	
	printf("section s's type is %s.\n",s->type);

	if(!strcmp("meter",s->type)) //this section is a meter
	{

	    //find a meter section, allocate a new meter;
	    printf("this seciton is a meter.\n");
	    if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "status")))
	    {
		if(!strcmp(value,"enable"))
		{
		    printf("this meter seciton is enabled.\n");
		
		    meter = (Meter*) malloc(sizeof(Meter));
		    if (meter == NULL)
		    {
			(void) fprintf(stderr,"meter malloc failed\n");
			ret = -1;
			goto cleanup;
		    }

		    (void) fprintf(stderr,"\n---------------[ appending ]----------\n");
		    new=allocateNode((void *) meter);
		    appendNode(&head,&new);

		    //initialize meter
		    if(s->anonymous == false )
		    {
			meter->name = strdup(s->e.name);
			printf(" meter name is  %s.\n",meter->name);
		    }

		    if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "modbus_id")))
		    {
			meter->modbus_id = atoi(value); 
			printf(" modbus_id is %d.\n",meter->modbus_id);
		    }
		    if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "attr_num")))
		    {
			meter->attr_num = atoi(value); 
			printf(" attr_num is %d.\n",meter->attr_num);
			//should verify the existance of option attr_num before alloc memory for attr
			meter->attribute = (Meter_Attribute *) malloc(meter->attr_num * sizeof(Meter_Attribute));
			if(meter->attribute == NULL)
			{
			    (void)fprintf(stderr," Attribute:malloc failed\n");
			    destroyNodes(&head,freeData);
			    ret = -1;
			    goto cleanup;
			}
		    }
		    if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "sender_id")))
		    {
			meter->sender_id = strdup(value); 
			printf(" sender_id is %s.\n",meter->sender_id);
		    }
		    if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "receiver_id")))
		    {
			meter->receiver_id = strdup(value); 
			printf(" receiver_id is %s.\n",meter->receiver_id);
		    }
		    if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "customer_id")))
		    {
			meter->customer_id = strdup(value); 
			printf(" customer_id is %s.\n",meter->customer_id);
		    }
		    if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "customer_name")))
		    {
			meter->customer_name = strdup(value); 
			printf(" customer_name is %s.\n",meter->customer_name);
		    }
		    if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "account_id")))
		    {
			meter->account_id = strdup(value); 
			printf(" account_id is %s.\n",meter->account_id);
		    }
		    if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "account_name")))
		    {
			meter->account_name = strdup(value); 
			printf(" account_name is %s.\n",meter->account_name);
		    }
		    if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "meter_id")))
		    {
			meter->meter_id = strdup(value); 
			printf(" meter_id is %s.\n",meter->meter_id);
		    }
		    if (NULL != (value = uci_lookup_option_string(ctx_uci, s, "commodity")))
		    {
			meter->commodity = strdup(value); 
			printf(" commodity is %s.\n",meter->commodity);
		    }

		    meter->current_attr = meter->attribute;
		}
		else
		    printf("this meter section is disabled.\n");
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
    if (UCI_OK != (ret = load_meter_config()))
	return ret;
    if (UCI_OK != (ret = load_attr_config()))
	return ret;
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
	printf("output_file_prefix is %s.\n",value);
	return ret;
    }
    else
    {
	printf("failed to get output file prefix.\n");
	return ret;
    }

}

int meter_output_format(void){

    char value[32] = {0};
    char string[64] = {0};
    int ret = UCI_OK;

    sprintf(string,"%s",UCI_GATEWAY_TYPE);
    if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
    {
	printf("gateway type is %s.\n",value);
	if(!strcmp(value,"cmep")){
	    meter_opfm = cmep;
	    return ret;
	}
	else{
		sprintf(string,"%s",UCI_V2_TYPE);
		if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
		{
		    printf("v2 type is %s.\n",value);
		    if(!strcmp(value,"xml"))
		    {
			printf("v2 type is xml\n");
			meter_opfm = xml;
			return ret;
		    }
		    else if(!strcmp(value,"csv"))
		    {
			printf("v2 type is csv\n");
			meter_opfm = csv;
			return ret;
		    }
		}
		else
		{
		    printf("failed to get v2 type.\n");
		    return ret;
		}
	}
    }
    else
    {
	printf("failed to get gateway type.\n");
	return ret;
    }
}

int uart_init(void)
{
    int ret = 0;

    uart_config = (UART_config *)malloc(sizeof(UART_config));
    if(uart_config == NULL){
	printf("uart_init: failed to alloc memeory.\n");
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
	printf("uart_config->baudrate is %d.\n",uart_config->baudrate);
    }
    else
    {
	printf("failed to get option:%s.\n",string);
	return ret;
    }
    //get uart databits 
    sprintf(string,"%s",UCI_UART_DATABITS);
    if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
    {
	uart_config->databits = atoi(value);
	printf("uart_config->databits is %d.\n",uart_config->databits);
    }
    else
    {
	printf("failed to get option:%s.\n",string);
	return ret;
    }
    //get uart stopbits 
    sprintf(string,"%s",UCI_UART_STOPBITS);
    if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
    {
	uart_config->stopbits = atoi(value);
	printf("uart_config->stopbits is %d.\n",uart_config->stopbits);
    }
    else
    {
	printf("failed to get option:%s.\n",string);
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
	printf("uart_config->parity is %c.\n",uart_config->parity);
    }
    else
    {
	printf("failed to get option:%s.\n",string);
	return ret;
    }
	

    return ret;
}


int ftp_init(void)
{
    int ret = 0;


    ftp_config = (FTP_config *)malloc(sizeof(FTP_config));
    if(ftp_config == NULL){
	printf("ftp_init: failed to alloc memeory.\n");
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
	printf("ftp_config->ftp_addr is %s.\n",ftp_config->ftp_addr);
    }
    else
    {
	printf("failed to get option:%s.\n",string);
	return ret;
    }
	

    //get ftp server port
    sprintf(string,"%s",UCI_FTP_PORT);
    if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
    {
	ftp_config->ftp_port = strdup(value);
	printf("ftp_config->ftp_port is %s.\n",ftp_config->ftp_port);
    }
    else
    {
	printf("failed to get option:%s.\n",string);
	return ret;
    }

    //get ftp user name
    sprintf(string,"%s",UCI_FTP_USER_NAME);
    if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
    {
	ftp_config->ftp_user_name = strdup(value);
	printf("ftp_config->ftp_user_name is %s.\n",ftp_config->ftp_user_name);
    }
    else
    {
	printf("failed to get option:%s.\n",string);
	return ret;
    }

    //get ftp user password
    sprintf(string,"%s",UCI_FTP_PASSWORD);
    if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
    {
	ftp_config->ftp_password = strdup(value);
	printf("ftp_config->ftp_password is %s.\n",ftp_config->ftp_password);
    }
    else
    {
	printf("failed to get option:%s.\n",string);
	return ret;
    }

    //get ftp file path
    sprintf(string,"%s",UCI_FTP_FILE_PATH);
    if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
    {
	ftp_config->ftp_file_path = strdup(value);
	printf("ftp_config->ftp_file_path is %s.\n",ftp_config->ftp_file_path);
    }
    else
    {
	printf("failed to get option:%s.\n",string);
	return ret;
    }
    
    //get ftp ssl
    sprintf(string,"%s",UCI_FTP_SSL);
    if ( UCI_OK == (ret = ftp_uci_get_option(string,value)))
    {
	ftp_config->ftp_ssl = strdup(value);
	printf("ftp_config->ftp_ssl is %s.\n",ftp_config->ftp_ssl);
    }
    else
    {
	printf("failed to get option:%s.\n",string);
	return ret;
    }
    sprintf(FTP_REMOTE_URL,"ftp://%s:%s@%s:%s/%s/",ftp_config->ftp_user_name,
			ftp_config->ftp_password,ftp_config->ftp_addr,ftp_config->ftp_port,ftp_config->ftp_file_path);
    printf("FTP_REMOTE_URL is %s.\n",FTP_REMOTE_URL);
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
	printf("%s is %s.\n",string,value);
    }
    else
    {
	printf("failed to get option:%s.\n",string);
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
	    printf("%s is %s.\n",string,value);
	}
	else
	{
	    printf("failed to get option:%s.\n",string);
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
    //before the create the interval timer, we need to initialize the interval first
    int ret = 0;
    printf("about to initialize sample interval and upload interval\n");
    int value;
    if((value = get_sample_interval()) == -1)
    {
	printf("get_sample_interval failed.\n");
	return -1;
    }
    else
	interval->sample_interval = value;   //minutes to seconds
    if((value = get_upload_interval()) == -1)
    {
	printf("get_upload_interval failed.\n");
	return -1;
    }
    else
	interval->upload_interval = value;  //minutes to seconds

    printf("sample interval:%d\n",interval->sample_interval);
    printf("upload interval:%d\n\n",interval->upload_interval);
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
	
    printf("secs_left is %d.\n",secs_left);	
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
    CURL *curl;
    CURLcode res;
    FILE *hd_src;
    struct stat file_info;
    curl_off_t fsize;
    int ret = 0;
    char remote_url[128] = {0};

    sprintf(remote_url,"%s%s",FTP_REMOTE_URL,rename_to);
    printf("remote_url is %s.\n",remote_url);
	
    struct curl_slist *headerlist=NULL;

    /* get the file size of the local file */
    if(stat(file_to_upload, &file_info)) {
	printf("Couldnt open '%s': %s\n", file_to_upload, strerror(errno));
	sprintf(error_str,"Couldnt open '%s': %s\n", file_to_upload, strerror(errno));
	ret = -1;
	return ret;
    }
    fsize = (curl_off_t)file_info.st_size;
				  
	
    printf("Local file size: %" CURL_FORMAT_CURL_OFF_T " bytes.\n", fsize);
				
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
	    fprintf(stderr, "curl_easy_perform() failed: %s\n",
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

    (void) fprintf(stderr,
"=========================================================================\n");
    (void) fprintf(stderr," Sampling timer expired\n");
    (void) fprintf(stderr,"UTC: %s.\n", time_utc);
    (void) fprintf(stderr,"LOCAL: %s.\n", time_local);
    (void) fprintf(stderr,"LOCAL FILE: %s.\n", time_local_file);
    (void) fprintf(stderr,
"=========================================================================\n");

//*****************************sampling data**********************//
    Sll *l;
    Meter *meter;

    int n=0;
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

    printf("thread_sample: tring to lock uart_mutex .\n");
    pthread_mutex_lock(&uart_mutex);
    printf("thread_sample: get the uart_mutex lock.\n");

    ctx_modbus = modbus_new_rtu("/dev/ttyUSB0", uart_config->baudrate, uart_config->parity, 
						uart_config->databits, uart_config->stopbits);
    if (ctx_modbus == NULL) {
	fprintf(stderr, "Unable to allocate libmodbus context\n");
	exit -1;
    }
    modbus_set_debug(ctx_modbus, TRUE);
    modbus_set_error_recovery(ctx_modbus,MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);

    if (modbus_connect(ctx_modbus) == -1) {
        fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
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
		(void) fprintf(stderr,"malloc failed\n");
       		exit(-1);
	    }
	    (void )fprintf(stderr,"the file name is %s.\n",meter->file_name);
	    (void )fprintf(stderr,"the file path is %s.\n",meter->file_path);
	    (void )fprintf(stderr,"the tmp file path is %s.\n",meter->file_tmp_path);
	}

	(void )fprintf(stderr,"opening file %s.\n",meter->file_tmp_path);

	meter->file = fopen(meter->file_tmp_path,"a");			
	if(meter->file == NULL){
	    perror("fopen failed:");
	    exit(-1);
	}



    	modbus_set_slave(ctx_modbus, meter->modbus_id);

	Meter_Attribute *attribute = meter->attribute;
	printf("attr_num is %d.\n",meter->attr_num);
	for(i = 0; i < meter->attr_num && attribute; i++,attribute++){

	    char attr_option[64] = {0};
	    char option_value[64] = {0};
	    float attr_lastvalue;

	    sprintf(attr_option,"meter_lastvalue.%s.%s",meter->name,attribute->value_unit);
	    fprintf(stderr,"attr_option is %s.\n",attr_option);
	    ftp_uci_get_option(attr_option,option_value);
	    attr_lastvalue = atof(option_value);
	    printf("attr_lastvalue is %f.\n",attr_lastvalue);

    	    /* Single register */
	    printf("reading register.\n");
	    printf("addr is %d.\n",attribute->addr);
	    rc = modbus_read_registers(ctx_modbus,attribute->addr,attribute->reg_num,tab_rp_registers);
    	    if (rc == attribute->reg_num) 
	    {

		float attr_value;
		if(!strcmp(attribute->value_type,"float")){
		    fprintf(stderr,"attribute value_type is float.\n");
		    attr_value = modbus_get_float(tab_rp_registers);
		}
		if(!strcmp(attribute->value_type,"float swap")){
		    fprintf(stderr,"attribute value_type is float swap.\n");
		    attr_value = modbus_get_float_cdab(tab_rp_registers);
		}
		sprintf(attr_option,"meter_lastvalue.%s.%s=%f",meter->name,attribute->value_unit,attr_value);
		fprintf(stderr,"attr_option is %s.\n",attr_option);

		if( ret = uci_set_option(ctx_uci,attr_option) != UCI_OK){
		   fprintf(stderr,"ret is not ok.\n");
		}

		if(!strcmp(attribute->total_diff,"diff")){
		    fprintf(stderr,"this attribute is diff.\n");
		    fprintf(stderr,"attr_value is %f.\n",attr_value);
		    fprintf(stderr,"attr_lastvalue is %f.\n",attr_lastvalue);
		    attr_value = attr_value - attr_lastvalue;
		    fprintf(stderr,"attr_value is %f.\n",attr_value);
		}
		
	        //xml
		if(meter_opfm == xml){
		    fprintf(stderr,"###debug3######.\n");
		    fprintf(meter->file,"    <MeterData schema=\"Default\" version=\"1.0.0\">\n      <AcquistionDateTime>%s</AcquisitionDateTime>\n      <Value>%f</Value>\n      <MeterLocalId>%s</MeterLocalId>\n    </MeterData>\n",time_utc_xml,attr_value,attribute->tagid);
		    fflush(meter->file);
		}
		else if(meter_opfm == csv){
		    //csv
		    fprintf(meter->file,"%s,%f,%s\n",time_utc_csv,attr_value,attribute->tagid);
		    fflush(meter->file);
		}
		else{
		    second_trans(interval.sample_interval,interval_string);
		    if(counter == 1){
			fprintf(meter->file,"%s,%s,,%s,\"%s\\%s|%s\\%s\",%s,%s,%s,%s,%s,%d,%s,%d,%s,,%f#","MEPMD01,19970819",meter->sender_id,meter->receiver_id,meter->customer_id,meter->customer_name,meter->account_id,meter->account_name,time_local,meter->meter_id,"OK",meter->commodity,attribute->value_unit,attribute->constant,interval_string,get_upload_interval() / get_sample_interval(),time_utc,attr_value);
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
	    else
		printf("FAILED (nb points %d)\n", rc);

	}
	if(meter_opfm == cmep)
	    fprintf(meter->file,"\n");

	(void )fprintf(stderr,"closing file %s.\n",meter->file_tmp_path);
	fclose(meter->file);
   


//******************************end of sampling data******************//


	printf("counter is %d.\n",counter);
	//upload is true
	if(upload && meter_opfm == cmep)
	{
	    char system_arguments[128];

	    //sprintf(system_arguments,"awk '{for(i=1;i<=NF;i++){a[FNR,i]=$i}}END{for(i=1;i<=NF;i++){for(j=1;j<=FNR;j++){printf a[j,i]\" \"}print \"\"}}' %s | sed s/[[:space:]]//g > %s_new",meter->file_path,meter->file_path);
	    sprintf(system_arguments,"awk 'BEGIN{FS=\"#\"}{for(i=1;i<=NF;i++){a[FNR,i]=$i}}END{for(i=1;i<=NF;i++){for(j=1;j<=FNR;j++){printf a[j,i]\"#\"}print \"\"}}' \"%s\" | sed s/#//g >> \"%s\"",meter->file_tmp_path,file_path);
	    if( system(system_arguments) != 0) 
		(void )fprintf(stderr,"system call error.\n"); 

	}
    }
    /* Free the memory */

    printf("closing the modbus\n");
    free(tab_rp_registers);
    /* Close the connection */
    modbus_close(ctx_modbus);
    modbus_free(ctx_modbus);
    printf("thread_sample: tring to unlock uart_mutex .\n");
    pthread_mutex_unlock(&uart_mutex);

    if(upload)
    {
	 /*
	 printf("######upload time######.\n");
	 counter = 0;
	 for (l=head; l; l=l->next)
	 {
	    meter = (Meter*) l->data;
	    upload_file(meter->file_path,meter->file_name);
	 }
	 */
	 printf("######upload time######.\n");
	 counter = 0;
	if(meter_opfm == xml){
	    char args[128];
	    sprintf(args,"echo \"%s\" > \"%s\"",xml_header,file_path);
	    fprintf(stderr,"args is %s.\n",args);
	    if(system(args) != 0) 
		(void )fprintf(stderr,"system call error.\n"); 
	 }
	
	if(meter_opfm == csv){
	    char args[128];
	    sprintf(args,"echo \"%s\" > \"%s\"",csv_header,file_path);
	    fprintf(stderr,"args is %s.\n",args);
	    if(system(args) != 0) 
		(void )fprintf(stderr,"system call error.\n"); 
	}
	if(meter_opfm == xml || meter_opfm == csv){
	    for (l=head; l; l=l->next)
	    {
		char args[64];
		meter = (Meter*) l->data;
		sprintf(args,"cat \"%s\" >> \"%s\"",meter->file_tmp_path,file_path);
		fprintf(stderr,"args is %s.\n",args);
		if(system(args) != 0) 
		    (void )fprintf(stderr,"system call error.\n"); 
	    }
	}
	if(meter_opfm == xml){
	    char args[128];
	    sprintf(args,"echo \"%s\" >> \"%s\"",xml_ender,file_path);
	    fprintf(stderr,"args is %s.\n",args);
	    if(system(args) != 0) 
		(void )fprintf(stderr,"system call error.\n"); 
	 }
	 char error_str[128] = {0};
	 upload_file(file_path,file_name,error_str);
    }
}
//CallBack

int SBS_MsgProc(int SrcModuleID,int MsgType,int wParam,int lParam,char* StringParam,int len)
{
	int iret = 0;
	printf("SrcModuleID=%d MessageID=%d wParam=%d lParam=%d StringParam=%s len=%d\n",
		SrcModuleID, MsgType, wParam, lParam, StringParam, len);
    switch(MsgType)
	{
        case MSG_SBS_REBOOT:
	    printf("about to send SIGINT to %d.\n",mainpid);
	    kill(mainpid,SIGINT);
            break;
        default:
            break;
    }

    return iret;
}

int SBS_GetValue_Proc(int SrcModuleID, int MessageID,int *param1,int *param2,char** str, int *len)
{
	printf("SrcModuleID=%d MessageID=%d\n ", SrcModuleID, MessageID);

    FILE *ftp_file = NULL;
    struct tm *info;
    time_t raw;
    char error_str[128] = {0};
    
    switch(MessageID)
	{
        case MSG_SBS_FTP_TEST:
	    printf("receive ftp test request.\n");

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
		printf("meter_status:tring to lock uart_mutex .\n");
		pthread_mutex_lock(&uart_mutex);
		printf("meter_status:get the uart_mutex lock.\n");
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
#if 0
		    ctx_modbus = modbus_new_rtu("/dev/ttyUSB0", uart_config->baudrate, uart_config->parity, 
							    uart_config->databits, uart_config->stopbits);
		    if (ctx_modbus == NULL) {
			sprintf(str, "0|Unable to allocate libmodbus context");
			return 0;
		    }
		    modbus_set_debug(ctx_modbus, TRUE);
		    modbus_set_error_recovery(ctx_modbus,
					    MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);
#endif
		    modbus_set_slave(ctx_modbus, meter->modbus_id);
#if 0
		    if (modbus_connect(ctx_modbus) == -1) {
			sprintf(str, "0|modbus Connection failed: %s", modbus_strerror(errno));
			modbus_free(ctx_modbus);
			return 0;
		    }

		    /* Allocate and initialize the memory to store the bits */
		    nb_points = (UT_BITS_NB > UT_INPUT_BITS_NB) ? UT_BITS_NB : UT_INPUT_BITS_NB;
		    tab_rp_bits = (uint8_t *) malloc(nb_points * sizeof(uint8_t));
		    memset(tab_rp_bits, 0, nb_points * sizeof(uint8_t));
		    /* Allocate and initialize the memory to store the bits */
		    nb_points = (UT_BITS_NB > UT_INPUT_BITS_NB) ? UT_BITS_NB : UT_INPUT_BITS_NB;
		    tab_rp_registers = (uint16_t *) malloc(nb_points * sizeof(uint16_t));
		    memset(tab_rp_registers, 0, nb_points * sizeof(uint16_t));
#endif

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

#if 0	    
		    free(tab_rp_bits);
		    free(tab_rp_registers);

		    modbus_close(ctx_modbus);
		    modbus_free(ctx_modbus);
#endif
		    printf("returned str is %s.\n",str);
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
        default:
            break;
    }
    
    return 0;
}
void sig_handler(int sig_no)
{
    if(sig_no == SIGINT)
	printf("catch signal interrupt.\n");
}

int main(int argc,char *argv[])
{
    //save the current pid;
    mainpid = getpid();

    //register signal SIGINT handler
    if(0 != pthread_mutex_init(&uart_mutex,NULL))
    {
	perror("uart_mutext init failed:");
	return -1;
    }

    if( signal(SIGINT,sig_handler) == SIG_ERR)
	printf("failed to register SIGINT handler.\n");

    if(UCI_OK != uart_init())
    {
	printf("uart init failed.\n");
	return -1;
    }
    
    if(UCI_OK != ftp_init())
    {
	printf("ftp init failed.\n");
	return -1;
    }
	
    if(UCI_OK != meter_init())
    {
	printf("meter init failed.\n");
	return -1;
    }
    Sll *l;
    Meter *addr;
    (void) fprintf(stderr,"\n---------------[ Meters printing ]----------\n");
    int n=0;
    for (l=head; l; l=l->next)
    {
        addr=(Meter*) l->data;
    	(void) fprintf(stderr,"Node: %d\n", ++n);
    	(void) fprintf(stderr,"  %d\n",addr->modbus_id);

    	Meter_Attribute *attribute = addr->attribute;
    	int i;
    	for(i = 0; i < addr->attr_num && attribute; i++,attribute++){
	   (void) fprintf(stderr,"  %d\n",attribute->addr);
	   (void) fprintf(stderr,"  %d\n",attribute->reg_num);
	   (void) fprintf(stderr,"  %d\n",attribute->constant);
	   (void) fprintf(stderr,"  %s\n",attribute->value_type);
	   (void) fprintf(stderr,"  %s\n",attribute->value_unit);
	   (void) fprintf(stderr,"\n");
   	 }
    }
    if(UCI_OK != meter_output_format())
    {
	printf("meter_output_format failed.\n");
	return -1;
    }
    if(UCI_OK != meter_output_prefix())
    {
	printf("meter_output_prefix failed.\n");
	return -1;
    }

    if ( 0 != interval_init(&interval) )
    {
	printf("interval_init failed.\n");
	return -1;
    }
#if 1
    time_t rawtime;
    struct tm *info, *info_local;

    timer_t timerid_sample;
    struct sigevent evp_sample;
    memset(&evp_sample, 0, sizeof(struct sigevent));		

    evp_sample.sigev_value.sival_int = 100;			evp_sample.sigev_notify = SIGEV_THREAD;		
    evp_sample.sigev_notify_function = timer_thread_sample;		

    if (timer_create(CLOCKID, &evp_sample, &timerid_sample) == -1) {
	perror("fail to sample timer_create");
	exit(-1);
    }

    time(&rawtime);
    /* Get GMT time */
    info = gmtime(&rawtime );
    printf("Current world clock:\n");
    printf("UTC: %4d%02d%02d%02d%02d%02d\n\n", (info->tm_year + 1900),(info->tm_mon + 1),info->tm_mday,info->tm_hour, info->tm_min,info->tm_sec);

    info_local = localtime(&rawtime);
    printf("Current local clock:\n");
    printf("UTC: %4d%02d%02d%02d%02d%02d\n\n", (info_local->tm_year + 1900),(info_local->tm_mon + 1),info_local->tm_mday,info_local->tm_hour, info_local->tm_min,info_local->tm_sec);
	
	   
    struct itimerspec it_sample;

    itimer_init(info,&it_sample,interval.sample_interval);
	
    if (timer_settime(timerid_sample, 0, &it_sample, NULL) == -1) {
	perror("fail to sample timer_settime");
	exit(-1);
    }
#endif
    printf("sbs server\n");

    /*init socket file and handle*/
    Init(MODULE_SERVER);
    FuncHostCallback FC;
    FC.pSBS_MsgProc=SBS_MsgProc;
    FC.pSBS_GetValue=SBS_GetValue_Proc;
	/*register call back func*/
    RegisterHostCallBack(FC);
    pause();
    perror("pause return reason:");
    execv(argv[0],argv);
    Clear(MODULE_SERVER);
	
    return 0;
}

