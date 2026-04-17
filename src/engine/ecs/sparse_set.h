// engine/ecs/sparse_set.h
#pragma once

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <type_traits>
#include <vector>

namespace motrix::engine {

/**
 * ============================================================================
 * SparseSet<T, Entity>
 * ============================================================================
 *
 * High-performance sparse-dense associative storage:
 *
 *      Entity ID -> Component T
 *
 * Characteristics:
 *   • O(1) insert
 *   • O(1) erase (swap-remove)
 *   • O(1) contains
 *   • O(1) get
 *   • tightly packed iteration
 *
 * Layout:
 *
 *   sparse[e] -> dense index
 *
 *   dense_entities = [e0, e1, e2, ...]
 *   components     = [c0, c1, c2, ...]
 *
 * Sparse storage is paged to avoid massive contiguous allocations.
 *
 * ============================================================================
 */
template <typename T, typename Entity = std::uint32_t>

class SparseSet {
 public:
  static_assert(!std::is_void_v<T>,
                "SparseSet<void> is invalid. Use an explicit tag component.");

  using value_type = T;
  using entity_type = Entity;

  static constexpr Entity INVALID = static_cast<Entity>(-1);

  static constexpr std::size_t PAGE_BITS = 11;
  static constexpr std::size_t PAGE_CAPACITY = 1ull << PAGE_BITS;
  static constexpr std::size_t PAGE_MASK = PAGE_CAPACITY - 1;

  SparseSet() = default;
  ~SparseSet() = default;

  SparseSet(const SparseSet&) = delete;
  SparseSet& operator=(const SparseSet&) = delete;

  SparseSet(SparseSet&&) noexcept = default;
  SparseSet& operator=(SparseSet&&) noexcept = default;

  /**
   * --------------------------------------------------------------------------
   * Memory Management
   * --------------------------------------------------------------------------
   */
  void reserve(std::size_t capacity) {
    dense_entities_.reserve(capacity);
    components_.reserve(capacity);
  }

  /**
   * --------------------------------------------------------------------------
   * Lookup
   * --------------------------------------------------------------------------
   */
  [[nodiscard]] bool contains(Entity entity) const {
    const Entity* slot = sparse_ptr(entity);
    if (!slot) return false;

    const Entity index = *slot;
    return index != INVALID && index < dense_entities_.size() &&
           dense_entities_[index] == entity;
  }

  /**
   * --------------------------------------------------------------------------
   * Insert / update
   * --------------------------------------------------------------------------
   */
  T& insert(Entity entity, const T& value = T()) {
    const std::size_t page_index = entity >> PAGE_BITS;
    const std::size_t offset = entity & PAGE_MASK;

    ensure_page(page_index);
    Entity& slot = pages_[page_index][offset];

    if (slot != INVALID && slot < dense_entities_.size() &&
        dense_entities_[slot] == entity) {
      components_[slot] = value;
      return components_[slot];
    }

    slot = static_cast<Entity>(dense_entities_.size());

    dense_entities_.push_back(entity);
    components_.push_back(value);

    return components_.back();
  }

  /**
   * --------------------------------------------------------------------------
   * Erase (swap-remove)
   * --------------------------------------------------------------------------
   */
  void erase(Entity entity) {
    const std::size_t page_index = entity >> PAGE_BITS;
    if (page_index >= pages_.size() || !pages_[page_index]) return;

    const std::size_t offset = entity & PAGE_MASK;
    const Entity index = pages_[page_index][offset];

    if (index == INVALID || index >= dense_entities_.size() ||
        dense_entities_[index] != entity) {
      return;
    }

    const Entity last_index = static_cast<Entity>(dense_entities_.size() - 1);
    const Entity last_entity = dense_entities_[last_index];

    dense_entities_[index] = last_entity;
    components_[index] = std::move(components_[last_index]);

    const std::size_t last_page_idx = last_entity >> PAGE_BITS;
    const std::size_t last_offset = last_entity & PAGE_MASK;
    pages_[last_page_idx][last_offset] = index;

    pages_[page_index][offset] = INVALID;

    dense_entities_.pop_back();
    components_.pop_back();
  }

  /**
   * --------------------------------------------------------------------------
   * Access
   * --------------------------------------------------------------------------
   */
  T& get(Entity entity) {
    assert(contains(entity));
    // Fast path: bypass ensure_page() since the entity is guaranteed to exist.
    const std::size_t page_index = entity >> PAGE_BITS;
    const std::size_t offset = entity & PAGE_MASK;

    const Entity index = pages_[page_index][offset];
    return components_[index];
  }

  const T& get(Entity entity) const {
    assert(contains(entity));
    const std::size_t page_index = entity >> PAGE_BITS;
    const std::size_t offset = entity & PAGE_MASK;

    const Entity index = pages_[page_index][offset];
    return components_[index];
  }

  T& operator[](Entity entity) { return get(entity); }
  const T& operator[](Entity entity) const { return get(entity); }

  /**
   * --------------------------------------------------------------------------
   * Iteration
   * --------------------------------------------------------------------------
   */
  template <typename Func>
  void for_each(Func&& fn) {
    const std::size_t count = dense_entities_.size();
    for (std::size_t i = 0; i < count; ++i) {
      fn(dense_entities_[i], components_[i]);
    }
  }

  template <typename Func>
  void for_each(Func&& fn) const {
    const std::size_t count = dense_entities_.size();
    for (std::size_t i = 0; i < count; ++i) {
      fn(dense_entities_[i], components_[i]);
    }
  }

  /**
   * --------------------------------------------------------------------------
   * Metadata
   * --------------------------------------------------------------------------
   */
  [[nodiscard]] std::size_t size() const { return dense_entities_.size(); }

  [[nodiscard]] const std::vector<Entity>& entities() const {
    return dense_entities_;
  }

  [[nodiscard]] std::vector<T>& data() { return components_; }

  [[nodiscard]] const std::vector<T>& data() const { return components_; }

 private:
  /**
   * --------------------------------------------------------------------------
   * Sparse page allocation
   * --------------------------------------------------------------------------
   */
  void ensure_page(std::size_t page_index) {
    if (page_index >= pages_.size()) {
      pages_.resize(page_index + 1);
    }

    if (!pages_[page_index]) {
      auto page = std::make_unique<Entity[]>(PAGE_CAPACITY);

      for (std::size_t i = 0; i < PAGE_CAPACITY; ++i) {
        page[i] = INVALID;
      }

      pages_[page_index] = std::move(page);
    }
  }

  Entity& sparse_ref(Entity entity) {
    const std::size_t page_index = entity >> PAGE_BITS;
    const std::size_t offset = entity & PAGE_MASK;

    ensure_page(page_index);
    return pages_[page_index][offset];
  }

  const Entity* sparse_ptr(Entity entity) const {
    const std::size_t page_index = entity >> PAGE_BITS;

    if (page_index >= pages_.size() || !pages_[page_index]) {
      return nullptr;
    }

    return &pages_[page_index][entity & PAGE_MASK];
  }

 private:
  /**
   * --------------------------------------------------------------------------
   * Storage
   * --------------------------------------------------------------------------
   */
  std::vector<std::unique_ptr<Entity[]>> pages_;

  std::vector<Entity> dense_entities_;
  std::vector<T> components_;
};

}  // namespace motrix::engine
