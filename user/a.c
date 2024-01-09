#include "tty.h"
#include "proto.h"
#include "stdio.h"

int main(){
  int i;
  for(;;){
    printf("a");
    for(i=0; i<10000000; i++);
    yield();
  }
}