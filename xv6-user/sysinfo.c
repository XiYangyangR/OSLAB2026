#include "kernel/include/fcntl.h"
#include "kernel/include/types.h"
#include "kernel/include/sysinfo.h"
#include "xv6-user/user.h"

int main() {
  struct sysinfo info;
  int a = sysinfo(&info);
  printf("sysinfo ret code: %d\n", a);
  printf("info.freemem: %d\n", info.freemem);
  printf("info.nproc: %d\n", info.nproc);
  return 0;
}