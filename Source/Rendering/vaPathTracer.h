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

#include "Rendering/vaRendering.h"

#include "Core/vaUI.h"

namespace Vanilla
{
    class vaSkybox;
    class vaRenderMaterial;
    class vaRenderMesh;
    class vaPixelShader;
    class vaGPUSort;

    class vaPathTracer : public vaRenderingModule//, public vaUIPanel
    {
    protected:

        shared_ptr<vaConstantBuffer>                m_constantBuffer;

        shared_ptr<vaShaderLibrary>                 m_shaderLibrary                 = nullptr;
        shared_ptr<vaPixelShader>                   m_PSWriteToOutput               = nullptr;

        shared_ptr<vaComputeShader>                 m_CSKickoff                     = nullptr;
        //shared_ptr<vaComputeShader>                 m_CSTickCounters                = nullptr;

        shared_ptr<vaTexture>                       m_radianceAccumulation;
        shared_ptr<vaTexture>                       m_viewspaceDepth;

        shared_ptr<vaRenderBuffer>                  m_pathTracerControl;
        shared_ptr<vaRenderBuffer>                  m_pathPayloads;
        shared_ptr<vaRenderBuffer>                  m_pathGeometryHitInfoPayloads;
        //shared_ptr<vaRenderBuffer>                  m_pathListUnsorted;             // path indices, 0...pathCount
        shared_ptr<vaRenderBuffer>                  m_pathListSorted;               // path indices, sorted
        shared_ptr<vaRenderBuffer>                  m_pathSortKeys;
        //shared_ptr<vaRenderBuffer>                  m_pathSortKeysSorted;

        shared_ptr<vaGPUSort>                       m_GPUSort;

        // these manage frame sample accumulation and track changes that require restarting accumulation
        vaCameraBase                                m_accumLastCamera;          
        int64                                       m_accumLastShadersID            = -1;
        int                                         m_accumFrameTargetCount         = 256; //32768;     // stop at this number of samples per pixel
        int                                         m_accumFrameCount               = 0;        // current number of samples per pixel
        int                                         m_maxBounces                    = 4;
        constexpr static int                        c_maxBounceUpperBound           = 16;

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

