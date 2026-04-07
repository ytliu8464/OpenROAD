// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2019-2025, The OpenROAD Authors
//
// Verilog-based FF-to-FF Connectivity Extractor
// Parse verilog netlist to extract FF-to-FF edges with timing info from STA
//
// JYJ (2026-03-21) openroad_260321:
//   - Multi-module Verilog support (iterates all modules, not just first)
//   - SDFF* register recognition (scan flip-flops)
//   - IO edge extraction (PI->FF, FF->PO) from V51_IO
//   - Dual CSV output: *.reg2reg.csv + *.all.csv (reg2reg + IO)

#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace odb {
class dbBlock;
}

namespace sta {
class dbSta;
class dbNetwork;
}

namespace utl {
class Logger;
}

namespace cts {

// JYJ (2026-03-17) V51_IO: IO edge (PI->FF or FF->PO) with worst setup/hold slack.
struct IOEdgeVerilog {
  std::string edge_type;   // "PI_TO_FF" or "FF_TO_PO"
  std::string port_name;   // Primary input or output port name
  std::string ff_name;     // FF instance name
  float slack_setup_ns = 0.0f;
  float slack_hold_ns = 0.0f;
  bool has_setup = false;
  bool has_hold = false;
};

struct FFEdgeVerilog {
  std::string from_ff;
  std::string to_ff;
  std::string via_net;

  // Locations (filled from ODB)
  int from_x = 0;
  int from_y = 0;
  int from_tier = 0;
  int to_x = 0;
  int to_y = 0;
  int to_tier = 0;

  // Setup timing (max path)
  float slack_max = 0.0f;
  float arrival_max = 0.0f;
  float required_max = 0.0f;
  // Hold timing (min path)
  float slack_min = 0.0f;
  float arrival_min = 0.0f;
  float required_min = 0.0f;
  bool has_timing = false;
};

struct RegisterInfo {
  std::string cell_type;
  std::unordered_map<std::string, std::string> pins;  // pin_name -> net_name
};

class VerilogFFExtractor {
public:
  VerilogFFExtractor(const std::string& verilog_path,
                     odb::dbBlock* block,
                     sta::dbSta* sta,
                     sta::dbNetwork* network,
                     utl::Logger* logger);

  // Parse verilog file to extract instances and connections
  // JYJ (2026-03-21): Now iterates ALL modules (multi-module support)
  // WARNING: multi-module parsing has net-name scope issues (module-local nets
  // collide in global map). Use parseODB() for hierarchical Innovus netlists.
  void parseVerilog();

  // JYJ (2026-03-21): ODB-based extraction — uses flat ODB netlist directly.
  // Correct for ANY netlist (Innovus multi-module, OpenROAD flat, etc.)
  // because ODB always stores a flattened view of all instances.
  // Call this INSTEAD of parseVerilog() for hierarchical designs.
  void parseODB();

  // Extract FF-to-FF edges based on connectivity (works after parseVerilog or parseODB)
  std::vector<FFEdgeVerilog> extractFFEdges();

  // Fill timing info for edges using STA (per-from_ff findPathEnds — slow but per-edge exact)
  void fillTimingInfo(std::vector<FFEdgeVerilog>& edges);

  // JYJ (2026-03-22): Fast timing fill using per-vertex slack/arrival/required lookup.
  // After updateTiming(), each capture FF D-pin vertex has worst slack already computed.
  // Assigns capture FF's vertex timing to ALL edges targeting that FF.
  // Not per-edge exact (all edges to same FF get same worst timing), but ~100x faster.
  // Use FAST_TIMING=1 env var to select this mode.
  void fillTimingInfoFast(std::vector<FFEdgeVerilog>& edges);

  // Write FF-to-FF edges to CSV (reg2reg only)
  void writeCSV(const std::vector<FFEdgeVerilog>& edges,
                const std::string& output_file);

  // JYJ (2026-03-17) V51_IO: Exhaustive IO edge extraction via STA per-port.
  std::vector<IOEdgeVerilog> extractIOEdges();

  // Write IO edges to CSV
  void writeIOCSV(const std::vector<IOEdgeVerilog>& edges,
                  const std::string& output_file);

private:
  std::string verilog_path_;
  odb::dbBlock* block_;
  sta::dbSta* sta_;
  sta::dbNetwork* network_;
  utl::Logger* logger_;

  // Parsed data
  std::unordered_map<std::string, RegisterInfo> registers_;
  std::unordered_map<std::string, RegisterInfo> all_instances_;
  std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>>
      net_connections_;  // net_name -> [(inst, pin), ...]

  // JYJ (2026-03-21): Check if cell type is a register (DFF*, SDFF*, DFFH*)
  bool isRegisterCell(const std::string& cell_type) const;

  // JYJ (2026-03-21): ODB mode flag — when true, FF names are already ODB-compatible
  // (no leading backslash, no escaping needed). Set by parseODB().
  bool odb_mode_ = false;

  // JYJ (2026-03-21): ODB mode — combo gate output nets (from dbITerm::getIoType)
  // Populated by parseODB(). Empty in Verilog mode (falls back to pin-name heuristic).
  std::unordered_map<std::string, std::vector<std::string>> combo_out_nets_;

  // Helper: Get location from ODB
  void fillLocations(FFEdgeVerilog& edge);

  // JYJ (2026-03-21): Parse a single module body and accumulate results
  void parseModuleBody(const std::string& body, const std::string& module_name);
};

}  // namespace cts
