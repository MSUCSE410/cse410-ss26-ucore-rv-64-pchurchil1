#include "syscall.h"
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
	debugf("sys_write fd = %d va = %x, len = %d", fd, va, len);
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

uint64 sys_gettimeofday(TimeVal *val, int _tz) // TODO: implement sys_gettimeofday in pagetable. (VA to PA)
{

	if (val == 0)
        return -1;

	struct proc *p = curr_proc();
    uint64 pa = useraddr(p->pagetable, (uint64)val);
    if (pa == 0)
        return -1;

    TimeVal *kval = (TimeVal *)pa;
    uint64 cycle = get_cycle();
    kval->sec = cycle / CPU_FREQ;
    kval->usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	/* The code in `ch3` will leads to memory bugs*/

	// uint64 cycle = get_cycle();
	// val->sec = cycle / CPU_FREQ;
	// val->usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	return 0;
}

// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)
/*
* LAB1: you may need to define sys_task_info here
*/
uint64 sys_task_info(struct TaskInfo *task)
{
    if (task == 0)
        return -1;

    struct proc *p = curr_proc();
    uint64 pa = useraddr(p->pagetable, (uint64)task);
    if (pa == 0)
        return -1;

    struct TaskInfo *ktask = (struct TaskInfo *)pa;

    ktask->status = Running;

    for (int i = 0; i < MAX_SYSCALL_NUM; i++) {
        ktask->syscall_times[i] = p->syscall_times[i];
    }

    if (p->start_time == 0) {
        ktask->time = 0;
    } else {
        ktask->time = (int)((get_cycle() - p->start_time) / (CPU_FREQ / 1000));
    }

    return 0;
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
	struct proc *p = curr_proc();
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);

	if (id >= 0 && id < MAX_SYSCALL_NUM) {
    p->syscall_times[id]++;
	}
	/*
	* LAB1: you may need to update syscall counter for task info here
	*/
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday((TimeVal *)args[0], args[1]);
		break;
	/*
	* LAB1: you may need to add SYS_taskinfo case here
	*/
	case SYS_task_info:
        ret = sys_task_info((struct TaskInfo *)args[0]);
        break;
    case SYS_mmap:
        ret = sys_mmap((void *)args[0], args[1], (int)args[2], (int)args[3], (int)args[4]);
        break;
    case SYS_munmap:
        ret = sys_munmap((void *)args[0], args[1]);
        break;
	case SYS_getpid:
        ret = p->pid;
        break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
