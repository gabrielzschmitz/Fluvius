#pragma once
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <vector>

// ------------------------------------------------------------
// ANSI COLORS
// ------------------------------------------------------------
#define C_RESET "\033[0m"
#define C_RED "\033[31m"
#define C_GREEN "\033[32m"
#define C_YELLOW "\033[33m"
#define C_BLUE "\033[34m"
#define C_MAGENTA "\033[35m"
#define C_CYAN "\033[36m"
#define C_BOLD "\033[1m"

// =======================================================
// TEST REGISTRY
// =======================================================
class TestRegistry {
public:
  struct Entry {
    const char *name;
    std::function<void()> func;
  };
  std::vector<Entry> tests;

  static TestRegistry &instance() {
    static TestRegistry r;
    return r;
  }

  void add(const char *name, std::function<void()> fn) {
    tests.push_back({name, fn});
  }

  void run_all() {
    std::cout << C_MAGENTA << C_BOLD << "Running Tests" << C_RESET << "\n";
    for (auto &t : tests) {
      std::cout << C_BLUE << C_BOLD << "[TEST] " << C_RESET << t.name
                << " ... ";
      t.func();
      std::cout << C_GREEN << "OK" << C_RESET << "\n";
    }
  }

private:
  TestRegistry() = default;
};

struct TestRegistrar {
  TestRegistrar(const char *name, std::function<void()> fn) {
    TestRegistry::instance().add(name, fn);
  }
};

#define TEST(name)                                                             \
  static void name();                                                          \
  static TestRegistrar _test_reg_##name(#name, name);                          \
  static void name()

// =======================================================
// BENCHMARK REGISTRY
// =======================================================
class BenchRegistry {
public:
  struct Entry {
    const char *name;
    std::function<void()> fn;
  };
  std::vector<Entry> benches;

  static BenchRegistry &instance() {
    static BenchRegistry r;
    return r;
  }

  void add(const char *name, std::function<void()> fn) {
    benches.push_back({name, fn});
  }

  // ----------------------------------------------
  // Benchmark execution with averaging + stddev
  // ----------------------------------------------
  void run_all(int runs = 5,  // number of measurement runs
               int warmup = 1 // warmup runs
  ) {
    using clock = std::chrono::high_resolution_clock;

    std::cout << "\n" << C_MAGENTA << C_BOLD << "Running Benchmarks" << C_RESET;

    for (auto &b : benches) {
      std::cout << C_CYAN << C_BOLD << "\n[BENCH] " << C_RESET << b.name
                << "\n";

      // warmup ---------------------------------------------------------
      for (int i = 0; i < warmup; i++)
        b.fn();

      std::vector<double> times;
      times.reserve(runs);

      // timed runs -----------------------------------------------------
      for (int i = 0; i < runs; i++) {
        auto start = clock::now();
        b.fn();
        auto end = clock::now();

        double ms =
            std::chrono::duration<double, std::milli>(end - start).count();
        times.push_back(ms);
      }

      // stats ----------------------------------------------------------
      double sum = 0;
      for (double t : times)
        sum += t;
      double avg = sum / runs;

      double variance = 0;
      for (double t : times)
        variance += (t - avg) * (t - avg);
      variance /= runs;
      double stddev = std::sqrt(variance);

      // output ---------------------------------------------------------
      std::cout << "  avg:  " << C_GREEN << avg << " ms" << C_RESET << "\n";
      std::cout << "  std:  " << C_YELLOW << stddev << " ms" << C_RESET << "\n";

      std::cout << "  runs: ";
      for (double t : times)
        std::cout << C_BLUE << t << " " << C_RESET;
    }
  }
};

struct BenchRegistrar {
  BenchRegistrar(const char *name, std::function<void()> fn) {
    BenchRegistry::instance().add(name, fn);
  }
};

#define BENCH(name)                                                            \
  static void name();                                                          \
  static BenchRegistrar _bench_reg_##name(#name, name);                        \
  static void name()
