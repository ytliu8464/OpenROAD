// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, UCSD
// Full Netlist Graph Extractor — dumps all cells and pin-to-pin
// connectivity from ODB for external sequential-graph weight computation.

#pragma once

#include <string>

namespace odb {
class dbBlock;
}

namespace utl {
class Logger;
}

namespace cts {

class FullGraphExtractor
{
 public:
  FullGraphExtractor(odb::dbBlock* block, utl::Logger* logger);

  // Write <output_base>.nodes.csv and <output_base>.edges.csv
  void extractFullGraph(const std::string& output_base);

 private:
  odb::dbBlock* block_;
  utl::Logger* logger_;

  void writeNodes(const std::string& path);
  void writeEdges(const std::string& path);
};

}  // namespace cts
