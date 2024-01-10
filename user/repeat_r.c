#include "stdio.h"
#include "tty.h"
#include "proto.h"

int main()
{
    char line[1024];
    for (;;)
    {
      printf("%c", getchar());
    }
}