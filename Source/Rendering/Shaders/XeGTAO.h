///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// XeGTAO is based on GTAO/GTSO "Jimenez et al. / Practical Real-Time Strategies for Accurate Indirect Occlusion", 
// https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
// 
// Implementation:  Filip Strugar (filip.strugar@intel.com), Steve Mccalla <stephen.mccalla@intel.com>         (\_/)
// Version:         1.01                                                                                      (='.'=)
// Details:         https://github.com/GameTechDev/XeGTAO                                                     (")_(")
//
// Version history:
// 1.00 (2021-08-09): Initial release
// 1.01 (2021-09-09): Fix for depth going to inf for 'far' depth buffer values that are out of fp16 range
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __XE_GTAO_TYPES_H__
#define __XE_GTAO_TYPES_H__

#ifdef __cplusplus

#include <cmath>

namespace XeGTAO
{

    // cpp<->hlsl mapping
    struct Matrix4x4    { float           m[16];    };
    struct Vector3      { float           x,y,z;    };
    struct Vector2      { float           x,y;      };
    struct Vector2i     { int             x,y;      };
    typedef unsigned int uint;

#else // #ifdef __cplusplus

    // cpp<->hlsl mapping
    #define Matrix4x4       float4x4
    #define Vector3         float3
    #define Vector2         float2
    #define Vector2i        int2
    
#endif

    // Global consts that need to be visible from both shader and cpp side
    #define XE_GTAO_DEPTH_MIP_LEVELS                    5                   // this one is hard-coded to 5 for now
    #define XE_GTAO_NUMTHREADS_X                        8                   // these can be changed
    #define XE_GTAO_NUMTHREADS_Y                        8                   // these can be changed
    #define XE_GTAO_DENOISE_EXTERIOR_THREADS_X          8                   // these can't be changed
    #define XE_GTAO_DENOISE_EXTERIOR_THREADS_Y          16                  // these can't be changed
    #define XE_GTAO_DENOISE_INTERIOR_X                  (XE_GTAO_DENOISE_EXTERIOR_THREADS_X*2-2)
    #define XE_GTAO_DENOISE_INTERIOR_Y                  (XE_GTAO_DENOISE_EXTERIOR_THREADS_Y-2)

    struct GTAOConstants
    {
        Vector2i                ViewportSize;
        Vector2                 ViewportPixelSize;                  // .zw == 1.0 / ViewportSize.xy

        Vector2                 DepthUnpackConsts;
        Vector2                 CameraTanHalfFOV;

        Vector2                 NDCToViewMul;
        Vector2                 NDCToViewAdd;

        Vector2                 NDCToViewMul_x_PixelSize;
        float                   EffectRadius;                       // world (viewspace) maximum size of the shadow
        float                   EffectFalloffRange;

        float                   RadiusMultiplier;
        float                   Padding0;
        float                   FinalValuePower;
        float                   DenoiseBlurBeta;

        float                   SampleDistributionPower;
        float                   ThinOccluderCompensation;
        float                   DepthMIPSamplingOffset;
        int                     NoiseIndex;                         // frameIndex % 64 if using TAA or 0 otherwise
    };

    // This is used only for the development (ray traced ground truth).
    struct ReferenceRTAOConstants
    {
        float                   TotalRaysLength     ;       // similar to Radius from GTAO
        float                   Albedo              ;       // the assumption on the average material albedo
        int                     MaxBounces          ;       // how many rays to recurse before stopping
        int                     AccumulatedFrames   ;       // how many frames have we accumulated so far (after resetting/clearing). If 0 - this is the first.
        int                     AccumulateFrameMax  ;       // how many frames are we aiming to accumulate; stop when we hit!
        int                     Padding0;
        int                     Padding1;
        int                     Padding2;
#ifdef __cplusplus
        ReferenceRTAOConstants( ) { TotalRaysLength = 1.0f; Albedo = 0.0f; MaxBounces = 1; AccumulatedFrames = 0; AccumulateFrameMax = 0; }
#endif
    };

    #ifndef XE_GTAO_USE_DEFAULT_CONSTANTS
    #define XE_GTAO_USE_DEFAULT_CONSTANTS 1
    #endif

    // some constants reduce performance if provided as dynamic values; if these constants are not required to be dynamic and they match default values, 
    // set XE_GTAO_USE_DEFAULT_CONSTANTS and the code will compile into a more efficient shader
    #define XE_GTAO_DEFAULT_RADIUS_MULTIPLIER              (1.475f  )   // allows us to use different value as compared to ground truth radius to counter inherent screen space biases
    #define XE_GTAO_DEFAULT_FALLOFF_RANGE                  (0.657f  )   // distant samples contribute less
    #define XE_GTAO_DEFAULT_SAMPLE_DISTRIBUTION_POWER      (2.0f    )   // small crevices more important than big surfaces
    #define XE_GTAO_DEFAULT_THIN_OCCLUDER_COMPENSATION     (0.0f    )   // the new 'thickness heuristic' approach
    #define XE_GTAO_DEFAULT_FINAL_VALUE_POWER              (2.2f    )   // modifies the final ambient occlusion value using power function - this allows some of the above heuristics to do different things
    #define XE_GTAO_DEFAULT_DEPTH_MIP_SAMPLING_OFFSET      (3.30f   )   // main trade-off between performance (memory bandwidth) and quality (temporal stability is the first affected, thin objects next)

    // From https://www.shadertoy.com/view/3tB3z3 - except we're using R2 here
    #define XE_HILBERT_LEVEL    6U
    #define XE_HILBERT_WIDTH    ( (1U << XE_HILBERT_LEVEL) )
    #define XE_HILBERT_AREA     ( XE_HILBERT_WIDTH * XE_HILBERT_WIDTH )
    inline uint HilbertIndex( uint posX, uint posY )
    {   
        uint index = 0U;
        for( uint curLevel = XE_HILBERT_WIDTH/2U; curLevel > 0U; curLevel /= 2U )
        {
            uint regionX = ( posX & curLevel ) > 0U;
            uint regionY = ( posY & curLevel ) > 0U;
            index += curLevel * curLevel * ( (3U * regionX) ^ regionY);
            if( regionY == 0U )
            {
                if( regionX == 1U )
                {
                    posX = uint( (XE_HILBERT_WIDTH - 1U) ) - posX;
                    posY = uint( (XE_HILBERT_WIDTH - 1U) ) - posY;
                }

                uint temp = posX;
                posX = posY;
                posY = temp;
            }
        }
        return index;
    }

#ifdef __cplusplus

    struct GTAOSettings
    {
        int         QualityLevel                        = 2;        // 0: low; 1: medium; 2: high; 3: ultra
        int         DenoiseLevel                        = 1;        // 0: disabled; 1: standard; 2: blurry
        float       Radius                              = 0.5f;     // [0.0,  ~ ]   World (view) space size of the occlusion sphere.

        // auto-tune-d settings
        float       RadiusMultiplier                    = XE_GTAO_DEFAULT_RADIUS_MULTIPLIER;
        float       FalloffRange                        = XE_GTAO_DEFAULT_FALLOFF_RANGE;
        float       SampleDistributionPower             = XE_GTAO_DEFAULT_SAMPLE_DISTRIBUTION_POWER;
        float       ThinOccluderCompensation            = XE_GTAO_DEFAULT_THIN_OCCLUDER_COMPENSATION;    
        float       FinalValuePower                     = XE_GTAO_DEFAULT_FINAL_VALUE_POWER;
        float       DepthMIPSamplingOffset              = XE_GTAO_DEFAULT_DEPTH_MIP_SAMPLING_OFFSET;
    };

    template<class T> inline T clamp( T const & v, T const & min, T const & max ) { assert( max >= min ); if( v < min ) return min; if( v > max ) return max;  return v; }

    // If using TAA then set noiseIndex to frameIndex % 64 - otherwise use 0
    inline void GTAOUpdateConstants( XeGTAO::GTAOConstants& consts, int viewportWidth, int viewportHeight, const XeGTAO::GTAOSettings & settings, const float projMatrix[16], bool rowMajor, unsigned int frameCounter )
    {
        consts.ViewportSize                 = { viewportWidth, viewportHeight };
        consts.ViewportPixelSize            = { 1.0f / (float)viewportWidth, 1.0f / (float)viewportHeight };

        float depthLinearizeMul = (rowMajor)?(-projMatrix[3 * 4 + 2]):(-projMatrix[3 + 2 * 4]);     // float depthLinearizeMul = ( clipFar * clipNear ) / ( clipFar - clipNear );
        float depthLinearizeAdd = (rowMajor)?( projMatrix[2 * 4 + 2]):( projMatrix[2 + 2 * 4]);     // float depthLinearizeAdd = clipFar / ( clipFar - clipNear );

        // correct the handedness issue. need to make sure this below is correct, but I think it is.
        if( depthLinearizeMul * depthLinearizeAdd < 0 )
            depthLinearizeAdd = -depthLinearizeAdd;
        consts.DepthUnpackConsts            = { depthLinearizeMul, depthLinearizeAdd };

        float tanHalfFOVY = 1.0f / ((rowMajor)?(projMatrix[1 * 4 + 1]):(projMatrix[1 + 1 * 4]));    // = tanf( drawContext.Camera.GetYFOV( ) * 0.5f );
        float tanHalfFOVX = 1.0F / ((rowMajor)?(projMatrix[0 * 4 + 0]):(projMatrix[0 + 0 * 4]));    // = tanHalfFOVY * drawContext.Camera.GetAspect( );
        consts.CameraTanHalfFOV             = { tanHalfFOVX, tanHalfFOVY };

        consts.NDCToViewMul                 = { consts.CameraTanHalfFOV.x * 2.0f, consts.CameraTanHalfFOV.y * -2.0f };
        consts.NDCToViewAdd                 = { consts.CameraTanHalfFOV.x * -1.0f, consts.CameraTanHalfFOV.y * 1.0f };

        consts.NDCToViewMul_x_PixelSize     = { consts.NDCToViewMul.x * consts.ViewportPixelSize.x, consts.NDCToViewMul.y * consts.ViewportPixelSize.y };

        consts.EffectRadius                 = settings.Radius;

        consts.EffectFalloffRange           = settings.FalloffRange;
        consts.DenoiseBlurBeta              = (settings.DenoiseLevel==0)?(1e4f):(1.2f);    // high value disables denoise - more elegant & correct way would be do set all edges to 0

        consts.RadiusMultiplier             = settings.RadiusMultiplier;
        consts.SampleDistributionPower      = settings.SampleDistributionPower;
        consts.ThinOccluderCompensation     = settings.ThinOccluderCompensation;
        consts.FinalValuePower              = settings.FinalValuePower;
        consts.DepthMIPSamplingOffset       = settings.DepthMIPSamplingOffset;
        consts.NoiseIndex                   = (settings.DenoiseLevel>0)?(frameCounter % 64):(0);
        consts.Padding0 = 0;
    }

#ifdef IMGUI_API
    inline bool GTAOImGuiSettings( XeGTAO::GTAOSettings & settings )
    {
        bool hadChanges = false;
    
        ImGui::PushItemWidth( 120.0f );

        ImGui::Text( "Performance/quality settings:" );

        ImGui::Combo( "Quality Level", &settings.QualityLevel, "Low\0Medium\0High\0Ultra\0");
        if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Higher quality settings use more samples per pixel but are slower" );
        settings.QualityLevel       = clamp( settings.QualityLevel , 0, 3 );

        ImGui::Combo( "Denoising level", &settings.DenoiseLevel, "Disabled\0Standard\0Blurry\0");
        if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "The amount of edge-aware spatial denoise applied" );
        settings.DenoiseLevel       = clamp( settings.DenoiseLevel , 0, 2 );

        ImGui::Text( "Visual settings:" );

        settings.Radius             = clamp( settings.Radius, 0.0f, 100000.0f );

        hadChanges |= ImGui::InputFloat( "Effect radius",               &settings.Radius              , 0.05f, 0.0f, "%.2f" );
        if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "World (viewspace) effect radius\nExpected range: depends on the scene & requirements, anything from 0.01 to 1000+" );
        settings.Radius                             = clamp( settings.Radius                          , 0.0f, 10000.0f      );

        if( ImGui::CollapsingHeader( "Auto-tuned settings (heuristics)" ) )
        {
            hadChanges |= ImGui::InputFloat( "Radius multiplier",    &settings.RadiusMultiplier , 0.05f, 0.0f, "%.2f" );
            if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Multiplies the 'Effect Radius' - used by the auto-tune to best match raytraced ground truth\nExpected range: [0.3, 3.0], defaults to %.3f", XE_GTAO_DEFAULT_RADIUS_MULTIPLIER );
            settings.RadiusMultiplier               = clamp( settings.RadiusMultiplier          , 0.3f, 3.0f          );

            hadChanges |= ImGui::InputFloat( "Falloff range",        &settings.FalloffRange     , 0.05f, 0.0f, "%.2f" );
            if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Gently reduce sample impact as it gets out of 'Effect radius' bounds\nExpected range: [0.0, 1.0], defaults to %.3f", XE_GTAO_DEFAULT_FALLOFF_RANGE );
            settings.FalloffRange                   = clamp( settings.FalloffRange              , 0.0f, 1.0f      );

            hadChanges |= ImGui::InputFloat( "Sample distribution power",   &settings.SampleDistributionPower  , 0.05f, 0.0f, "%.2f" );
            if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Make samples on a slice equally distributed (1.0) or focus more towards the center (>1.0)\nExpected range: [1.0, 3.0], 2defaults to %.3f", XE_GTAO_DEFAULT_SAMPLE_DISTRIBUTION_POWER );
            settings.SampleDistributionPower        = clamp( settings.SampleDistributionPower   , 1.0f, 3.0f      );

            hadChanges |= ImGui::InputFloat( "Thin occluder compensation",   &settings.ThinOccluderCompensation, 0.05f, 0.0f, "%.2f" );
            if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Slightly reduce impact of samples further back to counter the bias from depth-based (incomplete) input scene geometry data\nExpected range: [0.0, 0.7], defaults to %.3f", XE_GTAO_DEFAULT_THIN_OCCLUDER_COMPENSATION );
            settings.ThinOccluderCompensation       = clamp( settings.ThinOccluderCompensation      , 0.0f, 0.7f       );

            hadChanges |= ImGui::InputFloat( "Final power",                 &settings.FinalValuePower, 0.05f, 0.0f, "%.2f" );
            if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Applies power function to the final value: occlusion = pow( occlusion, finalPower )\nExpected range: [0.5, 5.0], defaults to %.3f", XE_GTAO_DEFAULT_FINAL_VALUE_POWER );
            settings.FinalValuePower                = clamp( settings.FinalValuePower           , 0.5f, 5.0f       );

            hadChanges |= ImGui::InputFloat( "Depth MIP sampling offset",   &settings.DepthMIPSamplingOffset, 0.05f, 0.0f, "%.2f" );
            if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Mainly performance (texture memory bandwidth) setting but as a side-effect reduces overshadowing by thin objects and increases temporal instability\nExpected range: [2.0, 6.0], defaults to %.3f", XE_GTAO_DEFAULT_DEPTH_MIP_SAMPLING_OFFSET );
            settings.DepthMIPSamplingOffset         = clamp( settings.DepthMIPSamplingOffset    , 0.0f, 30.0f      );
        }

        ImGui::PopItemWidth( );

        return hadChanges;
    }
#endif // IMGUI_API

}   // close the namespace

#endif // #ifdef __cplusplus


#endif // __XE_GTAO_TYPES_H__