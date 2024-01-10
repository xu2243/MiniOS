#include "stdio.h"
#include "tty.h"
#include "proto.h"

int main()
{
    char line[1024];
    line[0] = 0;
    for (;;)
    {
        // gets(line);
        printf("pipe says:[]");
        yield();
    }
}