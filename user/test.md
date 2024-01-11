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

1. **普通管道数据传输**：`pipe`

   ```c
   void pipe_test(){
   	int pipefd[2];
   	if (pipe(pipefd) == -1){
   		printf("pipe failed ");
   		return ;
   	}
   ```
    pipe的系统调用接口：
    `int pipe(int pipefd[2])`
    在pipe.c中实现

    ```c
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

   为方便测试，已在系统调用中添加简易的`exit()`和`wait()`。

   **理论输出**：`fatehr says:hi son`
   **流程**：父进程向管道写端写入`"hi son"`并退出 -> 子进程从管道读端读入并打印出内容

2. **普通管道数据传输+重定向**：`t1`

   单管道测试程序：
   ```c
    void pipe_2(usr_prog p1, usr_prog p2)
    {
        int pipefd[2];
        if (pipe(pipefd) == -1)
        {
            printf("pipe failed ");
            return;
        }
        int cpid = fork();
   
        if (cpid > 0)
        {
            printf("father, son:%d\n", cpid);
            close(pipefd[0]);
            if (dup2(pipefd[1], STD_OUT) == -1)
                printf("dup2 failed\n");
            p1();
        }
        else if (cpid == 0)
        {
            printf("son\n");
            close(pipefd[1]);
            if (dup2(pipefd[0], STD_IN) == -1)
                printf("dup2 failed\n");
            p2();
        }
    }
   ```

   该函数在建立管道后，fork后分别重定位父子进程的stdout和stdin到管道的写端和读端，然后分别执行两个用户程序。

   在该测试中，两个程序如下：
   ```c
    void hello()
    {
        int i;
        for (i = 0; i < 10; i++)
        {
            printf("Hello world!\n");
            yield();
        }
        exit(0);
    }
   
    void says()
    {
        char line[1024];
        int i;
        for (i = 0; i < 10; i++)
        {
            gets(line);
            printf("pipe says:%s\n", line);
            yield();
        }
        exit(0);
    }
   ```

   **理论输出**：十行`"pipe says: Hello world!"`
   **流程**：这个命令会先执行`hello`，它会往stdout输出`"Hello,world!"`（而这里stdout经过dup2指向管道的写端），建立了管道的话在此时`yield()`后，`says`理应能读取并输出`"pipe says: Hello world!"`到终端。因此这里的正确结果会输出

3. **写满读空测试**：`t2`

   ```c
   void repeat_r()
   {
       char line[1024];
       for (;;)
       {
           printf("%c", getchar());
       }
   }
   
   void repeat_w()
   {
       int i;
       // 确保读进程加入读等待队列
       yield();
       for (;;)
       {
           printf("repeat");
       }
   }
   ```

   **理论输出**：不断输出repeat

   **流程**：正确实现的pipe应该是这个流程：进入repeat_w后，yield进入repeat_r，此时尝试读空pipe，导致repeat_r阻塞并sched。repeat_w于是开始写pipe，这又会唤醒repeat_r。写满后，又会阻塞并调用sched到repeat_r开始读pipe，循环往复。

   > 这个测试之所以能测试写满读空，一方面是因为minios的时钟中断不会调度线程，这样一来，如果写满或读空时，如果不主动调度就会造成线程的永久阻塞。因此，这里因为能够在shell里观察到输出，就说明成功实现了写满读空的调度。

   ```c
   //minios的时钟中断处理
   void clock_handler(int irq)
   {
   	ticks++;
   	
   	/* There is two stages - in kernel intializing or in process running.
   	 * Some operation shouldn't be valid in kernel intializing stage.
   	 * added by xw, 18/6/1
   	 */
   	if(kernel_initial == 1){
   		return;
   	}
   	irq = 0;
   	p_proc_current->task.ticks--;
   	sys_wakeup(&ticks);
   }
   void sys_wakeup(void *channel)
   {
   	PROCESS *p;
   	
   	for(p = proc_table; p < proc_table + NR_PCBS; p++){
   		if(p->task.stat == SLEEPING && p->task.channel == channel){
   			p->task.stat = READY;
   		}
   	}
   }
   ```

4. **多组管道测试**：`t3`

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

   **理论输出**：不断输出`"pipe chain!"`

5. **命名管道测试**：`fifo`

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

   命名管道由系统调用`mkfifo(char* path)`建立。
   
   **理论输出**：不断输出`hear from fa:hi son`
   
   **流程**：`mkfifo`建立有名管道后，`fork`后父子进程分别打开该管道，并且利用该管道和`read & write`进行进程间通信：父进程向管道写端写入`hi son`，在`yield`后进入子进程，子进程从管道读端读入管道中的`hi son`并输出，然后再`yield`，进入父进程，开始循环。


### 综合测试

- pipe的`close` 和`dup2`在shell中每次运行管道命令都会执行，因此作为综合测试的部分。



## P.S.

如果在用户测试程序中不调用exit，那么多次执行用户程序后因为pcb未释放会导致fork失败。

