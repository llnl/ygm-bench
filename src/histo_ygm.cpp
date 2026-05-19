// Copyright 2019-2021 Lawrence Livermore National Security, LLC and other YGM
// Project Developers. See the top-level COPYRIGHT file for details.
//
// SPDX-License-Identifier: MIT

#include <random>
#include <rmat_edge_generator.hpp>
#include <utility.hpp>
#include <ygm/comm.hpp>
#include <ygm/container/array.hpp>
#include <ygm/container/detail/base_async_reduce.hpp>
#include <ygm/container/detail/reducing_adapter.hpp>
#include <ygm/container/map.hpp>
#include <ygm/detail/ygm_cereal_archive.hpp>
#include <ygm/utility/timer.hpp>

#include <boost/json/src.hpp>

struct parameters_t {
  enum class distribution { uniform, rmat };

  int          log_table_size;
  int64_t      local_updates;
  int          num_trials;
  distribution dist;
  bool         use_reducing_adapter;
  bool         pretty_print;

  parameters_t()
      : log_table_size(15),
        local_updates(1024 * 1024),
        num_trials(5),
        dist(distribution::uniform),
        use_reducing_adapter(false),
        pretty_print(false) {}
};

void usage(ygm::comm &comm) {
  comm.cerr0()
      << "histo_ygm usage:"
      << "\n\t-s <int>\t- Log_2 of global table size"
      << "\n\t-i <int>\t- Number of insertions per rank"
      << "\n\t-t <int>\t- Number of trials"
      << "\n\t-r\t\t- Flag indicating insertions should use RMAT generator"
      << "\n\t-a\t\t- Flag indicating use of reducing_adapter"
      << "\n\t-p\t\t- Pretty print output"
      << "\n\t-h\t\t- Print help" << std::endl;
}

parameters_t parse_cmd_line(int argc, char **argv, ygm::comm &comm) {
  parameters_t params;
  int          c;
  bool         prn_help = false;

  // Suppress error messages from getopt
  extern int opterr;
  opterr = 0;

  while ((c = getopt(argc, argv, "s:i:t:raph")) != -1) {
    switch (c) {
      case 'h':
        prn_help = true;
        break;
      case 's':
        params.log_table_size = atoi(optarg);
        break;
      case 'i':
        params.local_updates = atoll(optarg);
        break;
      case 't':
        params.num_trials = atoi(optarg);
        break;
      case 'r':
        params.dist = parameters_t::distribution::rmat;
        break;
      case 'a':
        params.use_reducing_adapter = true;
        break;
      case 'p':
        params.pretty_print = true;
        break;
      default:
        comm.cerr0() << "Unrecognized option: " << char(optopt) << std::endl;
        prn_help = true;
        break;
    }
  }

  if (prn_help) {
    usage(comm);
    exit(-1);
  }

  return params;
}

std::vector<uint64_t> generate_indices(ygm::comm     &world,
                                       const uint64_t local_updates,
                                       const int      log_table_size,
                                       const int      trial,
                                       const parameters_t::distribution dist) {
  std::vector<uint64_t> indices;
  indices.reserve(local_updates);
  uint64_t global_table_size = ((uint64_t)1) << log_table_size;

  if (dist == parameters_t::distribution::uniform) {
    std::mt19937 gen(world.size() * trial + world.rank());
    std::uniform_int_distribution<uint64_t> dist(0, global_table_size - 1);
    for (int64_t i = 0; i < local_updates; ++i) {
      indices.push_back(dist(gen));
    }
  } else if (dist == parameters_t::distribution::rmat) {
    distributed_rmat_edge_generator rmat(
        world, log_table_size, local_updates * world.size() / 2, trial);

    rmat.for_all([&indices](const auto first, const auto second) {
      indices.push_back(first);
      indices.push_back(second);
    });
  }

  world.barrier();

  return indices;
}

template <typename Container>
void run_reductions(ygm::comm &world, const std::vector<uint64_t> &indices,
                    Container &cont) {
  for (const auto &index : indices) {
    if constexpr (ygm::container::detail::HasAsyncReduceWithoutReductionOp<
                      Container>) {
      cont.async_reduce(index, 1);
    } else {  // For reducing_adapter
      cont.async_visit(index, [](const auto i, auto &v) { ++v; });
    }
  }

  world.barrier();
}

template <typename Container>
void check_counts(ygm::comm &world, Container &cont, int64_t local_count) {
  int64_t local_insertions{0};

  cont.for_all([&local_insertions](const auto &index, const auto &count) {
    local_insertions += count;
  });

  // YGM_ASSERT_RELEASE(ygm::sum(local_insertions, world) ==
  // ygm::sum(local_count, world));
}

std::tuple<long long, long, long> memory_usage(ygm::comm &world) {
  std::ifstream status("/proc/self/status");
  std::string   line;
  int32_t       memoryUsage = 0;

  while (std::getline(status, line)) {
    if (line.find("VmRSS") == 0) {
      size_t pos = line.find(":");
      if (pos != std::string::npos) {
        memoryUsage = std::stol(line.substr(pos + 1));
      }
    }
  }

  return std::make_tuple(ygm::sum((int64_t)memoryUsage, world),
                         ygm::min(memoryUsage, world),
                         ygm::max(memoryUsage, world));
}

int main(int argc, char **argv) {
  {
    ygm::comm world(&argc, &argv);

    parameters_t params = parse_cmd_line(argc, argv, world);

    uint64_t global_table_size = ((uint64_t)1) << params.log_table_size;
    ygm::container::array<uint64_t> arr(world, global_table_size);
    // ygm::container::map<uint64_t, size_t> arr(world, global_table_size);

    boost::json::object output;

    output["NAME"]                         = "HISTO_YGM";
    output["TIME"]                         = boost::json::array();
    output["INSERTS_PER_SECOND(BILLIONS)"] = boost::json::array();
    output["GLOBAL_ASYNC_COUNT"]           = boost::json::array();
    output["GLOBAL_ISEND_COUNT"]           = boost::json::array();
    output["GLOBAL_ISEND_BYTES"]           = boost::json::array();
    output["MAX_WAITSOME_ISEND_IRECV"]     = boost::json::array();
    output["MAX_WAITSOME_IALLREDUCE"]      = boost::json::array();
    output["COUNT_IALLREDUCE"]             = boost::json::array();
    output["TABLE_SIZE"]                   = global_table_size;
    output["INSERTIONS"]       = params.local_updates * world.size();
    output["REDUCING_ADAPTER"] = params.use_reducing_adapter;
    if (params.dist == parameters_t::distribution::uniform) {
      output["GENERATOR"] = "UNIFORM";
    } else if (params.dist == parameters_t::distribution::rmat) {
      output["GENERATOR"] = "RMAT";
    } else {
      output["GENERATOR"] = "UNKNOWN";
    }

    parse_welcome(world, output);

    for (int trial = 0; trial < params.num_trials; ++trial) {
      world.stats_reset();
      arr.clear();
      arr.resize(global_table_size);

      std::vector<uint64_t> indices =
          generate_indices(world, params.local_updates, params.log_table_size,
                           trial, params.dist);

      double trial_time;
      double trial_rate;
      world.barrier();
      if (params.use_reducing_adapter) {
        /*
        auto reducing_arr = ygm::container::detail::make_reducing_adapter(
            arr, [](const uint64_t &a, const uint64_t &b) { return a + b; });

        world.barrier();

        ygm::utility::timer update_timer{};

        run_reductions(world, indices, reducing_arr);

        trial_time = update_timer.elapsed();
        trial_rate = params.local_updates * world.size() / trial_time /
                     (1000 * 1000 * 1000);
                     */
      } else {
        world.barrier();
        ygm::utility::timer update_timer{};

        run_reductions(world, indices, arr);

        trial_time = update_timer.elapsed();
        trial_rate = params.local_updates * world.size() / trial_time /
                     (1000 * 1000 * 1000);
      }

      check_counts(world, arr, params.local_updates);

      output["TIME"].as_array().emplace_back(trial_time);
      output["INSERTS_PER_SECOND(BILLIONS)"].as_array().emplace_back(
          trial_rate);

      parse_stats(world, output);
    }

    if (params.pretty_print) {
      pretty_print(world.cout0(), output);
      world.cout0() << "\n";
    } else {
      world.cout0(output);
    }
  }

  return 0;
}
