#pragma once
#include <algorithm>
#include <cassert>
#include <random>
#include <vector>

#include "../../engine/ecs/sparse_set.h"
#include "../test_lib.h"

// ------------------------------------------------------------
// TESTS
// ------------------------------------------------------------
TEST(test_sparse_basic_ops) {
  motrix::engine::SparseSet<uint32_t, uint32_t> s;

  for (int i = 0; i < 10000; i++) s.insert(i, i * 2);

  for (int i = 0; i < 10000; i++) assert(s.contains(i) && s.get(i) == i * 2);

  for (int i = 0; i < 5000; i++) s.erase(i);

  for (int i = 0; i < 5000; i++) assert(!s.contains(i));

  for (int i = 5000; i < 10000; i++) assert(s.contains(i));
}

TEST(test_sparse_random_stress) {
  motrix::engine::SparseSet<uint32_t, uint32_t> s;

  const int N = 200000;
  std::vector<uint32_t> keys(N);
  for (int i = 0; i < N; i++) keys[i] = i;

  std::mt19937 rng(1234);
  std::shuffle(keys.begin(), keys.end(), rng);

  for (int k : keys) s.insert(k, k + 10);

  for (int i = 0; i < N / 3; i++) s.erase(keys[i]);

  int alive = 0;
  for (int k : keys)
    if (s.contains(k)) alive++;

  assert(alive > 0);
}

TEST(test_sparse_fragmentation) {
  motrix::engine::SparseSet<uint32_t, uint32_t> s;
  const int N = 200000;

  for (int i = 0; i < N; i++) s.insert(i, i);

  for (int i = 0; i < N; i += 2) s.erase(i);

  for (int i = 0; i < N; i++) s.insert(i, i * 3);

  for (int i = 0; i < N; i++) assert(s.contains(i));
}

TEST(test_sparse_reinsertion) {
  motrix::engine::SparseSet<uint32_t, uint32_t> s;
  s.insert(10, 1);
  s.insert(20, 2);
  s.erase(10);
  s.insert(10, 3);

  assert(s.get(10) == 3);
  assert(s.get(20) == 2);
}

TEST(test_sparse_random_iteration) {
  motrix::engine::SparseSet<uint32_t, uint32_t> s;

  const int N = 50000;
  for (int i = 0; i < N; i++) s.insert(i, i);

  std::vector<uint32_t> seen;
  seen.reserve(N);

  s.for_each([&](auto key, auto& value) { seen.push_back(key); });

  assert(seen.size() == N);
}
