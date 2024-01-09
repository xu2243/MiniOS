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

int do_wait(int *wstatus){
  if(p_proc_current->task.info.child_p_num == 0)
    return -ECHILD;

  int i, j, cpid;
  PROCESS* p;

  for(;;){
    for(i=0; i<NR_CHILD_MAX; i++){
      cpid = p_proc_current->task.info.child_process[i];
      for(j=0; j<NR_PCBS; j++){
        if(proc_table[j].task.pid == cpid && proc_table[j].task.stat == ZOMBIE){

        }

      }
    }

  }

  return 0;
}

int sys_wait(void* uesp){
  return do_wait((int *)get_arg(uesp, 1));
}
