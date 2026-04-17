// g++ -std=c++17 -O3 -DRUN_TESTS -march=native -o tests tests.cpp && ./tests
#include "ecs/bench_ecs.h"
#include "ecs/test_ecs.h"
#include "sparse_set/bench_sparse.h"
#include "sparse_set/test_sparse.h"
#include "test_lib.h"

#ifdef RUN_TESTS
int main() {
  TestRegistry::instance().run_all();

  int runs = 20, warmup = 5;
  BenchRegistry::instance().run_all(runs, warmup);
  std::cout << "\n";
}
#endif
