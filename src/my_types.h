#ifndef __MY_TYPES__
#define __MY_TYPES__

/*
Platform specific defines for Windows
For others, they are set in Makefile
*/
#ifdef _MSC_VER
#   define ARC_64BIT
#   define HAS_POPCNT
#   define HAS_PREFETCH
#   define USE_SPINLOCK
#endif
#ifdef HAS_BSF
#   define HAS_BSF
#endif

/*
int types
*/
#include <stdint.h>

/*
Os stuff
*/
#ifdef _WIN32
#    include <windows.h>
#    include <process.h>
#    undef CDECL
#    define CDECL __cdecl
#    define GETPID()  _getpid()
#    define GETTID()  GetCurrentThreadId()
#else
#    include <unistd.h>
#    include <sys/syscall.h>
#    include <sys/mman.h>
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
        FORCEINLINE int bsf(uint64_t b) {
            unsigned long x;
            _BitScanForward64(&x, b);
            return (int) x;
        }
        FORCEINLINE int bsr(uint64_t b) {
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
#   define bswap64(x)  _bswap64(x)
#elif defined(_MSC_VER)
#   define bswap32(x)  _byteswap_ulong(x)
#   define bswap64(x)  _byteswap_uint64(x)
#elif defined(__GNUC__)
#   define bswap32(x)  __builtin_bswap32(x)
#   define bswap64(x)  __builtin_bswap64(x)
#endif

/*
cache line memory alignment (64 bytes)
*/
#include <cstdlib>
#define CACHE_LINE_SIZE  64
#define CACHE_ALIGN alignas(CACHE_LINE_SIZE)

template<typename T>
void aligned_free(T*& mem) {
#ifdef _WIN32
    if(mem) _aligned_free(mem);
#else
    if(mem) free(mem);
#endif
    mem = 0;
}

template<typename T, int ALIGNMENT = CACHE_LINE_SIZE, bool large_pages = false>
void aligned_reserve(T*& mem,const size_t& size) {
#ifdef __ANDROID__
    mem = (T*) memalign(ALIGNMENT,size * sizeof(T));
#elif defined(_WIN32)
    mem = (T*)_aligned_malloc(size * sizeof(T),ALIGNMENT);
#else
    posix_memalign((void**)&mem,ALIGNMENT,size * sizeof(T));
#if defined(MADV_HUGEPAGE)
    if(large_pages)
        madvise(mem,size * sizeof(T),MADV_HUGEPAGE);
#endif
#endif
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
#include<thread>
#include<chrono>
#include<atomic>
#include<mutex>
#include<condition_variable>

#define t_create(f,p)    std::thread(f,p)
#define t_join(h)        h.join()
#define t_yield()        std::this_thread::yield()

#if defined _WIN32
#   define t_pause()  YieldProcessor()
#   define t_sleep(x) Sleep(x)
#else
#   define t_sleep(x) usleep((x) * 1000)
#if defined __ANDROID__
#   define t_pause()
#else
#   define t_pause()  asm volatile("pause\n": : :"memory")
#endif
#endif

/*
*locks
*/
//atomic ops
#define l_set(x,v) x.exchange(v)
#define l_add(x,v) x.fetch_add(v)
#define l_and(x,v) x.fetch_and(v)
#define l_or(x,v) x.fetch_or(v)
#define l_barrier()
//conditional variable
#define COND std::condition_variable
#define c_create(x)
#define c_signal(x)   x.notify_all()
#define c_wait(x,l,p) x.wait(l,p)
//mutex
#define MUTEX std::mutex
#define m_create(x)
#define m_try_lock(x) x.trylock()
#define m_lock(x)     x.lock()
#define m_unlock(x)   x.unlock()
//spinlock
#ifdef USE_SPINLOCK
#   define LOCK std::atomic_int
#   define l_create(x)   ((x) = 0)
#   define l_try_lock(x) (l_set(x,1) != 0)
#   define l_lock(x)     while(l_try_lock(x)) {while((x) != 0) t_pause();}
#   define l_unlock(x)   ((x) = 0)
#else
#   define LOCK MUTEX
#   define l_create(x)   m_create(x)
#   define l_try_lock(x) m_try_lock(x)
#   define l_lock(x)     m_lock(x)
#   define l_unlock(x)   m_unlock(x)
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
*optional code compilation
*/
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

#endif
