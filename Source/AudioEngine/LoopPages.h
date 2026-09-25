#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace slyce {
// Stereo copy-on-write pages. A snapshot retains page references rather than
// copying a multi-megabyte PCM buffer while the audio callback is stopped.
// The callback copies at most 256 stereo frames on the first write to a shared
// page. Retained pages remain immutable until the final reader releases them.
class LoopPagePool {
public:
    static constexpr int framesPerPage = 256;
    static constexpr std::uint32_t none = 0xffffffffu;
    struct Page {
        std::atomic<std::uint32_t> references{0};
        std::atomic<std::uint32_t> next{none};
        // Uninitialised until acquired: do not touch hundreds of MB merely to
        // open the editor. acquire() always clears or copies before publishing.
        std::array<float, framesPerPage*2> samples;
    };
private:
    std::unique_ptr<Page[]> pages;
    std::uint32_t pageCount;
    std::atomic<std::uint64_t> freeHead{0}; // generation:index, ABA-resistant
    static std::uint64_t head(std::uint32_t tag, std::uint32_t index) noexcept {
        return (static_cast<std::uint64_t>(tag)<<32) | index;
    }
public:
    explicit LoopPagePool(std::uint32_t count) : pages(new Page[count]), pageCount(count) {
        for (std::uint32_t i=0; i<count; ++i) pages[i].next.store(i+1<count ? i+1 : none);
        freeHead.store(head(0, count ? 0 : none));
    }
    Page* acquire(const Page* source=nullptr) noexcept {
        auto old=freeHead.load(std::memory_order_acquire);
        for (;;) {
            auto index=static_cast<std::uint32_t>(old);
            if (index==none) return nullptr;
            auto next=pages[index].next.load(std::memory_order_relaxed);
            auto replacement=head(static_cast<std::uint32_t>(old>>32)+1, next);
            if (freeHead.compare_exchange_weak(old,replacement,std::memory_order_acq_rel)) {
                auto* p=&pages[index];
                p->references.store(1,std::memory_order_relaxed);
                if(source) p->samples=source->samples;
                else p->samples.fill(0.0f);
                return p;
            }
        }
    }
    static void retain(Page* p) noexcept { if(p) p->references.fetch_add(1,std::memory_order_relaxed); }
    void release(Page* p) noexcept {
        if(!p || p->references.fetch_sub(1,std::memory_order_acq_rel)!=1) return;
        const auto index=static_cast<std::uint32_t>(p-pages.get());
        assert(index<pageCount);
        auto old=freeHead.load(std::memory_order_acquire);
        for (;;) {
            p->next.store(static_cast<std::uint32_t>(old),std::memory_order_relaxed);
            if(freeHead.compare_exchange_weak(old,head(static_cast<std::uint32_t>(old>>32)+1,index),std::memory_order_acq_rel)) break;
        }
    }
};

class LoopPages {
    std::shared_ptr<LoopPagePool> owner;
    std::vector<LoopPagePool::Page*> pages;
public:
    LoopPages() = default;
    LoopPages(std::shared_ptr<LoopPagePool> pool, int maxFrames) : owner(std::move(pool)),
        pages(static_cast<std::size_t>((maxFrames+LoopPagePool::framesPerPage-1)/LoopPagePool::framesPerPage), nullptr) {}
    ~LoopPages(){ clear(); }
    LoopPages(const LoopPages&)=delete;
    LoopPages& operator=(const LoopPages&)=delete;
    LoopPages(LoopPages&& other) noexcept {swap(other);}
    LoopPages& operator=(LoopPages&& other) noexcept { if(this!=&other){clear();owner.reset();pages.clear();swap(other);} return *this; }
    void swap(LoopPages& other) noexcept { owner.swap(other.owner);pages.swap(other.pages); }
    void clear() noexcept {
        if(owner) for(auto*& p:pages){owner->release(p);p=nullptr;}
    }
    // Destination slot array is preallocated before entering the audio callback.
    void captureFrom(const LoopPages& other, int frames) noexcept {
        assert(owner==other.owner && pages.size()==other.pages.size());
        clear();
        const auto count=static_cast<std::size_t>((frames+LoopPagePool::framesPerPage-1)/LoopPagePool::framesPerPage);
        for(std::size_t i=0;i<count && i<pages.size();++i){ pages[i]=other.pages[i];LoopPagePool::retain(pages[i]); }
    }
    float get(int channel,int index) const noexcept {
        assert(channel>=0 && channel<2 && index>=0);
        const auto pg=static_cast<std::size_t>(index/LoopPagePool::framesPerPage);
        if(pg>=pages.size() || !pages[pg]) return 0.0f;
        return pages[pg]->samples[static_cast<std::size_t>(channel*LoopPagePool::framesPerPage + index%LoopPagePool::framesPerPage)];
    }
    bool set(int index,float left,float right) noexcept {
        assert(index>=0);
        const auto pg=static_cast<std::size_t>(index/LoopPagePool::framesPerPage);
        if(pg>=pages.size() || !owner) return false;
        auto*& p=pages[pg];
        if(!p || p->references.load(std::memory_order_acquire)>1){
            auto* replacement=owner->acquire(p);
            if(!replacement) return false;
            owner->release(p); p=replacement;
        }
        const auto at=static_cast<std::size_t>(index%LoopPagePool::framesPerPage);
        p->samples[at]=left;p->samples[at+LoopPagePool::framesPerPage]=right;
        return true;
    }
};
}
