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

#include <iostream>
#include <memory>
#include <utility>
#include <vector>
#include <string>
#include <queue>

namespace rsz {

// ----------------------------------------------------------------------------
// BufferedTreeNode
// ----------------------------------------------------------------------------

std::string bufferedTreeNodeTypeToString(BufferedTreeNodeType type)
{
  switch (type) {
    case BufferedTreeNodeType::DRIVER:
      return "Driver";
    case BufferedTreeNodeType::SINK:
      return "Sink";
    case BufferedTreeNodeType::BUFFER:
      return "Buffer";
  }
}


// ----------------------------------------------------------------------------
// BufferedTree
// ----------------------------------------------------------------------------
void BufferedTree::setRoot(BufferedTreeNodePtr root_node)
{
  root_ = std::move(root_node);
}

BufferedTreeNodePtr BufferedTree::getRoot() const
{
  return root_;
}

void BufferedTree::printTree() const
{
  // Print the tree in a pre-order DFS traversal.
  int node_id = 0;
  std::queue<BufferedTreeNodePtr> q;
  q.push(root_);

  while (!q.empty()) {
    auto node = q.front();
    q.pop();
    node->node_id = node_id;
    node_id++;
    for (const auto& child : node->children) {
      q.push(child);
    }
  }


  std::cout << "Begin Tree Print\n";
  // Reset the queue and print the nodes.
  auto printNode = [](BufferedTreeNodePtr node) {
    std::cout << "Node ID: " << node->node_id << "\t";
    std::cout << "Node type: " << bufferedTreeNodeTypeToString(node->node_type) << "\t";
    std::cout << "Pin name: " << node->pin_name << "\t";
    std::cout << "Instance name: " << node->instance_name << "\t";
    std::cout << "Location: ( " << node->x << " , " << node->y << " )\t";
    std::cout << "Input slew: " << node->input_slew << "\t";
    std::cout << "Output slew: " << node->output_slew << "\t";
    std::cout << "Input cap: " << node->input_cap << "\t";
    std::cout << "Output cap: " << node->output_cap << "\t";
    std::cout << "Fanout: " << node->fanout << "\t";
    std::cout << "Parent: " << (node->parent ? std::to_string(node->parent->node_id) : "None") << "\t";
    std::cout << "Children: " << node->children.size() << "\n";
  };

  q.push(root_);
  while (!q.empty()) {
    auto node = q.front();
    q.pop();
    printNode(node);
    for (const auto& child : node->children) {
      q.push(child);
    }
  }

  std::cout << "End Tree Print\n";
}



void BufferedTree::clear()
{
  // Disconnect all nodes from the tree.
  std::queue<BufferedTreeNodePtr> q;
  q.push(root_);
  while (!q.empty()) {
    auto node = q.front();
    q.pop();

    for (const auto& child : node->children) {
      q.push(child);
    }

    node->children.clear();
    node->parent = nullptr;
  }
  root_ = nullptr;
}



}  // namespace rsz
