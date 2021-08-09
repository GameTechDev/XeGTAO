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
#include "Core/vaUI.h"

#include "Rendering/vaRendering.h"
#include "Rendering/vaShader.h"
#include "Rendering/vaRenderCamera.h"

#include "Rendering/vaTexture.h"

#include "Rendering/Effects/vaPostProcessBlur.h"

#include "Rendering/Shaders/vaSharedTypes.h"

#include "Rendering/Shaders/vaPostProcessShared.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
// at the moment, very simple Reinhard implementation 
// ("High Dynamic Range Imaging, Acquisition, Display, and Image - Based Lighting, 2nd Edition")
//
// for future, read:
//  - http://filmicgames.com/archives/75
//  - https://mynameismjp.wordpress.com/2010/04/30/a-closer-look-at-tone-mapping/
//  - http://gpuopen.com/optimized-reversible-tonemapper-for-resolve/
//  - https://developer.nvidia.com/preparing-real-hdr
//  - https://developer.nvidia.com/implementing-hdr-rise-tomb-raider
///////////////////////////////////////////////////////////////////////////////////////////////////

namespace Vanilla
{
    class vaPostProcessTonemap : public vaRenderingModule, public vaUIPanel
    {
    protected:
        VA_RENDERING_MODULE_MAKE_FRIENDS( );
        vaPostProcessTonemap( const vaRenderingModuleParams & params );

    public:

        struct AdditionalParams
        {
            bool                                    SkipCameraLuminanceUpdate           = false;
            bool                                    SkipTonemapper                      = false;        // skip tone mapping, just apply results directly
            std::shared_ptr<vaTexture>              OutExportLuma                       = nullptr;
        };

    protected:
        //TMSettings                                  m_settings;

        shared_ptr<vaTexture>                       m_avgLuminance1x1;
        shared_ptr<vaTexture>                       m_avgLuminanceScratch;

        shared_ptr<vaTexture>                       m_halfResRadiance;

        //float                                       m_lastAverageLuminance;

        shared_ptr<vaPostProcessBlur>               m_bloomBlur;

        vaAutoRMI<vaPixelShader>                    m_PSPassThrough;
        vaAutoRMI<vaPixelShader>                    m_PSTonemap;
        vaAutoRMI<vaPixelShader>                    m_PSTonemapWithLumaExport;
        vaAutoRMI<vaComputeShader>                  m_CSHalfResDownsampleAndAvgLum;
        vaAutoRMI<vaComputeShader>                  m_CSAvgLumHoriz;
        vaAutoRMI<vaComputeShader>                  m_CSAvgLumVert;
        vaAutoRMI<vaPixelShader>                    m_PSAddBloom;

        vaAutoRMI<vaComputeShader>                  m_CSDebugColorTest;

        bool                                        m_shadersDirty;

        PostProcessTonemapConstants                 m_lastShaderConsts;

        vaTypedConstantBufferWrapper< PostProcessTonemapConstants, true >
                                                    m_constantsBuffer;

        std::vector< pair< string, string > >       m_staticShaderMacros;

        bool                                        m_dbgGammaTest = false;
        bool                                        m_dbgColorTest = false;

    public:
        virtual ~vaPostProcessTonemap( );

    public:
        virtual vaDrawResultFlags                   TickAndApplyCameraPostProcess( vaRenderDeviceContext & renderContext, vaRenderCamera & renderCamera, const std::shared_ptr<vaTexture> & dstColor, const std::shared_ptr<vaTexture> & srcRadiance, const AdditionalParams & additionalParams = AdditionalParams() );

    public:
        //TMSettings &                                Settings( )                                 { return m_settings; }

    protected:
        // void                                        Tick( float deltaTime );
        // void                                        ResetHistory( );            // if determinism is required between changing of scenes or similar

    protected:
        virtual void                                UpdateConstants( vaRenderDeviceContext & renderContext, const vaRenderCamera & renderCamera, const std::shared_ptr<vaTexture> & srcRadiance );
        virtual void                                UpdateShaders( bool waitCompileShaders );

    protected:
        //virtual void                                UpdateLastAverageLuminance( vaRenderDeviceContext & renderContext, bool noDelayInGettingLuminance );

    protected:
        virtual void                                UIPanelTick( vaApplicationBase & application ) override;
    };

}
