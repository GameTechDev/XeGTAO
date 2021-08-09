///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Core/vaCoreIncludes.h"

#include "Rendering/vaRenderBuffers.h"

#include "Rendering/vaRendering.h"

#include "Rendering/vaTexture.h"

#include "Rendering/Shaders/vaSharedTypes.h"

#include "Rendering/Shaders/vaPostProcessShared.h"

#include "Rendering/vaShader.h"


//////////////////////////////////////////////////////////////////////////
// read:
// - https://community.arm.com/servlet/JiveServlet/download/96891546-19463/siggraph2015-mmg-marius-slides.pdf 
// (dual blur seems like an ideal solution, just check the procedure for fract kernels - probably needs 
// upgrading for that)
//
//////////////////////////////////////////////////////////////////////////

namespace Vanilla
{

    class vaPostProcessBlur : public vaRenderingModule
    {
    protected:
        int                                     m_texturesUpdatedCounter;

        vaResourceFormat                        m_textureFormat = vaResourceFormat::Unknown;
        vaVector2i                              m_textureSize;

        shared_ptr<vaTexture>                   m_fullresPingTexture;
        shared_ptr<vaTexture>                   m_fullresPongTexture;
        
        shared_ptr<vaTexture>                   m_lastScratchTexture;

        int                                     m_currentGaussKernelRadius;
        float                                   m_currentGaussKernelSigma;
        std::vector<float>                      m_currentGaussKernel;
        std::vector<float>                      m_currentGaussWeights;
        std::vector<float>                      m_currentGaussOffsets;

        vaTypedConstantBufferWrapper< PostProcessBlurConstants, true >
                                                m_constantsBuffer;
        bool                                    m_constantsBufferNeedsUpdate;

        vaAutoRMI< vaComputeShader >            m_CSGaussHorizontal;
        vaAutoRMI< vaComputeShader >            m_CSGaussVertical;

        // float                                   m_inputMaxLimit     = VA_FLOAT_HIGHEST;     // clamp on input so no value above is considered

    private:
        bool                                    m_shadersDirty = true;
        std::vector< pair< string, string > >   m_staticShaderMacros;

    protected:
        VA_RENDERING_MODULE_MAKE_FRIENDS( );
        vaPostProcessBlur( const vaRenderingModuleParams & params );

    public:
        virtual ~vaPostProcessBlur( );

    public:

        void                                    UpdateGPUConstants( vaRenderDeviceContext & renderContext, float factor0 );

        // for HDR images use gaussRadius that is at least 6 * ceil(gaussSigma); for LDR 3 * ceil(gaussSigma) is enough
        // if -1 is used, gaussRadius will be calculated as 6 * ceil(gaussSigma)
        // gaussRadius is the only factor that determines performance; 
        vaDrawResultFlags                       Blur( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & dstTexture, const shared_ptr<vaTexture> & srcTexture, float gaussSigma, int gaussRadius = -1 );

        // same as Blur except output goes into m_lastScratchTexture which remains valid until next call to Blur or BlurToScratch or device reset
        vaDrawResultFlags                       BlurToScratch( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & srcTexture, float gaussSigma, int gaussRadius = -1 );

        // output from last BlurToScratch or nullptr if invalid
        const shared_ptr<vaTexture> &           GetLastScratch( ) const                             { return m_lastScratchTexture; }

        // void                                    SetClampInput( float maxLimit = VA_FLOAT_HIGHEST )  { m_inputMaxLimit = maxLimit; }

    protected:
        void                                    UpdateTextures( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & srcTexture );
        bool                                    UpdateKernel( float gaussSigma, int gaussRadius );

    private:
        void                                    UpdateFastKernelWeightsAndOffsets( );

    public:
        void                                    UpdateShaders( vaRenderDeviceContext & renderContext );

    protected:
        virtual vaDrawResultFlags               BlurInternal( vaRenderDeviceContext& renderContext, const shared_ptr<vaTexture> & dstTexture, const shared_ptr<vaTexture> & srcTexture );
    };

}
