///|/ Copyright (c) Prusa Research 2020 - 2023 Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas, Lukáš Matěna @lukasmatena, Roman Beránek @zavorka
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifdef _WIN32
	#include <windows.h>
	#include <boost/nowide/convert.hpp>
#else
	// any posix system
	#include <pthread.h>
	#ifdef __APPLE__
		#include <pthread/qos.h>
	#endif // __APPLE__
#endif

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <random>
#include <thread>
#include <time.h>
#include <chrono>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/task_arena.h>

#include "Thread.hpp"
#include "Utils.hpp"
#include "LocalesUtils.hpp"

namespace Slic3r {

#ifdef _WIN32
// The new API is better than the old SEH style thread naming since the names also show up in crash dumpsand ETW traces.
// Because the new API is only available on newer Windows 10, look it up dynamically.

typedef HRESULT(__stdcall* SetThreadDescriptionType)(HANDLE, PCWSTR);
typedef HRESULT(__stdcall* GetThreadDescriptionType)(HANDLE, PWSTR*);

static bool 					s_SetGetThreadDescriptionInitialized = false;
static HMODULE					s_hKernel32 = nullptr;
static SetThreadDescriptionType s_fnSetThreadDescription = nullptr;
static GetThreadDescriptionType	s_fnGetThreadDescription = nullptr;

static bool WindowsGetSetThreadNameAPIInitialize()
{
	if (! s_SetGetThreadDescriptionInitialized) {
		// Not thread safe! It is therefore a good idea to name the main thread before spawning worker threads
		// to initialize 
		s_hKernel32 = LoadLibraryW(L"Kernel32.dll");
		if (s_hKernel32) {
			s_fnSetThreadDescription = (SetThreadDescriptionType)::GetProcAddress(s_hKernel32, "SetThreadDescription");
			s_fnGetThreadDescription = (GetThreadDescriptionType)::GetProcAddress(s_hKernel32, "GetThreadDescription");
		}
		s_SetGetThreadDescriptionInitialized = true;
	}
	return s_fnSetThreadDescription && s_fnGetThreadDescription;
}

#ifndef NDEBUG
	// Use the old way by throwing an exception, so at least in Debug mode the thread names are shown by the debugger.
	static constexpr DWORD MSVC_SEH_EXCEPTION_NAME_THREAD = 0x406D1388;

#pragma pack(push,8)
	typedef struct tagTHREADNAME_INFO
	{
		DWORD  dwType; 		// Must be 0x1000.
		LPCSTR szName; 		// Pointer to name (in user addr space).
		DWORD  dwThreadID; 	// Thread ID (-1=caller thread).
		DWORD  dwFlags; 	// Reserved for future use, must be zero.
	} THREADNAME_INFO;
#pragma pack(pop)

	static void WindowsSetThreadNameSEH(HANDLE hThread, const char* thread_name)
	{
		THREADNAME_INFO info;
		info.dwType 	= 0x1000;
		info.szName 	= thread_name;
		info.dwThreadID = ::GetThreadId(hThread);
		info.dwFlags 	= 0;
		__try {
			RaiseException(MSVC_SEH_EXCEPTION_NAME_THREAD, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}
#endif // NDEBUG

static bool WindowsSetThreadName(HANDLE hThread, const char *thread_name)
{
	if (! WindowsGetSetThreadNameAPIInitialize()) {
#ifdef NDEBUG
		return false;
#else // NDEBUG
		// Running on Windows 7 or old Windows 7 in debug mode,
		// inform the debugger about the thread name by throwing an SEH.
		WindowsSetThreadNameSEH(hThread, thread_name);
		return true;
#endif // NDEBUG
	}

	size_t len = strlen(thread_name);
	if (len < 1024) {
		// Allocate the temp string on stack.
		wchar_t buf[1024];
		s_fnSetThreadDescription(hThread, boost::nowide::widen(buf, 1024, thread_name));
	} else {
		// Allocate dynamically.
		s_fnSetThreadDescription(hThread, boost::nowide::widen(thread_name).c_str());
	}
	return true;
}

bool set_thread_name(std::thread &thread, const char *thread_name)
{
   	return WindowsSetThreadName(static_cast<HANDLE>(thread.native_handle()), thread_name);
}

bool set_thread_name(boost::thread &thread, const char *thread_name)
{
   	return WindowsSetThreadName(static_cast<HANDLE>(thread.native_handle()), thread_name);
}

bool set_current_thread_name(const char *thread_name)
{
    return WindowsSetThreadName(::GetCurrentThread(), thread_name);
}

std::optional<std::string> get_current_thread_name()
{
	if (! WindowsGetSetThreadNameAPIInitialize())
		return std::nullopt;

	wchar_t *ptr = nullptr;
	s_fnGetThreadDescription(::GetCurrentThread(), &ptr);
	return (ptr == nullptr) ? std::string() : boost::nowide::narrow(ptr);
}

#else // _WIN32

#ifdef __APPLE__

// Appe screwed the Posix norm.
bool set_thread_name(std::thread &thread, const char *thread_name)
{
// not supported
//   	pthread_setname_np(thread.native_handle(), thread_name);
	return false;
}

bool set_thread_name(boost::thread &thread, const char *thread_name)
{
// not supported	
//   	pthread_setname_np(thread.native_handle(), thread_name);
	return false;
}

bool set_current_thread_name(const char *thread_name)
{
	pthread_setname_np(thread_name);
	return true;
}

std::optional<std::string> get_current_thread_name()
{
// not supported	
//	char buf[16];
//	return std::string(thread_getname_np(buf, 16) == 0 ? buf : "");
	return std::nullopt;
}

#else

// posix
bool set_thread_name(std::thread &thread, const char *thread_name)
{
   	pthread_setname_np(thread.native_handle(), thread_name);
	return true;
}

bool set_thread_name(boost::thread &thread, const char *thread_name)
{
   	pthread_setname_np(thread.native_handle(), thread_name);
	return true;
}

bool set_current_thread_name(const char *thread_name)
{
	pthread_setname_np(pthread_self(), thread_name);
	return true;
}

std::optional<std::string> get_current_thread_name()
{
	char buf[16];
	return std::string(pthread_getname_np(pthread_self(), buf, 16) == 0 ? buf : "");
}

#endif

#endif // _WIN32

// To be called at the start of the application to save the current thread ID as the main (UI) thread ID.
static boost::thread::id g_main_thread_id;

void save_main_thread_id()
{
	g_main_thread_id = boost::this_thread::get_id();
}

// Retrieve the cached main (UI) thread ID.
boost::thread::id get_main_thread_id()
{
	return g_main_thread_id;
}

// Checks whether the main (UI) thread is active.
bool is_main_thread_active()
{
	return get_main_thread_id() == boost::this_thread::get_id();
}

#ifdef _DEBUGINFO
void parallel_for(size_t begin, size_t size, std::function<void(size_t)> process_one_item) {
    // TODO: sort the idx by difficulty (difficult first) (number of points, region, surfaces, .. ?)

    //For now, this is just use in debug mode, to be able toswitch from // to sequential withotu recompiling evrything.

    // normal step
    tbb::parallel_for(begin, size, [&process_one_item](size_t item_idx) { process_one_item(item_idx); });
    // if you need to debug without // stuff
    //for (size_t idx = begin; idx < size; ++idx) {
    //    process_one_item(idx);
    //}
}
void not_parallel_for(size_t begin, size_t size, std::function<void(size_t)> process_one_item) {
    for (size_t idx = begin; idx < size; ++idx) {
        process_one_item(idx);
    }
}
#endif

static thread_local ThreadData s_thread_data;
ThreadData& thread_data()
{
	return s_thread_data;
}

std::mt19937&   ThreadData::random_generator() {
    if (! m_random_generator_initialized) {
        std::random_device rd;
        m_random_generator.seed(rd()); //can also be initialized by clock() + std::this_thread::get_id().hash()
        m_random_generator_initialized = true;
    }
    return m_random_generator;
}

// Thread-safe function that returns a random number between 0 and max (inclusive, like rand()).
int safe_rand(int max) {
    std::mt19937 &generator = thread_data().random_generator();
    std::uniform_int_distribution<int> distribution(0, max);
    return distribution(generator);
}

// Spawn (n - 1) worker threads on Intel TBB thread pool and name them by an index and a system thread ID.
// Also it sets locale of the worker threads to "C" for the G-code generator to produce "." as a decimal separator.
void name_tbb_thread_pool_threads_set_locale()
{
	static bool initialized = false;
	if (initialized)
		return;
	initialized = true;

	// see GH issue #5661 PrusaSlicer hangs on Linux when run with non standard task affinity
	// TBB will respect the task affinity mask on Linux and spawn less threads than std::thread::hardware_concurrency().
//	const size_t nthreads_hw = std::thread::hardware_concurrency();
	const size_t nthreads_hw = tbb::this_task_arena::max_concurrency();
	size_t       nthreads    = nthreads_hw;
    if (thread_count) {
        nthreads = std::min(nthreads_hw, *thread_count);
    }

    enforce_thread_count(nthreads);

	size_t                  nthreads_running(0);
	std::condition_variable cv;
	std::mutex				cv_m;
	auto					master_thread_id = std::this_thread::get_id();
	auto					now = std::chrono::system_clock::now();
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, nthreads, 1),
        [&nthreads_running, nthreads, &master_thread_id, &cv, &cv_m, &now](const tbb::blocked_range<size_t> &range) {
        	assert(range.begin() + 1 == range.end());
			if (std::unique_lock<std::mutex> lk(cv_m);  ++nthreads_running == nthreads) {
				lk.unlock();
        		// All threads are spinning.
        		// Wake them up.
    			cv.notify_all();
        	} else {
        		// Wait for the last thread to wake the others.
                // here can be deadlock with the main that creates me.
               cv.wait_until(lk, now + std::chrono::milliseconds(50), [&nthreads_running, nthreads]{return nthreads_running == nthreads;});
        	}
        	auto thread_id = std::this_thread::get_id();
			if (thread_id == master_thread_id) {
				// The calling thread runs the 0'th task.
                //assert(range.begin() == 0);
			} else {
                //assert(range.begin() > 0);
				std::ostringstream name;
		        name << "slic3r_tbb_" << range.begin();
		        set_current_thread_name(name.str().c_str());
                // Set locales of the worker thread to "C".
                set_c_locales();
    		}
        });
}

void set_current_thread_qos()
{
#ifdef __APPLE__
	// OSX specific: Set Quality of Service to "user initiated", so that the threads will be scheduled to high performance
	// cores if available.
	// With QOS_CLASS_USER_INITIATED the worker threads drop priority once slicer loses user focus.
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif // __APPLE__
}

void ThreadData::tbb_worker_thread_set_c_locales()
{
//    static std::atomic<int> cnt = 0;
//    std::cout << "TBBLocalesSetter Entering " << cnt ++ << " ID " << std::this_thread::get_id() << "\n";
    if (! m_tbb_worker_thread_c_locales_set) {
        // Set locales of the worker thread to "C".
        set_c_locales();
        // OSX specific: Elevate QOS on Apple Silicon.
        set_current_thread_qos();
        m_tbb_worker_thread_c_locales_set = true;
    }
}

}
