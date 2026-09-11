// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/PowerPC/StencilInterpreter/StencilInterpreterBlockCache.h"

#include "Core/PowerPC/CachedInterpreter/CachedInterpreterEmitter.h"
#include "Core/PowerPC/JitCommon/JitBase.h"

StencilInterpreterBlockCache::StencilInterpreterBlockCache(JitBase& jit) : JitBaseBlockCache{jit}
{
}

void StencilInterpreterBlockCache::Init()
{
  JitBaseBlockCache::Init();
  ClearRangesToFree();
}

void StencilInterpreterBlockCache::DestroyBlock(JitBlock& block)
{
  JitBaseBlockCache::DestroyBlock(block);

  if (block.near_begin != block.near_end)
    m_ranges_to_free_on_next_codegen.emplace_back(block.near_begin, block.near_end);
}

void StencilInterpreterBlockCache::ClearRangesToFree()
{
  m_ranges_to_free_on_next_codegen.clear();
}

#include <cstring>
#include "Core/PowerPC/CachedInterpreter/CachedInterpreterEmitter.h"
#include "Core/PowerPC/StencilInterpreter/StencilInterpreter.h"

void StencilInterpreterBlockCache::WriteLinkBlock(const JitBlock::LinkData& source,
                                                  const JitBlock* dest)
{
  if (!source.exitPtrs)
    return;

#ifdef _M_ARM_64
  if (source.exitFarcode != nullptr)
  {
    // Native Tape Link: resolve target first_stencil and tape cursor
    StencilInterpreter::TapeTargetLink link{nullptr, nullptr};
    if (dest && dest->normalEntry)
    {
      const auto cb = *reinterpret_cast<const CachedInterpreterEmitter::AnyCallback*>(dest->normalEntry);
      if (cb == CachedInterpreterEmitter::AnyCallbackCast(StencilInterpreter::ExecuteTapeRun<true>) ||
          cb == CachedInterpreterEmitter::AnyCallbackCast(StencilInterpreter::ExecuteTapeRun<false>))
      {
        const auto* header = reinterpret_cast<const StencilInterpreter::TapeRunOperands*>(
            dest->normalEntry + sizeof(CachedInterpreterEmitter::AnyCallback));
        link.first_stencil = header->first_stencil;
        link.tape_cursor = dest->normalEntry + sizeof(CachedInterpreterEmitter::AnyCallback) + sizeof(StencilInterpreter::TapeRunOperands);
      }
    }
    std::memcpy(source.exitPtrs, &link, sizeof(link));
    return;
  }
#endif

  const u8* target = dest ? dest->normalEntry : nullptr;
  std::memcpy(source.exitPtrs, &target, sizeof(target));
}

void StencilInterpreterBlockCache::WriteDestroyBlock(const JitBlock& block)
{
  CachedInterpreterEmitter emitter(block.normalEntry, block.near_end);
  emitter.Write(CachedInterpreterEmitter::PoisonCallback);
}
