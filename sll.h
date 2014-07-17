#ifndef SLL_H
#define SLL_H

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <malloc.h>

#include <sys/time.h>
#include <time.h>

#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>

#define SLL_SUCCESS     0
#define SLL_ERROR       -1

#ifndef TRUE
#define TRUE    1
#endif

#ifndef FALSE
#define FALSE   0
#endif

typedef int     Bool;
typedef void    (*Vfunc)(void **);
typedef int     (*Ifunc)(void *,void *);


/*
** the linked list structure
*/

typedef struct _Sll
{
    void
        *data;          /* void pointer for user data */

    struct _Sll
        *next;          /* pointer to next node */
} Sll;

/*
** function prototypes
*/
Sll         *allocateNode       (void *data);
void        appendNodeSorted    (Sll **head,Sll **new,Ifunc compFunc);
void        appendNode          (Sll **list,Sll **new);
void        delNode             (Sll **list,Sll *node);
/*
void        destroyNode         (Sll **list,Sll *node,
                                 void (*freeFunc)(void **));
*/
void        destroyNode         (Sll **list,Sll *node,Vfunc freeFunc);
/*
void        destroyNodes        (Sll **head,
                                 void (*freeFunc)(void **));
*/
void        destroyNodes        (Sll **head,Vfunc freeFunc);
Bool        emptyList           (Sll *list);
void        freeNode            (Sll **list);
Sll         *getNthNode         (Sll *list,int n);
void        initList            (Sll **list);
void        insertNode          (Sll **list,Sll **new);
int         numNodes            (Sll **head);

#endif  /* SLL_H */
