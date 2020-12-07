/*
Author: John Tsiombikas <nuclear@member.fsf.org>
I place this piece of code in the public domain. Feel free to use as you see
fit.  I'd appreciate it if you keep my name at the top of the code somehwere,
but whatever.
Main project site: https://github.com/jtsiomb/c11threads
*/

#ifndef C11THREADS_H_
#define C11THREADS_H_

#include <time.h>
#include <errno.h>

enum {
	mtx_plain = 0,
#ifndef __EMSCRIPTEN__
	mtx_recursive = 1,
	mtx_timed = 2,
#endif
};

enum {
	thrd_success,
	thrd_timedout,
	thrd_busy,
	thrd_error,
	thrd_nomem
};

typedef int (*thrd_start_t)(void*);
typedef void (*tss_dtor_t)(void*);

#ifdef _WIN32
#define _WINSOCKAPI_
#include <windows.h>
#include <stdio.h>

typedef CRITICAL_SECTION mtx_t;

static inline int mtx_init(mtx_t* mtx, int type) {
	if (type != mtx_plain) fprintf(stderr, "too lazy to implement non-plain mutexes on windows");
	return InitializeCriticalSectionAndSpinCount(mtx, 0)!=0 ? thrd_success : thrd_error;
}

static inline int mtx_lock(mtx_t* mtx) {
	EnterCriticalSection(mtx);
	return thrd_success;
}

static inline int mtx_unlock(mtx_t* mtx) {
	LeaveCriticalSection(mtx);
	return thrd_success;
}

static inline void mtx_destroy(mtx_t* mtx) {
  DeleteCriticalSection(mtx);
}

typedef struct {
	thrd_start_t fn;
	void* arg;
	HANDLE thrd;
} _thrd_desc;

typedef _thrd_desc* thrd_t;

static inline DWORD WINAPI _thrd_run(void* ptr) {
	_thrd_desc* desc = ptr;
	int ret = desc->fn(desc->arg);
	return *((DWORD*)&ret);
}

static inline int thrd_create(thrd_t* thr, thrd_start_t func, void* arg) {
	*thr = malloc(sizeof(_thrd_desc));
	(*thr)->fn = func; (*thr)->arg = arg;
	(*thr)->thrd = CreateThread(NULL, 0, _thrd_run, *thr, 0, NULL);
	return (*thr)->thrd != NULL ? thrd_success : thrd_error;
}

static inline void thrd_exit(int res) {
	ExitThread(res);
}

static inline int thrd_join(thrd_t thr, int* res) {
	if (WaitForSingleObject(thr->thrd, INFINITE) == WAIT_OBJECT_0) {
		DWORD ret;
		GetExitCodeThread(thr->thrd, &ret);
		*res = *((int*)&ret);
		return thrd_success;
	} else {
		return thrd_error;
	}
}

static inline int thrd_detach(thrd_t thr) {
	return CloseHandle(thr->thrd)!=0 ? thrd_success : thrd_error;
}

typedef CONDITION_VARIABLE cnd_t;

static inline int cnd_init(cnd_t* cnd) {
  InitializeConditionVariable(cnd);
  return thrd_success;
}

static inline int cnd_wait(cnd_t* cnd, mtx_t* mtx) {
  return SleepConditionVariableCS(cnd, mtx, INFINITE)!=0 ? thrd_success : thrd_error;
}

static inline int cnd_broadcast(cnd_t* cnd) {
	WakeAllConditionVariable(cnd);
	return thrd_success;
}

static inline void cnd_destroy(cnd_t* cnd) {
	//empty, windows doesnt delete these?
}

#else
#include <pthread.h>
#include <sched.h>	/* for sched_yield */
#include <sys/time.h>

#define ONCE_FLAG_INIT	PTHREAD_ONCE_INIT

#ifdef __APPLE__
/* Darwin doesn't implement timed mutexes currently */
#define C11THREADS_NO_TIMED_MUTEX
#endif

#ifdef C11THREADS_NO_TIMED_MUTEX
#define PTHREAD_MUTEX_TIMED_NP PTHREAD_MUTEX_NORMAL
#define C11THREADS_TIMEDLOCK_POLL_INTERVAL 5000000	/* 5 ms */
#endif

/* types */
typedef pthread_t thrd_t;
typedef pthread_mutex_t mtx_t;
typedef pthread_cond_t cnd_t;
typedef pthread_key_t tss_t;
typedef pthread_once_t once_flag;

/* ---- thread management ---- */

static inline int thrd_create(thrd_t *thr, thrd_start_t func, void *arg)
{
	int res = pthread_create(thr, 0, (void*(*)(void*))func, arg);
	if(res == 0) {
		return thrd_success;
	}
	return res == ENOMEM ? thrd_nomem : thrd_error;
}

static inline void thrd_exit(int res)
{
	pthread_exit((void*)(long)res);
}

static inline int thrd_join(thrd_t thr, int *res)
{
	void *retval;

	if(pthread_join(thr, &retval) != 0) {
		return thrd_error;
	}
	if(res) {
		*res = (int)(long)retval;
	}
	return thrd_success;
}

static inline int thrd_detach(thrd_t thr)
{
	return pthread_detach(thr) == 0 ? thrd_success : thrd_error;
}

static inline thrd_t thrd_current(void)
{
	return pthread_self();
}

static inline int thrd_equal(thrd_t a, thrd_t b)
{
	return pthread_equal(a, b);
}

static inline int thrd_sleep(const struct timespec *ts_in, struct timespec *rem_out)
{
	if(nanosleep(ts_in, rem_out) < 0) {
		if(errno == EINTR) return -1;
		return -2;
	}
	return 0;
}

static inline void thrd_yield(void)
{
	sched_yield();
}

/* ---- mutexes ---- */

static inline int mtx_init(mtx_t *mtx, int type)
{
	int res;
	pthread_mutexattr_t attr;

	pthread_mutexattr_init(&attr);

#ifndef __EMSCRIPTEN__ //no mutexattrs
	if(type & mtx_timed) {
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_TIMED_NP);
	}
	if(type & mtx_recursive) {
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	}
#endif

	res = pthread_mutex_init(mtx, &attr) == 0 ? thrd_success : thrd_error;
	pthread_mutexattr_destroy(&attr);
	return res;
}

static inline void mtx_destroy(mtx_t *mtx)
{
	pthread_mutex_destroy(mtx);
}

static inline int mtx_lock(mtx_t *mtx)
{
	int res = pthread_mutex_lock(mtx);
	if(res == EDEADLK) {
		return thrd_busy;
	}
	return res == 0 ? thrd_success : thrd_error;
}

static inline int mtx_trylock(mtx_t *mtx)
{
	int res = pthread_mutex_trylock(mtx);
	if(res == EBUSY) {
		return thrd_busy;
	}
	return res == 0 ? thrd_success : thrd_error;
}

static inline int mtx_timedlock(mtx_t *mtx, const struct timespec *ts)
{
	int res;
#ifdef C11THREADS_NO_TIMED_MUTEX
	/* fake a timedlock by polling trylock in a loop and waiting for a bit */
	struct timeval now;
	struct timespec sleeptime;

	sleeptime.tv_sec = 0;
	sleeptime.tv_nsec = C11THREADS_TIMEDLOCK_POLL_INTERVAL;

	while((res = pthread_mutex_trylock(mtx)) == EBUSY) {
		gettimeofday(&now, NULL);

		if(now.tv_sec > ts->tv_sec || (now.tv_sec == ts->tv_sec &&
					(now.tv_usec * 1000) >= ts->tv_nsec)) {
			return thrd_timedout;
		}

		nanosleep(&sleeptime, NULL);
	}
#else
	if((res = pthread_mutex_timedlock(mtx, ts)) == ETIMEDOUT) {
		return thrd_timedout;
	}
#endif
	return res == 0 ? thrd_success : thrd_error;
}

static inline int mtx_unlock(mtx_t *mtx)
{
	return pthread_mutex_unlock(mtx) == 0 ? thrd_success : thrd_error;
}

/* ---- condition variables ---- */

static inline int cnd_init(cnd_t *cond)
{
	return pthread_cond_init(cond, 0) == 0 ? thrd_success : thrd_error;
}

static inline void cnd_destroy(cnd_t *cond)
{
	pthread_cond_destroy(cond);
}

static inline int cnd_signal(cnd_t *cond)
{
	return pthread_cond_signal(cond) == 0 ? thrd_success : thrd_error;
}

static inline int cnd_broadcast(cnd_t *cond)
{
	return pthread_cond_broadcast(cond) == 0 ? thrd_success : thrd_error;
}

static inline int cnd_wait(cnd_t *cond, mtx_t *mtx)
{
	return pthread_cond_wait(cond, mtx) == 0 ? thrd_success : thrd_error;
}

static inline int cnd_timedwait(cnd_t *cond, mtx_t *mtx, const struct timespec *ts)
{
	int res;

	if((res = pthread_cond_timedwait(cond, mtx, ts)) != 0) {
		return res == ETIMEDOUT ? thrd_timedout : thrd_error;
	}
	return thrd_success;
}

/* ---- thread-specific data ---- */

static inline int tss_create(tss_t *key, tss_dtor_t dtor)
{
	return pthread_key_create(key, dtor) == 0 ? thrd_success : thrd_error;
}

static inline void tss_delete(tss_t key)
{
	pthread_key_delete(key);
}

static inline int tss_set(tss_t key, void *val)
{
	return pthread_setspecific(key, val) == 0 ? thrd_success : thrd_error;
}

static inline void *tss_get(tss_t key)
{
	return pthread_getspecific(key);
}

/* ---- misc ---- */

static inline void call_once(once_flag *flag, void (*func)(void))
{
	pthread_once(flag, func);
}

#endif //non-windows
#endif	/* C11THREADS_H_ */
