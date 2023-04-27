/**
 * @file nodes_sieve.cpp
 *
 * @section LICENSE
 *
 * The MIT License
 *
 * @copyright Copyright (c) 2022 TileDB, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * @section DESCRIPTION
 *
 * Demo program sieve of Eratosthenes, function components for block
 * (and parallelizable) implementation.
 *
 * The block sieve algorithm begins by sequentially finding all the primes in
 * [2, sqrt(n)).  Using that initial set of primes, the algorithm finds primes
 * in each block of numbers delimited by
 *
 *       [sqrt(n) + p*block_size, sqrt(n) + (p+1)*block_size)
 *
 *  for p in [0, n/blocksize).
 *
 * This file provides a decomposition of that computation into the following
 * five tasks:
 *   input_body() generates p, a sequence of integers, starting at 0
 *   gen_range() creates a bitmap for indicating primality (or not)
 *   range_sieve() applies sieve, to block p, using initial set of
 *     sqrt(n) primes and records results in bitmap obtained from
 *     gen_range()
 *   sieve_to_primes_part() generates a list of prime numbers from the
 *     bitmap generated by range_sieve()
 *   output_body() saves the list of primes in a vector at location p+1.
 *     The original set of sqrt(n) primes is stored at loccation 0.
 *   A set of n/block_size parallel task chains is launched to carry
 *     out the computation.
 *
 * These functions take regular values as input parameters and return regular
 * values. They can be composed together to produce the sieve algorithm
 * described above.
 */

#ifdef _MSC_VER
int main() {
}
#else
#include <cassert>
#include <chrono>
#include <cmath>
#include <functional>
#include <future>
#include <iostream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

/**
 * Nullify the CHECK macro that might be spread throughout the code for
 * debugging/testing purposes.
 */
#define CHECK(str)

#include "experimental/tiledb/common/dag/edge/edge.h"
#include "experimental/tiledb/common/dag/nodes/nodes.h"
#include "experimental/tiledb/common/dag/ports/ports.h"
#include "experimental/tiledb/common/dag/state_machine/fsm.h"
#include "experimental/tiledb/common/dag/state_machine/item_mover.h"
#include "experimental/tiledb/common/dag/state_machine/policies.h"
#include "experimental/tiledb/common/dag/state_machine/test/helpers.h"
#include "experimental/tiledb/common/dag/state_machine/test/types.h"
#include "experimental/tiledb/common/dag/utility/print_types.h"

// #include "experimental/tiledb/common/dag/execution/threadpool.h"

using namespace tiledb::common;
using namespace std::placeholders;

/*
 * File-local variables for enabling debugging and tracing
 */
static bool debug = false;
static bool chart = false;

/*
 * Function to enable time based tracing of different portions of program
 * execution.
 */
template <class TimeStamp, class StartTime>
void stamp_time(
    const std::string& msg,
    size_t index,
    TimeStamp& timestamps,
    std::atomic<size_t>& time_index,
    StartTime start_time) {
  if (debug) {
    std::cout << "Thread " << index << std::endl;
  }

  if (chart) {
    timestamps[time_index++] = std::make_tuple(
        time_index.load(),
        index,
        msg,
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start_time)
            .count());
  }
}

/**
 * Some convenience type aliases
 */
template <class bool_t>
using part_info =
    std::tuple<size_t, size_t, size_t, std::shared_ptr<std::vector<bool_t>>>;
using prime_info = std::tuple<size_t, std::shared_ptr<std::vector<size_t>>>;

/**
 * Takes a vector of "bool" (which may be actual bool, or may be char) and
 * extracts the indicated prime numbers.
 */
template <class bool_t>
auto sieve_to_primes(std::vector<bool_t>& sieve) {
  std::vector<size_t> primes;

  for (size_t i = 2; i < sieve.size(); ++i) {
    if (sieve[i]) {
      primes.push_back(i);
    }
  }
  return primes;
}

/**
 * Takes a vector of bool which has a true value for any number that is a prime,
 * and converts to a vector of prime numbers.
 */
template <class bool_t>
auto sieve_to_primes(
    std::vector<bool_t>& sieve,
    std::vector<size_t>& base_primes,
    size_t sqrt_n) {
  std::vector<size_t> primes(base_primes);

  for (size_t i = sqrt_n; i < sieve.size(); ++i) {
    assert(i < sieve.size());
    if (sieve[i]) {
      primes.push_back(i);
    }
  }
  return primes;
}

/**
 * Purely sequential program for finding primes in the range 0 to n.  Returns a
 * vector of "bool" where each location whose index corresponds to a prime
 * number is true and all others are false.
 */
template <class bool_t>
auto sieve_seq(size_t n) {
  if (debug)
    std::cout << "** I am running too" << std::endl;

  std::vector<bool_t> sieve(n, true);

  sieve[0] = sieve[1] = true;

  size_t sqrt_n = static_cast<size_t>(std::ceil(std::sqrt(n)));

  for (size_t i = 2; i < sqrt_n; ++i) {
    assert(i < sieve.size());
    if (sieve[i]) {
      for (size_t j = i * i; j < n; j += i) {
        assert(j < sieve.size());
        sieve[j] = false;
      }
    }
  }

  return sieve;
}

/**
 * Class for generating a (thread safe) sequence of integers, starting at 0
 *
 * @return integer, value one greater than previously returned
 */
class input_body {
  std::atomic<size_t> p{0};

 public:
  input_body()
      : p{0} {
  }
  input_body(const input_body& rhs)
      : p{rhs.p.load()} {
  }
  size_t operator()() {
    if (debug)
      std::cout << "input_body " << p << std::endl;
    return p++;
  }
};

/**
 * Create a bitmap for storing sieve results
 * @tparam bool_t type of elements for bitmap
 * @param the block number to create bitmap for
 * @return tuple with block number, bitmap range, and bitmap
 */
template <class bool_t>
auto gen_range(size_t p, size_t block_size, size_t sqrt_n, size_t n) {
  if (debug)
    std::cout << "gen_range " << p << std::endl;

  size_t sieve_start = std::min(sqrt_n + p * block_size, n);
  size_t sieve_end = std::min(sieve_start + block_size, n);
  return std::make_tuple(
      p + 1,
      sieve_start,
      sieve_end,
      std::make_shared<std::vector<bool_t>>(sieve_end - sieve_start, true));
};

/**
 * Find primes in indicated range and record in bitmap
 * @param tuple with block number, bitmap range, and bitmap
 * @return tuple with block number, bitmap range, and bitmap
 */
template <class bool_t>
auto range_sieve(
    const part_info<bool_t>& in, const std::vector<size_t>& base_primes) {
  auto [p, sieve_start, sieve_end, local_sieve] = in;
  if (debug)
    std::cout << "range_sieve " << p << std::endl;

  for (size_t i = 0; i < base_primes.size(); ++i) {
    assert(i < base_primes.size());
    size_t pr = base_primes[i];

    size_t q = (pr + sieve_start - 1) / pr;
    q *= pr;

    for (size_t j = q - sieve_start; j < sieve_end - sieve_start; j += pr) {
      assert(j < local_sieve->size());
      (*local_sieve)[j] = false;
    }
  }

  return in;
};

/**
 * Create list of primes from bitmap
 * @param tuple with block number, bitmap range, and bitmap
 * @return tuple with block number and shared_ptr to vector of primes
 */
template <class bool_t>
auto sieve_to_primes_part(const part_info<bool_t>& in) {
  auto [p, sieve_start, sieve_end, local_sieve] = in;
  if (debug)
    std::cout << "sieve_to_primes_part " << p << std::endl;

  std::vector<size_t> primes;
  primes.reserve(local_sieve->size());
  for (size_t i = 0; i < local_sieve->size(); ++i) {
    assert(i < local_sieve->size());
    if ((*local_sieve)[i]) {
      primes.push_back(i + sieve_start);
    }
  }
  return std::make_tuple(p, std::make_shared<std::vector<size_t>>(primes));
};

/**
 * Store list of primes in vector
 * @param tuple with block number and shared_ptr to vector of primes
 */
auto output_body(
    const prime_info& in,
    std::vector<std::shared_ptr<std::vector<size_t>>>& prime_list) {
  auto [p, primes] = in;
  if (debug)
    std::cout << "output_body " << p << " / " << prime_list.size() << std::endl;
  assert(p < prime_list.size());
  prime_list[p] = primes;
};

/**
 * Some structures to simulate a task graph.  Here, a "task graph" is a tuple of
 * task graph nodes.  We later construct a vector of these graphs so that we can
 * run multiple graphs in parallel.
 *
 * @todo (IMPORTANT) Only run a subset of graphs at a time, rather than all of
 * them.
 *
 * @todo Run with TileDB ThreadPool.
 */
template <class... Ts>
using the = std::vector<std::tuple<Ts...>>;

// For future evaluation
// ThreadPool<true, false, true> tp(std::thread::hardware_concurrency());

/**
 * Utility function for putting Ith node in a single sieve task graph into the
 * vector of task graph nodes.
 */
template <size_t I, class Futs, class Graph, class TimeStamp, class StartTime>
void do_emplace(
    Futs& futs,
    Graph& graph,
    size_t N,
    size_t w,
    TimeStamp& timestamps,
    std::atomic<size_t>& time_index,
    StartTime start_time) {
  futs.emplace_back(std::async(std::launch::async, [&, N, w]() {
    stamp_time("start", I, timestamps, time_index, start_time);
    std::get<I>(graph[w]).run_for(N);
    stamp_time("stop", I, timestamps, time_index, start_time);
  }));
};

/*
 * Attempt at generating all graph nodes for the vector in one call.
 */
template <
    class Futs,
    class Graph,
    class TimeStamp,
    class StartTime,
    size_t... Is>
void do_emplace_x(
    Futs& futs,
    Graph& graph,
    size_t N,
    size_t w,
    TimeStamp& timestamps,
    std::atomic<size_t>& time_index,
    StartTime start_time,
    std::index_sequence<Is...>) {
  ((futs.emplace_back(std::async(
       std::launch::async,
       [&, w]() {
         stamp_time("start", Is, timestamps, time_index, start_time);
         std::get<Is>(graph[w]).run_for(N);
         stamp_time("stop", Is, timestamps, time_index, start_time);
       }))),
   ...);

#if 0
  std::apply(
      [&, w](auto&&... args) {
        ((

             /* args.dosomething() */
             futs.emplace_back(std::async(
                 std::launch::async,
                 [&, args, w]() {
                   stamp_time(
                       "start", args, timestamps, time_index, start_time);
                   std::get<args>(graph[w]).run_for(N);
                   stamp_time("stop", args, timestamps, time_index, start_time);
                 }))

             ),
         ...);
      },
      is);
#endif
}

/*
 * Another attempt at generating all graph nodes for the vector in one call.
 */
template <
    class Futs,
    class Graph,
    class TimeStamp,
    class StartTime,
    size_t... Is>
void do_emplace_x_width(
    Futs& futs,
    Graph& graph,
    size_t N,
    size_t width,
    TimeStamp& timestamps,
    std::atomic<size_t>& time_index,
    StartTime start_time,
    std::index_sequence<Is...>) {
  (([&]() {
     for (size_t w = 0; w < width; ++w) {
       futs.emplace_back(std::async(std::launch::async, [&, w]() {
         stamp_time("start", Is, timestamps, time_index, start_time);
         std::get<Is>(graph[w]).run_for(N);
         stamp_time("stop", Is, timestamps, time_index, start_time);
       }));
     }
   }()),
   ...);
}

/**
 * Main sieve function
 *
 * @brief Generate primes from 2 to n using sieve of Eratosthenes.
 * @tparam bool_t the type to use for the "bitmap"
 * @param n upper bound of sieve
 * @param block_size how many primes to search for given a base set of primes
 */
template <template <class> class AsyncMover, class bool_t>
auto sieve_async_block(
    size_t n,
    size_t block_size,
    size_t width,
    bool reverse_order,
    bool grouped,
    [[maybe_unused]] bool use_futures,
    [[maybe_unused]] bool use_threadpool) {
  if (debug)
    std::cout << "== I am running" << std::endl;

  /*
   * Pseudo graph type. A structure to hold simple dag task graph nodes.
   */
  using GraphType =
      the<ProducerNode<AsyncMover, size_t>,
          FunctionNode<AsyncMover, size_t, AsyncMover, part_info<bool_t>>,
          FunctionNode<
              AsyncMover,
              part_info<bool_t>,
              AsyncMover,
              part_info<bool_t>>,
          FunctionNode<AsyncMover, part_info<bool_t>, AsyncMover, prime_info>,
          ConsumerNode<AsyncMover, prime_info>>;

  GraphType graph;
  input_body gen;

  // Use threads instead of tasks
  // std::vector<std::thread> threads;
  // threads.clear();

  std::vector<std::future<void>> futs;
  futs.clear();

  graph.clear();

  size_t sqrt_n = static_cast<size_t>(std::ceil(std::sqrt(n)));

  /* Generate base set of sqrt(n) primes to be used for subsequent sieving */
  auto first_sieve = sieve_seq<bool_t>(sqrt_n);
  std::vector<size_t> base_primes = sieve_to_primes(first_sieve);

  /* Store vector of list of primes (each list generated by separate task
   * chain)
   */
  std::vector<std::shared_ptr<std::vector<size_t>>> prime_list(
      n / block_size + 2 + n % block_size);

  if (debug)
    std::cout << prime_list.size() << std::endl;

  prime_list[0] = std::make_shared<std::vector<size_t>>(base_primes);

  size_t rounds = (n / block_size + 2) / width + 1;

  if (debug)
    std::cout << n << " "
              << " " << block_size << " " << width << " " << rounds
              << std::endl;

  //  using time_t =
  //  std::chrono::time_point<std::chrono::high_resolution_clock>;

  std::vector<std::tuple<size_t, size_t, std::string, double>> timestamps(
      width * rounds * 20);
  std::atomic<size_t> time_index{0};
  auto start_time = std::chrono::high_resolution_clock::now();

  graph.reserve(width);
  std::vector<GraphEdge> edges;
  edges.reserve(4 * width);

  /*
   * Create the "graphs" by emplacing the nodes for each "graph" into a vector.
   */
  for (size_t w = 0; w < width; ++w) {
    if (debug)
      std::cout << "w: " << w << std::endl;

#if 0
    graph.emplace_back(
        std::ref(gen),
        std::bind(gen_range<bool_t>, _1, block_size, sqrt_n, n),
        std::bind(range_sieve<bool_t>, _1, std::cref(base_primes)),
        sieve_to_primes_part<bool_t>,
        std::bind(output_body, _1, std::ref(prime_list)));
#else
    // std::bind(gen_range<bool_t>, _1, block_size, sqrt_n, n),
    // std::bind(range_sieve<bool_t>, _1, std::cref(base_primes)),
        [&](auto&& x) {
      return range_sieve<bool_t>(x, std::cref(base_primes));
        },
        sieve_to_primes_part<bool_t>,
        // std::bind(output_body, _1, std::ref(prime_list))
        [&](auto&& x) {
      output_body(x, std::ref(prime_list)); });
#endif

        /*
         * Connect the nodes in the graph.  We try to keep the edges from going
         * out of scope by putting them into a vector.
         */
        edges.emplace_back(std::move(
            Edge(std::get<0>(graph.back()), std::get<1>(graph.back()))));
        edges.emplace_back(std::move(
            Edge(std::get<1>(graph.back()), std::get<2>(graph.back()))));
        edges.emplace_back(std::move(
            Edge(std::get<2>(graph.back()), std::get<3>(graph.back()))));
        edges.emplace_back(std::move(
            Edge(std::get<3>(graph.back()), std::get<4>(graph.back()))));
        if (debug)
          std::cout << "Post edge" << std::endl;
  }

  size_t N = rounds;

  /*
   * Launch a thread to execute the graph.  We use the "abundant thread"
   * scheduling policy, which launches a thread to run each node in each
   * graph.
   *
   * @todo Only launch a subset of the graphs and launch new ones as running
   * ones complete.  It might be easier to do this with `std::async` rather
   * than `std::thread`.
   */

  /*
   * Put the nodes for every graph sequentially into the vector.
   */
  if (grouped == false && reverse_order == false) {
    for (size_t w = 0; w < width; ++w) {
      do_emplace<0>(futs, graph, N, w, timestamps, time_index, start_time);
      do_emplace<1>(futs, graph, N, w, timestamps, time_index, start_time);
      do_emplace<2>(futs, graph, N, w, timestamps, time_index, start_time);
      do_emplace<3>(futs, graph, N, w, timestamps, time_index, start_time);
      do_emplace<4>(futs, graph, N, w, timestamps, time_index, start_time);
    }
  }

  /*
   * Put the nodes for every graph sequentially into the vector, in reverse
   * order.
   */
  if (grouped == false && reverse_order == true) {
    for (size_t w = 0; w < width; ++w) {
      do_emplace<4>(futs, graph, N, w, timestamps, time_index, start_time);
      do_emplace<3>(futs, graph, N, w, timestamps, time_index, start_time);
      do_emplace<2>(futs, graph, N, w, timestamps, time_index, start_time);
      do_emplace<1>(futs, graph, N, w, timestamps, time_index, start_time);
      do_emplace<0>(futs, graph, N, w, timestamps, time_index, start_time);
    }
  }

  /*
   * Put the nodes at each stage of the graph together into the vector.
   */
  if (grouped == true && reverse_order == false) {
    for (size_t w = 0; w < width; ++w)
      do_emplace<0>(futs, graph, N, w, timestamps, time_index, start_time);
    for (size_t w = 0; w < width; ++w)
      do_emplace<1>(futs, graph, N, w, timestamps, time_index, start_time);
    for (size_t w = 0; w < width; ++w)
      do_emplace<2>(futs, graph, N, w, timestamps, time_index, start_time);
    for (size_t w = 0; w < width; ++w)
      do_emplace<3>(futs, graph, N, w, timestamps, time_index, start_time);
    for (size_t w = 0; w < width; ++w)
      do_emplace<4>(futs, graph, N, w, timestamps, time_index, start_time);
  }

  /*
   * Put the nodes at each stage of the graph together into the vector, in
   * reverse order.
   */
  if (grouped == true && reverse_order == true) {
    for (size_t w = 0; w < width; ++w)
      do_emplace<4>(futs, graph, N, w, timestamps, time_index, start_time);
    for (size_t w = 0; w < width; ++w)
      do_emplace<3>(futs, graph, N, w, timestamps, time_index, start_time);
    for (size_t w = 0; w < width; ++w)
      do_emplace<2>(futs, graph, N, w, timestamps, time_index, start_time);
    for (size_t w = 0; w < width; ++w)
      do_emplace<1>(futs, graph, N, w, timestamps, time_index, start_time);
    for (size_t w = 0; w < width; ++w)
      do_emplace<0>(futs, graph, N, w, timestamps, time_index, start_time);
  }

#if 0
  // If using threads
  for (size_t i = 0; i < threads.size(); ++i) {
    if (debug)
      std::cout << "joining " << i << std::endl;

    threads[i].join();
  }
#else
  // If using thread pool
  // tp.wait_all(futs);

  // If using tasks
  for (auto&& f : futs) {
    f.wait();
  }
#endif

  if (debug)
    std::cout << "threads size: " << futs.size() << std::endl;

  /*
   * Output tracing information from the runs.
   */
  if (chart) {
    std::for_each(
        timestamps.begin(), timestamps.begin() + time_index, [](auto&& a) {
          auto&& [idx, id, str, tm] = a;

          size_t col = 0;
          if (id < 5) {
            col = 2 * id;
          } else {
            col = 2 * (id % 5) + 1;
          }
          col = id;
          std::cout << idx << "\t" << id << "\t" << tm << "\t";
          for (size_t k = 0; k < col; ++k) {
            std::cout << "\t";
          }
          std::cout << str << std::endl;
        });
  }
  if (debug)
    std::cout << "Post sieve" << std::endl;

  return prime_list;
}

/*
 * Program for running different sieve function configurations.
 */
template <class F>
auto timer_2(
    F f,
    size_t max,
    size_t blocksize,
    size_t width,
    bool reverse_order,
    bool grouped,
    bool use_futures,
    bool use_threadpool) {
  auto start = std::chrono::high_resolution_clock::now();
  auto s =
      f(max,
        blocksize,
        width,
        reverse_order,
        grouped,
        use_futures,
        use_threadpool);
  auto stop = std::chrono::high_resolution_clock::now();

  size_t num = 0;
  for (auto& j : s) {
    if (j) {
      num += j->size();
    }
  }
  std::cout << num << ": " << std::endl;

  return stop - start;
}

int main(int argc, char* argv[]) {
  size_t number = 100'000'000;
  size_t block_size = 100;

  if (argc >= 2) {
    number = std::stol(argv[1]);
  }
  if (argc >= 3) {
    block_size = std::stol(argv[2]);
  }

#if 0
#ifdef __clang__
  std::cout << "Found __clang__" << std::endl;
#endif
#ifdef __GNUG__
  std::cout << "Found __GNUG__" << std::endl;
#endif

#ifdef _GLIBCXX_USE_TBB_PAR_BACKEND
#if _GLIBCXX_USE_TBB_PAR_BACKEND
  std::cout << "Found _GLIBCXX_USE_TBB_PAR_BACKEND is true" << std::endl;
#else
  std::cout << "Found _GLIBCXX_USE_TBB_PAR_BACKEND is false" << std::endl;
#endif
#else
  std::cout << "Did not find _GLIBCXX_USE_TBB_PAR_BACKEND" << std::endl;
#endif

  tbb::global_control(tbb::global_control::max_allowed_parallelism, 2);
#endif
  size_t width = 4;

  /*
   * Test with two_stage connections
   */
  for (auto reverse_order : {false, true}) {
    for (auto grouped : {false, true}) {
      for (size_t i = 0; i < 3; ++i) {
        auto using_char_async_block = timer_2(
            sieve_async_block<AsyncMover2, char>,
            number,
            block_size * 1024,
            width,
            reverse_order,
            grouped, /* grouped */
            true,    /* use_futures */
            false);  /* use_threadpool */

        std::cout << "Time using char async block, two stage, " +
                         std::string(
                             reverse_order ? "reverse order" :
                                             "forward order") +
                         "  " + std::string(grouped ? "grouped" : "ungrouped")
                  << " : "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         using_char_async_block)
                         .count()
                  << "\n";
      }

      /*
       * Test with three_stage connections
       */
      for (size_t i = 0; i < 3; ++i) {
        if (i == 0) {
          debug = false;
        } else {
          debug = false;
        }

        auto using_char_async_block = timer_2(
            sieve_async_block<AsyncMover3, char>,
            number,
            block_size * 1024,
            width,
            reverse_order,
            grouped, /* grouped */
            true,    /* use_futures */
            false);  /* use_threadpool */
        std::cout << "Time using char async block, three stage, " +
                         std::string(
                             reverse_order ? "reverse order" :
                                             "forward order") +
                         "  " + std::string(grouped ? "grouped" : "ungrouped")
                  << " : "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         using_char_async_block)
                         .count()
                  << "\n";
      }
    }
  }

  return 0;
}

#endif