///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
// This is a slightly updated and simplified ASSAO implementation based on the original code at
// https://github.com/GameTechDev/ASSAO. Some of the important changes are:
//  * moved to compute shaders
//  * moved to platform-independent layer
//  * removed lowest (half res) quality codepath 
//  * removed adaptive (highest) quality codepath due to complexity and replaced it by High-equivalent with more taps
//    (this slightly hurts performance vs before but the codebase is a lot easier to maintain and upgrade)
//  * re-enabled RadiusDistanceScalingFunction as some users like it
//  * platform independent implementation only (going through vaGraphicsItem path) but since this is compute only
//    implementation, it should be easy to port anyway.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Rendering/Shaders/vaShaderCore.h"

#include "Rendering/vaRendering.h"

#include "Rendering/vaShader.h"
#include "Rendering/vaRenderBuffers.h"

#include "Rendering/vaTexture.h"

#include "Core/vaUI.h"

#include "IntegratedExternals/vaImguiIntegration.h"
#include "Rendering\Shaders\vaASSAOLite_types.h"

namespace Vanilla
{
    class vaASSAOLite : public vaRenderingModule, public vaUIPanel
    {
    protected:
        string                                      m_debugInfo;
		mutable bool                                m_enableMLSSAO;
		mutable bool                                m_debugShowNormals;
		mutable bool                                m_debugShowEdges;

        vaResourceFormat                            m_depthViewspaceFormat          = vaResourceFormat::R16_FLOAT;       // increase to R32_FLOAT if using very low FOVs (for ex, for sniper scope effect) or similar, or in case you suspect artifacts caused by lack of precision; performance will degrade slightly

        vaVector2i                                  m_size;
        vaVector2i                                  m_halfSize;

        shared_ptr<vaTexture>                       m_workingDepthsAll;
        shared_ptr<vaTexture>                       m_workingDepthsMipViews[ASSAO_DEPTH_MIP_LEVELS];
        shared_ptr<vaTexture>                       m_pingPongWorkingA;
        shared_ptr<vaTexture>                       m_pingPongWorkingB;
        shared_ptr<vaTexture>                       m_normals;

        shared_ptr<vaTexture>                       m_debugImage;

        mutable ASSAO::ASSAOSettings                m_settings;

        // MSAA versions include 1-sample for non-MSAA
        vaAutoRMI<vaComputeShader>                  m_CSPrepareDepthsAndNormals;
        vaAutoRMI<vaComputeShader>                  m_CSGenerate[3];
        vaAutoRMI<vaComputeShader>                  m_CSSmartBlur;
        vaAutoRMI<vaComputeShader>                  m_CSApply;

        bool                                        m_shadersDirty                  = true;

        shared_ptr<vaConstantBuffer>                m_constantBuffer;

        std::vector< pair< string, string > >       m_staticShaderMacros;
        pair< string, string >                      m_specialShaderMacro;

    protected:
        VA_RENDERING_MODULE_MAKE_FRIENDS( );
        vaASSAOLite( const vaRenderingModuleParams & params );

    public:
        virtual ~vaASSAOLite( ) { }

    public:
        vaDrawResultFlags                           Compute( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & outputAO, const vaMatrix4x4 & viewMatrix, const vaMatrix4x4 & projMatrix, const shared_ptr<vaTexture> & inputDepth, const shared_ptr<vaTexture> & inputNormals = nullptr );

    public:
        ASSAO::ASSAOSettings &                      Settings( )                                         { return m_settings; }

        // used for debugging & optimization tests - just sets a single shader macro for all shaders (and triggers a shader recompile)
        void                                        SetSpecialShaderMacro( const pair< string, string > & ssm ) { m_specialShaderMacro = ssm; }

        bool &                                      DebugShowNormals( )                                 { return m_debugShowNormals;        }
        bool &                                      DebugShowEdges( )                                   { return m_debugShowEdges;          }
        const shared_ptr<vaTexture>                 DebugImage( )                                       { return m_debugImage;              }

    public:
        virtual void                                UIPanelTick( vaApplicationBase & application ) override;

    private:
        bool                                        ReCreateIfNeeded( shared_ptr<vaTexture> & inoutTex, vaVector2i size, vaResourceFormat format, float & inoutTotalSizeSum, int mipLevels, int arraySize );
        void                                        UpdateWorkingTextures( int width, int height, bool generateNormals );
        void                                        UpdateConstants( vaRenderDeviceContext & renderContext, const vaMatrix4x4 & viewMatrix, const vaMatrix4x4 & projMatrix );
    };

} // namespace Vanilla

