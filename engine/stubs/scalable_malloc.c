/*
 * oneTBB's scalable_malloc family for the threaded engine (mt).
 *
 * libslic3r calls scalable_malloc / scalable_free directly (SupportMaterial.cpp), which live in tbbmalloc. The engine
 * links oneTBB without tbbmalloc (engine_without_tbbmalloc in Dependencies.cmake: a second allocator over wasm's one
 * shared heap only costs memory), so these forward to the C allocator, which is mimalloc in the mt build: thread-safe
 * and fast. Weak, so a linked tbbmalloc would win. Unused in the single-threaded build.
 */
#include <stddef.h>
#include <stdlib.h>

__attribute__((weak)) void *scalable_malloc(size_t size) { return malloc(size); }
__attribute__((weak)) void scalable_free(void *ptr) { free(ptr); }
__attribute__((weak)) void *scalable_realloc(void *ptr, size_t size) { return realloc(ptr, size); }
__attribute__((weak)) void *scalable_calloc(size_t nobj, size_t size) { return calloc(nobj, size); }
__attribute__((weak)) int scalable_posix_memalign(void **memptr, size_t alignment, size_t size)
{
    return posix_memalign(memptr, alignment, size);
}
__attribute__((weak)) void *scalable_aligned_malloc(size_t size, size_t alignment)
{
    void *p = NULL;
    return posix_memalign(&p, alignment < sizeof(void *) ? sizeof(void *) : alignment, size) == 0 ? p : NULL;
}
__attribute__((weak)) void scalable_aligned_free(void *ptr) { free(ptr); }
