// Copyright 2019-2021 Lawrence Livermore National Security, LLC and other YGM
// Project Developers. See the top-level COPYRIGHT file for details.
//
// SPDX-License-Identifier: MIT

#include <random>
#include <utility.hpp>
#include <ygm/comm.hpp>
#include <ygm/container/array.hpp>
#include <ygm/detail/ygm_cereal_archive.hpp>
#include <ygm/utility/timer.hpp>

struct parameters_t {
  int     log_table_size;
  int64_t local_updaters;
  int     updater_lifetime;
  int     num_trials;
  bool    pretty_print;

  parameters_t()
      : log_table_size(15),
        local_updaters(1024 * 16),
        updater_lifetime(100),
        num_trials(5),
        pretty_print(false) {}
};

void usage(ygm::comm &comm) {
  comm.cerr0() << "agups_ygm usage:"
               << "\n\t-s <int>\t- Log_2 of global table size"
               << "\n\t-u <int>\t- Number of updaters spawned per rank"
               << "\n\t-l <int>\t- Updater lifetime"
               << "\n\t-t <int>\t- Number of trials"
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

  while ((c = getopt(argc, argv, "s:u:l:t:ph")) != -1) {
    switch (c) {
      case 'h':
        prn_help = true;
        break;
      case 's':
        params.log_table_size = atoi(optarg);
        break;
      case 'u':
        params.local_updaters = atoll(optarg);
        break;
      case 'l':
        params.updater_lifetime = atoi(optarg);
        break;
      case 't':
        params.num_trials = atoi(optarg);
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

static parameters_t params;
static size_t       global_table_size;

class updater {
 public:
  // Needs default constructor to send through YGM
  updater() {}

  updater(size_t seed) : m_state(seed), m_counter(0) {}

  void increment_counter() const { ++m_counter; }

  bool is_alive() const { return get_counter() < params.updater_lifetime; }

  uint32_t get_counter() const { return m_counter; }

  size_t get_state() const { return m_state; }

  template <typename Archive>
  void serialize(Archive &ar) {
    ar(m_state, m_counter);
  }

  void update_state(size_t value) const { m_state ^= value; }

 private:
  mutable size_t   m_state;
  mutable uint32_t m_counter;
};

struct recursive_functor {
 public:
  // Creates a copy of u because YGM passes arguments by const reference
  void operator()(ygm::ygm_ptr<ygm::container::array<size_t>> parray,
                  const size_t index, size_t &value, updater u) const {
    u.update_state(value);
    value = u.get_state();
    u.increment_counter();

    if (u.is_alive()) {
      size_t new_index = value & (global_table_size - 1);
      parray->async_visit(new_index, recursive_functor(), u);
    }
  }
};

int main(int argc, char **argv) {
  {
    ygm::comm world(&argc, &argv);

    params = parse_cmd_line(argc, argv, world);

    global_table_size = ((size_t)1) << params.log_table_size;
    ygm::container::array<size_t> arr(world, global_table_size);

    boost::json::object output;

    output["NAME"]                     = "AGUPS_YGM";
    output["TIME"]                     = boost::json::array();
    output["GUPS"]                     = boost::json::array();
    output["GLOBAL_ASYNC_COUNT"]       = boost::json::array();
    output["GLOBAL_ISEND_COUNT"]       = boost::json::array();
    output["GLOBAL_ISEND_BYTES"]       = boost::json::array();
    output["MAX_WAITSOME_ISEND_IRECV"] = boost::json::array();
    output["MAX_WAITSOME_IALLREDUCE"]  = boost::json::array();
    output["COUNT_IALLREDUCE"]         = boost::json::array();
    output["TABLE_SIZE"]               = global_table_size;
    output["UPDATERS"]                 = params.local_updaters * world.size();
    output["UPDATER_LIFESPAN"]         = params.updater_lifetime;

    parse_welcome(world, output);

    std::mt19937                          gen(world.rank());
    std::uniform_int_distribution<size_t> dist;

    arr.for_all(
        [&dist, &gen](const auto index, auto &value) { value = dist(gen); });

    world.cf_barrier();

    for (int trial = 0; trial < params.num_trials; ++trial) {
      world.stats_reset();

      std::vector<updater> updater_vec;

      for (int64_t i = 0; i < params.local_updaters; ++i) {
        updater_vec.emplace_back(updater(dist(gen)));
      }

      world.barrier();

      ygm::utility::timer update_timer{};

      for (auto &u : updater_vec) {
        size_t first_index = u.get_state() & (arr.size() - 1);
        arr.async_visit(first_index, recursive_functor(), u);
      }

      world.barrier();

      double trial_time = update_timer.elapsed();
      double trial_gups = params.local_updaters * world.size() *
                          params.updater_lifetime / trial_time /
                          (1000 * 1000 * 1000);

      output["TIME"].as_array().emplace_back(trial_time);
      output["GUPS"].as_array().emplace_back(trial_gups);

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
