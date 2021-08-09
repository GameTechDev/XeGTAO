///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Trapper Mcferron (trapper.mcferron@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __VA_DEPTHOFFIELD_HLSL__
#define __VA_DEPTHOFFIELD_HLSL__

#include "vaSharedTypes.h"

#define DOF_CB                      0

#define DOF_FULL_BLUR_SRV_COLOR 0
#define DOF_FULL_BLUR_UAV_DEST  0

#define DOF_SPLIT_PLANES_SRV_DEPTH  0
#define DOF_SPLIT_PLANES_SRV_COLOR  1
#define DOF_SPLIT_PLANES_UAV_NEAR  0
#define DOF_SPLIT_PLANES_UAV_FAR   1
#define DOF_SPLIT_PLANES_UAV_COC   2

#define DOF_FAR_BLUR_SRV_COC    0
#define DOF_FAR_BLUR_SRV_COLOR  1
#define DOF_FAR_BLUR_UAV_COLOR  0

#define DOF_NEAR_BLUR_SRV_COLOR  0
#define DOF_NEAR_BLUR_UAV_COLOR  0

#define DOF_RESOLVE_SRV_COC     0
#define DOF_RESOLVE_SRV_FAR     1
#define DOF_RESOLVE_SRV_NEAR    2
#define DOF_RESOLVE_UAV_OUT     0

#ifdef __cplusplus
namespace Vanilla
{
#endif

struct DepthOfFieldShaderConstants
{
    float   focalStart;
    float   focalEnd;
    
    float   nearKernel;
    float   farKernel;

    float   nearBlend;
    float   cocRamp;
    
    float   _pad0;
    float   _pad1;
};

#ifdef __cplusplus
}
#endif

// The rest below is shader only code
#ifndef __cplusplus

#include "vaShared.hlsl"

cbuffer DepthOfFieldShaderConstantsBuffer : register( B_CONCATENATER( DOF_CB ) )
{
    DepthOfFieldShaderConstants g_dof_cb;
}

static const uint c_gaussTotalColors = 7;
static const float c_gaussWeights[ c_gaussTotalColors ] =
{
    0.1964825501511404,
    0.2969069646728344,
    0.2969069646728344,
    0.09447039785044732,
    0.09447039785044732,
    0.010381362401148057,
    0.010381362401148057,
};

float4 blur_gauss_weighted(Texture2D<float4> colorTexture, float coc, float2 pixel, uint2 resolution, float2 direction, float kernel, uniform bool far)
{
   float4 color[ c_gaussTotalColors ];
   
   float dist = kernel * coc;

   color[ 0 ] = colorTexture.SampleLevel( g_samplerLinearClamp, pixel / resolution, 0 );

   if( far )
   {
#if VA_DIRECTX == 11
       [flatten]    // avoid buggy fxc compiler warning: error X4000: use of potentially uninitialized variable (blur_gauss_weighted)
#else
       [branch]
#endif
       if( color[ 0 ].a < 0.5 )
       {
           return color[ 0 ];
       }
   }


   float2 off1 = float2(1.411764705882353, 1.411764705882353) * direction * dist;
   float2 off2 = float2(3.2941176470588234, 3.2941176470588234) * direction * dist;
   float2 off3 = float2(5.176470588235294, 5.176470588235294) * direction * dist;

   
   color[ 1 ] = colorTexture.SampleLevel( g_samplerLinearClamp, (pixel + off1) / resolution, 0 );
   color[ 2 ] = colorTexture.SampleLevel( g_samplerLinearClamp, (pixel - off1) / resolution, 0 );
   color[ 3 ] = colorTexture.SampleLevel( g_samplerLinearClamp, (pixel + off2) / resolution, 0 );
   color[ 4 ] = colorTexture.SampleLevel( g_samplerLinearClamp, (pixel - off2) / resolution, 0 );
   color[ 5 ] = colorTexture.SampleLevel( g_samplerLinearClamp, (pixel + off3) / resolution, 0 );
   color[ 6 ] = colorTexture.SampleLevel( g_samplerLinearClamp, (pixel - off3) / resolution, 0 );

    float4 blur_color = 0;
    //blur_color.a = color[0].a;

    const float convertedBleedK = (far)?(4):(1/4);

    float sumWeights = max( 0.1, pow( color[0].a, convertedBleedK ) * c_gaussWeights[ 0 ] );
    blur_color.rgba += color[0].rgba * sumWeights;

    [unroll]
    for ( uint i = 1; i < c_gaussTotalColors; i++ )
    {
        float weight = pow( color[i].a, convertedBleedK ) * c_gaussWeights[ i ];
        blur_color.rgba += color[i].rgba * weight;
        sumWeights += weight;
    }
    blur_color.rgba /= sumWeights;

   return blur_color;
}

#if DOF_SPLIT_PLANES

Texture2D<float> g_splitplanes_depth : register( T_CONCATENATER( DOF_SPLIT_PLANES_SRV_DEPTH ) );
Texture2D<float3> g_splitplanes_color : register( T_CONCATENATER( DOF_SPLIT_PLANES_SRV_COLOR  ) );

RWTexture2D<float4>  g_splitplanes_near  : register( U_CONCATENATER( DOF_SPLIT_PLANES_UAV_NEAR )  );
RWTexture2D<float4>  g_splitplanes_far   : register( U_CONCATENATER( DOF_SPLIT_PLANES_UAV_FAR )  );
RWTexture2D<unorm float>  g_splitplanes_coc   : register( U_CONCATENATER( DOF_SPLIT_PLANES_UAV_COC )  );

float2 compute_coc( float focal_start, float focal_end, float depth )
{
    const float far_transition = max( 0, g_dof_cb.cocRamp );
    const float near_transition = max( 0, g_dof_cb.nearBlend );
    
    float2 coc;
   
    const float eps = .0000001;
   
    float local_near_depth = max( 0, focal_start - depth );
    coc.x = local_near_depth / (near_transition + eps);
    coc.x = min( coc.x, 1 );
    
    float local_far_depth = max( 0, depth - focal_end );
    coc.y = local_far_depth / (far_transition + eps);
    coc.y = min( coc.y, 1 );

    return coc;
}

[numthreads(16, 16, 1)]
void CSSplitPlanes( uint2 dispatch_thread_id : SV_DispatchThreadID )
{
    const float focal_start = g_dof_cb.focalStart;
    const float focal_end   = g_dof_cb.focalEnd;

    const uint2 pixelTL     = dispatch_thread_id.xy * 2;
   
    int2 fullRes;
    g_splitplanes_coc.GetDimensions( fullRes.x, fullRes.y );


    const float depth00 = NDCToViewDepth( g_splitplanes_depth.Load( int3(pixelTL, 0), int2(0, 0) ).x );
    const float depth10 = NDCToViewDepth( g_splitplanes_depth.Load( int3(pixelTL, 0), int2(1, 0) ).x );
    const float depth01 = NDCToViewDepth( g_splitplanes_depth.Load( int3(pixelTL, 0), int2(0, 1) ).x );
    const float depth11 = NDCToViewDepth( g_splitplanes_depth.Load( int3(pixelTL, 0), int2(1, 1) ).x );
   
    float2 coc00 = compute_coc( focal_start, focal_end, depth00 );
    float2 coc10 = compute_coc( focal_start, focal_end, depth10 );
    float2 coc01 = compute_coc( focal_start, focal_end, depth01 );
    float2 coc11 = compute_coc( focal_start, focal_end, depth11 );
   
    // write to full resolution coc
    g_splitplanes_coc[ pixelTL + uint2(0, 0) ] = coc00.y;
    g_splitplanes_coc[ pixelTL + uint2(1, 0) ] = coc10.y;
    g_splitplanes_coc[ pixelTL + uint2(0, 1) ] = coc01.y;
    g_splitplanes_coc[ pixelTL + uint2(1, 1) ] = coc11.y;

    float2 coc = min( min( coc00, coc10 ), min( coc01, coc11 ) );

#if 0 // cheaper kernel
    // write to quarter res color buffers
    const float2 uv = (pixelTL + 1.0) / (float2)fullRes;
    const float3 color = g_splitplanes_color.SampleLevel(g_samplerLinearClamp, uv, 0);
    g_splitplanes_near[ dispatch_thread_id.xy ] = float4( color, coc.x );
    g_splitplanes_far[ dispatch_thread_id.xy ] = float4( color, coc.y );
#else // more expensive kernel
    const float2 uv = (pixelTL + 1.0) / (float2)fullRes;
    const float3 colorc  = g_splitplanes_color.SampleLevel(g_samplerLinearClamp, uv, 0, int2(0, 0));
    const float3 color00 = g_splitplanes_color.SampleLevel(g_samplerLinearClamp, uv, 0, int2(-1, -1));
    const float3 color10 = g_splitplanes_color.SampleLevel(g_samplerLinearClamp, uv, 0, int2(+1, -1));
    const float3 color01 = g_splitplanes_color.SampleLevel(g_samplerLinearClamp, uv, 0, int2(-1, +1));
    const float3 color11 = g_splitplanes_color.SampleLevel(g_samplerLinearClamp, uv, 0, int2(+1, +1));

    float wnear = 0.1 + coc00.x + coc10.x + coc01.x + coc11.x;
    float wfar  = 0.1 + coc00.y + coc10.y + coc01.y + coc11.y;

    g_splitplanes_near[ dispatch_thread_id.xy ] = float4( (colorc*0.1 + color00*coc00.x + color10*coc10.x + color01*coc01.x + color11*coc11.x) / wnear, coc.x );
    g_splitplanes_far[ dispatch_thread_id.xy ]  = float4( (colorc*0.1 + color00*coc00.y + color10*coc10.y + color01*coc01.y + color11*coc11.y) / wfar, coc.y );
#endif
}

#endif

#if DOF_FAR_BLUR

Texture2D<unorm float> g_coc : register( T_CONCATENATER( DOF_FAR_BLUR_SRV_COC ) );
Texture2D<float4> g_farblur_color : register( T_CONCATENATER( DOF_FAR_BLUR_SRV_COLOR ) );
RWTexture2D<float4>  g_farblur_out  : register( U_CONCATENATER( DOF_FAR_BLUR_UAV_COLOR ) );

float4 blur(float coc, float2 pixel, uint2 resolution, float kernel_size) 
{
    // Max Points On Ring: 16
    // Number of Rings: 4
    static const uint max_bokeh_samples = 27; //28;
    static const float2 kernel[max_bokeh_samples] = {
        //float2(0,0),
        float2(0.25,0),
        float2(0.07429166058208397,0.2933711331132184),
        float2(-0.31244462213914737,0.16908657384211828),
        float2(-0.2762595884262893,-0.3000979109324614),
        float2(0.2518840201879596,-0.3855371939366909),
        float2(0.5,0),
        float2(0.39124001499917244,0.3566623995775891),
        float2(0.05156173028831584,0.5564396867531075),
        float2(-0.35449096257603313,0.469421898400141),
        float2(-0.60713044392241,0.1134923492396464),
        float2(-0.5501404995897503,-0.34063257597946556),
        float2(-0.18512496387229152,-0.650646758616907),
        float2(0.31463883937167364,-0.6318799703682794),
        float2(0.6856413451502616,-0.2656188721964361),
        float2(0.75,0),
        float2(0.6712710424624072,0.3780528079214011),
        float2(0.409953306572099,0.6762608584991258),
        float2(0.026001024662720397,0.810807695795456),
        float2(-0.38466194545538834,0.7373249333612194),
        float2(-0.7140852730455989,0.46481800256204914),
        float2(-0.8706564395934057,0.05589799804439777),
        float2(-0.8044364891985885,-0.3873961956406768),
        float2(-0.5224942967442266,-0.7490348651676279),
        float2(-0.08965415174033534,-0.929359069743384),
        float2(0.3861963528769777,-0.8724242882854946),
        float2(0.7809693968203044,-0.5828526087950118),
        float2(0.9867298606914948,-0.1272247271861163),
    };


    float dist = kernel_size;
   
    float2 texel_size = 1.0f / resolution;
    float2 uv = pixel * texel_size;
   
    float4 bokeh_samples[ max_bokeh_samples ];
    
    uint i;
 
    float4 blur_color = g_farblur_color.SampleLevel( g_samplerLinearClamp, uv, 0 );
   
    [flatten]   // avoids a weird compile error (potential use of uninitialized variable)
    if ( coc == 0 )
        return blur_color;

    //dist *= coc;
   
    const uint samples = max_bokeh_samples;//(int) ((kernel_size / 20.0) * max_bokeh_samples);
    
    [unroll]
    for ( i = 0; i < samples; i++ )
    {
        float2 sample_uv = uv + (texel_size * kernel[max_bokeh_samples - i - 1] * dist);
        bokeh_samples[i] = g_farblur_color.SampleLevel( g_samplerLinearClamp, sample_uv, 0 );
    }

    // only use pixels which are a definite blur (alpha == 1)
    // this prevents haloing when bilinear samples take in a blur and non blur pixel
    float valid_count = 2;

    // a little extra wait for our center color
    // helps prevents holes in the middle
    blur_color *= valid_count;
    
    [unroll]
    for ( i = 0; i < samples; i++ )
    {
        [branch]
        if ( bokeh_samples[ i ].a > 0 )
        {
            blur_color += (bokeh_samples[i] * bokeh_samples[ i ].a);
            valid_count += bokeh_samples[ i ].a;
        }
    }

    blur_color /= valid_count;

    // this pixel has blur
    // mark it as such (with the alpha) so the gauss blur passes
    // will make sure to incorporate it
    blur_color.a = 1;
  
    return blur_color;
}

[numthreads(8, 8, 1)]
void CSFarBlur(uint3 group_thread_id : SV_GroupThreadID, uint3 dispatch_thread_id : SV_DispatchThreadID)
{  
   const int type = DOF_BLUR_TYPE;
   const float far_kernel = g_dof_cb.farKernel;
   const float2 pixel = dispatch_thread_id.xy + .5;
   
    uint2 resolution;
    g_farblur_out.GetDimensions( resolution.x, resolution.y );

    float2 dof_pixel = pixel;
    
    float2 coc_uv = dof_pixel / resolution;
   
    float coc = g_coc.SampleLevel( g_samplerLinearClamp, coc_uv, 0 );
    
    const float2 blur_directions[] = 
    {
        float2(1, 0),
        float2(0, 1),
    };

    // bokeh blur
    //[branch]
    if ( type == 0 )
        g_farblur_out[ dof_pixel ] = blur(coc, dof_pixel, resolution, far_kernel );
    else //gaussian out bokeh
        g_farblur_out[ dof_pixel ] = blur_gauss_weighted( g_farblur_color, coc, pixel, resolution, blur_directions[type - 1], 1.0, true);
}

#endif

#if DOF_NEAR_BLUR

Texture2D<float4> g_nearblur_color : register( T_CONCATENATER( DOF_NEAR_BLUR_SRV_COLOR  ) );
RWTexture2D<float4>  g_nearblur_out  : register( U_CONCATENATER( DOF_NEAR_BLUR_UAV_COLOR )  );

float4 blur(float2 pixel, uint2 resolution, float kernel_size) 
{
    // trapzz.com/code/bokeh.html
    // Max Points On Ring: 10
    // Number of Rings: 3
    static const uint max_bokeh_samples = 11;
    static const float2 kernel[max_bokeh_samples] = {
       //float2(0,0),
       float2(0.3333333333333333,0),
       float2(2.5513474982236523e-17,0.41666666666666663),
       float2(-0.5,6.123233995736766e-17),
       float2(-1.0715659492539339e-16,-0.5833333333333333),
       float2(0.6666666666666666,0),
       float2(0.445349858470524,0.5584510589057355),
       float2(-0.1695397592048109,0.7428022188051989),
       float2(-0.7293557502067202,0.3512392173808805),
       float2(-0.772259029630645,-0.3719003478150497),
       float2(-0.2013284640557132,-0.8820776348311737),
       float2(0.5937998112940317,-0.7446014118743142),
    }; 

   const uint total_colors = max_bokeh_samples + 1;

   float dist = kernel_size;
   
   float2 texel_size = 1.0f / resolution;
   float2 uv = pixel / resolution;
   
   
   float4 color[ total_colors ];
   
   uint i;
 
   color[ 0 ] = g_nearblur_color.SampleLevel( g_samplerLinearClamp, uv, 0 );

   int start = 1;
   
   [unroll]
   for ( i = 0; i < max_bokeh_samples; i++ )
   {
      float2 sample_uv = uv + (texel_size * kernel[i] * dist);
      color[ start++ ] = g_nearblur_color.SampleLevel( g_samplerLinearClamp, sample_uv, 0 );
   }

 
   // only use pixels which should have some blur (alpha > 0)
   // this prevents smeering of focal pixels in the foreground blur
   float4 blur_color = color[ 0 ];

   uint valid_count = 1;
   
   [unroll]
   for ( i = 1; i < total_colors; i++ )
   {
      [branch]
      if ( color[ i ].a > 0 )
      {
         blur_color += color[i];
         valid_count++;
      }
   }

   blur_color *= (1.0f / valid_count);

   return blur_color;
}

[numthreads(8, 8, 1)]
void CSNearBlur(uint3 group_thread_id : SV_GroupThreadID, uint3 dispatch_thread_id : SV_DispatchThreadID)
{  
    const int type = DOF_BLUR_TYPE;
    const float near_kernel = g_dof_cb.nearKernel;
    const float2 pixel = dispatch_thread_id.xy + .5;
   
    uint2 resolution;
    g_nearblur_out.GetDimensions( resolution.x, resolution.y );
   
    const float2 blur_directions[] = 
    {
        float2(1, 0),
        float2(0, 1),
    };

    //[branch]
    if ( type == 0 )
        g_nearblur_out[ pixel ] = blur(pixel, resolution, near_kernel);
    else
        g_nearblur_out[ pixel ] = blur_gauss_weighted( g_nearblur_color, 1, pixel, resolution, blur_directions[type - 1], 1, false);
}


#endif

#if DOF_RESOLVE

Texture2D<unorm float> g_coc : register(T_CONCATENATER(DOF_RESOLVE_SRV_COC));
Texture2D<float4> g_dof_far : register(T_CONCATENATER(DOF_RESOLVE_SRV_FAR));
Texture2D<float4> g_dof_near : register(T_CONCATENATER(DOF_RESOLVE_SRV_NEAR));
RWTexture2D<unorm float4> g_color : register(U_CONCATENATER(DOF_RESOLVE_UAV_OUT));

[numthreads(16, 16, 1)]
void CSResolve(uint3 dispatch_thread_id : SV_DispatchThreadID )   
{
    const float focal_start = g_dof_cb.focalStart;
    const float focal_end = g_dof_cb.focalEnd;
    const float near_pow = g_dof_cb.nearBlend;
    
    int2 d_pixel = dispatch_thread_id.xy;
    
    int2 dispatch_res;
    g_coc.GetDimensions( dispatch_res.x, dispatch_res.y );

    const float2 uv = (d_pixel + .5) / (float2) dispatch_res;
   
    float far_coc = g_coc[ d_pixel ];

    float4 dof_near = g_dof_near.SampleLevel( g_samplerLinearClamp, uv, 0 );
   
    float3 color = SRGB_to_LINEAR( g_color[ d_pixel ].rgb );
    
    if ( far_coc > 0 )
    {
        float3 far_color = g_dof_far.SampleLevel( g_samplerLinearClamp, uv, 0 ).rgb;
        color = lerp( color, far_color, far_coc );
    }

    float alpha = dof_near.a * (1.0 - far_coc); // fade the more we're going into far blur territory
    color = lerp( color, dof_near.rgb, alpha );
   
    g_color[ d_pixel ] = float4( LINEAR_to_SRGB( color ), 1 );
}

#endif

#endif // #ifndef __cplusplus

#endif // #ifndef __VA_DEPTHOFFIELD_HLSL__

