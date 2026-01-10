#include <cstddef>
#include <new>
#include <utility> //std::forward

struct Arena
{
    void*  memory;
    size_t capacity;
    size_t offset;

    Arena() = delete;
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    explicit Arena(size_t cap)
        : memory(operator new(cap)),
          capacity(cap),
          offset(0)
    {}

    void* allocate(size_t size, size_t alignment = alignof(std::max_align_t))
    {
        size_t aligned_offset = roundup(offset, alignment);

        if (aligned_offset + size > capacity)
            return nullptr;

        void* ptr = static_cast<char*>(memory) + aligned_offset;
        offset = aligned_offset + size;
        return ptr;
    }

    ~Arena()
    {
        operator delete(memory);
    }

private:
    static size_t roundup(size_t offset, size_t alignment)
    {
        // alignment must be power-of-two
        return (offset + alignment - 1) & ~(alignment - 1);
    }
};

template <typename T>

T* arena_allocate(struct Arena& arena)
{
    void* mem = arena.allocate(sizeof(T), alignof(T));
    if (!mem)
        return nullptr;
    return new (mem) T();
}
template <typename T, typename... Args>
void arena_destroy(T* obj)
{
    if (obj)
        obj->~T();
}

int main()
{
    
}
