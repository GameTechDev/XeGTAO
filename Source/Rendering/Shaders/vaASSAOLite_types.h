///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __VA_ASSAOLITE_TYPES_H__
#define __VA_ASSAOLITE_TYPES_H__

#ifdef __cplusplus

#include <cmath>

namespace ASSAO
{

    struct Matrix4x4    { float           m[16];    };
    struct Vector4      { float           x,y,z,w;  };
    struct Vector3      { float           x,y,z;    };
    struct Vector2      { float           x,y;      };
    struct Vector2i     { int             x,y;      };
    struct Vector4i     { int             x,y,z,w;  };
    struct Vector4ui    { unsigned int    x,y,z,w;  };

#else

#define Matrix4x4       float4x4
#define Vector4         float4
#define Vector3         float3
#define Vector2         float2
#define Vector2i        int2
#define Vector4i        int4
#define Vector4ui       uint4

#ifndef CONCATENATE_HELPER
#define CONCATENATE_HELPER(a, b) a##b
#define CONCATENATE(a, b) CONCATENATE_HELPER(a, b)

#define B_CONCATENATER(x) CONCATENATE(b,x)
#define S_CONCATENATER(x) CONCATENATE(s,x)
#define T_CONCATENATER(x) CONCATENATE(t,x)
#define U_CONCATENATER(x) CONCATENATE(u,x)
#endif

#endif

// Global consts that need to be visible from both shader and cpp side
#define ASSAO_DEPTH_MIP_LEVELS                       4                   // this one is hard-coded to 4
#define ASSAO_NUMTHREADS_X                           8                   // these can be changed
#define ASSAO_NUMTHREADS_Y                           8                   // these can be changed
// TODO: on TitanV faster path was to actually to dispatch XxYx1 groups that are threadgroup[8x8x4] in size, while I expected
// the texture cache behaviour to be better with XxYx4 and threadgroup[8x8x1] but perhaps the first one has better thread 
// utilization. This needs to get tested on Intel, AMD and newer Nvidia hardware!
#define ASSAO_NUMTHREADS_LAYERED_Z                   4

#define ASSAO_MAX_BLUR_PASS_COUNT                    4

// Default binding slots for samplers, constants and SRVs/UAVs
// If not using defaults, one needs to provide custom definitions using
// ASSAO_DEFINE_EXTERNAL_SAMPLERS, ASSAO_DEFINE_EXTERNAL_CONSTANTBUFFER, ASSAO_DEFINE_EXTERNAL_SRVS_UAVS

#define ASSAO_POINTCLAMP_SAMPLERSLOT                 10
#define ASSAO_LINEARCLAMP_SAMPLERSLOT                12

#define ASSAO_CONSTANTBUFFER_SLOT                    0

#define ASSAO_SRV_SOURCE_NDC_DEPTH_SLOT              0
#define ASSAO_SRV_SOURCE_NORMALMAP_SLOT              1
#define ASSAO_SRV_WORKING_DEPTH_SLOT                 2
#define ASSAO_SRV_WORKING_OCCLUSION_EDGE_SLOT        3

#define ASSAO_UAV_DEPTHS_SLOT                        0
#define ASSAO_UAV_DEPTHS_MIP1_SLOT                   1
#define ASSAO_UAV_DEPTHS_MIP2_SLOT                   2
#define ASSAO_UAV_DEPTHS_MIP3_SLOT                   3
#define ASSAO_UAV_NORMALMAP_SLOT                     4
#define ASSAO_UAV_OCCLUSION_EDGE_SLOT                5
#define ASSAO_UAV_FINAL_OCCLUSION_SLOT               6
#define ASSAO_UAV_DEBUG_IMAGE_SLOT                   7

#if defined(ASSAO_SHADER_PING) || defined(ASSAO_SHADER_PONG)  // if there are separate shaders for pinging ponging when custom slot assignment is not available on the engine side
#define ASSAO_SRV_WORKING_OCCLUSION_EDGE_B_SLOT      4
#define ASSAO_UAV_OCCLUSION_EDGE_B_SLOT              8
#endif

// sizeof(ASSAOConstants) is 448b, which is 28x16 - so it will not mess up packing if added into a bigger command buffer struct
struct ASSAOConstants
{
    Matrix4x4               ViewMatrix;                             // if input normals are in world space, otherwise keep identity

    Vector2i                ViewportSize;
    Vector2i                HalfViewportSize;

    Vector2                 ViewportPixelSize;                      // .zw == 1.0 / ViewportSize.xy
    Vector2                 HalfViewportPixelSize;                  // .zw == 1.0 / ViewportHalfSize.xy

    Vector2                 DepthUnpackConsts;
    Vector2                 CameraTanHalfFOV;

    Vector2                 NDCToViewMul;
    Vector2                 NDCToViewAdd;

    Vector2                 Viewport2xPixelSize;
    Vector2                 Viewport2xPixelSize_x_025;              // Viewport2xPixelSize * 0.25 (for fusing add+mul into mad)

    float                   EffectRadius;                           // world (viewspace) maximum size of the shadow
    float                   EffectShadowStrength;                   // global strength of the effect (0 - 5)
    float                   EffectShadowPow;
    float                   EffectShadowClamp;

    float                   EffectFadeOutMul;                       // effect fade out from distance (ex. 25)
    float                   EffectFadeOutAdd;                       // effect fade out to distance   (ex. 100)
    float                   EffectHorizonAngleThreshold;            // limit errors on slopes and caused by insufficient geometry tessellation (0.05 to 0.5)
    float                   EffectSamplingRadiusNearLimitRec;       // if viewspace pixel closer than this, don't enlarge shadow sampling radius anymore (makes no sense to grow beyond some distance, not enough samples to cover everything, so just limit the shadow growth; could be SSAOSettingsFadeOutFrom * 0.1 or less)

    float                   NegRecEffectRadius;                     // -1.0 / EffectRadius
    float                   DetailAOStrength;
    float                   RadiusDistanceScalingFunctionPow;
    float                   InvSharpness;

    Vector4                 PatternRotScaleMatrices[4*5];
};

#ifdef __cplusplus

struct ASSAOSettings
{
    float       Radius                              = 1.2f;     // [0.0,  ~ ]   World (view) space size of the occlusion sphere.
    float       ShadowMultiplier                    = 1.0f;     // [0.0, 5.0]   Effect strength linear multiplier
    float       ShadowPower                         = 1.50f;    // [0.5, 5.0]   Effect strength pow modifier
    float       ShadowClamp                         = 1.00f;    // [0.0, 1.0]   Effect max limit (applied after multiplier but before blur)
    float       HorizonAngleThreshold               = 0.05f;    // [0.0, 0.2]   Limits self-shadowing (makes the sampling area less of a hemisphere, more of a spherical cone, to avoid self-shadowing and various artifacts due to low tessellation and depth buffer imprecision, etc.)
    float       FadeOutFrom                         = 50.0f;    // [0.0,  ~ ]   Distance to start start fading out the effect.
    float       FadeOutTo                           = 300.0f;   // [0.0,  ~ ]   Distance at which the effect is faded out.
    int         QualityLevel                        = 2;        // [ 0,   2 ]   Effect quality; 0 - low, 1 - medium, 2 - high; each quality level is roughly 1.5x more costly than the previous.
    int         BlurPassCount                       = 2;        // [  0,   3]   Number of edge-sensitive smart blur passes to apply. Quality 0 is an exception with only one 'dumb' blur pass used.
    float       Sharpness                           = 0.99f;    // [0.0, 1.0]   (How much to bleed over edges; 1: not at all, 0.5: half-half; 0.0: completely ignore edges)
    float       TemporalSupersamplingAngleOffset    = 0.0f;     // [0.0,  PI]   Used to rotate sampling kernel; If using temporal AA / supersampling, suggested to rotate by ( (frame%3)/3.0*PI ) or similar. Kernel is already symmetrical, which is why we use PI and not 2*PI.
    float       TemporalSupersamplingRadiusOffset   = 1.0f;     // [0.0, 2.0]   Used to scale sampling kernel; If using temporal AA / supersampling, suggested to scale by ( 1.0f + (((frame%3)-1.0)/3.0)*0.1 ) or similar.
    float       DetailShadowStrength                = 0.5f;     // [0.0, 5.0]   Used for high-res detail AO using neighboring depth pixels: adds a lot of detail but also reduces temporal stability (adds aliasing).
    float       RadiusDistanceScalingFunction       = 0.0f;     // [0.0, 1.0]   Use 0 for default behavior (world-space radius always Settings::Radius). Anything above 0 means radius will be dynamically scaled per-pixel based on distance from viewer - this breaks consistency but adds AO on distant areas which might be desireable (for ex, for large open-world outdoor areas).
};


template<class T> inline T clamp( T const & v, T const & min, T const & max )
{
    assert( max >= min );
    if( v < min ) return min;
    if( v > max ) return max;
    return v;
}

inline void ASSAOUpdateConstants( ASSAO::ASSAOConstants& consts, int viewportWidth, int viewportHeight, const ASSAO::ASSAOSettings & settings, const float viewMatrix[16], const float projMatrix[16], bool rowMajor )
{
    int halfViewportWidth = ( viewportWidth + 1 ) / 2;
    int halfViewportHeight = ( viewportHeight + 1 ) / 2;

    for( int x = 0; x < 4; x++ )
        for( int y = 0; y < 4; y++ )
            consts.ViewMatrix.m[ x + y*4 ] = viewMatrix[ ( rowMajor )?(x + y*4):(y + x*4) ];

    consts.ViewportSize = { viewportWidth, viewportHeight };
    consts.HalfViewportSize = { halfViewportWidth, halfViewportHeight };

    consts.ViewportPixelSize = { 1.0f / (float)viewportWidth, 1.0f / (float)viewportHeight };
    consts.HalfViewportPixelSize = { 1.0f / (float)halfViewportWidth, 1.0f / (float)halfViewportHeight };

    consts.Viewport2xPixelSize = { consts.ViewportPixelSize.x * 2.0f, consts.ViewportPixelSize.y * 2.0f };
    consts.Viewport2xPixelSize_x_025 = { consts.Viewport2xPixelSize.x * 0.25f, consts.Viewport2xPixelSize.y * 0.25f };

    float depthLinearizeMul = (rowMajor)?(-projMatrix[3 * 4 + 2]):(-projMatrix[3 + 2 * 4]);     // float depthLinearizeMul = ( clipFar * clipNear ) / ( clipFar - clipNear );
    float depthLinearizeAdd = (rowMajor)?( projMatrix[2 * 4 + 2]):( projMatrix[2 + 2 * 4]);     // float depthLinearizeAdd = clipFar / ( clipFar - clipNear );
    // correct the handedness issue. need to make sure this below is correct, but I think it is.
    if( depthLinearizeMul * depthLinearizeAdd < 0 )
        depthLinearizeAdd = -depthLinearizeAdd;
    consts.DepthUnpackConsts = { depthLinearizeMul, depthLinearizeAdd };

    float tanHalfFOVY = 1.0f / ((rowMajor)?(projMatrix[1 * 4 + 1]):(projMatrix[1 + 1 * 4]));    // = tanf( drawContext.Camera.GetYFOV( ) * 0.5f );
    float tanHalfFOVX = 1.0F / ((rowMajor)?(projMatrix[0 * 4 + 0]):(projMatrix[0 + 0 * 4]));    // = tanHalfFOVY * drawContext.Camera.GetAspect( );
    consts.CameraTanHalfFOV = { tanHalfFOVX, tanHalfFOVY };

    consts.NDCToViewMul = { consts.CameraTanHalfFOV.x * 2.0f, consts.CameraTanHalfFOV.y * -2.0f };
    consts.NDCToViewAdd = { consts.CameraTanHalfFOV.x * -1.0f, consts.CameraTanHalfFOV.y * 1.0f };

    consts.EffectRadius = clamp( settings.Radius, 0.0f, 100000.0f );
    consts.EffectShadowStrength = clamp( settings.ShadowMultiplier * 4.3f, 0.0f, 10.0f );
    consts.EffectShadowPow = clamp( settings.ShadowPower, 0.0f, 5.0f );
    consts.EffectShadowClamp = clamp( settings.ShadowClamp, 0.0f, 1.0f );
    consts.EffectFadeOutMul = -1.0f / ( settings.FadeOutTo - settings.FadeOutFrom );
    consts.EffectFadeOutAdd = settings.FadeOutFrom / ( settings.FadeOutTo - settings.FadeOutFrom ) + 1.0f;
    consts.EffectHorizonAngleThreshold = clamp( settings.HorizonAngleThreshold, 0.0f, 1.0f );

    // 1.2 seems to be around the best trade off - 1.0 means on-screen radius will stop/slow growing when the camera is at 1.0 distance, so, depending on FOV, basically filling up most of the screen
    // This setting is viewspace-dependent and not screen size dependent intentionally, so that when you change FOV the effect stays (relatively) similar.
    float effectSamplingRadiusNearLimit = ( settings.Radius * 1.2f );
    consts.NegRecEffectRadius = -1.0f / consts.EffectRadius;
    consts.DetailAOStrength = settings.DetailShadowStrength;
    consts.RadiusDistanceScalingFunctionPow = settings.RadiusDistanceScalingFunction;
    effectSamplingRadiusNearLimit /= tanHalfFOVY; // to keep the effect same regardless of FOV
    consts.EffectSamplingRadiusNearLimitRec = 1.0f / effectSamplingRadiusNearLimit;
    consts.InvSharpness = clamp( 1.0f - settings.Sharpness, 0.0f, 1.0f );

    float additionalAngleOffset = settings.TemporalSupersamplingAngleOffset;  // if using temporal supersampling approach (like "Progressive Rendering Using Multi-frame Sampling" from GPU Pro 7, etc.)
    float additionalRadiusScale = settings.TemporalSupersamplingRadiusOffset; // if using temporal supersampling approach (like "Progressive Rendering Using Multi-frame Sampling" from GPU Pro 7, etc.)
    const int subPassCount = 5;
    const float PIf = 3.1415926535897932384626433832795f;
    for( int pass = 0; pass < 4; pass++ )
    {
        for( int subPass = 0; subPass < subPassCount; subPass++ )
        {
            int a = pass;
            int b = subPass;

            int spmap[5]{ 0, 1, 4, 3, 2 };
            b = spmap[subPass];

            float ca, sa;
            float angle0 = ( (float)a + (float)b / (float)subPassCount ) * PIf * 0.5f;
            angle0 += additionalAngleOffset;

            ca = std::cosf( angle0 );
            sa = std::sinf( angle0 );

            float scale = 1.0f + ( a - 1.5f + ( b - ( subPassCount - 1.0f ) * 0.5f ) / (float)subPassCount ) * 0.07f;
            scale *= additionalRadiusScale;

            consts.PatternRotScaleMatrices[pass * subPassCount + subPass] = { scale * ca, scale * -sa, -scale * sa, -scale * ca };
        }
    }
}

#ifdef IMGUI_API
inline void ASSAOImGuiSettings( ASSAO::ASSAOSettings & settings )
{
    ImGui::PushItemWidth( 120.0f );

    ImGui::Text( "Performance/quality settings:" );
    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1.0f, 0.8f, 0.8f, 1.0f ) );
    ImGui::Combo( "Quality level", &settings.QualityLevel, "Low\0Medium\0High\0\0" );  // Combo using values packed in a single constant string (for really quick combo)
    settings.QualityLevel = clamp( settings.QualityLevel, 0, 2 );

    if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Each quality level is roughly 1.5x more costly than the previous" );
    ImGui::PopStyleColor( 1 );

    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.75f, 0.75f, 0.75f, 1.0f ) );

    ImGui::InputInt( "Smart blur passes (0-3)", &settings.BlurPassCount );
    if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "The amount of edge-aware smart blur; each additional pass increases blur effect but adds to the cost" );
    settings.BlurPassCount = clamp( settings.BlurPassCount, 0, ASSAO_MAX_BLUR_PASS_COUNT );

    ImGui::Separator();
    ImGui::Text( "Visual settings:" );
    ImGui::InputFloat( "Effect radius",                     &settings.Radius                          , 0.05f, 0.0f, "%.2f" );
    if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "World (viewspace) effect radius" );
    ImGui::InputFloat( "Occlusion multiplier",              &settings.ShadowMultiplier                , 0.05f, 0.0f, "%.2f" );
    if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Effect strength" );
    ImGui::InputFloat( "Occlusion power curve",             &settings.ShadowPower                     , 0.05f, 0.0f, "%.2f" );
    if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "occlusion = pow( occlusion, value ) - changes the occlusion curve" );
    ImGui::InputFloat( "Fadeout distance from",             &settings.FadeOutFrom                     , 1.0f , 0.0f, "%.1f" );
    if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Distance at which to start fading out the effect" );
    ImGui::InputFloat( "Fadeout distance to",               &settings.FadeOutTo                       , 1.0f , 0.0f, "%.1f" );
    if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Distance at which to completely fade out the effect" );
    ImGui::InputFloat( "Sharpness",                         &settings.Sharpness                       , 0.01f, 0.0f, "%.2f" );
    if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "How much to bleed over edges; 1: not at all, 0.5: half-half; 0.0: completely ignore edges" );

    ImGui::Separator( );
    ImGui::Text( "Advanced visual settings:" );
    ImGui::InputFloat( "Detail occlusion multiplier",       &settings.DetailShadowStrength            , 0.05f, 0.0f, "%.2f" );
    if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Additional small radius / high detail occlusion; too much will create aliasing & temporal instability" );
    ImGui::InputFloat( "Horizon angle threshold",           &settings.HorizonAngleThreshold           , 0.01f, 0.0f, "%.2f" );
    if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Reduces precision and tessellation related unwanted occlusion" );
    ImGui::InputFloat( "Occlusion max clamp",               &settings.ShadowClamp                     , 0.01f, 0.0f, "%.2f" );
    if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "occlusion = min( occlusion, value ) - limits the occlusion maximum" );    
    ImGui::InputFloat( "Radius distance-based modifier",    &settings.RadiusDistanceScalingFunction   , 0.05f, 0.0f, "%.2f" );
    if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "Used to modify ""Effect radius"" based on distance from the camera; for 0.0, effect world radius is constant (default);\nfor values above 0.0, the effect radius will grow the more distant from the camera it is ( effectRadius *= pow(distance, scaling) );\nif changed, ""Effect Radius"" often needs to be rebalanced as well" );

    settings.Radius                           = clamp( settings.Radius                          , 0.0f, 100.0f      );
    settings.HorizonAngleThreshold            = clamp( settings.HorizonAngleThreshold           , 0.0f, 1.0f        );
    settings.ShadowMultiplier                 = clamp( settings.ShadowMultiplier                , 0.0f, 5.0f       );
    settings.ShadowPower                      = clamp( settings.ShadowPower                     , 0.5f, 5.0f        );
    settings.ShadowClamp                      = clamp( settings.ShadowClamp                     , 0.1f, 1.0f        );
    settings.FadeOutFrom                      = clamp( settings.FadeOutFrom                     , 0.0f, 1000000.0f  );
    settings.FadeOutTo                        = clamp( settings.FadeOutTo                       , 0.0f, 1000000.0f  );
    settings.Sharpness                        = clamp( settings.Sharpness                       , 0.0f, 1.0f        );
    settings.DetailShadowStrength             = clamp( settings.DetailShadowStrength            , 0.0f, 5.0f        );
    settings.RadiusDistanceScalingFunction    = clamp( settings.RadiusDistanceScalingFunction   , 0.0f, 2.0f        );

    ImGui::PopStyleColor( 1 );

    ImGui::PopItemWidth( );
}
#endif

}   // close the namespace
#endif


#endif // __VA_ASSAOLITE_TYPES_H__