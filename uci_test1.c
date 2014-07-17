#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <uci.h>

#define UCI_CONFIG_FILE "/etc/config/meter"
static struct uci_context * ctx = NULL; //定义一个UCI上下文的静态变量.
/*********************************************
*   载入配置文件,并遍历Section.
*/
bool load_config()
{
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

	if(!strcmp)

	if(!strcmp("meter",s->type)) //this section is a meter
	{
		printf("this seciton is a meter.\n");
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "modbus_id")))
        	{
            		tmp = strdup(value); //如果您想持有该变量值，一定要拷贝一份。当 pkg销毁后value的内存会被释放。
            		printf("%s's modbus_id is %s.\n",s->e.name,value);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "num_attr")))
        	{
            		tmp = strdup(value); //如果您想持有该变量值，一定要拷贝一份。当 pkg销毁后value的内存会被释放。
            		printf("%s's num_attr is %s.\n",s->e.name,value);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "sender_id")))
        	{
            		tmp = strdup(value); //如果您想持有该变量值，一定要拷贝一份。当 pkg销毁后value的内存会被释放。
            		printf("%s's sender_id is %s.\n",s->e.name,value);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "customer_id")))
        	{
            		tmp = strdup(value); //如果您想持有该变量值，一定要拷贝一份。当 pkg销毁后value的内存会被释放。
            		printf("%s's customer_id is %s.\n",s->e.name,value);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "customer_name")))
        	{
            		tmp = strdup(value); //如果您想持有该变量值，一定要拷贝一份。当 pkg销毁后value的内存会被释放。
            		printf("%s's customer_name is %s.\n",s->e.name,value);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "account_id")))
        	{
            		tmp = strdup(value); //如果您想持有该变量值，一定要拷贝一份。当 pkg销毁后value的内存会被释放。
            		printf("%s's account_id is %s.\n",s->e.name,value);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "account_name")))
        	{
            		tmp = strdup(value); //如果您想持有该变量值，一定要拷贝一份。当 pkg销毁后value的内存会被释放。
            		printf("%s's account_name is %s.\n",s->e.name,value);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "meter_id")))
        	{
            		tmp = strdup(value); //如果您想持有该变量值，一定要拷贝一份。当 pkg销毁后value的内存会被释放。
            		printf("%s's meter_id is %s.\n",s->e.name,value);
        	}
        	if (NULL != (value = uci_lookup_option_string(ctx, s, "commodity")))
        	{
            		tmp = strdup(value); //如果您想持有该变量值，一定要拷贝一份。当 pkg销毁后value的内存会被释放。
            		printf("%s's commodity is %s.\n",s->e.name,value);
        	}
	}
        // 如果您不确定是 string类型 可以先使用 uci_lookup_option() 函数得到Option 然后再判断.
        // Option 的类型有 UCI_TYPE_STRING 和 UCI_TYPE_LIST 两种.


    }
    uci_unload(ctx, pkg); // 释放 pkg 
cleanup:
    uci_free_context(ctx);
    ctx = NULL;
}
int main(int argc, char* argv[])
{
	
	load_config();
}

