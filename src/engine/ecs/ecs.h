// engine/ecs/ecs.h
#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../logger.h"
#include "sparse_set.h"

namespace motrix::engine {

/**
 * ============================================================================
 * ECS
 * ============================================================================
 *
 * Entity-Component System with:
 *
 *      - stable entity handles (index + generation)
 *      - sparse packed storage per component type
 *      - dense runtime component IDs
 *      - flat bitmask signatures per entity
 *      - cache-friendly multi-component iteration
 *
 * Characteristics:
 *   • O(1) entity creation
 *   • O(1) entity destruction
 *   • O(1) component insert/remove
 *   • O(1) component lookup
 *   • packed dense iteration
 *
 * Layout:
 *
 *   Entity:
 *
 *      index   -> slot in version table
 *      version -> generation counter for stale-handle detection
 *
 *   Components:
 *
 *      each component type owns one SparseSet<T>
 *
 *   Signatures:
 *
 *      entity_masks[e] = component bitset for entity e
 *
 *      bit N => component type N attached
 *
 * View iteration:
 *
 *   • chooses the smallest dense storage
 *   • checks signature mask
 *   • resolves component references only on match
 *
 * ============================================================================
 */
struct Entity {
  std::uint32_t index;
  std::uint32_t version;

  bool operator==(const Entity& other) const {
    return index == other.index && version == other.version;
  }

  bool operator!=(const Entity& other) const { return !(*this == other); }
};

inline constexpr Entity INVALID_ENTITY{
  UINT32_MAX,
  UINT32_MAX,
};

/**
 * ============================================================================
 * Component type ID generator
 * ============================================================================
 *
 * Each distinct T receives one dense global runtime ID:
 *
 *      Position -> storage slot
 *      Bit      -> signature bit
 *
 * IDs are assigned lazily on first use.
 *
 * ============================================================================
 */
struct ComponentBase {
 protected:
  static inline std::size_t count = 0;
};

template <typename T>
struct Component : ComponentBase {
  static std::size_t id() {
    static const std::size_t value = count++;
    return value;
  }

  static const char* name() { return T::Name.data(); }
};

/**
 * ============================================================================
 * BitMask helper
 * ============================================================================
 *
 * Utility for flat uint64_t component signatures.
 *
 * Storage layout:
 *
 *      [ block0 | block1 | block2 | ... ]
 *
 * Each block stores 64 component flags.
 *
 * ============================================================================
 */
class BitMaskHelper {
 public:
  static constexpr std::size_t BLOCK_BITS = 64;

  [[nodiscard]] static constexpr std::size_t blocks_for_bits(std::size_t bits) {
    return (bits + BLOCK_BITS - 1) / BLOCK_BITS;
  }

  static inline void set_bit(std::uint64_t* mask, std::size_t bit) {
    mask[bit / BLOCK_BITS] |= (std::uint64_t(1) << (bit % BLOCK_BITS));
  }

  static inline void reset_bit(std::uint64_t* mask, std::size_t bit) {
    mask[bit / BLOCK_BITS] &= ~(std::uint64_t(1) << (bit % BLOCK_BITS));
  }

  /**
   * Returns true when:
   *
   *      entity_mask contains all bits in required_mask
   */
  static inline bool test_mask_match(const std::uint64_t* entity_mask,
                                     const std::uint64_t* required_mask,
                                     std::size_t blocks) {
    for (std::size_t i = 0; i < blocks; ++i) {
      if ((entity_mask[i] & required_mask[i]) != required_mask[i]) return false;
    }
    return true;
  }
};

/**
 * ============================================================================
 * ECS
 * ============================================================================
 *
 * Runtime storage:
 *
 *      component_storages_[id] -> SparseSet<T>
 *
 * Entity lifecycle:
 *
 *      create  -> new/recycled slot
 *      destroy -> generation bump + storage cleanup
 *
 * Signatures:
 *
 *      entity_masks_[entity * mask_blocks + block]
 *
 * ============================================================================
 */
class ECS {
  struct IStorageBase {
    virtual ~IStorageBase() = default;

    /**
     * Remove entity from storage without type knowledge.
     */
    virtual void erase_entity(std::uint32_t index) = 0;

    /**
     * Dense component count.
     */
    virtual std::size_t dense_size() const = 0;

    /**
     * Iterate dense entity indices.
     */
    virtual void for_each_raw(std::function<void(std::uint32_t)> fn) = 0;
  };

  template <typename T>
  struct Storage final : IStorageBase {
    SparseSet<T> set;

    void erase_entity(std::uint32_t index) override { set.erase(index); }

    std::size_t dense_size() const override { return set.size(); }

    void for_each_raw(std::function<void(std::uint32_t)> fn) override {
      for (auto e : set.entities()) fn(e);
    }
  };

 public:
  ECS() = default;

  void print_entities(
    logger::Level lvl = logger::Level::Debug,
    const std::vector<std::string>& exclude_components = {}) const {
    std::vector<std::vector<std::string>> rows;

    if (versions_.empty()) {
      rows.push_back({"-", "-", "-", "No entities allocated."});
    } else {
      for (size_t i = 0; i < versions_.size(); ++i) {
        if (std::find(free_list_.begin(), free_list_.end(), i) !=
            free_list_.end())
          continue;

        // Mask
        std::stringstream mask_ss;
        mask_ss << "[";
        for (size_t b = 0; b < mask_blocks_; ++b) {
          if (b > 0) mask_ss << " ";
          mask_ss << entity_masks_[i * mask_blocks_ + b];
        }
        mask_ss << "]";

        // Components
        std::stringstream comp_ss;
        comp_ss << "[";
        bool first = true;

        bool skip_entity = false;  // Flag to check exclude list

        for (size_t cid = 0; cid < component_names_.size(); ++cid) {
          size_t block = cid / 64;
          size_t bit = cid % 64;

          if (block >= mask_blocks_) continue;

          if (entity_masks_[i * mask_blocks_ + block] & (1ull << bit)) {
            const std::string& comp_name = component_names_[cid];

            // Check if this component is in the exclude list
            if (std::find(exclude_components.begin(), exclude_components.end(),
                          comp_name) != exclude_components.end()) {
              skip_entity = true;
              break;  // Skip this entity entirely
            }

            if (!first) comp_ss << ", ";
            comp_ss << comp_name;
            first = false;
          }
        }

        if (skip_entity) continue;

        comp_ss << "]";
        rows.push_back({std::to_string(i), std::to_string(versions_[i]),
                        mask_ss.str(), comp_ss.str()});
      }
    }

    logger::table(lvl, "ECS ENTITY DUMP",
                  {"Index", "Version", "Mask", "Components"}, rows);
  }

  /**
   * --------------------------------------------------------------------------
   * Entity lifecycle
   * --------------------------------------------------------------------------
   */
  Entity create_entity() {
    if (!free_list_.empty()) {
      const std::uint32_t index = free_list_.back();
      free_list_.pop_back();
      return {index, versions_[index]};
    }
    const std::uint32_t index = static_cast<std::uint32_t>(versions_.size());
    versions_.push_back(1);
    ensure_entity_mask_size(versions_.size());
    return {index, 1};
  }

  /**
   * Destroy entity:
   *
   *   - invalidate current handle
   *   - erase all attached components
   *   - clear signature bits
   *   - recycle slot
   */
  void destroy_entity(Entity entity) {
    if (!is_alive(entity)) return;

    ++versions_[entity.index];
    // Corrected: Iterate through vector directly
    for (auto& storage : component_storages_) {
      if (storage) storage->erase_entity(entity.index);
    }

    if (mask_blocks_ > 0) {
      std::uint64_t* mask = &entity_masks_[entity.index * mask_blocks_];
      for (std::size_t i = 0; i < mask_blocks_; ++i) mask[i] = 0;
    }

    update_groups(entity.index);
    free_list_.push_back(entity.index);
  }

  [[nodiscard]] bool is_alive(Entity entity) const {
    return entity.index < versions_.size() &&
           versions_[entity.index] == entity.version;
  }

  /**
   * --------------------------------------------------------------------------
   * Component operations
   * --------------------------------------------------------------------------
   */
  template <typename T, typename... Args>
  T& add(Entity entity, Args&&... args) {
    assert(is_alive(entity));
    const std::size_t id = Component<T>::id();
    auto* storage = get_or_create_storage<T>(id);
    T& component =
      storage->set.insert(entity.index, T{std::forward<Args>(args)...});
    set_entity_bit(entity.index, id);
    update_groups(entity.index);
    return component;
  }

  template <typename T>
  bool has(Entity entity) const {
    if (!is_alive(entity)) return false;
    const std::size_t id = Component<T>::id();
    if (id >= component_storages_.size() || !component_storages_[id])
      return false;
    return static_cast<Storage<T>*>(component_storages_[id].get())
      ->set.contains(entity.index);
  }

  template <typename T>
  T& get(Entity entity) {
    assert(is_alive(entity));
    return static_cast<Storage<T>*>(
             component_storages_[Component<T>::id()].get())
      ->set.get(entity.index);
  }

  template <typename T>
  void remove(Entity entity) {
    if (!is_alive(entity)) return;
    const std::size_t id = Component<T>::id();
    if (id < component_storages_.size() && component_storages_[id]) {
      component_storages_[id]->erase_entity(entity.index);
      reset_entity_bit(entity.index, id);
      update_groups(entity.index);
    }
  }

  /**
   * --------------------------------------------------------------------------
   * Multi-component iteration
   * --------------------------------------------------------------------------
   *
   * Strategy:
   *
   *   1. Resolve storages
   *   2. Select smallest dense set
   *   3. Filter using bitmask
   *   4. Fetch components only for matches
   *
   * Minimizes random access when component densities differ.
   */
  template <typename... Ts, typename Func>
  void view(Func&& fn) {
    std::array<IStorageBase*, sizeof...(Ts)> stores = {
      get_storage_by_id(Component<Ts>::id())...};
    for (auto* s : stores)
      if (!s) return;

    IStorageBase* best = stores[0];
    for (auto* s : stores) {
      if (s->dense_size() < best->dense_size()) best = s;
    }

    std::vector<std::uint64_t> required(mask_blocks_, 0ull);
    (set_bit_in_mask(required, Component<Ts>::id()), ...);

    best->for_each_raw([&](std::uint32_t entity_index) {
      if (BitMaskHelper::test_mask_match(mask_ptr(entity_index),
                                         required.data(), mask_blocks_)) {
        fn(Entity{entity_index, versions_[entity_index]},
           static_cast<Storage<Ts>*>(
             component_storages_[Component<Ts>::id()].get())
             ->set.get(entity_index)...);
      }
    });
  }

  /**
   * --------------------------------------------------------------------------
   * Cached Multi-component iteration (Groups)
   * --------------------------------------------------------------------------
   */
  template <typename... Ts, typename Func>
  void group_view(Func&& fn) {
    std::array<std::size_t, sizeof...(Ts)> ids = {Component<Ts>::id()...};
    for (auto id : ids)
      if (id >= component_storages_.size() || !component_storages_[id]) return;

    std::vector<std::uint64_t> required(mask_blocks_, 0ull);
    (set_bit_in_mask(required, Component<Ts>::id()), ...);

    Group* target_group = nullptr;
    for (auto& g : groups_) {
      if (g->required_mask == required) {
        target_group = g.get();
        break;
      }
    }

    if (!target_group) {
      auto new_group = std::make_unique<Group>();
      new_group->required_mask = required;
      target_group = new_group.get();
      groups_.push_back(std::move(new_group));

      for (std::size_t i = 0; i < versions_.size(); ++i)
        if (is_alive({static_cast<std::uint32_t>(i), versions_[i]}))
          evaluate_group(static_cast<std::uint32_t>(i), *target_group);
    }

    for (auto entity_index : target_group->entities.entities()) {
      fn(Entity{entity_index, versions_[entity_index]},
         static_cast<Storage<Ts>*>(
           component_storages_[Component<Ts>::id()].get())
           ->set.get(entity_index)...);
    }
  }

 private:
  /**
     * --------------------------------------------------------------------------
     * Cached Groups
     * --------------------------------------------------------------------------
     */
  struct Group {
    std::vector<std::uint64_t> required_mask;
    SparseSet<std::uint32_t> entities;
  };

  std::vector<std::unique_ptr<Group>> groups_;

  void evaluate_group(std::uint32_t entity_index, Group& group) {
    bool matches = BitMaskHelper::test_mask_match(
      mask_ptr(entity_index), group.required_mask.data(), mask_blocks_);
    bool contains = group.entities.contains(entity_index);

    if (matches && !contains)
      group.entities.insert(entity_index, entity_index);
    else if (!matches && contains)
      group.entities.erase(entity_index);
  }

  void update_groups(std::uint32_t entity_index) {
    for (auto& g : groups_) evaluate_group(entity_index, *g);
  }

  /**
   * --------------------------------------------------------------------------
   * Storage lookup / creation
   * --------------------------------------------------------------------------
   */
  IStorageBase* get_storage_by_id(std::size_t id) const {
    return id < component_storages_.size() ? component_storages_[id].get()
                                           : nullptr;
  }

  template <typename T>
  Storage<T>* get_or_create_storage(std::size_t id) {
    if (id >= component_storages_.size()) {
      component_storages_.resize(id + 1);
      component_names_.resize(id + 1);
      expand_masks_for_new_component(id + 1);
    }

    if (!component_storages_[id]) {
      component_storages_[id] = std::make_unique<Storage<T>>();
      component_names_[id] = Component<T>::name();
    }

    return static_cast<Storage<T>*>(component_storages_[id].get());
  }

  /**
   * Expand signature storage when a new component ID exceeds current capacity.
   */
  void expand_masks_for_new_component(std::size_t new_count) {
    const std::size_t new_blocks = BitMaskHelper::blocks_for_bits(new_count);
    if (new_blocks == mask_blocks_) return;

    std::vector<std::uint64_t> new_masks(versions_.size() * new_blocks, 0ull);
    for (std::size_t entity = 0; entity < versions_.size(); ++entity) {
      for (std::size_t b = 0; b < mask_blocks_; ++b) {
        new_masks[entity * new_blocks + b] =
          entity_masks_[entity * mask_blocks_ + b];
      }
    }
    entity_masks_.swap(new_masks);
    for (auto& g : groups_) g->required_mask.resize(new_blocks, 0ull);
    mask_blocks_ = new_blocks;
  }

  void ensure_entity_mask_size(std::size_t entity_count) {
    entity_masks_.resize(entity_count * mask_blocks_, 0ull);
  }

  const std::uint64_t* mask_ptr(std::size_t entity_index) const {
    return mask_blocks_ == 0 ? nullptr
                             : &entity_masks_[entity_index * mask_blocks_];
  }

  void set_entity_bit(std::size_t entity_index, std::size_t id) {
    BitMaskHelper::set_bit(&entity_masks_[entity_index * mask_blocks_], id);
  }

  void reset_entity_bit(std::size_t entity_index, std::size_t id) {
    if (mask_blocks_ > 0)
      BitMaskHelper::reset_bit(&entity_masks_[entity_index * mask_blocks_], id);
  }

  void set_bit_in_mask(std::vector<std::uint64_t>& mask, std::size_t bit) {
    const std::size_t block = bit / 64;
    if (block >= mask.size()) mask.resize(block + 1, 0ull);
    mask[block] |= (1ull << (bit % 64));
  }

 private:
  /**
   * --------------------------------------------------------------------------
   * Storage
   * --------------------------------------------------------------------------
   */
  std::vector<std::string> component_names_;
  std::vector<std::unique_ptr<IStorageBase>> component_storages_;
  std::vector<std::uint32_t> versions_;
  std::vector<std::uint32_t> free_list_;
  std::vector<std::uint64_t> entity_masks_;
  std::size_t mask_blocks_ = 0;
};

}  // namespace motrix::engine

namespace std {
template <>
struct hash<motrix::engine::Entity> {
  std::size_t operator()(const motrix::engine::Entity& e) const noexcept {
    return std::hash<uint32_t>{}(e.index) ^
           (std::hash<uint32_t>{}(e.version) << 1);
  }
};
}  // namespace std
