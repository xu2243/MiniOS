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

进入实现环节，我们不仅需要熟悉系统调用的功能，还要深入其中了解其实现细节。

所谓管道，是指用于连接一个读进程和一个写进程，以实现它们之间通信的共享文件，又称 pipe 文件。

向管道（共享文件）提供输入的发送进程（即写进程），以字符流形式将大量的数据送入管道；而接收管道输出的接收进程（即读进程），可从管道中接收数据。由于发送进程和接收进程是利用管道进行通信的，故又称管道通信。

这种通信形式在类Unix操作系统中被统一进了文件系统，通信就是发送消息和接收消息的过程，本质和文件读写一样，因此管道通信也就被统一成了对**管道类文件**的读写。既然是文件，那么就需要按**文件描述符**和**inode**的方式来管理。

```c
// in fs_misc.h
/**
 * @struct inode
 * @brief  i-node
 *
 * The \c start_sect and\c nr_sects locate the file in the device,
 * and the size show how many bytes is used.
 * If <tt> size < (nr_sects * SECTOR_SIZE) </tt>, the rest bytes
 * are wasted and reserved for later writing.
 *
 * \b NOTE: Remember to change INODE_SIZE if the members are changed
 */
struct inode {
	u32	i_mode;		/**< Accsess mode */
	u32	i_size;		/**< File size */
	u32	i_start_sect;	/**< The first sector of the data */
	u32	i_nr_sects;	/**< How many sectors the file occupies */
	u8	_unused[16];	/**< Stuff for alignment */

	/* the following items are only present in memory */
	int	i_dev;
	int	i_cnt;		/**< How many procs share this inode  */
	int	i_num;		/**< inode nr.  */

    struct pipe_inode_info	*i_pipe; // added by xuxinping 2023-12-31
};
```

我们在pipe_inode里添加了一个字段，这是一个从linux的定义中简化过来的版本：

```
struct pipe_inode_info	*i_pipe; 

/**
 *	struct pipe_inode_info - Simplified version of pipe, ported from Linux.
 *	@mutex: mutex protecting the whole thing
 *	@rd_wait: reader wait point in case of empty pipe
 *	@wr_wait: writer wait point in case of full pipe
 *	@head: The point of buffer production
 *	@tail: The point of buffer consumption
 *	@note_loss: The next read() should insert a data-lost message
 *	@max_usage: how many buf i ve used
 *	@ring_size: total number of buffers (should be a power of 2)
 *	@nr_accounted: The amount this pipe accounts for in user->pipe_bufs
 *	@tmp_page: cached released page
 *	@readers: number of current readers of this pipe
 *	@writers: number of current writers of this pipe
 *	@files: number of struct file referring this pipe (protected by ->i_lock)
 *	@r_counter: reader counter
 *	@w_counter: writer counter
 *	@poll_usage: is this pipe used for epoll, which has crazy wakeups?
 *	@fasync_readers: reader side fasync
 *	@fasync_writers: writer side fasync
 *	@bufs: the circular array of pipe buffers
 *	@user: the user who created this pipe
 *	@watch_queue: If this pipe is a watch_queue, this is the stuff for that
 **/
struct pipe_inode_info {
	struct spinlock mutex;
	wait_queue_head_t rd_wait, wr_wait;
	unsigned int head;
	unsigned int tail;
	unsigned int max_usage;
	unsigned int ring_size;
#ifdef CONFIG_WATCH_QUEUE
	bool note_loss;
#endif
	// unsigned int nr_accounted;
	unsigned int readers;
	unsigned int writers;
	// use inode->cnt instead. files is repeated.
    // unsigned int files; 
	unsigned int r_counter;
	unsigned int w_counter;
	// bool poll_usage;
	// struct page *tmp_page;
	// struct fasync_struct *fasync_readers;
	// struct fasync_struct *fasync_writers;
	char *bufs;
	PROCESS *user;
#ifdef CONFIG_WATCH_QUEUE
	struct watch_queue *watch_queue;
#endif
};
```





为了协调双方的通信，管道通信机制必须提供以下3 方面的协调能力。

- 互斥。当一个进程正在对 pipe 进行读/写操作时，另一个进程必须等待。
- 同步。当写（输入）进程把一定数量（如4KB）数据写入 pipe 后，便去睡眠等待，直到读（输出）进程取走数据后，再把它唤醒。当读进程读到一空 pipe 时，也应睡眠等待，直至写进程将数据写入管道后，才将它唤醒。
- 对方是否存在。只有确定对方已存在时，才能进行通信。

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
```

新增了一个虚拟文件系统pipefifo：

```c
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

虽然我们的pipe







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
