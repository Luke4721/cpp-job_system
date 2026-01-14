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
    //Invariant : After reset(), All objects allocated from the arena are invalid, and the next allocation will start from the beginning of the buffer.
    void reset()
    {
        offset = 0;
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

template <typename T, typename... Args>

T* arena_allocate(struct Arena& arena, Args&&... args)
{
    void* mem = arena.allocate(sizeof(T), alignof(T));
    if (!mem)
        return nullptr;
    return new (mem) T(std::forward<Args>(args)...);
}
template <typename T>
void arena_destroy(T* obj)
{
    if (obj)
        obj->~T();
}
struct JobCounter {
    int remaining;
};
//Job Structure 
struct Job
{
    void (*fn)(void*);
    void* data;
    JobCounter* counter;
};
struct SumJobData {
    int* array;
    size_t count;
    int* result;

    SumJobData(int* array, size_t count, int* result)
        : array(array), count(count), result(result) {}
};

//Sum job
void sum_job(void* ptr)
{
    auto* data = static_cast<SumJobData*>(ptr);

    int sum=0;

    for(size_t i=0; i< data->count; ++i)
    {
        sum += data->array[i];
    }
    *data->result = sum;
}
void execute_job(Job& job)
{
    job.fn(job.data);
    if (job.counter)
    {
        --job.counter->remaining;
    }
}

int main()
{
    JobCounter counter;
    counter.remaining = 2;
    Arena frameArena(1024);
    int a[] = {1,2,3};
    int b[] = {4,5,6};
    int out1 = 0;
    int out2 = 0;

    auto* p1 = arena_allocate<SumJobData>(frameArena, a, 3, &out1);
    auto* p2 = arena_allocate<SumJobData>(frameArena, b, 3, &out2);

    Job jobs[2];
    jobs[0] = Job{sum_job, p1, &counter};
    jobs[1] = Job{sum_job, p2, &counter};

    for(auto& job : jobs)
    {
        execute_job(job);
    }
    if(counter.remaining == 0)
    {
        frameArena.reset();
    }
}
