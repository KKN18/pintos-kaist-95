#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
/* Our Implementation */
#include "threads/synch.h"
// END
#ifdef VM
#include "vm/vm.h"
#endif

/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */
#define FD_MAX 100
/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */

	/* Our Implementation */
	int64_t wake_tick;
	int base_priority;
	struct lock *wait_on_lock;
	struct list donation_list;
	struct list_elem donation_elem;
	int nice;
	int recent_cpu;
	/* END */

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	// Renamed Implementation
	struct file *fd_table[100];
	int next_fd;
	struct file *prog_file;
	// END

	// 부모의 자식 리스트에 들어가는 원소입니다.
    struct list_elem child_elem;

    // 현재 스레드의 자식 스레드 리스트입니다. 오직 이 스레드만이
    // 이 리스트를 변경할 것이므로 동기화하지 않아도 됩니다.
    struct list child_list;

    // 종료 상태를 나타냅니다.
    int exit_status;

	// RYU Test
    // 증가: 이 스레드가 종료되려고 할 때
    // 감소: 이 스레드의 종료를 "대기"하기 위하여
    struct semaphore wait_sema;

    // 증가: 이 스레드 자료 구조를 제거해도 좋을 상황이 되었을 때
    // 감소: 이 스레드 자료 구조를 "제거"하기 직전
    struct semaphore destroy_sema;

    // 적재 성공 여부입니다.
    bool load_succeeded;

    // 증가: 성공 여부를 따지지 않고, 적재 작업이 완료되었을 때
    // 감소: 부모 프로세스의 exec에서 적재 완료 대기
    struct semaphore load_sema;

	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);
/* Our Implementation */
// Functions for mlfqs
int load_avg;
int ready_threads (void);
void update_mlfqs_priority (struct thread *t);
void update_mlfqs_recent_cpu (struct thread *t);
void update_mlfqs_load_avg (void);
void thread_increment_recent_cpu (void);
void update_all_mlfqs (void);
/* END */

void do_iret (struct intr_frame *tf);
/* Our Implemetation */
int64_t ret_first_wake_tick(void);
void awake_threads(int64_t);
void thread_sleep (int64_t);
bool less_thread_priority (const struct list_elem *a,
	const struct list_elem *b, void *aux);
void thread_preempt (void);
void thread_donate (struct thread *, struct thread *, int);
void update_donate_priority (struct thread *);
void thread_remove_lock (struct lock *);

// RYU Test
struct thread *thread_get_child (tid_t tid);
#endif /* threads/thread.h */
