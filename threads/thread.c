#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/fixed-point.h"
#include "intrinsic.h"
#include "filesys/directory.h"
#include "userprog/syscall.h"

#ifdef USERPROG
#include "userprog/process.h"
#include "devices/timer.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* Our implementation */
static struct list sleep_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;
/* Our Implementation */
static struct list all_list;

static int thread_get_max_priority (void);
static int64_t first_wake_tick = INT64_MAX;
bool less_thread_sleep(const struct list_elem *a,
	const struct list_elem *b, void *aux);
/* END */

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);
static int thread_get_max_priority (void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	/* Our Implementation */
	list_init (&sleep_list);
	list_init (&destruction_req);
	list_init (&all_list);
	load_avg = 0;
	/* END */

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Our implementation */
	struct list_elem *p = list_begin(&sleep_list);
	struct list_elem *nextp;
	struct thread *sleep_thread;
	while(p!=list_end(&sleep_list))
	{
		sleep_thread = list_entry(p, struct thread, elem);
		nextp = list_next(p);
		if(timer_ticks() >= sleep_thread->wake_tick)
		{
			list_remove(&sleep_thread->elem);
			thread_unblock(sleep_thread);
		}
		p = nextp;
	}

	/* Enforce preemption. */
if (++thread_ticks >= TIME_SLICE)
	intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	struct thread *parent = thread_current();

	if(parent->cur_dir != NULL)
	{
		t->cur_dir = dir_reopen(parent->cur_dir);
	}

	/* Our Implementation */
	list_push_back (&parent->child_list, &t->child_elem);
	// t->fd_table = (struct file **)malloc(sizeof(struct file *) * FD_MAX);
	// END

	/* Add to run queue. */
	thread_unblock (t);

	/* Our Implementation */		
	struct thread *curr = thread_current();
	/* If newly created thread has higher priority than current,
	   reschedule. */
	if(curr->priority < t->priority){
		thread_yield();
	}
	return tid;
	/* END */
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	/* Original Implementation
	 * list_push_back (&ready_list, &t->elem);
	 */

	/* Our Implementation */
  list_insert_ordered(&ready_list, &t->elem, less_thread_priority, NULL);
	/* END */
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
	sema_up (&thread_current()->wait_sema); //let parent know that it is going to exit
	sema_down (&thread_current()->exit_sema); //child should wait until parent finish waiting

#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	list_remove(&thread_current()->allelem);
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
	{
		/* Original Implemetation */
		/* list_push_back (&ready_list, &curr->elem); */
		/* Our Implemetation*/
		list_insert_ordered(&ready_list, &curr->elem, less_thread_priority, NULL);
		/* END */
	}
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	/* Our Implementation */
	enum intr_level old_level = intr_disable();
	struct thread *t = thread_current();
	t->base_priority = new_priority;

	// Only change the current_priority
	// When it has no relation to priority donation	
	if(list_empty(&t->donation_list))
	{
		t->priority = new_priority;
	}

	intr_set_level(old_level);
	thread_preempt ();
	/* END */
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) {
	/* TODO: Your implementation goes here */
	/* Our Implementation */
	struct thread *curr = thread_current();
	curr->nice = nice;
	update_mlfqs_priority(curr);
	int priority = thread_get_priority();
	if(thread_get_max_priority() > priority)
		thread_yield();
	/* END */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	/* Our Implementation */
	struct thread *curr = thread_current();
	int nice = curr->nice;
	return nice;
	/* END */
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	/* Our Implementation */
	int ret = CONVERT_TO_INTN(MULT_BY_N(load_avg, 100));
	return ret;
	/* END */
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	/* Our Implementation */
	struct thread *curr = thread_current();
	int recent_cpu = curr->recent_cpu;
	int ret = CONVERT_TO_INTN(MULT_BY_N(recent_cpu, 100));
	return ret;
	/* END */
}
/* END */

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	enum intr_level old_level;

	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;
	// Renamed Implementation
	t->prog_file = NULL;
	t->fd = 2;
	// END
	list_init(&t->mmap_list);	// At the beginning, there is no memory mapped file on the memory so initizlize the list
	list_init(&t->sym_list);

	old_level = intr_disable();
	list_push_back(&all_list, &t->allelem);
	intr_set_level(old_level);

#ifdef USERPROG

	sema_init (&t->wait_sema, 0);
	sema_init (&t->exit_sema, 0);
	sema_init (&t->filecopy_sema, 0);
	list_init (&t->child_list);
	list_init (&t->file_list);
	
#endif
	t->wake_tick = 0;
	list_init(&t->donation_list);
	t->base_priority = priority;
	t->cur_dir = NULL;

	/* END */
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used bye the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}

/* Our Implementation */
/* Returns true if current thread is idle thread. */
/* Otherwise, return false. */
static bool is_idle(struct thread* t) {
	if (t == idle_thread)
		return true;
	return false;
}

/* Our Implementation */
/* Let current thread sleep for the amount of time */
void thread_sleep(int64_t time){
	enum intr_level old_level;
	old_level = intr_disable();
	struct thread *curr = thread_current ();
	if(!is_idle(curr))
	{
		curr -> wake_tick = time;
		list_insert_ordered(&sleep_list, &curr->elem, less_thread_sleep, NULL);
		if (time < first_wake_tick)
			first_wake_tick = time;
		thread_block();
	}
	intr_set_level(old_level);
}

/* Return tick of thread that has to be waken first */
int64_t ret_first_wake_tick(void)
{
	return first_wake_tick;
}

/* Wake threads whose wake_tick value is lower than ticks(parameter) */
void awake_threads(int64_t ticks){
	struct list_elem *p = list_begin(&sleep_list);
	struct list_elem *nextp;
	struct thread *sleep_thread;
	first_wake_tick = INT64_MAX;
	while(p!=list_end(&sleep_list))
	{
		sleep_thread = list_entry(p, struct thread, elem);
		int64_t t_wake_tick = sleep_thread->wake_tick;
		nextp = list_next(p);
		if(ticks >= t_wake_tick)
		{
			list_remove(&sleep_thread->elem);
			thread_unblock(sleep_thread);
		}
		else
		{
			first_wake_tick = t_wake_tick;
			break;
		}
		p = nextp;
	}
}

/* Our Implementation */
bool less_thread_priority(const struct list_elem *a,
	const struct list_elem *b, void *aux) {
		const struct thread *a_thread = list_entry(a, struct thread, elem);
		const struct thread *b_thread = list_entry(b, struct thread, elem);
		return a_thread->priority > b_thread->priority;
}

/* list_less_func about wake tick of thread */
bool less_thread_sleep(const struct list_elem *a,
	const struct list_elem *b, void *aux) {
		const struct thread *a_thread = list_entry(a, struct thread, elem);
		const struct thread *b_thread = list_entry(b, struct thread, elem);
		return a_thread->wake_tick < b_thread->wake_tick;
}
/* END */

static int thread_get_max_priority (void) {
   // What if ready_list is empty?
   if (!list_empty(&ready_list))
   {
		struct list_elem *p = list_front(&ready_list);
		struct thread *target = list_entry(p, struct thread, elem);
		return target->priority;
   }
   return -1;
}

/* PRIORITY DONATION */
void thread_preempt (void) {
	enum intr_level old_level = intr_disable ();
	int priority = thread_current()->priority;
	int max_priority = thread_get_max_priority;
	if(max_priority!=-1)
	{
		intr_set_level(old_level);
		thread_yield();
	}
	intr_set_level(old_level);
}

void thread_donate (struct thread *holder, struct thread *t, int depth) {
	if(depth >= 8)
		return;
	if(holder == NULL)
		return;
	// priority donation happened
	if(holder->priority < t->priority) {
		update_donate_priority (holder);
		// Nested donation
		if(holder->wait_on_lock!=NULL)
			thread_donate(holder->wait_on_lock->holder, holder, ++depth);
	}
	// Else, no priority donation occurs.
	return;
}

void _update_donate_priority (struct thread *cur) {
	int max_priority = 0;
	struct list_elem *p;
	for(p = list_begin(&cur->donation_list); p!=list_end(&cur->donation_list); p=list_next(p)) 
	{
		struct thread *t = list_entry(p, struct thread, donation_elem);
		if(t->priority > max_priority)
			max_priority = t->priority;
	}
	cur->priority = max_priority;
}

void update_donate_priority (struct thread *cur) {
   int max_priority = 0;
   struct list_elem *p = list_begin(&cur->donation_list);
   struct list_elem *nextp;
   while(p!=list_end(&cur->donation_list))
   {
      struct thread *t = list_entry(p, struct thread, donation_elem);
      nextp = list_next(p);
      if(t->priority > max_priority)
         max_priority = t->priority;
      p = nextp;
   }
   cur->priority = max_priority;
}

void _thread_remove_lock (struct lock *lock) {
   struct thread *holder = thread_current();
   struct list_elem *p;
   for(p = list_begin(&holder->donation_list); p!=list_end(&holder->donation_list);)
   {
      struct thread *t = list_entry(p, struct thread, donation_elem);
      if(t->wait_on_lock == lock)
         p = list_remove(p);
      else p = list_next(p);
   }
   
   holder->priority = holder->base_priority;
   if(!list_empty(&holder->donation_list))
      update_donate_priority(holder);
}

void thread_remove_lock (struct lock *lock) {
   struct thread *holder = thread_current();
   struct list_elem *p = list_begin(&holder->donation_list);
   struct list_elem *nextp;
   while(p!=list_end(&holder->donation_list))
   {
      struct thread *t = list_entry(p, struct thread, donation_elem);
      nextp = list_next(p);
      if(t->wait_on_lock == lock)
         list_remove(p);
      p = nextp;
   }
   
   holder->priority = holder->base_priority;
   if(!list_empty(&holder->donation_list))
      update_donate_priority(holder);
}

// MLFQS

/* Our Implementation */
/* Return number of threads that are either running or in ready list */
int ready_threads (void) {
	int ret = list_size(&ready_list);
	if (!is_idle(thread_current()))
      ret += 1;
	return ret;
}

/* Update priority of thread t according to its recent cpu and nice value */
void update_mlfqs_priority (struct thread *t) {
	if(is_idle(t))
		return;
	int nice = t->nice;
	int fp_priority = SUB_N(SUB_Y(CONVERT_TO_FP(PRI_MAX),
	DIVIDE_BY_N(t->recent_cpu, 4)), nice* 2);
	// priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)
	int priority = CONVERT_TO_INTN(fp_priority);
	if (priority < PRI_MIN)
		t->priority = PRI_MIN;
	else if (priority > PRI_MAX)
		t->priority = PRI_MAX;
	else
		t->priority = priority;

	return;
}

/* Update recent cpu of thread t according to its nice value and load average */
void update_mlfqs_recent_cpu (struct thread *t) {
	if(is_idle(t))
		return;
	int nice = t->nice;
	int recent_cpu = t->recent_cpu;
	int MUL_l_2 = MULT_BY_N(load_avg,2);
	int MUL_PLUS_1 = ADD_N(MUL_l_2, 1);
	int DIV = DIVIDE_BY_Y(MUL_l_2, MUL_PLUS_1);
	int MUL_REC = MULT_BY_Y(DIV, recent_cpu);
	int ADD_NICE = ADD_N(MUL_REC, nice);

	recent_cpu = ADD_NICE;
	// recent_cpu = (2 * load_avg) / (2 * load_avg + 1) * recent_cpu + nice
	t->recent_cpu = recent_cpu;
	return;
}

/* Update load average according to value of ready_threads */
void update_mlfqs_load_avg (void) {
	load_avg = ADD_Y(MULT_BY_Y(DIVIDE_BY_N(CONVERT_TO_FP(59),60),load_avg),
	MULT_BY_N(DIVIDE_BY_N(CONVERT_TO_FP(1),60), ready_threads()));
	// load_avg = (59/60) * load_avg + (1/60) * ready_threads;
	return;
}

/* Increment thread t's recent cpu by 1 */
void thread_increment_recent_cpu (void) {
	struct thread *curr = thread_current();
	if(is_idle(curr))
		return;
	curr->recent_cpu = ADD_N(curr->recent_cpu, 1);
}

/* Update recent cpu and priority of all threads(Running, Ready, Sleeping) 
   and update load average */
void update_all_mlfqs (void) {
   update_mlfqs_load_avg();
   struct list_elem *p = list_begin(&ready_list);
   struct thread *t;
   t = thread_current();
   update_mlfqs_recent_cpu(t);
   update_mlfqs_priority(t);
   for(p; p!=list_end(&ready_list); p=list_next(p))
   {
      t = list_entry(p, struct thread, elem);
      update_mlfqs_recent_cpu(t);
      update_mlfqs_priority(t);
   }
   p = list_begin(&sleep_list);
   for(p; p!=list_end(&sleep_list); p=list_next(p))
   {
      t = list_entry(p, struct thread, elem);
      update_mlfqs_recent_cpu(t);
      update_mlfqs_priority(t);
   }
   if (!list_empty(&ready_list))
      list_sort(&ready_list, less_thread_priority, NULL);
}

/* Our Implementation */
// Get thread by tid
struct thread
*thread_get_by_id (int32_t id)
{
  ASSERT (id != TID_ERROR);
  struct list_elem *e;
  struct thread *t;
  e = list_tail (&all_list);
  while ((e = list_prev (e)) != list_head (&all_list))
    {
      t = list_entry (e, struct thread, allelem);
      if (t->tid == id && t->status != THREAD_DYING)
        return t;
    }
  return NULL;
}
/* END */

