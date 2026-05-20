// Copyright 2019-2026 Lawrence Livermore National Security, LLC and other YGM
// Project Developers. See the top-level COPYRIGHT file for details.
//
// SPDX-License-Identifier: MIT

#include <utility.hpp>
#include <ygm/comm.hpp>
#include <ygm/utility/timer.hpp>

#include <boost/json/src.hpp>

struct parameters_t {
  int  num_barriers;
  int  asyncs_per_barrier;
  int  num_trials;
  bool mpi_barrier;
  bool pretty_print;

  parameters_t()
      : num_barriers(100000),
        asyncs_per_barrier(0),
        num_trials(5),
        mpi_barrier(false),
        pretty_print(false) {}
};

void usage(ygm::comm &comm) {
  comm.cerr0() << "barrier_ygm usage:"
               << "\n\t-b <int>\t- Number of barriers per trial"
               << "\n\t-a <int>\t- Number of asyncs per barrier"
               << "\n\t-t <int>\t- Number of trials"
               << "\n\t-m\t\t- Test MPI barrier instead of YGM barrier"
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

  while ((c = getopt(argc, argv, "b:a:t:mph")) != -1) {
    switch (c) {
      case 'h':
        prn_help = true;
        break;
      case 'b':
        params.num_barriers = atoi(optarg);
        break;
      case 'a':
        params.asyncs_per_barrier = atoi(optarg);
        break;
      case 't':
        params.num_trials = atoi(optarg);
        break;
      case 'm':
        params.mpi_barrier = true;
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

  if (params.mpi_barrier && (params.asyncs_per_barrier > 0)) {
    comm.cerr0() << "Unable to perform asyncs while testing MPI barrier."
                 << std::endl;
    exit(-1);
  }

  if (prn_help) {
    usage(comm);
    exit(-1);
  }

  return params;
}

std::vector<int> generate_async_dests(ygm::comm &comm, const int num_asyncs) {
  std::vector<int> dests;

  int node_id         = comm.layout().node_id();
  int num_nodes       = comm.layout().node_size();
  int local_id        = comm.layout().local_id();
  int num_local_ranks = comm.layout().local_size();
  for (int i = 1; i <= num_asyncs; ++i) {
    int dest_node     = (node_id + i) % num_nodes;
    int dest_local_id = (local_id + (i / num_nodes)) % num_local_ranks;

    int dest_rank = comm.layout().nl_to_rank(dest_node, dest_local_id);

    dests.push_back(dest_rank);
  }

  return dests;
}

void run_mpi_barriers(ygm::comm &comm, const int num_barriers) {
  for (int b = 0; b < num_barriers; ++b) {
    comm.cf_barrier();
  }
}

void run_ygm_barriers(ygm::comm &comm, const int num_barriers,
                      const std::vector<int> dests) {
  for (int b = 0; b < num_barriers; ++b) {
    for (const int dest : dests) {
      comm.async(dest, [] {});
    }
    comm.barrier();
  }
}

int main(int argc, char **argv) {
  ygm::comm world(&argc, &argv);

  parameters_t params = parse_cmd_line(argc, argv, world);

  boost::json::object output;

  output["NAME"]                = "BARRIER_YGM";
  output["TIME"]                = boost::json::array();
  output["BARRIERS_PER_SECOND"] = boost::json::array();
  output["NUM_BARRIERS"]        = params.num_barriers;
  output["ASYNCS_PER_BARRIER"]  = params.asyncs_per_barrier;
  if (params.mpi_barrier) {
    output["BARRIER_TYPE"] = "MPI";
  } else {
    output["BARRIER_TYPE"] = "YGM";
  }

  parse_welcome(world, output);

  std::vector<int> async_dests =
      generate_async_dests(world, params.asyncs_per_barrier);

  for (int trial = 0; trial < params.num_trials; ++trial) {
    world.stats_reset();

    double trial_time;
    double trial_rate;
    world.cf_barrier();
    ygm::utility::timer barriers_timer{};

    if (params.mpi_barrier) {
      run_mpi_barriers(world, params.num_barriers);
    } else {
      run_ygm_barriers(world, params.num_barriers, async_dests);
    }

    trial_time = barriers_timer.elapsed();
    trial_rate = params.num_barriers / trial_time;

    output["TIME"].as_array().emplace_back(trial_time);
    output["BARRIERS_PER_SECOND"].as_array().emplace_back(trial_rate);

    parse_stats(world, output);
  }

  if (params.pretty_print) {
    pretty_print(world.cout0(), output);
    world.cout0() << "\n";
  } else {
    world.cout0(output);
  }
}
