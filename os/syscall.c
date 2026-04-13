#include "syscall.h"
#include "console.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"

static int port_to_pte_perm(int port)
{
    int perm = PTE_U;

    if (port & 0x1) perm |= PTE_R;
    if (port & 0x2) perm |= PTE_W;
    if (port & 0x4) perm |= PTE_X;

    return perm;
}

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d str = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

uint64 sys_read(int fd, uint64 va, uint64 len)
{
	debugf("sys_read fd = %d str = %x, len = %d", fd, va, len);
	if (fd != STDIN)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	for (int i = 0; i < len; ++i) {
		int c = consgetc();
		str[i] = c;
	}
	copyout(p->pagetable, va, str, len);
	return len;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(uint64 val, int _tz)
{
	struct proc *p = curr_proc();
	uint64 cycle = get_cycle();
	TimeVal t;
	t.sec = cycle / CPU_FREQ;
	t.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	copyout(p->pagetable, val, (char *)&t, sizeof(TimeVal));
	return 0;
}

uint64 sys_getpid()
{
	return curr_proc()->pid;
}

uint64 sys_getppid()
{
	struct proc *p = curr_proc();
	return p->parent == NULL ? IDLE_PID : p->parent->pid;
}

uint64 sys_clone()
{
	debugf("fork!\n");
	return fork();
}

uint64 sys_exec(uint64 va)
{
	struct proc *p = curr_proc();
	char name[200];
	copyinstr(p->pagetable, name, va, 200);
	debugf("sys_exec %s\n", name);
	return exec(name);
}

uint64 sys_wait(int pid, uint64 va)
{
	struct proc *p = curr_proc();
	int *code = (int *)useraddr(p->pagetable, va);
	return wait(pid, code);
}

uint64 sys_spawn(uint64 va)
{
	// TODO: your job is to complete the sys call
    //ADDED: completed syscall
    struct proc *p = curr_proc();
    char name[200];

    // Copy the program name string from user space into a kernel bufer
    if (copyinstr(p->pagetable, name, va, 200) < 0)
        return -1;

    // Create a fresh child process and directly load the named program
    return spawn(name);
}

uint64 sys_set_priority(long long prio){
    // TODO: your job is to complete the sys call
    //ADDED: Call to setpriority
    return set_priority(prio);
}

uint64 sys_mmap(void *start, uint64 len, int port, int flag, int fd)
{
    (void)flag;
    (void)fd;

    struct proc *p = curr_proc();
    uint64 va = (uint64)start;

    if (len == 0)
        return 0;

    // must be page aligned
    if (va % PGSIZE != 0)
        return -1;

    // upper limit 1 GiB
    if (len > (1UL << 30))
        return -1;

    // valid port bits only: low 3 bits, and not all zero
    if ((port & ~0x7) != 0)
        return -1;
    if ((port & 0x7) == 0)
        return -1;

    uint64 end = PGROUNDUP(va + len);
    int perm = port_to_pte_perm(port);

    // first pass: ensure every page is unmapped
    for (uint64 a = va; a < end; a += PGSIZE) {
        if (walkaddr(p->pagetable, a) != 0) {
            return -1;
        }
    }

    // second pass: allocate and map one page at a time
    for (uint64 a = va; a < end; a += PGSIZE) {
        void *pa = kalloc();
        if (pa == 0) {
            return -1;
        }
        memset(pa, 0, PGSIZE);

        if (mappages(p->pagetable, a, PGSIZE, (uint64)pa, perm) != 0) {
            kfree(pa);
            return -1;
        }
    }

    return 0;
}

uint64 sys_munmap(void *start, uint64 len)
{
    struct proc *p = curr_proc();
    uint64 va = (uint64)start;

    if (len == 0)
        return 0;

    if (va % PGSIZE != 0)
        return -1;

    uint64 end = PGROUNDUP(va + len);

    // verify whole interval is mapped first
    for (uint64 a = va; a < end; a += PGSIZE) {
        if (walkaddr(p->pagetable, a) == 0) {
            return -1;
        }
    }

    uvmunmap(p->pagetable, va, (end - va) / PGSIZE, 1);
    return 0;
}

extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_read:
		ret = sys_read(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday(args[0], args[1]);
		break;
	case SYS_mmap:
        ret = sys_mmap((void *)args[0], args[1], (int)args[2], (int)args[3], (int)args[4]);
        break;
    case SYS_munmap:
        ret = sys_munmap((void *)args[0], args[1]);
        break;
	case SYS_getpid:
		ret = sys_getpid();
		break;
	case SYS_getppid:
		ret = sys_getppid();
		break;
	case SYS_clone: // SYS_fork
		ret = sys_clone();
		break;
	case SYS_execve:
		ret = sys_exec(args[0]);
		break;
	case SYS_wait4:
		ret = sys_wait(args[0], args[1]);
		break;
	case SYS_spawn:
		ret = sys_spawn(args[0]);
		break;
    case SYS_setpriority:
        // ADDED: Handler for priority-setting syscall
        // args[0] =  the requested priority value
        ret = sys_set_priority(args[0]);
        break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
