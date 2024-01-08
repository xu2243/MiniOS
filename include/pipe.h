#ifndef PIPE_H 
#define PIPE_H  

#include "list.h"
#include "proc.h"
#include "fs.h"

#define PIPE_BUF_PAGE_NUM   1
#define KB (unsigned)(1024)
#define MB (KB*KB)
#define PAGE_SIZE (4 * KB)
#define READ_MODE   0x1
#define WRITE_MODE  0x2

typedef struct wait_queue_head{
    struct list_head wait_queue;
    PROCESS *proc;
} wait_queue_head_t;


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
	int mutex;
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
	unsigned int files;
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

int sys_pipe(void *uesp);
int sys_mkfifo(void *uesp);
int pipe_read(int fd, void *buf, int count);
int pipe_write(int fd, const void *buf, int count);
int pipe_close(int fd);
int pipe_info_release(struct pipe_inode_info *pipe_info);
// int fifo_unlink(const char *path);


#endif