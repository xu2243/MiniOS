#include "tty.h"
#include "proto.h"
#include "stdio.h"

int main()
{
    int i;
    //确保读进程加入读等待队列
    yield();
    for (;;)
    {
      printf("repeat");
    }
}