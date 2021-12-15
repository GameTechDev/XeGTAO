///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __VA_DEBUGGING_HLSL__
#define __VA_DEBUGGING_HLSL__

#ifndef VA_COMPILED_AS_SHADER_CODE
#error not intended to be included outside of HLSL!
#endif

#include "vaSharedTypes.h"

// TODO: add a VA_SHADER_DEBUG_BUILD and #ifdef below? :)

// useful if one wants to draw global debug stuff just once for ex.
bool DebugOnce( )
{
    uint originalValue;
    InterlockedOr( g_shaderFeedbackStatic[0].OnceFlag, 1, originalValue );
    return !originalValue;
}

uint DebugCounter( )
{
    uint originalValue;
    InterlockedAdd( g_shaderFeedbackStatic[0].GenericCounter, 1, originalValue );
    return originalValue;
}

bool IsUnderCursor( float2 svPos )
{
    bool3 conditions = bool3( int2(svPos.xy) == int2(g_globals.CursorViewportPosition.xy), g_globals.CursorHoverItemCaptureEnabled );
    return all( conditions );
}

bool IsCursorClicked( ) // for different keys just add 'uint key'
{   uint key = 0;
    return (g_globals.CursorKeyClicked & (1U<<key)) != 0;
}

bool IsUnderCursorRange( float2 svPos, int2 range )
{
    bool3 conditions = bool3( int2(svPos.xy+range/2) >= int2(g_globals.CursorViewportPosition.xy) && int2(svPos.xy-(range+1)/2) < int2(g_globals.CursorViewportPosition.xy), g_globals.CursorHoverItemCaptureEnabled );
    return all( conditions );
}

// Don't forget [earlydepthstencil] :)
void ReportCursorInfo( const in ShaderInstanceConstants instance, const in int2 cursorPos, const in float3 worldspacePos )
{
#if 1
    const ShaderInstanceConstants instanceConsts = instance;

    [branch]
    if( IsUnderCursor(cursorPos) )
    {
        uint itemIndex = 0;

        InterlockedAdd( g_shaderFeedbackStatic[0].CursorHoverInfoCounter, 1, itemIndex );
        if( itemIndex < ShaderFeedbackStatic::MaxCursorHoverInfoItems )
        {
            CursorHoverInfo item;
            item.OriginInfo = instance.OriginInfo;
            
            // used to compute it all from NDC z, but switched to getting worldspace pos as input now
            //float4 pos;
            //pos.xy = ScreenToNDCSpaceXY( g_globals.CursorViewportPosition.xy );
            //pos.z = svPosition.z;
            //pos.w = 1.0;
            //pos = mul( g_globals.ViewProjInv, pos );
            //pos /= pos.w;
            //item.WorldspacePos  = pos.xyz;
            //item.ViewspaceDepth = NDCToViewDepth( svPosition.z );

            item.WorldspacePos  = worldspacePos;
            item.ViewspaceDepth = clamp( dot( g_globals.CameraDirection.xyz, worldspacePos - g_globals.CameraWorldPosition.xyz ), g_globals.CameraNearFar.x, g_globals.CameraNearFar.y );

            g_shaderFeedbackStatic[0].CursorHoverInfoItems[itemIndex] = item;
        }
    }
#endif
}

void DebugText( )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_LogTextNewLine;
    item.Ref0   = 0;
    item.Ref1   = float4( 0, 0, 0, 0 );
    item.Color  = 0;
    item.Param0 = item.Param1 = item.Param2 = 0;
    g_shaderFeedbackDynamic[index] = item;
}

void DebugText( uint value )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_LogTextUINT;
    item.Ref0   = 0;
    item.Ref1   = float4( asfloat(value), 0, 0, 0 );
    item.Color  = 0;
    item.Param0 = item.Param1 = item.Param2 = 0;
    g_shaderFeedbackDynamic[index] = item;
}

void DebugText( uint4 value )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_LogTextUINT4;
    item.Ref0   = 0;
    item.Ref1   = asfloat(value);
    item.Color  = 0;
    item.Param0 = item.Param1 = item.Param2 = 0;
    g_shaderFeedbackDynamic[index] = item;
}

void DebugText( float value )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_LogTextFLT;
    item.Ref0   = 0;
    item.Ref1   = float4( asfloat(value), 0, 0, 0 );
    item.Color  = 0;
    item.Param0 = item.Param1 = item.Param2 = 0;
    g_shaderFeedbackDynamic[index] = item;
}

void DebugText( float2 value )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_LogTextFLT2;
    item.Ref0   = 0;
    item.Ref1   = float4( value, 0, 0 );
    item.Color  = 0;
    item.Param0 = item.Param1 = item.Param2 = 0;
    g_shaderFeedbackDynamic[index] = item;
}

void DebugText( float3 value )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_LogTextFLT3;
    item.Ref0   = 0;
    item.Ref1   = float4( value, 0 );
    item.Color  = 0;
    item.Param0 = item.Param1 = item.Param2 = 0;
    g_shaderFeedbackDynamic[index] = item;
}

void DebugText( float4 value )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_LogTextFLT4;
    item.Ref0   = 0;
    item.Ref1   = value;
    item.Color  = 0;
    item.Param0 = item.Param1 = item.Param2 = 0;
    g_shaderFeedbackDynamic[index] = item;
}

void DebugDraw2DLine( float2 screenPosFrom, float2 screenPosTo, float4 color )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_2DLine;
    item.Ref0   = float4( screenPosFrom, 0, 0 );
    item.Ref1   = float4( screenPosTo, 0, 0 );
    item.Color  = color;
    item.Param0 = item.Param1 = item.Param2 = 0;
    g_shaderFeedbackDynamic[index] = item;
}

void DebugDraw2DCircle( float2 screenPos, float radius, float4 color )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_2DCircle;
    item.Ref0   = float4( screenPos, radius, 0 );
    item.Ref1   = float4( 0, 0, 0, 0 );
    item.Color  = color;
    item.Param0 = item.Param1 = item.Param2 = 0;
    g_shaderFeedbackDynamic[index] = item;
}

void DebugDraw2DRectangle( float2 topLeft, float2 bottomRight, float4 color )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_2DRectangle;
    item.Ref0   = float4( topLeft, 0, 0 );
    item.Ref1   = float4( bottomRight, 0, 0 );
    item.Color  = color;
    item.Param0 = item.Param1 = item.Param2 = 0;
    g_shaderFeedbackDynamic[index] = item;
}

void DebugDraw2DText( float2 topLeft, float4 color, uint val )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_2DTextUINT;
    item.Ref0   = float4( topLeft, 0, 0 );
    item.Ref1   = float4( asfloat(val), 0, 0, 0 );
    item.Color  = color;
    item.Param0 = item.Param1 = item.Param2 = 0;
    g_shaderFeedbackDynamic[index] = item;
}
void DebugDraw2DText( float2 topLeft, float4 color, uint4 val )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_2DTextUINT4;
    item.Ref0   = float4( topLeft, 0, 0 );
    item.Ref1   = asfloat(val);
    item.Color  = color;
    item.Param0 = item.Param1 = item.Param2 = 0;
    g_shaderFeedbackDynamic[index] = item;
}
void DebugDraw2DText( float2 topLeft, float4 color, float val )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_2DTextFLT;
    item.Ref0   = float4( topLeft, 0, 0 );
    item.Ref1   = float4( val, 0, 0, 0 );
    item.Color  = color;
    item.Param0 = item.Param1 = item.Param2 = 0;
    g_shaderFeedbackDynamic[index] = item;
}
void DebugDraw2DText( float2 topLeft, float4 color, float4 val )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_2DTextFLT4;
    item.Ref0   = float4( topLeft, 0, 0 );
    item.Ref1   = val;
    item.Color  = color;
    item.Param0 = item.Param1 = item.Param2 = 0;
    g_shaderFeedbackDynamic[index] = item;
}
void DebugDraw3DText( float3 worldPos, float2 screenSpaceOffset, float4 color, uint val )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_3DTextUINT;
    item.Ref0   = float4( worldPos, 1 );
    item.Ref1   = float4( asfloat(val), 0, 0, 0 );
    item.Color  = color;
    item.Param0 = 0; 
    item.Param1 = screenSpaceOffset.x;
    item.Param2 = screenSpaceOffset.y;
    g_shaderFeedbackDynamic[index] = item;
}
void DebugDraw3DText( float3 worldPos, float2 screenSpaceOffset, float4 color, uint4 val )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_3DTextUINT4;
    item.Ref0   = float4( worldPos, 1 );
    item.Ref1   = asfloat(val);
    item.Color  = color;
    item.Param0 = 0; 
    item.Param1 = screenSpaceOffset.x;
    item.Param2 = screenSpaceOffset.y;
    g_shaderFeedbackDynamic[index] = item;
}
void DebugDraw3DText( float3 worldPos, float2 screenSpaceOffset, float4 color, float val )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_3DTextFLT;
    item.Ref0   = float4( worldPos, 1 );
    item.Ref1   = float4( val, 0, 0, 0 );
    item.Color  = color;
    item.Param0 = 0; 
    item.Param1 = screenSpaceOffset.x;
    item.Param2 = screenSpaceOffset.y;
    g_shaderFeedbackDynamic[index] = item;
}
void DebugDraw3DText( float3 worldPos, float2 screenSpaceOffset, float4 color, float4 val )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_3DTextFLT4;
    item.Ref0   = float4( worldPos, 1 );
    item.Ref1   = val;
    item.Color  = color;
    item.Param0 = 0; 
    item.Param1 = screenSpaceOffset.x;
    item.Param2 = screenSpaceOffset.y;
    g_shaderFeedbackDynamic[index] = item;
}

void DebugDraw3DLine( float3 worldPosFrom, float3 worldPosTo, float4 color )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_3DLine;
    item.Ref0   = float4( g_globals.WorldBase.xyz + worldPosFrom, 0 );
    item.Ref1   = float4( g_globals.WorldBase.xyz + worldPosTo, 0 );
    item.Color  = color;
    item.Param0 = item.Param1 = item.Param2 = 0;
    g_shaderFeedbackDynamic[index] = item;
}

void DebugDraw3DSphere( float3 worldPos, float worldSize, float4 color )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_3DSphere;
    item.Ref0   = float4( g_globals.WorldBase.xyz + worldPos, worldSize );
    item.Ref1   = float4( 0, 0, 0, 0 );
    item.Color  = color;
    item.Param0 = item.Param1 = item.Param2 = 0;
    g_shaderFeedbackDynamic[index] = item;
}

void DebugDraw3DBox( float3 worldPosMin, float3 worldPosMax, float4 color )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_3DBox;
    item.Ref0   = float4( g_globals.WorldBase.xyz + worldPosMin, 0 );
    item.Ref1   = float4( g_globals.WorldBase.xyz + worldPosMax, 0 );
    item.Color  = color;
    item.Param0 = item.Param1 = item.Param2 = 0;
    g_shaderFeedbackDynamic[index] = item;
}

void DebugDraw3DCylinder( float3 worldPosFrom, float3 worldPosTo, float worldFromRadius, float worldToRadius, float4 color )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_3DCylinder;
    item.Ref0   = float4( g_globals.WorldBase.xyz + worldPosFrom, worldFromRadius );
    item.Ref1   = float4( g_globals.WorldBase.xyz + worldPosTo,   worldToRadius );
    item.Color  = color;
    item.Param0 = item.Param1 = item.Param2 = 0;
    g_shaderFeedbackDynamic[index] = item;
}

void DebugDraw3DArrow( float3 worldPosFrom, float3 worldPosTo, float worldRadius, float4 color )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_3DArrow;
    item.Ref0   = float4( g_globals.WorldBase.xyz + worldPosFrom, worldRadius );
    item.Ref1   = float4( g_globals.WorldBase.xyz + worldPosTo, 0);
    item.Color  = color;
    item.Param0 = item.Param1 = item.Param2 = 0;
    g_shaderFeedbackDynamic[index] = item;
}

// this isn't really too useful
void DebugDraw3DCone( float3 worldPosFrom, float3 worldPosTo, float halfAngle, float4 color )
{
    DebugDraw3DCylinder( worldPosFrom, worldPosTo, 0, length(worldPosTo-worldPosFrom) * tan( halfAngle ), color );
}

void DebugDraw3DSphereCone( float3 worldCenter, float3 worldDirection, float worldRadius, float halfAngle, float4 color )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_3DSphereCone;
    item.Ref0   = float4( g_globals.WorldBase.xyz + worldCenter, worldRadius );
    item.Ref1   = float4( worldDirection, halfAngle );
    item.Color  = color;
    item.Param0 = item.Param1 = item.Param2 = 0;
    g_shaderFeedbackDynamic[index] = item;
}

// coneInnerAngle/coneOuterAngle are half-angles!
void DebugDraw3DLightViz( float3 worldCenter, float3 worldDirection, float size, float range, float coneInnerAngle, float coneOuterAngle, float3 color )
{
    int index = 0; InterlockedAdd( g_shaderFeedbackStatic[0].DynamicItemCounter, 1, index );
    if( index >= ShaderFeedbackDynamic::MaxItems )
        return;
    ShaderFeedbackDynamic item;
    item.Type   = ShaderFeedbackDynamic::Type_3DLightViz;
    item.Ref0   = float4( g_globals.WorldBase.xyz + worldCenter, size );
    item.Ref1   = float4( worldDirection, range );
    item.Color  = float4( color, 0 );
    item.Param0 = 0;
    item.Param1 = coneInnerAngle;
    item.Param2 = coneOuterAngle;
    g_shaderFeedbackDynamic[index] = item;
}

void DebugAssert( bool assertVal, uint payloadU, float payloadF )
{
    if( !assertVal )
    {
        uint originalValue;
        InterlockedOr( g_shaderFeedbackStatic[0].AssertFlag, 1, originalValue );
        if( !originalValue )
        {
            g_shaderFeedbackStatic[0].AssertPayloadUINT     = payloadU;
            g_shaderFeedbackStatic[0].AssertPayloadFLOAT    = payloadF;
        }
    }
}

void DebugAssert( bool assertVal, uint payloadU )       { DebugAssert( assertVal, payloadU, 0 ); }
void DebugAssert( bool assertVal, float payloadF )      { DebugAssert( assertVal, 0 , payloadF ); }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif // __VA_DEBUGGING_HLSL__