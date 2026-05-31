
#include "include/types.h"
#include "include/riscv.h"
#include "include/param.h"
#include "include/memlayout.h"
#include "include/spinlock.h"
#include "include/proc.h"
#include "include/syscall.h"
#include "include/timer.h"
#include "include/kalloc.h"
#include "include/string.h"
#include "include/printf.h"
#include "include/vm.h"

extern int exec(char *path, char **argv);
extern uint ticks;

extern int clone(uint64 stack);
extern int wait4(int target_pid, uint64 addr);


uint64
sys_exec(void)
{
  char path[FAT32_MAX_PATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, FAT32_MAX_PATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_trace(void)
{
  int mask;
  if(argint(0, &mask) < 0) {
    return -1;
  }
  myproc()->tmask = mask;
  return 0;
}

uint64
sys_times(void)
{
  uint64 addr;
  if (argaddr(0, &addr) < 0)
    return -1;

  struct tms {
    uint64 tms_utime;
    uint64 tms_stime;
    uint64 tms_cutime;
    uint64 tms_cstime;
  } ktms;

  uint64 ticks_now;
  acquire(&tickslock);
  ticks_now = ticks;
  release(&tickslock);

  ktms.tms_utime = ticks_now;
  ktms.tms_stime = ticks_now;
  ktms.tms_cutime = 0;
  ktms.tms_cstime = 0;

  if (copyout2(addr, (char*)&ktms, sizeof(ktms)) < 0)
    return -1;

  return ticks_now;
}

uint64
sys_uname(void)
{
  uint64 addr;
  if (argaddr(0, &addr) < 0)
    return -1;

  struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
  } kuts;

  safestrcpy(kuts.sysname, "xv6-k210", sizeof(kuts.sysname));
  safestrcpy(kuts.nodename, "oslab", sizeof(kuts.nodename));
  safestrcpy(kuts.release, "0.1", sizeof(kuts.release));
  safestrcpy(kuts.version, "2024", sizeof(kuts.version));
  safestrcpy(kuts.machine, "riscv64", sizeof(kuts.machine));

  if (copyout2(addr, (char*)&kuts, sizeof(kuts)) < 0)
    return -1;

  return 0;
}

// 获取父进程的 PID
uint64
sys_getppid(void)
{
  struct proc *p = myproc();
  uint64 ppid;
  
  // xv6 中的 proc 结构体有 parent 指针
  if(p->parent){
    ppid = p->parent->pid;
  } else {
    // 如果没有父进程（比如 init 进程），通常返回 0 或 1
    ppid = 0; 
  }
  return ppid;
}

// 主动让出 CPU
uint64
sys_yield(void)
{
  // xv6 在 trap.c 内部有时钟中断时就会调用 yield()
  // 直接封装成系统调用暴露给用户态
  yield();
  return 0;
}

// 获取系统时间 (对应 syscall 169)
uint64
sys_gettimeofday(void)
{
  uint64 addr;
  if (argaddr(0, &addr) < 0)
    return -1;

  // Linux 标准的 timeval 结构体 (RISC-V 64 下通常是 64 位整数)
  struct timeval {
    uint64 tv_sec;
    uint64 tv_usec;
  } tv;

  // 读取 RISC-V 的 time 寄存器
  uint64 time = r_time(); 
  
  // 按照 10MHz 的频率转换为秒和微秒
  tv.tv_sec = time / 10000000;
  tv.tv_usec = (time % 10000000) / 10;

  // 将结果拷贝回用户态提供的指针地址中
  if (copyout2(addr, (char*)&tv, sizeof(tv)) < 0)
    return -1;

  return 0;
}

// 进程休眠 (对应 syscall 101)
uint64
sys_nanosleep(void)
{
  uint64 addr_req;
  // nanosleep 第一个参数是请求休眠的时间结构体指针
  if (argaddr(0, &addr_req) < 0)
    return -1;

  struct timespec {
    uint64 tv_sec;
    uint64 tv_nsec;
  } req;

  if (copyin2((char*)&req, addr_req, sizeof(req)) < 0)
    return -1;

  // xv6 的时钟中断(tick)通常是每秒 10 次(取决于 timer.c 配置)
  // 将秒转换为 tick 数，+1 是为了确保至少休眠那么久
  int ticks_to_sleep = req.tv_sec * 10 + 1; 

  uint ticks0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < ticks_to_sleep){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    // 复用 xv6 底层的进程睡眠机制
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  
  return 0;
}

uint64
sys_clone(void)
{
  uint64 flags, stack;
  // clone 的前两个参数是 flags 和 stack
  if(argaddr(0, &flags) < 0 || argaddr(1, &stack) < 0)
    return -1;
  return clone(stack);
}

uint64
sys_wait4(void)
{
  int pid, options;
  uint64 status, rusage;
  // wait4 的参数依次为: pid, status, options, rusage
  if(argint(0, &pid) < 0 || argaddr(1, &status) < 0 || 
     argint(2, &options) < 0 || argaddr(3, &rusage) < 0)
    return -1;
  return wait4(pid, status);
}

uint64
sys_brk(void)
{
  uint64 addr;
  if(argaddr(0, &addr) < 0)
    return -1;

  struct proc *p = myproc();
  uint64 old_sz = p->sz; // 当前的堆顶位置

  // 1. 如果传入 0，直接返回当前堆顶 (测试用例常用的探测方式)
  if (addr == 0) {
    return old_sz;
  }

  // 2. 如果新地址 > 老地址，说明要扩大堆
  if (addr > old_sz) {
    // 调用 xv6 底层扩展内存的函数，参数是增加的字节数
    if (growproc(addr - old_sz) < 0) {
      return old_sz; // Linux 语义：失败时返回旧边界，不返回 -1
    }
  } 
  // 3. 如果新地址 < 老地址，说明要收缩堆（释放内存）
  else if (addr < old_sz) {
    // growproc 传负数就是收缩内存
    if (growproc(addr - old_sz) < 0) {
      return old_sz;
    }
  }

  return p->sz; // 返回新的堆顶地址
}