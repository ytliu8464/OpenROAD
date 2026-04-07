// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2019-2025, The OpenROAD Authors

#pragma once

#include <cassert>
#include <cstdint>
#include <deque>
#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Util.h"
#include "odb/db.h"
#include "odb/dbObject.h"

namespace utl {
class Logger;
}  // namespace utl

namespace cts {

enum InstType : uint8_t
{
  CLOCK_BUFFER,
  CLOCK_SINK,
  HYBRID_BOND  // HB via for 3D IC tier crossing
};

// 3D IC tier enumeration
enum Tier : int8_t
{
  TIER_UNKNOWN = -1,
  TIER_BOTTOM = 0,  // T1
  TIER_UPPER = 1    // T2
};

class ClockInst
{
 public:
  ClockInst(const std::string& name,
            const std::string& master,
            InstType type,
            int x,
            int y,
            odb::dbITerm* pinObj = nullptr,
            float inputCap = 0.0,
            float insertionDelay = 0.0,
            float outputCap = 0.0,
            float idealOutputCap = 0.0)
      : name_(name),
        master_(master),
        type_(type),
        location_(x, y),
        inputPinObj_(pinObj),
        inputCap_(inputCap),
        insertionDelay_(insertionDelay),
        outputCap_(outputCap),
        idealOutputCap_(idealOutputCap)
  {
  }

  std::string getName() const { return name_; }
  std::string getMaster() const { return master_; }
  int getX() const { return location_.getX(); }
  int getY() const { return location_.getY(); }
  Point<int> getLocation() const { return location_; }

  void setInstObj(odb::dbInst* inst) { instObj_ = inst; }
  odb::dbInst* getDbInst() const { return instObj_; }
  void setInputPinObj(odb::dbITerm* pin) { inputPinObj_ = pin; }
  odb::dbITerm* getDbInputPin() const { return inputPinObj_; }
  void setInputCap(float cap) { inputCap_ = cap; }
  float getInputCap() const { return inputCap_; }
  bool isClockBuffer() const { return type_ == CLOCK_BUFFER; }
  double getInsertionDelay() const { return insertionDelay_; }
  void setOutputCap(float cap) { outputCap_ = cap; }
  float getOutputCap() const { return outputCap_; }
  void setIdealOutputCap(float cap) { idealOutputCap_ = cap; }
  float getIdealOutputCap() const { return idealOutputCap_; }

  // 3D CTS tier support
  void setTier(Tier tier) { tier_ = tier; }
  Tier getTier() const { return tier_; }
  void inferTierFromMaster()
  {
    // Infer tier from master cell name suffix
    if (master_.find("_upper") != std::string::npos) {
      tier_ = TIER_UPPER;
    } else if (master_.find("_bottom") != std::string::npos) {
      tier_ = TIER_BOTTOM;
    } else {
      tier_ = TIER_UNKNOWN;
    }
  }

 private:
  std::string name_;
  std::string master_;
  InstType type_;
  Point<int> location_;
  odb::dbInst* instObj_ = nullptr;
  odb::dbITerm* inputPinObj_ = nullptr;
  float inputCap_;
  double insertionDelay_;  // insertion delay in terms of length, not time
  float outputCap_;        // current load cap seen by this instance
  float idealOutputCap_;   // ideal load cap needed for perfectly balanced tree
  Tier tier_ = TIER_UNKNOWN;  // 3D IC tier (upper or bottom)
};

//-----------------------------------------------------------------------------

class ClockSubNet
{
 private:
  std::string name_;
  std::deque<ClockInst*> instances_;
  std::unordered_map<ClockInst*, unsigned> mapInstToIdx_;
  bool leafLevel_ = false;
  int level_ = -1;
  odb::dbNet* netObj_ = nullptr;

 public:
  explicit ClockSubNet(const std::string& name) : name_(name) {}

  void setNetObj(odb::dbNet* net) { netObj_ = net; }
  odb::dbNet* getNetObj() { return netObj_; }
  void setTreeLevel(int level) { level_ = level; }
  int getTreeLevel() { return level_; }
  void setLeafLevel(bool isLeaf) { leafLevel_ = isLeaf; }
  bool isLeafLevel() const { return leafLevel_; }

  void addInst(ClockInst& inst)
  {
    instances_.push_back(&inst);
    mapInstToIdx_[&inst] = instances_.size() - 1;
  }

  unsigned findIndex(ClockInst* inst) const { return mapInstToIdx_.at(inst); }

  void replaceSink(ClockInst* curSink, ClockInst* newSink)
  {
    const unsigned idx = findIndex(curSink);
    instances_[idx] = newSink;
    mapInstToIdx_.erase(curSink);
    mapInstToIdx_[newSink] = idx;
  }

  void removeSinks(std::set<ClockInst*> sinksToRemove)
  {
    ClockInst* driver = getDriver();
    std::vector<ClockInst*> instsToPreserve;
    forEachSink([&](ClockInst* inst) {
      if (sinksToRemove.find(inst) == sinksToRemove.end()) {
        instsToPreserve.emplace_back(inst);
      }
    });
    instances_.clear();
    mapInstToIdx_.clear();
    addInst(*driver);
    for (auto inst : instsToPreserve) {
      addInst(*inst);
    }
  }

  std::string getName() const { return name_; }
  int getNumSinks() const { return instances_.size() - 1; }

  ClockInst* getDriver() const
  {
    assert(instances_.size() > 0);
    return instances_[0];
  }

  void forEachSink(const std::function<void(ClockInst*)>& func) const
  {
    if (instances_.size() < 2) {
      return;
    }
    for (unsigned inst = 1; inst < instances_.size(); ++inst) {
      func(instances_[inst]);
    }
  }
};

class Clock
{
 public:
  Clock(const std::string& netName,
        const std::string& clockPin,
        const std::string& sdcClockName,
        int clockPinX,
        int clockPinY);

  ClockInst& addClockBuffer(const std::string& name,
                            const std::string& master,
                            int x,
                            int y)
  {
    clockBuffers_.emplace_back(
        name + "_" + getName(), master, CLOCK_BUFFER, x, y);
    mapNameToInst_[name + "_" + getName()] = &clockBuffers_.back();
    return clockBuffers_.back();
  }

  // 3D CTS: Add HB (Hybrid Bond) via instance for tier crossing
  ClockInst& addHybridBond(const std::string& name,
                           int x,
                           int y,
                           Tier sourceTier,
                           Tier targetTier)
  {
    // HB via connects two tiers at the same (x,y) location
    std::string hbName = "hb_" + name + "_" + getName();
    hybridBonds_.emplace_back(hbName, "HB_VIA", HYBRID_BOND, x, y);
    ClockInst& hb = hybridBonds_.back();
    hb.setTier(sourceTier);  // source tier
    mapNameToInst_[hbName] = &hb;
    numHybridBonds_++;
    return hb;
  }

  unsigned getNumHybridBonds() const { return numHybridBonds_; }

  ClockInst* findClockByName(const std::string& name)
  {
    if (mapNameToInst_.find(name) == mapNameToInst_.end()) {
      return nullptr;
    }

    return mapNameToInst_.at(name);
  }

  ClockSubNet& addSubNet(const std::string& name)
  {
    subNets_.emplace_back(name + "_" + getName());
    return subNets_.back();
  }

  void addSink(const std::string& name, int x, int y)
  {
    sinks_.emplace_back(name, "", CLOCK_SINK, x, y);
  }

  void addSink(const std::string& name,
               int x,
               int y,
               odb::dbITerm* pinObj,
               float inputCap)
  {
    sinks_.emplace_back(name, "", CLOCK_SINK, x, y, pinObj, inputCap);
  }

  void addSink(const std::string& name,
               int x,
               int y,
               odb::dbITerm* pinObj,
               float inputCap,
               float insDelay)
  {
    sinks_.emplace_back(name, "", CLOCK_SINK, x, y, pinObj, inputCap, insDelay);
  }

  // 3D CTS: addSink with tier parameter
  void addSink(const std::string& name,
               int x,
               int y,
               odb::dbITerm* pinObj,
               float inputCap,
               float insDelay,
               Tier tier)
  {
    sinks_.emplace_back(name, "", CLOCK_SINK, x, y, pinObj, inputCap, insDelay);
    sinks_.back().setTier(tier);
  }

  std::string getName() const { return netName_; }
  std::string getSdcName() const { return sdcClockName_; }
  unsigned getNumSinks() const { return sinks_.size(); }

  Box<int> computeSinkRegion();
  Box<double> computeSinkRegionClustered(
      const std::vector<std::pair<float, float>>& sinks);
  Box<double> computeNormalizedSinkRegion(double factor);

  void report(utl::Logger* logger) const;

  void forEachSink(const std::function<void(const ClockInst&)>& func) const;
  void forEachSink(const std::function<void(ClockInst&)>& func);
  void forEachClockBuffer(
      const std::function<void(const ClockInst&)>& func) const;
  void forEachClockBuffer(const std::function<void(ClockInst&)>& func);
  void forEachSubNet(const std::function<void(const ClockSubNet&)>& func) const
  {
    for (const ClockSubNet& subNet : subNets_) {
      func(subNet);
    }
  }

  void forEachSubNet(const std::function<void(ClockSubNet&)>& func)
  {
    unsigned size = subNets_.size();
    // We want to use ranged for loops in here beacause
    // the user may add new items to subNets_.
    // C++11 "for each" loops will crash due to invalid
    // iterators.
    for (unsigned idx = 0; idx < size; ++idx) {
      func(subNets_[idx]);
    }
  }

  void setMaxLevel(unsigned level) { numLevels_ = level; }
  unsigned getMaxLevel() const { return numLevels_; }

  void setNetObj(odb::dbNet* net) { netObj_ = net; }
  odb::dbNet* getNetObj() { return netObj_; }

  void setDriverPin(odb::dbObject* pin) { driverPin_ = pin; }
  odb::dbObject* getDriverPin() const { return driverPin_; }

 private:
  std::string netName_;
  std::string clockPin_;
  std::string sdcClockName_;
  int clockPinX_;
  int clockPinY_;

  std::deque<ClockInst> sinks_;
  std::deque<ClockInst> clockBuffers_;
  std::deque<ClockInst> hybridBonds_;  // 3D CTS: HB via instances
  std::deque<ClockSubNet> subNets_;
  std::unordered_map<std::string, ClockInst*> mapNameToInst_;

  odb::dbNet* netObj_ = nullptr;
  odb::dbObject* driverPin_ = nullptr;

  unsigned numLevels_ = 0;
  unsigned numHybridBonds_ = 0;  // 3D CTS: count of HB vias
};

}  // namespace cts
