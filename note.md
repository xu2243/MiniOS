待补充...

# syscall.asm

```asm
// added by xuxinping 2023-12-16
_NR_pipe        equ 28 ;  

global  pipe        ;   

pipe:
	push	ebx
	mov	ebx, esp
	mov	eax, _NR_pipe
	int	INT_VECTOR_SYS_CALL
	pop	ebx
	ret
```

# global.c

```c
// added by xuxinping 2023-12-16
system_call sys_call_table[NR_SYS_CALL] = {
	/* other_sys_call */
    sys_pipe
};

```

# vfs.c

### pipe相关操作：

1. **create (CreateFile):**
   - 不需要。管道的创建通常由 `pipe()` 系统调用完成。
2. **delete (DeleteFile):**
   - 不需要。管道的删除通常由 `close()` 系统调用完成。
3. **open (OpenFile):**
   - 不需要。同 `create()` ，管道虽然需要支持打开操作，但是是在 `pipe()`系统调用中完成创建和打开文件描述符的。
4. **close (CloseFile):**
   - 需要。关闭操作用于释放与管道相关的资源，例如文件描述符等。
5. **write (WriteFile):**
   - 需要。管道的写入操作用于将数据写入管道，允许一个进程向管道中写入数据。
6. **read (ReadFile):**
   - 需要。管道的读取操作用于从管道中读取数据，允许一个进程从管道中读取数据。
7. **opendir (OpenDir):**
   - 不需要。管道是一种文件通信机制，不是一个目录。
8. **createdir (CreateDir):**
   - 不需要。管道不是目录，不支持在其中创建子目录。
9. **deletedir (DeleteDir):**
   - 不需要。同上，管道不是目录，不支持删除目录。

### FIFO ：

1. **创建（Create）：**
   - `mkfifo()` 或 `mknod()` 等系统调用用于在文件系统中创建 FIFO。
2. **打开（Open）：**
   - `open()` 系统调用用于打开 FIFO，并返回一个文件描述符。
3. **关闭（Close）：**
   - `close()` 系统调用用于关闭 FIFO 文件描述符。
4. **读取（Read）：**
   - `read()` 系统调用用于从 FIFO 中读取数据。
5. **写入（Write）：**
   - `write()` 系统调用用于向 FIFO 中写入数据。
6. **删除（Delete）：**
   - `unlink()` 等系统调用用于删除 FIFO 的文件系统入口。

### linux way

```c
const struct file_operations pipefifo_fops = {
	.open		= fifo_open,
	.llseek		= no_llseek,
	.read_iter	= pipe_read,
	.write_iter	= pipe_write,
	.poll		= pipe_poll,
	.unlocked_ioctl	= pipe_ioctl,
	.release	= pipe_release,
	.fasync		= pipe_fasync,
	.splice_write	= iter_file_splice_write,
};
```

### what did i do

```c
int sys_pipe(void *uesp);
int pipe_read(int fd, void *buf, int count);
int pipe_write(int fd, const void *buf, int count);
int pipe_release(int fd);
```

