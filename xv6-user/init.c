// init: The initial user-level program

#include "kernel/include/types.h"
#include "kernel/include/stat.h"
#include "kernel/include/file.h"
#include "kernel/include/fcntl.h"
#include "xv6-user/user.h"          //用户态的头文件，包含了用户态的系统调用接口 exec, fork, wait, exit 等函数的声明

char *argv[] = { "sh", 0 };
char *test[]={"getcwd", "write", "getpid", "times", "uname","getppid","yield","gettimeofday","sleep","fork","clone","wait","waitpid","execve","exit","brk","mmap","munmap",
              "dup","dup2","open","openat","close","read","mkdir_","chdir","getdents",0};

int
main(void)
{
  int pid, wpid;

  // if(open("console", O_RDWR) < 0){
  //   mknod("console", CONSOLE, 0);
  //   open("console", O_RDWR);
  // }
  dev(O_RDWR, CONSOLE, 0);
  dup(0);  // stdout
  dup(0);  // stderr

  for(int i=0;test[i];i++){
    printf("init: starting sh\n");
    pid = fork();
    if(pid < 0){
      printf("init: fork %s failed\n",test[i]);
      continue;
    }
    if(pid == 0){
      char *argv[]={ test[i],0};
      exec(test[i], argv);                  //原本的 exec("shell",argv)  #拉起一个用户的进程
      printf("init: exec %s failed\n", test[i]);
      exit(1);
    }

    for(;;){
      // this call to wait() returns if the shell exits,
      // or if a parentless process exits.
      wpid = wait((int *) 0);
      if(wpid == pid){
        // the shell exited; restart it.
        break;
      } else if(wpid < 0){
        printf("init: wait returned an error\n");
        exit(1);
      } else {
        // it was a parentless process; do nothing.
      }
    }
  }
  shutdown();
  return 0;
}
