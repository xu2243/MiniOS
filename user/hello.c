#include "tty.h"
#include "proto.h"
#include "stdio.h"

int main()
{
    int i;
    for (;;)
    {
        printf("Hello world!");
        yield();
    }
}