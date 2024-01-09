#include "tty.h"
#include "proto.h"
#include "stdio.h"

int stdin, stdout, stderr;

int main(){
  int i;
  close(0);
  close(1);
  close(2);
  stdin = open("dev_tty0", O_RDWR);
	stdout = open("dev_tty0", O_RDWR);
	stderr = open("dev_tty0", O_RDWR);

  printf("in:%d, out:%d, err:%d\n", stdin, stdout, stderr);
  for(;;){
    printf("a\n");
    for(i=0; i<10000000; i++);
    yield();
  }
}