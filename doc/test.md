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

### PIPE详解 - 一个虚拟文件系统

进入实现环节，我们不仅需要熟悉系统调用的功能，还要深入其中了解其实现细节。

所谓管道，是指用于连接一个读进程和一个写进程，以实现它们之间通信的共享文件，又称 pipe 文件。

向管道（共享文件）提供输入的发送进程（即写进程），以字符流形式将大量的数据送入管道；而接收管道输出的接收进程（即读进程），可从管道中接收数据。由于发送进程和接收进程是利用管道进行通信的，故又称管道通信。

这种通信形式在类Unix操作系统中被统一进了文件系统，通信就是发送消息和接收消息的过程，本质和文件读写一样，因此管道通信的接口也就被统一成了对**管道类文件**的读写。既然是文件，那么就需要按 **文件描述符file_descreptor** 和 **文件节点inode** 的方式来管理。

但是它实际读写的形式和正常的文件是不一样的，正常的文件在硬盘中有指定的存储位置，但是pipe没有，它所有的数据都存储在内核的缓冲区中，它和普通文件的共同点就是有**文件系统形式的接口**，但所有数据一旦关机就会消失。

所以我们需要给inode添加新字段，标识pipe需要的信息，重写pipe的读写操作。

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

我们在pipe_inode里添加了一个字段，这是一个从linux的定义中简化过来的版本，以表示pipe类型的inode：

```c
struct pipe_inode_info	*i_pipe; 
/* in  */

/**
 *	struct pipe_inode_info - Simplified version of pipe, ported from Linux.
 *	@mutex: mutex protecting the whole thing
 *	@rd_wait: reader wait point in case of empty pipe
 *	@wr_wait: writer wait point in case of full pipe
 *	@head: The point of buffer production
 *	@tail: The point of buffer consumption
 *	@max_usage: how many buf i ve used
 *	@ring_size: total number of buffers (4KB)
 *	@readers: number of current readers of this pipe
 *	@writers: number of current writers of this pipe
 *	@files: number of struct file referring this pipe (protected by ->i_lock)
 *	@r_counter: reader counter
 *	@w_counter: writer counter
 *	@bufs: the circular array of pipe buffers
 *	@user: the user who created this pipe
 **/
struct pipe_inode_info {
	struct spinlock mutex;
	wait_queue_head_t rd_wait, wr_wait;
	unsigned int head;
	unsigned int tail;
	unsigned int max_usage;
	unsigned int ring_size;
	unsigned int readers;
	unsigned int writers;
	unsigned int r_counter;
	unsigned int w_counter;
	char *bufs;
	PROCESS *user;
};
```

### 管道通信 - 互斥与同步

为了协调双方的通信，管道通信机制必须提供以下3 方面的协调能力。

- 互斥。当一个进程正在对 pipe 进行读/写操作时，另一个进程必须等待。
- 同步。当写（输入）进程把一定数量（如4KB）数据写入 pipe 后，便去睡眠等待，直到读（输出）进程取走数据后，再把它唤醒。当读进程读到一空 pipe 时，也应睡眠等待，直至写进程将数据写入管道后，才将它唤醒。
- 对方是否存在。只有确定对方已存在时，才能进行通信。

#### 自旋锁

自旋锁是一个简单的互斥锁，保证了对一个管道inode资源访问的互斥。

每当一个进程需要读写管道时，通过`acquire(struct spinlock *lock)`和`void release(struct spinlock *lock)`管理对管道资源的访问。

```c
int pipe_read(int fd, void *buf, int count) {
	
    check_fd_and_preparation_code();
    
    acquire(&(pipe_info->mutex));
    
    do_pipe_read_code();

    release(&pipe_info->mutex);

}
```

#### 等待队列

`wait_queue_head_t` 是一个控制进程访问顺序的队列，每个希望对这个管道进行访问（读写）的进程，都会先进入管道的等待队列：`rd_wait` `wr_wait`。

这是一个链表实现的队列结构，先进入管道的进程会优先对管道进行访问。没有排在队头的进程则会被阻塞，直到前面的队头依次离开，排队排到自己，这个结构保证了每个进程都能有机会访问到管道。当然，在加入队列之前，这个进程首先要占用管道的锁，保证单独访问资源。

链表采用linux的list模板。

```c
typedef struct wait_queue_head{
    struct list_head wait_queue;
    PROCESS *proc;
} wait_queue_head_t;
```

这个等待队列提供了相应的接口，方便我们进行进程管理：

```c
struct wait_queue_head_t;

void init_queue_head(wait_queue_head_t *wq) {
    INIT_LIST_HEAD(&wq->wait_queue);
}

PROCESS *wait_queue_head(wait_queue_head_t *wq) {
    if (!list_empty(&wq->wait_queue)) {
        wait_queue_head_t *p = list_first_entry(&wq->wait_queue, wait_queue_head_t, wait_queue);
        return p->proc;
    }
    return NULL;
}

void wait_queue_push(wait_queue_head_t *wq, PROCESS *proc) {
    wait_queue_head_t *p = (wait_queue_head_t *)K_PHY2LIN(sys_kmalloc(sizeof(wait_queue_head_t)));
    p->proc = proc;
    list_add_tail(&p->wait_queue, &wq->wait_queue);
}

void wait_queue_pop(wait_queue_head_t *wq) {
    if (!list_empty(&wq->wait_queue)) {
        wait_queue_head_t *p = list_first_entry(&wq->wait_queue, wait_queue_head_t, wait_queue);
        list_del(&p->wait_queue);
        do_free((u32)p, sizeof(wait_queue_head_t)); // 在这里释放内存
    }
}

int wait_queue_is_empty(wait_queue_head_t *wq) {
    return list_empty(&wq->wait_queue);
}
```

#### 多进程读写调度

读写调度本质是一个状态机，通过对pipeinfo里的队列首尾指针，我们检查buffer的读写情况：

① 如果**写满**或者**读空**了，也就是 `tail == head`：那么我们需要：

- **释放锁**
- **阻塞当前进程**
- **唤醒另一个队列的队列头（如果有）**
- **等待下一次调度时，尝试占用锁并重复读写操作**

② 如果**完成读写**，也就是 **实际读写字节** 达到指定长度，那么：

- **退出读（写）队列**
- **唤醒另一个队列的队列头（如果有）**
- **释放锁，退出**

③ 如果**另一个队列为空**，且无法进行读写（写满或者读空），那么肯定不能一直阻塞当前进程，不然会永远等待下去，因为对方不存在：

- **释放锁，直接退出**，返回实际读写字节数（可能小于调用时的长度参数）

```c
int pipe_write(int fd, const void *buf, int count) {    
	
    /* other code */
    
	acquire(&pipe_info->mutex);

    // memcpy from pipe_info->bufs to user buffer
    unsigned int ret = 0;

    while (1) {
        if (ret == count) {
            wait_queue_pop(&pipe_info->wr_wait);
            wait_queue_head(&file->fd_node.fd_inode->i_pipe->rd_wait)->task.stat = READY;
            pipe_info->writers--;
            release(&pipe_info->mutex);
            return ret;
        } else if (pipe_info->max_usage < PAGE_SIZE) {
            *(char *)(pipe_info->bufs + (pipe_info->tail % PAGE_SIZE)) = *(char *)(buf + ret);
            pipe_info->tail++;
            pipe_info->max_usage++;
            pipe_info->tail %= PAGE_SIZE;
            ret++;
        } else {
            if (pipe_info->r_counter == 0) {
                pipe_info->readers--;
                release(&pipe_info->mutex);
                return ret;
            }
            wait_queue_head(&file->fd_node.fd_inode->i_pipe->rd_wait)->task.stat = READY;
            release(&pipe_info->mutex);
            p_proc_current->task.stat = SLEEPING;
		    sched();

            acquire(&pipe_info->mutex);
        }
    }
    
    /* other code */
}
```

### 系统调用 - 管道文件读写接口

首先我们有对应的调用函数，作为pipe对外的接口：

```c
int sys_pipe(void *uesp);
int sys_mkfifo(void *uesp);
int pipe_read(int fd, void *buf, int count);
int pipe_write(int fd, const void *buf, int count);
int pipe_close(int fd);
int pipe_info_release(struct pipe_inode_info *pipe_info);
```

在miniOS中，read的调用链是`read->do_vread->read_op`.

在这个调用链中，值得注意的是miniOS是根据vfs_table的dev对应的op操作来选择的。如果是orange系统，则选择orange系统的op；如果是fat32系统，则选择fat32系统的op。

```c
int do_vread(int fd, char *buf, int count) {
    int index = p_proc_current->task.filp[fd]->dev_index;
    return vfs_table[index].op->read(fd, buf, count);   //modified by mingxuan 2020-10-18
}
```

file_op定义如下，作为文件系统对外的接口：

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

并且新增了一个虚拟文件系统pipefifo：

```c
vfs_table[PIPEFIFO].fs_name = "pipefifo"; 
vfs_table[PIPEFIFO].op = &f_op_table[3];
```

但是这在miniOS中并不是一个很融洽的设计，因为我们的fifo最后应该出现在orange的文件目录里。

而orange也贴心地还给我们的fifo关键字留了一个宏定义（这是一个仿照`linux-0.12`的宏定义设计）！

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

我们的pipe和fifo现在也应该被归入orange的文件设备中，使用orange对应的`file_op` ，即 `real_open`  和 `real_unlink` 等接口。

于是我们修改了 `fs.c ` 中对应的函数文件读写函数，如果发现这个inode是pipe类型，那么转接到我们的`pipe_read` 和 `pipe_write` ，因为我们实际上并没有真的读写硬盘，而是进行pipe的缓冲区操作。

在这个过程中，我们修复（有选择的忽略）了若干miniOS的文件系统的bug，因为精力有限，没有进行大范围的重构。只是在有限的修改下保证我们的pipe能正常运行。

这些bug包括但不限于（写的时候只记得这么多）：

没有目录管理，明明有I_DIRECTORY的类型，但是没有目录相关的调用，所有的查询读写操作直接与硬盘交互，只有一个根目录；

调用关系混乱，直接越过hd接口调用hd_service更底层的函数hd_rdwt，实现了一个'完全串行的文件读写'；

没有时间中断调度，没有wait和exit等进程管理函数，进程状态定义甚至没有zombie；

在read和wiite的

会修改调用者参数的逆天函数；

写了但是不用的文件，写了但是不用的函数，写了但是不用的字段，你甚至不知道这个字段能取什么值；

fork中若干bug，exec中若干bug；

### 管道资源管理 - 存储与释放

#### 内核缓冲区申请

因为管道的数据不是存储在硬盘中，而是在内核的缓冲区中，那么我们区别于正常的文件，需要额外申请内核空间用于存储pipe额外的信息和缓冲区。

对于`pipe`来说，不仅要申请内核缓冲区，还要申请存储自身的空间。因为`pipe`不能通过文件路径索引到，没有文件名，也没有存储地址，只由`pipe`系统调用创建，供父子进程间使用，进程退出后直接销毁，所以需要额外的管理方式。`pipe`的`inode`没有编号，不在`inodemap`中，故需要自己申请空间。

```c
struct pipe_inode_info *alloc_pipe_info() {
    /* 申请pipe_inode的内存 */
    struct pipe_inode_info *pipe = (void*)K_PHY2LIN(sys_kmalloc(sizeof(struct pipe_inode_info)));
    memset(pipe, 0, sizeof(struct pipe_inode_info));
    pipe->ring_size = PIPE_BUF_PAGE_NUM * PAGE_SIZE;
    pipe->user = p_proc_current;
    
    /* 初始化pipe锁 */
    initlock(&pipe->mutex, "PIPE_LOCK");
	
    /* 初始化进程队列 */
    init_queue_head(&pipe->rd_wait);
    init_queue_head(&pipe->wr_wait);

    pipe->rd_wait.proc = NULL;
    pipe->wr_wait.proc = NULL;
	
    /* 申请内核缓冲区 */
    pipe->bufs = (void *)K_PHY2LIN((sys_kmalloc_4k()));
    return pipe;
}
```

#### 内核缓冲区释放

这是对管道通用的释放接口，无论是`pipe`还是`fifo`都需要释放这段内核缓冲区。

```c
int pipe_info_release(struct pipe_inode_info *pipe_info) {
    if (!wait_queue_is_empty(&pipe_info->rd_wait) || !wait_queue_is_empty(&pipe_info->wr_wait)) {
        kprintf("[release failed, its still being reading!]");
        return -1;
    }
    do_free((unsigned)pipe_info->bufs, pipe_info->ring_size);
    do_free((unsigned)pipe_info, sizeof(struct pipe_inode_info));
    return 0;
}
```

#### CLOSE行为和UNLINK行为

`pipe`的`close`行为会关闭对应的文件描述符，并操作inode当中记录的数据。当`pipe_inode`的所有文件描述符都关闭后，自动释放`pipe`的相关资源。

因为fd在内存中有一个固定大小的table，不需要我们释放，只需要设置指针即可。

```c
int pipe_close(int fd) {
    struct file_desc *pfile = p_proc_current->task.filp[fd];
    struct inode *pipe_inode = pfile->fd_node.fd_inode;
    struct pipe_inode_info *pipe_info = pfile->fd_node.fd_inode->i_pipe;
    pipe_inode->i_cnt--;
    if (pfile->fd_mode == O_RDONLY) pipe_info->r_counter--;
    else if (pfile->fd_mode == O_WRONLY) pipe_info->w_counter--;
    else {
        pipe_info->r_counter--;
        pipe_info->w_counter--;
    } 

    if (pipe_inode->i_cnt == 0) {
        pipe_info_release(pipe_inode->i_pipe);
        if (pipe_inode->i_mode == I_NAMED_PIPE) {
            pipe_inode->i_mode = 0;
            pipe_inode->i_size = 0;
            pipe_inode->i_start_sect = 0;
            pipe_inode->i_nr_sects = 0;
            sync_inode(pipe_inode);
        }
    }
    p_proc_current->task.filp[fd]->fd_node.fd_inode = 0;
    p_proc_current->task.filp[fd]->flag = 0;
    p_proc_current->task.filp[fd] = 0;
    return 0;
}
```

`fifo`的`close`行为和pipe完全一致（除了需要额外释放inode表资源），都会在进程结束后释放缓冲区资源，但是fifo在硬盘中有目录项数据，只能被另外的系统调用释放。

`fifo`的删除由`unlink()`函数触发，也就是shell中的`rm [path]`指令。

**注意**：unlink会删除正在被读写的fifo文件项，此时fifo文件被删除，无法再通过open打开。但是正在被读写的fifo资源仍然在内存当中，**可以被进程正常读写**，只有被close时资源才会被释放。**换句话说，open和close才负责fifo的资源分配，mkfifo和unlink只负责创建和删除对应的目录项和文件记录。**

```c
/* 在open调用的get_inode函数当中，如果打开的文件时fifo，且没有在内存中找到对应的inode，则创建对应的inode，并申请pipe缓存资源 */
if (q->i_mode == I_NAMED_PIPE) {
    q->i_pipe = alloc_pipe_info();
}
```

```c
/* 在unlink中，如果打开的文件时fifo，我们直接释放其对应的目录项，但不删除其inode */
if (pin->i_mode == I_NAMED_PIPE) {
    char fsbuf[SECTOR_SIZE];	//local array, to substitute global fsbuf. added by xw, 18/12/27

    release_imap_bit(pin, fsbuf);

    release_dir_entry(dir_inode, fsbuf, inode_nr);
    return 0;
}
```

#### 等待队列的资源释放

等待队列申请的资源也会在队列操作中对称释放。

```c
void wait_queue_pop(wait_queue_head_t *wq) {
    if (!list_empty(&wq->wait_queue)) {
        wait_queue_head_t *p = list_first_entry(&wq->wait_queue, wait_queue_head_t, wait_queue);
        list_del(&p->wait_queue);
        do_free((u32)p, sizeof(wait_queue_head_t)); // 在这里释放内存
    }
}
```

#### 没有调用close而exit退出的进程

`exit` 函数会处理所有未关闭的文件描述符。由于pipe和fifo调用的释放都是在close中统一管理的，所以不会出现未释放。

```c
/* exit.c */
for (i = 0; i < NR_FILES; i++)
{
    if (p_proc_current->task.filp[i]->flag == 1)
    {
        do_vclose(i);
    }
}
```

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

6. shell中的`mkfifo`与`rm`测试

   mkfifo一般以shell中命令的形式存在，我们在shell中也实现了相关命令。依次执行下面的命令：

   `mkfifo /testfifo`

   `rm orange/testfifo`

   分别可以创建一个有名管道`testfifo`以及将它移除；途中可以用另一个shell中的命令`ls`来查看文件系统中的文件，以查看建立和删除的结果。


### 综合测试

- pipe的`close` 和`dup2`在shell中每次运行管道命令都会执行，因此作为综合测试的部分。



## P.S.

如果在用户测试程序中不调用exit，那么多次执行用户程序后因为pcb未释放会导致fork失败。
