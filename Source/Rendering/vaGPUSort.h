///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Note, this sorter relies on AMD FidelityFX Parallel Sort, but is modified for Vanilla use cases, where 'keys' are
// required to remain unmodified, while the output is sorted indices. This is the same as using the original with the
// provided index buffer as a payload, except this version will initialize the index buffer itself, and will not
// modify keys (required for ability to partially modify keys and resort).

#pragma once

#include "Rendering/vaRendering.h"

namespace Vanilla
{
    class vaGPUSort : public vaRenderingModule//, public vaUIPanel
    {
    protected:

        //shared_ptr<vaConstantBuffer>                m_constantBuffer;
        shared_ptr<vaRenderBuffer>                  m_constantBuffer;
        shared_ptr<vaRenderBuffer>                  m_dispatchIndirectBuffer;

        shared_ptr<vaComputeShader>                 m_CSSetupIndirect           = nullptr;
        shared_ptr<vaComputeShader>                 m_CSCount                   = nullptr;
        shared_ptr<vaComputeShader>                 m_CSCountReduce             = nullptr;
        shared_ptr<vaComputeShader>                 m_CSScanPrefix              = nullptr;
        shared_ptr<vaComputeShader>                 m_CSScanAdd                 = nullptr;
        shared_ptr<vaComputeShader>                 m_CSScatter                 = nullptr;
        shared_ptr<vaComputeShader>                 m_CSFirstPassCount          = nullptr;  // first pass will initialize indices
        shared_ptr<vaComputeShader>                 m_CSFirstPassScatter        = nullptr;  // first pass will initialize indices

        shared_ptr<vaRenderBuffer>                  m_scratchBuffer;
        shared_ptr<vaRenderBuffer>                  m_scratchIndicesBuffer;
        shared_ptr<vaRenderBuffer>                  m_reducedScratchBuffer;

    protected:
        VA_RENDERING_MODULE_MAKE_FRIENDS( );
        vaGPUSort( const vaRenderingModuleParams & params );

    public:
        virtual ~vaGPUSort( );

    public:
        vaDrawResultFlags                           Sort( vaRenderDeviceContext & renderContext, const shared_ptr<vaRenderBuffer> & keys, const shared_ptr<vaRenderBuffer> & sortedIndices, bool resetIndices, const uint32 keyCount, const uint32 maxKeyValue = 0xFFFFFFFF, const shared_ptr<vaRenderBuffer> & actualNumberOfKeysToSort = nullptr, const uint32 actualNumberOfKeysToSortByteOffset = 0 );

    };

} // namespace Vanilla

