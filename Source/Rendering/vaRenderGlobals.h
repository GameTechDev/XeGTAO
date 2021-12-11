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
#include "Rendering/vaRenderDevice.h"

namespace Vanilla
{
    class vaTexture;

    // Manages global shader constants and buffers that are not changed 'too frequently' (not per draw call but usually more than once per frame)
    // These are usually (but not necessarily) shared between all draw items between vaRenderDeviceContext::BeginItems/EndItems
    // Also handles some global UI stuff like info about what's under the mouse cursor.
    // Also handles some shader debugging stuff.
    class vaRenderGlobals : public vaRenderingModule, public vaUIPanel
    {
    public:
        struct GenericDataCapture
        {
            bool                        HasData     = false;
            uint32                      NumRows     = 0;
            uint32                      NumColumns  = SHADERGLOBAL_GENERICDATACAPTURE_COLUMNS;
            float                       Data[SHADERGLOBAL_GENERICDATACAPTURE_ROWS][SHADERGLOBAL_GENERICDATACAPTURE_COLUMNS];

            void                        Reset( )    { HasData = false; NumRows = 0; }
        };

    private:
        shared_ptr<void> const              m_aliveToken = std::make_shared<int>( 42 );    // this is only used to track object lifetime for callbacks and etc.

    protected:
        shared_ptr<vaTexture>               m_genericDataCaptureGPUTextures[vaRenderDevice::c_BackbufferCount];
        shared_ptr<vaTexture>               m_genericDataCaptureCPUTextures[vaRenderDevice::c_BackbufferCount];
        bool                                m_genericDataCaptureCPUTexturesHasData[vaRenderDevice::c_BackbufferCount];
        int64                               m_genericDataCaptureLastResolveFrameIndex = -1;
        int64                               m_genericDataCaptureStarted    = -1;
        int64                               m_genericDataCaptureFinalized  = -1;
        GenericDataCapture                  m_genericDataCaptured;


        shared_ptr<vaRenderBuffer>          m_shaderFeedbackStaticGPU[vaRenderDevice::c_BackbufferCount];
        shared_ptr<vaRenderBuffer>          m_shaderFeedbackStaticCPU[vaRenderDevice::c_BackbufferCount];
        bool                                m_shaderFeedbackStaticCPUHasData[vaRenderDevice::c_BackbufferCount];
        shared_ptr<vaRenderBuffer>          m_shaderFeedbackDynamicGPU[vaRenderDevice::c_BackbufferCount];
        shared_ptr<vaRenderBuffer>          m_shaderFeedbackDynamicCPU[vaRenderDevice::c_BackbufferCount];
        int64                               m_shaderFeedbackStartedFrame    = -1;
        int64                               m_shaderFeedbackProcessedFrame  = -1;
        ShaderFeedbackStatic                m_shaderFeedbackLastCapture     = {};

        std::vector<CursorHoverInfo>        m_cursorHoverInfoItems;

        shared_ptr<vaConstantBuffer>        m_constantBuffer;
        bool                                m_debugDrawDepth;
        bool                                m_debugDrawNormalsFromDepth;

        shared_ptr<vaPixelShader>           m_debugDrawDepthPS;
        shared_ptr<vaPixelShader>           m_debugDrawNormalsFromDepthPS;
        
        //vaAutoRMI<vaComputeShader>          m_genericDataCaptureCS;
        //vaAutoRMI<vaComputeShader>          m_cursorCaptureCS;

        // get context info for middle-mouse clicks for entities, meshes and assets; make sure to enable SetCursorHoverInfoEnabled on the main render view
        bool                                m_enableContextMenu             = true;

        std::vector<ShaderFeedbackDynamic>  m_debugDrawItems;
        bool                                m_freezeDebugDrawItems          = false;

        std::weak_ptr<class vaScene>        m_uiLastScene;

    protected:
        VA_RENDERING_MODULE_MAKE_FRIENDS( );
        vaRenderGlobals( const vaRenderingModuleParams & params );

    public:
        ~vaRenderGlobals( );

    public:
        const GenericDataCapture &          GetLastGenericDataCaptured( ) const                                                             { return m_genericDataCaptured; }

        // x, y, z are worldspace positions of the pixel depth value that was under the cursor when Update3DCursor was called; w is the raw depth data
        const std::vector<CursorHoverInfo>& GetCursorHoverInfo( )  const                                                                    { return m_cursorHoverInfoItems; }

        void                                UpdateAndSetToGlobals( vaRenderDeviceContext & renderContext, vaShaderItemGlobals & shaderItemGlobals, const vaDrawAttributes * drawAttributes );

    protected:
        void                                UpdateShaderConstants( vaRenderDeviceContext & renderContext, const vaDrawAttributes * drawAttributes );

        virtual void                        DigestGenericDataCapture( vaRenderDeviceContext & renderContext );
        virtual void                        ProcessShaderFeedback( vaRenderDeviceContext & renderContext );

        void                                DigestShaderFeedbackInfo( ShaderFeedbackDynamic dynamicItems[], int dynamicItemCount );

    protected:
        //virtual string                      UIPanelGetDisplayName( ) const override                                                     { return "RenderingGlobals"; }
        virtual void                        UIPanelTick( vaApplicationBase & application ) override;
        virtual void                        UIPanelTickAlways( vaApplicationBase & application ) override;   // (optional) - will get called even when panel is not visible or not an active tab - useful if a tool needs to respond to special keys or something
        
    public:
        void                                UIMenuHandler( vaApplicationBase & application );
    };

}

