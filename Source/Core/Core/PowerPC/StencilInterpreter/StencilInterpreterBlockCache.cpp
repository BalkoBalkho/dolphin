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

void StencilInterpreterBlockCache::WriteLinkBlock(const JitBlock::LinkData& source,
                                                  const JitBlock* dest)
{
}

void StencilInterpreterBlockCache::WriteDestroyBlock(const JitBlock& block)
{
  CachedInterpreterEmitter emitter(block.normalEntry, block.near_end);
  emitter.Write(CachedInterpreterEmitter::PoisonCallback);
}
