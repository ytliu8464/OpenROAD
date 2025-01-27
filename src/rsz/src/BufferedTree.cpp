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

#include "BufferedTree.h"

#include <array>
#include <iostream>
#include <memory>
#include <vector>

namespace rsz {

// BufferedTreeNode construction function
BufferedTreeNode::BufferedTreeNode(BufferedTreeNodeType _type,
                                   int _x,
                                   int _y,
                                   double _slew,
                                   double _capacitance,
                                   const std::string& _pin_name,
                                   const std::string& _instance_name,
                                   int _id)
    : node_type_(_type),
      x_(_x),
      y_(_y),
      slew_(_slew),
      capacitance_(_capacitance),
      pin_name_(_pin_name),
      instance_name_(_instance_name),
      id_(_id)
{
}

// Getter implementations
BufferedTreeNodeType BufferedTreeNode::getNodeType() const
{
  return node_type_;
}

int BufferedTreeNode::getX() const
{
  return x_;
}

int BufferedTreeNode::getY() const
{
  return y_;
}

double BufferedTreeNode::getSlew() const
{
  return slew_;
}

double BufferedTreeNode::getCapacitance() const
{
  return capacitance_;
}

const std::string& BufferedTreeNode::getPinName() const
{
  return pin_name_;
}

const std::string& BufferedTreeNode::getInstanceName() const
{
  return instance_name_;
}

int BufferedTreeNode::getId() const
{
  return id_;
}

const std::vector<BufferedTreeNodePtr>& BufferedTreeNode::getChildren() const
{
  return children_;
}

std::vector<BufferedTreeNodePtr>& BufferedTreeNode::getChildren()
{
  return children_;
}

// Setter implementations
void BufferedTreeNode::setNodeType(BufferedTreeNodeType type)
{
  node_type_ = type;
}

void BufferedTreeNode::setX(int x)
{
  x_ = x;
}

void BufferedTreeNode::setY(int y)
{
  y_ = y;
}

void BufferedTreeNode::setSlew(double slew)
{
  slew_ = slew;
}

void BufferedTreeNode::setCapacitance(double capacitance)
{
  capacitance_ = capacitance;
}

void BufferedTreeNode::setPinName(const std::string& pin_name)
{
  pin_name_ = pin_name;
}

void BufferedTreeNode::setInstanceName(const std::string& instance_name)
{
  instance_name_ = instance_name;
}

void BufferedTreeNode::setId(int id)
{
  id_ = id;
}

// Add a child node
void BufferedTreeNode::addChild(const BufferedTreeNodePtr& child)
{
  children_.push_back(child);
}

void BufferedTreeNode::printNode(int depth) const
{
  std::string typeStr;
  switch (node_type_) {
    case BufferedTreeNodeType::DRIVER:
      typeStr = "Driver";
      break;
    case BufferedTreeNodeType::BUFFER:
      typeStr = "Buffer";
      break;
    case BufferedTreeNodeType::SINK:
      typeStr = "Sink";
      break;
  }

  for (int i = 0; i < depth; i++)
    std::cout << "--";
  std::cout << typeStr << " (" << x_ << ", " << y_ << ") "
            << "Instance_name: " << instance_name_ << "Slew: " << slew_
            << ", Capacitance: " << capacitance_ << std::endl;

  for (const auto& child : children_) {
    child->printNode(depth + 1);
  }
}

void BufferedTree::setRoot(const BufferedTreeNodePtr& root)
{
  root_ = root;
}

BufferedTreeNodePtr BufferedTree::getRoot() const
{
  return root_;
}

void BufferedTree::addChild(const BufferedTreeNodePtr& parent,
                            const BufferedTreeNodePtr& child)
{
  if (parent) {
    parent->addChild(child);
  }
}

void BufferedTree::printTree() const
{
  if (root_) {
    root_->printNode();
  } else {
    std::cout << "Tree is empty.\n";
  }
}

void BufferedTree::clear()
{
  clearTree(root_);
  root_ = nullptr;
}

void BufferedTree::clearTree(const BufferedTreeNodePtr& node)
{
  if (!node)
    return;

  // Recursively clear children
  for (const auto& child : node->getChildren()) {
    clearTree(child);
  }

  node->getChildren().clear();
}

}  // namespace rsz
