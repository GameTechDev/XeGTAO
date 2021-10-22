///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// A lot of the stuff here is based on: https://github.com/google/filament (which is awesome)
//
// Filament is the best compact opensource PBR renderer (that I could find), including excellent documentation and 
// performance. The size and complexity of the Filament library itself is a bit out of scope of what Vanilla is 
// intended to provide, and Vanilla's shift to path-tracing means Filament isn't used directly but a lot of its
// shaders and structures are inherited.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// this is where the entry points and all the glue is

#define VA_FILAMENT_SPECGLOSS
#include "vaStandardPBR.hlsl"