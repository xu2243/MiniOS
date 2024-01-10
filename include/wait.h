#ifndef WAIT_H
#define WAIT_H

#include "proc.h"

#define ECHILD 10

int sys_wait(void* uesp);


#endif