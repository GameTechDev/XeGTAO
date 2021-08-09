///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Trapper Mcferron (trapper.mcferron@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Core/vaCoreIncludes.h"
#include "Core/vaUI.h"

#include "Core/Misc/vaResourceFormats.h"
#include "Rendering/vaRendering.h"
#include "Rendering/vaTexture.h"
#include "Rendering/vaShader.h"
#include "Rendering/vaRenderBuffers.h"

#include "Rendering/Shaders/vaDepthOfField.hlsl"

namespace Vanilla
{
    class vaDepthOfField : public Vanilla::vaRenderingModule, public vaUIPanel
    {
    public:
        struct DoFSettings
        {
            float   InFocusFrom         = 1;        // a.k.a. DoF near
            float   InFocusTo           = 2;        // a.k.a. DoF far
            float   NearTransitionRange = 0.5f;     // (InFocusFrom-NearTransitionRange) is when it's at top blur, gradually reducing until InFocusFrom
            float   FarTransitionRange  = 2.0f;     // (InFocusTo+FarTransitionRange) is when it's at top blur, gradually incrasing from InFocusTo
            float   NearBlurSize        = 6.0f;
            float   FarBlurSize         = 6.0f;
            // float   Vrs2x2Distance      = 1 + 2 + 2; //focal distance + focal region + far transition
            // float   Vrs4x4Distance      = (1 + 2 + 2) * 4; //(focal distance + focal region + far transition) * 4

            DoFSettings( ) { }

            void Serialize( vaXMLSerializer & serializer )
            {
                serializer.Serialize<float>( "InFocusFrom"          , InFocusFrom           , InFocusFrom           );
                serializer.Serialize<float>( "InFocusTo"            , InFocusTo             , InFocusTo             );
                serializer.Serialize<float>( "NearBlurSize"         , NearBlurSize          , NearBlurSize          );
                serializer.Serialize<float>( "FarBlurSize"          , FarBlurSize           , FarBlurSize           );
                serializer.Serialize<float>( "NearTransitionRange"  , NearTransitionRange   , NearTransitionRange   );
                serializer.Serialize<float>( "FarTransitionRange"   , FarTransitionRange    , FarTransitionRange    );
                //Vrs2x2Distance = FocalLength + FocalRegion + FarTransition;
                //Vrs4x4Distance = Vrs2x2Distance * 4;

                // this here is just to remind you to update serialization when changing the struct
                size_t dbgSizeOfThis = sizeof( *this ); dbgSizeOfThis;
                assert( dbgSizeOfThis == 24 );
            }
        };

    protected:
        DoFSettings                 m_settings;

        vaTypedConstantBufferWrapper<DepthOfFieldShaderConstants> m_constantsBuffer;
        vaAutoRMI<vaComputeShader>  m_CSResolve;
        vaAutoRMI<vaComputeShader>  m_CSSplitPlanes;
        vaAutoRMI<vaComputeShader>  m_CSFarBlur[3];   // bokeh, gauss horiz, gauss vert
        vaAutoRMI<vaComputeShader>  m_CSNearBlur[3];  // bokeh, gauss horiz, gauss vert

        //shared_ptr<vaTexture>       m_offscreenColorBlurA;
        //shared_ptr<vaTexture>       m_offscreenColorBlurB;
        shared_ptr<vaTexture>       m_offscreenColorNearA;
        shared_ptr<vaTexture>       m_offscreenColorNearB;
        shared_ptr<vaTexture>       m_offscreenColorFarA;
        shared_ptr<vaTexture>       m_offscreenColorFarB;
        shared_ptr<vaTexture>       m_offscreenCoc;

    public:
        vaDepthOfField( const vaRenderingModuleParams & params );
        ~vaDepthOfField( );

    public:
        DoFSettings &               Settings( )                                                             { return m_settings; }

        // sceneContext needed for NDCToViewDepth to work - could be split out and made part of the constant buffer here
        virtual vaDrawResultFlags   Draw( vaRenderDeviceContext & renderContext, const vaDrawAttributes & drawAttributes, const shared_ptr<vaTexture>& inDepth, const shared_ptr<vaTexture>& inOutColor, const shared_ptr<vaTexture>& outColorNoSRGB );

        float                       ComputeConservativeBlurFactor( const vaCameraBase & camera, const vaOrientedBoundingBox & obbWorldSpace );

    protected:
        virtual void                UpdateConstants( vaRenderDeviceContext & renderContext, float kernelScale );

    private:
        virtual void                UIPanelTick( vaApplicationBase & application ) override;
    };
    
}
