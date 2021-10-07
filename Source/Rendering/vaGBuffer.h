///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if 0 // obsolete, left as placeholder

#pragma once

#include "Core/vaCoreIncludes.h"
#include "Core/vaUI.h"

#include "vaRendering.h"

#include "Rendering/vaShader.h"

#include "vaTexture.h"

namespace Vanilla
{
    // A helper for GBuffer rendering - does creation and updating of render target textures and provides debug views of them.
    class vaGBuffer : public vaRenderingModule, public vaUIPanel
    {
    public:

        // to disable a texture, use vaResourceFormat::Unknown
        struct BufferFormats
        {
            vaResourceFormat        DepthBuffer                     = vaResourceFormat::D32_FLOAT;
            vaResourceFormat        DepthBufferViewspaceLinear      = vaResourceFormat::R32_FLOAT;              // could be R16 for lower precision preset
            vaResourceFormat        Albedo                          = vaResourceFormat::R8G8B8A8_UNORM_SRGB;
            vaResourceFormat        NormalMap                       = vaResourceFormat::R8G8B8A8_UNORM;         // improve encoding, drop to R8G8?
            vaResourceFormat        Radiance                        = vaResourceFormat::R16G16B16A16_FLOAT;     // could be R11G11B10_FLOAT for lower precision preset
            vaResourceFormat        OutputColorView                 = vaResourceFormat::R8G8B8A8_UNORM_SRGB;
            vaResourceFormat        OutputColorIgnoreSRGBConvView   = vaResourceFormat::R8G8B8A8_UNORM;         // for HDR displays use something higher
            vaResourceFormat        OutputColorTypeless             = vaResourceFormat::R8G8B8A8_TYPELESS;
            vaResourceFormat        OutputColorR32UINT_UAV          = vaResourceFormat::R32_UINT;

            bool                    RadianceEnableUAV               = false;

            bool operator ==( const BufferFormats & other ) const { return memcmp(this, &other, sizeof(*this)) == 0; }
            bool operator !=( const BufferFormats & other ) const { return !(*this==other); }
        };

    protected:
        string                                          m_debugInfo;
        mutable int                                     m_debugSelectedTexture;

        BufferFormats                                   m_formats;

        shared_ptr<vaTexture>                           m_depthBuffer;                      // just a regular depth buffer
        shared_ptr<vaTexture>                           m_depthBufferViewspaceLinear;       // regular depth buffer converted to viewspace 
        shared_ptr<vaTexture>                           m_normalMap;                        // screen space normal map
        shared_ptr<vaTexture>                           m_albedo;                           // material color plus whatever else
        shared_ptr<vaTexture>                           m_radiance;                         // a.k.a. light accumulation, a.k.a. screen color - final lighting output goes here as well as emissive materials
        shared_ptr<vaTexture>                           m_outputColorTypeless;              // final output color
        shared_ptr<vaTexture>                           m_outputColorView;                  // final output color, standard view
        shared_ptr<vaTexture>                           m_outputColorIgnoreSRGBConvView;    // final output color, view used to ignore color space conversions (if resource data is in _SRGB, you'll get _SRGB in the shader - in some cases you want that, and in some cases you need it for compatibility such as on HW that doesn't support _SRGB UAV stores)
        shared_ptr<vaTexture>                           m_outputColorR32UINT_UAV;           // final output color (R32_UINT from typeless if possible)

        // Light Pre-Pass
        // shared_ptr<vaTexture>                           m_diffuseIrradiance;        // placeholder for Light Pre-Pass
        // shared_ptr<vaTexture>                           m_specularIrradiance;       // placeholder for Light Pre-Pass

        vaVector2i                                      m_resolution;
        int                                             m_sampleCount;
        bool                                            m_deferredEnabled;

        //vaAutoRMI<vaPixelShader>                        m_pixelShader;
        vaAutoRMI<vaPixelShader>                        m_debugDrawDepthPS;
        vaAutoRMI<vaPixelShader>                        m_debugDrawDepthViewspaceLinearPS;
        vaAutoRMI<vaPixelShader>                        m_debugDrawNormalMapPS;
        vaAutoRMI<vaPixelShader>                        m_debugDrawAlbedoPS;
        vaAutoRMI<vaPixelShader>                        m_debugDrawRadiancePS;
        bool                                            m_shadersDirty;

//        vaDirectXConstantBuffer< GBufferConstants >
//                                                m_constantBuffer;

        vector< pair< string, string > >                m_staticShaderMacros;

        wstring                                         m_shaderFileToUse;


    protected:
        vaGBuffer( const vaRenderingModuleParams & params );
    public:
        virtual ~vaGBuffer( );

    private:
        friend class vaGBufferDX11;

    public:

        const shared_ptr<vaTexture> &                   GetDepthBuffer( ) const                 { return m_depthBuffer;              ; } 
        const shared_ptr<vaTexture> &                   GetDepthBufferViewspaceLinear( ) const  { return m_depthBufferViewspaceLinear; } 
        const shared_ptr<vaTexture> &                   GetNormalMap( ) const                   { return m_normalMap;                ; } 
        const shared_ptr<vaTexture> &                   GetAlbedo( ) const                      { return m_albedo;                   ; } 
        const shared_ptr<vaTexture> &                   GetRadiance( ) const                    { return m_radiance;                 ; } 
        const shared_ptr<vaTexture> &                   GetOutputColor( ) const                 { return m_outputColorView;      ; } 
        const shared_ptr<vaTexture> &                   GetOutputColorIgnoreSRGBView( ) const   { return m_outputColorIgnoreSRGBConvView;    ; } 
        const shared_ptr<vaTexture> &                   GetOutputColorR32UINT_UAV( ) const      { return m_outputColorR32UINT_UAV;   ; } 

        const BufferFormats &                           GetFormats( ) const                     { return m_formats; }
        void                                            SetFormats( const BufferFormats & formats ) { m_formats = formats; }
        const vaVector2i &                              GetResolution( ) const                  { return m_resolution; }
        int                                             GetSampleCount( ) const                 { return m_sampleCount; }
        bool                                            GetDeferredEnabled( ) const             { return m_deferredEnabled; }

    public:
        void                                            UpdateResources( int width, int height, int msaaSampleCount = 1, bool enableDeferred = false );

        virtual void                                    RenderDebugDraw( vaDrawAttributes & drawAttributes );

        // // draws provided depthTexture (can be the one obtained using GetDepthBuffer( )) into currently selected RT; relies on settings set in vaRenderGlobals and will assert and return without doing anything if those are not present
        // virtual void                                    DepthToViewspaceLinear( vaDrawAttributes & drawAttributes, vaTexture & depthTexture );

    private:
        virtual string                                  UIPanelGetDisplayName( ) const { return m_debugInfo; }
        virtual void                                    UIPanelTick( vaApplicationBase & application ) override;

    protected:
        void                                            UpdateShaders( );
        bool                                            ReCreateIfNeeded( shared_ptr<vaTexture> & inoutTex, int width, int height, vaResourceFormat format, bool needsUAV, int msaaSampleCount, float & inoutTotalSizeSum, vaResourceFormat fastClearFormat );
    };

}

#endif // #if 0 // obsolete, left as placeholder