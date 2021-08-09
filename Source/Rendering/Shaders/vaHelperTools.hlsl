///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaShared.hlsl"

#include "vaHelperToolsShared.h"

cbuffer ImageCompareToolConstantsBuffer         : register( B_CONCATENATER( IMAGE_COMPARE_TOOL_BUFFERSLOT ) )
{
    ImageCompareToolShaderConstants              g_imageCompareToolConstants;
}

Texture2D           g_ReferenceImage            : register( T_CONCATENATER( IMAGE_COMPARE_TOOL_TEXTURE_SLOT0 ) );
Texture2D           g_CurrentImage              : register( T_CONCATENATER( IMAGE_COMPARE_TOOL_TEXTURE_SLOT1 ) );

float4 ImageCompareToolVisualizationPS( const float4 svPos : SV_Position ) : SV_Target
{
    int mode = g_imageCompareToolConstants.VisType;

    float4 referenceCol = g_ReferenceImage.Load( int3( svPos.xy, 0 ) );
    float4 currentCol = g_CurrentImage.Load( int3( svPos.xy, 0 ) );

    if( mode == 1 )
    {
        return referenceCol;
    }
    else
    {
        float mult = pow( 10, max( 0, (float)mode-2.0 ) );

        float4 retVal = abs( referenceCol - currentCol ) * mult;
        return retVal;
    }
}

cbuffer UITextureDrawShaderConstantsBuffer         : register( B_CONCATENATER( TEXTURE_UI_DRAW_TOOL_BUFFERSLOT ) )
{
    UITextureDrawShaderConstants              g_UITextureDrawShaderConstants;
}

Texture2D           g_UIDrawTexture2D           : register( T_CONCATENATER( TEXTURE_UI_DRAW_TOOL_TEXTURE_SLOT0 ) );
Texture2DArray      g_UIDrawTexture2DArray      : register( T_CONCATENATER( TEXTURE_UI_DRAW_TOOL_TEXTURE_SLOT0 ) );
TextureCube         g_UIDrawTextureCube         : register( T_CONCATENATER( TEXTURE_UI_DRAW_TOOL_TEXTURE_SLOT0 ) );

float4 ConvertToUIColor( float4 value, int contentsType, float alpha, int showAlphaInRGB )
{
    float4 retVal;
    switch( contentsType )
    {
        case( 7 ) : // linear depth
        {
            //value.x += +0.17;
            retVal = float4( frac(value.x * 2.0), frac(value.x/10.0), value.x/100.0, alpha );
        }
        break;
        default:
            retVal = float4( value.rgb, alpha );
            break;
    }
    if( showAlphaInRGB )
    {
        retVal.xyz = SRGB_to_FLOAT( value.a ).xxx;
        retVal.a = alpha;
    }
    return retVal;
}

float4 UIDrawTexture2DPS( const float4 svPos : SV_Position ) : SV_Target
{
    float2 clip = (svPos.xy - g_UITextureDrawShaderConstants.ClipRect.xy)/g_UITextureDrawShaderConstants.ClipRect.zw;
    if( clip.x < 0 || clip.y < 0 || clip.x > 1 || clip.y > 1 )
        discard;

    const float4 dstRect = g_UITextureDrawShaderConstants.DestinationRect;
    float2 uv = (svPos.xy - dstRect.xy) / dstRect.zw;
    if( uv.x < 0 || uv.y < 0 || uv.x > 1 || uv.y > 1 )
        discard;

    float4 value = g_UIDrawTexture2D.SampleLevel( g_samplerLinearClamp, uv, g_UITextureDrawShaderConstants.TextureMIPIndex );

    return ConvertToUIColor( value, g_UITextureDrawShaderConstants.ContentsType, g_UITextureDrawShaderConstants.Alpha, g_UITextureDrawShaderConstants.ShowAlpha );
}

float4 UIDrawTexture2DArrayPS( const float4 svPos : SV_Position ) : SV_Target
{
    float2 clip = (svPos.xy - g_UITextureDrawShaderConstants.ClipRect.xy)/g_UITextureDrawShaderConstants.ClipRect.zw;
    if( clip.x < 0 || clip.y < 0 || clip.x > 1 || clip.y > 1 )
        discard;

    const float4 dstRect = g_UITextureDrawShaderConstants.DestinationRect;
    float2 uv = (svPos.xy - dstRect.xy) / dstRect.zw;
    if( uv.x < 0 || uv.y < 0 || uv.x > 1 || uv.y > 1 )
        discard;

    float4 value = g_UIDrawTexture2DArray.SampleLevel( g_samplerLinearClamp, float3( uv, g_UITextureDrawShaderConstants.TextureArrayIndex ), g_UITextureDrawShaderConstants.TextureMIPIndex );
    
    return ConvertToUIColor( value, g_UITextureDrawShaderConstants.ContentsType, g_UITextureDrawShaderConstants.Alpha, g_UITextureDrawShaderConstants.ShowAlpha );
}

float4 UIDrawTextureCubePS( const float4 svPos : SV_Position ) : SV_Target
{
    float2 clip = (svPos.xy - g_UITextureDrawShaderConstants.ClipRect.xy)/g_UITextureDrawShaderConstants.ClipRect.zw;
    if( clip.x < 0 || clip.y < 0 || clip.x > 1 || clip.y > 1 )
        discard;

    const float4 dstRect = g_UITextureDrawShaderConstants.DestinationRect;
    float2 uv = (svPos.xy - dstRect.xy) / dstRect.zw;
    if( uv.x < 0 || uv.y < 0 || uv.x > 1 || uv.y > 1 )
        discard;

    // not correct - use CubemapGetDirectionFor
    float3 norm = normalize( float3( uv * 2.0 - 1.0, 1 ) );

    float4 value = g_UIDrawTextureCube.SampleLevel( g_samplerLinearClamp, norm, g_UITextureDrawShaderConstants.TextureMIPIndex );
    
    // debug
    value.xyz = norm.rgb;

    return ConvertToUIColor( value, g_UITextureDrawShaderConstants.ContentsType, g_UITextureDrawShaderConstants.Alpha, g_UITextureDrawShaderConstants.ShowAlpha );
}

#ifdef VA_RENDERING_GLOBALS_DEBUG_DRAW_SPECIFIC

Texture2D<float>    g_DepthSource               : register( t0 );

float4 DebugDrawDepthPS( in float4 inPos : SV_Position ) : SV_Target0
{
    int2 screenPos = (int2)inPos.xy;

    float viewspaceDepth = NDCToViewDepth( g_DepthSource.Load( int3( screenPos, 0 ) ) );
    
    return float4( frac(viewspaceDepth / 800.0), frac(viewspaceDepth / 40.0), frac(viewspaceDepth / 2.0), 1.0 );
}

// Inputs are viewspace depth
float4 ComputeEdgesFromDepth( const float centerZ, const float leftZ, const float rightZ, const float topZ, const float bottomZ )
{
    // slope-sensitive depth-based edge detection
    float4 edgesLRTB = float4( leftZ, rightZ, topZ, bottomZ ) - centerZ;
    float4 edgesLRTBSlopeAdjusted = edgesLRTB + edgesLRTB.yxwz;
    edgesLRTB = min( abs( edgesLRTB ), abs( edgesLRTBSlopeAdjusted ) );
    return saturate( ( 1.3 - edgesLRTB / (centerZ * 0.040) ) );
}
// Compute center pixel's viewspace normal from neighbours
float3 ComputeNormal( const float4 edgesLRTB, float3 pixCenterPos, float3 pixLPos, float3 pixRPos, float3 pixTPos, float3 pixBPos )
{
    float4 acceptedNormals  = float4( edgesLRTB.x*edgesLRTB.z, edgesLRTB.z*edgesLRTB.y, edgesLRTB.y*edgesLRTB.w, edgesLRTB.w*edgesLRTB.x );

    pixLPos = normalize(pixLPos - pixCenterPos);
    pixRPos = normalize(pixRPos - pixCenterPos);
    pixTPos = normalize(pixTPos - pixCenterPos);
    pixBPos = normalize(pixBPos - pixCenterPos);

    float3 pixelNormal = float3( 0, 0, -0.0005 );
    pixelNormal += ( acceptedNormals.x ) * cross( pixLPos, pixTPos );
    pixelNormal += ( acceptedNormals.y ) * cross( pixTPos, pixRPos );
    pixelNormal += ( acceptedNormals.z ) * cross( pixRPos, pixBPos );
    pixelNormal += ( acceptedNormals.w ) * cross( pixBPos, pixLPos );
    pixelNormal = normalize( pixelNormal );
    
    return pixelNormal;
}
float4 DebugDrawNormalsFromDepthPS( in float4 inPos : SV_Position ) : SV_Target0
{
    float viewspaceDepthC = NDCToViewDepth( g_DepthSource.Load( int3( inPos.xy, 0 ) ) );
    float viewspaceDepthL = NDCToViewDepth( g_DepthSource.Load( int3( inPos.xy, 0 ), int2( -1, 0 ) ) );
    float viewspaceDepthR = NDCToViewDepth( g_DepthSource.Load( int3( inPos.xy, 0 ), int2( 1,  0 ) ) );
    float viewspaceDepthT = NDCToViewDepth( g_DepthSource.Load( int3( inPos.xy, 0 ), int2( 0, -1 ) ) );
    float viewspaceDepthB = NDCToViewDepth( g_DepthSource.Load( int3( inPos.xy, 0 ), int2( 0,  1 ) ) );

    float4 edgesLRTB = ComputeEdgesFromDepth( viewspaceDepthC, viewspaceDepthL, viewspaceDepthR, viewspaceDepthT, viewspaceDepthB );

    float3 viewspacePosC  = NDCToViewspacePosition( inPos.xy + float2(  0.0,  0.0 ), viewspaceDepthC );
    float3 viewspacePosL  = NDCToViewspacePosition( inPos.xy + float2( -1.0,  0.0 ), viewspaceDepthL );
    float3 viewspacePosR  = NDCToViewspacePosition( inPos.xy + float2(  1.0,  0.0 ), viewspaceDepthR );
    float3 viewspacePosT  = NDCToViewspacePosition( inPos.xy + float2(  0.0, -1.0 ), viewspaceDepthT );
    float3 viewspacePosB  = NDCToViewspacePosition( inPos.xy + float2(  0.0,  1.0 ), viewspaceDepthB );

    float3 normal = ComputeNormal( edgesLRTB, viewspacePosC, viewspacePosL, viewspacePosR, viewspacePosT, viewspacePosB );

    return float4( DisplayNormalSRGB( normal ), 1.0 );
}

#endif

#ifdef VA_ZOOM_TOOL_SPECIFIC

cbuffer ZoomToolConstantsBuffer                      : register( B_CONCATENATER( ZOOMTOOL_CONSTANTSBUFFERSLOT ) )
{
    ZoomToolShaderConstants         g_zoomToolConstants;
}

#ifdef VA_ZOOM_TOOL_USE_UNORM_FLOAT
RWTexture2D<unorm float4>           g_screenTexture             : register( u0 );
#else
RWTexture2D<float4>                 g_screenTexture             : register( u0 );
#endif

bool IsInRect( float2 pt, float4 rect )
{
    return ( (pt.x >= rect.x) && (pt.x <= rect.z) && (pt.y >= rect.y) && (pt.y <= rect.w) );
}

void DistToClosestRectEdge( float2 pt, float4 rect, out float dist, out int edge )
{
    edge = 0;
    dist = 1e20;

    float distTmp;
    distTmp = abs( pt.x - rect.x );
    if( distTmp <= dist ) { dist = distTmp; edge = 2; }  // left

    distTmp = abs( pt.y - rect.y );
    if( distTmp <= dist ) { dist = distTmp; edge = 3; }  // top

    distTmp = abs( pt.x - rect.z ); 
    if( distTmp <= dist ) { dist = distTmp; edge = 0; }  // right

    distTmp = abs( pt.y - rect.w );
    if( distTmp <= dist ) { dist = distTmp; edge = 1; }  // bottom
}

float2 RectToRect( float2 pt, float2 srcRCentre, float2 srcRSize, float2 dstRCentre, float2 dstRSize )
{
    pt -= srcRCentre;
    pt /= srcRSize;

    pt *= dstRSize;
    pt += dstRCentre;
    
    return pt;
}

[numthreads(16, 16, 1)]
void ZoomToolCS( uint2 dispatchThreadID : SV_DispatchThreadID )
{
    const float2 screenPos = float2( dispatchThreadID ) + float2( 0.5, 0.5 ); // same as SV_Position

    const float zoomFactor = g_zoomToolConstants.ZoomFactor;

    float4 srcRect  = g_zoomToolConstants.SourceRectangle;
    float4 srcColor = g_screenTexture[dispatchThreadID];

    uint2 screenSizeUI;
    g_screenTexture.GetDimensions( screenSizeUI.x, screenSizeUI.y );

    const float2 screenSize     = float2(screenSizeUI);
    const float2 screenCenter   = float2(screenSizeUI) * 0.5;

    float2 srcRectSize = float2( srcRect.z - srcRect.x, srcRect.w - srcRect.y );
    float2 srcRectCenter = srcRect.xy + srcRectSize.xy * 0.5;

    float2 displayRectSize = srcRectSize * zoomFactor.xx;
    float2 displayRectCenter; 
    displayRectCenter.x = (srcRectCenter.x > screenCenter.x)?(srcRectCenter.x - srcRectSize.x * 0.5 - displayRectSize.x * 0.5 - 100):(srcRectCenter.x + srcRectSize.x * 0.5 + displayRectSize.x * 0.5 + 100);
    
    //displayRectCenter.y = (srcRectCenter.y > screenCenter.y)?(srcRectCenter.y - srcRectSize.y * 0.5 - displayRectSize.y * 0.5 - 50):(srcRectCenter.y + srcRectSize.y * 0.5 + displayRectSize.y * 0.5 + 50);
    displayRectCenter.y = lerp( displayRectSize.y/2, screenSize.y - displayRectSize.y/2, srcRectCenter.y / screenSize.y );
    
    float4 displayRect = float4( displayRectCenter.xy - displayRectSize.xy * 0.5, displayRectCenter.xy + displayRectSize.xy * 0.5 );

    bool chessPattern = (((uint)screenPos.x + (uint)screenPos.y) % 2) == 0;

    if( IsInRect(screenPos.xy, displayRect ) )
    {
        float2 texCoord = RectToRect( screenPos.xy, displayRectCenter, displayRectSize, srcRectCenter, srcRectSize );
        float2 cursorPosZoomed = g_globals.CursorViewportPosition.xy; //RectToRect( g_globals.CursorViewportPosition.xy+0.5, displayRectCenter, displayRectSize, srcRectCenter, srcRectSize );
        float3 colour = g_screenTexture.Load( int2( texCoord ) ).rgb;

        float crosshairThickness = 0.5f;
        if( abs(texCoord.x - cursorPosZoomed.x)<crosshairThickness || abs(texCoord.y - cursorPosZoomed.y)<crosshairThickness )
        {
            if( length(float2(cursorPosZoomed) - float2(texCoord)) > 1.5 )
                colour.rgb = saturate( float3( 2, 1, 1 ) - normalize(colour.rgb) );
        }

        {
            // draw destination box frame
            float dist; int edge;
            DistToClosestRectEdge( screenPos.xy, displayRect, dist, edge );

            if( dist < 1.1 )
            {
                g_screenTexture[dispatchThreadID] = float4( 1.0, 0.8, 0.8, dist < 1.1 );
                return;
            }
        }
       
        g_screenTexture[dispatchThreadID] = float4( colour, 1 );
        return;
    }

    srcRect.xy -= 1.0;
    srcRect.zw += 1.0;
    if( IsInRect(screenPos.xy, srcRect ) )
    {
        // draw source box frame
        float dist; int edge;
        DistToClosestRectEdge( screenPos.xy, srcRect, dist, edge );

        if( dist < 1.1 )
        {
            g_screenTexture[dispatchThreadID] = float4( 0.8, 1, 0.8, 1 ); // lerp( srcColor, float4( 0.8, 1, 0.8, 1 ) );
            return;
        }
    }

}
#endif


#ifdef VA_SMART_UPSCALE_SPECIFIC

#ifdef VA_SMART_UPSCALE_UNORM_FLOAT
RWTexture2D<unorm float4>       g_outputColor               : register( u0 );
#else
RWTexture2D<float4>             g_outputColor               : register( u0 );
#endif
Texture2D                       g_sourceColor               : register( t0 );
Texture2D                       g_sourceDepth               : register( t1 );

Texture2D                       g_outputDepth               : register( t2 );

// depth discontinuity filter from ASSAO - just as a reference
float4 CalculateEdges( const float centerZ, const float leftZ, const float rightZ, const float topZ, const float bottomZ )
{
    // slope-sensitive depth-based edge detection
    float4 edgesLRTB = float4( leftZ, rightZ, topZ, bottomZ ) - centerZ;
    float4 edgesLRTBSlopeAdjusted = edgesLRTB + edgesLRTB.yxwz;
    edgesLRTB = min( abs( edgesLRTB ), abs( edgesLRTBSlopeAdjusted ) );
    return saturate( ( 1.3 - edgesLRTB / (centerZ * 0.030) ) ); // (centerZ * 0.040) ) );

    // cheaper version but has artifacts
    // edgesLRTB = abs( float4( leftZ, rightZ, topZ, bottomZ ) - centerZ; );
    // return saturate( ( 1.3 - edgesLRTB / (pixZ * 0.06 + 0.1) ) );
}


[numthreads(16, 16, 1)]
void SmartUpscaleCS( uint2 dispatchThreadID : SV_DispatchThreadID )
{
    const float2 screenPos = float2( dispatchThreadID ) + float2( 0.5, 0.5 ); // same as SV_Position

    float2 sourceSize;
    g_sourceColor.GetDimensions( sourceSize.x, sourceSize.y );
    float2 dstSize;
    g_outputColor.GetDimensions( dstSize.x, dstSize.y );

    const float outputDepth = NDCToViewDepth( g_outputDepth.Load( int3(screenPos, 0) ).x );

    float2 uv = screenPos / dstSize;

#if 0
    //float4 srcColor = g_sourceColor.Load( int3(screenPos / 2, 0) ); //g_outputColor[dispatchThreadID];
    float4 srcColor = g_sourceColor.SampleLevel( g_samplerLinearWrap, uv, 0 );
#elif 1
    float2  lowResPosF  = screenPos / 2 - float2( 0.5, 0.5 );
    uint2   lowResPosUI = uint2( lowResPosF );
    float depth00  = NDCToViewDepth( g_sourceDepth.Load( uint3( lowResPosUI, 0 ), int2( 0, 0 ) ).x );
    float depth10  = NDCToViewDepth( g_sourceDepth.Load( uint3( lowResPosUI, 0 ), int2( 1, 0 ) ).x );
    float depth01  = NDCToViewDepth( g_sourceDepth.Load( uint3( lowResPosUI, 0 ), int2( 0, 1 ) ).x );
    float depth11  = NDCToViewDepth( g_sourceDepth.Load( uint3( lowResPosUI, 0 ), int2( 1, 1 ) ).x );
    float4 edges = CalculateEdges( outputDepth, depth00, depth10, depth01, depth11 );

    float3 col00  = g_sourceColor.Load( int3( lowResPosUI, 0 ), int2( 0, 0 ) ).rgb;
    float3 col10  = g_sourceColor.Load( int3( lowResPosUI, 0 ), int2( 1, 0 ) ).rgb;
    float3 col01  = g_sourceColor.Load( int3( lowResPosUI, 0 ), int2( 0, 1 ) ).rgb;
    float3 col11  = g_sourceColor.Load( int3( lowResPosUI, 0 ), int2( 1, 1 ) ).rgb;

    float2 intPt    = lowResPosUI;
    float2 fractPt  = lowResPosF-lowResPosUI;

    edges = saturate( edges + 0.01);  // make them never all zero!
    float w00 = edges.x * (1-fractPt.x)   * (1-fractPt.y);
    float w10 = edges.y * fractPt.x       * (1-fractPt.y);
    float w01 = edges.z * (1-fractPt.x)   * fractPt.y;
    float w11 = edges.w * fractPt.x       * fractPt.y;
    float sum = w00+w10+w01+w11;

    float4 srcColor = float4( w00 * col00 + w10 * col10 + w01 * col01 + w11 * col11, 0 ) / sum;

     //srcColor.rgba = 1.0 - float4( edges.x, edges.y * 0.5 + edges.w * 0.5, edges.z, 0.0 );

#else
    float4 srcColor = SampleBicubic9( g_sourceColor, g_samplerLinearWrap, uv );
#endif


    // do it before blending? yeah.
#ifdef VA_SMART_UPSCALE_UNORM_FLOAT
    srcColor.rgb = FLOAT3_to_SRGB( srcColor.rgb );
#endif

    g_outputColor[dispatchThreadID] = srcColor;
}

void SmartUpscaleDepthPS( in const float4 svpos : SV_Position, out float depth : SV_Depth ) //, out float4 color : SV_Target )
{
    uint2 coords = (uint2)svpos.xy * 2;
    //color = frac( g_sourceDepth.Load( uint3( coords, 0 ) ).x * 1000 );
    depth = g_sourceDepth.Load( uint3( coords, 0 ) ).x;
}

#endif
