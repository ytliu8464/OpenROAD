/////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2025, The Regents of the University of California
// All rights reserved.
//
// BSD 3-Clause License
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////////
#pragma once
#include <array>
#include <iostream>
#include <memory>
#include <vector>

namespace rsz {

enum class BufferedTreeNodeType
{
  DRIVER,
  SINK,
  BUFFER
};

class BufferedTreeNode;
using BufferedTreeNodePtr = std::shared_ptr<BufferedTreeNode>;

class BufferedTreeNode
{
 public:
  // BufferedTreeNodeType node_type;
  // int x, y;
  // double slew;
  // double capacitance;
  // std::string pin_name;
  // std::string instance_name;
  // int id; //id of each node
  // std::vector<BufferedTreeNodePtr> children;

  // Default constructor
  BufferedTreeNode() = default;

  // Parameterized constructor
  BufferedTreeNode(BufferedTreeNodeType _type,
                   int _x,
                   int _y,
                   double _slew,
                   double _capacitance,
                   const std::string& _pin_name,
                   const std::string& _instance_name,
                   int _id);

  // Getter functions
  BufferedTreeNodeType getNodeType() const;
  int getX() const;
  int getY() const;
  double getSlew() const;
  double getCapacitance() const;
  const std::string& getPinName() const;
  const std::string& getInstanceName() const;
  int getId() const;
  const std::vector<BufferedTreeNodePtr>& getChildren() const;
  std::vector<BufferedTreeNodePtr>& getChildren();

  // Setter functions
  void setNodeType(BufferedTreeNodeType type);
  void setX(int x);
  void setY(int y);
  void setSlew(double slew);
  void setCapacitance(double capacitance);
  void setPinName(const std::string& pin_name);
  void setInstanceName(const std::string& instance_name);
  void setId(int id);

  // Add a child node
  void addChild(const BufferedTreeNodePtr& child);
  void printNode(int depth = 0) const;

 private:
  BufferedTreeNodeType node_type_;
  int x_;
  int y_;
  double slew_;
  double capacitance_;
  std::string pin_name_;
  std::string instance_name_;
  int id_;
  std::vector<BufferedTreeNodePtr> children_;
};

class BufferedTree
{
 public:
  // Default constructor
  BufferedTree() = default;

  void setRoot(const BufferedTreeNodePtr& node);

  BufferedTreeNodePtr getRoot() const;

  void addChild(const BufferedTreeNodePtr& parent,
                const BufferedTreeNodePtr& child);

  void printTree() const;

  void clear();

 private:
  BufferedTreeNodePtr root_;

  // Helper function to recursively clear nodes
  void clearTree(const BufferedTreeNodePtr& node);
};

}  // namespace rsz
