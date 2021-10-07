///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Note, this sorter relies on AMD FidelityFX Parallel Sort, please refer to FFX_ParallelSort.h for details

#pragma once

#include "Rendering/vaRendering.h"

namespace Vanilla
{
    class vaGPUSort : public vaRenderingModule//, public vaUIPanel
    {
    protected:

        shared_ptr<vaConstantBuffer>                m_constantBuffer;

        shared_ptr<vaComputeShader>                 m_FPS_Count                     = nullptr;
        shared_ptr<vaComputeShader>                 m_FPS_CountReduce               = nullptr;
        shared_ptr<vaComputeShader>                 m_FPS_ScanPrefix                = nullptr;
        shared_ptr<vaComputeShader>                 m_FPS_ScanAdd                   = nullptr;
        shared_ptr<vaComputeShader>                 m_FPS_Scatter                   = nullptr;

        shared_ptr<vaRenderBuffer>                  m_scratchBuffer;
        shared_ptr<vaRenderBuffer>                  m_scratchKeysBuffer;
        shared_ptr<vaRenderBuffer>                  m_scratchIndicesBuffer;
        shared_ptr<vaRenderBuffer>                  m_reducedScratchBuffer;

    protected:
        VA_RENDERING_MODULE_MAKE_FRIENDS( );
        vaGPUSort( const vaRenderingModuleParams & params );

    public:
        virtual ~vaGPUSort( );

    public:
        vaDrawResultFlags                           Sort( vaRenderDeviceContext & renderContext, const shared_ptr<vaRenderBuffer> & inOutKeys, const shared_ptr<vaRenderBuffer> & outSortedIndices, const uint32 keyCount, const uint32 maxKeyValue = 0xFFFFFFFF );

    };

} // namespace Vanilla

