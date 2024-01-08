#include "stdio.h"

int main(){
  char line[1024];
  for(;;){
    gets(line);
    printf("pipe says:%s\n", line);
  }
}