#ifndef __MY_TYPES__
#define __MY_TYPES__

/*
Platform specific defines
*/
#define ARC_64BIT
#define HAS_POPCNT
#define HAS_PREFETCH
#define PARALLEL
#define USE_SPINLOCK

/*
int types
*/
#ifdef _MSC_VER
typedef __int8 int8_t;
typedef unsigned __int8 uint8_t;
typedef __int16 int16_t;
typedef unsigned __int16 uint16_t;
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
#else
#    include <inttypes.h>
#endif

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
#    define UINT64(x) (x##ui64)
#    define FMTU64   "%016I64x"
#    define FMT64    "%I64d"
#    define FMT64W   "%20I64d"
#	 define GETPID()  _getpid()
#else
#    include <unistd.h>
#    define CDECL
#    ifdef ARC_64BIT
#        define UINT64
#    else
#        define UINT64(x) (x##ULL)
#    endif
#    define FMTU64     "%016llx"
#    define FMT64      "%lld"
#    define FMT64W     "%20lld"
#	 define GETPID()  getpid()
#endif

/*
Force inline
*/
#if defined (_MSC_VER)
#  define FORCEINLINE  __forceinline
#elif defined (__GNUC__)
#  define FORCEINLINE  __inline __attribute__((always_inline))
#else
#  define FORCEINLINE  __inline
#endif

/*
Intrinsic popcnt
*/
#if defined(HAS_POPCNT) && defined(ARC_64BIT)
#   if defined (__GNUC__)
#       define popcnt(x)								\
	({													\
	typeof(x) __ret;									\
	__asm__("popcnt %1, %0" : "=r" (__ret) : "r" (x));	\
	__ret;							                    \
})
#   elif defined(_MSC_VER) && defined(__INTEL_COMPILER)
#       include <nmmintrin.h>
#       define popcnt(b) _mm_popcnt_u64(b)
#   else
#       include<intrin.h>
#       define popcnt(b) __popcnt64(b)
#   endif
#   define popcnt_sparse(b) popcnt(b)
#endif 

/*
Byte swap
*/
#if defined (__GNUC__)
#	define bswap32(x)  __builtin_bswap32(x)
#elif defined(_MSC_VER) && defined(__INTEL_COMPILER)
#	define bswap32(x)  _bswap(x)
#else
#	define bswap32(x)  _byteswap_ulong(x)
#endif

/*
cache line memory alignment (64 bytes)
*/
#include <cstdlib>
#define CACHE_LINE_SIZE  64

#if defined (__GNUC__)
#	define CACHE_ALIGN  __attribute__ ((aligned(CACHE_LINE_SIZE)))
#else
#	define CACHE_ALIGN __declspec(align(CACHE_LINE_SIZE))
#endif

template<typename T>
void aligned_reserve(T*& mem,const size_t& size) {
	if((sizeof(T) & (sizeof(T) - 1)) == 0) {
#ifdef _WIN32
		if(mem) _aligned_free(mem);
		mem = (T*)_aligned_malloc(size * sizeof(T),CACHE_LINE_SIZE);
#else
		if(mem) free(mem);
		posix_memalign((void**)&mem,CACHE_LINE_SIZE,size * sizeof(T));
#endif
	} else {
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
#	include <xmmintrin.h>
#	define PREFETCH_T0(addr) _mm_prefetch(((char *)(addr)),_MM_HINT_T0);
#else
#	define PREFETCH_T0(addr)
#endif
/*
* Threads
*/
#if defined _WIN32
#	include <process.h>
#	define t_create(f,p)  _beginthread(f,0,(void*)p)
#	define t_sleep(x)     Sleep(x)
#	define t_yield()	  SwitchToThread()
#else
#	include <pthread.h>
#	define t_create(f,p)  {pthread_t t = 0; pthread_create(&t,0,(void*(*)(void*))&f,(void*)p);}
#	define t_sleep(x)     usleep((x) * 1000)
#	define t_yield()	  pthread_yield()
#endif
/*
*locks
*/
#if defined PARALLEL
#	 if defined OMP
#	    include <omp.h>
#		define LOCK          omp_lock_t
#		define l_create(x)   omp_init_lock(&x)
#		define l_lock(x)     omp_set_lock(&x)
#		define l_unlock(x)   omp_unset_lock(&x)
inline void l_barrier() { 
#		pragma omp barrier 
}
#    elif defined _WIN32
#		define VOLATILE volatile
#       ifdef USE_SPINLOCK
#             define LOCK VOLATILE int
#             define l_create(x)   ((x) = 0)
#             define l_lock(x)     while(InterlockedExchange((LPLONG)&(x),1) != 0) {while((x) != 0);}
#             define l_unlock(x)   ((x) = 0)
#       else
#             define LOCK CRITICAL_SECTION
#             define l_create(x)   InitializeCriticalSection(&x)
#             define l_lock(x)     EnterCriticalSection(&x)
#             define l_unlock(x)   LeaveCriticalSection(&x)
#       endif   
#    else
#			  define VOLATILE volatile
#			  define LOCK pthread_mutex_t
#			  define l_create(x)   pthread_mutex_init(&(x),0)
#			  define l_lock(x)     pthread_mutex_lock(&(x))
#			  define l_unlock(x)   pthread_mutex_unlock(&(x))
#    endif
#else
#    define VOLATILE
#    define LOCK int
#    define l_create(x)
#    define l_lock(x)
#    define l_unlock(x)
#	 define l_barrier()
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
#	 define SMP_CODE(x) x
#else
#    define SMP_CODE(x)
#endif
#ifdef CLUSTER
#	 define CLUSTER_CODE(x) x
#else
#    define CLUSTER_CODE(x)
#endif
#ifdef _DEBUG
#	 define DEBUG_CODE(x) x
#else
#    define DEBUG_CODE(x)
#endif

/*
end
*/
#endif
