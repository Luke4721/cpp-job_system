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
struct JobContext;
//Job Structure 
struct Job
{
    void (*fn)(void*);
    void* data;
    JobCounter* counter;
    JobContext* ctx;

    bool is_leaf;
};

struct JobQueue //Owner Thread pushes & pops from tail. Stealers pop from head
{
    Job jobs[MAX_JOBS];
    std::atomic<size_t> head;
    std::atomic<size_t> tail;
};

struct Worker
{
    JobQueue queue;
    size_t id;

    
};

struct JobContext{
    Arena* arena;
    Worker* worker;
};

struct SumJobData {
    int* array;
    size_t count;
    std::atomic<int>* result;

    SumJobData(int* array, size_t count, std::atomic<int>* result)
        : array(array), count(count), result(result) {}
};
//Structure for the sum_job function to spawn child jobs if the payload array is big enough
struct SumRangeJobData{
    int* array;
    size_t begin;
    size_t end;
    std::atomic<int>* result;

    JobContext* ctx;
    JobCounter* counter;
    SumRangeJobData(
        int* array,
        size_t begin,
        size_t end,
        std::atomic<int>* result,

        JobContext* ctx,
        JobCounter* counter
    )
    : array(array),
      begin(begin),
      end(end),
      result(result),
      ctx(ctx),
      counter(counter)
      {}
};
void push_job(Worker& worker, Job job);
//Sum job
void sum_job(void* ptr)
{
    auto* data {static_cast<SumRangeJobData*>(ptr)};
    constexpr size_t Threshold {64};
    size_t count {data->end - data->begin};
    if(count <= Threshold)
    {
        int local = 0;
        for(size_t i = data->begin;i < data->end; ++i)
        {
            local += data->array[i];
        }
        data->result->fetch_add(local,std::memory_order_relaxed);
        return;
    }
    //Split into two child jobs
    size_t mid {((data->begin + count)/2)};

    SumRangeJobData* left = arena_allocate<SumRangeJobData>(*data->ctx->arena,data->array,data->begin,mid,data->result,data->ctx,data->counter);
    SumRangeJobData* right = arena_allocate<SumRangeJobData>(*data->ctx->arena,data->array,mid,data->end,data->result,data->ctx,data->counter);

    //Increment the counter BEFORE publishing
    Worker* self {data->ctx->worker};
    data->counter->remaining.fetch_add(2, std::memory_order_relaxed);
    push_job(*self, Job{sum_job, left, data->counter, data->ctx, false});
    push_job(*self, Job{sum_job, right, data->counter, data->ctx, false});

}
void execute_job(Job& job)
{
    job.fn(job.data);
    if (job.is_leaf&&job.counter)
    {
        job.counter->remaining.fetch_sub(1, std::memory_order_release);
    }
}

bool pop_local(JobQueue& q, Job& out)
{
    size_t t = q.tail.load(std::memory_order_relaxed);
    if(t == q.head.load(std::memory_order_acquire))
    {
        return false;
    }

    t--;
    q.tail.store(t,std::memory_order_relaxed);
    out = q.jobs[t];
    return true;
}

bool steal(JobQueue& victim, Job& out)
{
    size_t h = victim.head.load(std::memory_order_acquire);
    size_t t = victim.tail.load(std::memory_order_acquire);
    
    if(h>=t)
    {
        return false;
    }
    if(!victim.head.compare_exchange_strong(
        h,h+1,
        std::memory_order_acq_rel
    ))
    {
        return false;
    }

    out = victim.jobs[h];
    return true;
}

//Thread function
void worker_thread(
    Worker* self,
    Worker* all_workers,
    size_t worker_count,
    JobCounter* counter
)
{
    Job job;
    while(true)
    {
        //1. Trying local work
        if(pop_local(self->queue,job))
        {
            execute_job(job);
            continue;
        }
        
        //2.Trying to steal work from other Jobs
        bool stolen{false};
        for(size_t i = 0; i < worker_count; ++i)
        {
            if(i == self->id)
            {continue;}

            if(steal(all_workers[i].queue, job))
        {
            execute_job(job);
            stolen = true;
            break;
        }

        }

        if(stolen)
        {
            continue;
        }

        //When no work is found anywhere
        if(counter->remaining.load(std::memory_order_acquire)==0)
        {
            break;
        }
    }
}

void push_job(Worker& w, Job job)
{
    size_t t {w.queue.tail.load(std::memory_order_relaxed)};
    w.queue.jobs[t] = job;
    w.queue.tail.store(t+1, std::memory_order_relaxed);
}

int main()
{
    const unsigned int worker_count {std::thread::hardware_concurrency() > 1? std::thread::hardware_concurrency() - 1:1};//getting the hint towards the available number of threads
    std::vector<std::thread> threads;//Created a dynamic array to store threads
    std::vector<Worker> workers(worker_count);
    JobCounter counter;
    counter.remaining.store(0, std::memory_order_relaxed);  // ✅ start at 0

    for(size_t i =0; i<worker_count;++i)
    {
        workers[i].id = i;
        workers[i].queue.head.store(0);
        workers[i].queue.tail.store(0);
    }

    Arena frameArena(1024);
    
    std::vector<JobContext> contexts(worker_count);

    for(size_t i =0; i<worker_count;++i)
    {
        contexts[i] =  JobContext{&frameArena, &workers[i]};
    }

    int a[] = {1,2,3};
    int b[] = {4,5,6};
    std::atomic<int> out1{0};
    std::atomic<int> out2{0};

    auto* p1 = arena_allocate<SumRangeJobData>(frameArena, a, 0 , 3, &out1, &contexts[0], &counter);
    auto* p2 = arena_allocate<SumRangeJobData>(frameArena, b, 0 , 3, &out2, &contexts[0], &counter);




    // Initial jobs = future work → increment counter
    counter.remaining.fetch_add(2, std::memory_order_relaxed);

    // Pushing the initial jobs
    push_job(workers[0], Job{sum_job, p1, &counter, &contexts[0], true});
    push_job(workers[0], Job{sum_job, p2, &counter, &contexts[0], true});
    //---- Launching worker threads ----
    for(unsigned int i =0; i<worker_count; ++i)
    {
        threads.emplace_back(
            worker_thread,
            &workers[i],
            workers.data(),
            worker_count,
            &counter
        );
    }


    worker_thread(&workers[0], workers.data(),worker_count, &counter);//Main thread also works as a worker


    //wait for all worker
    for (auto& t :threads)
    {
        t.join();
    }

    // ✅ completion check
    if ((counter.remaining.load(std::memory_order_acquire)) == 0)
    {
        frameArena.reset();
    }
}

