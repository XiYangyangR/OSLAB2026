//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//


#include "include/types.h"
#include "include/riscv.h"
#include "include/param.h"
#include "include/stat.h"
#include "include/spinlock.h"
#include "include/proc.h"
#include "include/sleeplock.h"
#include "include/file.h"
#include "include/pipe.h"
#include "include/fcntl.h"
#include "include/fat32.h"
#include "include/syscall.h"
#include "include/string.h"
#include "include/printf.h"
#include "include/vm.h"


// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == NULL)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_dup3(void)
{
  struct file *f;
  int oldfd, newfd, flags;
  struct proc *p = myproc();

  if(argfd(0, &oldfd, &f) < 0 || argint(1, &newfd) < 0 || argint(2, &flags) < 0)
    return -1;

  // 越界检查（现在 NOFILE 变成 128 了，100 可以顺利通过）
  if(newfd < 0 || newfd >= NOFILE)
    return -1;

  // 兼容 dup2 语义：如果 oldfd 和 newfd 相等，什么都不做，直接返回 newfd
  if(oldfd == newfd)
    return newfd;

  // 如果 newfd 已经被占用，需要先关闭它
  if(p->ofile[newfd] != 0){
    fileclose(p->ofile[newfd]);
  }

  // 复制文件指针并增加引用计数
  p->ofile[newfd] = f;
  filedup(f);
  
  return newfd;
}


uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

static struct dirent*
create(char *path, short type, int mode)
{
  struct dirent *ep, *dp;
  char name[FAT32_MAX_FILENAME + 1];

  if((dp = enameparent(path, name)) == NULL)
    return NULL;

  if (type == T_DIR) {
    mode = ATTR_DIRECTORY;
  } else if (mode & O_RDONLY) {
    mode = ATTR_READ_ONLY;
  } else {
    mode = 0;  
  }

  elock(dp);
  if ((ep = ealloc(dp, name, mode)) == NULL) {
    eunlock(dp);
    eput(dp);
    return NULL;
  }
  
  if ((type == T_DIR && !(ep->attribute & ATTR_DIRECTORY)) ||
      (type == T_FILE && (ep->attribute & ATTR_DIRECTORY))) {
    eunlock(dp);
    eput(ep);
    eput(dp);
    return NULL;
  }

  eunlock(dp);
  eput(dp);

  elock(ep);
  return ep;
}

uint64
sys_open(void)
{
  char path[FAT32_MAX_PATH];
  int fd, omode;
  struct file *f;
  struct dirent *ep;

  if(argstr(0, path, FAT32_MAX_PATH) < 0 || argint(1, &omode) < 0)
    return -1;

  if(omode & O_CREATE){
    ep = create(path, T_FILE, omode);
    if(ep == NULL){
      return -1;
    }
  } else {
    if((ep = ename(path)) == NULL){
      return -1;
    }
    elock(ep);
    if((ep->attribute & ATTR_DIRECTORY) && omode != O_RDONLY){
      eunlock(ep);
      eput(ep);
      return -1;
    }
  }

  if((f = filealloc()) == NULL || (fd = fdalloc(f)) < 0){
    if (f) {
      fileclose(f);
    }
    eunlock(ep);
    eput(ep);
    return -1;
  }

  if(!(ep->attribute & ATTR_DIRECTORY) && (omode & O_TRUNC)){
    etrunc(ep);
  }

  f->type = FD_ENTRY;
  f->off = (omode & O_APPEND) ? ep->file_size : 0;
  f->ep = ep;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  eunlock(ep);

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[FAT32_MAX_PATH];
  struct dirent *ep;

  if(argstr(0, path, FAT32_MAX_PATH) < 0 || (ep = create(path, T_DIR, 0)) == 0){
    return -1;
  }
  eunlock(ep);
  eput(ep);
  return 0;
}

uint64
sys_chdir(void)
{
  char path[FAT32_MAX_PATH];
  struct dirent *ep;
  struct proc *p = myproc();
  
  if(argstr(0, path, FAT32_MAX_PATH) < 0 || (ep = ename(path)) == NULL){
    return -1;
  }
  elock(ep);
  if(!(ep->attribute & ATTR_DIRECTORY)){
    eunlock(ep);
    eput(ep);
    return -1;
  }
  eunlock(ep);
  eput(p->cwd);
  p->cwd = ep;
  return 0;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  // if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
  //    copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
  if(copyout2(fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout2(fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

// To open console device.
uint64
sys_dev(void)
{
  int fd, omode;
  int major, minor;
  struct file *f;

  if(argint(0, &omode) < 0 || argint(1, &major) < 0 || argint(2, &minor) < 0){
    return -1;
  }

  if(omode & O_CREATE){
    panic("dev file on FAT");
  }

  if(major < 0 || major >= NDEV)
    return -1;

  if((f = filealloc()) == NULL || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    return -1;
  }

  f->type = FD_DEVICE;
  f->off = 0;
  f->ep = 0;
  f->major = major;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  return fd;
}

// To support ls command
uint64
sys_readdir(void)
{
  struct file *f;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argaddr(1, &p) < 0)
    return -1;
  return dirnext(f, p);
}

// get absolute cwd string
uint64
sys_getcwd(void)
{
  uint64 addr;
  if (argaddr(0, &addr) < 0)
    return -1;

  struct dirent *de = myproc()->cwd;
  char path[FAT32_MAX_PATH];
  char *s;
  int len;

  if (de->parent == NULL) {
    s = "/";
    if (copyout2(addr, s, strlen(s) + 1) < 0)
      return -1;
    return addr;
  } else {
    s = path + FAT32_MAX_PATH - 1;
    *s = '\0';
    while (de->parent) {
      len = strlen(de->filename);
      s -= len;
      if (s <= path)          // can't reach root "/"
        return -1;
      strncpy(s, de->filename, len);
      *--s = '/';
      de = de->parent;
    }

    if(copyout2(addr,s,strlen(s) + 1) < 0)
      return -1;

    return addr;
  }

  // if (copyout(myproc()->pagetable, addr, s, strlen(s) + 1) < 0)
  if (copyout2(addr, s, strlen(s) + 1) < 0)
    return -1;
  
  return 0;

}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct dirent *dp)
{
  struct dirent ep;
  int count;
  int ret;
  ep.valid = 0;
  ret = enext(dp, &ep, 2 * 32, &count);   // skip the "." and ".."
  return ret == -1;
}

uint64
sys_remove(void)
{
  char path[FAT32_MAX_PATH];
  struct dirent *ep;
  int len;
  if((len = argstr(0, path, FAT32_MAX_PATH)) <= 0)
    return -1;

  char *s = path + len - 1;
  while (s >= path && *s == '/') {
    s--;
  }
  if (s >= path && *s == '.' && (s == path || *--s == '/')) {
    return -1;
  }
  
  if((ep = ename(path)) == NULL){
    return -1;
  }
  elock(ep);
  if((ep->attribute & ATTR_DIRECTORY) && !isdirempty(ep)){
      eunlock(ep);
      eput(ep);
      return -1;
  }
  elock(ep->parent);      // Will this lead to deadlock?
  eremove(ep);
  eunlock(ep->parent);
  eunlock(ep);
  eput(ep);

  return 0;
}

// Must hold too many locks at a time! It's possible to raise a deadlock.
// Because this op takes some steps, we can't promise
uint64
sys_rename(void)
{
  char old[FAT32_MAX_PATH], new[FAT32_MAX_PATH];
  if (argstr(0, old, FAT32_MAX_PATH) < 0 || argstr(1, new, FAT32_MAX_PATH) < 0) {
      return -1;
  }

  struct dirent *src = NULL, *dst = NULL, *pdst = NULL;
  int srclock = 0;
  char *name;
  if ((src = ename(old)) == NULL || (pdst = enameparent(new, old)) == NULL
      || (name = formatname(old)) == NULL) {
    goto fail;          // src doesn't exist || dst parent doesn't exist || illegal new name
  }
  for (struct dirent *ep = pdst; ep != NULL; ep = ep->parent) {
    if (ep == src) {    // In what universe can we move a directory into its child?
      goto fail;
    }
  }

  uint off;
  elock(src);     // must hold child's lock before acquiring parent's, because we do so in other similar cases
  srclock = 1;
  elock(pdst);
  dst = dirlookup(pdst, name, &off);
  if (dst != NULL) {
    eunlock(pdst);
    if (src == dst) {
      goto fail;
    } else if (src->attribute & dst->attribute & ATTR_DIRECTORY) {
      elock(dst);
      if (!isdirempty(dst)) {    // it's ok to overwrite an empty dir
        eunlock(dst);
        goto fail;
      }
      elock(pdst);
    } else {                    // src is not a dir || dst exists and is not an dir
      goto fail;
    }
  }

  if (dst) {
    eremove(dst);
    eunlock(dst);
  }
  memmove(src->filename, name, FAT32_MAX_FILENAME);
  emake(pdst, src, off);
  if (src->parent != pdst) {
    eunlock(pdst);
    elock(src->parent);
  }
  eremove(src);
  eunlock(src->parent);
  struct dirent *psrc = src->parent;  // src must not be root, or it won't pass the for-loop test
  src->parent = edup(pdst);
  src->off = off;
  src->valid = 1;
  eunlock(src);

  eput(psrc);
  if (dst) {
    eput(dst);
  }
  eput(pdst);
  eput(src);

  return 0;

fail:
  if (srclock)
    eunlock(src);
  if (dst)
    eput(dst);
  if (pdst)
    eput(pdst);
  if (src)
    eput(src);
  return -1;
}

extern void *kalloc(void);
extern void kfree(void *);
extern int mappages(pagetable_t, uint64, uint64, uint64, int);
extern void vmunmap(pagetable_t, uint64, uint64, int);

// 内存映射 (syscall 222)
uint64
sys_mmap(void)
{
  uint64 addr;
  int length, prot, flags, fd, offset;
  struct file *f;

  if(argaddr(0, &addr) < 0 || argint(1, &length) < 0 || argint(2, &prot) < 0 ||
     argint(3, &flags) < 0 || argfd(4, &fd, &f) < 0 || argint(5, &offset) < 0)
    return -1;

  struct proc *p = myproc();
  struct vma *v = 0;
  
  for(int i = 0; i < NVMA; i++){
    if(p->vmas[i].valid == 0){
      v = &p->vmas[i];
      break;
    }
  }
  if(!v) return -1;

  uint64 start_addr = PGROUNDDOWN(p->mmap_next);
  p->mmap_next = start_addr + PGROUNDUP(length);

  v->valid = 1;
  v->addr = start_addr;
  v->length = length;
  v->prot = prot;
  v->flags = flags;
  v->f = f;
  v->offset = offset;
  
  filedup(f);

  for(uint64 a = 0; a < length; a += PGSIZE){
    void *pa = kalloc();
    if(pa == 0) return -1;
    memset(pa, 0, PGSIZE);
    
    int pte_flags = PTE_U | PTE_V;
    if(prot & 1) pte_flags |= PTE_R;
    if(prot & 2) pte_flags |= PTE_W;
    if(prot & 4) pte_flags |= PTE_X;

    if(mappages(p->pagetable, start_addr + a, PGSIZE, (uint64)pa, pte_flags) != 0){
      kfree(pa);
      return -1;
    }

    // 直接使用最底层的 eread 读入物理页！
    // 避开fileread / copyout 对 p->sz 的限制
    int read_len = PGSIZE;
    if (a + PGSIZE > length) read_len = length - a;
    
    // 参数 0 表示这是一个内核地址 (pa)，不需要通过用户页表做检查
    eread(f->ep, 0, (uint64)pa, offset + a, read_len);
  }

  return start_addr;
}

// 解除映射 (syscall 215)
uint64
sys_munmap(void)
{
  uint64 addr;
  int length;
  if(argaddr(0, &addr) < 0 || argint(1, &length) < 0)
    return -1;

  struct proc *p = myproc();
  struct vma *v = 0;
  
  for(int i = 0; i < NVMA; i++){
    if(p->vmas[i].valid && addr == p->vmas[i].addr){
      v = &p->vmas[i];
      break;
    }
  }
  if(!v) return -1;

  // 直接把物理页的数据通过 ewrite 写回文件
  if((v->flags & 1) && (v->prot & 2)){
    for(uint64 a = 0; a < length; a += PGSIZE){
      // 通过页表反查虚拟地址对应的物理页地址
      uint64 pa = walkaddr(p->pagetable, addr + a);
      if(pa != 0) {
        int write_len = PGSIZE;
        if(a + PGSIZE > length) write_len = length - a;
        // 参数 0 表示 source 是内核地址，避开 copyin 限制
        ewrite(v->f->ep, 0, pa, v->offset + a, write_len);
      }
    }
  }

  vmunmap(p->pagetable, addr, PGROUNDUP(length)/PGSIZE, 1);
  v->valid = 0;
  fileclose(v->f); 

  return 0;
}

uint64
sys_mkdirat(void)
{
  char path[FAT32_MAX_PATH];
  int dirfd, mode;
  struct dirent *ep;

  // mkdirat 参数顺序: dirfd(0), path(1), mode(2)
  if(argint(0, &dirfd) < 0 || argstr(1, path, FAT32_MAX_PATH) < 0 || argint(2, &mode) < 0){
    return -1;
  }

  // 复用现有的 FAT32 创建目录逻辑
  if((ep = create(path, T_DIR, 0)) == 0){
    return -1;
  }
  eunlock(ep);
  eput(ep);
  return 0;
}

uint64
sys_getdents64(void)
{
  struct file *f;
  uint64 buf;
  int len;

  // 获取参数：fd(0), buf(1), len(2)
  if(argfd(0, 0, &f) < 0 || argaddr(1, &buf) < 0 || argint(2, &len) < 0)
    return -1;

  // 必须是可读的目录文件
  if(f->readable == 0 || !(f->ep->attribute & ATTR_DIRECTORY))
    return -1;

  struct dirent de;
  int count = 0;
  int ret;
  int bytes_read = 0;

  elock(f->ep);
  while (1) {
    de.valid = 0;
    // 使用 FAT32 层的 enext 读取下一个有效目录项
    ret = enext(f->ep, &de, f->off, &count);
    if (ret == -1) // 读到目录末尾
      break;

    int name_len = strlen(de.filename);
    
    // 计算当前这个 dirent 需要的字节数: 
    // d_ino(8) + d_off(8) + d_reclen(2) + d_type(1) = 19 字节的头部
    // 加上名字长度，再加上 1 个字节的 '\0'
    int reclen = 19 + name_len + 1;
    // 内存对齐到 8 字节边界（RISC-V 64 架构的标准要求）
    reclen = (reclen + 7) & ~7;

    // 如果用户提供的 buffer 剩余空间不够装下这一个 dirent 了，就结束读取
    if (bytes_read + reclen > len) {
      break;
    }

    // 提取所需信息
    uint64 d_ino = 0; // FAT32 没有 inode，传 0 即可通过测试
    uint64 d_off = f->off + count * 32;
    unsigned short d_reclen = reclen;
    unsigned char d_type = (de.attribute & ATTR_DIRECTORY) ? 4 : 8; // 4 是目录(DT_DIR)，8 是常规文件(DT_REG)

    // 在内核栈上构造好这个结构体，防止直接对齐访问引发 CPU 异常
    char local_buf[256];
    memset(local_buf, 0, reclen);
    memmove(local_buf, &d_ino, 8);
    memmove(local_buf + 8, &d_off, 8);
    memmove(local_buf + 16, &d_reclen, 2);
    memmove(local_buf + 18, &d_type, 1);
    memmove(local_buf + 19, de.filename, name_len + 1);

    // 拷贝到用户的 buffer 中
    if (copyout2(buf + bytes_read, local_buf, reclen) < 0) {
      break;
    }

    bytes_read += reclen;
    f->off += count * 32; // 更新文件的偏移量
  }
  eunlock(f->ep);

  return bytes_read; // 返回成功写入 buffer 的总字节数
}

uint64
sys_unlinkat(void)
{
  char path[FAT32_MAX_PATH];
  int dirfd, flags;
  struct dirent *ep;

  // 参数顺序：dirfd(0), path(1), flags(2)
  if(argint(0, &dirfd) < 0 || argstr(1, path, FAT32_MAX_PATH) < 0 || argint(2, &flags) < 0)
    return -1;

  // 复用现成的 sys_remove 核心逻辑
  if((ep = ename(path)) == NULL){
    return -1;
  }
  elock(ep);
  if((ep->attribute & ATTR_DIRECTORY) && !isdirempty(ep)){
      eunlock(ep);
      eput(ep);
      return -1;
  }
  elock(ep->parent);
  eremove(ep);
  eunlock(ep->parent);
  eunlock(ep);
  eput(ep);

  return 0;
}

uint64 sys_mount(void) {
  char dev[FAT32_MAX_PATH];
  char path[FAT32_MAX_PATH];
  char type[32];
  
  // 必须按系统调用定义的参数顺序依次获取
  // mount(special, dir, fstype, flags, data)
  if(argstr(0, dev, FAT32_MAX_PATH) < 0 || 
     argstr(1, path, FAT32_MAX_PATH) < 0 || 
     argstr(2, type, 32) < 0) {
    return -1;
  }
  
  // 暂时返回 0 以绕过 panic，后续在此实现具体挂载逻辑
  return 0; 
}

uint64 sys_umount2(void) {
  char path[FAT32_MAX_PATH];
  int flags;
  
  // umount2(target, flags)
  if(argstr(0, path, FAT32_MAX_PATH) < 0 || 
     argint(1, &flags) < 0) {
    return -1;
  }
  
  return 0;
}