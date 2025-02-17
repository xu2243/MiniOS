# pipe 和 dup 两类系统调用在miniOS中的实现

## 要求

 实现管道系统： 

- 实现 pipe 和 dup 两类系统调用
  - pipe 创建管道文件描述符， close 结合 dup 系统调用实现文件描述符的移动
- 基于管道系统修改shell程序，使得通过管道实现多个进程间数据的传输，以及重定向功能
- 测试参考 
  - 父子进程之间的管道数据传输，写满和读空的情况 
  - 特殊情况处理，例如进程退出而没有关闭管道，多组进程同时使用管道系统 
  - 其他的功能，例如基于管道实现进程间同步等

## pipe 系统调用

管道是类 Unix 操作系统（包括 Linux）中的一种机制，允许数据在两个进程之间流动。 它使一个进程能够与另一个进程通信，而无需共享内存或其他资源。 管道可以使用管道系统调用（pipe）创建，由读端和写端组成。

### Syntax:

```c
int pipe(int pipefd[2]);
```

**Parameters:**

- `pipefd[0]`: 管道读端的文件描述符
- `pipefd[1]`: 管道写端的文件描述符

### Return Value:

- 如果成功，则返回0，并且文件描述符对于从读端读取和向写端写入有效。
- 否则，它返回一个错误代码（通常为-1），表明发生了错误。

## mkfifo 系统调用

mkfifo 命令主要是用于创建有名管道（Named Pipe），依参数pathname建立特殊的FIFO文件，参数mode为该文件的权限。

这个系统调用没有在题设中提到，但是我们认为为了管道系统的完整性，需要完成这部分工作。

### Syntax:

```c
int mkfifo(const char *pathname, mode_t mode);
```

**Parameters:**

- `pathname`: fifo文件路径
- `mode_t mode`: 指定文件权限位

但是在miniOS中，没有chmod系统调用，没有区分用户组。故在本实现中**没有mode参数**。

```c
int mkfifo(const char *pathname);
```

### Return Value:

- 如果成功，则返回0。
- 否则，它返回一个错误代码（通常为-1），表明发生了错误。

## dup2 系统调用

dup2与dup区别是dup2可以用参数newfd指定新文件描述符的数值。若参数newfd已经被程序使用，则系统就会将newfd所指的文件关闭，若newfd等于oldfd，则返回newfd,而不关闭newfd所指的文件。dup2所复制的文件描述符与原来的文件描述符共享各种文件状态。共享所有的锁定，读写位置和各项权限或flags等.

### Syntax:

```c
int dup2(int oldfd, int newfd);
```

**Parameters:**

- `oldfd`: 旧的文件描述符
- `newfd`: 新的文件描述符

### Return Value:

- 如果成功，则返回newfd。
- 否则，它返回一个错误代码（通常为-1），表明发生了错误。

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

1. 没有目录管理，明明有`I_DIRECTORY`的类型，但是没有目录相关的调用，所有的查询读写操作直接与硬盘交互，只有一个根目录；

2. 调用关系混乱，直接越过hd接口调用`hd_service`更底层的函数`hd_rdwt`，实现了一个'完全串行的文件读写'；

3. 没有时间中断调度，没有wait和exit等进程管理函数，进程状态定义甚至没有`zombie`；
4. 文件的读写标志只定义了`O_CREAT` 和 `O_RDWR`，没有定义只读和只写等等标志。
5. 会修改调用者参数的逆天函数；

6. 写了但是不用的文件，写了但是不用的函数，写了但是不用的字段，你甚至不知道这个字段能取什么值；

7. fork中若干bug，exec中若干bug；


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

### fork对文件描述符的继承

fork不能只继承fd的指针，因为父子进程对文件的访问显然是独立的。所以我们需要修改fork，增加对文件描述符的复制和转移。

```c
/* in sys_fork() */

static int fork_fd_cpy(struct file_desc **filp) {
    int src = proc2pid(p_proc_current);
    for (int i = 0; i < NR_FILES; i++) {
        if (filp[i]->flag == 0) continue;
        int fd_nr = get_available_fd_table();
        memcpy(&f_desc_table[fd_nr], filp[i], sizeof(struct file_desc));
        filp[i] = (struct file_desc*)(&f_desc_table[fd_nr]);
        filp[i]->fd_node.fd_inode->i_cnt++;
        if (filp[i]->fd_node.fd_inode->i_mode == I_NAMED_PIPE || filp[i]->fd_node.fd_inode->i_mode == I_UNAMED_PIPE) {
            if (filp[i]->fd_mode == O_WRONLY)
                filp[i]->fd_node.fd_inode->i_pipe->w_counter++;
            else if(filp[i]->fd_mode == O_RDONLY)
                filp[i]->fd_node.fd_inode->i_pipe->r_counter++;
            else {
                kprintf("bad mode! ");
                return -1;
            }
        }
    } 
    return 0;
}
```

### DUP2 - 重定向

dup2函数，把指定的newfd也指向oldfd指向的文件，也就是说，执行完dup2之后，有newfd和oldfd同时指向同一个文件，**共享文件偏移量和文件状态**。若参数`newfd`已经被程序使用，则系统就会将`newfd`所指的文件关闭，若`newfd`等于`oldfd`，则返回`newfd`,而不关闭`newfd`所指的文件。

有了前面的铺垫之后，dup2就是一个简单的指针转移：

```c
static int do_dup2(int oldfd, int newfd) {
    if (oldfd == newfd) {
		return newfd;
    }
    if (p_proc_current->task.filp[newfd]->flag == 1) {
        if (do_vclose(newfd) == -1) {
            kprintf("close error");
            return -1;
        }
    }
    p_proc_current->task.filp[newfd] = p_proc_current->task.filp[oldfd];
    return newfd;
}
```

