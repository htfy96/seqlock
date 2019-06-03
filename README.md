# seqlock
A C++11 SeqLock implementation with reader/writer support, designed for non-blocking reads.

![GitHub](https://img.shields.io/github/license/htfy96/seqlock.svg) [![CircleCI](https://circleci.com/gh/htfy96/seqlock.svg?style=svg&circle-token=17626543a8bc4d2f5ee0beba9b78c66ca3540c09)](https://circleci.com/gh/htfy96/seqlock)



## Usage
```cpp
#include "seqlock.hpp"

struct Foo {
    int v1;
    double v2;
    long v3;
};

// Basic usage:
using namespace seqlock;
SeqLock<Foo> protected_data {2, 3.0};

protected_data.write(foo_1); // thread-safe. No serialization.
Foo data = protected_data.load(); // does not block write, thread-safe

// Advanced usage:
// mainly used for large structs to avoid copy
{
    auto writer = protected_data.get_writer(); // move-only
    // exclusive access
    auto old_value = writer.read_member(&Foo::v);
    writer.write_member(&Foo::v, old_value + 2);
    // writer lock will be automatically released at the exit of scope
}

std::tuple<int, long> v1_and_v3 = protected_data.load_members(&Foo::v1, &Foo::v3);
```


A seqlock is a writer-first userspace lock.
Reader doesn't block writers by retrying read based on version numbers.

Use cases:
- busy writer(s)
- a small number of writers
- starvation between writers can be tolerated
- non-blocking reads

```cpp
template <typename T,
          typename ReadConflictPolicy = conflict_policies::Auto,
          typename WriteConflictPolicy = conflict_policies::Auto,
          typename SeqCounterT = std::uint_fast32_t>
class SeqLock;
```

Precondition: `is_trivial<T>`

Config:
- `T`: protected data, must be TrivialType
- `ReadConflictPolicy`: Behavior when load() finds another thread writing the
value.
  Default: `conflict_policies::Auto`.
- `WriteConflictPolicy`: Behavior when store() finds another thread writing
the value.
  Default: `conflict_policies::Auto`.
- `SeqCounterT`: the sequence counter. Set this to a larger type if there are
too many writers causing it to wrap around.

Implementation based on: *Can Seqlocks Get Along with Programming Language
Memory Models?, Hans-J. Boehm HP Labs*

`conflict_policies`:
- `Pause`: use x86 instruction `pause` on conflict
- `Yield`: yield to another thread
- `RetryImmediately`: no-op
- `Auto`: use `Pause` when possible, `Yield` otherwise
