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

在linux里，file结构体有一个file_operations字段。open，write等系统调用可以直接调用fd对应的fop接口进行操作。

```c
/* fs.h */
struct file {
	/* other fields */
	struct path		f_path;
	struct inode		*f_inode;	/* cached value */
	const struct file_operations	*f_op;
	/* other fields */
} __randomize_layout

/* pipe.c */
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

首先我们有对应的调用函数：

```c
int sys_pipe(void *uesp);
int pipe_read(int fd, void *buf, int count);
int pipe_write(int fd, const void *buf, int count);
int pipe_release(int fd);
```

在miniOS中，read的调用链是read->do_vread->read_op.

在这个调用链中，值得注意的是miniOS是根据vfs_table对应的op操作来选择的。如果是orange系统，则选择orange系统的op；如果是fat32系统，则选择fat32系统的op。

```c
int do_vread(int fd, char *buf, int count) {
    int index = p_proc_current->task.filp[fd]->dev_index;
    return vfs_table[index].op->read(fd, buf, count);   //modified by mingxuan 2020-10-18
}
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

但是这显然不是一个正确的设计，因为我们的fifo最后应该出现在orange的文件目录里，而orange甚至还给我们的fifo关键字留了一个宏定义（这是一个仿照`linux-0.12`的宏定义设计）！

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

但是在具体的操作中，系统调用并不区分这些文件类型



# NOTE

在linux中，文件目录被抽象成了一个虚拟文件系统，每个”目录文件“管理目录下的文件条目。对于文件识别，则是使用inode中的superblock所指向的魔数，或者直接通过后缀来识别。

而在miniOS中，没有这样独立的目录管理，它只有一个根目录，你需要直接和磁盘硬件交互：

```c
 /* 在 base.c 的代码中，miniOS直接默认了 2 是父目录的所在簇，也就是根目录。*/
 STATE PathToCluster(PCHAR path, PDWORD cluster)
{
	/* ... */
	DWORD parentCluster=2;
	/* ... */
	if(i>=len)//说明是根目录
	{
		*cluster=2;
		return OK;
	}
	/* ... */
	*cluster=parentCluster;
	return OK;
}

// where says in fs.c
/*
 * In Orange'S FS v1.0, all files are stored in the root directory.
 * There is no sub-folder thing.
 */
```

文件路径调用需要在路径前加上文件所属的系统，这样它才能正常识别，非常的独特：

```c
/*
 * You need to add the system to which the file belongs as a prefix for it to be correctly 
 * identified. MiniOS uses the get_index function to retrieve its device.
 * Examples are as follows.
 */
exec("orange/shell_0.bin");
open("fat0/test.tt", O_CREAT));


/*
 * in vfs.c/do_vopen()
 * System prefix is needed to identify the file dev.
 */
int do_vopen(const char *path, int flags) {
    /* ... */
    int index;
    
    index = get_index(pathname);
    if(index == -1){
        kprintf("pathname error! path: %s\n", path);
        return -1;
    }
	/* here to be used */
    fd = vfs_table[index].op->open(pathname, flags);    //modified by mingxuan 2020-10-18
    
	/* ... */
                   
    return fd;    
}
```



bug here

```c
// fs.c/do_unlink(MESSAGE *fs_msg)
// mix up the i_link and i_count
if (pin->i_cnt > 1) {	/* the file was opened */
    kprintf("cannot remove file %s, because pin->i_cnt is %d\n", pathname, pin->i_cnt);
    return -1;
}
```

