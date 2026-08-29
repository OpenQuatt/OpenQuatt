#pragma once

#include <cstddef>
#include <cstdlib>
#include <new>
#include <utility>

#include "esphome/core/helpers.h"

namespace esphome::openquatt_common {

/// Large OpenQuatt history buffer that prefers PSRAM but falls back to internal RAM
/// when PSRAM is not available on the target hardware.
template <typename T>
class PsramBuffer {
 public:
  PsramBuffer() = default;
  ~PsramBuffer() { this->release(); }

  PsramBuffer(const PsramBuffer&) = delete;
  PsramBuffer& operator=(const PsramBuffer&) = delete;

  void swap(PsramBuffer& other) noexcept {
    using std::swap;
    swap(this->data_, other.data_);
    swap(this->size_, other.size_);
    swap(this->external_, other.external_);
  }

  bool allocate(size_t count) { return this->allocate_(count, true); }

  /// Allocate strictly from PSRAM. This intentionally does not fall back to
  /// internal RAM when preserving internal heap is part of the safety budget.
  bool allocate_external(size_t count) { return this->allocate_(count, false); }

  void release() {
    if (this->data_ != nullptr) {
      free(this->data_);  // NOLINT(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
      this->data_ = nullptr;
    }
    this->size_ = 0;
    this->external_ = false;
  }

  T* data() { return this->data_; }
  const T* data() const { return this->data_; }
  size_t size() const { return this->size_; }
  bool is_external() const { return this->external_; }
  explicit operator bool() const { return this->data_ != nullptr; }

  T& operator[](size_t index) { return this->data_[index]; }
  const T& operator[](size_t index) const { return this->data_[index]; }

 private:
  bool allocate_(size_t count, bool allow_internal_fallback) {
    this->release();
    if (count == 0) {
      return true;
    }

    RAMAllocator<T> external_allocator(RAMAllocator<T>::ALLOC_EXTERNAL);
    this->data_ = external_allocator.allocate(count);
    if (this->data_ == nullptr) {
      if (!allow_internal_fallback) {
        return false;
      }
      RAMAllocator<T> internal_allocator(RAMAllocator<T>::ALLOC_INTERNAL);
      this->data_ = internal_allocator.allocate(count);
      this->external_ = false;
      if (this->data_ == nullptr) {
        return false;
      }
    } else {
      this->external_ = true;
    }

    this->size_ = count;
    return true;
  }

  T* data_{nullptr};
  size_t size_{0};
  bool external_{false};
};

/// Fixed-size object storage that is placement-constructed strictly in PSRAM.
///
/// Unlike PsramBuffer, this helper never falls back to internal RAM and manages
/// the lifetime of non-trivial objects. Allocate it from setup(), after PSRAM
/// initialization, and keep all accesses in normal task context.
template <typename T, size_t Count>
class PsramObjectArray {
 public:
  PsramObjectArray() = default;
  ~PsramObjectArray() { this->release(); }

  PsramObjectArray(const PsramObjectArray&) = delete;
  PsramObjectArray& operator=(const PsramObjectArray&) = delete;

  bool allocate() {
    this->release();
    if constexpr (Count == 0U) {
      return true;
    }

    RAMAllocator<T> allocator(RAMAllocator<T>::ALLOC_EXTERNAL);
    this->data_ = allocator.allocate(Count);
    if (this->data_ == nullptr) {
      return false;
    }
    for (size_t index = 0U; index < Count; ++index) {
      new (&this->data_[index]) T{};
    }
    return true;
  }

  void release() {
    if (this->data_ == nullptr) {
      return;
    }
    for (size_t index = Count; index > 0U; --index) {
      this->data_[index - 1U].~T();
    }
    free(this->data_);  // NOLINT(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
    this->data_ = nullptr;
  }

  T* data() { return this->data_; }
  const T* data() const { return this->data_; }
  static constexpr size_t size() { return Count; }
  explicit operator bool() const { return this->data_ != nullptr; }

  T& operator[](size_t index) { return this->data_[index]; }
  const T& operator[](size_t index) const { return this->data_[index]; }

 private:
  T* data_{nullptr};
};

}  // namespace esphome::openquatt_common
