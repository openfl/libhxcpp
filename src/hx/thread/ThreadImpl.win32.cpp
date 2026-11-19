#include <hxcpp.h>
#include <windows.h>
#include <memory>
#include <sstream>
#include <hx/thread/Thread.hpp>
#include "ThreadImpl.hpp"
#include <comdef.h>

namespace
{
	// Windows 10 1607 and later have these. Imported directly, they stopped a
	//  program loading on anything older, Windows 8.1 among them, whatever it
	//  did with threads (libhxcpp #456), so they are looked up once instead
	typedef HRESULT (WINAPI *GetThreadDescriptionFn)(HANDLE, PWSTR *);
	typedef HRESULT (WINAPI *SetThreadDescriptionFn)(HANDLE, PCWSTR);

	#ifdef HX_WINRT
	GetThreadDescriptionFn getThreadDescription() { return GetThreadDescription; }
	SetThreadDescriptionFn setThreadDescription() { return SetThreadDescription; }
	#else
	template<typename F> F kernelFunction(const char *inName)
	{
		HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
		return kernel ? reinterpret_cast<F>(reinterpret_cast<void *>(GetProcAddress(kernel, inName))) : nullptr;
	}

	GetThreadDescriptionFn getThreadDescription()
	{
		static GetThreadDescriptionFn fn = kernelFunction<GetThreadDescriptionFn>("GetThreadDescription");
		return fn;
	}

	SetThreadDescriptionFn setThreadDescription()
	{
		static SetThreadDescriptionFn fn = kernelFunction<SetThreadDescriptionFn>("SetThreadDescription");
		return fn;
	}
	#endif
}

String hx::thread::ThreadImpl_obj::getName()
{
	auto get = getThreadDescription();
	if (!get)
		return null();

	hx::EnterGCFreeZone();
	
	auto buffer = PWSTR{ nullptr };
	auto result = get(native->handle, &buffer);
	if (FAILED(result))
	{
		_com_error error(result);
	
		auto msg = error.ErrorMessage();
	
		hx::ExitGCFreeZone();
		hx::Throw(String::create(msg));
	}
	
	hx::ExitGCFreeZone();
	
	auto converted = String::create(buffer);
	
	LocalFree(buffer);
	
	return converted;
}

void hx::thread::ThreadImpl_obj::setName(const String& name)
{
	auto set = setThreadDescription();
	if (!set)
		return;

	hx::strbuf buffer;
	hx::EnterGCFreeZone();

	auto result = set(native->handle, name.wchar_str(&buffer));
	if (FAILED(result))
	{
		_com_error error(result);
	
		auto msg = error.ErrorMessage();
	
		hx::ExitGCFreeZone();
		hx::Throw(String::create(msg));
	}
	else
	{
		hx::ExitGCFreeZone();
	}
}

// A real handle: GetCurrentThread's pseudo-handle names whichever thread uses it
hx::thread::ThreadImpl_obj::Native::Native()
	: thread()
	, handle(OpenThread(THREAD_QUERY_LIMITED_INFORMATION | THREAD_SET_LIMITED_INFORMATION, FALSE, GetCurrentThreadId()))
	, ownsHandle(handle != nullptr)
{
	if (!handle)
		handle = GetCurrentThread();
}

hx::thread::ThreadImpl_obj::Native::~Native()
{
	if (ownsHandle)
		CloseHandle(handle);
}
