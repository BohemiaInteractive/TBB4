/*
    Copyright (c) 2005-2016 Intel Corporation

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.




*/

#include "TypeDefinitions.h" // Customize.h and proxy.h get included
#include "tbbmalloc_internal_api.h"

#include "../tbb/tbb_assert_impl.h" // Out-of-line TBB assertion handling routines are instantiated here.

#undef UNICODE

#if USE_PTHREAD
#include <dlfcn.h> // dlopen
#elif USE_WINTHREAD
#include "tbb/machine/windows_api.h"
#endif

#include "tbb/scalable_allocator.h"

namespace rml {
namespace internal {

#if TBB_USE_DEBUG
#define DEBUG_SUFFIX "d"
#else
#define DEBUG_SUFFIX
#endif /* TBB_USE_DEBUG */

#if __TBB_x86_64
#define PLATFORM_SUFFIX "_x64"
#else
#define PLATFORM_SUFFIX
#endif /* __TBB_x86_64 */

// MALLOCLIB_NAME is the name of the TBB memory allocator library.
#if _WIN32||_WIN64
#define MALLOCLIB_NAME "tbb4malloc_bi" PLATFORM_SUFFIX DEBUG_SUFFIX ".dll"
#elif __APPLE__
#define MALLOCLIB_NAME "tbb4malloc_bi" PLATFORM_SUFFIX DEBUG_SUFFIX ".dylib"
#elif __FreeBSD__ || __NetBSD__ || __sun || _AIX || __ANDROID__
#define MALLOCLIB_NAME "tbb4malloc_bi" PLATFORM_SUFFIX DEBUG_SUFFIX ".so"
#elif __linux__
#define MALLOCLIB_NAME "tbb4malloc_bi" PLATFORM_SUFFIX DEBUG_SUFFIX  __TBB_STRING(.so.TBB_COMPATIBLE_INTERFACE_VERSION)
#else
#error Unknown OS
#endif

void init_tbbmalloc() {
#if DO_ITT_NOTIFY
    MallocInitializeITT();
#endif

/* Preventing TBB allocator library from unloading to prevent
   resource leak, as memory is not released on the library unload.
*/
#if USE_WINTHREAD && !__TBB_SOURCE_DIRECTLY_INCLUDED && !__TBB_WIN8UI_SUPPORT
    // Prevent Windows from displaying message boxes if it fails to load library
    UINT prev_mode = SetErrorMode (SEM_FAILCRITICALERRORS);
    HMODULE lib;
    BOOL ret = GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                 |GET_MODULE_HANDLE_EX_FLAG_PIN,
                                 (LPCTSTR)&scalable_malloc, &lib);
    MALLOC_ASSERT(lib && ret, "Allocator can't find itself.");
    SetErrorMode (prev_mode);
#endif /* USE_PTHREAD && !__TBB_SOURCE_DIRECTLY_INCLUDED */
}

#if !__TBB_SOURCE_DIRECTLY_INCLUDED
#if USE_WINTHREAD
extern "C" BOOL WINAPI DllMain( HINSTANCE /*hInst*/, DWORD callReason, LPVOID )
{

    if (callReason==DLL_THREAD_DETACH)
    {
        __TBB_mallocThreadShutdownNotification();
    }
    else if (callReason==DLL_PROCESS_DETACH)
    {
        __TBB_mallocProcessShutdownNotification();
    }
    return TRUE;
}
#else /* !USE_WINTHREAD */
struct RegisterProcessShutdownNotification {
// Work around non-reentrancy in dlopen() on Android
#if !__TBB_USE_DLOPEN_REENTRANCY_WORKAROUND
    RegisterProcessShutdownNotification() {
        // prevents unloading, POSIX case
        dlopen(MALLOCLIB_NAME, RTLD_NOW);
    }
#endif /* !__TBB_USE_DLOPEN_REENTRANCY_WORKAROUND */
    ~RegisterProcessShutdownNotification() {
        __TBB_mallocProcessShutdownNotification();
    }
};

static RegisterProcessShutdownNotification reg;
#endif /* !USE_WINTHREAD */
#endif /* !__TBB_SOURCE_DIRECTLY_INCLUDED */

} } // namespaces

#if (_WIN32 || _WIN64) && __TBB_DYNAMIC_LOAD_ENABLED

#define DLL_EXPORT __declspec(dllexport)

extern "C" {
  DLL_EXPORT size_t __stdcall MemTotalCommitted() {return scalable_footprint();}
  DLL_EXPORT size_t __stdcall MemTotalReserved() {return scalable_footprint();}
  DLL_EXPORT size_t __stdcall MemFlushCache(size_t size)
  {
    //return (scalable_allocation_command(TBBMALLOC_CLEAN_THREAD_BUFFERS, nullptr) == TBBMALLOC_OK) ? size : 0;
    return size; // Caches are flushed automatically internally by TBB so just tell Arma we're done here
  }
  DLL_EXPORT void __stdcall  MemFlushCacheAll() { scalable_allocation_command(TBBMALLOC_CLEAN_ALL_BUFFERS, nullptr); }
  DLL_EXPORT size_t __stdcall MemSize(void *mem) { return scalable_msize(mem); }
  DLL_EXPORT void * __stdcall MemAlloc(size_t size) { return scalable_malloc(size); }
  DLL_EXPORT void  __stdcall MemFree(void *mem) { scalable_free(mem); }

  DLL_EXPORT size_t __stdcall MemSizeA(void *mem, size_t aligment) { return scalable_msize(mem); }
  DLL_EXPORT void * __stdcall MemAllocA(size_t size, size_t aligment) { return scalable_aligned_malloc(size, aligment); }
  DLL_EXPORT void  __stdcall MemFreeA(void *mem) { scalable_aligned_free(mem); }

  DLL_EXPORT void  __stdcall EnableHugePages() { enable_huge_pages(); }
}

#endif //(_WIN32 || _WIN64) && __TBB_DYNAMIC_LOAD_ENABLED

#if __TBB_ipf
/* It was found that on IA-64 architecture inlining of __TBB_machine_lockbyte leads
   to serious performance regression with ICC. So keep it out-of-line.

   This code is copy-pasted from tbb_misc.cpp.
 */
extern "C" intptr_t __TBB_machine_lockbyte( volatile unsigned char& flag ) {
    tbb::internal::atomic_backoff backoff;
    while( !__TBB_TryLockByte(flag) ) backoff.pause();
    return 0;
}
#endif
