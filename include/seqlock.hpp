#ifndef SEQLOCK_H
#define SEQLOCK_H
/* This file is an implementation of SeqLock.
 *
 * Configs:
 * - SEQLOCK_NO_INTEL_INTRINSIC: don't try to include emmintrin.h.
 *   read_conflict_policies::Pause will be disabled
 * - SEQLOCK_NO_BUILTIN_EXPECT: don't try to use __builtin_expect
 */

#include <atomic>
#include <cstdint>
#include <thread>
#include <tuple>
#include <type_traits>

#ifndef SEQLOCK_NO_INTEL_INTRINSIC
#include "emmintrin.h"
#endif

#ifndef SEQLOCK_NO_BUILTIN_EXPECT
#define SEQLOCK_LIKELY(x) __builtin_expect((x), 1)
#define SEQLOCK_UNLIKELY(x) __builtin_expect((x), 0)
#else
#define SEQLOCK_LIKELY(x) (x)
#define SEQLOCK_UNLIKELY(x) (x)
#endif

namespace seqlock {

namespace conflict_policies {
#ifndef SEQLOCK_NO_INTEL_INTRINSIC
struct Pause {
  static void on_conflict() { _mm_pause(); }
};
#endif

// Use x86 pause when possible
// otherwise use yield()
struct Auto {
  static void on_conflict() {
#if defined(__SSE2__) && !defined(SEQLOCK_NO_INTEL_INTRINSIC)
    Pause::on_conflict();
#else
    Yield::on_conflict();
#endif
  }
};

struct Yield {
  static void on_conflict() { std::this_thread::yield(); }
};

struct RetryImmediately {
  static void on_conflict() {
    (void)0;  // A no-op
  }
};
}  // namespace conflict_policies

namespace internal {
// TODO: Ideally, this two methods should use
// llvm.memcpy.element.unordered.atomic

template <typename T>
T read_out_of_order_atomic(const T* ptr) {
#ifdef __cpp_lib_atomic_ref
  T res;
  for (size_t i = 0; i < sizeof(T); ++i) {
    reinterpret_cast<char*>(&res)[i] =
        std::atomic_ref<const char>(reinterpret_cast<const char*>(ptr)[i])
            .load(std::memory_order_relaxed);
  }
  return res;
#else
  T res;
  const char* src = reinterpret_cast<const char*>(ptr);
  char* dst = reinterpret_cast<char*>(&res);
  for (size_t i = 0; i < sizeof(T); ++i) {
    dst[i] = __atomic_load_n(&src[i], __ATOMIC_RELAXED);
  }
#endif
  return res;
}

template <typename T>
void store_out_of_order_atomic(T* ptr, const T& val) {
#ifdef __cpp_lib_atomic_ref
  for (size_t i = 0; i < sizeof(T); ++i) {
    std::atomic_ref<char>(reinterpret_cast<char*>(ptr)[i])
        .store(reinterpret_cast<const char*>(&val)[i],
               std::memory_order_relaxed);
#else
  const char* src = reinterpret_cast<const char*>(&val);
  char* dst = reinterpret_cast<char*>(ptr);
  for (size_t i = 0; i < sizeof(T); ++i)
    __atomic_store_n(&dst[i], src[i], __ATOMIC_RELAXED);
#endif
  }
}  // namespace internal

// A SeqLock holding an instance of T.
//
// A seqlock is a writer-first userspace lock.
// Reader doesn't block writers by retrying read based on version numbers.
//
// Use cases:
// - busy writer(s)
// - a small number of writers
// - blocking-free
// - small structs
//
// Precondition: is_trivial<T>
//
// Config:
// - T: protected data, must be TrivialType
// - ReadConflictPolicies: Behavior when load() finds another thread writing the
// value.
//   Default: conflict_policies::Auto.
// - WriteConflictPolicies: Behavior when store() finds another thread writing
// the value.
//   Default: conflict_policies::Auto.
// - SeqCounterT: the sequence counter. Set this to a larger type if there are
// too many writers causing it to wrap around.
//
// Implementation based on: Can Seqlocks Get Along with Programming Language
// Memory Models?, Hans-J. Boehm HP Labs
template <typename T,
          typename ReadConflictPolicy = conflict_policies::Auto,
          typename WriteConflictPolicy = conflict_policies::Auto,
          typename SeqCounterT = std::uint_fast32_t>
class SeqLock {
  static_assert(std::is_trivial<T>::value,
                "The contained type of SeqLock should be a TrivialType");
  static_assert(std::is_unsigned<SeqCounterT>::value,
                "SeqCounterT must be an unsigned type");
  alignas(8) T val;
  std::atomic<SeqCounterT> seq;

  template <typename FT>
  void run_in_reader_section(FT f) {
    SeqCounterT seq1, seq2;
    do {
      seq1 = seq.load(std::memory_order_acquire);
      if (SEQLOCK_UNLIKELY(seq1 & 1)) {
        ReadConflictPolicy::on_conflict();
        continue;
      }
      f();
      std::atomic_thread_fence(std::memory_order_acquire);
      seq2 = seq.load(std::memory_order_relaxed);
    } while ((seq1 & 1) || seq1 != seq2);
  }

 public:
  template <typename... Args>
  SeqLock(Args... arg) : val(arg...), seq(0) {}

  T load() {
    T result;
    run_in_reader_section([&result, this]() {
      result = internal::read_out_of_order_atomic(&val);
    });
    return result;
  }

  void write(const T& new_val) {
    auto writer = get_writer();
    writer.write(new_val);
  }

  class Writer {
    SeqLock* parent;
    SeqCounterT seq1;
    friend SeqLock;
    Writer(SeqLock& lk, SeqCounterT seq1) : parent(&lk), seq1(seq1) {}

   public:
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    Writer(Writer&& other) noexcept {
      parent = nullptr;
      std::swap(parent, other.parent);
      seq1 = other.seq1;
    }
    Writer&& operator=(Writer&& other) noexcept {
      parent = nullptr;
      std::swap(parent, other.parent);
      seq1 = other.seq1;
    }
    template <typename MemberT>
    MemberT read_member(MemberT(T::*member)) {
      return internal::read_out_of_order_atomic(&(parent->val.*member));
    }
    template <typename MemberT>
    void write_member(MemberT(T::*member), const MemberT& new_val) {
      internal::store_out_of_order_atomic(&(parent->val.*member), new_val);
    }
    void write(const T& new_val) {
      internal::store_out_of_order_atomic(&parent->val, new_val);
    }
    ~Writer() {
      if (parent)
        parent->seq.store(seq1 + 2, std::memory_order_release);
    }
  };

  // Advanced usage
  Writer get_writer() {
    SeqCounterT seq1 = seq.load(std::memory_order_relaxed);
    for (;;) {
      while (seq1 & 1) {
        WriteConflictPolicy::on_conflict();
        seq1 = seq.load(std::memory_order_relaxed);
      }
      if (seq.compare_exchange_weak(seq1, seq1 + 1, std::memory_order_release,
                                    std::memory_order_relaxed)) {
        break;
      }
      WriteConflictPolicy::on_conflict();
    }
    return Writer(*this, seq1);
  }

  template <typename... MemberTs>
  std::tuple<MemberTs...> load_members(MemberTs(T::*... member_ptrs)) {
    std::tuple<MemberTs...> result;
    run_in_reader_section([&result, this, member_ptrs...]() {
      result = std::tuple<MemberTs...>{
          internal::read_out_of_order_atomic(&(val.*member_ptrs))...};
    });
    return result;
  }
};
}  // namespace internal
#endif
