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

typedef void (*usr_prog)(void);

void exec_eof(char *eof)
{
    if (exec(eof) != 0)
        printf("exec %s failed\n", eof);
}

void hello(){
	printf("Hello world!\n");
	yield();
}

void says(){
	char line[1024];
	for(;;){
		gets(line);
		printf("pipe says:%s\n", line);
	}
}

void repeat_r(){
	char line[1024];
    for (;;)
    {
      printf("%c", getchar());
    }
}

void repeat_w(){
	int i;
    //确保读进程加入读等待队列
    yield();
    for (;;)
    {
      printf("repeat");
    }
}

void a(){
	for(;;){
		printf("pipe chain!\n");
		yield();
	}
}

void b(){
	char line[1024];
	for(;;){
		printf("%s", gets(line));
		yield();
	}
}

void c(){
	char line[1024];
	for(;;){
		printf("%s", gets(line));
		yield();
	}
}

void hello(){
	printf("Hello world!\n");
	yield();
}

void says(){
	char line[1024];
	for(;;){
		gets(line);
		printf("pipe says:%s\n", line);
	}
}

void repeat_r(){
	char line[1024];
    for (;;)
    {
      printf("%c", getchar());
    }
}

void repeat_w(){
	int i;
    //确保读进程加入读等待队列
    yield();
    for (;;)
    {
      printf("repeat");
    }
}

void a(){
	for(;;){
		printf("pipe chain!\n");
		yield();
	}
}

void b(){
	char line[1024];
	for(;;){
		printf("%s", gets(line));
		yield();
	}
}

void c(){
	char line[1024];
	for(;;){
		printf("%s", gets(line));
		yield();
	}
}

// 复制line里的第idx个文件名到filename中
void get_filename(char *filename, char *line, int idx)
{
    int cnt = 0, i;
    int start = 0, finish = 0;
    for (i = 0; i < strlen(line) - 1; i++)
    {
        if (line[i] == '|')
        {
            cnt++;
            if (cnt == idx)
            {
                finish = i - 1;
                break;
            }
            else
                start = i + 1;
        }
    }
    if (idx == cnt + 1)
        finish = strlen(line) - 1;

    for (; line[start] == ' '; start++)
        ;
    for (; line[finish] == ' '; finish--)
        ;

    // printf("finish:%d, start:%d\n", finish, start);
    memcpy(filename, line + start, finish - start + 1);
    // printf("finish:%d, start:%d\n", finish, start);
    //  printf("here\n");
    filename[finish - start + 1] = '\0';
    // printf("file:*%s*\n", filename);
}

void pp(char *buf)
{
    int pipenum = 0;
    for (int i = 0; i < strlen(buf) - 1; i++)
    {
        if (buf[i] == '|')
            pipenum++;
    }
    // printf("[pipen: %d]", pipenum);

    if (pipenum == 0){
        exec_eof(buf);
    }
	else{
		printf("invalid test!\n");
	}

    // else if(pipenum == 1){
    //     int pipefd[2];

    //     if (pipe(pipefd) == -1)
    //     {
    //         printf("pipe failed ");
    //         return;
    //     }
    //     else
    //     {
    //         // printf("pipe good!! ");
    //     }

    //     int cpid = fork();

    //     printf("[%d, %d]", pipefd[0], pipefd[1]);

	// 	if (cpid > 0) {
	// 		printf("father, son:%d\n", cpid);
	// 		close(pipefd[0]);
	// 		if (dup2(pipefd[1], STD_OUT) == -1)
	// 			printf("dup2 failed\n");

	// 	}
	// 	else if (cpid == 0) {
	// 		printf("son\n");
	// 		close(pipefd[1]);
	// 		if (dup2(pipefd[0], STD_IN) == -1)
	// 			printf("dup2 failed\n");


    //     }
    // }else{ //多管道，递归解决
	// 	int pipefd[2];
    //     if (pipe(pipefd) == -1){
    //         printf("pipe failed ");
    //         return ;
	// 	}
	// 	int cpid = fork();
	// 	if (cpid > 0) {
    //         printf("father, son:%d\n", cpid);
    //         close(pipefd[0]);
    //         if (dup2(pipefd[1], STD_OUT) == -1)
    //           	printf("dup2 failed\n");
    //         get_filename(eof, buf, 1);
    //         exec_eof(eof);
	// 	}else {
    //         close(pipefd[1]);
    //         if (dup2(pipefd[0], STD_IN) == -1)
    //           	printf("dup2 failed\n");
	// 		char *newline = buf;
	// 		//去掉第一个文件名
	// 		for(; *newline != '|'; newline++);
	// 		newline++;
	// 		pp(newline);
	// 	}
	// }
}

void pipe_2(usr_prog p1, usr_prog p2){
	int pipefd[2];
	if (pipe(pipefd) == -1){
		printf("pipe failed ");
		return ;
	}
    int cpid = fork();

	if (cpid > 0) {
		printf("father, son:%d\n", cpid);
		close(pipefd[0]);
		if (dup2(pipefd[1], STD_OUT) == -1)
			printf("dup2 failed\n");
		p1();
	}
	else if (cpid == 0) {
		printf("son\n");
		close(pipefd[1]);
		if (dup2(pipefd[0], STD_IN) == -1)
			printf("dup2 failed\n");
		p2();
	}
}

void pipe_3(usr_prog p1, usr_prog p2, usr_prog p3){
	int pipefd1[2];
	if (pipe(pipefd1) == -1){
		printf("pipe failed ");
		return ;
	}
    int cpid1 = fork();

	if (cpid1 > 0) {
		printf("father, son:%d\n", cpid1);
		close(pipefd1[0]);
		if (dup2(pipefd1[1], STD_OUT) == -1)
			printf("dup2 failed\n");
		p1();
	}
	else if (cpid1 == 0) {
		printf("son\n");
		close(pipefd1[1]);
		if (dup2(pipefd1[0], STD_IN) == -1)
			printf("dup2 failed\n");
		
		int pipefd2[2];
		if (pipe(pipefd2) == -1){
			printf("pipe failed ");
			return ;
		}
		int cpid2 = fork();
		if (cpid2 > 0) {
			printf("father, son:%d\n", cpid2);
			close(pipefd2[0]);
			if (dup2(pipefd2[1], STD_OUT) == -1)
				printf("dup2 failed\n");
			p2();
		}
		else if (cpid2 == 0) {
			printf("son\n");
			close(pipefd2[1]);
			if (dup2(pipefd2[0], STD_IN) == -1)
				printf("dup2 failed\n");
			p3();
		}
	}
}

void run(char* line, int tn){
	int cpid = fork();
	if(cpid > 0){
		// printf("wait4child:%d\n", cpid);
		int wpid = waity(NULL);
		printf("child:%d exited!\n", wpid);
		//神奇的地方在于，如果不写这两行那么stdin不再连接到tty0
		close(0);
		open("dev_tty0", O_RDWR);
	}else{
		switch(tn){
		case 0:
			pp(line);
			break;
		case 1:
			pipe_2(hello, says);
			break;
		case 2:
			pipe_2(repeat_w, repeat_r);
			break;
		case 3:
			pipe_3(a, b, c);
			break;
		}
	}
}


int main(int arg, char *argv[])
{
    stdin = open("dev_tty0", O_RDWR);
    stdout = open("dev_tty0", O_RDWR);
    stderr = open("dev_tty0", O_RDWR);

    // printf("in:%d, out:%d, err:%d\n", stdin, stdout, stderr);

    char buf[1024];
    int pid;
    int times = 0;
    while (1)
    {
        printf("\nminiOS:/ $ ");
        if (gets(buf) && strlen(buf) != 0)
        {
            if (strncmp("ls", buf, 2) == 0)
              ls();
            else if (strncmp("t1", buf, 2) == 0) 
              run("hello | says", 1);			//测试案例引号里的东西没有实际作用，仅作注释
            else if (strncmp("t2", buf, 2) == 0) 
              run("repeat_w | repeat_r", 2);
            else if (strncmp("t3", buf, 2) == 0) 
              run("a | b | c", 3);
            else
              run(buf, 0);
        }
    }
}