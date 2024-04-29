//////////////////////////////////////////////////////////////////////////
// Homemade GPS Receiver
// Copyright (C) 2013 Andrew Holme
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// http://www.holmea.demon.co.uk/GPS/Main.htm
//////////////////////////////////////////////////////////////////////////

// Copyright (c) 2014-2017 John Seamons, ZL4VO/KF6VO

#include "types.h"
#include "kiwi.h"
#include "config.h"
#include "valgrind.h"
#include "misc.h"
#include "str.h"
#include "coroutines.h"
#include "debug.h"
#include "peri.h"

#include <pthread.h>
#include <sched.h>

bool itask_run;

// global variables
u4_t task_medium_priority = TASK_MED_PRI_NEW;
struct Task
{
	int id;

	void *user_parameter;
	void *wake_param;

	const char *name;
	funcP_t entry;
	u4_t flags;
	int f_arg;
	sched_param priority;

	bool killed;
	bool sleeping;
#define N_REASON 64
	char reason[N_REASON];

	// stats
	u4_t s1_func, s2_func;
	int stat1, stat2;
	const char *units1, *units2;
	u4_t spi_retry, cmds;

	// the object to support sleep/wakeup
	pthread_t pthread;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
};

#define MAX_TASKS 64
struct Task Tasks[MAX_TASKS];

static __thread struct Task *current;
pthread_mutex_t task_lock;

void TaskInit()
{
	Task *current_task = &Tasks[0];

	task_medium_priority = TASK_MED_PRI_NEW;

	current_task->id = 0;
	current_task->entry = (funcP_t)1;
	current_task->name = "main thread";

	// create the mutex and cond
	pthread_cond_init(&current_task->cond, NULL);
	pthread_mutex_init(&current_task->mutex, NULL);

	pthread_mutex_init(&task_lock, NULL);

	current = current_task;
}

void TaskInitCfg()
{
}

void TaskCollect()
{
}

void TaskForkChild()
{
}

bool TaskIsChild()
{
	return false;
}

void TaskCheckStacks(bool report)
{
}

const char *Task_s(int id)
{
	return Tasks[id].name;
}

// TODO: This is ugly, we should not do this
void TaskSleepID(int id, int usec)
{
	if (current->id == id)
	{
		_TaskSleep("TaskSleepId", usec, NULL);
	}
	else
	{
		panic("Stop sleep other task.");
	}
}

void TaskRemove(int id)
{
	Tasks[id].killed = true;
	if (id == current->id)
	{
		pthread_exit(0);
	}
}

// wait on the current task's sleep condition variable
void *_TaskSleep(const char *reason, u64_t usec, u4_t *wakeup_test)
{
	int ret;
	struct timespec max_wait = {0, 0};
	if (usec > 0)
	{
		ret = clock_gettime(CLOCK_REALTIME, &max_wait);
		int sec = usec / 1000 / 1000;
		max_wait.tv_sec += sec;
		max_wait.tv_nsec += (usec - sec * 1000 * 1000) * 1000;
	}

	ret = pthread_mutex_lock(&current->mutex);
	strcpy(current->reason, reason);
	current->sleeping = true;
	if (usec > 0)
	{
		ret = pthread_cond_timedwait(&current->cond, &current->mutex,
									 &max_wait);
	}
	else
	{
		ret = pthread_cond_wait(&current->cond, &current->mutex);
	}
	current->reason[0] = '\0';
	current->sleeping = false;
	ret = pthread_mutex_unlock(&current->mutex);

	return current->wake_param;
}

// Wakeup the sepecific task via signal the condition variable
void _TaskWakeup(int id, u4_t flags, void *wake_param)
{
	pthread_mutex_lock(&Tasks[id].mutex);
	Tasks[id].wake_param = wake_param;
	pthread_cond_signal(&Tasks[id].cond);
	pthread_mutex_unlock(&Tasks[id].mutex);
}

// thread entry for every thread
static void *ThreadEntry(void *parameter)
{
	// setup the thread local variable to point to the thread structure
	struct Task *current_task = (struct Task *)parameter;

	// create the mutex and cond
	pthread_cond_init(&current_task->cond, NULL);
	pthread_mutex_init(&current_task->mutex, NULL);

	current = current_task;

	// call user function
	current_task->entry(current_task->user_parameter);

	pthread_cond_destroy(&current->cond);
	pthread_mutex_destroy(&current->mutex);
	pthread_exit(0);
}

// create the task and invoke the entry function
int _CreateTask(funcP_t entry, const char *name, void *param, int priority, u4_t flags, int f_arg)
{
	struct Task *current_task = nullptr;

	pthread_mutex_lock(&task_lock);
	for (auto i = 1; i < MAX_TASKS; i++)
	{
		if (Tasks[i].entry == nullptr)
		{
			current_task = &Tasks[i];
			memset(current_task, 0, sizeof(Task));
			current_task->id = i;
			break;
		}
	}
	pthread_mutex_unlock(&task_lock);

	if (current_task == nullptr)
	{
		// out of tasks
		dump();
		panic("create_task: no tasks available");
	}

	// Initialize the structure
	current_task->entry = entry;
	current_task->name = name;
	current_task->user_parameter = param;
	current_task->priority.sched_priority = priority;
	current_task->flags = flags;
	current_task->f_arg = f_arg;

	// initialize attributes for thread
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	// set priority
	pthread_attr_setschedpolicy(&attr, SCHED_RR);
	pthread_attr_setschedparam(&attr, &current_task->priority);

	pthread_create(&current_task->pthread, &attr, ThreadEntry, current_task);

	pthread_setname_np(current_task->pthread, name);

	return current_task->id;
}

// TODO
void TaskDump(u4_t flags)
{
	u4_t printf_type = flags & PRINTF_FLAGS;

	int tused = 0;
	for (unsigned i = 0; i < MAX_TASKS; i++)
	{
		if (Tasks[i].entry)
			tused++;
	}

	lfprintf(printf_type, "\n");
	lfprintf(printf_type, "TASKS: used %d/%d\n", tused, MAX_TASKS);
	lfprintf(printf_type, "stat1, units1, stat2, units2, ch, name, reason");

	for (unsigned i = 0; i < MAX_TASKS; i++)
	{
		Task *t = &Tasks[i];

		if (!t->entry)
			continue;
		int rx_channel = (t->flags & CTF_RX_CHANNEL) ? (t->flags & CTF_CHANNEL) : -1;

		lfprintf(printf_type, "%5d %-3s %5d %-3s %s%d %-10s %-24s\n",
				 t->stat1, t->units1 ? t->units1 : " ", t->stat2, t->units2 ? t->units2 : " ",
				 (rx_channel != -1) ? "c" : "", rx_channel,
				 t->name, t->reason);
	};
}

int TaskStat(u4_t s1_func, int s1_val, const char *s1_units, u4_t s2_func, int s2_val, const char *s2_units)
{
	int r = 0;
	Task *t = current;

	t->s1_func |= s1_func & TSTAT_LATCH;
	if (s1_units)
		t->units1 = s1_units;
	switch (s1_func & TSTAT_MASK)
	{
	case TSTAT_NC:
		break;
	case TSTAT_SET:
		t->stat1 = s1_val;
		break;
	case TSTAT_INCR:
		t->stat1++;
		break;
	case TSTAT_MIN:
		if (s1_val < t->stat1 || t->stat1 == 0)
			t->stat1 = s1_val;
		break;
	case TSTAT_MAX:
		if (s1_val > t->stat1)
			t->stat1 = s1_val;
		break;
	default:
		break;
	}

	t->s2_func |= s2_func & TSTAT_LATCH;
	if (s2_units)
		t->units2 = s2_units;
	switch (s2_func & TSTAT_MASK)
	{
	case TSTAT_NC:
		break;
	case TSTAT_SET:
		t->stat2 = s2_val;
		break;
	case TSTAT_INCR:
		t->stat2++;
		break;
	case TSTAT_MIN:
		if (s2_val < t->stat2 || t->stat2 == 0)
			t->stat2 = s2_val;
		break;
	case TSTAT_MAX:
		if (s2_val > t->stat2)
			t->stat2 = s2_val;
		break;
	default:
		break;
	}

	if ((t->s1_func | t->s2_func) & TSTAT_CMDS)
		t->cmds++;

	if ((t->s1_func | t->s2_func) & TSTAT_SPI_RETRY)
	{
		t->spi_retry++;
		r = t->spi_retry;
		if ((r > 100) && ev_dump)
		{
			evNT(EC_DUMP, EV_NEXTTASK, ev_dump, "spi_retry", evprintf("DUMP IN %.3f SEC", ev_dump / 1000.0));
		}
	}

	return r;
}

u4_t TaskPriority(int priority)
{
	if (!current)
		return 0;

	return current->priority.sched_priority;
}

u4_t TaskID()
{
	if (!current)
		return 0;
	return current->id;
}

const char *_TaskName(const char *name, bool free_name)
{
	struct Task *ct = current;

	if (!ct)
		return "main";
	if (name != NULL)
	{
		if (ct->flags & CTF_TNAME_FREE)
		{
			free((void *)ct->name);
			ct->flags &= ~CTF_TNAME_FREE;
		}
		ct->name = name;
		if (free_name)
			ct->flags |= CTF_TNAME_FREE;
	}
	return ct->name;
}

void TaskMinRun(u4_t minrun_us)
{
	struct Task *t = current;

	// t->minrun = minrun_us;
}

u4_t TaskFlags()
{
	struct Task *t = current;

	return t->flags;
}

void TaskSetFlags(u4_t flags)
{
	struct Task *t = current;

	t->flags = flags;
}

void *TaskGetUserParam()
{
	return current->user_parameter;
}

void TaskSetUserParam(void *param)
{
	current->user_parameter = param;
}

/*
 * ------------------------------------------------------------
 *  Lock related functions
 */
#define MAX_LOCK_N 256
static int n_lock_list = 0;
static lock_t *locks[MAX_LOCK_N];

void _lock_init(lock_t *lock, const char *name)
{
	memset(lock, 0, sizeof(*lock));
	lock->name = name;
	pthread_mutex_init(&lock->mutex, NULL);
}

void lock_register(lock_t *lock)
{
	int lock_index = __atomic_fetch_add(&n_lock_list, 1, __ATOMIC_SEQ_CST);
	locks[lock_index] = lock;
}

void lock_dump()
{
	// TODO
}

bool lock_check()
{
	// TODO
	return false;
}

void lock_enter(lock_t *lock)
{
	pthread_mutex_lock(&lock->mutex);
}

void lock_leave(lock_t *lock)
{
	pthread_mutex_unlock(&lock->mutex);
}