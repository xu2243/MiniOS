#include "tty.h"
#include "proto.h"
#include "stdio.h"

int stdin, stdout, stderr;

int main(){
  int i;

  printf("in:%d, out:%d, err:%d\n", stdin, stdout, stderr);
  for(;;){
    printf("a\n");
    for(i=0; i<10000000; i++);
    yield();
  }
}