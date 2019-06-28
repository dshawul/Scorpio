#ifndef __MY_TYPES__
#define __MY_TYPES__

/*
Platform specific defines for Windows
For others, they are set in Makefile
*/
#ifdef _MSC_VER
#define ARC_64BIT
#define HAS_POPCNT
#define HAS_PREFETCH
#define PARALLEL
#define USE_SPINLOCK
#endif
#ifdef HAS_BSF
#define HAS_BSF
#endif
/*
int types
*/
#    include <stdint.h>

typedef int8_t BMP8;
typedef uint8_t UBMP8;
typedef int16_t BMP16;
typedef uint16_t UBMP16;
typedef int32_t BMP32;
typedef uint32_t UBMP32;
typedef int64_t BMP64;
typedef uint64_t UBMP64;

/*
Os stuff
*/
#ifdef _WIN32
#    include <windows.h>
#    undef CDECL
#    define CDECL __cdecl
#    define GETPID()  _getpid()
#    define GETTID()  GetCurrentThreadId()
#else
#    include <unistd.h>
#    include <sys/syscall.h>
#    define CDECL
#    define GETPID()  getpid()
#    define GETTID()  syscall(SYS_gettid)
#endif

#ifdef _MSC_VER
#    define UINT64(x) (x##ui64)
#    define FMTU64   "%016I64x"
#    define FMT64    "%I64d"
#    define FMT64W   "%20I64d"
#else
#    ifdef ARC_64BIT
#        define UINT64
#    else
#        define UINT64(x) (x##ULL)
#    endif
#    define FMTU64     "%016llx"
#    define FMT64      "%lld"
#    define FMT64W     "%20lld"
#endif

/*
Force inline
*/
#if !defined(FORCEINLINE)
#   if defined (__GNUC__)
#       define FORCEINLINE  __inline __attribute__((always_inline))
#   elif defined (_WIN32)
#       define FORCEINLINE  __forceinline
#   else
#       define FORCEINLINE  __inline
#   endif
#endif

/*
Intrinsic popcnt
*/
#if defined(HAS_POPCNT) && defined(ARC_64BIT)
#   if defined(__GNUC__)
#       define popcnt(x) __builtin_popcountll(x)
#   elif defined(_WIN32)
#       include <nmmintrin.h>
#       define popcnt(b) _mm_popcnt_u64(b)
#   endif
#   define popcnt_sparse(b) popcnt(b)
#endif 

/*
Intrinsic bsf
*/
#if defined(HAS_BSF) && defined(ARC_64BIT)
#   if defined(__GNUC__)
#       define bsf(b) __builtin_ctzll(b)
#       define bsr(b) (63 - __builtin_clzll(b))
#   elif defined(_WIN32)
#       include <intrin.h>
        FORCEINLINE int bsf(UBMP64 b) {
            unsigned long x;
            _BitScanForward64(&x, b);
            return (int) x;
        }
        FORCEINLINE int bsr(UBMP64 b) {
            unsigned long x;
            _BitScanReverse64(&x, b);
            return (int) x;
        }
#   endif
#endif

/*
Byte swap
*/
#if defined(__INTEL_COMPILER)
#   define bswap32(x)  _bswap(x)
#elif defined(_MSC_VER)
#   define bswap32(x)  _byteswap_ulong(x)
#elif defined(__GNUC__)
#   define bswap32(x)  __builtin_bswap32(x)
#endif

/*
cache line memory alignment (64 bytes)
*/
#include <cstdlib>
#define CACHE_LINE_SIZE  64

#if defined (__GNUC__)
#   define CACHE_ALIGN  __attribute__ ((aligned(CACHE_LINE_SIZE)))
#else
#   define CACHE_ALIGN __declspec(align(CACHE_LINE_SIZE))
#endif

template<typename T>
void aligned_reserve(T*& mem,const size_t& size) {
#ifndef __ANDROID__
    if((sizeof(T) & (sizeof(T) - 1)) == 0) {
#ifdef _WIN32
        if(mem) _aligned_free(mem);
        mem = (T*)_aligned_malloc(size * sizeof(T),CACHE_LINE_SIZE);
#else
        if(mem) free(mem);
        posix_memalign((void**)&mem,CACHE_LINE_SIZE,size * sizeof(T));
#endif
    } else 
#endif
    {
        if(mem) free(mem);
        mem = (T*) malloc(size * sizeof(T));
    }
}

template<typename T>
void aligned_free(T*& mem) {
    if((sizeof(T) & (sizeof(T) - 1)) == 0) {
#ifdef _WIN32
        if(mem) _aligned_free(mem);
#else
        if(mem) free(mem);
#endif
    } else {
        if(mem) free(mem);
    }
    mem = 0;
}
/*
Prefetch
*/
#if defined(HAS_PREFETCH)
#   include <xmmintrin.h>
#   define PREFETCH_T0(addr) _mm_prefetch(((char *)(addr)),_MM_HINT_T0);
#else
#   define PREFETCH_T0(addr)
#endif
/*
* Threads
*/
#if defined _WIN32
#   include <process.h>
#   define pthread_t HANDLE
#   define t_create(h,f,p)  h=(HANDLE)_beginthread(f,0,(void*)p)
#   define t_join(h)      WaitForSingleObject(h,INFINITE)
#   define t_sleep(x)     Sleep(x)
#   define t_yield()      SwitchToThread()
#   define t_pause()      YieldProcessor()
#else
#   include <pthread.h>
#   define t_create(h,f,p)  pthread_create(&h,0,(void*(*)(void*))&f,(void*)p)
#   define t_join(h)      pthread_join((pthread_t)h,0)
#   define t_sleep(x)     usleep((x) * 1000)
#   define t_yield()      sched_yield()
#   define t_pause()      asm volatile("pause\n": : :"memory")
#endif
#if defined __ANDROID__
#   undef t_pause
#   define t_pause()
#endif
/*
*locks
*/
#if defined PARALLEL
#    define VOLATILE volatile
#    if defined _MSC_VER
#       define l_set(x,v) InterlockedExchange((unsigned*)&(x),v)
#       define l_add(x,v) InterlockedExchangeAdd((unsigned*)&(x),v)
#       define l_set16(x,v) InterlockedExchange16((short*)&(x),v)
#       define l_add16(x,v) InterlockedExchangeAdd16((short*)&(x),v)
#       define l_and16(x,v) InterlockedAnd16((short*)&(x),v)
#       define l_or16(x,v) InterlockedOr16((short*)&(x),v)
#       define l_set8(x,v) InterlockedExchange8((char*)&(x),v)
#       define l_add8(x,v) InterlockedExchangeAdd8((char*)&(x),v)
#       define l_and8(x,v) InterlockedAnd8((char*)&(x),v)
#       define l_or8(x,v) InterlockedOr8((char*)&(x),v)
#    else
#       define l_set(x,v) __sync_lock_test_and_set(&(x),v)
#       define l_add(x,v) __sync_fetch_and_add(&(x),v)
#       define l_set16(x,v) __sync_lock_test_and_set((short*)&(x),v)
#       define l_add16(x,v) __sync_fetch_and_add((short*)&(x),v)
#       define l_and16(x,v) __sync_fetch_and_and((short*)&(x),v)
#       define l_or16(x,v) __sync_fetch_and_or((short*)&(x),v)
#       define l_set8(x,v) __sync_lock_test_and_set((char*)&(x),v)
#       define l_add8(x,v) __sync_fetch_and_add((char*)&(x),v)
#       define l_and8(x,v) __sync_fetch_and_and((char*)&(x),v)
#       define l_or8(x,v) __sync_fetch_and_or((char*)&(x),v)
#    endif
#    if defined OMP
#       include <omp.h>
#       define LOCK          omp_lock_t
#       define l_create(x)   omp_init_lock(&x)
#       define l_try_lock(x) omp_set_lock(&x)
#       define l_lock(x)     omp_test_lock(&x)
#       define l_unlock(x)   omp_unset_lock(&x)
inline void l_barrier() { 
#       pragma omp barrier 
}
#    else
#       ifdef USE_SPINLOCK
#           define LOCK VOLATILE int
#           define l_create(x)   ((x) = 0)
#           define l_try_lock(x) (l_set(x,1) != 0)
#           define l_lock(x)     while(l_try_lock(x)) {while((x) != 0) t_pause();}
#           define l_unlock(x)   ((x) = 0)
#       else
#           if defined _WIN32
#               define LOCK CRITICAL_SECTION
#               define l_create(x)   InitializeCriticalSection(&x)
#               define l_try_lock(x) TryEnterCriticalSection(&x)
#               define l_lock(x)     EnterCriticalSection(&x)
#               define l_unlock(x)   LeaveCriticalSection(&x)  
#           else
#               define LOCK pthread_mutex_t
#               define l_create(x)   pthread_mutex_init(&(x),0)
#               define l_try_lock(x) pthread_mutex_trylock(&(x))
#               define l_lock(x)     pthread_mutex_lock(&(x))
#               define l_unlock(x)   pthread_mutex_unlock(&(x))
#           endif
#       endif
#    endif
#else
#    define VOLATILE
#    define LOCK int
#    define l_create(x)
#    define l_lock(x)
#    define l_try_lock(x) (1)
#    define l_unlock(x)
#    define l_barrier()
#    define l_set(x,v) ((x) = v)
#    define l_add(x,v) ((x) += v)
#    define l_set16(x,v) ((x) = v)
#    define l_add16(x,v) ((x) += v)
#    define l_and16(x,v) ((x) &= v)
#    define l_or16(x,v) ((x) |= v)
#    define l_set8(x,v) ((x) = v)
#    define l_add8(x,v) ((x) += v)
#    define l_and8(x,v) ((x) &= v)
#    define l_or8(x,v) ((x) |= v)
#endif
/*
* Performance counters
*/
#ifdef _WIN32
typedef LARGE_INTEGER TIMER;
#define get_perf(x)  QueryPerformanceCounter(&x)
inline double get_diff(TIMER s,TIMER e) {
    TIMER freq; 
    QueryPerformanceFrequency( &freq );  
    return (e.QuadPart - s.QuadPart)/(double(freq.QuadPart) / 1e9);
}
#else
#include <ctime>
typedef struct timespec TIMER;
#define get_perf(x)  clock_gettime(CLOCK_MONOTONIC,&x)
inline double get_diff(TIMER s,TIMER e) {
    return (e.tv_sec - s.tv_sec) * 1e9 + (e.tv_nsec - s.tv_nsec);
}
#endif
/*
*optional compilation
*/
#ifdef PARALLEL
#    define SMP_CODE(x) x
#else
#    define SMP_CODE(x)
#endif
#ifdef CLUSTER
#    define CLUSTER_CODE(x) x
#else
#    define CLUSTER_CODE(x)
#endif
#ifdef _DEBUG
#    define DEBUG_CODE(x) x
#else
#    define DEBUG_CODE(x)
#endif

/*
end
*/
#endif
