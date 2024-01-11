## 要求

 实现管道系统： 

- 实现 pipe 和 dup 两类系统调用
  - pipe 创建管道文件描述符， close 结合 dup 系统调用实现文件描述符的移动
- 基于管道系统修改shell程序，使得通过管道实现多个进程间数据的传输，以及重定向功能
- 测试参考 
  - 父子进程之间的管道数据传输，写满和读空的情况 
  - 特殊情况处理，例如进程退出而没有关闭管道，多组进程同时使用管道系统 
  - 其他的功能，例如基于管道实现进程间同步等

## 测试案例

### 单元测试

1. 普通管道数据传输：`pipe`

   ```c
   void pipe_test(){
   	int pipefd[2];
   	if (pipe(pipefd) == -1){
   		printf("pipe failed ");
   		return ;
   	}
       int cpid = fork();
   
   	if (cpid > 0) {
   		close(pipefd[0]);
   		write(pipefd[1], "hi son", 7);
   		exit(0);
   	}
   	else if (cpid == 0) {
   		char line[1024];
   		close(pipefd[1]);
   		printf("father says:");
           read(pipefd[0], line, 1024);
           printf("%s", line);
           exit(0);
   		
   	}
   }
   ```

   理论输出：`fatehr says:hi son`

2. 普通管道数据传输+重定向：`orange/hello.bin | orange/says.bin`

   ```c
   //hello.c 
   int main()
   {
       int i;
       for (;;)
       {
           printf("Hello world!\n");
           yield();
       }
   }
   //says.c
   int main()
   {
       char line[1024];
       line[0] = 0;
       for (;;)
       {
           gets(line);
           printf("pipe says:[%s]\n", line);
           yield();
       }
   }
   ```

   这个命令，按理说会先执行`orange/hello.bin`，它会往stdout输出"Hello,world!"，建立了管道的话在此时yield()后，`orange/says.bin`理应能读取并输出"pipe says: Hello world!"到终端。

3. 写满读空测试：`orange/repeat_w.bin | orange/repeat_r.bin`

   ```c
   //repeat_w.c
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
   //repeat_r.c
   int main()
   {
       char line[1024];
       for (;;)
       {
         printf("%c", getchar());
       }
   }
   ```

   正确实现的pipe应该是这个流程：进入repeat_w后，yield进入repeat_r，此时尝试读空pipe，导致repeat_r阻塞并sched。repeat_w于是开始写pipe，这又会唤醒repeat_r。写满后，又会阻塞并调用sched到repeat_r开始读pipe，循环往复。

4. 多组管道：`orange/a.bin | orange/b.bin | orange/c.bin `

   ```c
   void a()
   {
       for (;;)
       {
           printf("pipe chain!\n");
           yield();
       }
   }
   
   void b()
   {
       char line[1024];
       for (;;)
       {
           printf("%s", gets(line));
           yield();
       }
   }
   
   void c()
   {
       char line[1024];
       for (;;)
       {
           printf("%s", gets(line));
           yield();
       }
   }
   ```

   这也是一开始设立的目标。该命令将一个字符串从`a.bin`传递到`c.bin`再输出。

5. 命名管道测试：`fifo`

   ```c
   void fifo_test()
   {
       if (mkfifo("/fifo") != 0)
           printf("mkfifo failed!\n");
       int cpid = fork();
       if (cpid > 0)
       {
           int fd = open("orange/fifo", O_RDWR);
           for (;;)
           {
               printf("fa:hi son\n");
               write(fd, "hi son\n", 6);
               sleep(100);
               yield();
           }
       }
       else
       {
           int fd = open("orange/fifo", O_RDWR);
           char line[1024];
           memset(line, 0, 1024);
           for (;;)
           {
               read(fd, line, 6);
               printf("hear from fa:");
               if (line[0] != 'h') {
                   printf("bad!");
               }
               printf("%s\n", line);
               sleep(100);
               yield();
           }
       }
   }
   ```

   


### 综合测试

- pipe的`close` 和`dup2`在shell中每次运行管道命令都会执行，因此作为综合测试的部分。



## P.S.

如果在用户测试程序中不调用exit，那么多次执行用户程序后因为pcb未释放会导致fork失败。

