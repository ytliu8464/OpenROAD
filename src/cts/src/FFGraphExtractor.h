// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, UCSD
// FF-to-FF Sequential Timing Graph Extractor for 3D-CTS

#pragma once

#include <string>
#include <vector>
#include <map>

namespace odb {
class dbBlock;
class dbInst;
class dbNet;
}

namespace sta {
class dbSta;
class dbNetwork;
}

namespace utl {
class Logger;
}

namespace cts {

// Structure to hold FF-to-FF edge information
struct FFEdge {
  std::string from_ff;
  std::string to_ff;
  double slack;      // in seconds
  double arrival;    // in seconds
  double required;   // in seconds
  int from_x, from_y; // location of source FF
  int to_x, to_y;     // location of sink FF
};

class FFGraphExtractor
{
 public:
  FFGraphExtractor(odb::dbBlock* block,
                   sta::dbSta* sta,
                   sta::dbNetwork* network,
                   utl::Logger* logger);

  // Extract FF-to-FF timing paths and write to CSV file
  void extractGraph(const std::string& output_file);

  // Get extracted edges (for use in partitioning algorithm)
  const std::vector<FFEdge>& getEdges() const { return edges_; }

  // Get all sequential instances
  std::vector<odb::dbInst*> getSequentialInsts();

 private:
  odb::dbBlock* block_;
  sta::dbSta* sta_;
  sta::dbNetwork* network_;
  utl::Logger* logger_;

  std::vector<FFEdge> edges_;

  // Helper: check if instance is sequential (FF)
  bool isSequential(odb::dbInst* inst);

  // Helper: get instance location
  void getInstLocation(odb::dbInst* inst, int& x, int& y);
};

}  // namespace cts
