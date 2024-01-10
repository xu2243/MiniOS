#include "tty.h"
#include "proto.h"
#include "stdio.h"

int main(){
  int i;
  char line[1024];
  for(;;){
    printf("%s", gets(line));
    yield();
  }
}