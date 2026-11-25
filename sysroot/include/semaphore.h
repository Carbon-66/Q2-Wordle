#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H
/* glibc 2.28 o32: a sem_t is 16 bytes. 32 here is harmless room. sem_init, sem_wait and sem_post
 * are bound to GLIBC_2.2, the version Q2 Pod's demo imports (sysroot/stubs/gen.py). */
typedef union { char size[32]; long align; } sem_t;
int sem_init(sem_t *, int, unsigned);
int sem_wait(sem_t *);
int sem_post(sem_t *);
#endif
