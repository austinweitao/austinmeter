#include <stdio.h>
#include <string.h>
#include <uci.h>
#include <stdlib.h>

int main (int argc, char *argv[])
{
  struct uci_context *ctx;

  ctx = uci_alloc_context ();

  struct uci_ptr ptr = {
	.package = "cwt_config",
	.section = "server",
	.option = "value",
	.value = "256",
  };
  uci_set(ctx,&ptr);
  uci_commit(ctx,&ptr.p,false);
  uci_unload(ctx,ptr.p);
  uci_free_context (ctx);
  return 0;
}
