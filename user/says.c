#include "stdio.h"

int main(int arg, char *argv[])
{
    char line[1024];
    for (;;)
    {
        gets(line);
        printf("pipe says:%s\n", line);
    }
}