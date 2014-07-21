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

#include "sll.h"
#include <errno.h>
#include <modbus/modbus.h>

#include "unit-test.h"
#include <curl/curl.h>

#define	    METER_FILE_LOCATION	"/meters/"
#define	    UPLOAD_FILE_AS	"while-uploading.txt"
#define	    REMOTE_URL		"ftp://cwt:110weitao660@192.168.5.51:990/"  UPLOAD_FILE_AS
#define	    CLOCKID CLOCK_REALTIME

//#define USER_UCI_SAMPLE_INTERVAL (85 * SECSPERMIN)
//#define USER_UCI_UPLOAD_INTERVAL (30 * SECSPERMIN)
#define	    USER_UCI_SAMPLE_INTERVAL	(10)
#define	    USER_UCI_UPLOAD_INTERVAL	(80)
#define	    SECSPERHOUR			 3600
#define	    SECSPERMIN			60	
#define	    INTERVAL_CMEP		"00000005"


#define	    METER_UCI_CONFIG_FILE   "/etc/config/meter"
#define	    GATEWAY_UCI_CONFIG_FILE "/etc/config/gateway"
#define	    FTP_UCI_CONFIG_FILE	    "/etc/config/ftp"

#define	    UCI_SAMPLE_INTERVAL		"gateway.general.sample_interval"
#define	    CUSTOM_UCI_SAMPLE_INTERVAL  "gateway.general.custom_sample_interval"
#define	    UCI_GATEWAY_TYPE		"gateway.general.gw_type"
#define	    UCI_FILE_PREFIX		"gateway.general.file_prefix"
#define	    UCI_V2_TYPE			"gateway.general.v2_type"
#define	    UCI_UPLOAD_INTERVAL		"ftp.general.upload_interval"
#define	    CUSTOM_UCI_SAMPLE_INTERVAL	"ftp.general.custom_upload_interval"
#define	    UCI_FTP_ADDR		"ftp.general.ftp_addr"
#define	    UCI_FTP_PORT		"ftp.general.ftp_port"
#define	    UCI_FTP_USER_NAME		"ftp.general.ftp_user_name"
#define	    UCI_FTP_PASSWORD		"ftp.general.ftp_password"
#define	    UCI_FTP_FILE_PATH		"ftp.general.ftp_file_path"
#define	    UCI_FTP_SSL			"ftp.general.ftp_ssl"

typedef struct{
    int addr;	// meter attribute register start address
    int reg_num;	//meter attribute register number
    int constant;	//attribute value constant
    char* value_type;	//typde of the value: float,int,long{0,1,2}	
    char* value_unit;  //type of the value unit: KWh,KVarh,Volt
    char* total_diff;
    char* tag_id;
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
	for(i = 0; i< (*addr)->num_attribute; i++){
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


/*  
	global meter single linked list head
	super important, be aware of mutual exclusion
	global shared resources
*/
static struct uci_context * ctx = NULL; 
Sll *head = NULL;

int load_attr_config()
{
    struct uci_package * pkg = NULL;
    struct uci_element *e;
    const char *value;
    int modbus_id;
    Sll *lp;
    Meter *meter;
    int ret = 0;


    ctx = uci_alloc_context(); 
    if (UCI_OK != uci_load(ctx, METER_UCI_CONFIG_FILE, &pkg)){
	ret = -1;
        goto cleanup; 
    }
	


    uci_foreach_element(&pkg->sections, e)
    {
    	struct uci_section *s = uci_to_section(e);
	
	printf("section s's type is %s.\n",s->type);

	if(!strcmp("attr",s->type))
	{
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "name")))
        	{
            		printf("this attr belongs to meter: %s.\n",value);

			for(lp=head;lp;lp=lp->next)
			{
				meter = (Meter *)lp->data;
				if(!strcmp(meter->name,value))
					break;
			}
					
		}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "addr")))
        	{
            		meter->current_attr->addr = atoi(value); 
            		printf(" attr addr is  %d.\n",meter->current_attr->addr);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "reg_num")))
        	{
            		meter->current_attr->reg_num = atoi(value); 
            		printf(" reg num  is  %d.\n",meter->current_attr->reg_num);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "constant")))
        	{
            		meter->current_attr->constant = atoi(value); 
            		printf(" constant  is  %d.\n",meter->current_attr->constant);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "value_type")))
        	{
            		meter->current_attr->value_type = strdup(value); 
            		printf(" value_type  is  %s.\n",meter->current_attr->value_type);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "value_unit")))
        	{
            		meter->current_attr->value_unit = strdup(value); 
            		printf(" value_unit  is  %s.\n",meter->current_attr->value_unit);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "total_diff")))
        	{
            		meter->current_attr->total_diff = strdup(value); 
            		printf(" total_diff is %s.\n",meter->current_attr->value_unit);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "tag_id")))
        	{
            		meter->current_attr->tag_id = strdup(value); 
            		printf(" tag_id is %s.\n",meter->current_attr->tag_id);
        	}
					
		meter->current_attr++;

	}

    }
    uci_unload(ctx, pkg); // 释放 pkg 
cleanup:
    uci_free_context(ctx);
    ctx = NULL;
    return ret;
}

int load_meter_config()
{
    Sll *l,*new=NULL;
    Meter *meter;
    int n=0;
    int ret = 0;


    struct uci_package * pkg = NULL;
    struct uci_element *e;
    const char *value;


    ctx = uci_alloc_context(); 
    if (UCI_OK != uci_load(ctx, METER_UCI_CONFIG_FILE, &pkg)){
	ret = -1;
        goto cleanup; 
    }


    uci_foreach_element(&pkg->sections, e)
    {
        struct uci_section *s = uci_to_section(e);
	
	printf("section s's type is %s.\n",s->type);

	if(!strcmp("meter",s->type)) //this section is a meter
	{

		//find a meter section, allocate a new meter;
    		meter = (Meter*) malloc(sizeof(Meter));
    		if (meter == NULL) {
        		(void) fprintf(stderr," malloc failed\n");
    			destroyNodes(&head,freeData);
        		ret = -1;
			goto cleanup;
    		}

    		(void) fprintf(stderr,"\n---------------[ appending ]----------\n");
    		new=allocateNode((void *) meter);
    		appendNode(&head,&new);
		printf("this seciton is a meter.\n");

		//initialize meter
		if(s->anonymous == false ){
			meter->name = strdup(s->e.name);
            		printf(" meter name is  %s.\n",meter->name);
		}

        	if (NULL != (value = uci_lookup_option_string(ctx, s, "modbus_id")))
        	{
            		meter->modbus_id = atoi(value); 
            		printf(" modbus_id is %d.\n",meter->modbus_id);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "attr_num")))
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
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "sender_id")))
        	{
            		meter->sender_id = strdup(value); 
            		printf(" sender_id is %s.\n",meter->sender_id);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "receiver_id")))
        	{
            		meter->receiver_id = strdup(value); 
            		printf(" receiver_id is %s.\n",meter->receiver_id);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "customer_id")))
        	{
            		meter->customer_id = strdup(value); 
            		printf(" customer_id is %s.\n",meter->customer_id);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "customer_name")))
        	{
            		meter->customer_name = strdup(value); 
            		printf(" customer_name is %s.\n",meter->customer_name);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "account_id")))
        	{
            		meter->account_id = strdup(value); 
            		printf(" account_id is %s.\n",meter->account_id);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "account_name")))
        	{
            		meter->account_name = strdup(value); 
            		printf(" account_name is %s.\n",meter->account_name);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "meter_id")))
        	{
            		meter->meter_id = strdup(value); 
            		printf(" meter_id is %s.\n",meter->meter_id);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "commodity")))
        	{
            		meter->commodity = strdup(value); 
            		printf(" commodity is %s.\n",meter->commodity);
        	}

		addr->current_attr = addr->attribute;
	}

    }
    uci_unload(ctx, pkg); // 释放 pkg 
cleanup:
    uci_free_context(ctx);
    ctx = NULL;
    return ret;
}

void meter_init(void)
{
    load_meter_config();
    load_attr_config();
}
enum output_format
{
    cmep = 0;
    xml;
    csv;
};
output_format meter_opfm;
char output_file_prefix[8] = {0};

int meter_output_prefix(void){
    struct uci_ptr p;
    int value;

    ctx = uci_alloc_context();
    if (uci_lookup_ptr (ctx, &p, UCI_FILE_PREFIX, true) != UCI_OK)
    {
	uci_perror (ctx, "XXX");
	return -1;
    }

    printf("%s\n", p.o->v.string);
    strcpy(output_file_prefix,p.o->v.string);

    return 0;

}

int meter_output_format(void){
    struct uci_ptr p;
    int value;

    ctx = uci_alloc_context();
    if (uci_lookup_ptr (ctx, &p, UCI_GATEWAY_TYPE, true) != UCI_OK)
    {
	uci_perror (ctx, "XXX");
	return -1;
    }

    printf("%s\n", p.o->v.string);
    if(!strcmp(p.o->v.string,"cmep")){
	meter_opfm = cmep;
	uci_free_context (ctx);
	return 0;
    }
    else{

	if (uci_lookup_ptr (ctx, &p, UCI_V2_TYPE, true) != UCI_OK)
	{
	    uci_perror (ctx, "XXX");
	    uci_free_context (ctx);
	    return -1;
	}
	if(!strcmp(p.o->v.string,"xml")){
	    meter_opfm = xml;
	    uci_free_context (ctx);
	    return 0;
	}
	else if(!strcmp(p.o->v.string,"csv")){
	    meter_opfm = csv;
	    uci_free_context (ctx);
	    return 0;
	}
	else{
	    uci_free_context (ctx);
	    return -1;
	}
    }
}


typedef struct{
    char *ftp_addr;
    char *ftp_port;
    char *ftp_user_name;
    char *ftp_password;
    char *ftp_file_path;
    char *ftp_ssl;
}FTP_config;

FTP_config *ftp_config;

int ftp_init(void)
{
    struct uci_ptr p;
    int ret = 0;

    ftp_config = (FTP_config *)malloc(sizeof(FTP_config));
    if(ftp_config == NULL){
	printf("failed to alloc memeory.\n");
	ret = -1;
	goto cleanup;
    }

    ctx = uci_alloc_context();
    if (uci_lookup_ptr (ctx, &p, UCI_FTP_ADDR, true) != UCI_OK)
    {
	uci_perror (ctx, "XXX");
	ret = -1;
	goto cleanup;
    }
    printf("%s\n", p.o->v.string);
    ftp_config->ftp_addr = strdup(p.o->v.string);

    if (uci_lookup_ptr (ctx, &p, UCI_FTP_PORT, true) != UCI_OK)
    {
	uci_perror (ctx, "XXX");
	ret = -1;
	goto cleanup;
    }
    printf("%s\n", p.o->v.string);
    ftp_config->ftp_port = strdup(p.o->v.string);

    if (uci_lookup_ptr (ctx, &p, UCI_FTP_USER_NAME, true) != UCI_OK)
    {
	uci_perror (ctx, "XXX");
	ret = -1;
	goto cleanup;
    }
    printf("%s\n", p.o->v.string);
    ftp_config->ftp_user_name = strdup(p.o->v.string);

    if (uci_lookup_ptr (ctx, &p, UCI_FTP_PASSWORD, true) != UCI_OK)
    {
	uci_perror (ctx, "XXX");
	ret = -1;
	goto cleanup;
    }
    printf("%s\n", p.o->v.string);
    ftp_config->ftp_password = strdup(p.o->v.string);

    if (uci_lookup_ptr (ctx, &p, UCI_FTP_FILE_PATH, true) != UCI_OK)
    {
	uci_perror (ctx, "XXX");
	ret = -1;
	goto cleanup;
    }
    printf("%s\n", p.o->v.string);
    ftp_config->ftp_file_path = strdup(p.o->v.string);

    if (uci_lookup_ptr (ctx, &p, UCI_FTP_SSL, true) != UCI_OK)
    {
	uci_perror (ctx, "XXX");
	ret = -1;
	goto cleanup;
    }
    printf("%s\n", p.o->v.string);
    ftp_config->ftp_SSL = strdup(p.o->v.string);

cleanup:
    uci_free_context(ctx);
    ctx = NULL;
    return ret;
}
//unit:minutes
typedef struct {
	int sample_interval;
	int upload_interval;
}Interval;

/*  
	user defined sample interval and upload interval
	super important, be aware of mutual exclusion
	global shared resources
*/
Interval interval;
int get_interval(char *interval_name, char *custom_interval_name)
{
    struct uci_ptr p;
    int value;

    ctx = uci_alloc_context();
    if (uci_lookup_ptr (ctx, &p, interval_name, true) != UCI_OK)
    {
	uci_perror (ctx, "XXX");
	return -1;
    }

    printf("%s\n", p.o->v.string);
    value = atoi(p.o->v.string);
    if(value){
	uci_free_context (ctx);
	return value;
    }
    else{

	if (uci_lookup_ptr (ctx, &p, custom_interval_name, true) != UCI_OK)
	{
	    uci_perror (ctx, "XXX");
	    return -1;
	}
	value = atoi(p.o->v.string);
	uci_free_context (ctx);
	return value;
    }

}

int get_sample_interval()
{
    return get_interval(UCI_SAMPLE_INTERVAL,CUSTOM_UCI_SAMPLE_INTERVAL);
}

int get_upload_interval()
{
    return get_interval(UCI_UPLOAD_INTERVAL,CUSTOM_UCI_UPLOAD_INTERVAL);
}

void interval_init(Interval *interval)
{
    //before the create the interval timer, we need to initialize the interval first
    printf("about to initialize sample interval and upload interval\n");
    interval->sample_interval = get_sample_interval();
    interval->upload_interval = get_upload_interval();

    printf("sample interval:%d\n",interval->sample_interval);
    printf("upload interval:%d\n\n",interval->upload_interval);
}

#define SECSPERHOUR 3600
#define SECSPERMIN 60	
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
    int secs_interval = it_interval * SECSPERMIN;
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
static void upload_file(char *file_to_upload, char *rename_to)
{
    CURL *curl;
    CURLcode res;
    FILE *hd_src;
    struct stat file_info;
    curl_off_t fsize;
	
    struct curl_slist *headerlist=NULL;
    static const char buf_1 [] = "RNFR " UPLOAD_FILE_AS;
    static char buf_2 [64];
    sprintf(buf_2,"RNTO %s",rename_to);
    /* get the file size of the local file */
    if(stat(file_to_upload, &file_info)) {
	printf("Couldnt open '%s': %s\n", file_to_upload, strerror(errno));
	exit -1;
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
	headerlist = curl_slist_append(headerlist, buf_1);
	headerlist = curl_slist_append(headerlist, buf_2);
		
	/* We activate SSL and we require it for both control and data */
	curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
	
	/* Switch on full protocol/debug output */
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
	
	curl_easy_setopt (curl, CURLOPT_SSL_VERIFYPEER, 0L); 
	curl_easy_setopt (curl, CURLOPT_SSL_VERIFYHOST, 0L); 
				
	/* we want to use our own read function */
	curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
	
	/* enable uploading */
	curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
	
	/* specify target */
	curl_easy_setopt(curl,CURLOPT_URL, REMOTE_URL);
			
	/* pass in that last of FTP commands to run after the transfer */
	curl_easy_setopt(curl, CURLOPT_POSTQUOTE, headerlist);
		
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
	if(res != CURLE_OK)
	    fprintf(stderr, "curl_easy_perform() failed: %s\n",
		curl_easy_strerror(res));
			
	/* clean up the FTP commands list */
	curl_slist_free_all (headerlist);
			
	/* always cleanup */
	curl_easy_cleanup(curl);
    }
    fclose(hd_src); /* close the local file */
    curl_global_cleanup();
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

    if(counter == Interval.upload_interval / Interval.sample_interval)
	upload = true;

    char time_utc[16] = {0};
    char time_local[16] = {0};
    char time_local_file[24] = {0};
    time_t rawtime;
    struct tm *info, *info_local;
    time(&rawtime);
    /* Get GMT time */
    info = gmtime(&rawtime );
    sprintf(time_utc,"4d02d02d02d02d02d",(1900 + info->tm_year),(info->tm_mon + 1),info->tm_mday,info->tm_hour,info->tm_min);
    /* Get LOCAL time */
    info_local = localtime(&rawtime);
    sprintf(time_local,"4d02d02d02d02d02d",(1900 + info_local->tm_year),(info_local->tm_mon + 1),info_local->tm_mday,info_local->tm_hour,info_local->tm_min);
    sprintf(time_local_file,"4d02d02d02d02d02d02d",(1900 + info_local->tm_year),(info_local->tm_mon + 1),info_local->tm_mday,info_local->tm_hour,info_local->tm_min,info_local->tm_sec);

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
    char file_path[64];
    char file_tmp_path[64];
    char file_name[64];

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
	    sprintf(file_path,"%s%s_tmp",METER_FILE_LOCATION,file_name);
	    meter->file_path = strdup(file_path);
	    meter->file_tmp_path = strdup(file_tmp_path);
	    meter->file_name = strdup(file_name);


	    if (meter->file_path == NULL || meter->file_tmp_path == NULL || meter->file_name) 
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
	if(meter->file == NULL)
	    perror("fopen failed:");

	//if(counter == 1){
	//	fprintf(meter->file_xml,"<?xml_version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\"?>\n<XML>\n"  "<action type=\"update\">\n");
	//		fprintf(meter->file_csv,"###ACTION:UPDATE ENTITY:MeterData SCHEMA:Default VERSION:1.0.0\n");
	//}


    	uint8_t *tab_rp_bits;
    	uint16_t *tab_rp_registers;
    	uint16_t *tab_rp_registers_bad;
    	modbus_t *ctx;
    	int i;
    	uint8_t value;
    	int nb_points;
    	int rc;
    	float real;
    	uint32_t ireal;
    	struct timeval old_response_timeout;
    	struct timeval response_timeout;
    	int use_backend;
    	uint16_t tmp_value;
    	float float_value;
	char interval_string[16];

	/* gateway RS485 port config
	Portname: /dev/ttyUSB0
	Baudrate: 19200
	Even/Odd: None
	Databits: 8
	Stopbits: 1
	*/
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
	printf("num_attribute is %d.\n",meter->num_attribute);
	for(i = 0; i < meter->num_attribute && attribute; i++,attribute++){

    	    /* Single register */
	    printf("reading register.\n");
	    rc = modbus_read_registers(ctx,attribute->addr,attribute->reg_num,tab_rp_registers);
    	    if (rc == attribute->reg_num) 
	    {
	        //xml
		fprintf(meter->file_xml,"    <MeterData schema=\"Default\" version=\"1.0.0\">\n      <AcquistionDateTime>%4d-%02d-%02dT%02d:%02d:%02d.0000000Z</AcquisitionDateTime>\n      <Value>%f</Value>\n      <MeterLocalId>%s%d_%s_%s_%d</MeterLocalId>\n    </MeterData>\n",(info->tm_year + 1900),(info->tm_mon + 1),info->tm_mday,info->tm_hour,info->tm_min,info->tm_sec,modbus_get_float_cdab(tab_rp_registers),"sbs",meter->modbus_id,meter->commodity,attribute->value_unit,attribute->scale);
		fflush(meter->file_xml);
		//csv
		fprintf(meter->file_csv,"%4d-%02d-%02dT%02d:%02d:%02d.0000000Z,%f,%s%d_%s_%s_%d\n",(info->tm_year + 1900),(info->tm_mon + 1),info->tm_mday,info->tm_hour,info->tm_min,info->tm_sec,modbus_get_float_cdab(tab_rp_registers),"sbs",meter->modbus_id,meter->commodity,attribute->value_unit,attribute->scale);
		fflush(meter->file_csv);

		second_trans(interval.sample_interval,interval_string);
		if(counter == 1){
		    fprintf(meter->file,"%s%s%s,%s,%s,%s,%d,%s,%d,%4d%02d%02d%02d%02d,,%f#","MEPMD01,19970819,Schneider Electric,,,","SECN\\Schneider Electric China|SBMV\\Beijing Middle Voltage Plant","201308010358,SBMV.MCSET.FHU_HVAC_Lighting1|129","OK",meter->commodity,attribute->value_unit,attribute->scale,interval_string,get_upload_interval() / get_sample_interval(),(info->tm_year + 1900),(info->tm_mon + 1),info->tm_mday,info->tm_hour,info->tm_min,modbus_get_float_cdab(tab_rp_registers));
		}
		else{	
		    if( i == meter->num_attribute -1){	
			fprintf(meter->file,",,,%f",modbus_get_float_cdab(tab_rp_registers));
		    }
		    else{
			fprintf(meter->file,",,,%f#",modbus_get_float_cdab(tab_rp_registers));
		    }
		    fflush(meter->file);
		}
	    }
	    else
		printf("FAILED (nb points %d)\n", rc);

	}
	fprintf(meter->file,"\n");

	if(counter == get_upload_interval() / get_sample_interval())
	    fprintf(meter->file_xml,"  </action>\n</XML>\n");
	    (void )fprintf(stderr,"closing file %s.\n",meter->file_tmp_path);
	    (void )fprintf(stderr,"closing file %s.\n",meter->file_tmp_path_xml);
	    (void )fprintf(stderr,"closing file %s.\n",meter->file_tmp_path_csv);
	    fclose(meter->file);
	    fclose(meter->file_xml);
	    fclose(meter->file_csv);

close:
    	/* Free the memory */

    	printf("closing the modbus\n");
    	free(tab_rp_registers);

    	/* Close the connection */
    	modbus_close(ctx);
    	modbus_free(ctx);
   


//******************************end of sampling data******************//


	printf("counter is %d.\n",counter);
	//upload is true
	if(upload)
	{
	    char system_arguments[128];

	    //sprintf(system_arguments,"awk '{for(i=1;i<=NF;i++){a[FNR,i]=$i}}END{for(i=1;i<=NF;i++){for(j=1;j<=FNR;j++){printf a[j,i]\" \"}print \"\"}}' %s | sed s/[[:space:]]//g > %s_new",meter->file_path,meter->file_path);
	    sprintf(system_arguments,"awk 'BEGIN{FS=\"#\"}{for(i=1;i<=NF;i++){a[FNR,i]=$i}}END{for(i=1;i<=NF;i++){for(j=1;j<=FNR;j++){printf a[j,i]\"#\"}print \"\"}}' %s | sed s/#//g > %s",meter->file_tmp_path,meter->file_path);
	    if( system(system_arguments) != 0) 
		(void )fprintf(stderr,"system call error.\n"); 

	}
    }
    if(counter ==  get_upload_interval() / get_sample_interval() )
    {
	 printf("######upload time######.\n");
	 counter = 0;
	 for (l=head; l; l=l->next)
	 {
	    meter = (Meter*) l->data;
	    upload_file(meter->file_path,meter->file_name);
	    upload_file(meter->file_path_xml,meter->file_name_xml);
	    upload_file(meter->file_path_csv,meter->file_name_csv);
	 }
    }
}

int main()
{
	
    char interval_string[16];
    second_trans(get_sample_interval(),interval_string);
    (void) fprintf(stderr,"sample interval string is %s\n",interval_string);

    Sll *l;
    Meter *addr;

    meter_init();
    (void) fprintf(stderr,"\n---------------[ printing ]----------\n");
    int n=0;
    for (l=head; l; l=l->next)
    {
        addr=(Meter*) l->data;
    	(void) fprintf(stderr,"Node: %d\n", ++n);
    	(void) fprintf(stderr,"  %d\n",addr->modbus_id);

    	Meter_Attribute *attribute = addr->attribute;
    	int i;
    	for(i = 0; i < addr->num_attribute && attribute; i++,attribute++){
	   (void) fprintf(stderr,"  %d\n",attribute->addr);
	   (void) fprintf(stderr,"  %d\n",attribute->reg_num);
	   (void) fprintf(stderr,"  %d\n",attribute->scale);
	   (void) fprintf(stderr,"  %s\n",attribute->value_type);
	   (void) fprintf(stderr,"  %s\n",attribute->value_unit);
	   (void) fprintf(stderr,"\n");
   	 }
    }

    meter_output_format();

    interval_init(&interval);

    time_t rawtime;
    struct tm *info, *info_local;

    timer_t timerid_sample;
    struct sigevent evp_sample;
    memset(&evp_sample, 0, sizeof(struct sigevent));		

    evp_sample.sigev_value.sival_int = 100;			
    evp_sample.sigev_notify = SIGEV_THREAD;		
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
	
    pause();
	
    return 0;
}

