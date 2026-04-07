// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, UCSD
// Buffer Sizing LP for Useful Skew Optimization in 3D-CTS
// Based on Uysal BLU LP framework concept

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "FFGraphExtractor.h"

namespace odb {
class dbBlock;
class dbInst;
}  // namespace odb

namespace sta {
class dbSta;
class dbNetwork;
}  // namespace sta

namespace utl {
class Logger;
}  // namespace utl

namespace cts {

// Result structure for LP optimization
struct LPResult {
  bool feasible;
  double objective_value;
  double worst_slack_before;
  double worst_slack_after;
  double tns_before;  // Total Negative Slack before
  double tns_after;   // Total Negative Slack after
  int num_violations_before;
  int num_violations_after;
  std::map<std::string, double> ff_arrival_deltas;  // FF name -> arrival time delta
};

// Optimization objective type
enum class LPObjective {
  MAXIMIZE_WORST_SLACK,    // Maximize minimum slack (default)
  MINIMIZE_TNS,            // Minimize Total Negative Slack
  MAXIMIZE_TOTAL_SLACK     // Maximize sum of all slacks
};

class BufferSizingLP {
 public:
  BufferSizingLP(odb::dbBlock* block,
                 sta::dbSta* sta,
                 sta::dbNetwork* network,
                 utl::Logger* logger);

  // Set clock period (in seconds)
  void setClockPeriod(double period) { clockPeriod_ = period; }
  double getClockPeriod() const { return clockPeriod_; }

  // Set optimization objective
  void setObjective(LPObjective obj) { objective_ = obj; }
  LPObjective getObjective() const { return objective_; }

  // Set maximum allowed arrival time delta (in seconds)
  // This bounds how much we can adjust clock arrival times
  void setMaxDelta(double delta) { maxDelta_ = delta; }
  double getMaxDelta() const { return maxDelta_; }

  // Set slack margin (safety margin for constraints)
  void setSlackMargin(double margin) { slackMargin_ = margin; }
  double getSlackMargin() const { return slackMargin_; }

  // Enable/disable hold constraint consideration
  void setConsiderHold(bool consider) { considerHold_ = consider; }
  bool getConsiderHold() const { return considerHold_; }

  // Run LP optimization using FF-to-FF timing graph
  // Returns optimization result
  LPResult optimize(const std::vector<FFEdge>& edges);

  // Run LP optimization from CSV file
  LPResult optimizeFromFile(const std::string& csv_file);

  // Apply arrival time deltas to clock tree (adjust buffer delays)
  // This is a placeholder for future implementation
  void applyDelayAdjustments(const LPResult& result);

  // Report LP results
  void reportResults(const LPResult& result);

  // Get unique FFs from edges
  std::vector<std::string> getUniqueFFs(const std::vector<FFEdge>& edges);

 private:
  odb::dbBlock* block_;
  sta::dbSta* sta_;
  sta::dbNetwork* network_;
  utl::Logger* logger_;

  double clockPeriod_ = 0.0;        // Clock period in seconds
  double maxDelta_ = 0.0;           // Max arrival time delta (0 = auto)
  double slackMargin_ = 0.0;        // Safety margin for constraints
  bool considerHold_ = true;        // Consider hold constraints
  LPObjective objective_ = LPObjective::MAXIMIZE_WORST_SLACK;

  // Build LP constraints from edges
  // Setup constraint: arrival[to] - arrival[from] <= T - D_ij + slack_setup
  // Hold constraint: arrival[to] - arrival[from] >= D_ij - slack_hold
  void buildConstraints(
      const std::vector<FFEdge>& edges,
      const std::map<std::string, int>& ff_to_idx,
      std::vector<std::vector<double>>& A_setup,
      std::vector<double>& b_setup,
      std::vector<std::vector<double>>& A_hold,
      std::vector<double>& b_hold);

  // Calculate statistics before/after optimization
  void calculateStats(const std::vector<FFEdge>& edges,
                      const std::map<std::string, double>& deltas,
                      double& worst_slack,
                      double& tns,
                      int& num_violations);

  // Parse CSV file to get edges
  std::vector<FFEdge> parseCSV(const std::string& csv_file);

  // Auto-detect clock period from edges
  double detectClockPeriod(const std::vector<FFEdge>& edges);
};

}  // namespace cts
