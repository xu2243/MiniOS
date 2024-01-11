# pipe 和 dup 两类系统调用在miniOS中的实现与测试

## 要求

 实现管道系统： 

- 实现 pipe 和 dup 两类系统调用
  - pipe 创建管道文件描述符， close 结合 dup 系统调用实现文件描述符的移动
- 基于管道系统修改shell程序，使得通过管道实现多个进程间数据的传输，以及重定向功能
- 测试参考 
  - 父子进程之间的管道数据传输，写满和读空的情况 
  - 特殊情况处理，例如进程退出而没有关闭管道，多组进程同时使用管道系统 
  - 其他的功能，例如基于管道实现进程间同步等

## pipe syscall

A pipe is a mechanism in Unix-like operating systems, including Linux, that allows the flow of data between two processes. It enables one process to communicate with another without having to share memory or other resources. A pipe can be created using the pipe system call (pipe) and consists of a read end and a write end.

## Syntax:

```c
int pipe(int pipefd[2]);
```

**Parameters:**

- `pipefd[0]`: File descriptor of the pipe's read end
- `pipefd[1]`: File descriptor of the pipe's write end

## Return Value:

- If successful, it returns 0 and the file descriptors are valid for reading from the read end and writing to the write end.
- Otherwise, it returns an error code (typically -1), indicating an error occurred.

## Usage:

To use a pipe in your program, you'll need to perform the following steps:

1. Create the pipe using the `pipe` system call. This creates two file descriptors, one for reading from the pipe and another for writing to the pipe. 

   ```c
   int pipefd[2];
   pipe(pipefd);
   if(pipe(pipefd) != 0) {
       perror("Failed to create a new pipe.");
       return -1;
   }
   ```

2. Create child process (or use existing one). The child process will read from the write end and write to the read end, while the parent process will read from the read end and write to the write end.

3. Close the file descriptors for both ends of the pipe that you're not using, in order to prevent leaks.

   ```c
   close(pipefd[0]); // Close the read end descriptor
   close(pipefd[1]); // Close the write end descriptor
   ```
   
4. For reading from the write end or writing to the read end, open the file descriptors for that end of the pipe.

## Examples:

Here are two simple examples to demonstrate how to use a pipe in miniOS:

### Creating a Pipe

```c
int pipefd[2];
pipe(pipefd);
if(pipe(pipefd) != 0) {
    perror("Failed to create a new pipe.");
    return -1;
}
close(pipefd[0]); // Close the read end descriptor
close(pipefd[1]); // Close the write end descriptor
```

### Using pipe to communicate between processes

```c
char *buf[MAX_BUF];
int pipefd[2];
if (pipe(pipefd) == -1)
{
    printf("pipe failed ");
    return;
}
int cpid = fork();

if (cpid > 0)
{
    close(pipefd[0]);
    if (dup2(pipefd[1], STD_OUT) == -1)
        printf("dup2 failed\n");
    printf("hello, my son!");
}
else if (cpid == 0)
{
    close(pipefd[1]);
    if (dup2(pipefd[0], STD_IN) == -1)
        printf("dup2 failed\n");
    gets(buf);
    printf("from father:%s", buf);
}
```

what suppose to print in console are as follows:

```
from father:hello, my son!
```

## 实现细节

### 系统分析

首先我们有对应的调用函数，作为pipe对外的接口：

```c
int sys_pipe(void *uesp);
int pipe_read(int fd, void *buf, int count);
int pipe_write(int fd, const void *buf, int count);
int pipe_release(int fd);
```

在miniOS中，read的调用链是`read->do_vread->read_op`.

在这个调用链中，值得注意的是miniOS是根据vfs_table的dev对应的op操作来选择的。如果是orange系统，则选择orange系统的op；如果是fat32系统，则选择fat32系统的op。

```c
int do_vread(int fd, char *buf, int count) {
    int index = p_proc_current->task.filp[fd]->dev_index;
    return vfs_table[index].op->read(fd, buf, count);   //modified by mingxuan 2020-10-18
}
```

file_op定义如下：

```c
struct file_op{
    int (*create)   (const char*);
	int (*open)    (const char* ,int);
	int (*close)   (int);
	int (*read)    (int,void * ,int);
	int (*write)   (int ,const void* ,int);
	int (*lseek)   (int ,int ,int);
	int (*unlink)  (const char*);
    int (*delete) (const char*);
	int (*opendir) (const char *);
	int (*createdir) (const char *);
	int (*deletedir) (const char *);
};
```

因此，在我们最初的设计中，我直接定义了一个新的vfs，以便文件的读写操作能直接对应到我们的pipe读写操作：

```c
// table[3] for pipefifo vfs
f_op_table[3].create = fifo_create; 
f_op_table[3].close = pipe_release;
f_op_table[3].write = pipe_write;
f_op_table[3].read = pipe_read;
f_op_table[3].unlink = pipe_unlink;

vfs_table[PIPEFIFO].fs_name = "pipefifo"; 
vfs_table[PIPEFIFO].op = &f_op_table[3];
```

但是这在miniOS中并不是一个很融洽的设计，因为我们的fifo最后应该出现在orange的文件目录里，而orange也甚至还给我们的fifo关键字留了一个宏定义（这是一个仿照`linux-0.12`的宏定义设计）！

```c
/* fs_const.h */
/* INODE::i_mode (octal, lower 32 bits reserved) */
#define I_TYPE_MASK     0170000			// 文件类型掩码，用于提取文件类型部分的位
#define I_REGULAR       0100000			// 常规文件的类型标志
#define I_BLOCK_SPECIAL 0060000			// 块设备文件的类型标志
#define I_DIRECTORY     0040000			// 目录的类型标志
#define I_CHAR_SPECIAL  0020000			// 字符设备文件的类型标志
#define I_NAMED_PIPE	0010000			// 命名管道（FIFO）的类型标志
```

也就是说，orange的文件系统用inode的i_mode字段把文件分为了：目录文件，常规文件，块设备文件，字符设备文件，fifo文件等。









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

