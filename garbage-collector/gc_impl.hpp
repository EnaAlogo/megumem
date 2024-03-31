#pragma once
#include <vector>
#include <sstream>
#include <unordered_map>
#include <iostream>
#include <assert.h>

namespace megu {
    

    enum GCMark : int8_t {
        GC_KEEP_ALIVE,
        GC_REFERENCED,
        GC_DEFAULT
    };
    
    static const char* marktostr(GCMark m) {
        switch (m)
        {
        case megu::GC_KEEP_ALIVE:
            return "GC_KEEP_ALIVE";
            break;
        case megu::GC_REFERENCED:
            return "GC_REFERENCED";
            break;
        case megu::GC_DEFAULT:
            return "GC_DEFAULT";
            break;
        default:
            return "UNDEFINED";
            break;
        }
    }

    struct AlignedDelete {

        void operator()(char* block)noexcept {
            ::operator delete(block, std::align_val_t(align));
        }

        std::size_t align;
    };

    struct Object {
#ifndef _MSC_VER
        constexpr 
#endif
            Object(
            std::size_t object_size,
            void(*dtor)(void*,std::size_t)noexcept,
            std::size_t align = __STDCPP_DEFAULT_NEW_ALIGNMENT__)
            :
            status_(GC_DEFAULT),
            data_(alloc(object_size,std::max(__STDCPP_DEFAULT_NEW_ALIGNMENT__,align))),
            dtor_(dtor),
            size_(object_size),
            alignment_(std::max(__STDCPP_DEFAULT_NEW_ALIGNMENT__,align)){}

        ~Object() {
            destroy();
            free();
        }

        constexpr Object(Object const&) = delete;
        constexpr Object& operator=(Object const&) = delete;
        constexpr Object(Object &&other) noexcept{
            private_move(std::move(other));
        }
        constexpr Object& operator=(Object && other)noexcept {
            if (this != &other) {
                private_move(std::move(other));
            }
            return *this;
        }

        constexpr std::size_t nbytes()const noexcept {
            return size_;
        }

        constexpr bool is_valid()const noexcept {
            return data_ != nullptr;
        }

        void destroy()noexcept {
            if (is_valid() && dtor_) {
                dtor_(data_,size_);
                dtor_ = nullptr;
            }
        }
        void free()noexcept {
            ::operator delete(data_, std::align_val_t(alignment_));
            data_ = nullptr;
        }

        constexpr char* data()const noexcept {
            return data_;
        }

        constexpr std::size_t alignment()const noexcept {
            return alignment_;
        }

        friend std::ostream& operator << (std::ostream & ss, Object const& o) {
            if (!o.is_valid()) {
                ss << "Invalid Object";
                return ss;
            }
            ss << "<Object addr:<" << (void*)o.data_ << ">";
            ss << " size:" << o.size_ << " dtor:<" << o.dtor_ << "> mark " << marktostr(o.status_)<< ">";
            return ss;
        }

        constexpr bool operator==(Object const& other)const noexcept {
            return data_ == other.data_;
        }

        GCMark mark()const noexcept {
            return status_;
        }

        void mark(GCMark mark)noexcept {
            status_ = mark;
        }

    private:
        char* data_;
        void(*dtor_)(void*,std::size_t)noexcept;
        std::size_t size_;
        std::size_t alignment_;
        GCMark status_;

#ifndef _MSC_VER
        constexpr
#endif
        static char* alloc(std::size_t v, std::size_t align) {
            return static_cast<char*>(::operator new(v, std::align_val_t(align)));
        }

        constexpr void private_move(Object&& other)noexcept {
            data_ = other.data_;
            dtor_ = other.dtor_;
            other.data_ = nullptr;
            other.dtor_ = nullptr;
            size_ = other.size_;
            alignment_ = other.alignment_;
            status_ = other.status_;
        }
    };

    using ObjectToChunkMap = std::unordered_map<void const*, Object>;
   
    


    struct GarbageCollectorImpl {
        GarbageCollectorImpl(Word const* rsp)
            :rsp_(rsp){}

        void free(void* data) {
            auto it = gc_map_.find(data);
            if (it != gc_map_.end()) {
                gc_map_.erase(it); 
            }
        }

        char* allocate_object(std::size_t nbytes, std::size_t align, void(*dtor)(void*, std::size_t)noexcept) {
            auto obj = Object(nbytes, dtor, align);
            char* data = obj.data();
            gc_map_.insert({ data, std::move(obj) }); 
            return data;
        }


        void collect() {
            Word const* rsp = std::launder(reinterpret_cast<Word const*>(MEGU_GET_SP()));
            //find out if stack grows up or down its most commonly down
            if (rsp < rsp_)[[likely]] {
                find_reachables(rsp, rsp_);
            }
            else {
                find_reachables(rsp_, rsp);
            }
            std::erase_if(gc_map_, [this](auto && entry) {
                Object& obj = std::get<1>(entry);
                if (obj.mark() == GC_KEEP_ALIVE) {
                    return false;
                }
                if (obj.mark() == GC_REFERENCED) { 
                    obj.mark(GC_DEFAULT);   
                    return false;
                }
                return true;
            });
        }

        std::string dump_usage()const {
            std::ostringstream ss; 
            ss << "GC stats {";  
            for (auto const& [ptr,c] : gc_map_) {
                ss << "\n  " << c;  
            }
            ss << "\n}\n";
            return ss.str();
        }

        ~GarbageCollectorImpl() {
            free_all();
        }

        void free_all()noexcept {
            gc_map_.clear();
        }

        void mark_reachability(void const* var, GCMark mark) {
            auto it = gc_map_.find(var);
            if (it != gc_map_.end()) {
                it->second.mark(mark); 
            }
        }

    private:
        ObjectToChunkMap gc_map_; 
        Word const* rsp_;


        void find_reachables(Word const* begin, Word const* end){
            assert(Word(begin) % alignof(Word) == 0);
            assert(begin < end);
            //min heap alignment is 16 in most x64 so this prolly wont be an issue ever
            for (; begin <= end; ++begin) {
                const Word* inner_ptr = std::bit_cast<Word const*>(*begin);
                auto it = gc_map_.find(inner_ptr); 
                if (it != gc_map_.end()) { 
                    Object& reg = it->second;
                    if (GC_DEFAULT != reg.mark() || !reg.is_valid()) { 
                        continue;
                    }
                    reg.mark(GC_REFERENCED);
                    if (reg.nbytes() < 8) {
                        continue;
                    }
                    find_reachables(reinterpret_cast<Word const*>(reg.data()),
                        reinterpret_cast<Word const*>(reg.data() + reg.nbytes()));
                }
            }
        }

    };


}//end megu