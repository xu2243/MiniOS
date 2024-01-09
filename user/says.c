#include "stdio.h"
#include "tty.h"
#include "proto.h"

int main()
{
    char line[1024];
    for (;;)
    {
        gets(line);
        printf("pipe says:[%s]\n", line);
        yield();
    }
}