#include <stdio.h>
#include <string.h>
#include <uci.h>
#include <stdlib.h>

int main (int argc, char *argv[])
{
  struct uci_context *c;
  struct uci_ptr p;

  if(argc != 2){
	printf("please specify one section type\n");
	exit -1;
  }
  char *a = strdup (argv[1]);

  printf("seciton type a is %s.\n",a);

  printf("allocating.\n");
  c = uci_alloc_context ();
  printf("allocating finish.\n");
  if (uci_lookup_ptr (c, &p, a, true) != UCI_OK)
    {
  	printf("allocating finish.\n");
      uci_perror (c, "XXX");
      return 1;
    }

  printf("%s\n", p.o->v.string);
  uci_free_context (c);
  free (a);
  return 0;
}
