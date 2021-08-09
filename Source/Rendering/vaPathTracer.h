///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
// ONGOING WORK
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Rendering/vaRendering.h"

#include "Rendering/vaShader.h"
#include "Rendering/vaRenderBuffers.h"
#include "Rendering/vaTexture.h"

#include "Core/vaUI.h"

#include "IntegratedExternals/vaImguiIntegration.h"

#include "Rendering/Shaders/vaRaytracingShared.h"
#include "Rendering/Shaders/vaPathTracerShared.h"

namespace Vanilla
{
    class vaSkybox;
    class vaRenderMaterial;
    class vaRenderMesh;
    class vaPixelShader;

    class vaPathTracer : public vaRenderingModule//, public vaUIPanel
    {
    protected:

        vaTypedConstantBufferWrapper< ShaderPathTracerConstants, true >
                                                    m_constantBuffer;

        shared_ptr<vaShaderLibrary>                 m_shaderLibrary                 = nullptr;
        shared_ptr<vaPixelShader>                   m_writeToOutputPS               = nullptr;

        shared_ptr<vaTexture>                       m_radianceAccumulation;
        shared_ptr<vaTexture>                       m_viewspaceDepth;

        // these manage frame sample accumulation and track changes that require restarting accumulation
        vaCameraBase                                m_accumLastCamera;          
        int64                                       m_accumLastShadersID            = -1;
        int                                         m_accumFrameTargetCount         = 128; //32768;     // stop at this number of samples per pixel
        int                                         m_accumFrameCount               = 0;        // current number of samples per pixel
        int                                         m_maxBounces                    = 2;

        bool                                        m_enableAA                      = true;

        bool                                        m_debugPathUnderCursor          = false;
        int                                         m_debugPathVizDim               = 1;
        bool                                        m_debugLightsForPathUnderCursor = false;
        //bool                                        m_debugCrosshairUnderCursor = false;

        ShaderDebugViewType                         m_debugViz                      = ShaderDebugViewType::None;

        float                                       m_debugForcedDispatchDivergence = 0.0f;

        bool                                        m_enablePathRegularization      = true;

        shared_ptr<vaRenderMesh>                    m_paintingsMesh;
        shared_ptr<vaRenderMaterial>                m_divergenceMirrorMaterial;
        shared_ptr<vaRenderMaterial>                m_paintingsMaterialBackup;
        bool                                        m_divergenceMirrorTestEnabled   = false;
        bool                                        m_divergenceMirrorCameraSetPos  = false;
        float                                       m_divergenceMirrorRoughness     = 0.0f;

        int                                         m_replaceAllBistroMaterials     = 0;    // 0 - not active; 1 - "are you sure"? 2 - replaced

    protected:
        VA_RENDERING_MODULE_MAKE_FRIENDS( );
        vaPathTracer( const vaRenderingModuleParams & params );

    public:
        virtual ~vaPathTracer( );

    public:
        vaDrawResultFlags                           Draw( vaRenderDeviceContext & renderContext, vaDrawAttributes & drawAttributes, const shared_ptr<vaSkybox> & skybox, const shared_ptr<vaTexture> & outputColor, const shared_ptr<vaTexture> & outputDepth );

        ShaderDebugViewType &                       DebugViz( )                                             { return m_debugViz; }

        void                                        UITick( vaApplicationBase & application ); // override;

        shared_ptr<std::pair<vaVector3, vaVector3>> GetNextCameraRequest( )                                 { if( !m_divergenceMirrorCameraSetPos ) return nullptr; m_divergenceMirrorCameraSetPos = false; return std::make_shared<std::pair<vaVector3, vaVector3>>( vaVector3( 10.40f, 0.78f, 2.37f ), vaVector3( 0.961f, 0.278f, -0.007f ) ); }

    private:
        void                                        UpdateDivergenceMirrorExperiment();
    };

} // namespace Vanilla

