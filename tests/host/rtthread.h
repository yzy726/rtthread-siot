#ifndef TEST_RTTHREAD_H
#define TEST_RTTHREAD_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <time.h>
#endif
typedef uint32_t rt_tick_t;
typedef int rt_bool_t;
#define RT_NULL NULL
#define RT_TRUE 1
#define RT_FALSE 0
#define RT_ERROR 1
#define RT_ETIMEOUT 2
#define RT_EFULL 3
#define RT_ENOMEM 5
#define RT_ENOSYS 6
#define RT_EINVAL 10
#define rt_calloc calloc
#define rt_free free
#define rt_memcpy memcpy
#define rt_snprintf snprintf
static inline rt_tick_t rt_tick_get(void)
{
#ifdef _WIN32
    return GetTickCount();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (rt_tick_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#endif
}
static inline rt_tick_t rt_tick_from_millisecond(int n) { return (rt_tick_t)n; }
#endif
