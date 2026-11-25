#ifndef _DLFCN_H
#define _DLFCN_H
/* libdl.so.2, glibc 2.28 */
#define RTLD_LAZY 0x0001
#define RTLD_NOW 0x0002
void *dlopen(const char *, int);
void *dlsym(void *, const char *);
int dlclose(void *);
char *dlerror(void);
#endif
