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
#include <errno.h>

// global variables
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

	bool sleeping;
	struct timespec deadline;
#define N_REASON 64
	char reason[N_REASON];

	// stats
	u4_t s1_func, s2_func;
	int stat1, stat2;
	const char *units1, *units2;
	u4_t cmds;

	// the object to support sleep/wakeup
	pthread_t pthread;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
};

#define MAX_TASKS 64
struct Task Tasks[MAX_TASKS];

static __thread struct Task *current;
pthread_mutex_t task_lock;

// terminate current thread
static void TaskCleanup(void *param)
{
	struct Task *current_task = (struct Task*)param;
	pthread_cond_destroy(&current->cond);
	pthread_mutex_destroy(&current->mutex);

	// clear out task
	pthread_mutex_lock(&task_lock);
	current->entry = nullptr;
	pthread_mutex_unlock(&task_lock);
}

void TaskInit()
{
	Task *current_task = &Tasks[0];

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

void TaskCheckStacks(bool report)
{
}

const char *Task_s(int id)
{
	if (id == -1)
	{
		id = current->id;
	}
	return Tasks[id].name;
}

const char *Task_ls(int id)
{
	return Task_s(id);
}

void TaskRemove(int id)
{
	struct Task *current_task = (struct Task *)&Tasks[id];

	if (id == current->id)
	{
		TaskCleanup(current_task);
		pthread_exit(0);
	}
	else
	{
		pthread_cancel(current_task->pthread);
		pthread_join(current_task->pthread, NULL);
	}
}

// wait on the current task's sleep condition variable
void *_TaskSleep(const char *reason, u64_t usec, u4_t *wakeup_test)
{
	int ret;
	if (usec > 0)
	{
		ret = clock_gettime(CLOCK_REALTIME, &current->deadline);
		u64_t sec = usec / 1000 / 1000;
		current->deadline.tv_sec += sec;
		current->deadline.tv_nsec += (usec - SEC_TO_USEC(sec)) * 1000;

		if (current->deadline.tv_nsec >= 1000000000) {
    	    current->deadline.tv_sec += current->deadline.tv_nsec / 1000000000;
        	current->deadline.tv_nsec %= 1000000000;
    	}
	}

	ret = pthread_mutex_lock(&current->mutex);
	strcpy(current->reason, reason);
	current->sleeping = true;
	if (usec > 0)
	{
		ret = pthread_cond_timedwait(&current->cond, &current->mutex,
									&current->deadline);
		current->deadline.tv_sec = current->deadline.tv_nsec = 0;
	}
	else
	{
		current->deadline.tv_sec = current->deadline.tv_nsec = 0;
		ret = pthread_cond_wait(&current->cond, &current->mutex);
	}

	if (ret == 0)
	{
		strcpy(current->reason, "wakeup by others");
	} else if (ret == ETIMEDOUT) {
		strcpy(current->reason, "timeout of sleep");
	} else {
		printf("Unhandled wait error=%d\n", ret);
		panic("sleep failed!");
	}

	current->sleeping = false;
	ret = pthread_mutex_unlock(&current->mutex);

	return current->wake_param;
}

// Wakeup the sepecific task via signal the condition variable
void _TaskWakeup(int id, u4_t flags, void *wake_param)
{
	if (Tasks[id].entry == nullptr)
		return;

	pthread_mutex_lock(&Tasks[id].mutex);
	if (Tasks[id].sleeping)
	{
		if (((flags & TWF_CANCEL_DEADLINE) == 0) && (Tasks[id].deadline.tv_sec != 0 || Tasks[id].deadline.tv_nsec != 0))
		{
			// don't do anything
		}
		else
		{
			Tasks[id].wake_param = wake_param;
			pthread_cond_signal(&Tasks[id].cond);
		}
	}
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

	pthread_cleanup_push(TaskCleanup, current_task);
	// call user function
	current_task->entry(current_task->user_parameter);
	pthread_cleanup_pop(1);

	pthread_exit(0);

	return nullptr;
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
			current_task->entry = entry;
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
	current_task->name = name;
	current_task->user_parameter = param;
	current_task->priority.sched_priority = priority;
	current_task->flags = flags;
	current_task->f_arg = f_arg;

	// initialize attributes for thread
	pthread_attr_t attr;
	pthread_attr_init(&attr);

	if (priority >= TASK_MED_PRIORITY)
	{
		// set priority
		pthread_attr_setschedpolicy(&attr, SCHED_RR);
		pthread_attr_setschedparam(&attr, &current_task->priority);
	}
	else
	{
		pthread_attr_setschedpolicy(&attr, SCHED_BATCH);
	}

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
	lfprintf(printf_type, "stat1, units1, stat2, units2, ch, name, reason\n");

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

void _lock_init(lock_t *lock, const char *name, bool recursive)
{
	memset(lock, 0, sizeof(*lock));
	lock->name = name;
	if (recursive)
	{
		pthread_mutexattr_t Attr;

		pthread_mutexattr_init(&Attr);
		pthread_mutexattr_settype(&Attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&lock->mutex, &Attr);
	}
	else
	{
		pthread_mutex_init(&lock->mutex, NULL);
	}
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