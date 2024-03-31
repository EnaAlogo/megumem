#pragma once
#include <assert.h>
#include <string>
#include <stdexcept>

#ifdef MEGU_USE_CONSTEXPR_ALLOC
#include <new>
#endif

#if defined(MEGU_USE_CONSTEXPR_ALLOC) && !defined(_MSC_VER)
#define MEGU_CONSTEXPR constexpr
#else
#define MEGU_CONSTEXPR 
#endif


#ifdef MEGU_DEBUG_LOGS
#include <iostream>
#include <cstring> 
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef NOKERNEL
#define NOKERNEL
#endif
#ifndef NOUSER
#define NOUSER
#endif
#ifndef NOSERVICE
#define NOSERVICE
#endif
#ifndef NOSOUND
#define NOSOUND
#endif
#ifndef NOMCX
#define NOMCX
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOMSG
#define NOMSG
#endif
#ifndef NOMB
#define NOMB
#endif
#ifndef NOCLIPBOARD
#define NOCLIPBOARD
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#if  defined(_WIN32) 
namespace megu::detail {
	inline std::string GetLastErrorMsg() {
		DWORD errorMessageID = ::GetLastError();
		LPSTR messageBuffer = nullptr;
		//Ask Win32 to give us the string version of that message ID.
		//The parameters we pass in, tell Win32 to create the buffer that holds the message for us (because we don't yet know how long the message string will be).
		size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);
		//Copy the error message into a std::string.
		if (size == 0) {
			DWORD error = ::GetLastError();
			return "Failed to retrieve error message. Error code: " + std::to_string(error);
		}
		auto out = std::string(messageBuffer, size);
		//Free the Win32's string's buffer.
		LocalFree(messageBuffer);
		return out;
	}
}

#endif // MEGU_USE_CONSTEXPR_ALLOC && _WIN32

namespace megu {
	inline int64_t GetPageSize() {
		static int64_t pg = [] {
			int64_t pg;
#ifdef _WIN32
			SYSTEM_INFO systemInfo;
			GetSystemInfo(&systemInfo);
			pg = systemInfo.dwPageSize;
#else 
			pg = sysconf(_SC_PAGE_SIZE);
#endif
			return pg < 0 ? (1 << 12) : pg;//default to 4kb
		}();
		return pg;
	}

}

namespace megu::detail {
#ifdef _WIN32
	enum class Protection_t {
		READ = PAGE_READONLY,
		READ_WRITE = PAGE_READWRITE,
		READ_EXEC = PAGE_EXECUTE_READ,
		EXEC_READWRITE = PAGE_EXECUTE_READWRITE
	};
#else
	enum class Protection_t {
		READ = PROT_READ,
		READ_WRITE = PROT_WRITE,
		READ_EXEC = (PROT_READ | PROT_EXEC),
		EXEC_READWRITE = (PROT_READ | PROT_EXEC | PROT_WRITE)
	};
#endif
	inline void SysProtect(void* mem, size_t size, Protection_t perms) {
#ifdef _WIN32
		DWORD oldProtect;
		if (!VirtualProtect(mem, size, static_cast<int>(perms), &oldProtect)) {
#ifdef MEGU_DEBUG_LOGS
			std::cerr << "Virtualprotect failed with error " << GetLastErrorMsg()<<"\n";
#endif
			throw std::runtime_error("VirtualProtect failed");
		}
#else
		int ret = mprotect(mem, size, static_cast<int>(perms));
		if (ret != 0) {
#ifdef MEGU_DEBUG_LOGS
			std::cerr << "mprotect failed with error " << strerror(ret) << "\n";
#endif
			throw std::runtime_error("mprotect failed");
		}
#endif
	}

#ifdef MEGU_USE_CONSTEXPR_ALLOC
	inline MEGU_CONSTEXPR void* SysAlloc(size_t bytes) { 
		return ::operator new(bytes);
	}
	inline MEGU_CONSTEXPR void* SysAlloc(size_t bytes,std::nothrow_t)noexcept {
		return ::operator new(bytes, std::nothrow);
	}
	inline MEGU_CONSTEXPR void* SysAllocAligned(size_t bytes,size_t alignment) {
		return ::operator new(bytes, std::align_val_t(alignment));
	}
	inline MEGU_CONSTEXPR void* SysAllocAligned(size_t bytes, size_t alignment,std::nothrow_t) noexcept {
		return ::operator new(bytes, std::align_val_t(alignment), std::nothrow);
	}

	inline MEGU_CONSTEXPR void SysFreeAligned(void* at, size_t bytes, size_t alignment) noexcept {//this is always nothrow
		return ::operator delete(at, std::align_val_t(alignment));
	}

	inline MEGU_CONSTEXPR void SysFreeAligned(void* at, size_t bytes, size_t alignment,std::nothrow_t) noexcept {
		return ::operator delete(at, std::align_val_t(alignment), std::nothrow);
	}

	inline  void SysFree(void* mem, size_t) noexcept {//this is always nothrow
		return ::operator delete(mem);
	}
	inline MEGU_CONSTEXPR void SysFree(void* mem, size_t,std::nothrow_t) noexcept {//this is always nothrow
	   return ::operator delete(mem,std::nothrow);
	}

#else // MEGU_USE_CONSTEXPR_ALLOC

	inline void* SysAlloc(size_t bytes) {
		void* at = nullptr;
#ifdef  _WIN32 
		LPVOID ptr = VirtualAllocEx(
			GetCurrentProcess(),
			nullptr,
			bytes,
			MEM_COMMIT | MEM_RESERVE,
			PAGE_READWRITE
		);
		if (!ptr || ptr == INVALID_HANDLE_VALUE) {
#ifdef MEGU_DEBUG_LOGS
			std::cerr << GetLastErrorMsg() <<"\n";
#endif // MEGU_DEBUG_LOGS
			throw std::bad_alloc();
		}
		at = ptr;
#else // _WIN32
		at = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (at == MAP_FAILED) {
#ifdef MEGU_DEBUG_LOGS
			std::cerr << "mmap failed, error" << strerror(errno) <<"\n";
#endif // MEGU_DEBUG_LOGS
			throw std::bad_alloc();
		}
#endif //  _WIN32 
		return at;
	}
	inline void* SysAlloc(size_t bytes,std::nothrow_t)noexcept {
		void* at = nullptr;
#ifdef  _WIN32 
		LPVOID ptr = VirtualAllocEx(
			GetCurrentProcess(),
			nullptr,
			bytes,
			MEM_COMMIT | MEM_RESERVE,
			PAGE_READWRITE
		);
		if (!ptr || ptr == INVALID_HANDLE_VALUE) {
#ifdef MEGU_DEBUG_LOGS
			std::cerr <<"SysAlloc failed, error "<< GetLastErrorMsg() << "\n";
#endif // MEGU_DEBUG_LOGS
			return nullptr;
		}
		at = ptr;
#else // _WIN32
		at = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (at == MAP_FAILED) {
#ifdef MEGU_DEBUG_LOGS
			std::cerr << "mmap failed, error " << strerror(errno) << "\n";
#endif // MEGU_DEBUG_LOGS
			return nullptr;
		}
#endif //  _WIN32 
		return at;
	}

	inline void* SysAllocAligned(size_t nbytes, size_t align) {
		if (nbytes == 0) {//malloc returns a 0 size allocations whatever that means 
			return nullptr;
		}
		if (((ptrdiff_t)nbytes) < 0) {
#ifdef MEGU_DEBUG_LOGS
			std::cerr << "SysAllocAligned() has been called with a negative number\n";
#endif //MEGU_DEBUG_LOGS
			throw std::bad_alloc();
		}
		void* data = nullptr;
#ifdef __ANDROID__
		data = memalign(align, nbytes);
		if (!data) {
#ifdef MEGU_DEBUG_LOGS
			std::cerr << "SysAllocAligned failed not enough memory to reserve "
				<< nbytes << " bytes with " << align << " alignment\n";
#endif //MEGU_DEBUG_LOGS
			throw std::bad_alloc();
		}
#elif defined(_MSC_VER) // __ANDROID__
		data = _aligned_malloc(nbytes, align);
		if (!data) {
#ifdef MEGU_DEBUG_LOGS
			std::cerr << "SysAllocAligned failed to reserve "
				<< nbytes << " bytes with " << align << " alignment";
			std::cerr << "\n Error : " << GetLastErrorMsg()<<"\n";
#endif //MEGU_DEBUG_LOGS
			throw std::bad_alloc();
		}
#else // __ANDOIRD__
		int err = posix_memalign(&data, align, nbytes);

		if (!data) {
#ifdef MEGU_DEBUG_LOGS
			std::cerr << "SysAllocAligned failed to reserve "
				<< nbytes << " bytes with " << align << " alignment";
			std::cerr << "\nError code : "
				<< err << " (" << strerror(err) << ")\n";
#endif //MEGU_DEBUG_LOGS
			throw std::bad_alloc();
		}

#endif // __ANDROID__
		return data;
	}

	inline void* SysAllocAligned(size_t nbytes, size_t align,std::nothrow_t)noexcept {
		if (nbytes == 0) {//malloc returns a 0 size allocations whatever that means 
			return nullptr;
		}
		if (((ptrdiff_t)nbytes) < 0) {
#ifdef MEGU_DEBUG_LOGS
			std::cerr << "SysAllocAligned() has been called with a negative number\n";
#endif //MEGU_DEBUG_LOGS
			return nullptr;
		}
		void* data = nullptr;
#ifdef __ANDROID__
		data = memalign(align, nbytes);
		if (!data) {
#ifdef MEGU_DEBUG_LOGS
			std::cerr << "SysAllocAligned failed not enough memory to reserve "
				<< nbytes << " bytes with " << align << " alignmet\n";
#endif //MEGU_DEBUG_LOGS
			return nullptr;
		}
#elif defined(_MSC_VER) // __ANDROID__
		data = _aligned_malloc(nbytes, align);
		if (!data) {
#ifdef MEGU_DEBUG_LOGS
			std::cerr << "SysAllocAligned failed to reserve "
				<< nbytes << " bytes with " << align << " alignmet";
			std::cerr << "\n Error : " << GetLastErrorMsg() << "\n";
#endif //MEGU_DEBUG_LOGS
			return nullptr;
		}
#else // __ANDOIRD__
		int err = posix_memalign(&data, align, nbytes);

		if (!data) {
#ifdef MEGU_DEBUG_LOGS
			std::cerr << "SysAllocAligned failed to reserve "
				<< nbytes << " bytes with " << align << " alignmet";
			std::cerr << "\nError code : "
				<< err << " (" << strerror(err) << ")\n";
#endif //MEGU_DEBUG_LOGS
			return nullptr;
		}

#endif // __ANDROID__
		return data;
	}

	inline void SysFree(void* mem, size_t b)noexcept {
#if defined(_WIN32)
		BOOL status = VirtualFreeEx(
			GetCurrentProcess(),
			mem,
			0,
			MEM_RELEASE
		);
#ifdef MEGU_DEBUG_LOG
		if (status == FALSE) {
			std::cerr << GetLastErrorMsg()<<"\n";
		}
#endif // MEGU_DEBUG_LOG
		assert(status != FALSE && "Freeing memory failed turn on MEGU_DEBUG_LOG for more information"); 
		if (status == FALSE) {
			exit(1);
	    }
#else // _WIN32
		int ret = munmap(mem, b);

#ifdef MEGU_DEBUG_LOG
		if (ret != 0) {
			std::cerr << strerror(errno)<<"\n";
		}
#endif //MEGU_DEBUG_LOG
		assert(ret == 0 && "Freeing memory failed turn on MEGU_DEBUG_LOG for more information"); 
		if (ret != 0) {
			exit(1);
		}
#endif // _WIN32
	}
	inline void SysFree(void* mem, size_t b, std::nothrow_t)noexcept {
		return SysFree(mem, b);
	}

	inline void SysFreeAligned(void* at, size_t bytes, size_t alignment) noexcept {
#ifdef _MSC_VER
		return _aligned_free(at);
#else 
		return free(at);
#endif
	}
	inline void SysFreeAligned(void* at, size_t bytes, size_t alignment, std::nothrow_t) noexcept {
		return SysFreeAligned(at, bytes, alignment);
	}
#endif

	
}//end megu