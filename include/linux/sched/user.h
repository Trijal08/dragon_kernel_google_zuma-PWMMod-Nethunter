/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_USER_H
#define _LINUX_SCHED_USER_H

#include <linux/uidgid.h>
#include <linux/atomic.h>
#include <linux/percpu_counter.h>
#include <linux/refcount.h>
#include <linux/ratelimit.h>
#include <linux/android_kabi.h>

/*
 * Some day this will be a full-fledged user tracking system..
 */
struct user_struct {
	refcount_t __count;	/* reference count */
#ifdef CONFIG_EPOLL
	struct percpu_counter epoll_watches; /* The number of file descriptors currently watched */
#endif
	unsigned long unix_inflight;	/* How many files in flight in unix sockets */
	atomic_long_t pipe_bufs;  /* how many pages are allocated in pipe buffers */

	/* Hash table maintenance information */
	struct hlist_node uidhash_node;
	kuid_t uid;

#if defined(CONFIG_PERF_EVENTS) || defined(CONFIG_BPF_SYSCALL) || \
    defined(CONFIG_NET) || defined(CONFIG_IO_URING)
	atomic_long_t locked_vm;
#endif
#ifdef CONFIG_WATCH_QUEUE
	atomic_t nr_watches;	/* The number of watches this user currently has */
#endif

	/* Miscellaneous per-user rate limit */
	struct ratelimit_state ratelimit;

	ANDROID_KABI_RESERVE(1);
	ANDROID_KABI_RESERVE(2);
	ANDROID_OEM_DATA_ARRAY(1, 2);

#ifdef CONFIG_KSU_SUSFS_SUS_PATH
	u64 android_kabi_reserved2;
#endif
};

extern int uids_sysfs_init(void);

extern struct user_struct *find_user(kuid_t);

extern struct user_struct root_user;
#define INIT_USER (&root_user)


/* per-UID process charging. */
extern struct user_struct * alloc_uid(kuid_t);
static inline struct user_struct *get_uid(struct user_struct *u)
{
	refcount_inc(&u->__count);
	return u;
}
extern void free_uid(struct user_struct *);

#endif /* _LINUX_SCHED_USER_H */
