#pragma once
#include <memory>
#include <string>
#include <cstdio>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

#if defined(_MSC_VER)
#define MEGU_GET_SP() (_AddressOfReturnAddress())
#else 
#define MEGU_GET_SP() (__builtin_frame_address(0))  
#endif

namespace megu {

	using Word = uintptr_t;

	struct GarbageCollectorImpl;

	// !WARNING! sometimes this will NOT work on release builds because not all references are stored in the stack
	//due to compiler optimizations

	//TODO add custom allocator support 
	//every object can save its allocator since its responsible for its allocation and deallocation
	// example : 
	/*
		class IGcAllocator {
		public:
			virtual char* allocate(std::size_t bytes,std::size_t alignment) = 0;
			virtual void deallocate(void* block,std::size_t bytes, std::size_t alignent)noexcept = 0;
		};

		struct DefaultGcAllocator final: public IGcAllocator {

			char* allocate(std::size_t bytes, std::size_t alignment) final override {
				return static_cast<char*>(::operator new(bytes, std::align_val_t(alignment)));
			};
			void deallocate(void* block,std::size_t,std::size_t alignment)noexcept final override {
				return ::operator delete(block, std::align_val_t(alignment));
			};
		};
	*/

	struct GarbageCollector {
		GarbageCollector(const Word* rsp);
		~GarbageCollector();

		void* Malloc(std::size_t bytes, std::size_t align = __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
			return AllocateObject(bytes, align, nullptr);
		}
		void* Calloc(std::size_t n, std::size_t size, std::size_t align = __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
			void* data = Malloc(n * size, align);
			std::memset(data, 0, n * size);
			return data;
		}

		void MarkReachable(void const*)const;
		void MarkUnreachable(void const*)const;
		void MarkKeepAlive(void const*)const;
		void UnmarkKeepAlive(void const*)const;

		void  Collect();
		void  Free(void* data);
		void  FreeAll();
		std::string DumpUsage()const;

		template<typename T, typename ...Args>
		T* NewObject(Args&&...args) {
			void(*dtor)(void*, std::size_t)noexcept = nullptr;
			if constexpr (std::is_trivially_destructible_v<T>) {
				dtor = nullptr;
			}
			else {
				static_assert(std::is_nothrow_destructible_v<T>, "Destructor should no noexcept");
				dtor = [](void* data, std::size_t) noexcept {
					reinterpret_cast<T*>(data)->~T();
				};
			}
			T* data = std::launder(reinterpret_cast<T*>(AllocateObject(sizeof(T), alignof(T), dtor)));
			new(data) T(std::forward<Args>(args)...);
			return data;
		}

		template<typename T>
		T* NewArray(std::size_t num, std::size_t alingment = alignof(T)) {
			void(*dtor)(void*, std::size_t)noexcept = nullptr;
			if constexpr (std::is_trivially_destructible_v<T>) {
				dtor = nullptr;
			}
			else {
				static_assert(std::is_nothrow_destructible_v<T>, "Destructor should no noexcept");
				dtor = [](void* data, std::size_t nb) noexcept {
					T* arr = reinterpret_cast<T*>(data);
					std::size_t const numel = nb / sizeof(T);
					for (size_t i = 0; i < numel; ++i) {
						arr[i].~T();
					}
				};
			}
			void* buffer = AllocateObject(sizeof(T) * num, alingment, dtor);
			return std::launder(reinterpret_cast<T*>(buffer));
		}

	private: 
		char* AllocateObject(std::size_t nbytes, std::size_t align, void(*dtor)(void*, std::size_t)noexcept);

		std::unique_ptr<GarbageCollectorImpl> pimpl_;
	};

	

#define MEGU_createGC() megu::GarbageCollector(std::launder(reinterpret_cast<uintptr_t const*>(MEGU_GET_SP())))

}//end megu
