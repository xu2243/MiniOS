#include "type.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "proc.h"
#include "global.h"
#include "proto.h"
#include "stdio.h"

char eof[1024];
int stdin, stdout, stderr;

void exec_eof(char* eof){
	if (exec(eof) != 0)
    printf("exec %s failed\n", eof);
}

// 复制line里的第idx个文件名到filename中，idx从1开始计数
void get_filename(char* filename, char* line, int idx){
	int cnt =0, i;
	int start=0, finish=0;
	for(i=0; i<strlen(line)-1; i++){
		if(line[i] == '|'){
			cnt++;
			if(cnt==idx){
				finish = i-1;
				break;
			}else 
				start = i+1;
		}
	}
  // printf("before for\n");
  if(idx == cnt+1)
    finish = strlen(line)-1;

  // printf("finish:%d, start:%d\n", finish, start);
	for(; line[start] == ' '; start++);
	for(; line[finish] == ' '; finish--);
  // printf("after for\n");

  // printf("finish:%d, start:%d\n", finish, start);
	memcpy(filename, line+start, finish-start+1);
  //printf("finish:%d, start:%d\n", finish, start);
  // printf("here\n");
	filename[finish-start+1] = '\0';
  printf("file:*%s*\n", filename);
}

int main(int arg, char *argv[])
{
	stdin = open("dev_tty0", O_RDWR);
	stdout = open("dev_tty0", O_RDWR);
	stderr = open("dev_tty0", O_RDWR);

  printf("in:%d, out:%d, err:%d\n", stdin, stdout, stderr);

	char buf[1024];
	int pid;
	int times = 0;
	while (1)
	{
		printf("\nminiOS:/ $ ");
		if (gets(buf) && strlen(buf) != 0) {   
      if (strncmp("ls", buf, 2) == 0) {
        ls();
      } else {
        int pipenum = 0;
        for(int i=0; i<strlen(buf)-1; i++){
          if(buf[i] == '|')
            pipenum++;
        }
        //printf("pipen: %d\n", pipenum);
        
        if(pipenum == 0)
          exec_eof(buf);
        else{
          if(pipenum > 1){
            //暂不支持1个以上的管道
            printf("pipe more than 1\n");
            continue ;
          }

          int pipefd[2];

          if(pipe(pipefd) == -1){
            printf("pipe failed\n");
            continue ;
          }

          int cpid = fork();

          if (cpid > 0) {
            printf("father, son:%d\n", cpid);
            close(pipefd[0]);
            if (dup2(pipefd[1], STD_OUT) == -1)
              printf("dup2 failed\n");
            get_filename(eof, buf, 1);
            exec_eof(eof);
            // printf("error1\n");
          }
          else if (cpid == 0) {
            printf("son");
            close(pipefd[1]);
            if (dup2(pipefd[0], STD_IN) == -1)
              printf("dup2 failed\n");
            get_filename(eof, buf, 2);
            exec_eof(eof);
            // printf("error2\n");
            return 0;
          }
        }
      }
    }
  }
}