# cpp-job_system

## C++ Job System (Learning Project)

This repository explores **low-level systems programming concepts** through the
implementation of a custom **job system with work stealing**.

The goal of this project is **learning by building**, focusing on how real
game engines and performance-oriented systems manage memory, concurrency,
and task scheduling.

This is **not a production-ready engine**, but a deliberately minimal and
explicit implementation designed to expose the underlying mechanics.

---

## Concepts Explored

- Custom **arena allocator** with frame-level lifetime
- Lock-free **job system design**
- Work-stealing scheduler
- Atomic synchronization and memory ordering
- Recursive job spawning
- Explicit job lifetime tracking
- Multithreading without mutexes

---

## Architecture Used for the Dynamic Job / Arena Allocator
# cpp-job_system

## C++ Job System (Learning Project)

This repository explores **low-level systems programming concepts** through the
implementation of a custom **job system with work stealing**.

The goal of this project is **learning by building**, focusing on how real
game engines and performance-oriented systems manage memory, concurrency,
and task scheduling.

This is **not a production-ready engine**, but a deliberately minimal and
explicit implementation designed to expose the underlying mechanics.

---

## Concepts Explored

- Custom **arena allocator** with frame-level lifetime
- Lock-free **job system design**
- Work-stealing scheduler
- Atomic synchronization and memory ordering
- Recursive job spawning
- Explicit job lifetime tracking
- Multithreading without mutexes

---

## Architecture Used for the Dynamic Job / Arena Allocator
JobSystem
 ├─ Arena (frame lifetime)
 ├─ JobCounter (global remaining jobs)
 ├─ Worker[N]
 │   ├─ JobQueue (local deque)
 │   └─ worker_thread()
 └─ Main thread = Worker 0



---

## Core Components

### Arena Allocator
- Linear allocator with no per-object frees
- All job data is allocated from a frame arena
- Arena is reset **only after all jobs complete**
- Guarantees fast allocation and simple lifetime rules

---

### Job System
- Each job contains:
  - Function pointer
  - Payload pointer
  - Shared `JobCounter`
  - Execution context (`JobContext`)
- Jobs may spawn child jobs recursively
- Child jobs are **published before execution**

---

### JobCounter
- Global atomic counter tracking remaining work
- Incremented **before** publishing new jobs
- Decremented on job completion
- Workers terminate only when `remaining == 0`
- Uses acquire/release semantics to ensure correctness

---

### Workers & Job Queues
- Each worker owns a local deque (`JobQueue`)
- Owner thread:
  - Pushes and pops from the tail
- Stealing threads:
  - Steal from the head using CAS
- This minimizes contention and improves scalability

---

## Concurrency Model

- `memory_order_relaxed`
  - Queue indices
  - Job publishing counters
- `memory_order_release`
  - Job completion
- `memory_order_acquire`
  - Shutdown checks

This avoids unnecessary synchronization while preserving correctness.

---

## Example Job: Parallel Sum

The project includes a recursive sum job:
- Splits work when input size exceeds a threshold
- Spawns child jobs dynamically
- Accumulates results using atomic operations

This demonstrates:
- Recursive task decomposition
- Work stealing under uneven workloads
- Correct synchronization of shared results

---

## Limitations (Intentional)

- Fixed-size job queues (`MAX_JOBS`)
- No dynamic backoff or sleeping for idle workers
- Child job count is currently hardcoded (binary split)

These choices keep the system simple and focused on fundamentals.

---

## Why This Project Exists

This project was built to deeply understand:
- Pointer ownership and lifetime
- Lock-free programming
- Job scheduling strategies
- Memory ordering guarantees
- How modern engines structure task systems

It serves as a foundation for more advanced systems such as:
- Fiber-based schedulers
- Priority queues
- Continuations
- Engine-level task graphs

---

## Status

🚧 **Work in progress (learning project)**  
Future improvements are expected as understanding deepens.