// Copyright 2019-2021 Lawrence Livermore National Security, LLC and other YGM
// Project Developers. See the top-level COPYRIGHT file for details.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <fstream>
#include <string>

#include <boost/json/src.hpp>

#include <ygm/comm.hpp>
#include <ygm/io/detail/csv.hpp>

void parse_welcome(ygm::comm &c, boost::json::object &o) {
  std::stringstream ss;

  c.welcome(ss);

  std::string line;
  while (std::getline(ss, line)) {
    if (line.find("YGM_") != std::string::npos ||
        line.find("MPI_") != std::string::npos) {
      auto space_pos = line.find(" ");
      auto equal_pos = line.find("=");

      std::string label = line.substr(0, space_pos);
      std::string value = line.substr(equal_pos + 2);

      // Please forgive me...
      ygm::io::detail::csv_field field(value);

      if (field.is_integer()) {
        o[label] = field.as_integer();
      } else if (field.is_double()) {
        o[label] = field.as_double();
      } else {
        o[label] = field.as_string();
      }
    }
  }

  // Getting these manually rather than parsing welcome()
  o["COMM_SIZE"]      = c.layout().size();
  o["RANKS_PER_NODE"] = c.layout().local_size();
  o["NUM_NODES"]      = c.layout().node_size();
}

void parse_stats(ygm::comm &c, boost::json::object &o) {
  std::stringstream ss;

  c.stats_print("", ss);

  std::string line;
  while (std::getline(ss, line)) {
    auto space_pos = line.find(" ");
    auto equal_pos = line.find("=");

    if (equal_pos != 0) {
      std::string label = line.substr(0, space_pos);
      std::string value = line.substr(equal_pos + 2);

      if (label != "NAME" && label != "TIME") {
        ygm::io::detail::csv_field field(value);

        if (not o.contains(label)) {
          o[label] = boost::json::array();
        }

        if (field.is_integer()) {
          o[label].as_array().emplace_back(field.as_integer());
        } else if (field.is_double()) {
          o[label].as_array().emplace_back(field.as_double());
        } else {
          o[label].as_array().emplace_back(field.as_string());
        }
      }
    }
  }
}

void print_indents(std::ostream &os, std::string indent, int indent_count) {
  for (int i = 0; i < indent_count; ++i) {
    os << indent;
  }
}
// Based on pretty_print example at
// https://www.boost.org/doc/libs/1_75_0/libs/json/doc/html/json/examples.html#json.examples.pretty
void pretty_print(std::ostream &os, const boost::json::value &jv,
                  std::string indent = "    ", int indent_count = 0) {
  switch (jv.kind()) {
    case boost::json::kind::object: {
      indent_count += 1;
      os << "{\n";
      const auto &obj = jv.get_object();
      if (!obj.empty()) {
        for (auto it = obj.begin(); it != obj.end(); ++it) {
          print_indents(os, indent, indent_count);
          os << boost::json::serialize(it->key()) << " : ";
          pretty_print(os, it->value(), indent, indent_count);
          os << "\n";
        }
      }
      indent_count -= 1;
      print_indents(os, indent, indent_count);
      os << "}";
      break;
    }

    case boost::json::kind::array: {
      indent_count += 1;
      os << "[\n";
      const auto &arr = jv.get_array();
      if (!arr.empty()) {
        auto it = arr.begin();
        for (auto it = arr.begin(); it != arr.end(); ++it) {
          print_indents(os, indent, indent_count);
          pretty_print(os, *it, indent, indent_count);
          os << "\n";
        }
      }
      indent_count -= 1;
      print_indents(os, indent, indent_count);
      os << "]";
      break;
    }

    default: {
      os << jv;
      break;
    }
  }
}

template <typename T>
std::vector<std::vector<T>> gather_vectors_rank_0(
    ygm::comm &world, const std::vector<T> &local_vec) {
  std::vector<std::vector<T>> to_return;

  if (world.rank0()) {
    to_return.resize(world.size());
  }

  auto to_return_ptr = world.make_ygm_ptr(to_return);

  world.barrier();

  auto gather_lambda = [](const std::vector<T> &local_vec, const int rank,
                          auto global_vec_ptr) {
    (*global_vec_ptr)[rank] = local_vec;
  };

  world.async(0, gather_lambda, local_vec, world.rank(), to_return_ptr);

  world.barrier();

  return to_return;
}
