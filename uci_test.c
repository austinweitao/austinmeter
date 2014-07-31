#include <stdio.h>
#include <string.h>
#include <uci.h>
#include <stdlib.h>
#define UCI_CONFIG_FILE "/etc/config/metervalue"

static void uci_show_value(struct uci_option *o)
{
	struct uci_element *e;
	bool sep = false;

	switch(o->type) {
	case UCI_TYPE_STRING:
		printf("%s\n", o->v.string);
		break;
	default:
		printf("<unknown>\n");
		break;
	}
}
int main (int argc, char *argv[])
{
    struct uci_context *ctx;
    struct uci_element *e;
    struct uci_ptr ptr;
    int ret = UCI_OK;

    if (argc != 3){
	printf("plz specify what you want to get.\n");
	return 255;
    }
    ctx = uci_alloc_context();
    if (!ctx) {
	fprintf(stderr, "Out of memory\n");
	return 1;
    }

    if (uci_lookup_ptr(ctx, &ptr, argv[1], true) != UCI_OK) {
	printf("lookup_ptr failed.\n");
	return 1;
    }
    ptr.value = argv[2];

    printf("ptr.package is %s.\n",ptr.package);
    printf("ptr.section is %s.\n",ptr.section);
    printf("ptr.option is %s.\n",ptr.option);
    printf("ptr.value is %s.\n",ptr.value);
    
    e = ptr.last;
    if (!(ptr.flags & UCI_LOOKUP_COMPLETE)) {
	ctx->err = UCI_ERR_NOTFOUND;
	printf("complete failed \n");
	return 1;
    }
    /*
    switch(e->type) {
	case UCI_TYPE_SECTION:
	    printf("%s\n", ptr.s->type);
	    break;
	case UCI_TYPE_OPTION:
	    uci_show_value(ptr.o);
	    break;
	 default:
	    break;
    }
    */
    ret = uci_set(ctx, &ptr);
    if (uci_commit(ctx, &ptr.p, false) != UCI_OK) {
	printf("uci commit failed.\n");
	ret = 1;
    }

    uci_unload(ctx,ptr.p);
    uci_free_context (ctx);
    return 0;
}
