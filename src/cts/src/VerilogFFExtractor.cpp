// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2019-2025, The OpenROAD Authors

#include "VerilogFFExtractor.h"

#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

#include "odb/db.h"
#include "db_sta/dbNetwork.hh"
#include "db_sta/dbSta.hh"
#include "sta/ExceptionPath.hh"
#include "sta/Graph.hh"
#include "sta/MinMax.hh"
#include "sta/Network.hh"
#include "sta/Path.hh"
#include "sta/PathEnd.hh"
#include "sta/PathExpanded.hh"
#include "sta/Sta.hh"
#include "utl/Logger.h"

namespace cts {

VerilogFFExtractor::VerilogFFExtractor(const std::string& verilog_path,
                                       odb::dbBlock* block,
                                       sta::dbSta* sta,
                                       sta::dbNetwork* network,
                                       utl::Logger* logger)
    : verilog_path_(verilog_path), block_(block), sta_(sta), network_(network), logger_(logger)
{
}

bool VerilogFFExtractor::isRegisterCell(const std::string& cell_type) const
{
  // JYJ (2026-03-21): Added SDFF* (scan flip-flops) recognition.
  // Covers NanGate45: DFF_X1, DFFR_X1, SDFFR_X1, SDFFRS_X1, etc.
  // Covers ASAP7: DFFHQNx*, etc.
  return cell_type.rfind("DFF", 0) == 0
      || cell_type.rfind("DFFH", 0) == 0
      || cell_type.rfind("SDFF", 0) == 0;
}

// JYJ (2026-03-21): Parse a single module body — no std::regex (avoids stack overflow).
// Uses manual string parsing: split by ");", then parse cell_type, inst_name, .pin(net).
void VerilogFFExtractor::parseModuleBody(const std::string& body,
                                          const std::string& module_name)
{
  // Skip keywords that are NOT instance declarations
  static const std::unordered_set<std::string> skip_words = {
    "input", "output", "inout", "wire", "reg", "assign",
    "supply0", "supply1", "tri", "wand", "wor"
  };

  int module_inst_count = 0;
  int module_reg_count = 0;

  // Split by ");" to find instance boundaries
  size_t pos = 0;
  while (pos < body.size()) {
    size_t end = body.find(");", pos);
    if (end == std::string::npos) break;

    // Block = everything from pos to end (exclusive of ");")
    // Find first .PINNAME( pattern to confirm this is an instance declaration.
    // Format: .D(net), .RN(net), .CK(net), etc.
    size_t first_dot_paren = std::string::npos;
    {
      size_t dp = pos;
      while (dp < end) {
        dp = body.find('.', dp);
        if (dp == std::string::npos || dp >= end) break;
        // Check if this is .PINNAME( or .PINNAME (
        size_t pn_start = dp + 1;
        size_t pn_end = pn_start;
        while (pn_end < end && (std::isalnum(body[pn_end]) || body[pn_end] == '_')) pn_end++;
        if (pn_end > pn_start) {
          size_t sk = pn_end;
          while (sk < end && (body[sk] == ' ' || body[sk] == '\t' || body[sk] == '\n' || body[sk] == '\r')) sk++;
          if (sk < end && body[sk] == '(') {
            first_dot_paren = dp;
            break;
          }
        }
        dp++;
      }
    }

    if (first_dot_paren == std::string::npos) {
      pos = end + 2;
      continue;
    }

    // Extract header: everything before the first .PIN( pattern
    std::string header = body.substr(pos, first_dot_paren - pos);

    // Remove trailing '(' and whitespace from header
    size_t hend = header.find_last_not_of(" \t\n\r(");
    if (hend != std::string::npos) header = header.substr(0, hend + 1);

    // Find first non-whitespace token (cell_type)
    size_t h_start = header.find_first_not_of(" \t\n\r");
    if (h_start == std::string::npos) { pos = end + 2; continue; }

    size_t h_space = header.find_first_of(" \t\n\r", h_start);
    if (h_space == std::string::npos) { pos = end + 2; continue; }

    std::string cell_type = header.substr(h_start, h_space - h_start);

    // Skip declarations
    if (skip_words.count(cell_type)) { pos = end + 2; continue; }

    // Instance name: everything after cell_type, trimmed
    std::string inst_name = header.substr(h_space);
    inst_name.erase(0, inst_name.find_first_not_of(" \t\n\r"));
    inst_name.erase(inst_name.find_last_not_of(" \t\n\r") + 1);

    if (inst_name.empty()) { pos = end + 2; continue; }

    // Parse all .PIN(NET) connections in the block
    RegisterInfo reg_info;
    reg_info.cell_type = cell_type;

    size_t scan = first_dot_paren;
    while (scan < end) {
      // Find .PINNAME(
      size_t dot = body.find('.', scan);
      if (dot == std::string::npos || dot >= end) break;

      // Extract pin name
      size_t pn_s = dot + 1;
      size_t pn_e = pn_s;
      while (pn_e < end && (std::isalnum(body[pn_e]) || body[pn_e] == '_')) pn_e++;
      if (pn_e == pn_s) { scan = pn_e + 1; continue; }
      std::string pin_name = body.substr(pn_s, pn_e - pn_s);

      // Find opening paren
      size_t op = body.find('(', pn_e);
      if (op == std::string::npos || op >= end) break;

      // Find matching closing paren
      size_t cp = body.find(')', op);
      if (cp == std::string::npos || cp > end) break;

      std::string net_name = body.substr(op + 1, cp - op - 1);
      // Trim
      net_name.erase(0, net_name.find_first_not_of(" \t\n\r"));
      net_name.erase(net_name.find_last_not_of(" \t\n\r") + 1);

      if (!net_name.empty()) {
        reg_info.pins[pin_name] = net_name;
        net_connections_[net_name].push_back({inst_name, pin_name});
      }

      scan = cp + 1;
    }

    if (!reg_info.pins.empty()) {
      all_instances_[inst_name] = reg_info;
      module_inst_count++;

      if (isRegisterCell(cell_type)) {
        registers_[inst_name] = reg_info;
        module_reg_count++;
      }
    }

    pos = end + 2;
  }

  logger_->info(utl::CTS, 321, "Module {}: {} instances, {} registers",
                module_name, module_inst_count, module_reg_count);
}

void VerilogFFExtractor::parseVerilog()
{
  logger_->info(utl::CTS, 350, "Parsing verilog file: {}", verilog_path_);

  std::ifstream file(verilog_path_);
  if (!file.is_open()) {
    logger_->error(utl::CTS, 351, "Cannot open verilog file: {}", verilog_path_);
  }

  // Read entire file
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();
  file.close();

  // JYJ (2026-03-21): Multi-module support — iterate ALL module/endmodule pairs.
  // Innovus post-route Verilog can have 100+ modules (hierarchical netlist).
  // Each module's instances are parsed and accumulated into shared maps.
  std::regex module_regex(R"(module\s+(\w+))");
  int module_count = 0;

  size_t search_pos = 0;
  while (search_pos < content.size()) {
    std::smatch module_match;
    std::string remaining = content.substr(search_pos);
    if (!std::regex_search(remaining, module_match, module_regex)) {
      break;
    }

    std::string module_name = module_match[1].str();
    size_t mod_start = search_pos + module_match.position() + module_match.length();

    // Find the first ';' after module declaration (end of port list)
    size_t port_end = content.find(';', mod_start);
    if (port_end == std::string::npos) {
      break;
    }
    size_t body_start = port_end + 1;

    // Find matching endmodule
    size_t endmod = content.find("endmodule", body_start);
    if (endmod == std::string::npos) {
      endmod = content.size();
    }

    std::string body = content.substr(body_start, endmod - body_start);

    module_count++;
    if (module_count <= 5 || module_count % 100 == 0) {
      logger_->info(utl::CTS, 304, "Module {}/{}: {}", module_count, "?", module_name);
    }

    parseModuleBody(body, module_name);

    // Move past this endmodule
    search_pos = endmod + 9;  // length of "endmodule"
  }

  if (module_count == 0) {
    logger_->error(utl::CTS, 352, "No module found in verilog file");
  }

  logger_->info(utl::CTS, 305, "Parsed {} modules: {} registers, {} instances, {} nets",
                module_count, registers_.size(), all_instances_.size(),
                net_connections_.size());
}

// JYJ (2026-03-21): ODB-based extraction — bypasses Verilog parsing entirely.
// Uses ODB's flat netlist (dbInst/dbITerm/dbNet) to build connectivity maps.
// This is correct for ANY design because ODB always stores flattened instances.
// Populates the same registers_, all_instances_, net_connections_ maps as parseVerilog().
void VerilogFFExtractor::parseODB()
{
  logger_->info(utl::CTS, 370, "ODB-based extraction (flat netlist)");

  registers_.clear();
  all_instances_.clear();
  net_connections_.clear();
  combo_out_nets_.clear();
  odb_mode_ = true;  // Names are already ODB-compatible, no escaping needed

  int total_insts = 0;
  int reg_count = 0;

  for (auto* inst : block_->getInsts()) {
    const std::string inst_name = inst->getName();
    const std::string master_name = inst->getMaster()->getName();
    const bool is_reg = isRegisterCell(master_name);

    RegisterInfo info;
    info.cell_type = master_name;

    for (auto* iterm : inst->getITerms()) {
      auto* mterm = iterm->getMTerm();
      const auto io_type = mterm->getIoType();

      // Skip power/ground (INOUT)
      if (io_type == odb::dbIoType::INOUT) continue;

      auto* net = iterm->getNet();
      if (net == nullptr) continue;

      const std::string pin_name = mterm->getName();
      const std::string net_name = net->getName();

      info.pins[pin_name] = net_name;

      if (io_type == odb::dbIoType::INPUT) {
        // Record sink connections for BFS traversal
        net_connections_[net_name].push_back({inst_name, pin_name});
      } else if (io_type == odb::dbIoType::OUTPUT && !is_reg) {
        // Record combo gate output nets for BFS (skip register outputs,
        // handled separately via Q/QN in extractFFEdges)
        combo_out_nets_[inst_name].push_back(net_name);
      }
    }

    if (!info.pins.empty()) {
      all_instances_[inst_name] = info;
      total_insts++;

      if (is_reg) {
        registers_[inst_name] = info;
        reg_count++;
      }
    }
  }

  logger_->info(utl::CTS, 371, "ODB parsed: {} instances, {} registers, {} nets",
                total_insts, reg_count, net_connections_.size());

  if (reg_count == 0) {
    logger_->warn(utl::CTS, 372, "No registers found. Check cell naming (DFF*/SDFF*/DFFH*).");
  }
}

std::vector<FFEdgeVerilog> VerilogFFExtractor::extractFFEdges()
{
  logger_->info(utl::CTS, 307, "Extracting FF-to-FF edges...");

  std::vector<FFEdgeVerilog> edges;

  // Debug: print first few registers
  int debug_count = 0;
  for (const auto& [inst_name, info] : registers_) {
    if (debug_count < 5) {
      std::string pins_str;
      for (const auto& [pin, net] : info.pins) {
        pins_str += pin + "->" + net + " ";
      }
      logger_->info(utl::CTS, 320, "Register {}: {}", inst_name, pins_str);
      debug_count++;
    }
  }

  // For each register, find its Q output net
  for (const auto& [from_inst, from_info] : registers_) {
    const auto& pins = from_info.pins;

    // Find Q or QN output
    std::string q_net;
    auto q_it = pins.find("Q");
    if (q_it != pins.end()) {
      q_net = q_it->second;
    } else {
      auto qn_it = pins.find("QN");
      if (qn_it != pins.end()) {
        q_net = qn_it->second;
      }
    }

    if (q_net.empty()) {
      continue;  // Some FFs may have only complemented outputs
    }

    // BFS to find all reachable FF D pins through combinational logic
    std::unordered_set<std::string> visited_nets;
    std::vector<std::string> queue;
    queue.push_back(q_net);
    visited_nets.insert(q_net);

    while (!queue.empty()) {
      std::string current_net = queue.back();
      queue.pop_back();

      auto net_it = net_connections_.find(current_net);
      if (net_it == net_connections_.end()) {
        continue;
      }

      const auto& connected = net_it->second;

      for (const auto& [to_inst, to_pin] : connected) {
        if (to_inst == from_inst) {
          continue;  // Skip self-loops
        }

        // Check if destination is a register D pin
        if (registers_.find(to_inst) != registers_.end()) {
          if (to_pin == "D") {
            FFEdgeVerilog edge;
            edge.from_ff = from_inst;
            edge.to_ff = to_inst;
            edge.via_net = current_net;

            // Fill locations from ODB if available
            fillLocations(edge);

            edges.push_back(edge);
          }
          // Don't traverse through registers
          continue;
        }

        // For non-register instances (combinational gates), find their output nets.
        // JYJ (2026-03-21): Use combo_out_nets_ if available (ODB mode, accurate).
        // Fall back to pin-name heuristic (Verilog mode).
        auto combo_it = combo_out_nets_.find(to_inst);
        if (combo_it != combo_out_nets_.end()) {
          // ODB mode: pre-built output net list (from dbITerm::getIoType)
          for (const auto& gate_net : combo_it->second) {
            if (visited_nets.find(gate_net) == visited_nets.end()) {
              queue.push_back(gate_net);
              visited_nets.insert(gate_net);
            }
          }
        } else {
          // Verilog mode fallback: use pin name heuristic
          auto gate_it = all_instances_.find(to_inst);
          if (gate_it != all_instances_.end()) {
            const auto& gate_pins = gate_it->second.pins;
            for (const auto& [gate_pin, gate_net] : gate_pins) {
              if (gate_pin == "Y" || gate_pin == "Z" || gate_pin == "Q" ||
                  gate_pin == "QN" || gate_pin == "ZN" ||
                  gate_pin == "CO" || gate_pin == "S") {
                if (visited_nets.find(gate_net) == visited_nets.end()) {
                  queue.push_back(gate_net);
                  visited_nets.insert(gate_net);
                }
              }
            }
          }
        }
      }
    }
  }

  logger_->info(utl::CTS, 308, "Found {} FF-to-FF edges", edges.size());

  return edges;
}

// Helper function to escape brackets for ODB lookup
static std::string escapeForODB(const std::string& name)
{
  std::string result;
  std::string input = name;

  // Remove leading backslash if present
  if (!input.empty() && input[0] == '\\') {
    input = input.substr(1);
  }

  // Escape brackets: [ -> \[, ] -> \]
  for (char c : input) {
    if (c == '[' || c == ']') {
      result += '\\';
    }
    result += c;
  }
  return result;
}

void VerilogFFExtractor::fillLocations(FFEdgeVerilog& edge)
{
  if (!block_) {
    return;
  }

  // Get locations from ODB
  // In ODB mode, names are already ODB-compatible (no escaping needed).
  // In Verilog mode, need to escape brackets and remove leading backslash.
  std::string from_odb = odb_mode_ ? edge.from_ff : escapeForODB(edge.from_ff);
  std::string to_odb = odb_mode_ ? edge.to_ff : escapeForODB(edge.to_ff);

  odb::dbInst* from_inst = block_->findInst(from_odb.c_str());
  odb::dbInst* to_inst = block_->findInst(to_odb.c_str());

  // JYJ (2026-03-22): Debug log for first few lookups to diagnose location fill
  static int loc_debug_count = 0;
  if (loc_debug_count < 5) {
    logger_->info(utl::CTS, 620, "fillLocations debug: odb_mode={}, from='{}' -> {}",
                  odb_mode_ ? "true" : "false", from_odb,
                  from_inst ? "FOUND" : "NULL");
    loc_debug_count++;
  }

  if (from_inst) {
    int x, y;
    from_inst->getLocation(x, y);
    edge.from_x = x;
    edge.from_y = y;
    // Get tier from master name (_upper -> 1, _bottom -> 0)
    std::string master = from_inst->getMaster()->getName();
    edge.from_tier = (master.find("_upper") != std::string::npos) ? 1 : 0;
  }

  if (to_inst) {
    int x, y;
    to_inst->getLocation(x, y);
    edge.to_x = x;
    edge.to_y = y;
    // Get tier from master name (_upper -> 1, _bottom -> 0)
    std::string master = to_inst->getMaster()->getName();
    edge.to_tier = (master.find("_upper") != std::string::npos) ? 1 : 0;
  }
}

void VerilogFFExtractor::fillTimingInfo(std::vector<FFEdgeVerilog>& edges)
{
  if (!sta_ || !network_) {
    logger_->warn(utl::CTS, 322, "STA not available, skipping timing info");
    return;
  }

  logger_->info(utl::CTS, 323, "Filling timing info for {} edges...", edges.size());

  // Ensure timing is updated
  sta_->ensureGraph();
  sta_->ensureClkArrivals();
  sta_->updateTiming(false);

  sta::Graph* graph = sta_->graph();
  if (!graph) {
    logger_->warn(utl::CTS, 324, "STA graph not available");
    return;
  }

  constexpr float SEC_TO_NS = 1e9f;
  int timing_found = 0;
  int hold_found = 0;

  // Group edges by from_ff for efficient findPathEnds calls
  std::unordered_map<std::string, std::vector<size_t>> from_ff_edges;
  for (size_t i = 0; i < edges.size(); i++) {
    from_ff_edges[edges[i].from_ff].push_back(i);
  }

  logger_->info(utl::CTS, 327, "Grouped into {} unique from_ff startpoints", from_ff_edges.size());

  // Build edge lookup: (from_ff, to_ff) -> edge index
  std::unordered_map<std::string, size_t> edge_lookup;
  for (size_t i = 0; i < edges.size(); i++) {
    std::string key = edges[i].from_ff + "|" + edges[i].to_ff;
    edge_lookup[key] = i;
  }

  int call_count = 0;

  // Helper lambda to find edge index from ODB instance name
  auto findEdgeIdx = [&](const std::string& from_ff_name,
                         const std::string& odb_name,
                         const std::vector<size_t>& edge_indices) -> int {
    std::string to_name;
    std::string key;
    auto it = edge_lookup.end();

    // Format 1: \name (Verilog escaped)
    to_name = "\\" + odb_name;
    key = from_ff_name + "|" + to_name;
    it = edge_lookup.find(key);

    // Format 2: name (raw ODB)
    if (it == edge_lookup.end()) {
      to_name = odb_name;
      key = from_ff_name + "|" + to_name;
      it = edge_lookup.find(key);
    }

    // Format 3: Try matching with escapeForODB conversion
    if (it == edge_lookup.end()) {
      for (size_t idx : edge_indices) {
        std::string edge_to_odb = escapeForODB(edges[idx].to_ff);
        if (edge_to_odb == odb_name) {
          it = edge_lookup.find(from_ff_name + "|" + edges[idx].to_ff);
          break;
        }
      }
    }

    return (it != edge_lookup.end()) ? static_cast<int>(it->second) : -1;
  };

  // For each unique from_ff, call findPathEnds for both SETUP and HOLD
  for (const auto& [from_ff_name, edge_indices] : from_ff_edges) {
    // Find from_ff instance in ODB
    std::string from_odb_name = odb_mode_ ? from_ff_name : escapeForODB(from_ff_name);
    odb::dbInst* from_odb_inst = block_ ? block_->findInst(from_odb_name.c_str()) : nullptr;
    if (!from_odb_inst) continue;

    sta::Instance* from_sta_inst = network_->dbToSta(from_odb_inst);
    if (!from_sta_inst) continue;

    // ============ SETUP (max) timing ============
    sta::InstanceSet* from_insts_setup = new sta::InstanceSet(network_);
    from_insts_setup->insert(from_sta_inst);
    sta::ExceptionFrom* from_setup = sta_->makeExceptionFrom(
        nullptr, nullptr, from_insts_setup, sta::RiseFallBoth::riseFall());

    sta::PathEndSeq path_ends_setup = sta_->findPathEnds(
        from_setup, nullptr, nullptr, true, nullptr,
        sta::MinMaxAll::max(), 10000, 1, true, false,
        -sta::INF, sta::INF, false, nullptr,
        true, false, false, false, false, false);

    // Process SETUP path ends
    for (sta::PathEnd* path_end : path_ends_setup) {
      if (!path_end) continue;
      const sta::Path* data_path = path_end->path();
      if (!data_path) continue;

      sta::Pin* end_pin = data_path->pin(sta_);
      if (!end_pin) continue;

      sta::Instance* end_inst = network_->instance(end_pin);
      if (!end_inst) continue;

      odb::dbInst* end_odb_inst = network_->staToDb(end_inst);
      if (!end_odb_inst) continue;

      std::string odb_name = end_odb_inst->getName();
      int idx = findEdgeIdx(from_ff_name, odb_name, edge_indices);

      if (idx >= 0 && !edges[idx].has_timing) {
        edges[idx].slack_max = sta::delayAsFloat(path_end->slack(sta_)) * SEC_TO_NS;
        edges[idx].arrival_max = sta::delayAsFloat(data_path->arrival()) * SEC_TO_NS;
        edges[idx].required_max = sta::delayAsFloat(path_end->requiredTime(sta_)) * SEC_TO_NS;
        edges[idx].has_timing = true;
        timing_found++;
      }
    }

    // ============ HOLD (min) timing ============
    sta::InstanceSet* from_insts_hold = new sta::InstanceSet(network_);
    from_insts_hold->insert(from_sta_inst);
    sta::ExceptionFrom* from_hold = sta_->makeExceptionFrom(
        nullptr, nullptr, from_insts_hold, sta::RiseFallBoth::riseFall());

    sta::PathEndSeq path_ends_hold = sta_->findPathEnds(
        from_hold, nullptr, nullptr, true, nullptr,
        sta::MinMaxAll::min(), 10000, 1, true, false,
        -sta::INF, sta::INF, false, nullptr,
        false, true, false, false, false, false);

    // Process HOLD path ends - use vertex->pin() instead of path->pin()
    for (sta::PathEnd* path_end : path_ends_hold) {
      if (!path_end) continue;

      // Get endpoint vertex directly from PathEnd
      sta::Vertex* end_vertex = path_end->vertex(sta_);
      if (!end_vertex) continue;

      sta::Pin* end_pin = end_vertex->pin();
      if (!end_pin) continue;

      sta::Instance* end_inst = network_->instance(end_pin);
      if (!end_inst) continue;

      odb::dbInst* end_odb_inst = network_->staToDb(end_inst);
      if (!end_odb_inst) continue;

      std::string odb_name = end_odb_inst->getName();
      int idx = findEdgeIdx(from_ff_name, odb_name, edge_indices);

      if (idx >= 0) {
        const sta::Path* data_path = path_end->path();
        if (data_path) {
          edges[idx].slack_min = sta::delayAsFloat(path_end->slack(sta_)) * SEC_TO_NS;
          edges[idx].arrival_min = sta::delayAsFloat(data_path->arrival()) * SEC_TO_NS;
          edges[idx].required_min = sta::delayAsFloat(path_end->requiredTime(sta_)) * SEC_TO_NS;
          hold_found++;
        }
      }
    }

    call_count++;
  }

  logger_->info(utl::CTS, 325, "Found setup timing for {} / {} edges, hold timing for {} edges ({} calls)",
                timing_found, edges.size(), hold_found, call_count);
}

// JYJ (2026-03-22): Fast timing fill — O(N_ff) vertex lookups instead of findPathEnds.
// For each unique capture FF (to_ff), look up its D-pin vertex slack/arrival/required.
// All edges targeting the same capture FF get the same (worst) timing values.
// This is approximate but ~100x faster than per-edge findPathEnds.
void VerilogFFExtractor::fillTimingInfoFast(std::vector<FFEdgeVerilog>& edges)
{
  if (!sta_ || !network_ || !block_) {
    logger_->warn(utl::CTS, 630, "STA not available, skipping fast timing fill");
    return;
  }

  logger_->info(utl::CTS, 631, "Fast timing fill for {} edges (per-vertex lookup)...",
                edges.size());

  sta_->ensureGraph();
  sta_->ensureClkArrivals();
  sta_->updateTiming(false);

  sta::Graph* graph = sta_->graph();
  if (!graph) {
    logger_->warn(utl::CTS, 632, "STA graph not available");
    return;
  }

  constexpr float SEC_TO_NS = 1e9f;

  // Collect unique to_ff names
  std::unordered_set<std::string> unique_to_ffs;
  for (const auto& edge : edges) {
    unique_to_ffs.insert(edge.to_ff);
  }

  // Build per-FF timing cache: to_ff -> {slack_max, slack_min, arrival, required, ...}
  struct FFTiming {
    float slack_max = 0.0f;
    float arrival_max = 0.0f;
    float required_max = 0.0f;
    float slack_min = 0.0f;
    float arrival_min = 0.0f;
    float required_min = 0.0f;
    bool found = false;
  };

  std::unordered_map<std::string, FFTiming> ff_timing_cache;
  int cache_found = 0;

  for (const auto& ff_name : unique_to_ffs) {
    std::string odb_name = odb_mode_ ? ff_name : escapeForODB(ff_name);
    odb::dbInst* odb_inst = block_->findInst(odb_name.c_str());
    if (!odb_inst) continue;

    sta::Instance* sta_inst = network_->dbToSta(odb_inst);
    if (!sta_inst) continue;

    // Find D pin on this FF
    sta::Pin* d_pin = nullptr;
    sta::InstancePinIterator* pin_iter = network_->pinIterator(sta_inst);
    while (pin_iter->hasNext()) {
      sta::Pin* pin = pin_iter->next();
      const char* pin_name = network_->portName(pin);
      if (pin_name && std::string(pin_name) == "D") {
        d_pin = pin;
        break;
      }
    }
    delete pin_iter;

    if (!d_pin) continue;

    // Get load vertex for D pin (capture side)
    sta::Vertex* d_vertex = graph->pinLoadVertex(d_pin);
    if (!d_vertex) continue;

    FFTiming timing;

    // Setup (max) timing at this vertex
    sta::Slack setup_slack = sta_->vertexSlack(d_vertex, sta::MinMax::max());
    if (!sta::delayIsInitValue(setup_slack, sta::MinMax::max())) {
      timing.slack_max = sta::delayAsFloat(setup_slack) * SEC_TO_NS;
      timing.arrival_max = sta::delayAsFloat(
          sta_->vertexArrival(d_vertex, sta::MinMax::max())) * SEC_TO_NS;
      timing.required_max = sta::delayAsFloat(
          sta_->vertexRequired(d_vertex, sta::MinMax::max())) * SEC_TO_NS;
    }

    // Hold (min) timing at this vertex
    sta::Slack hold_slack = sta_->vertexSlack(d_vertex, sta::MinMax::min());
    if (!sta::delayIsInitValue(hold_slack, sta::MinMax::min())) {
      timing.slack_min = sta::delayAsFloat(hold_slack) * SEC_TO_NS;
      timing.arrival_min = sta::delayAsFloat(
          sta_->vertexArrival(d_vertex, sta::MinMax::min())) * SEC_TO_NS;
      timing.required_min = sta::delayAsFloat(
          sta_->vertexRequired(d_vertex, sta::MinMax::min())) * SEC_TO_NS;
    }

    timing.found = true;
    ff_timing_cache[ff_name] = timing;
    cache_found++;
  }

  logger_->info(utl::CTS, 633, "Cached timing for {} / {} unique capture FFs",
                cache_found, unique_to_ffs.size());

  // Apply cached timing to all edges
  int timing_applied = 0;
  for (auto& edge : edges) {
    auto it = ff_timing_cache.find(edge.to_ff);
    if (it != ff_timing_cache.end() && it->second.found) {
      const auto& t = it->second;
      edge.slack_max = t.slack_max;
      edge.arrival_max = t.arrival_max;
      edge.required_max = t.required_max;
      edge.slack_min = t.slack_min;
      edge.arrival_min = t.arrival_min;
      edge.required_min = t.required_min;
      edge.has_timing = true;
      timing_applied++;
    }
  }

  logger_->info(utl::CTS, 634, "Applied timing to {} / {} edges",
                timing_applied, edges.size());
}

void VerilogFFExtractor::writeCSV(const std::vector<FFEdgeVerilog>& edges,
                                  const std::string& output_file)
{
  std::ofstream file(output_file);
  if (!file.is_open()) {
    logger_->error(utl::CTS, 309, "Cannot open output file: {}", output_file);
  }

  // Write header (with setup and hold timing + tier info)
  file << "from_ff,to_ff,slack_max_ns,slack_min_ns,arrival_max_ns,arrival_min_ns,required_max_ns,required_min_ns,from_x,from_y,from_tier,to_x,to_y,to_tier\n";

  // Write edges
  for (const auto& edge : edges) {
    file << edge.from_ff << "," << edge.to_ff << ","
         << edge.slack_max << "," << edge.slack_min << ","
         << edge.arrival_max << "," << edge.arrival_min << ","
         << edge.required_max << "," << edge.required_min << ","
         << edge.from_x << "," << edge.from_y << "," << edge.from_tier << ","
         << edge.to_x << "," << edge.to_y << "," << edge.to_tier << "\n";
  }

  file.close();

  logger_->info(
      utl::CTS, 310, "Wrote {} edges to {}", edges.size(), output_file);
}

// ============================================================================
// JYJ (2026-03-17) V51_IO: Exhaustive IO timing edge extraction via STA.
// Ported to openroad_260321 (2026-03-21).
//
// PI->FF: for each input BTerm, findPathEnds (setup + hold).
// FF->PO: for each output BTerm, findPathEnds (setup + hold).
// ============================================================================
std::vector<IOEdgeVerilog> VerilogFFExtractor::extractIOEdges()
{
  std::vector<IOEdgeVerilog> result;

  if (!sta_ || !network_ || !block_) {
    logger_->warn(utl::CTS, 600,
                  "STA/network/block not available, skipping IO extraction");
    return result;
  }

  sta_->ensureGraph();
  sta_->ensureClkArrivals();
  sta_->updateTiming(false);

  constexpr float SEC_TO_NS = 1e9f;

  // Build reverse lookup: ODB inst* -> name
  std::unordered_map<odb::dbInst*, std::string> inst_to_name;
  for (const auto& [verilog_name, info] : registers_) {
    std::string odb_name = escapeForODB(verilog_name);
    odb::dbInst* inst = block_->findInst(odb_name.c_str());
    if (inst) {
      inst_to_name[inst] = inst->getName();
    }
  }

  // Collect input / output BTerms
  std::vector<odb::dbBTerm*> input_ports, output_ports;
  for (auto* bterm : block_->getBTerms()) {
    odb::dbSigType sig = bterm->getSigType();
    if (sig == odb::dbSigType::CLOCK) continue;
    auto io = bterm->getIoType();
    if (io == odb::dbIoType::INPUT) {
      input_ports.push_back(bterm);
    } else if (io == odb::dbIoType::OUTPUT) {
      output_ports.push_back(bterm);
    }
  }

  logger_->info(utl::CTS, 601,
                "Extracting IO edges ({} input ports, {} output ports)",
                input_ports.size(), output_ports.size());

  std::unordered_map<std::string, IOEdgeVerilog> edge_map;

  auto getFFName = [&](sta::Pin* pin) -> std::string {
    if (!pin) return "";
    sta::Instance* inst = network_->instance(pin);
    if (!inst) return "";
    odb::dbInst* odb_inst = network_->staToDb(inst);
    if (!odb_inst) return "";
    auto it = inst_to_name.find(odb_inst);
    if (it != inst_to_name.end()) return it->second;
    return odb_inst->getName();
  };

  int pi_setup_ct = 0, pi_hold_ct = 0, po_setup_ct = 0, po_hold_ct = 0;

  // PI->FF
  for (auto* bterm : input_ports) {
    std::string port_name = bterm->getName();
    sta::Pin* port_pin = network_->dbToSta(bterm);
    if (!port_pin) continue;

    // SETUP
    {
      sta::PinSet* from_pins = new sta::PinSet(network_);
      from_pins->insert(port_pin);
      sta::ExceptionFrom* from = sta_->makeExceptionFrom(
          from_pins, nullptr, nullptr, sta::RiseFallBoth::riseFall());
      sta::PathEndSeq path_ends = sta_->findPathEnds(
          from, nullptr, nullptr, true, nullptr,
          sta::MinMaxAll::max(), 10000, 1, true, false,
          -sta::INF, sta::INF, false, nullptr,
          true, false, false, false, false, false);
      for (sta::PathEnd* pe : path_ends) {
        if (!pe) continue;
        const sta::Path* path = pe->path();
        if (!path) continue;
        sta::Pin* end_pin = path->pin(sta_);
        std::string ff = getFFName(end_pin);
        if (ff.empty()) continue;
        std::string key = "PI_TO_FF|" + port_name + "|" + ff;
        auto& e = edge_map[key];
        e.edge_type = "PI_TO_FF"; e.port_name = port_name; e.ff_name = ff;
        float slack = sta::delayAsFloat(pe->slack(sta_)) * SEC_TO_NS;
        if (!e.has_setup || slack < e.slack_setup_ns) {
          e.slack_setup_ns = slack; e.has_setup = true; pi_setup_ct++;
        }
      }
    }
    // HOLD
    {
      sta::PinSet* from_pins = new sta::PinSet(network_);
      from_pins->insert(port_pin);
      sta::ExceptionFrom* from = sta_->makeExceptionFrom(
          from_pins, nullptr, nullptr, sta::RiseFallBoth::riseFall());
      sta::PathEndSeq path_ends = sta_->findPathEnds(
          from, nullptr, nullptr, true, nullptr,
          sta::MinMaxAll::min(), 10000, 1, true, false,
          -sta::INF, sta::INF, false, nullptr,
          false, true, false, false, false, false);
      for (sta::PathEnd* pe : path_ends) {
        if (!pe) continue;
        sta::Vertex* end_vertex = pe->vertex(sta_);
        if (!end_vertex) continue;
        sta::Pin* end_pin = end_vertex->pin();
        std::string ff = getFFName(end_pin);
        if (ff.empty()) continue;
        std::string key = "PI_TO_FF|" + port_name + "|" + ff;
        auto& e = edge_map[key];
        e.edge_type = "PI_TO_FF"; e.port_name = port_name; e.ff_name = ff;
        float slack = sta::delayAsFloat(pe->slack(sta_)) * SEC_TO_NS;
        if (!e.has_hold || slack < e.slack_hold_ns) {
          e.slack_hold_ns = slack; e.has_hold = true; pi_hold_ct++;
        }
      }
    }
  }

  // FF->PO
  for (auto* bterm : output_ports) {
    std::string port_name = bterm->getName();
    sta::Pin* port_pin = network_->dbToSta(bterm);
    if (!port_pin) continue;

    // SETUP
    {
      sta::PinSet* to_pins = new sta::PinSet(network_);
      to_pins->insert(port_pin);
      sta::ExceptionTo* to = sta_->makeExceptionTo(
          to_pins, nullptr, nullptr,
          sta::RiseFallBoth::riseFall(), sta::RiseFallBoth::riseFall());
      sta::PathEndSeq path_ends = sta_->findPathEnds(
          nullptr, nullptr, to, true, nullptr,
          sta::MinMaxAll::max(), 10000, 1, true, false,
          -sta::INF, sta::INF, false, nullptr,
          true, false, false, false, false, false);
      for (sta::PathEnd* pe : path_ends) {
        if (!pe) continue;
        const sta::Path* path = pe->path();
        if (!path) continue;
        sta::PathExpanded expanded(path, sta_);
        if (expanded.size() == 0) continue;
        const sta::Path* start_path = expanded.path(0);
        if (!start_path) continue;
        sta::Pin* start_pin = start_path->pin(sta_);
        std::string ff = getFFName(start_pin);
        if (ff.empty()) continue;
        std::string key = "FF_TO_PO|" + port_name + "|" + ff;
        auto& e = edge_map[key];
        e.edge_type = "FF_TO_PO"; e.port_name = port_name; e.ff_name = ff;
        float slack = sta::delayAsFloat(pe->slack(sta_)) * SEC_TO_NS;
        if (!e.has_setup || slack < e.slack_setup_ns) {
          e.slack_setup_ns = slack; e.has_setup = true; po_setup_ct++;
        }
      }
    }
    // HOLD
    {
      sta::PinSet* to_pins = new sta::PinSet(network_);
      to_pins->insert(port_pin);
      sta::ExceptionTo* to = sta_->makeExceptionTo(
          to_pins, nullptr, nullptr,
          sta::RiseFallBoth::riseFall(), sta::RiseFallBoth::riseFall());
      sta::PathEndSeq path_ends = sta_->findPathEnds(
          nullptr, nullptr, to, true, nullptr,
          sta::MinMaxAll::min(), 10000, 1, true, false,
          -sta::INF, sta::INF, false, nullptr,
          false, true, false, false, false, false);
      for (sta::PathEnd* pe : path_ends) {
        if (!pe) continue;
        const sta::Path* path = pe->path();
        if (!path) continue;
        sta::PathExpanded expanded(path, sta_);
        if (expanded.size() == 0) continue;
        const sta::Path* start_path = expanded.path(0);
        if (!start_path) continue;
        sta::Pin* start_pin = start_path->pin(sta_);
        std::string ff = getFFName(start_pin);
        if (ff.empty()) continue;
        std::string key = "FF_TO_PO|" + port_name + "|" + ff;
        auto& e = edge_map[key];
        e.edge_type = "FF_TO_PO"; e.port_name = port_name; e.ff_name = ff;
        float slack = sta::delayAsFloat(pe->slack(sta_)) * SEC_TO_NS;
        if (!e.has_hold || slack < e.slack_hold_ns) {
          e.slack_hold_ns = slack; e.has_hold = true; po_hold_ct++;
        }
      }
    }
  }

  result.reserve(edge_map.size());
  for (auto& [key, edge] : edge_map) {
    result.push_back(std::move(edge));
  }

  logger_->info(utl::CTS, 602,
                "Extracted {} IO edges "
                "(PI->FF: setup={}, hold={}, FF->PO: setup={}, hold={})",
                result.size(), pi_setup_ct, pi_hold_ct, po_setup_ct, po_hold_ct);

  return result;
}

void VerilogFFExtractor::writeIOCSV(const std::vector<IOEdgeVerilog>& edges,
                                     const std::string& output_file)
{
  std::ofstream file(output_file);
  if (!file.is_open()) {
    logger_->error(utl::CTS, 603, "Cannot open IO output file: {}", output_file);
  }

  file << "edge_type,port_name,ff_name,slack_setup_ns,slack_hold_ns\n";

  for (const auto& e : edges) {
    file << e.edge_type << "," << e.port_name << "," << e.ff_name << ",";
    if (e.has_setup) file << e.slack_setup_ns;
    file << ",";
    if (e.has_hold) file << e.slack_hold_ns;
    file << "\n";
  }

  file.close();

  int pi_ct = 0, po_ct = 0;
  for (const auto& e : edges) {
    if (e.edge_type == "PI_TO_FF") pi_ct++; else po_ct++;
  }

  logger_->info(utl::CTS, 604,
                "Wrote {} IO edges to {} (PI->FF: {}, FF->PO: {})",
                edges.size(), output_file, pi_ct, po_ct);
}

}  // namespace cts
