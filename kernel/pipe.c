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
#include "string.h"
#include "stdio.h"
#include "memman.h"
#include "spinlock.h"
// #include "list.h"

extern struct file_desc f_desc_table[NR_FILE_DESC];
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
    wait_queue_head_t *p = K_PHY2LIN(sys_kmalloc(sizeof(wait_queue_head_t)));
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


struct pipe_inode_info *alloc_pipe_info() {
    struct pipe_inode_info *pipe = (void*)K_PHY2LIN(sys_kmalloc(sizeof(struct pipe_inode_info)));
    if ((int)pipe == -1) {
        kprintf("error malloc\n");
        return NULL;
    }
    memset(pipe, 0, sizeof(struct pipe_inode_info));
    pipe->ring_size = PIPE_BUF_SIZE;
    pipe->user = p_proc_current;
    
    initlock(&pipe->mutex, "PIPE_LOCK");

    init_queue_head(&pipe->rd_wait);
    init_queue_head(&pipe->wr_wait);

    pipe->rd_wait.proc = NULL;
    pipe->wr_wait.proc = NULL;

    pipe->bufs = (void *)K_PHY2LIN((sys_kmalloc(PIPE_BUF_SIZE)));
    if ((int)pipe->bufs == -1) {
        kprintf("error malloc\n");
        do_free((u32)pipe, sizeof(struct pipe_inode_info));
        return NULL;
    }
    return pipe;
}


int get_pipe_inode(struct inode *inode) {
    struct pipe_inode_info *pipe = alloc_pipe_info();
    if ((int)pipe == -1) {
        kprintf("error alloc_pipe_info\n");
        return -1;
    }

    inode->i_dev = 0;
    inode->i_cnt = 1;
    inode->i_pipe = pipe;

    return 0;
}

/* find a free slot in PROCESS::filp[] */
int get_available_proc_fd() {
    for (int i = 0; i < NR_FILES; i++) {
        if(p_proc_current->task.filp[i]->flag == 0) {
            // p_proc_current->task.filp[i]->flag = 1;
            return i;
        }
    }
    return -1;
}


/* mode == O_RDONLY or O_WRONLY */
int create_pipe(int *pipefd, struct inode *pipe_inode, int mode) {
    if (mode != O_RDONLY && mode != O_WRONLY) {
        return -1;
    }
    int f_table_idx = get_available_fd_table();
    if (f_table_idx == -1) {
        return -1;
    }
    int fd_num = get_available_proc_fd();
    if (fd_num == -1) {
        return -1;
    }
    // kprintf("*%d ", fd_num);
    /* f_desc_table doesnt have a mutex, probably take concurrency error */
    struct file_desc *pfd = &f_desc_table[f_table_idx];
    pfd->flag = 1;

    union ptr_node *ptr = &pfd->fd_node;

    // r_fd->fd_mode = ?; 
    // it feels really confused when you look into the source code of miniOS. i guess it was used to
    // discribed xwr mod or some kind like that.
    // I can hardly find any macro definitions related to the possible flags.
    // so I just use it on my own way, distinguish read and write mode 

    pfd->fd_mode = mode;
    pfd->dev_index = orange;
    ptr->fd_inode = pipe_inode;
    // pipe_inode->i_pipe->files++;
    pipe_inode->i_cnt++;

    if (mode == O_RDONLY) 
        pipe_inode->i_pipe->r_counter++;
    else if (mode == O_WRONLY) 
        pipe_inode->i_pipe->w_counter++;
    else {
        // O_RDWR
        pipe_inode->i_pipe->r_counter++;
        pipe_inode->i_pipe->w_counter++;
    }

    // readers and writers should be added by their own 
    // pipe_inode->i_pipe->readers++;
    // pipe_inode->i_pipe->writers++;

    p_proc_current->task.filp[fd_num] = pfd;
    
    *pipefd = fd_num;
    
    return 0;
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
int do_pipe(int *pipefd) {
    /* This is the unnamed pipe, as a result, we haven't assigned it 
     * an inode number and have allocated memory for it using malloc 
     * independently, instead of taking the inode table in fs.c.
     */
    struct inode *pipe_inode = (void*)K_PHY2LIN(sys_kmalloc(sizeof(struct inode)));;
    if (get_pipe_inode(pipe_inode) == -1) {
        kprintf("error get_pipe_inode\n");
        return -1;
    }
    pipe_inode->i_mode = I_UNAMED_PIPE;
    if (create_pipe(&pipefd[0], pipe_inode, O_RDONLY) == -1) {
        /* error handler */
        kprintf("pipe failed!");
        return -1;
    }
    if (create_pipe(&pipefd[1], pipe_inode, O_WRONLY) == -1) {
        /* error handler */
        kprintf("pipe failed!");
        return -1;
    }
    
    return 0;
}

int pipe_read(int fd, void *buf, int count) {
    struct file_desc *file = p_proc_current->task.filp[fd];

    // Check if the file descriptor is valid
    if (file == NULL || file->flag == 0 || count < 0) {
        // errno = EBADF; // Bad file descriptor
        return -1;
    }

    struct pipe_inode_info *pipe_info = file->fd_node.fd_inode->i_pipe;

    if (file->fd_mode != O_RDONLY && file->fd_mode != O_RDWR) {
        kprintf("[Bad file descriptor]");
        return -1;
    }

    wait_queue_push(&pipe_info->rd_wait, p_proc_current);
    pipe_info->readers++;

    while (wait_queue_head(&pipe_info->rd_wait) != p_proc_current) {
        p_proc_current->task.stat = SLEEPING;
		sched();
    }

    acquire(&(pipe_info->mutex));

    // TODO: memcpy from pipe_info->bufs to user buffer
    unsigned int ret = 0;

    while (1) {
        if (ret == count) {
            wait_queue_pop(&pipe_info->rd_wait);
            wait_queue_head(&file->fd_node.fd_inode->i_pipe->wr_wait)->task.stat = READY;
            pipe_info->readers--;
            release(&pipe_info->mutex);
            return ret;
        } else if (pipe_info->max_usage > 0) {
            *(char *)((char *)buf + ret) = *(pipe_info->bufs + (pipe_info->head % PIPE_BUF_SIZE));
            pipe_info->head++;
            pipe_info->max_usage--;
            pipe_info->head %= PIPE_BUF_SIZE;
            ret++;
        } else {
            if (pipe_info->w_counter == 0) {
                pipe_info->readers--;
                release(&pipe_info->mutex);
                return ret;
            }
            wait_queue_head(&file->fd_node.fd_inode->i_pipe->wr_wait)->task.stat = READY;
            release(&pipe_info->mutex);
            p_proc_current->task.stat = SLEEPING;
		    sched();
            acquire(&pipe_info->mutex);
        }
    }
    // never come to here.
    return 0;
}

int pipe_write(int fd, const void *buf, int count) {
    struct file_desc *file = p_proc_current->task.filp[fd];

    if (file == NULL || file->flag == 0 || count < 0) {
        // errno = EBADF; // Bad file descriptor
        kprintf("[Bad file descriptor mode]");
        return -1;
    }

    struct pipe_inode_info *pipe_info = file->fd_node.fd_inode->i_pipe;

    if (file->fd_mode != O_WRONLY && file->fd_mode != O_RDWR) {
        kprintf("[Bad file descriptor mode]");
        return -1;
    }

    wait_queue_push(&pipe_info->wr_wait, p_proc_current);
    pipe_info->writers++;

    while (wait_queue_head(&pipe_info->wr_wait) != p_proc_current) {
        p_proc_current->task.stat = SLEEPING;
		sched();
    }

    acquire(&pipe_info->mutex);

    // TODO: memcpy from pipe_info->bufs to user buffer
    unsigned int ret = 0;

    while (1) {
        if (ret == count) {
            wait_queue_pop(&pipe_info->wr_wait);
            wait_queue_head(&file->fd_node.fd_inode->i_pipe->rd_wait)->task.stat = READY;
            pipe_info->writers--;
            release(&pipe_info->mutex);
            return ret;
        } else if (pipe_info->max_usage < PIPE_BUF_SIZE) {
            *(char *)(pipe_info->bufs + (pipe_info->tail % PIPE_BUF_SIZE)) = *(char *)((char *)buf + ret);
            pipe_info->tail++;
            pipe_info->max_usage++;
            pipe_info->tail %= PIPE_BUF_SIZE;
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
    // never come to here.
    return 0;
}


int pipe_info_release(struct pipe_inode_info *pipe_info) {
    // struct pipe_inode_info *pipe_info = pipe_inode->i_pipe;
    if (!wait_queue_is_empty(&pipe_info->rd_wait) || !wait_queue_is_empty(&pipe_info->wr_wait)) {
        kprintf("[release failed, its still being reading!]");
        return -1;
    }
    do_free((unsigned)pipe_info->bufs, pipe_info->ring_size);
    do_free((unsigned)pipe_info, sizeof(struct pipe_inode_info));
    return 0;
}

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

    if (pipe_inode->i_cnt == 1) {
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

int do_mkfifo(const char *path) {
    char filename[MAX_FILENAME_LEN];
    char pathname[MAX_PATH];
    struct inode * dir_inode, *fifoinode;
    memcpy(pathname, path, strlen(path));
    memset(filename, 0, MAX_FILENAME_LEN);

    if (strip_path(filename, pathname, &dir_inode) != 0) {
		kprintf("{bad path!}");
        return -1;
    }
    // kprintf("(%s)", filename);

    /* This is the named pipe, we alloc it into the inode table */
    int inode_nr = alloc_imap_bit(dir_inode->i_dev);
    int free_sect_nr = alloc_smap_bit(dir_inode->i_dev, NR_DEFAULT_FILE_SECTS);
    fifoinode = get_inode(dir_inode->i_dev, inode_nr);

    fifoinode->i_mode = I_NAMED_PIPE;

    fifoinode->i_start_sect = free_sect_nr;
	fifoinode->i_nr_sects = NR_DEFAULT_FILE_SECTS;
    fifoinode->i_num = inode_nr;
    fifoinode->i_cnt = 1;

    /* update dir inode */
	sync_inode(fifoinode);

	new_dir_entry(dir_inode, fifoinode->i_num, filename);

	return 0;
}


int sys_pipe(void *uesp) {
    return do_pipe((int *)get_arg(uesp, 1));
}

int sys_mkfifo(void *uesp) {
    return do_mkfifo((const char *)get_arg(uesp, 1));
}