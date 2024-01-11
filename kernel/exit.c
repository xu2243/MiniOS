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
#include "x86.h"
#include "exit.h"

void do_exit(int status)
{
    int exit_code = (status & 0xff) << 8;
    int i;
    int ppid = p_proc_current->task.info.ppid;
    PROCESS *p;
    for (i = 0; i < NR_PCBS; i++)
    {
        if (proc_table[i].task.pid == ppid)
        {
            p = &proc_table[i];
            break;
        }
    }

    for (i = 0; i < NR_FILES; i++)
    {
        if (p_proc_current->task.filp[i]->flag == 1)
        {
            do_vclose(i);
        }
    }

    for (;;)
    {
        if (xchg(&p->task.lock, 1) == 1)
            goto loop;
        if (xchg(&p_proc_current->task.lock, 1) == 1)
            goto free;
        break;
    free:
        xchg(&p->task.lock, 0);
    loop:
        sched();
    }
    p_proc_current->task.exit_code = exit_code;
    p_proc_current->task.stat = ZOMBIE;
    p->task.stat = READY;
    xchg(&p_proc_current->task.lock, 0);
    xchg(&p->task.lock, 0);
    sched();
}

void sys_exit(void *uesp)
{
    do_exit(get_arg(uesp, 1));
}