/*
**  test appending a node at the end of the list. print the result
**  and then free the memory. a user defined function is called to free
**  the data.
**
**  Development History:
**      who                  when           why
**      ma_muquit@fccc.edu   Aug-09-1998    first cut
*/


#include "sll.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <uci.h>

#define UCI_CONFIG_FILE "/etc/config/meter"

typedef struct{
	int addr;	// meter attribute register start address
	int reg_num;	//meter attribute register number
	int scale;	//attribute value scale
	char*  value_type;	//typde of the value: float,int,long{0,1,2}	
	char*  value_unit;  //type of the value unit: KWh,KVarh,Volt
	char*  total_diff;
}Meter_Attribute;

typedef struct{
	char *name;
	int modbus_id;
	int num_attribute;
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
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "scale")))
        	{
            		meter->current_attr->scale = atoi(value); 
            		printf(" scale  is  %d.\n",meter->current_attr->scale);
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
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "num_attribute")))
        	{
            		addr->num_attribute = atoi(value); 
            		printf(" num_attribute is %d.\n",addr->num_attribute);
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
    		addr->attribute = (Meter_Attribute *) malloc(addr->num_attribute * sizeof(Meter_Attribute));
    		if(addr->attribute == NULL)
    		{
				(void)fprintf(stderr," Attribute:malloc failed\n");
    			destroyNodes(&head,freeData);
				exit(-1);
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
    	for(i = 0; i < addr->num_attribute && attribute; i++,attribute++){
    		printf("  %d\n",attribute->addr);
    		printf("  %d\n",attribute->reg_num);
    		printf("  %d\n",attribute->scale);
    		printf("  %s\n",attribute->value_type);
    		printf("  %s\n",attribute->value_unit);
    		printf("\n");
   	 }
    }

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
}

