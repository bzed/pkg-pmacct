/* Thread management routine header.
 * Copyright (C) 1998 Kunihiro Ishiguro
 *
 * This file is part of GNU Zebra.
 *
 * GNU Zebra is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * GNU Zebra is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Zebra; see the file COPYING.  If not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.  
 */

#ifndef _THREAD_H_
#define _THREAD_H_

struct rusage_t
{
  struct timeval real;
};
#define RUSAGE_T        struct rusage_t

#define GETRUSAGE(X) thread_getrusage(X)

/* Linked list of thread. */
struct thread_list
{
  struct thread *head;
  struct thread *tail;
  int count;
};

/* Master of the theads. */
struct thread_master
{
  struct thread_list read;
  struct thread_list write;
  struct thread_list timer;
  struct thread_list event;
  struct thread_list ready;
  struct thread_list unuse;
  struct thread_list background;
  fd_set readfd;
  fd_set writefd;
  fd_set exceptfd;
  unsigned long alloc;
};

typedef unsigned char thread_type;

/* Thread itself. */
struct thread
{
  thread_type type;		/* thread type */
  thread_type add_type;		/* thread type */
  struct thread *next;		/* next pointer of the thread */   
  struct thread *prev;		/* previous pointer of the thread */
  struct thread_master *master;	/* pointer to the struct thread_master. */
  int (*func) (struct thread *); /* event function */
  void *arg;			/* event argument */
  union {
    int val;			/* second argument of the event. */
    int fd;			/* file descriptor in case of read/write. */
    struct timeval sands;	/* rest of time sands value. */
  } u;
  RUSAGE_T ru;			/* Indepth usage info.  */
  struct cpu_thread_history *hist; /* cache pointer to cpu_history */
  char* funcname;
};

struct cpu_thread_history 
{
  int (*func)(struct thread *);
  char *funcname;
  unsigned int total_calls;
  struct time_stats
  {
    unsigned long total, max;
  } real;
  thread_type types;
};

/* Clocks supported by Quagga */
enum quagga_clkid {
  QUAGGA_CLK_REALTIME = 0,	/* ala gettimeofday() */
  QUAGGA_CLK_MONOTONIC,		/* monotonic, against an indeterminate base */
  QUAGGA_CLK_REALTIME_STABILISED, /* like realtime, but non-decrementing */
};

/* Thread types. */
#define THREAD_READ           0
#define THREAD_WRITE          1
#define THREAD_TIMER          2
#define THREAD_EVENT          3
#define THREAD_READY          4
#define THREAD_BACKGROUND     5
#define THREAD_UNUSED         6
#define THREAD_EXECUTE        7

/* Thread yield time.  */
#define THREAD_YIELD_TIME_SLOT     10 * 1000L /* 10ms */

/* Macros. */
#define THREAD_ARG(X) ((X)->arg)
#define THREAD_FD(X)  ((X)->u.fd)
#define THREAD_VAL(X) ((X)->u.val)

#define THREAD_READ_ON(master,thread,func,arg,sock) \
  do { \
    if (! thread) \
      thread = thread_add_read (master, func, arg, sock); \
  } while (0)

#define THREAD_WRITE_ON(master,thread,func,arg,sock) \
  do { \
    if (! thread) \
      thread = thread_add_write (master, func, arg, sock); \
  } while (0)

#define THREAD_TIMER_ON(master,thread,func,arg,time) \
  do { \
    if (! thread) \
      thread = thread_add_timer (master, func, arg, time); \
  } while (0)

#define THREAD_OFF(thread) \
  do { \
    if (thread) \
      { \
        thread_cancel (thread); \
        thread = NULL; \
      } \
  } while (0)

#define THREAD_READ_OFF(thread)  THREAD_OFF(thread)
#define THREAD_WRITE_OFF(thread)  THREAD_OFF(thread)
#define THREAD_TIMER_OFF(thread)  THREAD_OFF(thread)

#define thread_add_read(m,f,a,v) funcname_thread_add_read(m,f,a,v,#f)
#define thread_add_write(m,f,a,v) funcname_thread_add_write(m,f,a,v,#f)
#define thread_add_timer(m,f,a,v) funcname_thread_add_timer(m,f,a,v,#f)
#define thread_add_timer_msec(m,f,a,v) funcname_thread_add_timer_msec(m,f,a,v,#f)
#define thread_add_event(m,f,a,v) funcname_thread_add_event(m,f,a,v,#f)
#define thread_execute(m,f,a,v) funcname_thread_execute(m,f,a,v,#f)

/* The 4th arg to thread_add_background is the # of milliseconds to delay. */
#define thread_add_background(m,f,a,v) funcname_thread_add_background(m,f,a,v,#f)

/* Prototypes. */
#if (!defined __THREAD_C)
#define EXT extern
#else
#define EXT
#endif
EXT struct thread_master *thread_master_create (void);
EXT void thread_master_free (struct thread_master *);
EXT struct thread *funcname_thread_add_read (struct thread_master *, int (*)(struct thread *), void *, int, const char *);
EXT struct thread *funcname_thread_add_write (struct thread_master *, int (*)(struct thread *), void *, int, const char *);
EXT struct thread *funcname_thread_add_timer (struct thread_master *, int (*)(struct thread *), void *, long, const char *);
EXT struct thread *funcname_thread_add_timer_msec (struct thread_master *, int (*)(struct thread *), void *, long, const char *);
EXT struct thread *funcname_thread_add_event (struct thread_master *, int (*)(struct thread *), void *, int, const char *);
EXT struct thread *funcname_thread_add_background (struct thread_master *, int (*func)(struct thread *), void *arg, long, const char *);
EXT struct thread *funcname_thread_execute (struct thread_master *, int (*)(struct thread *), void *, int, const char *);
EXT void thread_cancel (struct thread *);
EXT unsigned int thread_cancel_event (struct thread_master *, void *);
EXT struct thread *thread_fetch (struct thread_master *, struct thread *);
EXT void thread_call (struct thread *);
EXT unsigned long thread_timer_remain_second (struct thread *);
EXT int thread_should_yield (struct thread *);
EXT void thread_getrusage (RUSAGE_T *);
EXT int quagga_gettime (enum quagga_clkid, struct timeval *);
EXT time_t quagga_time (time_t *);
EXT unsigned long thread_consumed_time(RUSAGE_T *after, RUSAGE_T *before, unsigned long *cpu_time_elapsed);

/* Global variable containing a recent result from gettimeofday.  This can
   be used instead of calling gettimeofday if a recent value is sufficient.
   This is guaranteed to be refreshed before a thread is called. */
EXT struct timeval recent_time;
/* Similar to recent_time, but a monotonically increasing time value */
EXT struct timeval recent_relative_time (void);
#undef EXT

#endif /* _THREAD_H_ */
