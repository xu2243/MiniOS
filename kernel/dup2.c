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

static int do_dup2(int oldfd, int newfd){
  p_proc_current->task.filp[newfd] = p_proc_current->task.filp[oldfd];
  return 0;
}

int sys_dup2(void* uesp) {
  return do_dup2(get_arg(uesp, 1), get_arg(uesp, 2));
}