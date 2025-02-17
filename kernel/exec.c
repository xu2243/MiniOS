﻿/**********************************************
*			exec.c 		add by visual 2016.5.23
*************************************************/
#include "type.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "proc.h"
#include "global.h"
#include "proto.h"
#include "elf.h"
#include "fs.h" //added by mingxuan 2019-5-19
#include "vfs.h"
#include "stdio.h"


static u32 exec_elfcpy(u32 fd,Elf32_Phdr Echo_Phdr,u32 attribute);
static u32 exec_load(u32 fd,const Elf32_Ehdr* Echo_Ehdr,const Elf32_Phdr Echo_Phdr[]);
static int exec_pcb_init(char* path);



/*======================================================================*
*                          sys_exec		add by visual 2016.5.23
*exec系统调用功能实现部分
*======================================================================*/
u32 sys_exec(char *path)
{
	Elf32_Ehdr *Echo_Ehdr = NULL;
	Elf32_Phdr *Echo_Phdr = NULL;
	u32 addr_lin;
	u32 err_temp;
	
	char* p_reg;	//point to a register in the new kernel stack, added by xw, 17/12/11

	if( 0==path )
	{
		vga_write_str_color("exec: path ERROR!",0x74);
		return -1;
	}
	
	/*******************打开文件************************/
	// u32 fd = open(path,"r");	//deleted by mingxuan 2019-5-19
	u32 fd = do_vopen(path, O_RDWR);	//deleted by mingxuan 2019-5-19

	if(fd==-1)
	{
		kprintf("sys_exec open error!\n");	//deleted by mingxuan 2019-5-23
		return -1; 
	}
	// u32 fd = fake_open(path,"r");	//modified by xw, 18/5/30 
	
	/*************获取elf信息**************/
	Echo_Ehdr = sys_kmalloc(sizeof(Elf32_Ehdr));
	read_Ehdr(fd, Echo_Ehdr, 0);
	Echo_Phdr = sys_kmalloc(sizeof(Elf32_Phdr) * Echo_Ehdr->e_phnum);
	for (int i = 0 ; i < Echo_Ehdr->e_phnum ; i++)
		read_Phdr(fd, Echo_Phdr + i, Echo_Ehdr->e_phoff + i * sizeof(Elf32_Phdr));
		
	/*************释放进程内存****************/
	//目前还没有实现 思路是：数据、代码根据text_info和data_info属性决定释放深度，其余内存段可以完全释放
	/*************根据elf的program复制文件信息**************/
	if(-1==exec_load(fd,Echo_Ehdr,Echo_Phdr)) return -1;//使用了const指针传递

	/*****************重新初始化该进程的进程表信息（包括LDT）、线性地址布局、进程树属性********************/	
	exec_pcb_init(path);	
	
	/***********************代码、数据、堆、栈***************************/
	//代码、数据已经处理，将eip重置即可
	p_proc_current->task.regs.eip = Echo_Ehdr->e_entry;//进程入口线性地址
	p_reg = (char*)(p_proc_current + 1);	//added by xw, 17/12/11
	*((u32*)(p_reg + EIPREG - P_STACKTOP)) = p_proc_current->task.regs.eip;	//added by xw, 17/12/11
	
	//栈
	p_proc_current->task.regs.esp=(u32)p_proc_current->task.memmap.stack_lin_base;			//栈地址最高处
	*((u32*)(p_reg + ESPREG - P_STACKTOP)) = p_proc_current->task.regs.esp;	//added by xw, 17/12/11
	
	for( addr_lin=p_proc_current->task.memmap.stack_lin_base; addr_lin >= p_proc_current->task.memmap.stack_lin_limit ; addr_lin-=num_4K )
	{
		err_temp = lin_mapping_phy(	addr_lin,//线性地址						//add by visual 2016.5.9
									MAX_UNSIGNED_INT,//物理地址						//edit by visual 2016.5.19
									p_proc_current->task.pid,//进程pid			//edit by visual 2016.5.19
									PG_P  | PG_USU | PG_RWW,//页目录的属性位
									PG_P  | PG_USU | PG_RWW);//页表的属性位

		if( err_temp!=0 )
		{
			vga_write_str_color("kernel_main Error:lin_mapping_phy",0x74);
			return -1;
		}
	}
	//堆    用户还没有申请，所以没有分配，只在PCB表里标示了线性起始位置
	
	real_close(fd);	//added by mingxuan 2019-5-23
	if (Echo_Ehdr != NULL)
		sys_free(Echo_Ehdr);
	if (Echo_Phdr != NULL)
		sys_free(Echo_Phdr);

	//vga_write_str_color("\n[exec success:",0x72);//灰底绿字
	//vga_write_str_color(path,0x72);//灰底绿字	
	//vga_write_str_color("]",0x72);//灰底绿字
	return 0;
}

/*======================================================================*
*                          exec_elfcpy		add by visual 2016.5.23
*复制elf中program到内存中
*======================================================================*/
static u32 exec_elfcpy(u32 fd,Elf32_Phdr Echo_Phdr,u32 attribute)  // 这部分代码将来要移动到exec.c文件中，包括下面exec()中的一部分
{
	u32 lin_addr = Echo_Phdr.p_vaddr;
	u32 lin_limit = Echo_Phdr.p_vaddr + Echo_Phdr.p_memsz;
	u32 file_offset = Echo_Phdr.p_offset;
	u32 file_limit = Echo_Phdr.p_offset + Echo_Phdr.p_filesz;
	char ch;
	//u32 pde_addr_phy = get_pde_phy_addr(p_proc_current->task.pid); //页目录物理地址			//delete by visual 2016.5.19
	//u32 addr_phy = do_malloc(Echo_Phdr.p_memsz);//申请物理内存					//delete by visual 2016.5.19
	for(  ; lin_addr<lin_limit ; lin_addr++,file_offset++ )
	{	
		lin_mapping_phy(lin_addr,MAX_UNSIGNED_INT,p_proc_current->task.pid,PG_P  | PG_USU | PG_RWW/*说明*/,attribute);//说明：PDE属性尽量为读写，因为它要映射1024个物理页，可能既有数据，又有代码	//edit by visual 2016.5.19
		if( file_offset<file_limit )
		{//文件中还有数据，正常拷贝
			//modified by xw, 18/5/30
			// seek(file_offset);
			// read(fd,&ch,1);

			//fake_seek(file_offset); //deleted by mingxuan 2019-5-22
			//real_lseek(fd, file_offset, SEEK_SET); //modified by mingxuan 2019-5-22
			do_vlseek(fd, file_offset, SEEK_SET);	//modified by mingxuan 2019-5-24
			
			//fake_read(fd,&ch,1); //deleted by mingxuan 2019-5-22
			//real_read(fd, &ch, 1); //modified by mingxuan 2019-5-22
			do_vread(fd, &ch, 1);	//modified by mingxuan 2019-5-24
			//~xw
			
			*((u8*)lin_addr) = ch;//memcpy((void*)lin_addr,&ch,1);
		}
		else
		{
			//已初始化数据段拷贝完毕，剩下的是未初始化的数据段，在内存中填0
			*((u8*)lin_addr) = 0;//memset((void*)lin_addr,0,1);
		}
	}
	return 0;
}


/*======================================================================*
*                          exec_load		add by visual 2016.5.23
*根据elf的program复制文件信息
*======================================================================*/
static u32 exec_load(u32 fd,const Elf32_Ehdr* Echo_Ehdr,const Elf32_Phdr Echo_Phdr[])
{
	u32 ph_num;
	
	if( 0==Echo_Ehdr->e_phnum )
	{
		vga_write_str_color("exec_load: elf ERROR!",0x74);
		return -1;
	}

    // 将基址和限制初始化为表示未初始化状态的值
    p_proc_current->task.memmap.text_lin_base = 0;
    p_proc_current->task.memmap.text_lin_limit = 0;
    p_proc_current->task.memmap.data_lin_base = 0;
    p_proc_current->task.memmap.data_lin_limit = 0;

	// (This is bullshit)我们还不能确定elf中一共能有几个program，但就目前我们查看过的elf文件中，只出现过两中program，一种.text（R-E）和一种.data（RW-）
	// 上面一句话导致了去年出现了诡异的错误，暂时只能说简单修复了一下，但是这个系统的权限就很混乱
    // (This is bullshit  -2024/1/11) 
	for( ph_num=0; ph_num<Echo_Ehdr->e_phnum ; ph_num++ )
	{
		if( 0==Echo_Phdr[ph_num].p_memsz )
		{//最后一个program
			break;
		}
        if (Echo_Phdr[ph_num].p_flags & 0x1) { // .text 段
            exec_elfcpy(fd, Echo_Phdr[ph_num], PG_P | PG_USU | PG_RWR);

            // 更新文本段限制
            if (p_proc_current->task.memmap.text_lin_base == 0 ||
                p_proc_current->task.memmap.text_lin_base > Echo_Phdr[ph_num].p_vaddr) {
                p_proc_current->task.memmap.text_lin_base = Echo_Phdr[ph_num].p_vaddr;
            }

            if (p_proc_current->task.memmap.text_lin_limit < Echo_Phdr[ph_num].p_vaddr + Echo_Phdr[ph_num].p_memsz) {
                p_proc_current->task.memmap.text_lin_limit = Echo_Phdr[ph_num].p_vaddr + Echo_Phdr[ph_num].p_memsz;
            }
        } else if (Echo_Phdr[ph_num].p_flags & 0x4) { // .data 段
            exec_elfcpy(fd, Echo_Phdr[ph_num], PG_P | PG_USU | PG_RWW);

            // 更新数据段限制
            if (p_proc_current->task.memmap.data_lin_base == 0 ||
                p_proc_current->task.memmap.data_lin_base > Echo_Phdr[ph_num].p_vaddr) {
                p_proc_current->task.memmap.data_lin_base = Echo_Phdr[ph_num].p_vaddr;
            }

            if (p_proc_current->task.memmap.data_lin_limit < Echo_Phdr[ph_num].p_vaddr + Echo_Phdr[ph_num].p_memsz) {
                p_proc_current->task.memmap.data_lin_limit = Echo_Phdr[ph_num].p_vaddr + Echo_Phdr[ph_num].p_memsz;
            }
        }
		else 
		{
			vga_write_str_color("exec_load: unKnown elf'program!",0x74);
			return -1;
		}
	}
	return 0;
}


/*======================================================================*
*                          exec_init		add by visual 2016.5.23
* 重新初始化寄存器和特权级、线性地址布局
*======================================================================*/
static int exec_pcb_init(char* path)
{
	char* p_regs;	//point to registers in the new kernel stack, added by xw, 17/12/11
	
	//名称 状态 特权级 寄存器
	strncpy(p_proc_current->task.p_name, path, sizeof(p_proc_current->task.p_name) - 1); // 名称
	p_proc_current->task.stat = READY;  						//状态
	p_proc_current->task.ldts[0].attr1 = DA_C | PRIVILEGE_USER << 5;//特权级修改为用户级
	p_proc_current->task.ldts[1].attr1 = DA_DRW | PRIVILEGE_USER << 5;//特权级修改为用户级
	p_proc_current->task.regs.cs	= ((8 * 0) & SA_RPL_MASK & SA_TI_MASK)| SA_TIL | RPL_USER;
	p_proc_current->task.regs.ds	= ((8 * 1) & SA_RPL_MASK & SA_TI_MASK)| SA_TIL | RPL_USER;
	p_proc_current->task.regs.es	= ((8 * 1) & SA_RPL_MASK & SA_TI_MASK)| SA_TIL | RPL_USER;
	p_proc_current->task.regs.fs	= ((8 * 1) & SA_RPL_MASK & SA_TI_MASK)| SA_TIL | RPL_USER;
	p_proc_current->task.regs.ss	= ((8 * 1) & SA_RPL_MASK & SA_TI_MASK)| SA_TIL | RPL_USER;
	p_proc_current->task.regs.gs	= (SELECTOR_KERNEL_GS & SA_RPL_MASK)| RPL_USER;
	p_proc_current->task.regs.eflags = 0x202; /* IF=1,bit2 永远是1 */
	
	/***************copy registers data****************************/
	//copy registers data to the bottom of the new kernel stack
	//added by xw, 17/12/11
	p_regs = (char*)(p_proc_current + 1);
	p_regs -= P_STACKTOP;
	memcpy(p_regs, (char*)p_proc_current, 18 * 4);
	
	//进程表线性地址布局部分，text、data已经在前面初始化了
	p_proc_current->task.memmap.vpage_lin_base = VpageLinBase;					//保留内存基址
	p_proc_current->task.memmap.vpage_lin_limit = VpageLinBase;					//保留内存界限
	p_proc_current->task.memmap.heap_lin_base = HeapLinBase;						//堆基址
	p_proc_current->task.memmap.heap_lin_limit = HeapLinBase;						//堆界限	
	p_proc_current->task.memmap.stack_child_limit = StackLinLimitMAX;		//add by visual 2016.5.27
	p_proc_current->task.memmap.stack_lin_base = StackLinBase;						//栈基址
	p_proc_current->task.memmap.stack_lin_limit = StackLinBase - 0x4000;			//栈界限（使用时注意栈的生长方向）
	p_proc_current->task.memmap.arg_lin_base = ArgLinBase;						//参数内存基址
	p_proc_current->task.memmap.arg_lin_limit = ArgLinBase;						//参数内存界限
	p_proc_current->task.memmap.kernel_lin_base = KernelLinBase;					//内核基址
	p_proc_current->task.memmap.kernel_lin_limit = KernelLinBase + KernelSize;		//内核大小初始化为8M
	
	//进程树属性,只要改两项，其余不用改
	//p_proc_current->task.info.type = TYPE_PROCESS;			//当前是进程还是线程
	//p_proc_current->task.info.real_ppid = -1;  	//亲父进程，创建它的那个进程
	//p_proc_current->task.info.ppid = -1;			//当前父进程	
	//p_proc_current->task.info.child_p_num = 0;	//子进程数量
	//p_proc_current->task.info.child_process[NR_CHILD_MAX];//子进程列表
	//p_proc_current->task.info.child_t_num = 0;		//子线程数量
	//p_proc_current->task.info.child_thread[NR_CHILD_MAX];//子线程列表	
	p_proc_current->task.info.text_hold = 1;			//是否拥有代码
	p_proc_current->task.info.data_hold = 1;			//是否拥有数据
	
	return 0;
}


