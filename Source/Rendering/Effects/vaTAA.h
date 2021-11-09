///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Lukasz, Migas (Lukasz.Migas@intel.com) - TAA code, Filip Strugar (filip.strugar@intel.com) - integration
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Core/vaUI.h"

#include "Rendering/vaRendering.h"

namespace Vanilla
{
    class vaTAA : public vaRenderingModule, public vaUIPanel
    {
    protected:
        bool                                        m_debugShowMotionVectors        = false;
        bool                                        m_paramShowNoHistoryPixels      = false;
        bool                                        m_debugDisableAAJitter          = false;

        vaVector2i                                  m_size;

        shared_ptr<vaTexture>                       m_debugImage;

        //shared_ptr<vaTexture>                       m_depth;
        shared_ptr<vaTexture>                       m_depthPrevious;
        shared_ptr<vaTexture>                       m_history;
        shared_ptr<vaTexture>                       m_historyPrevious;
        float                                       m_historyPreviousPreExposureMul = 1.0f;

        vaAutoRMI<vaComputeShader>                  m_CSTAA;
        vaAutoRMI<vaComputeShader>                  m_CSFinalApply;

        bool                                        m_paramUseFP16                  = false;
        bool                                        m_paramUseTGSM                  = false;
        bool                                        m_paramUseDepthThreshold        = true;
        bool                                        m_paramUseYCoCgSpace            = true;

        bool                                        m_shadersDirty                  = true;

        bool                                        m_resetHistory                  = true;

        shared_ptr<vaConstantBuffer>                m_constantBuffer;

        std::vector< pair< string, string > >       m_staticShaderMacros;

        // all of these updated by ComputeJitter and used in 'Apply'
        int64                                       m_previousFrameIndex            = -1;
        int64                                       m_currentFrameIndex             = -1;
        vaVector2                                   m_currentJitter                 = {0,0};
        vaVector2                                   m_previousJitter                = {0,0};

        float                                       m_globalMIPOffset               = -0.7f;
        float                                       m_lerpMul                       = 0.99f;    // for debugging
        float                                       m_lerpPow                       = 1.0f;     // for debugging
        vaVector2                                   m_varianceGammaMinMax           = { 0.75f, 6.0f };

    protected:
        VA_RENDERING_MODULE_MAKE_FRIENDS( );
        vaTAA( const vaRenderingModuleParams & params );

    public:
        virtual ~vaTAA( ) { }

    public:
        vaVector2                                   ComputeJitter( int64 frameIndex );
        vaVector2                                   GetCurrentJitter( ) const                           { return m_currentJitter; }
        float                                       GetGlobalMIPOffset( ) const                         { return m_globalMIPOffset; }
        vaDrawResultFlags                           Apply( vaRenderDeviceContext & renderContext, const vaCameraBase & cameraBase, const shared_ptr<vaTexture> & motionVectors, const shared_ptr<vaTexture> & viewspaceDepth, const shared_ptr<vaTexture> & inoutColor, const vaMatrix4x4 & reprojectionMatrix, const vaVector2 & cameraJitterDelta );

    public:
        bool                                        ShowDebugImage( )                                   { return false; }; //m_debugShowMotionVectors;  }
        const shared_ptr<vaTexture>                 DebugImage( )                                       { return m_debugImage;              }
        void                                        ResetHistory( )                                     { m_resetHistory = true; }

    private:

    private:
        virtual void                                UIPanelTick( vaApplicationBase & application ) override;

        bool                                        UpdateTexturesAndShaders( int width, int height );
        void                                        UpdateConstants( vaRenderDeviceContext & renderContext, const vaCameraBase & cameraBase, const vaMatrix4x4 & reprojectionMatrix, const vaVector2 & cameraJitterDelta );
    };

} // namespace Vanilla

