/*
 * pthread_setname_np / pthread_getname_np for the browser engine.
 *
 * Emscripten 6.0.10 declares both in <pthread.h> but leaves them out of its libc, in the
 * single-threaded and the pthreads build alike (tools/system_libs.py lists them as TODO #12216).
 * libslic3r's Thread.cpp calls them when Print::process() names its worker threads
 * (name_tbb_thread_pool_threads_set_locale -> set_current_thread_name), so the link needs them.
 * Names only serve debugging, so they are kept per thread for pthread_getname_np and otherwise ignored.
 *
 * Weak, so a libc that grows real implementations wins without a duplicate-symbol error.
 */
#define _GNU_SOURCE /* for the pthread_*name_np prototypes in <pthread.h> */
#include <errno.h>
#include <pthread.h>
#include <string.h>

/* Linux limits thread names to 16 bytes including the terminator; so does Orca's Thread.cpp. */
#define ENGINE_THREAD_NAME_MAX 16

static _Thread_local char s_thread_name[ENGINE_THREAD_NAME_MAX];

__attribute__((weak)) int pthread_setname_np(pthread_t thread, const char *name)
{
    if (name == NULL)
        return EINVAL;
    if (strlen(name) >= ENGINE_THREAD_NAME_MAX)
        return ERANGE;
    /* Only the calling thread's own name can be stored; other threads' names are dropped. */
    if (pthread_equal(thread, pthread_self()))
        strcpy(s_thread_name, name);
    return 0;
}

__attribute__((weak)) int pthread_getname_np(pthread_t thread, char *name, size_t len)
{
    if (name == NULL || len == 0)
        return EINVAL;
    const char *src = pthread_equal(thread, pthread_self()) ? s_thread_name : "";
    if (strlen(src) >= len)
        return ERANGE;
    strcpy(name, src);
    return 0;
}
