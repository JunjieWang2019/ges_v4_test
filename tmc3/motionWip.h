/* The copyright in this software is being made available under the BSD
 * Licence, included below.  This software may be subject to other third
 * party and contributor rights, including patent rights, and no such
 * rights are granted under this licence.
 *
 * Copyright (c) 2017-2018, ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of the ISO/IEC nor the names of its contributors
 *   may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once
#include "TMC3.h"
#include <vector>

#include "PCCPointSet.h"
#include "PCCTMC3Encoder.h"
#include "entropy.h"
#include "hls.h"

#include <queue>
#include <tuple>

namespace pcc {

//============================================================================
static const unsigned int motionParamPrec = 16;
static const unsigned int motionParamScale = 1 << motionParamPrec;
static const unsigned int motionParamOffset = 1 << (motionParamPrec - 1);
int plus1log2shifted4(int x);
struct PCCOctree3Node;
struct MSOctree;
//============================================================================

int roundIntegerHalfInf(const double x);

//----------------------------------------- LOCAL MOTION -------------------
struct PUtree {
  std::vector<bool> popul_flags;
  std::vector<bool> split_flags;
  std::vector<point_t> MVs;
};

int deriveMotionMaxPrefixBits(const GeometryParameterSet::Motion& param);
int deriveMotionMaxSuffixBits(const GeometryParameterSet::Motion& param);

struct LPUwindow {
  Vec3<int> pos;
  Vec3<attr_t> color;
};


void encode_splitPU_MV_MC(
  const MSOctree& mSOctree,
  PCCOctree3Node* node0,
  PUtree* local_PU_tree,
  const GeometryParameterSet::Motion& param,
  point_t nodeSizeLog2,
  EntropyEncoder* arithmeticEncoder,
  PCCPointSet3* compensatedPointCloud,
  int numLPUPerLine,
  int log2MotionBlkSize,
  std::vector<MotionVector>& motionVectors);

void extracPUsubtree(
  const GeometryParameterSet::Motion& param,
  PUtree* local_PU_tree,
  int block_size,
  int& pos_fs,
  int& pos_fp,
  int& pos_MV,
  PUtree* destination_tree);

// motion decoder

void decode_splitPU_MV_MC(
  const MSOctree& mSOctree,
  PCCOctree3Node* node0,
  const GeometryParameterSet::Motion& param,
  point_t nodeSizeLog2,
  EntropyDecoder* arithmeticDecoder,
  PCCPointSet3* compensatedPointCloud,
  int numLPUPerLine,
  int log2MotionBlkSize,
  std::vector<MotionVector>& motionVectors);

  //============================================================================

class MotionEntropyEstimate;
struct MSOctree {
  MSOctree& operator=(const MSOctree&) = default;
  MSOctree& operator=(MSOctree&&) = default;
  MSOctree() = default;
  MSOctree(const MSOctree&) = default;
  MSOctree(MSOctree&&) = default;
  MSOctree(
    PCCPointSet3* predPointCloud,
    point_t offsetOrigin,
    uint32_t leafSizeLog2 = 0
    );

  struct MSONode {
    uint32_t start;
    uint32_t end;
    std::array<uint32_t, 8> child = {}; // 0 means none
    int32_t sizeMinus1;
    point_t pos0;
    uint32_t parent;
    uint32_t reserved; // to align to 64 bytes (otherwise could be avoided)

    uint32_t numPoints() const { return end - start; }
  };

  point_t offsetOrigin; // offset applied to origin while construction the octree
  uint32_t maxDepth; // depth of full octree to get unitary sized nodes
  uint32_t depth; // depth of the motion search octree
  PCCPointSet3* pointCloud;
  std::vector<MSONode> nodes;

  int32_t
  nearestNeighbour_estimateDMax(point_t pos, int32_t d_max, uint32_t depthMax = UINT32_MAX) const;

  std::tuple<int, int, int>
  nearestNeighbour(point_t pos, int32_t d_max, uint32_t depthMax = UINT32_MAX) const;

  std::tuple<std::queue<uint32_t>, int>
  nearestNodes(point_t node0Pos0, int32_t d_max, uint32_t node0SizeLog2) const;

  std::tuple<uint32_t, int>
  nearestNode(point_t node0Pos0, int32_t d_max, uint32_t node0SizeLog2) const;

  double
  find_motion(
    const GeometryParameterSet::Motion& param,
    const MotionEntropyEstimate& motionEntropy,
    const MSOctree& mSOctreeOrig,
    uint32_t mSOctreeOrigNodeIdx,
    const PCCPointSet3& Block0,
    const point_t& xyz0,
    int local_size,
    PUtree* local_PU_tree
  ) const;

  void
  apply_motion(
    point_t Mvd,
    PCCOctree3Node* node0,
    const GeometryParameterSet::Motion& param,
    int nodeSizeLog2,
    PCCPointSet3* compensatedPointCloud,
    uint32_t depthMax = UINT32_MAX
  ) const;
private:
  mutable std::queue<int> fifo; // for search
};

//----------------------------------------------------------------------------
bool
motionSearchForNode(
  const MSOctree& mSOctreeOrig,
  const MSOctree& mSOctree,
  const PCCOctree3Node* node0,
  const GeometryParameterSet::Motion& param,
  int nodeSizeLog2,
  EntropyEncoder* arithmeticEncoder,
  PUtree* local_PU_tree);

//============================================================================

}  // namespace pcc
