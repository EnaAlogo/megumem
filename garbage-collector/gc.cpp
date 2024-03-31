#include "gc.hpp"
#include "gc_impl.hpp"

namespace megu {
	GarbageCollector::~GarbageCollector() = default;

	GarbageCollector::GarbageCollector(const Word* root)
		:pimpl_(std::make_unique<GarbageCollectorImpl>(root)) {}

	void  GarbageCollector::Collect() {
		pimpl_->collect();
	}
	void  GarbageCollector::Free(void* data) {
		pimpl_->free(data);
	}
	char* GarbageCollector::AllocateObject(std::size_t nbytes, std::size_t align, void(*dtor)(void*, std::size_t)noexcept) {
		return pimpl_->allocate_object(nbytes, align, dtor);
	}
	void GarbageCollector::MarkReachable(void const*ptr)const {
		pimpl_->mark_reachability(ptr,GCMark::GC_REFERENCED);
	}
	void GarbageCollector::MarkUnreachable(void const* ptr)const {
		pimpl_->mark_reachability(ptr, GCMark::GC_DEFAULT);
	}

	void GarbageCollector::MarkKeepAlive(void const*ptr)const {
		pimpl_->mark_reachability(ptr, GCMark::GC_KEEP_ALIVE);
	}
	void GarbageCollector::UnmarkKeepAlive(void const* ptr)const {
		pimpl_->mark_reachability(ptr, GCMark::GC_DEFAULT);
	}

	void GarbageCollector::FreeAll() {
		pimpl_->free_all();
	}

	std::string GarbageCollector::DumpUsage()const {
		return pimpl_->dump_usage();
	}

}//end megu