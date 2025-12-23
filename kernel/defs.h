struct buf;
struct context;
struct file;
struct inode;
struct pipe;
struct proc;
struct spinlock;
struct sleeplock;
struct stat;
struct superblock;

// bio.c
void            binit(void);
struct buf*     bread(uint, uint);
void            brelse(struct buf*);
void            bwrite(struct buf*);
void            bpin(struct buf*);
void            bunpin(struct buf*);

// console.c
void            consoleinit(void);
void            consoleintr(int);
void            consputc(int);
int             consoleread(int, uint64, int, int);
int             consolewrite(int, uint64, int, int);

// exec.c
int             exec(char*, char**);

// file.c
struct file*    filealloc(void);
void            fileclose(struct file*);
struct file*    filedup(struct file*);
void            fileinit(void);
int             fileread(struct file*, uint64, int n);
int             filestat(struct file*, uint64 addr);
int             filewrite(struct file*, uint64, int n);

// fs.c
void            fsinit(int);
int             dirlink(struct inode*, char*, uint, uint, char*);
struct inode*   dirlookup(struct inode*, char*, uint*);
struct inode*   ialloc(uint, short);
struct inode*   idup(struct inode*);
void            iinit();
void            ilock(struct inode*);
void            iput(struct inode*);
void            iunlock(struct inode*);
void            iunlockput(struct inode*);
void            iupdate(struct inode*);
int             namecmp(const char*, const char*);
struct inode*   namei(char*);
struct inode*   iget(uint dev, uint inum);
struct inode*   inamex(char *, int, struct inode *);
struct inode*   nameiparent(char*, char*);
int             readi(struct inode*, int, uint64, uint, uint);
void            stati(struct inode*, struct stat*);
int             writei(struct inode*, int, uint64, uint, uint);
void            itrunc(struct inode*);

// ramdisk.c
void            ramdiskinit(void);
void            ramdiskintr(void);
void            ramdiskrw(struct buf*);

// kalloc.c
void*           kalloc(void);
void            kfree(void *);
void            kref(void *);
void            kinit(void);

// log.c
void            initlog(int, struct superblock*);
void            log_write(struct buf*);
void            begin_op(void);
void            end_op(void);

// pipe.c
int             pipealloc(struct file**, struct file**);
void            pipeclose(struct pipe*, int);
int             piperead(struct pipe*, uint64, int);
int             pipewrite(struct pipe*, uint64, int);
int             pipereadavail(struct pipe*);

// file.c
struct file*    filealloc(void);
void            fileclose(struct file*);
int             filestat(struct file*, uint64 addr);
int             fileread(struct file*, uint64, int n);
int             filewrite(struct file*, uint64, int n);
int             filereadavail(struct file*);
struct file*    filedup(struct file*);

// ipc.c
int             ipc_send(int, char*, int);
int             ipc_recv(int*, char*, int);

// gwin.c
// void            gwininit(void);
// int             gwinwrite(int, uint64, int, int);
// int             gwinread(int, uint64, int, int);

// suluctl.c
void            suluctlinit(void);
int             suluctlread(int, uint64, int, int);
int             suluctlwrite(int, uint64, int, int);
int             suluctl_poll(void);  // For Sulu to poll for commands


void            virtio_gpu_init(void);

// printf.c
int             printf(char*, ...) __attribute__ ((format (printf, 1, 2)));
int             inputread(int, uint64, int, int);
int             inputreadavail(void);
void            panic(char*) __attribute__((noreturn));
void            printfinit(void);

#define BACKSPACE 0x100
#define ESCAPE    0x1B
#define SHIFT     0x00
#define LEFT      0x44
#define RIGHT     0x43
#define UP        0x41
#define DOWN      0x42
#define PARENTH_O 0x5B

#define CLEAR_SCREEN()  printf("\033[2J")
#define GOTO_XY(x,y)    printf("\033[%d;%dH", (y), (x))
#define MOVE_UP(x)      printf("\033[%dA", (x)) // Move up X lines;
#define MOVE_DOWN(x)    printf("\033[%dB", (x)) // Move down X lines;
#define MOVE_RIGHT(x)   printf("\033[%dC", (x)) // Move right X column;
#define MOVE_LEFT(x)    printf("\033[%dD", (x)) // Move left X column;

// proc.c
int             cpuid(void);
void            exit(int);
int             fork(void);
int             growproc(int);
void            proc_mapstacks(pagetable_t);
pagetable_t     proc_pagetable(struct proc *);
void            proc_freepagetable(pagetable_t, uint64);
void            shminit(void);
int             shmget(int, int);
void*           shmat(int, void*);
void            shminit(void);
int             kill(int);
int             killed(struct proc*);
void            setkilled(struct proc*);
struct cpu*     mycpu(void);
struct cpu*     getmycpu(void);
struct proc*    myproc();
void            procinit(void);
void            scheduler(void) __attribute__((noreturn));
void            sched(void);
void            sleep(void*, struct spinlock*);
void            userinit(void);
int             wait(uint64);
void            wakeup(void*);
void            yield(void);
int             either_copyout(int user_dst, uint64 dst, void *src, uint64 len);
int             either_copyin(void *dst, int user_src, uint64 src, uint64 len);
void            procdump(void);

// swtch.S
void            swtch(struct context*, struct context*);

// spinlock.c
void            acquire(struct spinlock*);
int             holding(struct spinlock*);
void            initlock(struct spinlock*, char*);
void            release(struct spinlock*);
void            push_off(void);
void            pop_off(void);

// sleeplock.c
void            acquiresleep(struct sleeplock*);
void            releasesleep(struct sleeplock*);
int             holdingsleep(struct sleeplock*);
void            initsleeplock(struct sleeplock*, char*);

// string.c
int             memcmp(const void*, const void*, uint);
void*           memmove(void*, const void*, uint);
void*           memset(void*, int, uint);
char*           safestrcpy(char*, const char*, int);
int             strlen(const char*);
int             strncmp(const char*, const char*, uint);
char*           strncpy(char*, const char*, int);
char *          strcat(char *, const char *);
void            strprep(char *, const char * );
void            sprintf( char * dest, const char *fmt, ...);
long            strtol(const char *, char **, register int );

// syscall.c
void            argint(int, int*);
int             argstr(int, char*, int);
void            argaddr(int, uint64 *);
int             fetchstr(uint64, char*, int);
int             fetchaddr(uint64, uint64*);
void            syscall();

// trap.c
extern uint     ticks;
void            trapinit(void);
void            trapinithart(void);
extern struct spinlock tickslock;
void            usertrapret(void);

// uart.c
void            uartinit(void);
void            uartintr(void);
void            uartputc(int);
void            uartputc_sync(int);
int             uartgetc(void);

// vm.c
void            kvminit(void);
void            kvminithart(void);
void            kvmmap(pagetable_t, uint64, uint64, uint64, int);
int             mappages(pagetable_t, uint64, uint64, uint64, int);
pagetable_t     uvmcreate(void);
void            uvmfirst(pagetable_t, uchar *, uint);
uint64          uvmalloc(pagetable_t, uint64, uint64, int);
uint64          uvmdealloc(pagetable_t, uint64, uint64);
int             uvmcopy(pagetable_t, pagetable_t, uint64);
void            uvmfree(pagetable_t, uint64);
void            uvmunmap(pagetable_t, uint64, uint64, int);
void            uvmclear(pagetable_t, uint64);
pte_t *         walk(pagetable_t, uint64, int);
uint64          walkaddr(pagetable_t, uint64);
int             copyout(pagetable_t, uint64, void *, uint64);
int             copyin(pagetable_t, void *, uint64, uint64);
int             copyinstr(pagetable_t, char *, uint64, uint64);

// plic.c
void            plicinit(void);
void            plicinithart(void);
int             plic_claim(void);
void            plic_complete(int);

// virtio_disk.c
void            virtio_disk_init(void);
void            virtio_disk_rw(struct buf *, int);
void            virtio_disk_intr(void);

// virtio_gpu.c
void            virtio_gpu_init(void);
void            virtio_gpu_transfer(uint32 x, uint32 y, uint32 w, uint32 h);
void            virtio_gpu_flush(uint32 x, uint32 y, uint32 w, uint32 h);
void            virtio_gpu_cursor_move(uint32 x, uint32 y);
extern int      gui_active;
extern uint32   framebuffer[];

// virtio_input.c
void            virtio_input_init(void);
void            virtio_input_intr(void);
extern int      mouse_x, mouse_y, mouse_btn, mouse_scroll;

#define NELEM(x) (sizeof(x)/sizeof((x)[0]))
