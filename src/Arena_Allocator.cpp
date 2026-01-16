#include <cstddef>
#include <new>
#include <utility> //std::forward
#include <atomic>
#include <thread>
#include <vector>

constexpr size_t MAX_JOBS{64}; // Job systems must fail loudly if job storage overflows. Silent overflow is instant UB

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
    std::atomic<int> remaining;
    JobCounter() : remaining(0) {}
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
        job.counter->remaining.fetch_sub(1, std::memory_order_relaxed);
    }
}
void spawn_child_jobs(
    void (*fn)(void*),
    Job* job_list,
    size_t& job_count,
    void** payloads,
    size_t count,
    JobCounter* counter
)
{
    if(job_count + count > MAX_JOBS) // assuming MAX_JOBS is 64
    {
        // Handle overflow, e.g., throw an error or log
        return;
    }
    
    counter->remaining.fetch_add(static_cast<int>(count), std::memory_order_relaxed);

    for(size_t i = 0; i < count; ++i)
    {
        job_list[job_count++] = Job{fn, payloads[i], counter};
    }
}

//Thread function
void worker_thread(
    Job* jobs,//Available Jobs
    size_t job_count,//Total Number of Jobs
    std::atomic<size_t>& next_job//Reference to the next_job variable in the main function
)
{
    while (true)
    {
        //Getting the index position of the next_job
        size_t index{next_job.fetch_add(1,std::memory_order_relaxed)};
        if(index >= job_count)//To check that if we are not going out of index (we start from 0 to n-1)
        {
            break;
        }

        execute_job(jobs[index]);//Execute the next job through the next available thread
    }
}

int main()
{
    const unsigned int worker_count {std::thread::hardware_concurrency() > 1? std::thread::hardware_concurrency() - 1:1};//getting the hint towards the available number of threads
    std::vector<std::thread> workers;//Created a dynamic array to store threads
    std::atomic<size_t> next_job {0};
    size_t job_count {0};   // ✅ tracks job storage
    JobCounter counter;
    counter.remaining.store(0, std::memory_order_relaxed);  // ✅ start at 0

    Arena frameArena(1024);

    int a[] = {1,2,3};
    int b[] = {4,5,6};
    int out1 = 0;
    int out2 = 0;

    auto* p1 = arena_allocate<SumJobData>(frameArena, a, 3, &out1);
    auto* p2 = arena_allocate<SumJobData>(frameArena, b, 3, &out2);

    Job jobs[MAX_JOBS]; // small buffer for now

    // Initial jobs = future work → increment counter
    counter.remaining.fetch_add(2, std::memory_order_relaxed);

    jobs[job_count++] = Job{sum_job, p1, &counter};
    jobs[job_count++] = Job{sum_job, p2, &counter};

    //---- Launching worker threads ----
    for(unsigned int i =0; i<worker_count; ++i)
    {
        workers.emplace_back(
            worker_thread,
            jobs,
            job_count,
            std::ref(next_job)

        );
    }


    worker_thread(jobs,job_count,next_job);

    //wait for all worker
    for (auto& t :workers)
    {
        t.join();
    }

    // ✅ completion check
    if ((counter.remaining.load(std::memory_order_relaxed)) == 0)
    {
        frameArena.reset();
    }
}

