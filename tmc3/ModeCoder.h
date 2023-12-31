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
#include <cstdint>

#include "FixedPoint.h"
#include "quantization.h"
#include "hls.h"
#include <vector>
#include <deque>
#include "TMC3.h"
#include "PCCPointSet.h"
#include "entropy.h"
#include "hls.h"
#include "PCCMisc.h"

#define NUMBER_OF_LEVELS_MODE 5
#define NUMBER_OF_CONTEXT_MODE (3 * 4 * 3 * 3)

namespace pcc {
namespace attr {

enum Mode
{
  Null,
  Intra,
  Inter,
  size
};

inline bool isNull(Mode mode) { return mode == Mode::Null; }
inline bool isIntra(Mode mode) { return mode == Mode::Intra; }
inline bool isInter(Mode mode) { return mode == Mode::Inter; }

class ModeCoder {
protected:
  std::array<AdaptiveBitModel, NUMBER_OF_CONTEXT_MODE> modeIsNull;
  std::array<AdaptiveBitModel, NUMBER_OF_CONTEXT_MODE> modeIsIntra;
  bool enableInter;

public:
  ModeCoder() : enableInter(0) { reset(); }
  void reset()
  {
    for (auto& p : modeIsNull)
      p.probability = 0xC000u;
    for (auto& p : modeIsIntra)
      p.probability = 0xAAABu;
  }

  void setInterEnabled(bool flag) { enableInter = flag; }
  bool isInterEnabled() { return enableInter; }
};

class ModeEncoder : public ModeCoder {
  EntropyEncoder* arith;
  std::deque<uint16_t> buffer;
  std::array<decltype(AdaptiveBitModel::probability), NUMBER_OF_CONTEXT_MODE> rdoModeIsNull;
  std::array<decltype(AdaptiveBitModel::probability), NUMBER_OF_CONTEXT_MODE> rdoModeIsIntra;
  std::array<uint16_t, 512> lut;
  double meanDist;
  double meanRate;
  double learnRate;

public:
  std::array<double, Mode::size> entropy;

  ModeEncoder()
    : ModeCoder()
    , arith(nullptr)
    , meanDist(0)
    , meanRate(1)
    , learnRate(1)
  {
    for (int i = 0; i < NUMBER_OF_CONTEXT_MODE; i++) {
      rdoModeIsNull[i] = modeIsNull[i].probability;
      rdoModeIsIntra[i] = modeIsIntra[i].probability;
    }
  }
  void reset()
  {
    ModeCoder::reset();
    for (int i = 0; i < NUMBER_OF_CONTEXT_MODE; i++) {
      rdoModeIsNull[i] = modeIsNull[i].probability;
      rdoModeIsIntra[i] = modeIsIntra[i].probability;
    }
  }
  void set(EntropyEncoder* coder) {
    arith = coder;
    coder->getProbabilityLUT(lut.data());
    meanDist = 0;
    meanRate = 1;
    learnRate = 1;
  }
  ~ModeEncoder() { if (arith) flush(); }

  void encode(int ctxMode, int ctxLevel, Mode real)
  {
    _encode<false>(ctxMode, ctxLevel, real);
  }

  void flush()
  {
    for (auto& val : buffer) {
      int ctxMode;
      int ctxLevel;
      Mode real;
      unpack(val, ctxMode, ctxLevel, real);
      _encode<true>(ctxMode, ctxLevel, real);
    }
    buffer.clear();
  }

  auto getEntropy(int ctxMode, int ctxLevel) -> std::array<double, Mode::size>&
  {
    assert(ctxMode >= 0 && ctxMode < NUMBER_OF_CONTEXT_MODE);
    assert(ctxLevel >= 0 && ctxLevel < NUMBER_OF_LEVELS_MODE);
    std::fill(
      entropy.begin(), entropy.end(), std::numeric_limits<double>::infinity());

    bool enableIntra = (ctxMode % 3) > 0;
    if (!enableInter && !enableIntra) {
      entropy[Mode::Null] = 0;
      return entropy;
    }

    double PnotNull = static_cast<double>(rdoModeIsNull[ctxMode]) / 65536.0;
    entropy[Mode::Null] = -std::log2(1.0 - PnotNull);
    if (!enableInter || !enableIntra) {
      if (enableInter) {
        entropy[Mode::Inter] = -std::log2(PnotNull);
      } else {
        entropy[Mode::Intra] = -std::log2(PnotNull);
      }
      return entropy;
    }

    double PnotIntra = static_cast<double>(rdoModeIsIntra[ctxMode]) / 65536.0;
    entropy[Mode::Intra] = -std::log2(PnotNull * (1.0 - PnotIntra));
    entropy[Mode::Inter] = -std::log2(PnotNull * PnotIntra);
    return entropy;
  }

  void update(double dist, double rate)
  {
    meanDist = meanDist * (1.0 - learnRate) + dist * learnRate;
    meanRate = meanRate * (1.0 - learnRate) + rate * learnRate;
    learnRate = learnRate * 0.98 + 0.001 * 0.02;
  }

  double getLambda(double rateWeight)
  {
    return rateWeight * meanDist / meanRate;
  }

  static uint16_t pack(int ctxMode, int ctxLevel, Mode real)
  {
    return uint16_t(real)
      + uint16_t(Mode::size)
      * (uint16_t(ctxLevel) + NUMBER_OF_LEVELS_MODE * uint16_t(ctxMode));
  }

  static void unpack(uint16_t val, int& ctxMode, int& ctxLevel, Mode& real)
  {
    real = Mode(val % uint16_t(Mode::size));
    val /= uint16_t(Mode::size);
    ctxLevel = val % NUMBER_OF_LEVELS_MODE;
    ctxMode = val / NUMBER_OF_LEVELS_MODE;
  }

private:
  template<bool writeOut>
  void _encode(int ctxMode, int ctxLevel, Mode real)
  {
    assert(ctxMode >= 0 && ctxMode < NUMBER_OF_CONTEXT_MODE);
    assert(ctxLevel >= 0 && ctxLevel < NUMBER_OF_LEVELS_MODE);
    bool enableIntra = (ctxMode % 3) > 0;
    if (!enableIntra && !enableInter) {
      assert(real == Mode::Null);
      return;
    }

    if (!writeOut)
      buffer.push_back(pack(ctxMode, ctxLevel, real));

    bool flag;
    flag = isNull(real);
    if (writeOut)
      arith->encode(flag, modeIsNull[ctxMode]);
    else
      _arith_encode(flag, rdoModeIsNull[ctxMode]);

    if (flag || !enableInter || !enableIntra)
      return;

    flag = isIntra(real);
    if (writeOut)
      arith->encode(flag, modeIsIntra[ctxMode]);
    else
      _arith_encode(flag, rdoModeIsIntra[ctxMode]);
  }

  void _arith_encode(
    bool flag, decltype(AdaptiveBitModel::probability)& probability)
  {
    if (flag)
      probability -= lut[probability >> 8];
    else
      probability += lut[255 - (probability >> 8)];
  }
};

class ModeDecoder : public ModeCoder {
  EntropyDecoder* arith;

public:
  ModeDecoder() : ModeCoder(), arith(nullptr) {}

  void set(EntropyDecoder* coder) { arith = coder; }

  Mode decode(int ctxMode, int ctxLevel)
  {
    assert(ctxMode >= 0 && ctxMode < NUMBER_OF_CONTEXT_MODE);
    assert(ctxLevel >= 0 && ctxLevel < NUMBER_OF_LEVELS_MODE);
    bool enableIntra = (ctxMode % 3) > 0;
    if (!enableIntra && !enableInter) {
      return Mode::Null;
    }

    bool flag;
    flag = arith->decode(modeIsNull[ctxMode]);
    if (flag)
      return Mode::Null;
    if (!enableInter)
      return Mode::Intra;
    if (!enableIntra)
      return Mode::Inter;

    flag = arith->decode(modeIsIntra[ctxMode]);
    if (flag)
      return Mode::Intra;
    return Mode::Inter;
  }
};
} // namespace attr
} /* namespace pcc */
