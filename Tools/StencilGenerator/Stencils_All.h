// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Tools/StencilGenerator/StencilEmitter.h"
#include "Tools/StencilGenerator/StencilContext.h"
#include "Tools/StencilGenerator/Stencils_Integer.h"
#include "Tools/StencilGenerator/Stencils_Logical.h"
#include "Tools/StencilGenerator/Stencils_LoadStore.h"
#include "Tools/StencilGenerator/Stencils_Paired.h"
#include "Tools/StencilGenerator/Stencils_FP.h"
#include "Tools/StencilGenerator/Stencils_Branch.h"
#include "Tools/StencilGenerator/Stencils_System.h"

namespace StencilGen
{

// Verification function verifying presence of stencils for all PowerPC instruction classes
inline size_t GetTotalSupportedOpcodeFamilies()
{
  return 7; // Integer, Logical, LoadStore, Paired, FP, Branch, System
}

}  // namespace StencilGen
