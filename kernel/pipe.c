/**********************************************************
*	pipe.c       //added by xuxinping 2024-01-01
***********************************************************/

#include "vfs.h"
#include "type.h"
#include "const.h"
#include "protect.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "proto.h"
#include "fs_const.h"
#include "fs.h"
#include "fs_misc.h"

#define MAX_PIPE_INODE 512

void init_queue_head(wait_queue_head_t *wq) {
    INIT_LIST_HEAD(&wq->wait_queue);
}

void wait_queue_push(wait_queue_head_t *wq, PROCESS *proc) {
    wait_queue_head_t *p = (wait_queue_head_t *)sys_kmalloc(sizeof(wait_queue_head_t));
    list_add_tail(&p->wait_queue, &wq->wait_queue);
}

PROCESS *wait_queue_head(wait_queue_head_t *wq) {
    return ((wait_queue_head_t *)(wq->wait_queue.next))->proc;
}

void wait_queue_pop(wait_queue_head_t *wq) {
    list_del(wq->wait_queue.next);
}

int wait_queue_is_empty(wait_queue_head_t *wq) {
    return list_empty(&wq->wait_queue);
}

struct pipe_inode_map {
    int max_inodes;             /**< 最大inode数量 */
    int *allocated_inodes;      /**< 已分配的inode数组，用于快速查找 */
}pipe_map;

// 初始化pipe_inode_map结构
void init_pipe_inode_map() {
    pipe_map.max_inodes = MAX_PIPE_INODE;
    pipe_map.allocated_inodes = (int *)sys_kmalloc(sizeof(int) * MAX_PIPE_INODE);
    for (int i = 0; i < MAX_PIPE_INODE; i++) {
        pipe_map.allocated_inodes[i] = 0; // 初始化为未分配状态
    }
}

// 释放pipe_inode_map结构占用的内存
void cleanup_pipe_inode_map() {
    sys_free(pipe_map.allocated_inodes);
}

// 获取下一个可用的pipe inode编号
static int get_nxt_pipe_inode_num() {
    for (int i = 0; i < pipe_map.max_inodes; i++) {
        if (pipe_map.allocated_inodes[i] == 0) {
            pipe_map.allocated_inodes[i] = 1;
            return i;
        }
    }
    return -1; // 没有可用的inode编号
}

// 释放指定的pipe inode编号
static void release_pipe_inode_num(int inode_number) {
    pipe_map.allocated_inodes[inode_number] = 0;
}


struct pipe_inode_info *alloc_pipe_info() {
    struct pipe_inode_info *pipe = (void*)K_PHY2LIN(sys_kmalloc(sizeof(struct pipe_inode_info)));

    pipe->mutex = 0;
    pipe->head = 0;
    pipe->tail = 0;
    pipe->max_usage = 0;
    pipe->ring_size = PIPE_BUF_PAGE_NUM * PAGE_SIZE;
    pipe->readers = 0;
    pipe->writers = 0;
    pipe->files = 0;
    pipe->r_counter = 1;
    pipe->w_counter = 1;
    pipe->user = p_proc_current;

    init_queue_head(&pipe->rd_wait);
    init_queue_head(&pipe->wr_wait);

    pipe->rd_wait.proc = NULL;
    pipe->wr_wait.proc = NULL;

    pipe->bufs = (void *)K_PHY2LIN((sys_kmalloc_4k()));
    return pipe;
}


struct inode *get_pipe_inode() {
    struct inode *inode = (void*)K_PHY2LIN(sys_kmalloc(sizeof(struct inode)));
    struct pipe_inode_info *pipe = alloc_pipe_info();

    inode->i_mode = I_NAMED_PIPE;
    inode->i_dev = PIPEFIFO;
    inode->i_num = get_nxt_pipe_inode_num();
    inode->i_pipe = pipe;

    return inode;
}

int get_available_fd() {
    for (int i = 0; i < NR_FILES; i++) {
        if(p_proc_current->task.filp[i]->flag == 0) {
            // p_proc_current->task.filp[i]->flag = 1;
            return i;
        }
    }
    return -1;
}


/**
 * This function creates a unidirectional communication channel using a pipe. The pipefd array
 * is used to return two file descriptors: pipefd[0] for reading from the pipe and pipefd[1]
 * for writing to the pipe. Data written to the write end of the pipe is buffered by the kernel
 * until it is read from the read end of the pipe. If successful, the function returns 0; otherwise,
 * it returns -1, and the errno variable is set to indicate the error.
 * added by xuxinping 2023-12-16
 * 
 * @param pipefd An array to store the file descriptors for read and write ends of the pipe.
 *                 pipefd[0] will contain the file descriptor for the read end.
 *                 pipefd[1] will contain the file descriptor for the write end.
 * @return 0 on success, -1 on failure. In case of failure, errno will be set appropriately.
 *
 * 
 */
int do_pipe(int pipefd[2]) {
    struct file_desc *r_fd = (void*)K_PHY2LIN(sys_kmalloc(sizeof (struct file_desc)));
    struct file_desc *w_fd = (void*)K_PHY2LIN(sys_kmalloc(sizeof (struct file_desc)));

    union ptr_node *r_ptr = (void*)K_PHY2LIN(sys_kmalloc(sizeof (union ptr_node)));
    union ptr_node *w_ptr = (void*)K_PHY2LIN(sys_kmalloc(sizeof (union ptr_node)));

    struct inode *pipe_inode = get_pipe_inode();
    // struct inode *w_inode = get_pipe_inode();

    int r_fd_num = get_available_fd();
    int w_fd_num = get_available_fd();

    // r_fd->fd_mode = ?; 
    // it feels really confused when you look into the source code of miniOS. i guess it was used to
    // discribed xwr mod or some kind like that.
    // I can hardly find any macro definitions related to the possible flags.
    // so I just use it on my own way, distinguish read and write mode 

    r_fd->fd_mode = READ_MODE;
    w_fd->fd_mode = WRITE_MODE;

    r_fd->dev_index = PIPEFIFO;
    w_fd->dev_index = PIPEFIFO;

    r_fd->flag = 1;
    w_fd->flag = 1;

    r_ptr->fd_inode = pipe_inode;
    w_ptr->fd_inode = pipe_inode;

    pipe_inode->i_pipe->files += 2;
    // readers and writers should be added by their own 
    // pipe_inode->i_pipe->readers++;
    // pipe_inode->i_pipe->writers++;

    p_proc_current->task.filp[r_fd_num] = r_fd;
    p_proc_current->task.filp[w_fd_num] = w_fd;
    
    pipefd[0] = r_fd_num;
    pipefd[1] = w_fd_num;
    
    return 0;
}

int pipe_read(int fd, void *buf, int count) {
    struct file_desc *file = p_proc_current->task.filp[fd];
    struct pipe_inode_info *pipe_info = file->fd_node.fd_inode->i_pipe;
    
    // Check if the file descriptor is valid
    if (file == NULL || file->flag == 0 || file->fd_mode != READ_MODE || count < 0) {
        // errno = EBADF; // Bad file descriptor
        return -1;
    }

    wait_queue_push(&pipe_info->rd_wait, p_proc_current);
    pipe_info->readers++;

    while (pipe_info->mutex || wait_queue_head(&pipe_info->rd_wait) != p_proc_current) {
        p_proc_current->task.stat = SLEEPING;
		sched();
    }

    pipe_info->mutex = 1;

    // TODO: memcpy from pipe_info->bufs to user buffer
    unsigned int ret = 0;

    while (1) {
        if (ret == count) {
            wait_queue_pop(&pipe_info->rd_wait);
            wait_queue_head(&file->fd_node.fd_inode->i_pipe->wr_wait)->task.stat = READY;
            pipe_info->readers--;
            pipe_info->mutex = 0;
            return ret;
        } else if (pipe_info->max_usage > 0) {
            *(char *)(buf + ret) = *(pipe_info->bufs + (pipe_info->head % PAGE_SIZE));
            pipe_info->head++;
            pipe_info->max_usage--;
            pipe_info->head %= PAGE_SIZE;
            ret++;
        } else {
            if (pipe_info->w_counter == 0) {
                pipe_info->readers--;
                pipe_info->mutex = 0;
                return ret;
            }
            wait_queue_head(&file->fd_node.fd_inode->i_pipe->wr_wait)->task.stat = READY;
            pipe_info->mutex = 0;
            p_proc_current->task.stat = SLEEPING;
		    sched();

            while (pipe_info->mutex) {
                p_proc_current->task.stat = SLEEPING;
                sched();
            }
            pipe_info->mutex = 1;
        }
    }
    // never come to here.
    return 0;
}

int pipe_write(int fd, const void *buf, int count) {
    struct file_desc *file = p_proc_current->task.filp[fd];
    struct pipe_inode_info *pipe_info = file->fd_node.fd_inode->i_pipe;
    
    // Check if the file descriptor is valid
    if (file == NULL || file->flag == 0 || file->fd_mode != WRITE_MODE || count < 0) {
        // errno = EBADF; // Bad file descriptor
        return -1;
    }

    wait_queue_push(&pipe_info->wr_wait, p_proc_current);
    pipe_info->writers++;

    while (pipe_info->mutex || wait_queue_head(&pipe_info->wr_wait) != p_proc_current) {
        p_proc_current->task.stat = SLEEPING;
		sched();
    }

    pipe_info->mutex = 1;

    // TODO: memcpy from pipe_info->bufs to user buffer
    unsigned int ret = 0;

    while (1) {
        if (ret == count) {
            wait_queue_pop(&pipe_info->wr_wait);
            wait_queue_head(&file->fd_node.fd_inode->i_pipe->rd_wait)->task.stat = READY;
            pipe_info->writers--;
            pipe_info->mutex = 0;
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
                pipe_info->mutex = 0;
                return ret;
            }
            wait_queue_head(&file->fd_node.fd_inode->i_pipe->rd_wait)->task.stat = READY;
            pipe_info->mutex = 0;
            p_proc_current->task.stat = SLEEPING;
		    sched();

            while (pipe_info->mutex) {
                p_proc_current->task.stat = SLEEPING;
                sched();
            }
            pipe_info->mutex = 1;
        }
    }
    // never come to here.
    return 0;
}

void __free(unsigned addr, unsigned size) {
    struct memfree memarg;
    memarg.addr = (u32)addr; memarg.size = (u32)size;
    sys_free((void *)&memarg);
}

int pipe_release(int fd) {
    struct file_desc *file = p_proc_current->task.filp[fd];
    struct pipe_inode_info *pipe_info = file->fd_node.fd_inode->i_pipe;

    pipe_info->files--;
    if (file->fd_mode == READ_MODE) pipe_info->r_counter--;
    if (file->fd_mode == WRITE_MODE) pipe_info->w_counter--;

    struct memfree memarg;

    if (pipe_info->files == 0) {
        release_pipe_inode_num(file->fd_node.fd_inode->i_num);
        __free((unsigned)pipe_info->bufs, pipe_info->ring_size);
        __free((unsigned)pipe_info, sizeof(struct pipe_inode_info));
        __free((unsigned)file->fd_node.fd_inode, sizeof(struct inode));
    }
    __free((unsigned)&file->fd_node, sizeof(union ptr_node));
    file->flag = 0;
    __free((unsigned)file, sizeof(struct file_desc));
    return 0;
}

int pipe_unlink(const char *path) {
    return 0;
}


int sys_pipe(void *uesp) {
    return do_pipe((int *)get_arg(uesp, 1));
}

int sys_mkfifo(void *uesp) {
    return 0;
}