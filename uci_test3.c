#include "sll.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <uci.h>

#define UCI_CONFIG_FILE "/etc/config/meter"
#define METER_LASTVALUE_UCI_CONFIG_FILE "/etc/config/meter_lastvalue"

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

}Meter;

static void freeData(void **data);
static struct uci_context * ctx = NULL; //定义一个UCI上下文的静态变量.
static Sll *head = NULL;
/*********************************************
*   载入配置文件,并遍历Section.
*/
static int uci_do_add(struct uci_context *ctx,struct uci_package *pkg,char *section_type,Meter *meter)
{
	struct uci_section *s = NULL;
	int ret;
	struct uci_ptr ptr;
	struct uci_element *e = NULL;

	ret = uci_add_section(ctx, pkg, section_type, &s);
	if (ret != UCI_OK){
		fprintf(stderr,"add  section failed.\n");
		return ret;
	}

	char ptr_str[64] = {0};
	sprintf(ptr_str,"meter_lastvalue.%s.modbus_id=%d",s->e.name,meter->modbus_id);
	printf("ptr_str is %s.\n",ptr_str);

	if (uci_lookup_ptr(ctx, &ptr, ptr_str, true) != UCI_OK) { printf("lookup_ptr failed.\n");
	    return 1;
	}
    
	ret = uci_set(ctx, &ptr);
	if (ret != UCI_OK){
		fprintf(stderr,"ret is not ok.\n");
		return ret;
	}

	int i;
	Meter_Attribute *attribute = meter->attribute;

	for(i = 0; i < meter->attr_num; i++,attribute++)
	{
	    sprintf(ptr_str,"meter_lastvalue.%s.%s=%d",s->e.name,attribute->value_unit,777);
	    printf("ptr_str is %s.\n",ptr_str);
	    if ((ret = uci_lookup_ptr(ctx, &ptr, ptr_str, true)) != UCI_OK) { 
		printf("lookup_ptr failed.\n");
		return ret;
	    }
	    ret = uci_set(ctx, &ptr);
	    if (ret != UCI_OK){
		fprintf(stderr,"ret is not ok.\n");
		return ret;
	    }
	
	}
	if ( UCI_OK != (ret = uci_commit(ctx, &pkg, false))) {
	    printf("uci commit failed.\n");
	    return ret;
	}



	return ret;
}
bool load_meter_lastvalue_config()
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
    char *tmp;
    const char *value;
    int modbus_id;
    struct uci_section *s;


    ctx = uci_alloc_context(); 
    if (UCI_OK != uci_load(ctx, METER_LASTVALUE_UCI_CONFIG_FILE, &pkg))
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
		if (NULL != (value = uci_lookup_option_string(ctx, s, "modbus_id"))) 
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
	    if ( UCI_OK != uci_do_add(ctx,pkg,"meter_lastvalue",meter))
		fprintf(stderr,"uci do add failed.\n");
	}
    }

cleanup:
    uci_free_context(ctx);
    ctx = NULL;
}
bool load_attr_config()
{
    Sll
        *l,
        *new=NULL;
    Meter
        *addr;

    int
        n=0;


    struct uci_package * pkg = NULL;
    struct uci_element *e;
    char *tmp;
    const char *value;


    ctx = uci_alloc_context(); // 申请一个UCI上下文.
    if (UCI_OK != uci_load(ctx, UCI_CONFIG_FILE, &pkg))
        goto cleanup; //如果打开UCI文件失败,则跳到末尾 清理 UCI 上下文.


    /*遍历UCI的每一个节*/
    uci_foreach_element(&pkg->sections, e)
    {
		int modbus_id;
        struct uci_section *s = uci_to_section(e);
		Sll *lp;
		Meter *meter;
	
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
            		printf(" total_diff  %s.\n",meter->current_attr->value_unit);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "tag_id")))
        	{
            		meter->current_attr->tag_id = strdup(value); 
            		printf(" tag_id  %s.\n",meter->current_attr->tag_id);
        	}
					
			meter->current_attr++;

		}

    }
    uci_unload(ctx, pkg); // 释放 pkg 
cleanup:
    uci_free_context(ctx);
    ctx = NULL;


}

bool load_meter_config()
{
    Sll
        *l,
        *new=NULL;
    Meter
        *addr;

    int
        n=0;


    struct uci_package * pkg = NULL;
    struct uci_element *e;
    char *tmp;
    const char *value;


    ctx = uci_alloc_context(); // 申请一个UCI上下文.
    if (UCI_OK != uci_load(ctx, UCI_CONFIG_FILE, &pkg))
        goto cleanup; //如果打开UCI文件失败,则跳到末尾 清理 UCI 上下文.


    /*遍历UCI的每一个节*/
    uci_foreach_element(&pkg->sections, e)
    {
        struct uci_section *s = uci_to_section(e);
	
	printf("section s's type is %s.\n",s->type);

	if(!strcmp("meter",s->type)) //this section is a meter
	{

		
			//find a meter section, allocate a new meter;
    		addr=(Meter*) malloc(sizeof(Meter));
    		if (addr == NULL) {
        		(void) fprintf(stderr," malloc failed\n");
    			destroyNodes(&head,freeData);
        		exit(-1);
    		}

    		(void) fprintf(stderr,"\n---------------[ appending ]----------\n");
    		new=allocateNode((void *) addr);
    		appendNode(&head,&new);
			printf("this seciton is a meter.\n");

			//initialize meter

		if(s->anonymous == false ){
			addr->name = strdup(s->e.name);
            		printf(" meter name is  %s.\n",addr->name);
		}

        	if (NULL != (value = uci_lookup_option_string(ctx, s, "modbus_id")))
        	{
            		addr->modbus_id = atoi(value); 
            		printf(" modbus_id is %d.\n",addr->modbus_id);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "attr_num")))
        	{
            		addr->attr_num = atoi(value); 
            		printf(" attr_num is %d.\n",addr->attr_num);
			//should verify the existance of option attr_num before alloc memory for attr
			addr->attribute = (Meter_Attribute *) malloc(addr->attr_num * sizeof(Meter_Attribute));
			if(addr->attribute == NULL)
			{
				(void)fprintf(stderr," Attribute:malloc failed\n");
				destroyNodes(&head,freeData);
				exit(-1);
			}
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "sender_id")))
        	{
            		addr->sender_id = strdup(value); 
            		printf(" sender_id is %s.\n",addr->sender_id);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "receiver_id")))
        	{
            		addr->receiver_id = strdup(value); 
            		printf(" receiver_id is %s.\n",addr->receiver_id);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "customer_id")))
        	{
            		addr->customer_id = strdup(value); 
            		printf(" customer_id is %s.\n",addr->customer_id);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "customer_name")))
        	{
            		addr->customer_name = strdup(value); 
            		printf(" customer_name is %s.\n",addr->customer_name);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "account_id")))
        	{
            		addr->account_id = strdup(value); 
            		printf(" account_id is %s.\n",addr->account_id);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "account_name")))
        	{
            		addr->account_name = strdup(value); 
            		printf(" account_name is %s.\n",addr->account_name);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "meter_id")))
        	{
            		addr->meter_id = strdup(value); 
            		printf(" meter_id is %s.\n",addr->meter_id);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "commodity")))
        	{
            		addr->commodity = strdup(value); 
            		printf(" commodity is %s.\n",addr->commodity);
        	}

			addr->current_attr = addr->attribute;
	}

    }
    uci_unload(ctx, pkg); // 释放 pkg 
cleanup:
    uci_free_context(ctx);
    ctx = NULL;
}

int main (int argc,char **argv) 
{
    load_meter_config();
    load_attr_config();

    int n=0;
	Sll *l;
	Meter *addr;

    for (l=head; l; l=l->next)
    {
        addr=(Meter*) l->data;
    	printf("Node: %d\n", ++n);
    	printf("  %d\n",addr->modbus_id);

    	Meter_Attribute *attribute = addr->attribute;
    	int i;
    	for(i = 0; i < addr->attr_num && attribute; i++,attribute++){
    		printf("  %d\n",attribute->addr);
    		printf("  %d\n",attribute->reg_num);
    		printf("  %d\n",attribute->constant);
    		printf("  %s\n",attribute->value_type);
    		printf("  %s\n",attribute->value_unit);
    		printf("\n");
   	 }
    }
    load_meter_lastvalue_config();

    exit(0);
}


/*
** routine to free the user data
*/

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
}

