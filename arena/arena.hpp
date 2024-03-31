#pragma once
#ifndef MEGU_USE_LOGGING
#define MEGU_USE_LOGGING true
#endif
#ifndef MEGU_USE_CPPNEW    
#define MEGU_USE_CPPNEW ((__cplusplus >=  202002L) && !defined(_MSC_VER))  
#endif
#if MEGU_USE_CPPNEW == true 
#define MEGU_USE_CONSTEXPR_ALLOC
#endif
#if MEGU_USE_LOGGING == true
#define MEGU_DEBUG_LOGS
#endif
#include "alloc.hpp"
#include <vector>
#include <mutex>
#include <sstream>

namespace megu {
	namespace detail {
		struct region_t {
			constexpr region_t(region_t const& other)noexcept = delete;
			constexpr region_t& operator=(region_t const& other)noexcept = delete;

			constexpr region_t(region_t&& other) noexcept {
				private_move(std::move(other));
			}
			constexpr region_t& operator=(region_t&& other) noexcept {
				if (this != &other) {
					private_move(std::move(other));
				}
				return *this;
			}
			MEGU_CONSTEXPR region_t(std::size_t capacity = (1 << 12),//assume page size is 4kb
				std::size_t align = __STDCPP_DEFAULT_NEW_ALIGNMENT__)noexcept
				:cap_(capacity), size_(0), chunk_(nullptr), alignment_(align),
				allocs_(0)
			{
				if (!use_default_align()) {
					chunk_ = static_cast<char*>(detail::SysAllocAligned(capacity, align, std::nothrow));
				}
				else {
					chunk_ = static_cast<char*>(detail::SysAlloc(cap_, std::nothrow));
				}
			}

			~region_t() {
				if (chunk_ != nullptr) {
					if (!use_default_align()) {
						detail::SysFreeAligned(chunk_, cap_, alignment_, std::nothrow);
					}
					else {
						detail::SysFree(chunk_, cap_, std::nothrow);
					}
				}
			}

			[[nodiscard]]
			constexpr void* release()noexcept {
				void* ret = chunk_;
				size_ = 0;
				cap_ = 0;
				chunk_ = nullptr;
				return ret;
			}

			[[nodiscard]]
			constexpr bool in_region(void const* ptr)const noexcept {
				return ptr >= chunk_ && (ptr < (reinterpret_cast<char const*>(chunk_) + size_));
			}

			[[nodiscard]]
			constexpr bool is_valid()const noexcept {
				return chunk_ != nullptr;
			}

			[[nodiscard]]
			constexpr void* data()const noexcept {
				return chunk_;
			}

			[[nodiscard]]
			constexpr std::size_t size()const noexcept {
				return size_;
			}
			[[nodiscard]]
			constexpr std::size_t& size()noexcept {
				return size_;
			}

			[[nodiscard]]
			constexpr std::size_t capacity()const noexcept {
				return cap_;
			}

			[[nodiscard]]
			constexpr std::size_t alignment()const noexcept {
				return alignment_;
			}

			constexpr bool is_empty() const noexcept {
				return size_ == 0 || allocs_ == 0;
			}

			constexpr void clear() noexcept {
				allocs_ = 0;
				size_ = 0;
			}

			[[nodiscard]]
			constexpr void* get_chunk(std::size_t bytes_offset)const noexcept {
				return (chunk_)+bytes_offset;
			}

			[[nodiscard]]
			constexpr char* begin()const noexcept {
				return (chunk_)+size_;
			}
			[[nodiscard]]
			constexpr char* end()const noexcept {
				return (chunk_)+cap_;
			}


			[[nodiscard]]
#ifdef MEGU_USE_CONSTEXPR_ALLOC
			constexpr
#endif
				bool use_default_align()const noexcept {
#ifdef MEGU_USE_CONSTEXPR_ALLOC  
				return alignment_ <= __STDCPP_DEFAULT_NEW_ALIGNMENT__;
#else
				return alignment_ <= GetPageSize();
#endif // MEGU_USE_CONSTEXPR_ALLOC
			}

			constexpr uint32_t& nallocations()noexcept {
				return allocs_;
			}

		protected:
			constexpr void private_move(region_t&& other) {
				chunk_ = other.chunk_;
				other.chunk_ = nullptr;
				cap_ = other.cap_;
				other.cap_ = 0;
				size_ = other.size_;
				other.size_ = 0;
				alignment_ = other.alignment_;
				allocs_ = other.allocs_;
				other.allocs_ = 0;
			}
		private:


			std::size_t cap_;
			std::size_t size_;
			std::size_t alignment_;
			char* chunk_;
			uint32_t allocs_;
		};

		class ArenaBase {
		public:
			constexpr std::size_t NumRegions()noexcept {
				return regs_.size();
			}
			std::string DumpUsage() {
				return regs_.dump_usage(); 
			}

		protected:
			constexpr ArenaBase(std::size_t min_region_capacity = (1 << 12))
				:min_cap_(min_region_capacity), regs_() {}

			MEGU_CONSTEXPR void FreeUnusedRegions()noexcept {
				regs_.remove_unused();
			}
			MEGU_CONSTEXPR void FreeArena()noexcept {
				regs_.free_all();
			}
			MEGU_CONSTEXPR void ClearArena()noexcept {
				regs_.clear_all();
			}
			[[nodiscard]]
			MEGU_CONSTEXPR std::vector<void*> ReleaseArena() {
				return regs_.release_all();
			}
			[[nodiscard]]
			MEGU_CONSTEXPR void* ReleaseRegionContaining(void const* mem)noexcept {
				return regs_.release_region_containing(mem);
			}

			[[nodiscard]]
			MEGU_CONSTEXPR void* alloc_nothrow(std::size_t bytes, std::size_t align)noexcept {
				return regs_.try_alloc(bytes, align, min_cap_);
			}
			[[nodiscard]]
			MEGU_CONSTEXPR void* realloc_nothrow(void* mem, std::size_t olds, std::size_t news, std::size_t align)noexcept {
				return regs_.try_realloc(mem, olds, news, align, min_cap_);
			}

			MEGU_CONSTEXPR void dealloc(void* mem, std::size_t bytes, std::size_t align)noexcept {
				return regs_.dealloc(mem, bytes, align);
			}

		private:
			static constexpr uintptr_t _alignment_shift(const uintptr_t ptr, const std::size_t aling)noexcept {
				return ((~(ptr)) + 1) & (aling - 1);
			}
			static constexpr std::ptrdiff_t alignment_offset(const std::size_t alignment, const void* ptr)noexcept {
				const auto off = _alignment_shift(reinterpret_cast<uintptr_t>(ptr), alignment);
				return off == alignment ? 0 : off;
			}
			struct region_list_t {
				constexpr region_list_t(region_list_t const&) = delete;
				constexpr region_list_t(region_list_t&&) = delete;
				constexpr region_list_t& operator=(region_list_t const&) = delete;
				constexpr region_list_t& operator=(region_list_t&&) = delete;

				struct region_node_t : public detail::region_t {
					using detail::region_t::region_t;
					constexpr region_node_t(region_node_t const&) = delete;
					constexpr region_node_t(region_node_t&&) = delete;
					constexpr region_node_t& operator=(region_node_t const&) = delete;
					constexpr region_node_t& operator=(region_node_t&&) = delete;

					region_node_t* next_{ nullptr };
				};


				constexpr region_list_t()
					:size_(0), head_(nullptr) {}

				~region_list_t() {
					free_all();
				}

				MEGU_CONSTEXPR void dealloc(void const* mem, std::size_t nbytes, std::size_t align)noexcept {
					if (!head_) {
						return;
					}
					if (head_->in_region(mem)) {
						return free_reservation_in_region(head_, mem, nbytes, align);
					}
					auto node = prev_node_containing(mem);
					if (!node) {
						return;
					}
					return free_reservation_in_region(node->next_, mem, nbytes, align);
				}

				MEGU_CONSTEXPR void* try_alloc(std::size_t nbytes, std::size_t align,
					std::size_t min_cap)noexcept {
					if (is_empty()) {
						auto* node = push_front(std::max(nbytes, min_cap), align);
						if (node == nullptr) {
							return nullptr;
						}
						return reserve_region(node, nbytes, align);
					}
					region_node_t* r = head_; 
					if (fits_in_region(r, nbytes, align)) { 
						return reserve_region(r, nbytes, align);
					}
					else {
						for (; r->next_ != nullptr; r = r->next_) { 
							if (fits_in_region(r, nbytes, align)) { 
								return reserve_region(r, nbytes, align); 
							} 
						} 
					}
					r = push_next(r, std::max(nbytes, min_cap), align);
					if (r == nullptr) {
						return nullptr;
					}
					return reserve_region(r, nbytes, align);
				}

				MEGU_CONSTEXPR void* try_realloc(void* mem, std::size_t olds, std::size_t news, std::size_t align,
					std::size_t min_cap)noexcept {
					if (mem == nullptr) {//if realloc was called in place of alloc
						return try_alloc(news, align, min_cap);
					}
					if (news == olds) {//weird case but whatever
						return mem;
					}
					if (is_empty()) {//if the memory is not part of this arena because this arena is empty
						return nullptr;
					}
					//find the region
					auto* region = head_->in_region(mem) ? head_ : prev_node_containing(mem);
					if (region == nullptr) {//if memory is not part of this arena return null
						return nullptr;
					}
					if (0 == news) {//if realloc to 0 free
						free_reservation_in_region(region, mem, olds, align);
						return nullptr;
					}
					std::ptrdiff_t const d = news - olds;
					// if shrinking or growing and is .back() resize and return
					if (region->begin() - olds == mem && region->begin() + d < region->end()) {
						region->size() += d;
						return mem;
					}
					if (d < 0) {//if its shrinking and not .back() return as is
						return mem;
					}

					//allocate a new region if it doesnt fit
					auto* newreg = try_alloc(news, align, min_cap);
					if (!newreg) {
						return nullptr;
					}
					// if we are not using constexpr do a memcpy
#ifndef MEGU_USE_CONSTEXPR_ALLOC
					std::memcpy(newreg, mem, olds);
#else //MEGU_USE_CONSTEXPR_ALLOC
				//otherwise do a normal copy
					char* newreg_ = reinterpret_cast<char*>(newreg);
					char const* oldreg_ = reinterpret_cast<char const*>(mem);
					std::copy(oldreg_, oldreg_ + olds, newreg_);
#endif //MEGU_USE_CONSTEXPR_ALLOC
					free_reservation_in_region(region, mem, olds, align);
					return newreg;
				}

				MEGU_CONSTEXPR void* release_region_containing(void const* mem)noexcept {
					if (is_empty()) {
						return nullptr;
					}
					void* data = nullptr;
					if (head_->in_region(mem)) {
						data = head_->release();
						remove_head();
					}
					else {
						auto* prev = prev_node_containing(mem);
						if (prev == nullptr) {
							return nullptr;
						}
						data = prev->next_->release();
						remove_next(prev);
					}
					return data;
				}

				MEGU_CONSTEXPR std::vector<void*> release_all() {
					std::vector<void*> vec;
					vec.reserve(size_);
					for (region_node_t* h = head_; h != nullptr; h = h->next_) {
						vec.push_back(h->release());
					}
					free_all();
					return vec;
				}

				MEGU_CONSTEXPR void clear_all()noexcept {
					for (region_node_t* h = head_; h != nullptr; h = h->next_) {
						h->clear();
					}
				}
				MEGU_CONSTEXPR void free_all()noexcept {
					free_nodes();
				}

				[[nodiscard]]
				constexpr bool is_empty()const noexcept {
					return head_ == nullptr;
				}

				[[nodiscard]]
				MEGU_CONSTEXPR region_node_t* push_front(std::size_t bytes,
					std::size_t align)noexcept
				{
					region_node_t* new_node = new(std::nothrow) region_node_t(bytes, align);
					if (!new_node || !new_node->is_valid()) {
						return nullptr;
					}
					if (!is_empty()) {
						new_node->next_ = head_;
					}
					head_ = new_node;
					size_++;
					return new_node;
				}

				[[nodiscard]]
				MEGU_CONSTEXPR region_node_t* push_next(region_node_t* node,
					std::size_t bytes,
					std::size_t align)noexcept
				{
					if (node->next_) {
						return node->next_;
					}
					node->next_ = new(std::nothrow) region_node_t(bytes, align);
					if (!node->next_ || !node->next_->is_valid()) {
						return nullptr;
					}
					size_++;
					return node->next_;
				}

				[[nodiscard]]
				constexpr region_node_t* prev_node_containing(void const* mem) const noexcept {//never call this for head 
					for (auto* n = head_; n->next_ != nullptr; n = n->next_) {
						if (n->next_->in_region(mem)) {
							return n;
						}
					}
					return nullptr;
				}

				MEGU_CONSTEXPR void remove_head()noexcept {
					if (!head_) {
						return;
					}
					region_node_t* new_head = head_->next_;
					head_->next_ = nullptr;
					delete head_;
					size_--;
					head_ = new_head;
				}

				MEGU_CONSTEXPR void remove_next(region_node_t* node)noexcept {
					if (node->next_ == nullptr) {
						return;
					}
					region_node_t* isolated = node->next_;//grab next node to delete
					node->next_ = node->next_->next_;//skip the next node
					isolated->next_ = nullptr;//remove detachment of the deleted node for proper destruction
					size_--;//dec size
					delete isolated;
				}

				[[nodiscard]]
				constexpr region_node_t* head()noexcept {
					return head_;
				}

				constexpr void remove_unused()noexcept {
					while (head_ && (0 == head_->size() || 0 == head_->nallocations())) {
						remove_head();
					}
					if (!head_) {
						return;
					}
					for (auto* n = head_; n->next_ != nullptr; n = n->next_) {
						if (0 == n->next_->size() || 0 == n->next_->nallocations()) {
							remove_next(n);
						}
					}
				}

				constexpr std::size_t size()const noexcept {
					return size_;
				}

				std::string dump_usage()const { 
					std::ostringstream ss;
					ss << "Dumping usage for arena region-list : " << this << " {\n"; 
					for (auto* h = head_; h != nullptr; h = h->next_) { 
						ss << "  ";
						dump_usage_node(ss, h);
						ss << "\n";
					}
					ss << "}\n";
					return ss.str();
				}

			private:
				MEGU_CONSTEXPR void free_nodes()noexcept {
					region_node_t* h = head_;
					while (h = free_node(h));
					head_ = nullptr;
					size_ = 0;
				}

				static MEGU_CONSTEXPR region_node_t* free_node(region_node_t* node)noexcept {
					if (node == nullptr) {
						return nullptr;
					}
					auto* nxt = node->next_;
					node->next_ = nullptr;
					delete node;
					return nxt;
				}

				static constexpr bool fits_in_region(region_node_t const* r,
					std::size_t nbytes, std::size_t align)noexcept {
					nbytes += alignment_offset(align, r->begin());
					return r->begin() + nbytes < r->end();
				}

				static constexpr void free_reservation_in_region(region_node_t* r,
					void const* block_to_dealloc,
					std::size_t nbytes,
					std::size_t align)noexcept {
					assert(r->is_valid());
					r->nallocations()--;
					if (0 == r->nallocations()) {
						r->clear();
					}
					else if (r->begin() - nbytes == block_to_dealloc) {
						r->size() -= nbytes;
					}
					/*r->size() -= alignment_offset(align, r->begin());  */
				}

				static void dump_usage_node(std::ostringstream& ss,region_node_t* n) {
					ss << "<Region[" << n << "], total_allocs : "
						<< n->nallocations() << ", reserved : "
						<< n->size() << ", capacity : " << n->capacity()
						<< ", data-address : " << n->data() << ">";
				}

				static constexpr void* reserve_region(region_node_t* r, std::size_t nbytes, std::size_t align)noexcept {
					assert(r->is_valid());
					auto const aligned = alignment_offset(align, r->begin());
					void* ret = r->begin() + aligned;
					r->size() += nbytes + aligned;
					r->nallocations()++;
					assert(ret != nullptr);
					return ret;
				}

				std::size_t size_;
				region_node_t* head_;
			};

			region_list_t regs_;
			std::size_t min_cap_;
		};

	}//end detail

	class Arena : public detail::ArenaBase { 
	public:
		constexpr Arena(std::size_t cap = (1 << 12))noexcept
			:ArenaBase(cap) {} 

		using ArenaBase::FreeArena; 
		using ArenaBase::FreeUnusedRegions; 
		using ArenaBase::ClearArena;
		using ArenaBase::ReleaseArena;
		using ArenaBase::ReleaseRegionContaining;

		[[nodiscard]]
		MEGU_CONSTEXPR
		void* Allocate(std::size_t nbytes, std::size_t align = __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
			void* mem = this->alloc_nothrow(nbytes, align);
			if (mem == nullptr) {
				throw std::bad_alloc();
			}
			return mem;
		}
		[[nodiscard]]
		MEGU_CONSTEXPR
		void* AllocateNoThrow(std::size_t nbytes, std::size_t align = __STDCPP_DEFAULT_NEW_ALIGNMENT__)noexcept {
			void* mem = this->alloc_nothrow(nbytes, align);
			return mem;
		}
		[[nodiscard]]
		MEGU_CONSTEXPR
		void* Reallocate(void* mem,
			std::size_t old_size,
			std::size_t new_size,
			std::size_t align = __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
			void* remem = this->realloc_nothrow(mem, old_size, new_size, align);
			if (remem == nullptr) {
				throw std::bad_alloc();
			}
			return remem;
		}

		[[nodiscard]]
		MEGU_CONSTEXPR
		void* ReallocateNoThrow(void* mem,
			std::size_t old_size,
			std::size_t new_size,
			std::size_t align = __STDCPP_DEFAULT_NEW_ALIGNMENT__)noexcept {
			return this->realloc_nothrow(mem, old_size, new_size, align);
		}

		[[nodiscard]]
		MEGU_CONSTEXPR
		void Deallocate(void* mem,
			std::size_t nbytes,
			std::size_t align = __STDCPP_DEFAULT_NEW_ALIGNMENT__)noexcept {
			return this->dealloc(mem, nbytes, align);
		}

	};

	//TODO remove ArenaBase and rewrite a thread safe arena using atomics 
	class ThreadSafeArena : public detail::ArenaBase {
	public:
		ThreadSafeArena(std::size_t min_cap = GetPageSize())
			:ArenaBase(min_cap), mutex_() {}

		void FreeUnusedRegions() {
			std::scoped_lock<std::mutex> lock(mutex_);
			ArenaBase::FreeUnusedRegions();
		}
		void FreeArena() {
			std::scoped_lock<std::mutex> lock(mutex_);
			ArenaBase::FreeArena();
		}
		void ClearArena(){
			std::scoped_lock<std::mutex> lock(mutex_);
			ArenaBase::ClearArena();
		}
		[[nodiscard]]
		std::vector<void*> ReleaseArena() {
			std::scoped_lock<std::mutex> lock(mutex_);
			return ArenaBase::ReleaseArena();
		}
		[[nodiscard]]
		void* ReleaseRegionContaining(void const* mem){ 
			std::scoped_lock<std::mutex> lock(mutex_);
			return ArenaBase::ReleaseRegionContaining(mem);  
		}
		[[nodiscard]]
		void* Allocate(std::size_t nbytes, std::size_t align = __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
			void* mem = nullptr;
			{
			   std::scoped_lock<std::mutex> lock(mutex_);
			   mem = this->alloc_nothrow(nbytes, align);
			}
			if (mem == nullptr) {
				throw std::bad_alloc();
			}
			return mem;
		}
		[[nodiscard]]
		void* Reallocate(void* mem,
			std::size_t old_size,
			std::size_t new_size, 
			std::size_t align = __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
			void* remem = nullptr;
			{
			    std::scoped_lock<std::mutex> lock(mutex_);
			    remem = this->realloc_nothrow(mem,old_size,new_size, align);
			}
			if (remem == nullptr) {
				throw std::bad_alloc();
			}
			return remem;
		}

		[[nodiscard]]
		void Deallocate(void* mem,
			std::size_t nbytes,
			std::size_t align = __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
			std::scoped_lock<std::mutex> lock(mutex_);
			return this->dealloc(mem, nbytes, align);
		}

	private:
		std::mutex mutex_{};
	};

}//end megu