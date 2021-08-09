///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Rendering/Effects/vaSky.h"

using namespace Vanilla;

vaSky::vaSky( const vaRenderingModuleParams & params ) : vaRenderingModule( params.RenderDevice ),
m_vertexShader( params.RenderDevice ),
m_pixelShader( params.RenderDevice ),
// m_screenTriangleVertexBuffer( params.RenderDevice ),
// m_screenTriangleVertexBufferReversedZ( params.RenderDevice ),
m_constantsBuffer( params.RenderDevice )
{ 
//    assert( vaRenderingCore::IsInitialized() );

    m_sunDir = vaVector3( 0.0f, 0.0f, 0.0f );
    m_sunDirTargetL0 = vaVector3( 0.0f, 0.0f, 0.0f );
    m_sunDirTargetL1 = vaVector3( 0.0f, 0.0f, 0.0f );

    m_settings.SunAzimuth           = 0.320f;//0.0f / 180.0f * (float)VA_PI;
    m_settings.SunElevation         = 15.0f / 180.0f * (float)VA_PI;
    m_settings.SkyColorLowPow       = 6.0f;
    m_settings.SkyColorLowMul       = 1.0f;
    m_settings.SkyColorLow          = vaVector4( 0.4f, 0.4f, 0.9f, 0.0f );
    m_settings.SkyColorHigh         = vaVector4( 0.0f, 0.0f, 0.6f, 0.0f );
    m_settings.SunColorPrimary      = vaVector4( 1.0f, 1.0f, 0.9f, 0.0f );
    m_settings.SunColorSecondary    = vaVector4( 1.0f, 1.0f, 0.7f, 0.0f );
    m_settings.SunColorPrimaryPow   = 500.0f;
    m_settings.SunColorPrimaryMul   = 2.5f;
    m_settings.SunColorSecondaryPow = 5.0;
    m_settings.SunColorSecondaryMul = 0.2f;

    m_settings.FogColor             = vaVector3( 0.4f, 0.4f, 0.9f );
    m_settings.FogDistanceMin       = 100.0f;
    m_settings.FogDensity           = 0.0007f;

    std::vector<vaVertexInputElementDesc> inputElements;
    inputElements.push_back( { "SV_Position", 0, vaResourceFormat::R32G32B32_FLOAT, 0, vaVertexInputElementDesc::AppendAlignedElement, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );

    m_vertexShader->CreateShaderAndILFromFile( "vaSky.hlsl", "SimpleSkyboxVS", inputElements, vaShaderMacroContaner{}, false );
    m_pixelShader->CreateShaderFromFile( "vaSky.hlsl", "SimpleSkyboxPS", vaShaderMacroContaner{}, false );

    /*
    // Create screen triangle vertex buffer
    {
        const float skyFarZ = 1.0f;
        vaVector3 screenTriangle[4];
        screenTriangle[0] = vaVector3( -1.0f,  1.0f, skyFarZ );
        screenTriangle[1] = vaVector3(  1.0f,  1.0f, skyFarZ );
        screenTriangle[2] = vaVector3( -1.0f, -1.0f, skyFarZ );
        screenTriangle[3] = vaVector3(  1.0f, -1.0f, skyFarZ );

        m_screenTriangleVertexBuffer = vaDynamicVertexBuffer::Create<vaVector3>( GetRenderDevice(), _countof(screenTriangle), screenTriangle );
    }

    // Create screen triangle vertex buffer
    {
        const float skyFarZ = 0.0f;
        vaVector3 screenTriangle[4];
        screenTriangle[0] = vaVector3( -1.0f,  1.0f, skyFarZ );
        screenTriangle[1] = vaVector3(  1.0f,  1.0f, skyFarZ );
        screenTriangle[2] = vaVector3( -1.0f, -1.0f, skyFarZ );
        screenTriangle[3] = vaVector3(  1.0f, -1.0f, skyFarZ );

        m_screenTriangleVertexBufferReversedZ = vaDynamicVertexBuffer::Create<vaVector3>( GetRenderDevice( ), _countof( screenTriangle ), screenTriangle );
    }*/
}

vaSky::~vaSky( )
{
}

// #ifdef VA_ANT_TWEAK_BAR_ENABLED   
// void vaSky::OnAntTweakBarInitialized( TwBar * mainBar )
// {
//     //m_debugBar = mainBar;
// 
//     if( mainBar == NULL )
//         return;
// 
//     // Create a new TwType to edit 3D points: a struct that contains two floats
//     TwStructMember structDef[] = {
//         { "Sun Azimuth",                TW_TYPE_FLOAT,      offsetof( Settings, SunAzimuth ),           " Min=0.0 Max=6.2831853     Step=0.01   Precision = 3" },
//         { "Sun Elevation",              TW_TYPE_FLOAT,      offsetof( Settings, SunElevation ),         " Min=-1.5708 Max=1.5708    Step=0.02   Precision = 3" },
//         { "Sky Colour High",            TW_TYPE_COLOR3F,    offsetof( Settings, SkyColorHigh ),         "" },
//         { "Sky Colour Low",             TW_TYPE_COLOR3F,    offsetof( Settings, SkyColorLow ),          "" },
//         { "Sun Colour Primary",         TW_TYPE_COLOR3F,    offsetof( Settings, SunColorPrimary ),      "" },
//         { "Sun Colour Secondary",       TW_TYPE_COLOR3F,    offsetof( Settings, SunColorSecondary ),    "" },
//         { "Sky Colour Low Pow",         TW_TYPE_FLOAT,      offsetof( Settings, SkyColorLowPow ),       "" },
//         { "Sky Colour Low Mul",         TW_TYPE_FLOAT,      offsetof( Settings, SkyColorLowMul ),       "" },
//         { "Sun Colour Primary Pow",     TW_TYPE_FLOAT,      offsetof( Settings, SunColorPrimaryPow ),   "" },
//         { "Sun Colour Primary Mul",     TW_TYPE_FLOAT,      offsetof( Settings, SunColorPrimaryMul ),   "" },
//         { "Sun Colour Secondary Pow",   TW_TYPE_FLOAT,      offsetof( Settings, SunColorSecondaryPow ), "" },
//         { "Sun Colour Secondary Mul",   TW_TYPE_FLOAT,      offsetof( Settings, SunColorSecondaryMul ), "" },
//         { "Fog Colour",                 TW_TYPE_COLOR3F,    offsetof( Settings, FogColor ),             "" },
//         { "FogMin",                     TW_TYPE_FLOAT,      offsetof( Settings, FogDistanceMin ),       " Min=0.0 Max=32768     Step=1.00       Precision = 2" },
//         { "FogDensity",                 TW_TYPE_FLOAT,      offsetof( Settings, FogDensity ),           " Min=0.0 Max=10        Step=0.0001     Precision = 4" },
//     };
// 
//     TwType TwSkySettings = TwDefineStruct( "SKYSETTINGS", structDef, _countof( structDef ), sizeof( Settings ), NULL, NULL );
// 
//     TwAddVarRW( mainBar, "Sky", TwSkySettings, &m_settings, "" );
// }
// #endif

void vaSky::Tick( float deltaTime, vaSceneLighting * lightingToUpdate )
{
    // this smoothing is not needed here, but I'll leave it in anyway
    static float someValue = 10000000.0f;
    float lf = vaMath::TimeIndependentLerpF( deltaTime, someValue );

    vaVector3 sunDirTargetL0    = m_sunDirTargetL0;
    vaVector3 sunDirTargetL1    = m_sunDirTargetL1;
    vaVector3 sunDir            = m_sunDir;

    if( sunDir.x < 1e-5f )
        lf = 1.0f;

    vaMatrix4x4 mCameraRot;
    vaMatrix4x4 mRotationY = vaMatrix4x4::RotationY( m_settings.SunElevation );
    vaMatrix4x4 mRotationZ = vaMatrix4x4::RotationZ( m_settings.SunAzimuth );
    mCameraRot = mRotationY * mRotationZ;
    sunDirTargetL0 = -mCameraRot.GetRotationX();

    sunDirTargetL1 = vaMath::Lerp( sunDirTargetL1, sunDirTargetL0, lf );
    sunDir = vaMath::Lerp( sunDir, sunDirTargetL1, lf );

    sunDirTargetL0 = sunDirTargetL0.Normalized();
    sunDirTargetL1 = sunDirTargetL1.Normalized();
    sunDir = sunDir.Normalized();

    m_sunDirTargetL0= sunDirTargetL0;
    m_sunDirTargetL1= sunDirTargetL1;
    m_sunDir        = sunDir;

    vaVector3 ambientLightColor = vaVector3( 0.1f, 0.1f, 0.1f );
    
    if( lightingToUpdate != nullptr )
    {
        assert( false );
        // lightingToUpdate->SetDirectionalLightDirection( -m_sunDir );
        // lightingToUpdate->SetFogParams( m_settings.FogColor, m_settings.FogDistanceMin, m_settings.FogDensity );
    }
}

void vaSky::Draw( vaDrawAttributes & drawAttributes )
{
    drawAttributes;
    assert( false );
}