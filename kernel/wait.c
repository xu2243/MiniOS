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
#include "wait.h"
#include "x86.h"

int do_wait(int *wstatus){
  if(p_proc_current->task.info.child_p_num == 0)
    return -ECHILD;

  int i, j, cpid;
  PROCESS* p;

  for(;;){
    for(i=0; i<p_proc_current->task.info.child_p_num; i++){
      cpid = p_proc_current->task.info.child_process[i];
      for(j=0; j<NR_PCBS; ){
        if(proc_table[j].task.pid == cpid && proc_table[j].task.stat == ZOMBIE){
          if(xchg(&proc_table[j].task.lock, 1)==1){
            sched();
            continue;
          }
          if(wstatus!=NULL)
            *wstatus = proc_table[j].task.exit_code;
          proc_table[j].task.stat = IDLE;
          p_proc_current->task.info.child_p_num--;
          for(int x=i; x<p_proc_current->task.info.child_p_num; x++)
            p_proc_current->task.info.child_process[x] = p_proc_current->task.info.child_process[x+1];
          
          xchg(&proc_table[j].task.lock, 0);
          return cpid;
        }
        j++;
      }
    }
    p_proc_current->task.stat = SLEEPING;
    sched();
  }
}

int sys_wait(void* uesp){
  return do_wait((int *)get_arg(uesp, 1));
}
