///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Core/vaCoreIncludes.h"
#include "Core/vaApplicationBase.h"
#include "Core/vaInput.h"

#include "Scene/vaCameraControllers.h"
#include "Scene/vaAssetImporter.h"

#include "Rendering/vaRenderCamera.h"
#include "Rendering/vaShader.h"
#include "Rendering/vaRenderMesh.h"
#include "Rendering/vaRenderMaterial.h"
#include "Rendering/vaDebugCanvas.h"
#include "Rendering/vaRenderGlobals.h"
#include "Rendering/vaSceneLighting.h"
#include "Rendering/Effects/vaSkybox.h"
#include "Rendering/Effects/vaPostProcessTonemap.h"
#include "Rendering/Misc/vaZoomTool.h"
#include "Rendering/Misc/vaImageCompareTool.h"
#include "Rendering/Effects/vaGTAO.h"

#include "Rendering/vaSceneRenderer.h"
#include "Rendering/vaSceneMainRenderView.h"

#include "IntegratedExternals/vaImguiIntegration.h"

#include <random>

using namespace Vanilla;

void Sample00_BlueScreen( vaRenderDevice & renderDevice, vaApplicationBase & application, float deltaTime, vaApplicationState applicationState )
{
    if( applicationState != vaApplicationState::Running )
        return;

    application.TickUI();

    // Do the rendering tick and present 
    renderDevice.BeginFrame( deltaTime );

    renderDevice.GetCurrentBackbufferTexture()->ClearRTV( *renderDevice.GetMainContext(), vaVector4( 0.0f, 0.0f, 1.0f, 1.0f ) );

    application.DrawUI( *renderDevice.GetMainContext( ), renderDevice.GetCurrentBackbuffer( ), nullptr );

    renderDevice.EndAndPresentFrame( (application.GetVsync())?(1):(0) );
}

void Sample01_FullscreenPass( vaRenderDevice & renderDevice, vaApplicationBase & application, float deltaTime, vaApplicationState applicationState )
{
    static shared_ptr<vaPixelShader>    pixelShader;
    if( applicationState == vaApplicationState::Initializing )
    {
        pixelShader = vaPixelShader::CreateFromBuffer( renderDevice, 
            "float4 main( in const float4 xPos : SV_Position ) : SV_Target          \n"
            "{                                                                      \n"
            "   return float4( frac(xPos.x / 256.0), frac(xPos.y / 256.0), 0, 1 );  \n"
            "}                                                                      \n"
            , "main", {}, true );
    }
    else if( applicationState == vaApplicationState::ShuttingDown )
    {
        pixelShader = nullptr;  // to avoid memory leak warnings
    }
    else if( applicationState == vaApplicationState::Running )
    {
        application.TickUI( );

        // Do the rendering tick and present 
        renderDevice.BeginFrame( deltaTime );

        // no need to clear, we're doing a fullscreen pass
        // renderDevice.GetCurrentBackbufferTexture()->ClearRTV( *renderDevice.GetMainContext(), vaVector4( 0.0f, 0.0f, 1.0f, 1.0f ) );

        vaGraphicsItem renderItem;                                  // everything needed for one draw call
        renderDevice.FillFullscreenPassGraphicsItem( renderItem );  // fill in full screen draw call stuff like vertex shader and full screen vertex buffer
        renderItem.PixelShader = pixelShader;                       // our pixel shader
        renderDevice.GetMainContext()->ExecuteSingleItem( renderItem, renderDevice.GetCurrentBackbuffer(), nullptr );   // draw to current backbuffer!

        // Draw ImGUI stuff
        application.DrawUI( *renderDevice.GetMainContext( ), renderDevice.GetCurrentBackbuffer(), nullptr );

        // Present and sync on vsync if required
        renderDevice.EndAndPresentFrame( (application.GetVsync())?(1):(0) );
    }
}

void Sample02_JustATriangle( vaRenderDevice & renderDevice, vaApplicationBase & application, float deltaTime, vaApplicationState applicationState )
{
    static shared_ptr<vaVertexShader>   vertexShader;
    static shared_ptr<vaRenderBuffer>   vertexBuffer;
    static shared_ptr<vaPixelShader>    pixelShader;
    if( applicationState == vaApplicationState::Initializing )
    {
        // vertex shader
        vertexShader = renderDevice.CreateModule<vaVertexShader>(); // get the platform dependent object
        std::vector<vaVertexInputElementDesc> inputElements = { 
            { "SV_Position", 0,    vaResourceFormat::R32G32B32A32_FLOAT,    0,  0, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 }, 
            { "TEXCOORD", 0,       vaResourceFormat::R32G32_FLOAT,          0, 16, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } };
        vertexShader->CompileVSAndILFromBuffer( 
            "void main( inout const float4 xPos : SV_Position, inout float2 UV : TEXCOORD0 ) { }"
            , "main", inputElements, vaShaderMacroContaner(), true );

        // pixel shader
        pixelShader = renderDevice.CreateModule<vaPixelShader>(); // get the platform dependent object
        pixelShader->CompileFromBuffer( 
            "Texture2D g_source           : register( t0 );                                                 \n"
            "float4 main( in const float4 xPos : SV_Position, in const float2 UV : TEXCOORD0  ) : SV_Target \n"
            "{                                                                                              \n"
            "   return float4( UV.x, UV.y, 0, 1 );                                                          \n"
            "}                                                                                              \n"
            , "main", vaShaderMacroContaner(), true );

        // vertex buffer
        struct SimpleVertex
        {
            float   Position[4];
            float   UV[2];
        };
        SimpleVertex triangleVerts[3] = {
            {  0.0f,   0.3f, 0.0f, 1.0f, 0.0f, 0.0f },
            {  0.25f, -0.2f, 0.0f, 1.0f, 2.0f, 0.0f },
            { -0.25f, -0.2f, 0.0f, 1.0f, 0.0f, 2.0f },
        };
        vertexBuffer = vaRenderBuffer::Create<SimpleVertex>( renderDevice, _countof(triangleVerts), vaRenderBufferFlags::VertexIndexBuffer, "vertices", triangleVerts );
        return;
    }
    else if( applicationState == vaApplicationState::ShuttingDown )
    {
        pixelShader = nullptr;
        vertexShader = nullptr;
        vertexBuffer = nullptr;
        return;
    }
    assert( applicationState == vaApplicationState::Running );

    application.TickUI( );
    
    // Do the rendering tick and present 
    renderDevice.BeginFrame( deltaTime );

    renderDevice.GetCurrentBackbufferTexture()->ClearRTV( *renderDevice.GetMainContext(), vaVector4( 0.8f, 0.8f, 0.9f, 1.0f ) );

    vaGraphicsItem renderItem;
    renderItem.Topology         = vaPrimitiveTopology::TriangleList;
    renderItem.VertexShader     = vertexShader;
    renderItem.VertexBuffer     = vertexBuffer;
    renderItem.PixelShader      = pixelShader;
    renderItem.DrawType         = vaGraphicsItem::DrawType::DrawSimple;
    renderItem.DrawSimpleParams.VertexCount = 3;
    //renderItem.ShadingRate      = vaShadingRate::ShadingRate2X4;

    renderDevice.GetMainContext()->ExecuteSingleItem( renderItem, renderDevice.GetCurrentBackbuffer(), nullptr );

    // update and draw imgui
    application.DrawUI( *renderDevice.GetMainContext( ), renderDevice.GetCurrentBackbuffer(), nullptr );

    // present the frame, flip the buffers, etc
    renderDevice.EndAndPresentFrame( (application.GetVsync())?(1):(0) );
}

void Sample03_TexturedTriangle( vaRenderDevice & renderDevice, vaApplicationBase & application, float deltaTime, vaApplicationState applicationState )
{
    static shared_ptr<vaVertexShader>   vertexShader;
    static shared_ptr<vaRenderBuffer>   vertexBuffer;
    static shared_ptr<vaPixelShader>    pixelShader;
    static shared_ptr<vaTexture>        texture;

    if( applicationState == vaApplicationState::Initializing )
    {
        // vertex shader
        vertexShader = renderDevice.CreateModule<vaVertexShader>(); // get the platform dependent object
        std::vector<vaVertexInputElementDesc> inputElements = { 
            { "SV_Position", 0,    vaResourceFormat::R32G32B32A32_FLOAT,    0,  0, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 }, 
            { "TEXCOORD", 0,       vaResourceFormat::R32G32_FLOAT,          0, 16, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } };
        vertexShader->CompileVSAndILFromBuffer( 
            "void main( inout const float4 xPos : SV_Position, inout float2 UV : TEXCOORD0 ) { }"
            , "main", inputElements, vaShaderMacroContaner(), true );

        // pixel shader
        pixelShader = renderDevice.CreateModule<vaPixelShader>(); // get the platform dependent object
        pixelShader->CompileFromBuffer( 
            "#include \"vaShared.hlsl\"                                                                     \n" // this defines g_samplerLinearClamp and bunch of other stuff
            "Texture2D g_source           : register( t0 );                                                 \n"
            "float4 main( in const float4 xPos : SV_Position, in const float2 UV : TEXCOORD0  ) : SV_Target \n"
            "{                                                                                              \n"
            "   return g_source.Sample( g_samplerPointClamp, UV );                                         \n"
            "}                                                                                              \n"
            , "main", vaShaderMacroContaner(), true );

        // vertex buffer
        struct SimpleVertex
        {
            float   Position[4];
            float   UV[2];
        };
        SimpleVertex triangleVerts[3] = {
            {  0.0f,   0.3f, 0.0f, 1.0f, 0.0f, 0.0f },
            {  0.25f, -0.2f, 0.0f, 1.0f, 1.0f, 0.0f },
            { -0.25f, -0.2f, 0.0f, 1.0f, 0.0f, 1.0f },
        };
        vertexBuffer = vaRenderBuffer::Create<SimpleVertex>( renderDevice, _countof(triangleVerts), vaRenderBufferFlags::VertexIndexBuffer, "vertices", triangleVerts );
        // texture
        {
            uint32 initialData[16*16];
            for( int y = 0; y < 16; y++ )
                for( int x = 0; x < 16; x++ )
                    initialData[ y * 16 + x ] = (((x+y)%2) == 0)?(0xFFFFFFFF):(0x00000000);
            texture = vaTexture::Create2D( renderDevice, vaResourceFormat::R8G8B8A8_UNORM, 16, 16, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource, vaResourceAccessFlags::Default, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::None, vaTextureContentsType::GenericColor, initialData, 16*sizeof( uint32 ) );
        }
        return;
    }
    else if( applicationState == vaApplicationState::ShuttingDown )
    {
        pixelShader = nullptr;
        vertexShader = nullptr;
        vertexBuffer = nullptr;
        texture = nullptr;
        return;
    }
    assert( applicationState == vaApplicationState::Running );

    application.TickUI( );   
    
    // Do the rendering tick and present 
    renderDevice.BeginFrame( deltaTime );

    renderDevice.GetCurrentBackbufferTexture()->ClearRTV( *renderDevice.GetMainContext(), vaVector4( 0.3f, 0.5f, 0.9f, 1.0f ) );

    vaGraphicsItem renderItem;
    renderItem.Topology         = vaPrimitiveTopology::TriangleList;
    renderItem.VertexShader     = vertexShader;
    renderItem.VertexBuffer     = vertexBuffer;
    renderItem.PixelShader      = pixelShader;
    renderItem.DrawType         = vaGraphicsItem::DrawType::DrawSimple;
    renderItem.DrawSimpleParams.VertexCount = 3;
    renderItem.ShaderResourceViews[0] = texture;

    vaDrawResultFlags res = renderDevice.GetMainContext()->ExecuteSingleItem( renderItem, renderDevice.GetCurrentBackbuffer(), nullptr );
    res;

    // update and draw imgui

    application.DrawUI( *renderDevice.GetMainContext( ), renderDevice.GetCurrentBackbuffer(), nullptr );

    // present the frame, flip the buffers, etc
    renderDevice.EndAndPresentFrame( (application.GetVsync())?(1):(0) );
}

void Sample04_ConstantBuffer( vaRenderDevice & renderDevice, vaApplicationBase & application, float deltaTime, vaApplicationState applicationState )
{
    static shared_ptr<vaVertexShader>   vertexShader;
    static shared_ptr<vaRenderBuffer>   vertexBuffer;
    static shared_ptr<vaRenderBuffer>   indexBuffer;
    static shared_ptr<vaPixelShader>    pixelShader;
    static shared_ptr<vaTexture>        texture;

    struct ShaderConstants
    {
        float UVOffset[2];
        float AspectRatio;
        float SomethingElse;
    };
    static shared_ptr<vaConstantBuffer> constantBuffer;

    if( applicationState == vaApplicationState::Initializing )
    {
        // vertex shader
        vertexShader = renderDevice.CreateModule<vaVertexShader>(); // get the platform dependent object
        std::vector<vaVertexInputElementDesc> inputElements = { 
            { "SV_Position", 0,    vaResourceFormat::R32G32B32A32_FLOAT,    0,  0, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 }, 
            { "TEXCOORD", 0,       vaResourceFormat::R32G32_FLOAT,          0, 16, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } };
        vertexShader->CompileVSAndILFromBuffer( 
            "struct ShaderConstants{ float2 UVOffset; float AspectRatio; float SomethingElse; };                           \n"
            "cbuffer Sample04Globals : register(b0) { ShaderConstants g_consts ; }                                        \n"
            "void main( inout float4 xPos : SV_Position, inout float2 UV : TEXCOORD0 ) { xPos *= float4( 1, g_consts.AspectRatio, 1, 1 ); }   \n"
            , "main", inputElements, vaShaderMacroContaner(), true );

        // pixel shader
        pixelShader = renderDevice.CreateModule<vaPixelShader>(); // get the platform dependent object
        pixelShader->CompileFromBuffer( 
            "#include \"vaShared.hlsl\"                                                                             \n" // this defines g_samplerLinearClamp and bunch of other stuff
            "struct ShaderConstants{ float2 UVOffset; float AspectRatio; float SomethingElse; };           \n"
            "cbuffer Sample04Globals : register(b0) { ShaderConstants g_consts ; }                        \n"
            "Texture2D g_source           : register( t0 );                                                         \n"
            "float4 main( in const float4 xPos : SV_Position, in const float2 UV : TEXCOORD0  ) : SV_Target         \n"
            "{                                                                                                      \n"
            "   return g_source.Sample( g_samplerPointWrap, UV+g_consts.UVOffset );                                \n"
            "}                                                                                                      \n"
            , "main", vaShaderMacroContaner(), true );

        // vertex buffer
        struct SimpleVertex
        {
            float   Position[4];
            float   UV[2];
        };
        SimpleVertex triangleVerts[3] = {
            {  0.0f,   0.141f, 0.0f, 1.0f, 0.0f, 0.0f },
            {  0.2f,  -0.141f, 0.0f, 1.0f, 1.0f, 0.0f },
            { -0.2f,  -0.141f, 0.0f, 1.0f, 0.0f, 1.0f },
        };
        vertexBuffer = vaRenderBuffer::Create<SimpleVertex>( renderDevice, _countof(triangleVerts), vaRenderBufferFlags::VertexIndexBuffer, "vertices", triangleVerts );

        uint32 indices[3] = { 0, 2, 1 };
        indexBuffer = vaRenderBuffer::Create( renderDevice, _countof(indices), vaResourceFormat::R32_UINT, vaRenderBufferFlags::VertexIndexBuffer, "indices", indices );

        // texture
        {
            uint32 initialData[16*16];
            for( int y = 0; y < 16; y++ )
                for( int x = 0; x < 16; x++ )
                    initialData[ y * 16 + x ] = (((x+y)%2) == 0)?(0xFFFFFFFF):(0x00000000);
            texture = vaTexture::Create2D( renderDevice, vaResourceFormat::R8G8B8A8_UNORM, 16, 16, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource, vaResourceAccessFlags::Default, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::None, vaTextureContentsType::GenericColor, initialData, 16*sizeof( uint32 ) );
        }
        constantBuffer = vaConstantBuffer::Create<ShaderConstants>( renderDevice, "constants" );
        return;
    }
    else if( applicationState == vaApplicationState::ShuttingDown )
    {
        pixelShader = nullptr;
        vertexShader = nullptr;
        vertexBuffer = nullptr;
        indexBuffer = nullptr;
        texture = nullptr;
        constantBuffer = nullptr;
        return;
    }
    assert( applicationState == vaApplicationState::Running );

    application.TickUI( );
    
    // Do the rendering tick and present 
    renderDevice.BeginFrame( deltaTime );

    renderDevice.GetCurrentBackbufferTexture()->ClearRTV( *renderDevice.GetMainContext(), vaVector4( 0.8f, 0.8f, 0.9f, 1.0f ) );

    ShaderConstants consts = { };
    consts.AspectRatio = (float)application.GetWindowClientAreaSize().x / (float)application.GetWindowClientAreaSize().y;
    consts.UVOffset[0] = 0.5f*(float)cos(application.GetTimeFromStart());
    consts.UVOffset[1] = 0.5f*(float)sin(application.GetTimeFromStart());
    constantBuffer->Upload(*renderDevice.GetMainContext(), consts);

    vaGraphicsItem renderItem;
    renderItem.Topology         = vaPrimitiveTopology::TriangleList;
    renderItem.VertexShader     = vertexShader;
    renderItem.VertexBuffer     = vertexBuffer;
    renderItem.IndexBuffer      = indexBuffer;
    renderItem.PixelShader      = pixelShader;
    renderItem.FrontCounterClockwise = true; // just to prove index buffer works
    renderItem.DrawType         = vaGraphicsItem::DrawType::DrawIndexed;
    renderItem.DrawIndexedParams.IndexCount = 3;
    renderItem.ShaderResourceViews[0] = texture;
    renderItem.ConstantBuffers[0] = constantBuffer;

    renderDevice.GetMainContext()->ExecuteSingleItem( renderItem, renderDevice.GetCurrentBackbuffer(), nullptr );

    // update and draw imgui
    application.DrawUI( *renderDevice.GetMainContext( ), renderDevice.GetCurrentBackbuffer(), nullptr );

    // present the frame, flip the buffers, etc
    renderDevice.EndAndPresentFrame( (application.GetVsync())?(1):(0) );
}

void Sample05_RenderToTexture( vaRenderDevice & renderDevice, vaApplicationBase & application, float deltaTime, vaApplicationState applicationState )
{
    static shared_ptr<vaVertexShader>   vertexShader;
    static shared_ptr<vaRenderBuffer>   vertexBuffer;
    static shared_ptr<vaRenderBuffer>   indexBuffer;
    static shared_ptr<vaPixelShader>    pixelShader;
    static shared_ptr<vaTexture>        texture;
    static shared_ptr<vaPixelShader>    texturePixelShader;
    static shared_ptr<vaTexture>        tempRTtexture;

    struct ShaderConstants
    {
        float UVOffset[2];
        float AspectRatio;
        float Time;
    };
    static shared_ptr<vaConstantBuffer> constantBuffer;

    if( applicationState == vaApplicationState::Initializing )
    {
        // vertex shader
        vertexShader = renderDevice.CreateModule<vaVertexShader>(); // get the platform dependent object
        std::vector<vaVertexInputElementDesc> inputElements = { 
            { "SV_Position", 0,    vaResourceFormat::R32G32B32A32_FLOAT,    0,  0, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 }, 
            { "TEXCOORD", 0,       vaResourceFormat::R32G32_FLOAT,          0, 16, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } };
        vertexShader->CompileVSAndILFromBuffer( 
            "struct ShaderConstants{ float2 UVOffset; float AspectRatio; float SomethingElse; };                           \n"
            "cbuffer Sample04Globals : register(b0) { ShaderConstants g_consts ; }                                        \n"
            "void main( inout float4 xPos : SV_Position, inout float2 UV : TEXCOORD0 ) { xPos *= float4( 1, g_consts.AspectRatio, 1, 1 ); }   \n"
            , "main", inputElements, vaShaderMacroContaner(), true );

        // pixel shader
        pixelShader = renderDevice.CreateModule<vaPixelShader>(); // get the platform dependent object
        pixelShader->CompileFromBuffer( 
            "#include \"vaShared.hlsl\"                                                                             \n" // this defines g_samplerLinearClamp and bunch of other stuff
            "struct ShaderConstants{ float2 UVOffset; float AspectRatio; float SomethingElse; };           \n"
            "cbuffer Sample04Globals : register(b0) { ShaderConstants g_consts ; }                        \n"
            "Texture2D g_source           : register( t0 );                                                         \n"
            "float4 main( in const float4 xPos : SV_Position, in const float2 UV : TEXCOORD0  ) : SV_Target         \n"
            "{                                                                                                      \n"
            "   return g_source.Sample( g_samplerLinearWrap, UV+g_consts.UVOffset );                               \n"
            "}                                                                                                      \n"
            , "main", vaShaderMacroContaner(), true );

        // texture pixel shader
        texturePixelShader = renderDevice.CreateModule<vaPixelShader>(); // get the platform dependent object
        texturePixelShader->CompileFromBuffer( 
            "#include \"vaShared.hlsl\"                                                                             \n" // this defines g_samplerLinearClamp and bunch of other stuff
            "struct ShaderConstants{ float2 UVOffset; float AspectRatio; float Time; };                    \n"
            "cbuffer Sample04Globals : register(b0) { ShaderConstants g_consts ; }                        \n"
            ""
            "" // https://www.shadertoy.com/view/ldBGRR
            "" // Created by Kastor in 2013-10-22
            "" // main code, *original shader by: 'Plasma' by Viktor Korsun (2011)
            ""
            "float4 main( in const float4 xPos : SV_Position, in const float2 UV : TEXCOORD0  ) : SV_Target         \n"
            "{                                                                                                      \n"
            "float x = UV.x;                                                                                        \n"
            "float y = UV.y;                                                                                        \n"
            "float mov0 = x+y+cos(sin(g_consts.Time)*2.0)*100.+sin(x/100.)*1000.;                                  \n"
            "float mov1 = y / 0.9 +  g_consts.Time;                                                                \n"
            "float mov2 = x / 0.2;                                                                                  \n"
            "float c1 = abs(sin(mov1+g_consts.Time)/2.+mov2/2.-mov1-mov2+g_consts.Time);                          \n"
            "float c2 = abs(sin(c1+sin(mov0/1000.+g_consts.Time)+sin(y/40.+g_consts.Time)+sin((x+y)/100.)*3.));   \n"
            "float c3 = abs(sin(c2+cos(mov1+mov2+c2)+cos(mov2)+sin(x/1000.)));                                      \n"
            "return float4(c1,c2,c3,1);                                                                             \n"
            "}                                                                                                      \n"
            , "main", vaShaderMacroContaner(), true );

        // vertex buffer
        struct SimpleVertex
        {
            float   Position[4];
            float   UV[2];
        };
        SimpleVertex triangleVerts[3] = {
            {  0.0f,   0.141f, 0.0f, 1.0f, 0.0f, 0.0f },
            {  0.2f,  -0.141f, 0.0f, 1.0f, 1.0f, 0.0f },
            { -0.2f,  -0.141f, 0.0f, 1.0f, 0.0f, 1.0f },
        };
        vertexBuffer = vaRenderBuffer::Create<SimpleVertex>( renderDevice, _countof( triangleVerts ), vaRenderBufferFlags::VertexIndexBuffer, "vertices", triangleVerts );

        uint32 indices[3] = { 0, 2, 1 };
        indexBuffer = vaRenderBuffer::Create( renderDevice, _countof( indices ), vaResourceFormat::R32_UINT, vaRenderBufferFlags::VertexIndexBuffer, "indices", indices );

        // textures
        {
            uint32 initialData[32*32];
            for( int y = 0; y < 32; y++ )
                for( int x = 0; x < 32; x++ )
                    initialData[ y * 32 + x ] = (((x+y)%2) == 0)?(0xFFFFFFFF):(0x00000000);
            texture = vaTexture::Create2D( renderDevice, vaResourceFormat::R8G8B8A8_UNORM_SRGB, 32, 32, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget, vaResourceAccessFlags::Default, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::None, vaTextureContentsType::GenericColor, initialData, 32*sizeof(uint32) );
        }
        constantBuffer = vaConstantBuffer::Create<ShaderConstants>( renderDevice, "constants" );
        return;
    }
    else if( applicationState == vaApplicationState::ShuttingDown )
    {
        pixelShader = nullptr;
        vertexShader = nullptr;
        vertexBuffer = nullptr;
        indexBuffer = nullptr;
        texture = nullptr;
        constantBuffer = nullptr;
        texturePixelShader = nullptr;
        return;
    }
    assert( applicationState == vaApplicationState::Running );

    application.TickUI( );

    // Do the rendering tick and present 
    renderDevice.BeginFrame( deltaTime );

    renderDevice.GetCurrentBackbufferTexture()->ClearRTV( *renderDevice.GetMainContext(), vaVector4( 0.8f, 0.8f, 0.9f, 1.0f ) );

    ShaderConstants consts = { };
    consts.AspectRatio  = (float)application.GetWindowClientAreaSize().x / (float)application.GetWindowClientAreaSize().y;
    consts.UVOffset[0]  = 0.5f*(float)cos(application.GetTimeFromStart()*0.2f);
    consts.UVOffset[1]  = 0.5f*(float)sin(application.GetTimeFromStart()*0.2f);
    consts.Time         = (float)fmod( application.GetTimeFromStart(), 1000.0 );
    constantBuffer->Upload(*renderDevice.GetMainContext(), consts);

    {
        // draw to offscreen texture
        vaGraphicsItem renderItem;
        renderDevice.FillFullscreenPassGraphicsItem( renderItem );
        renderItem.PixelShader = texturePixelShader;
        renderItem.ConstantBuffers[0] = constantBuffer;
        renderDevice.GetMainContext()->ExecuteSingleItem( renderItem, vaRenderOutputs::FromRTDepth(texture), nullptr );
    }

    {
        vaGraphicsItem renderItem;
        renderItem.Topology         = vaPrimitiveTopology::TriangleList;
        renderItem.VertexShader     = vertexShader;
        renderItem.VertexBuffer     = vertexBuffer;
        renderItem.IndexBuffer      = indexBuffer;
        renderItem.PixelShader      = pixelShader;
        renderItem.FrontCounterClockwise = true; // just to prove index buffer works
        renderItem.DrawType         = vaGraphicsItem::DrawType::DrawIndexed;
        renderItem.DrawIndexedParams.IndexCount = 3;
        renderItem.ShaderResourceViews[0] = texture;
        renderItem.ConstantBuffers[0] = constantBuffer;

        renderDevice.GetMainContext( )->ExecuteSingleItem( renderItem, renderDevice.GetCurrentBackbuffer( ), nullptr );
    }

    // update and draw imgui
    application.DrawUI( *renderDevice.GetMainContext( ), renderDevice.GetCurrentBackbuffer(), nullptr );

    // present the frame, flip the buffers, etc
    renderDevice.EndAndPresentFrame( (application.GetVsync())?(1):(0) );
}

void Sample06_RenderToTextureCS( vaRenderDevice & renderDevice, vaApplicationBase & application, float deltaTime, vaApplicationState applicationState )
{
    static shared_ptr<vaVertexShader>   vertexShader;
    static shared_ptr<vaRenderBuffer>   vertexBuffer;
    static shared_ptr<vaRenderBuffer>   indexBuffer;
    static shared_ptr<vaPixelShader>    pixelShader;
    static shared_ptr<vaTexture>        texture;
    static shared_ptr<vaComputeShader>  textureComputeShader;

    struct ShaderConstants
    {
        float UVOffset[2];
        float AspectRatio;
        float Time;
    };
    static shared_ptr<vaConstantBuffer> constantBuffer;

    if( applicationState == vaApplicationState::Initializing )
    {
        // vertex shader
        vertexShader = renderDevice.CreateModule<vaVertexShader>(); // get the platform dependent object
        std::vector<vaVertexInputElementDesc> inputElements = { 
            { "SV_Position", 0,    vaResourceFormat::R32G32B32A32_FLOAT,    0,  0, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 }, 
            { "TEXCOORD", 0,       vaResourceFormat::R32G32_FLOAT,          0, 16, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } };
        vertexShader->CompileVSAndILFromBuffer( 
            "struct ShaderConstants{ float2 UVOffset; float AspectRatio; float SomethingElse; };                           \n"
            "cbuffer Sample04Globals : register(b0) { ShaderConstants g_consts ; }                                        \n"
            "void main( inout float4 xPos : SV_Position, inout float2 UV : TEXCOORD0 ) { xPos *= float4( 1, g_consts.AspectRatio, 1, 1 ); }   \n"
            , "main", inputElements, vaShaderMacroContaner(), true );

        // pixel shader
        pixelShader = renderDevice.CreateModule<vaPixelShader>(); // get the platform dependent object
        pixelShader->CompileFromBuffer( 
            "#include \"vaShared.hlsl\"                                                                             \n" // this defines g_samplerLinearClamp and bunch of other stuff
            "struct ShaderConstants{ float2 UVOffset; float AspectRatio; float SomethingElse; };           \n"
            "cbuffer Sample04Globals : register(b0) { ShaderConstants g_consts ; }                        \n"
            "Texture2D g_source           : register( t0 );                                                         \n"
            "float4 main( in const float4 xPos : SV_Position, in const float2 UV : TEXCOORD0  ) : SV_Target         \n"
            "{                                                                                                      \n"
            "   return g_source.Sample( g_samplerLinearWrap, UV+g_consts.UVOffset );                               \n"
            "}                                                                                                      \n"
            , "main", vaShaderMacroContaner(), true );

        // texture pixel shader
        textureComputeShader = renderDevice.CreateModule<vaComputeShader>(); // get the platform dependent object
        textureComputeShader->CompileFromBuffer( 
            "#include \"vaShared.hlsl\"                                                                             \n" // this defines g_samplerLinearClamp and bunch of other stuff
            "struct ShaderConstants{ float2 UVOffset; float AspectRatio; float Time; };                    \n"
            "cbuffer Sample04Globals : register(b0) { ShaderConstants g_consts ; }                        \n"
            "RWTexture2D<uint>      g_textureUAV               : register( u0 );                                    \n"
            ""
            "" // https://www.shadertoy.com/view/ldBGRR
            "" // Created by Kastor in 2013-10-22
            "" // main code, *original shader by: 'Plasma' by Viktor Korsun (2011)
            ""
            "[numthreads( 16, 16, 1 )]"
            "void main( uint2 dispatchThreadID : SV_DispatchThreadID )                                              \n"
            "{                                                                                                      \n"
            "float2 UV = (float2(dispatchThreadID) + 0.5)/32;                                                       \n"
            "float x = UV.x;                                                                                        \n"
            "float y = UV.y;                                                                                        \n"
            "float mov0 = x+y+cos(sin(g_consts.Time)*2.0)*100.+sin(x/100.)*1000.;                                  \n"
            "float mov1 = y / 0.9 +  g_consts.Time;                                                                \n"
            "float mov2 = x / 0.2;                                                                                  \n"
            "float c1 = abs(sin(mov1+g_consts.Time)/2.+mov2/2.-mov1-mov2+g_consts.Time);                          \n"
            "float c2 = abs(sin(c1+sin(mov0/1000.+g_consts.Time)+sin(y/40.+g_consts.Time)+sin((x+y)/100.)*3.));   \n"
            "float c3 = abs(sin(c2+cos(mov1+mov2+c2)+cos(mov2)+sin(x/1000.)));                                      \n"
            "float3 color = LINEAR_to_SRGB( float3(c1,c2,c3) );                                                     \n"
            "g_textureUAV[ dispatchThreadID ] = FLOAT4_to_R8G8B8A8_UNORM( float4( color, 1 ) );                     \n"
            "}                                                                                                      \n"
            , "main", vaShaderMacroContaner(), true );

        // vertex buffer
        struct SimpleVertex
        {
            float   Position[4];
            float   UV[2];
        };
        SimpleVertex triangleVerts[3] = {
            {  0.0f,   0.141f, 0.0f, 1.0f, 0.0f, 0.0f },
            {  0.2f,  -0.141f, 0.0f, 1.0f, 1.0f, 0.0f },
            { -0.2f,  -0.141f, 0.0f, 1.0f, 0.0f, 1.0f },
        };
        vertexBuffer = vaRenderBuffer::Create<SimpleVertex>( renderDevice, _countof( triangleVerts ), vaRenderBufferFlags::VertexIndexBuffer, "vertices", triangleVerts );

        uint32 indices[3] = { 0, 2, 1 };
        indexBuffer = vaRenderBuffer::Create( renderDevice, _countof( indices ), vaResourceFormat::R32_UINT, vaRenderBufferFlags::VertexIndexBuffer, "indices", indices );

        // texture
        {
            uint32 initialData[32*32];
            for( int y = 0; y < 32; y++ )
                for( int x = 0; x < 32; x++ )
                    initialData[ y * 32 + x ] = (((x+y)%2) == 0)?(0xFFFFFFFF):(0x00000000);
            texture = vaTexture::Create2D( renderDevice, vaResourceFormat::R8G8B8A8_TYPELESS, 32, 32, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess, vaResourceAccessFlags::Default, vaResourceFormat::R8G8B8A8_UNORM_SRGB, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::R32_UINT, vaTextureFlags::None, vaTextureContentsType::GenericColor, initialData, 32*sizeof(uint32) );
        }
        constantBuffer = vaConstantBuffer::Create<ShaderConstants>( renderDevice, "constants" );
        return;
    }
    else if( applicationState == vaApplicationState::ShuttingDown )
    {
        pixelShader = nullptr;
        vertexShader = nullptr;
        vertexBuffer = nullptr;
        indexBuffer = nullptr;
        texture = nullptr;
        constantBuffer = nullptr;
        textureComputeShader = nullptr;
        return;
    }
    assert( applicationState == vaApplicationState::Running );

    application.TickUI( );
    
    // Do the rendering tick and present 
    renderDevice.BeginFrame( deltaTime );

    renderDevice.GetCurrentBackbufferTexture()->ClearRTV( *renderDevice.GetMainContext(), vaVector4( 0.8f, 0.8f, 0.9f, 1.0f ) );

    ShaderConstants consts = { };
    consts.AspectRatio  = (float)application.GetWindowClientAreaSize().x / (float)application.GetWindowClientAreaSize().y;
    consts.UVOffset[0]  = 0.5f*(float)cos(application.GetTimeFromStart()*0.2f);
    consts.UVOffset[1]  = 0.5f*(float)sin(application.GetTimeFromStart()*0.2f);
    consts.Time         = (float)fmod( application.GetTimeFromStart(), 1000.0 );
    constantBuffer->Upload(*renderDevice.GetMainContext(), consts);

    {
        // draw to offscreen texture
        vaComputeItem computeItem;
        vaRenderOutputs outputs;
        computeItem.ComputeShader = textureComputeShader;
        computeItem.ConstantBuffers[0] = constantBuffer;
        outputs.UnorderedAccessViews[0] = texture;
        assert( texture->GetSizeX() == 32 && texture->GetSizeY() == 32 );
        computeItem.SetDispatch( 32/16, 32/16, 1 );
        renderDevice.GetMainContext()->ExecuteSingleItem( computeItem, outputs, nullptr );
    }

    {
        vaGraphicsItem renderItem;
        renderItem.Topology         = vaPrimitiveTopology::TriangleList;
        renderItem.VertexShader     = vertexShader;
        renderItem.VertexBuffer     = vertexBuffer;
        renderItem.IndexBuffer      = indexBuffer;
        renderItem.PixelShader      = pixelShader;
        renderItem.FrontCounterClockwise = true; // just to prove index buffer works
        renderItem.DrawType         = vaGraphicsItem::DrawType::DrawIndexed;
        renderItem.DrawIndexedParams.IndexCount = 3;
        renderItem.ShaderResourceViews[0] = texture;
        renderItem.ConstantBuffers[0] = constantBuffer;

        renderDevice.GetMainContext()->ExecuteSingleItem( renderItem, renderDevice.GetCurrentBackbuffer(), nullptr );
    }

    // update and draw imgui
    application.DrawUI( *renderDevice.GetMainContext( ), renderDevice.GetCurrentBackbuffer(), nullptr );

    // present the frame, flip the buffers, etc
    renderDevice.EndAndPresentFrame( (application.GetVsync())?(1):(0) );
}

void Sample07_TextureUpload( vaRenderDevice & renderDevice, vaApplicationBase & application, float deltaTime, vaApplicationState applicationState )
{
    static shared_ptr<vaVertexShader>   vertexShader;
    static shared_ptr<vaRenderBuffer>   vertexBuffer;
    static shared_ptr<vaRenderBuffer>   indexBuffer;
    static shared_ptr<vaPixelShader>    pixelShader;
    static shared_ptr<vaTexture>        texture;
    static shared_ptr<vaTexture>        stagingTextures[vaRenderDevice::c_BackbufferCount];
    static int                          currentStagingTexture = 0;

    struct ShaderConstants
    {
        float UVOffset[2];
        float AspectRatio;
        float Time;
    };
    static shared_ptr<vaConstantBuffer> constantBuffer;

    if( applicationState == vaApplicationState::Initializing )
    {
        // vertex shader
        vertexShader = renderDevice.CreateModule<vaVertexShader>(); // get the platform dependent object
        std::vector<vaVertexInputElementDesc> inputElements = { 
            { "SV_Position", 0,    vaResourceFormat::R32G32B32A32_FLOAT,    0,  0, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 }, 
            { "TEXCOORD", 0,       vaResourceFormat::R32G32_FLOAT,          0, 16, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } };
        vertexShader->CompileVSAndILFromBuffer( 
            "struct ShaderConstants{ float2 UVOffset; float AspectRatio; float SomethingElse; };                           \n"
            "cbuffer Sample04Globals : register(b0) { ShaderConstants g_consts ; }                                        \n"
            "void main( inout float4 xPos : SV_Position, inout float2 UV : TEXCOORD0 ) { xPos *= float4( 1, g_consts.AspectRatio, 1, 1 ); }   \n"
            , "main", inputElements, vaShaderMacroContaner(), true );

        // pixel shader
        pixelShader = renderDevice.CreateModule<vaPixelShader>(); // get the platform dependent object
        pixelShader->CompileFromBuffer( 
            "#include \"vaShared.hlsl\"                                                                             \n" // this defines g_samplerLinearClamp and bunch of other stuff
            "struct ShaderConstants{ float2 UVOffset; float AspectRatio; float SomethingElse; };           \n"
            "cbuffer Sample04Globals : register(b0) { ShaderConstants g_consts ; }                        \n"
            "Texture2D g_source           : register( t0 );                                                         \n"
            "float4 main( in const float4 xPos : SV_Position, in const float2 UV : TEXCOORD0  ) : SV_Target         \n"
            "{                                                                                                      \n"
            "   return g_source.Sample( g_samplerLinearWrap, UV+g_consts.UVOffset );                               \n"
            "}                                                                                                      \n"
            , "main", vaShaderMacroContaner(), true );

        // vertex buffer
        struct SimpleVertex
        {
            float   Position[4];
            float   UV[2];
        };
        SimpleVertex triangleVerts[3] = {
            {  0.0f,   0.141f, 0.0f, 1.0f, 0.0f, 0.0f },
            {  0.2f,  -0.141f, 0.0f, 1.0f, 1.0f, 0.0f },
            { -0.2f,  -0.141f, 0.0f, 1.0f, 0.0f, 1.0f },
        };
        vertexBuffer = vaRenderBuffer::Create<SimpleVertex>( renderDevice, _countof( triangleVerts ), vaRenderBufferFlags::VertexIndexBuffer, "vertices", triangleVerts );

        uint32 indices[3] = { 0, 2, 1 };
        indexBuffer = vaRenderBuffer::Create( renderDevice, _countof( indices ), vaResourceFormat::R32_UINT, vaRenderBufferFlags::VertexIndexBuffer, "indices", indices );

        // textures
        {
            uint32 initialData[32*32];
            for( int y = 0; y < 32; y++ )
                for( int x = 0; x < 32; x++ )
                    initialData[ y * 32 + x ] = (((x+y)%2) == 0)?(0xFFFFFFFF):(0x00000000);
            texture = vaTexture::Create2D( renderDevice, vaResourceFormat::R8G8B8A8_UNORM_SRGB, 32, 32, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget, vaResourceAccessFlags::Default, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::None, vaTextureContentsType::GenericColor, initialData, 32*sizeof(uint32) );
            for( int i = 0; i < _countof( stagingTextures ); i++ )
                stagingTextures[i] = vaTexture::Create2D( renderDevice, vaResourceFormat::R8G8B8A8_TYPELESS, 32, 32, 1, 1, 1, vaResourceBindSupportFlags::None, vaResourceAccessFlags::CPUWrite, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::None, vaTextureContentsType::GenericColor, initialData, 32*sizeof(uint32) );
        }

        constantBuffer = vaConstantBuffer::Create<ShaderConstants>( renderDevice, "constants" );
        return;
    }
    else if( applicationState == vaApplicationState::ShuttingDown )
    {
        pixelShader = nullptr;
        vertexShader = nullptr;
        vertexBuffer = nullptr;
        indexBuffer = nullptr;
        texture = nullptr;
        constantBuffer = nullptr;
        for( int i = 0; i < _countof( stagingTextures ); i++ )
            stagingTextures[i] = nullptr;
        return;
    }
    assert( applicationState == vaApplicationState::Running );

    application.TickUI( );
    
    // Do the rendering tick and present 
    renderDevice.BeginFrame( deltaTime );

    renderDevice.GetCurrentBackbufferTexture()->ClearRTV( *renderDevice.GetMainContext(), vaVector4( 0.8f, 0.8f, 0.9f, 1.0f ) );

    ShaderConstants consts = { };
    consts.AspectRatio  = (float)application.GetWindowClientAreaSize().x / (float)application.GetWindowClientAreaSize().y;
    consts.UVOffset[0]  = 0.5f*(float)cos(application.GetTimeFromStart()*0.2f);
    consts.UVOffset[1]  = 0.5f*(float)sin(application.GetTimeFromStart()*0.2f);
    consts.Time         = (float)fmod( application.GetTimeFromStart(), 1000.0 );
    constantBuffer->Upload(*renderDevice.GetMainContext(), consts);

    {
        double time = application.GetTimeFromStart();

        // loop to test for fence lock safety
        //for( int zmz = 0; zmz < 99; zmz++ )
        //{
        //    static int mzm = 0; mzm = (mzm+1)%2;

            if( stagingTextures[currentStagingTexture]->TryMap( *renderDevice.GetMainContext(), vaResourceMapType::Write ) )
            {
                std::vector<vaTextureMappedSubresource> & mappedData = stagingTextures[currentStagingTexture]->GetMappedData( );
                assert( mappedData.size() == 1 );
                for( int iy = 0; iy < mappedData[0].SizeY; iy++ )
                    for( int ix = 0; ix < mappedData[0].SizeX; ix++ )
                    {
                        uint32 & pixel = mappedData[0].PixelAt<uint32>( ix, iy );

                        // https://www.shadertoy.com/view/ldBGRR
                        // Created by Kastor in 2013-10-22
                        // main code, *original shader by: 'Plasma' by Viktor Korsun (2011)
                        float x = (ix+0.5f)/(float)mappedData[0].SizeX;
                        float y = (iy+0.5f)/(float)mappedData[0].SizeY;
                        float mov0 = x+y+(float)cos(sin(time)*2.0f)*100.f+sin(x/100.f)*1000.f;
                        double mov1 = y / 0.9f + time;
                        float mov2 = x / 0.2f;
                        float c1 = abs((float)(sin(mov1+time)/2.f+mov2/2.f-mov1-mov2+time));
                        float c2 = abs((float)sin(c1+sin(mov0/1000.f+time)+sin(y/40.f+time)+sin((x+y)/100.f)*3.f));
                        float c3 = abs((float)sin(c2+cos(mov1+mov2+c2)+cos(mov2)+sin(x/1000.f)));
                        //if( mzm == 0 ) c1 = 0;
                        //if( mzm == 0 ) c2 = 0;
                        pixel = vaVector4::ToRGBA( vaColor::LinearToSRGB(vaMath::Saturate(c1)), vaColor::LinearToSRGB(vaMath::Saturate(c2)), vaColor::LinearToSRGB(vaMath::Saturate(c3) ), 1 );
                    }                                                   
                                                                    
                stagingTextures[currentStagingTexture]->Unmap( *renderDevice.GetMainContext() );

                stagingTextures[currentStagingTexture]->CopyTo( *renderDevice.GetMainContext(), texture );

                currentStagingTexture = (currentStagingTexture+1) % _countof(stagingTextures);
            }
            else
            {
                assert( false );
            }
        //}

    }

    {
        vaGraphicsItem renderItem;
        renderItem.Topology         = vaPrimitiveTopology::TriangleList;
        renderItem.VertexShader     = vertexShader;
        renderItem.VertexBuffer     = vertexBuffer;
        renderItem.IndexBuffer      = indexBuffer;
        renderItem.PixelShader      = pixelShader;
        renderItem.FrontCounterClockwise = true; // just to prove index buffer works
        renderItem.DrawType         = vaGraphicsItem::DrawType::DrawIndexed;
        renderItem.DrawIndexedParams.IndexCount = 3;
        renderItem.ShaderResourceViews[0] = texture;
        renderItem.ConstantBuffers[0] = constantBuffer;

        renderDevice.GetMainContext()->ExecuteSingleItem( renderItem, renderDevice.GetCurrentBackbuffer(), nullptr );
    }

    // update and draw imgui
    application.DrawUI( *renderDevice.GetMainContext( ), renderDevice.GetCurrentBackbuffer(), nullptr );

    // present the frame, flip the buffers, etc
    renderDevice.EndAndPresentFrame( (application.GetVsync())?(1):(0) );
}

// + Canvas2D
void Sample08_TextureDownload( vaRenderDevice & renderDevice, vaApplicationBase & application, float deltaTime, vaApplicationState applicationState )
{
    static shared_ptr<vaVertexShader>   vertexShader;
    static shared_ptr<vaRenderBuffer>   vertexBuffer;
    static shared_ptr<vaRenderBuffer>   indexBuffer;
    static shared_ptr<vaPixelShader>    pixelShader;
    static shared_ptr<vaTexture>        texture;
    static shared_ptr<vaComputeShader>  textureComputeShader;
    static shared_ptr<vaTexture>        stagingTextures[vaRenderDevice::c_BackbufferCount];
    static int                          currentStagingTexture = 0;

    struct ShaderConstants
    {
        float UVOffset[2];
        float AspectRatio;
        float Time;
    };
    static shared_ptr<vaConstantBuffer> constantBuffer;

    if( applicationState == vaApplicationState::Initializing )
    {
        // vertex shader
        vertexShader = renderDevice.CreateModule<vaVertexShader>(); // get the platform dependent object
        std::vector<vaVertexInputElementDesc> inputElements = { 
            { "SV_Position", 0,    vaResourceFormat::R32G32B32A32_FLOAT,    0,  0, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 }, 
            { "TEXCOORD", 0,       vaResourceFormat::R32G32_FLOAT,          0, 16, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } };
        vertexShader->CompileVSAndILFromBuffer( 
            "struct ShaderConstants{ float2 UVOffset; float AspectRatio; float SomethingElse; };                           \n"
            "cbuffer Sample04Globals : register(b0) { ShaderConstants g_consts ; }                                        \n"
            "void main( inout float4 xPos : SV_Position, inout float2 UV : TEXCOORD0 ) { xPos *= float4( 1, g_consts.AspectRatio, 1, 1 ); }   \n"
            , "main", inputElements, vaShaderMacroContaner(), true );

        // pixel shader
        pixelShader = renderDevice.CreateModule<vaPixelShader>(); // get the platform dependent object
        pixelShader->CompileFromBuffer( 
            "#include \"vaShared.hlsl\"                                                                             \n" // this defines g_samplerLinearClamp and bunch of other stuff
            "struct ShaderConstants{ float2 UVOffset; float AspectRatio; float SomethingElse; };           \n"
            "cbuffer Sample04Globals : register(b0) { ShaderConstants g_consts ; }                        \n"
            "Texture2D g_source           : register( t0 );                                                         \n"
            "float4 main( in const float4 xPos : SV_Position, in const float2 UV : TEXCOORD0  ) : SV_Target         \n"
            "{                                                                                                      \n"
            "   return g_source.Sample( g_samplerLinearWrap, UV+g_consts.UVOffset );                               \n"
            "}                                                                                                      \n"
            , "main", vaShaderMacroContaner(), true );

        // texture pixel shader
        textureComputeShader = renderDevice.CreateModule<vaComputeShader>(); // get the platform dependent object
        textureComputeShader->CompileFromBuffer( 
            "#include \"vaShared.hlsl\"                                                                             \n" // this defines g_samplerLinearClamp and bunch of other stuff
            "struct ShaderConstants{ float2 UVOffset; float AspectRatio; float Time; };                    \n"
            "cbuffer Sample04Globals : register(b0) { ShaderConstants g_consts ; }                        \n"
            "RWTexture2D<uint>      g_textureUAV               : register( u0 );                                    \n"
            ""
            "" // https://www.shadertoy.com/view/ldBGRR
            "" // Created by Kastor in 2013-10-22
            "" // main code, *original shader by: 'Plasma' by Viktor Korsun (2011)
            ""
            "[numthreads( 16, 16, 1 )]"
            "void main( uint2 dispatchThreadID : SV_DispatchThreadID )                                              \n"
            "{                                                                                                      \n"
            "float2 UV = (float2(dispatchThreadID) + 0.5)/32;                                                       \n"
            "float x = UV.x;                                                                                        \n"
            "float y = UV.y;                                                                                        \n"
            "float mov0 = x+y+cos(sin(g_consts.Time)*2.0)*100.+sin(x/100.)*1000.;                                  \n"
            "float mov1 = y / 0.9 +  g_consts.Time;                                                                \n"
            "float mov2 = x / 0.2;                                                                                  \n"
            "float c1 = abs(sin(mov1+g_consts.Time)/2.+mov2/2.-mov1-mov2+g_consts.Time);                          \n"
            "float c2 = abs(sin(c1+sin(mov0/1000.+g_consts.Time)+sin(y/40.+g_consts.Time)+sin((x+y)/100.)*3.));   \n"
            "float c3 = abs(sin(c2+cos(mov1+mov2+c2)+cos(mov2)+sin(x/1000.)));                                      \n"
            "float3 color = LINEAR_to_SRGB( float3(c1,c2,c3) );                                                     \n"
            "g_textureUAV[ dispatchThreadID ] = FLOAT4_to_R8G8B8A8_UNORM( float4( color, 1 ) );                     \n"
            "}                                                                                                      \n"
            , "main", vaShaderMacroContaner(), true );

        // vertex buffer
        struct SimpleVertex
        {
            float   Position[4];
            float   UV[2];
        };
        SimpleVertex triangleVerts[3] = {
            {  0.0f,   0.141f, 0.0f, 1.0f, 0.0f, 0.0f },
            {  0.2f,  -0.141f, 0.0f, 1.0f, 1.0f, 0.0f },
            { -0.2f,  -0.141f, 0.0f, 1.0f, 0.0f, 1.0f },
        };
        vertexBuffer = vaRenderBuffer::Create<SimpleVertex>( renderDevice, _countof( triangleVerts ), vaRenderBufferFlags::VertexIndexBuffer, "vertices", triangleVerts );

        uint32 indices[3] = { 0, 2, 1 };
        indexBuffer = vaRenderBuffer::Create( renderDevice, _countof( indices ), vaResourceFormat::R32_UINT, vaRenderBufferFlags::VertexIndexBuffer, "indices", indices );

        // textures
        {
            uint32 initialData[32*32];
            for( int y = 0; y < 32; y++ )
                for( int x = 0; x < 32; x++ )
                    initialData[ y * 32 + x ] = (((x+y)%2) == 0)?(0xFFFFFFFF):(0x00000000);
            texture = vaTexture::Create2D( renderDevice, vaResourceFormat::R8G8B8A8_TYPELESS, 32, 32, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess, vaResourceAccessFlags::Default, vaResourceFormat::R8G8B8A8_UNORM_SRGB, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::R32_UINT, vaTextureFlags::None, vaTextureContentsType::GenericColor, initialData, 32*sizeof(uint32) );
            for( int i = 0; i < _countof( stagingTextures ); i++ )
                stagingTextures[i] = vaTexture::Create2D( renderDevice, vaResourceFormat::R8G8B8A8_TYPELESS, 32, 32, 1, 1, 1, vaResourceBindSupportFlags::None, vaResourceAccessFlags::CPURead, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::None, vaTextureContentsType::GenericColor, initialData, 32*sizeof(uint32) );
        }
        constantBuffer = vaConstantBuffer::Create<ShaderConstants>( renderDevice, "constants" );
        return;
    }
    else if( applicationState == vaApplicationState::ShuttingDown )
    {
        pixelShader = nullptr;
        vertexShader = nullptr;
        vertexBuffer = nullptr;
        indexBuffer = nullptr;
        texture = nullptr;
        constantBuffer = nullptr;
        textureComputeShader = nullptr;
        for( int i = 0; i < _countof( stagingTextures ); i++ )
            stagingTextures[i] = nullptr;
        return;
    }
    assert( applicationState == vaApplicationState::Running );

    VA_TRACE_CPU_SCOPE( Sample08_TextureDownload );

    application.TickUI( );

    // Do the rendering tick and present 
    renderDevice.BeginFrame( deltaTime );

    renderDevice.GetCurrentBackbufferTexture()->ClearRTV( *renderDevice.GetMainContext(), vaVector4( 0.8f, 0.8f, 0.9f, 1.0f ) );

    ShaderConstants consts = { };
    consts.AspectRatio  = (float)application.GetWindowClientAreaSize().x / (float)application.GetWindowClientAreaSize().y;
    consts.UVOffset[0]  = 0.5f*(float)cos(application.GetTimeFromStart()*0.2f);
    consts.UVOffset[1]  = 0.5f*(float)sin(application.GetTimeFromStart()*0.2f);
    consts.Time         = (float)fmod( application.GetTimeFromStart(), 1000.0 );
    constantBuffer->Upload(*renderDevice.GetMainContext(), consts);

    // draw to offscreen texture
    {
        vaComputeItem computeItem;
        vaRenderOutputs outputs;
        computeItem.ComputeShader = textureComputeShader;
        computeItem.ConstantBuffers[0] = constantBuffer;
        outputs.UnorderedAccessViews[0] = texture;
        assert( texture->GetSizeX() == 32 && texture->GetSizeY() == 32 );
        computeItem.SetDispatch( 32/16, 32/16, 1 );
        renderDevice.GetMainContext()->ExecuteSingleItem( computeItem, outputs, nullptr );
    }

    // Map & draw contents pixel by pixel using Canvas2D and then copy the offscreen texture to CPU-accessible texture for the next frame.
    {
        vaDebugCanvas2D & debugCanvas = renderDevice.GetCanvas2D();

        if( stagingTextures[currentStagingTexture]->TryMap( *renderDevice.GetMainContext(), vaResourceMapType::Read ) )
        {
            std::vector<vaTextureMappedSubresource> & mappedData = stagingTextures[currentStagingTexture]->GetMappedData( );
            assert( mappedData.size() == 1 );
            for( int iy = 0; iy < mappedData[0].SizeY; iy++ )
                for( int ix = 0; ix < mappedData[0].SizeX; ix++ )
                {
                    const uint32 & pixel = mappedData[0].PixelAt<uint32>( ix, iy );
                    
                    vaVector4 col = vaVector4::SRGBToLinear( vaVector4::FromRGBA( pixel ) );
                    debugCanvas.FillRectangle( 100+(float)ix*8, 100+(float)iy*8, 8, 8, vaVector4::ToBGRA(col) );
                }                                                   
                                                                    
            stagingTextures[currentStagingTexture]->Unmap( *renderDevice.GetMainContext() );

            debugCanvas.Render( *renderDevice.GetMainContext(), renderDevice.GetCurrentBackbuffer() );
        }
        else
        {
            assert( false );
        }
        stagingTextures[currentStagingTexture]->CopyFrom( *renderDevice.GetMainContext(), texture );
        currentStagingTexture = (currentStagingTexture+1) % _countof(stagingTextures);
    }

    {
        vaGraphicsItem renderItem;
        renderItem.Topology         = vaPrimitiveTopology::TriangleList;
        renderItem.VertexShader     = vertexShader;
        renderItem.VertexBuffer     = vertexBuffer;
        renderItem.IndexBuffer      = indexBuffer;
        renderItem.PixelShader      = pixelShader;
        renderItem.FrontCounterClockwise = true; // just to prove index buffer works
        renderItem.DrawType         = vaGraphicsItem::DrawType::DrawIndexed;
        renderItem.DrawIndexedParams.IndexCount = 3;
        renderItem.ShaderResourceViews[0] = texture;
        renderItem.ConstantBuffers[0] = constantBuffer;

        renderDevice.GetMainContext()->ExecuteSingleItem( renderItem, renderDevice.GetCurrentBackbuffer(), nullptr );
    }

    // update and draw imgui
    application.DrawUI( *renderDevice.GetMainContext( ), renderDevice.GetCurrentBackbuffer(), nullptr );

    // present the frame, flip the buffers, etc
    renderDevice.EndAndPresentFrame( (application.GetVsync())?(1):(0) );
}

void Sample09_SavingScreenshot( vaRenderDevice & renderDevice, vaApplicationBase & application, float deltaTime, vaApplicationState applicationState )
{
    static bool                         captureSShotNextFrame = false;
    static shared_ptr<vaVertexShader>   vertexShader;
    static shared_ptr<vaRenderBuffer>   vertexBuffer;
    static shared_ptr<vaRenderBuffer>   indexBuffer;
    static shared_ptr<vaPixelShader>    pixelShader;
    static shared_ptr<vaTexture>        offscreenRT;
    static shared_ptr<vaUISimplePanel>  UIPanel;

    struct ShaderConstants
    {
        float UVOffset[2];
        float AspectRatio;
        float Time;
    };
    static shared_ptr<vaConstantBuffer> constantBuffer;

    if( applicationState == vaApplicationState::Initializing )
    {
        // vertex shader
        vertexShader = renderDevice.CreateModule<vaVertexShader>(); // get the platform dependent object
        std::vector<vaVertexInputElementDesc> inputElements = { 
            { "SV_Position", 0,    vaResourceFormat::R32G32B32A32_FLOAT,    0,  0, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 }, 
            { "TEXCOORD", 0,       vaResourceFormat::R32G32_FLOAT,          0, 16, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } };
        vertexShader->CompileVSAndILFromBuffer( 
            "struct ShaderConstants{ float2 UVOffset; float AspectRatio; float SomethingElse; };                           \n"
            "cbuffer Sample04Globals : register(b0) { ShaderConstants g_consts ; }                                        \n"
            "void main( inout float4 xPos : SV_Position, inout float2 UV : TEXCOORD0 ) { xPos *= float4( 1, g_consts.AspectRatio, 1, 1 ); }   \n"
            , "main", inputElements, vaShaderMacroContaner(), true );

        // pixel shader
        pixelShader = renderDevice.CreateModule<vaPixelShader>(); // get the platform dependent object
        pixelShader->CompileFromBuffer( 
            "#include \"vaShared.hlsl\"                                                                             \n" // this defines g_samplerLinearClamp and bunch of other stuff
            "struct ShaderConstants{ float2 UVOffset; float AspectRatio; float Time; };                    \n"
            "cbuffer Sample04Globals : register(b0) { ShaderConstants g_consts ; }                        \n"
            ""
            "" // https://www.shadertoy.com/view/ldBGRR
            "" // Created by Kastor in 2013-10-22
            "" // main code, *original shader by: 'Plasma' by Viktor Korsun (2011)
            ""
            "float4 main( in const float4 xPos : SV_Position, in const float2 UV : TEXCOORD0  ) : SV_Target         \n"
            "{                                                                                                      \n"
            "float x = UV.x;                                                                                        \n"
            "float y = UV.y;                                                                                        \n"
            "float mov0 = x+y+cos(sin(g_consts.Time)*2.0)*100.+sin(x/100.)*1000.;                                  \n"
            "float mov1 = y / 0.9 +  g_consts.Time;                                                                \n"
            "float mov2 = x / 0.2;                                                                                  \n"
            "float c1 = abs(sin(mov1+g_consts.Time)/2.+mov2/2.-mov1-mov2+g_consts.Time);                          \n"
            "float c2 = abs(sin(c1+sin(mov0/1000.+g_consts.Time)+sin(y/40.+g_consts.Time)+sin((x+y)/100.)*3.));   \n"
            "float c3 = abs(sin(c2+cos(mov1+mov2+c2)+cos(mov2)+sin(x/1000.)));                                      \n"
            "return float4(c1,c2,c3,1);                                                                             \n"
            "}                                                                                                      \n"
            , "main", vaShaderMacroContaner(), true );

        // vertex buffer
        struct SimpleVertex
        {
            float   Position[4];
            float   UV[2];
        };
        SimpleVertex triangleVerts[3] = {
            {  0.0f*4,   0.141f*4, 0.0f, 1.0f, 3*0.0f, 3*0.0f },
            {  0.2f*4,  -0.141f*4, 0.0f, 1.0f, 3*1.0f, 3*0.0f },
            { -0.2f*4,  -0.141f*4, 0.0f, 1.0f, 3*0.0f, 3*1.0f },
        };
        vertexBuffer = vaRenderBuffer::Create<SimpleVertex>( renderDevice, _countof( triangleVerts ), vaRenderBufferFlags::VertexIndexBuffer, "vertices", triangleVerts );

        uint32 indices[3] = { 0, 2, 1 };
        indexBuffer = vaRenderBuffer::Create( renderDevice, _countof( indices ), vaResourceFormat::R32_UINT, vaRenderBufferFlags::VertexIndexBuffer, "indices", indices );

        constantBuffer = vaConstantBuffer::Create<ShaderConstants>( renderDevice, "constants" );

        UIPanel = std::make_shared<vaUISimplePanel>( [&renderDevice] ( vaApplicationBase & ) 
        {
#ifdef VA_IMGUI_INTEGRATION_ENABLED
            if( ImGui::Button( "CaptureScreenshot!!" ) )
            {
                captureSShotNextFrame = true;
            }
#endif
        },
            "SavingScreenshotSample", 0, true, vaUIPanel::DockLocation::DockedLeft );
        return;
    }
    else if( applicationState == vaApplicationState::ShuttingDown )
    {
        pixelShader = nullptr;
        vertexShader = nullptr;
        vertexBuffer = nullptr;
        indexBuffer = nullptr;
        offscreenRT = nullptr;
        constantBuffer = nullptr;
        UIPanel = nullptr;
        return;
    }
    assert( applicationState == vaApplicationState::Running );

    auto backbufferTex = renderDevice.GetCurrentBackbufferTexture();
    
    if( offscreenRT == nullptr || offscreenRT->GetSizeX() != backbufferTex->GetSizeX() || offscreenRT->GetSizeY() != backbufferTex->GetSizeY() )
        offscreenRT = vaTexture::Create2D( renderDevice, vaResourceFormat::R8G8B8A8_UNORM_SRGB, backbufferTex->GetSizeX(), backbufferTex->GetSizeY(), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget );

    application.TickUI( );

    // Do the rendering tick and present 
    renderDevice.BeginFrame( deltaTime );

    offscreenRT->ClearRTV( *renderDevice.GetMainContext(), vaVector4( 0.8f, 0.8f, 0.9f, 1.0f ) );

    ShaderConstants consts = { };
    consts.AspectRatio = (float)application.GetWindowClientAreaSize().x / (float)application.GetWindowClientAreaSize().y;
    consts.UVOffset[0] = 0.5f*(float)cos(application.GetTimeFromStart());
    consts.UVOffset[1] = 0.5f*(float)sin(application.GetTimeFromStart());
    consts.Time         = (float)fmod( application.GetTimeFromStart(), 1000.0 );
    constantBuffer->Upload(*renderDevice.GetMainContext(), consts);

    vaGraphicsItem renderItem;
    renderItem.Topology         = vaPrimitiveTopology::TriangleList;
    renderItem.VertexShader     = vertexShader;
    renderItem.VertexBuffer     = vertexBuffer;
    renderItem.IndexBuffer      = indexBuffer;
    renderItem.PixelShader      = pixelShader;
    renderItem.FrontCounterClockwise = true; // just to prove index buffer works
    renderItem.DrawType         = vaGraphicsItem::DrawType::DrawIndexed;
    renderItem.DrawIndexedParams.IndexCount = 3;
    renderItem.ConstantBuffers[0] = constantBuffer;

    renderDevice.GetMainContext()->ExecuteSingleItem( renderItem, vaRenderOutputs::FromRTDepth(offscreenRT), nullptr );

    renderDevice.GetMainContext()->CopySRVToRTV( backbufferTex, offscreenRT );

    if( captureSShotNextFrame )
    {
        captureSShotNextFrame = false;
        wstring path = vaCore::GetExecutableDirectory() + L"test-screenshot.png";

        VA_LOG( L"Capturing screenshot to '%s'...", path.c_str() );

        if( offscreenRT->SaveToPNGFile( *(renderDevice.GetMainContext()), path ) )
            VA_LOG_SUCCESS( L"   OK" );
        else
            VA_LOG_ERROR( L"   FAILED" );
    }

    // update and draw imgui
    application.DrawUI( *renderDevice.GetMainContext( ), renderDevice.GetCurrentBackbuffer(), nullptr );

    // present the frame, flip the buffers, etc
    renderDevice.EndAndPresentFrame( (application.GetVsync())?(1):(0) );
}

// Also demonstrates loading a texture (in this case a cubemap)
void Sample10_Skybox( vaRenderDevice & renderDevice, vaApplicationBase & application, float deltaTime, vaApplicationState applicationState )
{
    static shared_ptr<vaSkybox>         skybox;
    static shared_ptr<vaTexture>        skyboxTexture;
    static shared_ptr<vaCameraBase>     camera;

    if( applicationState == vaApplicationState::Initializing )
    {
        skybox = renderDevice.CreateModule<vaSkybox>();

        skyboxTexture = vaTexture::CreateFromImageFile( renderDevice, vaStringTools::SimpleNarrow(vaCore::GetExecutableDirectory()) + "Media\\sky_cube.dds", vaTextureLoadFlags::Default );

        skybox->SetCubemap( skyboxTexture );

        // scale brightness
        skybox->Settings().ColorMultiplier    = 1.0f;

        camera = std::make_shared<vaCameraBase>();
        camera->SetYFOV( 65.0f / 180.0f * VA_PIf );

        return;
    }
    else if( applicationState == vaApplicationState::ShuttingDown )
    {
        skyboxTexture = nullptr;
        skybox = nullptr;
        camera = nullptr;
        return;
    }
    assert( applicationState == vaApplicationState::Running );

    // setup and rotate camera
    camera->SetViewport( vaViewport( renderDevice.GetCurrentBackbufferTexture()->GetWidth(), renderDevice.GetCurrentBackbufferTexture()->GetHeight() ) );
    camera->SetPosition( { 0, 0, 0 } );
    camera->SetOrientationLookAt( { (float)cos( 0.1 * application.GetTimeFromStart( ) ), (float)sin( 0.1 * application.GetTimeFromStart( ) ), 0 } );
    camera->Tick( deltaTime, true );

    application.TickUI( *camera );

    // Do the rendering tick and present 
    renderDevice.BeginFrame( deltaTime );

    // opaque skybox
    skybox->Draw( *renderDevice.GetMainContext(), renderDevice.GetCurrentBackbuffer(), vaDrawAttributes( *camera ) );

    // update and draw imgui
    application.DrawUI( *renderDevice.GetMainContext( ), renderDevice.GetCurrentBackbuffer(), nullptr );

    // present the frame, flip the buffers, etc
    renderDevice.EndAndPresentFrame( (application.GetVsync())?(1):(0) );
}

// basics required to draw a mesh (without any external complexities)
struct SimpleMeshRenderer
{
    struct SimpleInstanceConstants
    {
        vaMatrix4x4         WorldTrans              = vaMatrix4x4::Identity;
        vaMatrix4x4         WorldViewProjTrans      = vaMatrix4x4::Identity;
        vaVector4           SunDir                  = vaVector4( vaVector3( 0.5f, 0.5f, -1.0f ).Normalized(), 0 );
        vaVector4           SunIntensity            = vaVector4( 0.6f, 0.55f, 0.5f, 1.0f );
        vaVector4           AmbientIntensity        = vaVector4( 0.4f, 0.45f, 0.5f, 1.0f );
        vaVector4           PreExposureMultiplier   = vaVector4::Zero;
    };

    shared_ptr<vaVertexShader>      VertexShader;
    shared_ptr<vaPixelShader>       PixelShader;
    shared_ptr<vaConstantBuffer>    ConstantBuffer;

    SimpleMeshRenderer( vaRenderDevice & renderDevice )
    {
        // constants
        ConstantBuffer = vaConstantBuffer::Create<SimpleInstanceConstants>( renderDevice, "SimpleInstanceConstants" );

        // vertex shader
        VertexShader = renderDevice.CreateModule<vaVertexShader>(); // get the platform dependent object
         
        string globals =    
            "struct SimpleInstanceConstants                                         \n"
            "{                                                                      \n"
            "   float4x4            WorldTrans        ;                             \n" 
            "   float4x4            WorldViewProjTrans;                             \n"
            "   float4              SunDir         ;                                \n"
            "   float4              SunIntensity   ;                                \n"
            "   float4              AmbientIntensity;                               \n"
            "   float4              PreExposureMultiplier;                          \n"
            "};                                                                     \n"
            "cbuffer Globals : register(b0) { SimpleInstanceConstants g_consts ; }  \n";

        VertexShader->CompileVSAndILFromBuffer( globals + 
            "void main( inout float4 position : SV_Position, inout float3 normal : NORMAL, inout float4 texcoord01 : TEXCOORD0 )    \n"
            "{                                                                                                                      \n"
            " position = mul( g_consts.WorldViewProjTrans, float4( position.xyz, 1.0 ) );                                           \n"
            " normal   = normalize( mul( (float3x3)g_consts.WorldTrans, normal.xyz ).xyz );                                         \n"     // <- this isn't correct with scaling (see main vertex shader)
            "}                                                                                                                      \n"
            , "main", vaRenderMesh::GetStandardInputLayout(), {}, true );

        // pixel shader
        PixelShader = renderDevice.CreateModule<vaPixelShader>(); // get the platform dependent object
        PixelShader->CompileFromBuffer( globals + 
            "float4 main( const float4 position : SV_Position, float3 normal : NORMAL, float4 texcoord01 : TEXCOORD0 ) : SV_Target  \n"
            "{                                                                                                                      \n"
            " float3 albedo = 0.8.xxx + 0.1.xxx * (sin( texcoord01.x * 100 ) + sin( texcoord01.y * 100 ));                          \n"
            " float3 color = albedo * (dot( normal, -g_consts.SunDir.xyz ) * g_consts.SunIntensity.rgb + g_consts.AmbientIntensity.rgb);\n"
            " return float4( g_consts.PreExposureMultiplier.xxx * color, 1 );                                                       \n"
            "}                                                                                                                      \n"
            , "main", vaShaderMacroContaner(), true );
    }

    vaDrawResultFlags Draw( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, shared_ptr<vaRenderMesh> mesh, const vaMatrix4x4 & worldTransform, const vaCameraBase & camera, vaBlendMode blendMode = vaBlendMode::Opaque, vaRenderMeshDrawFlags drawFlags = vaRenderMeshDrawFlags::EnableDepthTest | vaRenderMeshDrawFlags::EnableDepthWrite )
    {
        assert( mesh != nullptr );
        const bool skipNonShadowCasters     = (drawFlags & vaRenderMeshDrawFlags::SkipNonShadowCasters  )   != 0; assert( !skipNonShadowCasters ); // not valid for simple renderer
        const bool enableDepthTest          = (drawFlags & vaRenderMeshDrawFlags::EnableDepthTest       )   != 0;
        const bool invertDepthTest          = (drawFlags & vaRenderMeshDrawFlags::InvertDepthTest       )   != 0; 
        const bool enableDepthWrite         = (drawFlags & vaRenderMeshDrawFlags::EnableDepthWrite      )   != 0;
        const bool depthTestIncludesEqual   = (drawFlags & vaRenderMeshDrawFlags::DepthTestIncludesEqual)   != 0;
        const bool depthTestEqualOnly       = (drawFlags & vaRenderMeshDrawFlags::DepthTestEqualOnly    )   != 0;
        const bool depthEnable              = enableDepthTest || enableDepthWrite;
        const bool useReversedZ             = (invertDepthTest)?(!camera.GetUseReversedZ()):(camera.GetUseReversedZ());
        vaComparisonFunc depthFunc          = vaComparisonFunc::Always;
        if( enableDepthTest )
        {
            if( !depthTestEqualOnly )
                depthFunc               = ( depthTestIncludesEqual ) ? ( ( useReversedZ )?( vaComparisonFunc::GreaterEqual ):( vaComparisonFunc::LessEqual ) ):( ( useReversedZ )?( vaComparisonFunc::Greater ):( vaComparisonFunc::Less ) );
            else
                depthFunc               = vaComparisonFunc::Equal;
        }

        vaGraphicsItem renderItem;
        renderItem.BlendMode            = blendMode;
        renderItem.DepthFunc            = depthFunc;
        renderItem.Topology             = vaPrimitiveTopology::TriangleList;
        renderItem.DepthEnable          = depthEnable;
        renderItem.DepthWriteEnable     = enableDepthWrite;
        renderItem.InstanceIndex        = 0xFFFFFFFF;
        renderItem.FillMode             = (false)?(vaFillMode::Wireframe):(vaFillMode::Solid);
        renderItem.ConstantBuffers[0]   = ConstantBuffer;
        renderItem.VertexShader         = VertexShader;
        renderItem.PixelShader          = PixelShader;
        renderItem.ShadingRate          = vaShadingRate::ShadingRate1X1;

        // mesh stuff
        {
            mesh->PreRenderUpdate( renderContext );
            // read lock
            std::shared_lock meshLock( mesh->Mutex( ) );
            renderItem.VertexBuffer                 = mesh->GetGPUVertexBufferFP( );
            renderItem.IndexBuffer                  = mesh->GetGPUIndexBufferFP( );
            renderItem.FrontCounterClockwise        = mesh->GetFrontFaceWindingOrder( ) == vaWindingOrder::CounterClockwise;

            const std::vector<vaRenderMesh::LODPart> & LODParts = mesh->GetLODParts();
            const vaRenderMesh::LODPart & LODPart = LODParts[0];    // just always LOD 0

            renderItem.SetDrawIndexed( LODPart.IndexCount, LODPart.IndexStart, 0 );
        }

        // constants
        {
            SimpleInstanceConstants consts;
            consts.WorldTrans               = worldTransform;
            consts.WorldViewProjTrans       = worldTransform * camera.GetViewMatrix( ) * camera.GetProjMatrix( );
            consts.PreExposureMultiplier.x  = camera.GetPreExposureMultiplier( true );
            ConstantBuffer->Upload( renderContext, consts );
        }
        return renderContext.ExecuteSingleItem( renderItem, renderOutputs, nullptr );
    }
};

struct SimpleSampleShared
{
    shared_ptr<vaTexture>               DepthBuffer;
    shared_ptr<vaSkybox>                Skybox;
    shared_ptr<vaTexture>               SkyboxTexture;
    shared_ptr<vaRenderCamera>          Camera;
    shared_ptr<vaRenderMesh>            MeshTeapot;
    shared_ptr<vaRenderMesh>            MeshPlane;
    shared_ptr<SimpleMeshRenderer>      MeshRenderer;
    std::vector<vaMatrix4x4>            TeapotInstances;
    bool                                AnimateCamera   = true;

    SimpleSampleShared( vaRenderDevice & renderDevice, bool animateCamera, bool thousandsOfTeapots ) : AnimateCamera( animateCamera )
    {
        MeshRenderer = std::make_shared<SimpleMeshRenderer>( renderDevice );
        Skybox = renderDevice.CreateModule<vaSkybox>();
        SkyboxTexture = vaTexture::CreateFromImageFile( renderDevice, vaStringTools::SimpleNarrow(vaCore::GetExecutableDirectory()) + "Media\\sky_cube.dds", vaTextureLoadFlags::Default );
        Skybox->SetCubemap( SkyboxTexture );
        Skybox->Settings().ColorMultiplier    = 1.0f;   // scale brightness
        Camera = std::make_shared<vaRenderCamera>( renderDevice, false ); //renderDevice.CreateModule<vaRenderCamera>();
        Camera->SetYFOV( 65.0f / 180.0f * VA_PIf );
        MeshPlane  = vaRenderMesh::CreatePlane( renderDevice, vaMatrix4x4::Identity, 500.0f, 500.0f );
        MeshTeapot = vaRenderMesh::CreateTeapot( renderDevice, vaMatrix4x4::Identity );

        if( thousandsOfTeapots )
        {
            vaRandom rnd;
            for( int x = 0; x < 10; x++ )
                for( int y = 0; y < 10; y++ )
                {
                    TeapotInstances.push_back( vaMatrix4x4::RotationZ( rnd.NextFloat() * VA_PIf ) * vaMatrix4x4::Translation( { (x-8) * 3.5f, (y-6) * 3.5f, 0.0f } ) );
                }
        }
        else
        {
            TeapotInstances.push_back( vaMatrix4x4::Identity );
        }
    }

    void Tick( vaRenderDevice & renderDevice, vaApplicationBase & application, float deltaTime )
    {
        auto backbufferTex = renderDevice.GetCurrentBackbufferTexture();

        // Create/update depth
        if( DepthBuffer == nullptr || DepthBuffer->GetSize() != backbufferTex->GetSize() || DepthBuffer->GetSampleCount() != backbufferTex->GetSampleCount() )
            DepthBuffer = vaTexture::Create2D( renderDevice, vaResourceFormat::R32_TYPELESS, backbufferTex->GetSizeX(), backbufferTex->GetSizeY(), 1, 1, 1, vaResourceBindSupportFlags::DepthStencil | vaResourceBindSupportFlags::ShaderResource, vaResourceAccessFlags::Default, 
                vaResourceFormat::R32_FLOAT, vaResourceFormat::Automatic, vaResourceFormat::D32_FLOAT );

        // setup and rotate camera
        Camera->SetViewport( vaViewport( renderDevice.GetCurrentBackbufferTexture()->GetWidth(), renderDevice.GetCurrentBackbufferTexture()->GetHeight() ) );
        double timeFromStart = AnimateCamera?application.GetTimeFromStart():4.0;
        Camera->SetPosition( 5.0f * vaVector3{ (float)cos(0.1*timeFromStart), (float)sin(0.1*timeFromStart), 0.5f } );
        Camera->SetOrientationLookAt( { 0, 0, 0 } );
        Camera->Tick( deltaTime, true );
    }

    void DrawOpaque( vaRenderDeviceContext & mainContext, vaRenderOutputs & outputs, vaDrawAttributes & drawAttributes )
    {
        {
            VA_TRACE_CPUGPU_SCOPE( Sky, mainContext );
            // opaque skybox
            Skybox->Draw( mainContext, outputs, drawAttributes );
        }

        {
            VA_TRACE_CPUGPU_SCOPE( Geometry, mainContext );
            // draw meshes
            MeshRenderer->Draw( mainContext, outputs, MeshPlane, vaMatrix4x4::Translation( 0, 0, -0.9f ), *Camera );
            for( const vaMatrix4x4 & transform : TeapotInstances )
                MeshRenderer->Draw( mainContext, outputs, MeshTeapot, transform, *Camera );
        }
    }
};

void Sample11_Basic3DMesh( vaRenderDevice & renderDevice, vaApplicationBase & application, float deltaTime, vaApplicationState applicationState )
{
    static shared_ptr<SimpleSampleShared>   sampleShared;

    if( applicationState == vaApplicationState::Initializing )
    {
        sampleShared = std::make_shared<SimpleSampleShared>( renderDevice, true, false );
        return;
    }
    else if( applicationState == vaApplicationState::ShuttingDown )
    {
        sampleShared = nullptr;
        return;
    }
    assert( applicationState == vaApplicationState::Running );
    
    auto backbufferTex = renderDevice.GetCurrentBackbufferTexture();
    vaRenderDeviceContext & mainContext = *renderDevice.GetMainContext( );

    sampleShared->Tick( renderDevice, application, deltaTime );

    application.TickUI( *sampleShared->Camera );

    // Do the rendering tick and present 
    renderDevice.BeginFrame( deltaTime );

    //currentBackbuffer->ClearRTV( mainContext, vaVector4( 0.0f, 0.0f, 0.0f, 0.0f ) );
    sampleShared->DepthBuffer->ClearDSV( mainContext, true, sampleShared->Camera->GetUseReversedZ( ) ? ( 0.0f ) : ( 1.0f ), false, 0 );

    vaRenderOutputs finalOutputs = vaRenderOutputs::FromRTDepth( backbufferTex, sampleShared->DepthBuffer );

    // needed for rendering of more complex items
    vaDrawAttributes drawAttributes( *sampleShared->Camera, vaDrawAttributes::RenderFlags::None );

    sampleShared->DrawOpaque( mainContext, finalOutputs, drawAttributes );

    // update and draw imgui
    application.DrawUI( mainContext, finalOutputs, nullptr );

    // present the frame, flip the buffers, etc
    renderDevice.EndAndPresentFrame( (application.GetVsync())?(1):(0) );
}

void Sample12_PostProcess( vaRenderDevice & renderDevice, vaApplicationBase & application, float deltaTime, vaApplicationState applicationState )
{
    struct Globals
    {
        shared_ptr<SimpleSampleShared>      SampleShared;
        shared_ptr<vaTexture>               OffscreenRT;
        shared_ptr<vaUISimplePanel>         UIPanel;

        float                               PPHue        = 0.1f;
        float                               PPSaturation = 0.5f;
        float                               PPBrightness = 0.2f;
        float                               PPContrast   = -0.01f;

    };
    static shared_ptr<Globals> globals;

    if( applicationState == vaApplicationState::Initializing )
    {
        assert( globals == nullptr );
        globals = std::make_shared<Globals>( );

        globals->SampleShared = std::make_shared<SimpleSampleShared>( renderDevice, true, false );

        globals->UIPanel = std::make_shared<vaUISimplePanel>( [g = &(*globals)] ( vaApplicationBase & ) 
        {
#ifdef VA_IMGUI_INTEGRATION_ENABLED
            ImGui::InputFloat( "Hue", &g->PPHue, 0.1f );
            ImGui::InputFloat( "Saturation", &g->PPSaturation, 0.1f );
            ImGui::InputFloat( "Brightness", &g->PPBrightness, 0.1f );
            ImGui::InputFloat( "Contrast", &g->PPContrast, 0.1f );
#endif
        },
            "PostProcessSample", 0, true, vaUIPanel::DockLocation::DockedLeft );
        return;
    }
    else if( applicationState == vaApplicationState::ShuttingDown )
    {
        globals = nullptr;
        return;
    }
    assert( applicationState == vaApplicationState::Running );
    
    auto backbufferTex = renderDevice.GetCurrentBackbufferTexture( );
    vaRenderDeviceContext & mainContext = *renderDevice.GetMainContext( );

    globals->SampleShared->Tick( renderDevice, application, deltaTime );

    // Create/update offscreen render target
    if( globals->OffscreenRT == nullptr || globals->OffscreenRT->GetSizeX() != backbufferTex->GetSizeX() || globals->OffscreenRT->GetSizeY() != backbufferTex->GetSizeY() )
        globals->OffscreenRT = vaTexture::Create2D( renderDevice, vaResourceFormat::R8G8B8A8_UNORM_SRGB, backbufferTex->GetSizeX(), backbufferTex->GetSizeY(), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget );

    globals->SampleShared->Tick( renderDevice, application, deltaTime );

    application.TickUI( *globals->SampleShared->Camera );

    // Do the rendering tick and present 
    renderDevice.BeginFrame( deltaTime );

    vaRenderOutputs offscreenOutputs = vaRenderOutputs::FromRTDepth( globals->OffscreenRT, globals->SampleShared->DepthBuffer );
    //currentBackbuffer->ClearRTV( mainContext, vaVector4( 0.0f, 0.0f, 0.0f, 0.0f ) );
    offscreenOutputs.DepthStencil->ClearDSV( mainContext, true, globals->SampleShared->Camera->GetUseReversedZ( ) ? ( 0.0f ) : ( 1.0f ), false, 0 );

    // needed for rendering of more complex items
    vaDrawAttributes drawAttributes( *globals->SampleShared->Camera, vaDrawAttributes::RenderFlags::None );

    globals->SampleShared->DrawOpaque( mainContext, offscreenOutputs, drawAttributes );

    // copy contents of offscreen rendertarget to the backbuffer
    // renderDevice.GetMainContext()->CopySRVToRTV( backbufferTex, globals->OffscreenRT );
    renderDevice.GetPostProcess().ColorProcessHSBC( mainContext, renderDevice.GetCurrentBackbuffer( ), globals->OffscreenRT, globals->PPHue, globals->PPSaturation, globals->PPBrightness, globals->PPContrast );

    // update and draw imgui
    application.DrawUI( *renderDevice.GetMainContext( ), renderDevice.GetCurrentBackbuffer( ), globals->SampleShared->DepthBuffer );

    // present the frame, flip the buffers, etc
    renderDevice.EndAndPresentFrame( (application.GetVsync())?(1):(0) );
}

void Sample13_Tonemap( vaRenderDevice & renderDevice, vaApplicationBase & application, float deltaTime, vaApplicationState applicationState )
{
    struct Globals
    {
        shared_ptr<SimpleSampleShared>      SampleShared;
        shared_ptr<vaTexture>               OffscreenRT;
        shared_ptr<vaUISimplePanel>         UIPanel;

        shared_ptr<vaPostProcessTonemap>    Tonemap;
    };
    static shared_ptr<Globals> globals;

    if( applicationState == vaApplicationState::Initializing )
    {
        assert( globals == nullptr );
        globals = std::make_shared<Globals>( );

        globals->SampleShared = std::make_shared<SimpleSampleShared>( renderDevice, true, false );

        globals->Tonemap         = renderDevice.CreateModule<vaPostProcessTonemap>();

        globals->UIPanel = std::make_shared<vaUISimplePanel>( [&] ( vaApplicationBase & application )
        {
#ifdef VA_IMGUI_INTEGRATION_ENABLED
            ImGui::Text( "Tone mapping settings are part of camera settings:" );
            globals->SampleShared->Camera->UIPanelTickCollapsable( application, true, true, false );
#endif
        },
            "Tone mapping sample", 0, true, vaUIPanel::DockLocation::DockedLeft );
        return;
    }
    else if( applicationState == vaApplicationState::ShuttingDown )
    {
        globals = nullptr;
        return;
    }
    assert( applicationState == vaApplicationState::Running );
    
    auto backbufferTex = renderDevice.GetCurrentBackbufferTexture( );
    vaRenderDeviceContext & mainContext = *renderDevice.GetMainContext( );

    globals->SampleShared->Tick( renderDevice, application, deltaTime );

    // Create/update offscreen render target
    if( globals->OffscreenRT == nullptr || globals->OffscreenRT->GetSizeX() != backbufferTex->GetSizeX() || globals->OffscreenRT->GetSizeY() != backbufferTex->GetSizeY() )
        globals->OffscreenRT = vaTexture::Create2D( renderDevice, vaResourceFormat::R11G11B10_FLOAT, backbufferTex->GetSizeX(), backbufferTex->GetSizeY(), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget );

    globals->SampleShared->Tick( renderDevice, application, deltaTime );

    application.TickUI( *globals->SampleShared->Camera );

    // Do the rendering tick and present 
    renderDevice.BeginFrame( deltaTime );

	// This is important - it readbacks luminance from the last Tonemap->TickAndApplyCameraPostProcess and computes exposure for the frame
    globals->SampleShared->Camera->PreRenderTick( mainContext, deltaTime );

    vaRenderOutputs offscreenOutputs = vaRenderOutputs::FromRTDepth( globals->OffscreenRT, globals->SampleShared->DepthBuffer );
    //currentBackbuffer->ClearRTV( mainContext, vaVector4( 0.0f, 0.0f, 0.0f, 0.0f ) );
    offscreenOutputs.DepthStencil->ClearDSV( mainContext, true, globals->SampleShared->Camera->GetUseReversedZ( ) ? ( 0.0f ) : ( 1.0f ), false, 0 );

    // needed for rendering of more complex items
    vaDrawAttributes drawAttributes( *globals->SampleShared->Camera, vaDrawAttributes::RenderFlags::None );

    globals->SampleShared->DrawOpaque( mainContext, offscreenOutputs, drawAttributes );

    // copy contents of offscreen rendertarget while performing tonemapping
    renderDevice.GetMainContext()->CopySRVToRTV( backbufferTex, globals->OffscreenRT );
    globals->Tonemap->TickAndApplyCameraPostProcess( mainContext, *globals->SampleShared->Camera, backbufferTex, globals->OffscreenRT );

    // update and draw imgui
    application.DrawUI( *renderDevice.GetMainContext( ), renderDevice.GetCurrentBackbuffer( ), globals->SampleShared->DepthBuffer );

    // present the frame, flip the buffers, etc
    renderDevice.EndAndPresentFrame( (application.GetVsync())?(1):(0) );
}

void Sample14_SSAO( vaRenderDevice & renderDevice, vaApplicationBase & application, float deltaTime, vaApplicationState applicationState )
{
    struct Globals
    {
        shared_ptr<SimpleSampleShared>      SampleShared;
        shared_ptr<vaTexture>               OffscreenRT;
        shared_ptr<vaTexture>               SSAORT;
        shared_ptr<vaUISimplePanel>         UIPanel;

        bool                                SSAOEnabled = true;
        shared_ptr<vaGTAO>                  SSAO;
    };
    static shared_ptr<Globals> globals;

    if( applicationState == vaApplicationState::Initializing )
    {
        assert( globals == nullptr );
        globals = std::make_shared<Globals>( );

        globals->SampleShared = std::make_shared<SimpleSampleShared>( renderDevice, false, true );

        globals->SSAO = renderDevice.CreateModule<vaGTAO>( );

        globals->UIPanel = std::make_shared<vaUISimplePanel>( [&] ( vaApplicationBase & application ) 
        {
#ifdef VA_IMGUI_INTEGRATION_ENABLED
            ImGui::Checkbox( "Animate camera", &globals->SampleShared->AnimateCamera );
            ImGui::Checkbox( "Enable XeGTAO", &globals->SSAOEnabled );
            if( globals->SSAOEnabled )
                globals->SSAO->UIPanelTickCollapsable( application, true, true, false );
#endif
        },
            "XeGTAO micro sample", 0, true, vaUIPanel::DockLocation::DockedLeft );
        return;
    }
    else if( applicationState == vaApplicationState::ShuttingDown )
    {
        globals = nullptr;
        return;
    }
    assert( applicationState == vaApplicationState::Running );

    auto backbufferTex = renderDevice.GetCurrentBackbufferTexture( );
    vaRenderDeviceContext & mainContext = *renderDevice.GetMainContext( );

    globals->SampleShared->Tick( renderDevice, application, deltaTime );

    // Create/update offscreen render target
    if( globals->OffscreenRT == nullptr || globals->OffscreenRT->GetSizeX() != backbufferTex->GetSizeX() || globals->OffscreenRT->GetSizeY() != backbufferTex->GetSizeY() )
    {
        globals->OffscreenRT = vaTexture::Create2D( renderDevice, vaResourceFormat::R8G8B8A8_UNORM_SRGB, backbufferTex->GetSizeX(), backbufferTex->GetSizeY(), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget );
        globals->SSAORT = vaTexture::Create2D( renderDevice, vaResourceFormat::R8_UNORM, backbufferTex->GetSizeX(), backbufferTex->GetSizeY(), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess );
    }

    globals->SampleShared->Tick( renderDevice, application, deltaTime );

    application.TickUI( *globals->SampleShared->Camera );

    // Do the rendering tick and present 
    renderDevice.BeginFrame( deltaTime );

    vaRenderOutputs offscreenOutputs = vaRenderOutputs::FromRTDepth( globals->OffscreenRT, globals->SampleShared->DepthBuffer );
    //currentBackbuffer->ClearRTV( mainContext, vaVector4( 0.0f, 0.0f, 0.0f, 0.0f ) );
    offscreenOutputs.DepthStencil->ClearDSV( mainContext, true, globals->SampleShared->Camera->GetUseReversedZ( ) ? ( 0.0f ) : ( 1.0f ), false, 0 );

    // needed for rendering of more complex items
    vaDrawAttributes drawAttributes( *globals->SampleShared->Camera, vaDrawAttributes::RenderFlags::None );

    globals->SampleShared->DrawOpaque( mainContext, offscreenOutputs, drawAttributes );

    globals->SSAO->Compute( mainContext, *globals->SampleShared->Camera, false, false, globals->SSAORT, globals->SampleShared->DepthBuffer, nullptr );

    // apply SSAO term.. or not
    if( globals->SSAOEnabled )
    {
        vaPostProcess & pp = renderDevice.GetPostProcess();
        if( globals->SSAO->DebugShowEdges() || globals->SSAO->DebugShowNormals() || globals->SSAO->ReferenceRTAOEnabled() )
            pp.MergeTextures( mainContext, backbufferTex, globals->OffscreenRT, globals->SSAO->DebugImage(), nullptr, "float4( srcB.xyz, 1.0 )" );
        else
            pp.MergeTextures( mainContext, backbufferTex, globals->OffscreenRT, globals->SSAORT, nullptr, "float4( srcA.rgb * srcB.xxx, 1.0 )" );
    }
    else
        renderDevice.GetMainContext()->CopySRVToRTV( backbufferTex, globals->OffscreenRT );
 
    // update and draw imgui
    application.DrawUI( *renderDevice.GetMainContext( ), renderDevice.GetCurrentBackbuffer( ), globals->SampleShared->DepthBuffer );

    // present the frame, flip the buffers, etc
    renderDevice.EndAndPresentFrame( (application.GetVsync())?(1):(0) );
}

void Sample15_BasicScene( vaRenderDevice & renderDevice, vaApplicationBase & application, float deltaTime, vaApplicationState applicationState )
{
    struct Globals
    {
        shared_ptr<vaUISimplePanel>         UIPanel;

        shared_ptr<vaScene>                 Scene;                  // this has most of the thingies
        shared_ptr<vaSceneRenderer>         SceneRenderer;          // this draws the scene
        shared_ptr<vaSceneMainRenderView>   SceneMainView;          // this is where the SceneRenderer draws the scene

        shared_ptr<vaRenderMesh>            MeshTeapot;
        shared_ptr<vaRenderMesh>            MeshPlane;

        shared_ptr<vaRenderCamera>          Camera;
        bool                                AnimateCamera   = true;
    };
    static shared_ptr<Globals> globals;

    if( applicationState == vaApplicationState::Initializing )
    {
        assert( globals == nullptr );
        globals = std::make_shared<Globals>( );

        globals->Camera = std::make_shared<vaRenderCamera>( renderDevice, false ); //renderDevice.CreateModule<vaRenderCamera>();
        globals->Camera->SetYFOV( 65.0f / 180.0f * VA_PIf );

        globals->MeshPlane  = vaRenderMesh::CreatePlane( renderDevice, vaMatrix4x4::Identity, 500.0f, 500.0f );
        globals->MeshTeapot = vaRenderMesh::CreateTeapot( renderDevice, vaMatrix4x4::Translation( 0, 0, 0.9f ) );

        globals->Scene           = std::make_shared<vaScene>( );
        globals->SceneRenderer   = renderDevice.CreateModule<vaSceneRenderer>( );
        globals->SceneMainView   = globals->SceneRenderer->CreateMainView( );
        globals->SceneRenderer->SetScene( globals->Scene );

        // build up the scene from scratch
        {
            // Sky
            entt::entity skyboxEntity = globals->Scene->CreateEntity( "DistantIBL" );
            Scene::DistantIBLProbe & distantIBL = globals->Scene->Registry( ).emplace<Scene::DistantIBLProbe>( skyboxEntity, Scene::DistantIBLProbe{} );
            distantIBL.SetImportFilePath( vaCore::GetMediaRootDirectoryNarrow( ) + "noon_grass_2k.hdr" );
            //            Scene->GetDistantIBL( ).SetImportFilePath( mediaPath + "sky_cube.dds" );

            // Plane mesh
            globals->Scene->CreateEntity( "Plane",   vaMatrix4x4::Identity, entt::null, globals->MeshPlane->UIDObject_GetUID() );

            // Teapots
            bool thousandsOfTeapots = true;
            if( thousandsOfTeapots )
            {
                vaRandom rnd;
                for( int x = 0; x < 41; x++ )
                    for( int y = 0; y < 41; y++ )
                    {
                        vaMatrix4x4 transform = vaMatrix4x4::RotationZ( rnd.NextFloat() * VA_PIf ) * vaMatrix4x4::Translation( { (x-20) * 3.5f, (y-20) * 3.5f, 0.0f } );
                        globals->Scene->CreateEntity( "Teapot", transform, entt::null, globals->MeshTeapot->UIDObject_GetUID() );
                    }
            }
            else
            {
                globals->Scene->CreateEntity( "Teapot", vaMatrix4x4::Identity, entt::null, globals->MeshTeapot->UIDObject_GetUID() );
            }
        }

        globals->UIPanel = std::make_shared<vaUISimplePanel>( [&] ( vaApplicationBase & application ) 
        {
            application;
#ifdef VA_IMGUI_INTEGRATION_ENABLED
            ImGui::Checkbox( "Animate camera", &globals->AnimateCamera );
                //globals->SSAO->UIPanelTickCollapsable( application, true, true, false );
#endif
        },
            "Basic scene sample", 0, true, vaUIPanel::DockLocation::DockedLeft );
        return;
    }
    else if( applicationState == vaApplicationState::ShuttingDown )
    {
        globals = nullptr;
        return;
    }
    assert( applicationState == vaApplicationState::Running );

    VA_TRACE_CPU_SCOPE( MainLoop );

    auto backbufferTexture = renderDevice.GetCurrentBackbufferTexture( );
    if( backbufferTexture == nullptr )
        { vaThreading::Sleep(10); return; }
    vaViewport mainViewport( backbufferTexture->GetWidth( ), backbufferTexture->GetHeight( ) );

    {
        // setup and rotate camera
        globals->Camera->SetViewport( mainViewport );
        double timeFromStart = globals->AnimateCamera?application.GetTimeFromStart():4.0;
        globals->Camera->SetPosition( 5.0f * vaVector3{ (float)cos(0.1*timeFromStart), (float)sin(0.1*timeFromStart), 0.5f } );
        globals->Camera->SetOrientationLookAt( { 0, 0, 0 } );
        globals->Camera->Tick( deltaTime, true );
        globals->SceneMainView->Camera()->FromOther( *globals->Camera );
        globals->SceneMainView->Camera()->Tick( deltaTime, application.HasFocus( ) ); //&& !freezeMotionAndInput );
    }

    application.TickUI( *globals->Camera );

    {
        VA_TRACE_CPU_SCOPE( SceneTick );
        globals->Scene->TickBegin( deltaTime, application.GetCurrentTickIndex() );
        globals->Scene->TickEnd( );
    }

    {
        // Draw grid using debug drawing system
        auto & canvas3D = renderDevice.GetCanvas3D( );
        canvas3D.DrawAxis( vaVector3( 0, 0, 0 ), 10000.0f, NULL, 0.3f );
        float zoffset = 0.01f;

        for( float gridStep = 1.0f; gridStep <= 1000; gridStep *= 10 )
        {
            int gridCount = 10;
            for( int i = -gridCount; i <= gridCount; i++ )
            {
                canvas3D.DrawLine( { i * gridStep, -gridCount * gridStep, zoffset }, { i * gridStep, +gridCount * gridStep, 0.0f }, 0x80000000 );
                canvas3D.DrawLine( { -gridCount * gridStep, i * gridStep, zoffset }, { +gridCount * gridStep, i * gridStep, 0.0f }, 0x80000000 );
            }
        }
    }

    {
        // Do the rendering tick and present 
        renderDevice.BeginFrame( deltaTime );
        vaRenderDeviceContext & renderContext = *renderDevice.GetMainContext( );

        // VA_TRACE_CPUGPU_SCOPE( RenderFrame, renderContext );

        globals->SceneRenderer->RenderTick( deltaTime, application.GetCurrentTickIndex() );

        const shared_ptr<vaTexture> & finalColor = globals->SceneMainView->GetOutputColor( );
        //    const shared_ptr<vaTexture> & finalDepth     = m_sceneMainView->GetTextureDepth();

        // this is possible
        if( finalColor == nullptr )
        {
            backbufferTexture->ClearRTV( renderContext, { 0.5f, 0.5f, 0.5f, 1.0f } );
        }
        else
        {
            VA_TRACE_CPUGPU_SCOPE( FinalApply, renderContext );

            renderDevice.StretchRect( renderContext, backbufferTexture, finalColor, vaVector4( (float)0.0f, (float)0.0f, (float)mainViewport.Width, (float)mainViewport.Height ), vaVector4( 0.0f, 0.0f, (float)mainViewport.Width, (float)mainViewport.Height ), false );
        }

        // update and draw imgui
        application.DrawUI( *renderDevice.GetMainContext( ), renderDevice.GetCurrentBackbuffer( ), globals->SceneMainView->GetOutputDepth() );

        // present the frame, flip the buffers, etc
        renderDevice.EndAndPresentFrame( ( application.GetVsync( ) ) ? ( 1 ) : ( 0 ) );
    }
}

#include "Rendering/Effects/vaSimpleParticles.h"
void Sample16_Particles( vaRenderDevice & renderDevice, vaApplicationBase & application, float deltaTime, vaApplicationState applicationState )
{
    renderDevice; application; deltaTime; applicationState;

    if( applicationState == vaApplicationState::Running )
    {
        application.TickUI( );

        renderDevice.BeginFrame( deltaTime );

        renderDevice.GetCurrentBackbufferTexture( )->ClearRTV( *renderDevice.GetMainContext( ), vaVector4( 0.5f, 0.7f, 0.5f, 0.0f ) );

        // update and draw imgui
        application.DrawUI( *renderDevice.GetMainContext( ), renderDevice.GetCurrentBackbuffer( ), nullptr );

        // present the frame, flip the buffers, etc
        renderDevice.EndAndPresentFrame( ( application.GetVsync( ) ) ? ( 1 ) : ( 0 ) );
    }

#if 0
    struct Globals
    {
        // settings
        const float                                 CubesPlacementRadius        = 11.0f;
        const int                                   BlockCount                  = 13;
        bool                                        Pause                       = true;
        bool                                        CameraRotate                = true;
        enum PSC : int { ParticleShaderComplexity_Low = 0, ParticleShaderComplexity_Medium = 1, ParticleShaderComplexity_High = 2 }
                                                    ParticleShaderComplexity    = ParticleShaderComplexity_Low;
        enum PDT : int { ParticleDrawTechnique_Basic = 0, ParticleDrawTechnique_OffscreenFullRes = 1, ParticleDrawTechnique_OffscreenHalfByHalfRes = 2, ParticleDrawTechnique_OffscreenQuarterByQuarterRes = 3,
                            ParticleDrawTechnique_VRS2x2 = 4, ParticleDrawTechnique_VRS4x4 = 5 }
                                                    ParticleDrawTechnique       = ParticleDrawTechnique_OffscreenQuarterByQuarterRes;
        enum OUF : int { OffscreenUpsampleFilter_Debug = 0, OffscreenUpsampleFilter_SimpleBilinear = 1, OffscreenUpsampleFilter_DepthAwareBilinear = 2 }
                                                    OffscreenUpsampleFilter     = OffscreenUpsampleFilter_DepthAwareBilinear;
        bool                                        ParticleEnableVolumetric    = true;
        bool                                        Wireframe                   = false;
        vaDepthFilterMode                           DepthDownsampleFilter       = vaDepthFilterMode::Farthest;
        int                                         RenderTargetFormat          = 0;    // 0 == R11G11B10_FLOAT, 1 == R16G16B16A16_FLOAT <- TODO: add enum, other formats if needed?

        // various vars
        double                                      AnimTime        = 0.0f;
        double                                      CameraAnimTime  = 4.5f;

        // needed stuff
        shared_ptr<vaSkybox>                        Skybox;
        shared_ptr<vaTexture>                       SkyboxTexture;
        shared_ptr<vaRenderCamera>                  Camera;
        shared_ptr<vaCameraControllerFreeFlight>    CameraFreeFlightController;
        shared_ptr<vaRenderMesh>                    MeshTeapot;
        shared_ptr<vaRenderMesh>                    MeshPlane;
        shared_ptr<vaRenderMesh>                    MeshBox;
        shared_ptr<vaRenderMesh>                    MeshSphere;
        vaRenderInstanceList                           MeshList;
        shared_ptr<vaSceneLighting>                      Lighting;
        shared_ptr<vaLight>                         ShinyLightRed;
        shared_ptr<vaLight>                         ShinyLightBlue;
        shared_ptr<vaTexture>                       PreTonemapRT;
        shared_ptr<vaTexture>                       PostTonemapRT;
        shared_ptr<vaTexture>                       DepthBuffer;
        shared_ptr<vaPostProcessTonemap>            Tonemap;
        shared_ptr<vaUISimplePanel>                 UIPanel;

        shared_ptr<vaTexture>                       ParticlesInputViewspaceDepths;
        shared_ptr<vaTexture>                       ParticlesOffscreenColor;

        shared_ptr<vaTexture>                       ParticleAlbedoAlphaTexture;
        shared_ptr<vaTexture>                       ParticleNormalmapTexture;

        shared_ptr<vaSimpleParticleSystem>          ParticleSystem;
        shared_ptr<vaSimpleParticleEmitter>         ParticleEmitter0;

        shared_ptr<vaRenderMaterial>                ShinySphereMaterial;
        shared_ptr<vaRenderMaterial>                ParticleMaterials[3];
        shared_ptr<vaRenderMaterial>                VolumetricParticleMaterials[_countof(ParticleMaterials)];

        shared_ptr<vaZoomTool>                      ZoomTool;
        shared_ptr<vaImageCompareTool>              ImageCompareTool;

    };
    static shared_ptr<Globals> globals;

    if( applicationState == vaApplicationState::Initializing )
    {
        assert( globals == nullptr );
        globals = std::make_shared<Globals>( );

        // Sky
        {
            globals->Skybox = renderDevice.CreateModule<vaSkybox>();

            globals->SkyboxTexture = vaTexture::CreateFromImageFile( renderDevice, vaCore::GetExecutableDirectory() + L"Media\\sky_cube.dds", vaTextureLoadFlags::Default );

            globals->Skybox->SetCubemap( globals->SkyboxTexture );

            // scale brightness
            globals->Skybox->Settings().ColorMultiplier    = 0.05f;
        }

        // Camera
        {
            globals->Camera = std::make_shared<vaRenderCamera>( renderDevice, true );
            globals->Camera->SetYFOV( 65.0f / 180.0f * VA_PIf );
            globals->Camera->SetPosition( {0, 0, 0} );

            globals->CameraFreeFlightController    = std::shared_ptr<vaCameraControllerFreeFlight>( new vaCameraControllerFreeFlight() );
            globals->CameraFreeFlightController->SetMoveWhileNotCaptured( false );

            auto & renderCameraSettings = globals->Camera->Settings();

            renderCameraSettings.GeneralSettings.AutoExposureAdaptationSpeed = 8.0f;
            renderCameraSettings.BloomSettings.UseBloom         = true;
            renderCameraSettings.BloomSettings.BloomMultiplier  = 0.2f;
            renderCameraSettings.BloomSettings.BloomSize        = 0.3f;
        }

        // Lighting
        {
            globals->Lighting            = renderDevice.CreateModule<vaSceneLighting>();
            std::vector<shared_ptr<vaLight>> lights;
            lights.push_back( std::make_shared<vaLight>( vaLight::MakeAmbient( "DefaultAmbient", vaVector3( 0.05f, 0.05f, 0.05f ), 1.0f ) ) );
            // lights.push_back( std::make_shared<vaLight>( vaLight::MakeDirectional( "DefaultDirectional", vaVector3( 1.0f, 1.0f, 0.9f ), vaVector3( 0.0f, -1.0f, -1.0f ).Normalized() ) ) );
            lights.push_back( globals->ShinyLightRed = std::make_shared<vaLight>( vaLight::MakePoint( "ShinyLightRed", 0.6f, vaVector3( 300.0f, 200.0f, 50.0f ), 1.0f, vaVector3( 0.0f, 2.0f, 2.0f ) ) ) );
            globals->ShinyLightRed->CastShadows = true; 
            lights.push_back( globals->ShinyLightBlue = std::make_shared<vaLight>( vaLight::MakePoint( "ShinyLightBlue", 0.6f, vaVector3( 5.0f, 200.0f, 300.0f ), 1.0f, vaVector3( 0.0f, 2.0f, 2.0f ) ) ) );
            globals->ShinyLightBlue->CastShadows = true; 
            globals->Lighting->SetLights( lights );
        }
        
        // Scene objects
        globals->MeshTeapot = vaRenderMesh::CreateTeapot( renderDevice, vaMatrix4x4::Identity );
        globals->MeshPlane  = vaRenderMesh::CreatePlane( renderDevice, vaMatrix4x4::Identity, 500.0f, 500.0f, true );
        globals->MeshBox    = vaRenderMesh::CreateCube( renderDevice, vaMatrix4x4::Scaling( 1.0f, 1.0f, 10.0f ), false );
        globals->MeshSphere = vaRenderMesh::CreateSphere( renderDevice, vaMatrix4x4::Scaling( 0.5f, 0.5f, 0.5f ), 2, true );

        // Post processing
        {
            globals->Tonemap         = renderDevice.CreateModule<vaPostProcessTonemap>();
        }

        // Particles
        {
            globals->ParticleSystem = std::make_shared<vaSimpleParticleSystem>( renderDevice );

            float spawnMultiplier = 6.0f;

            vaSimpleParticleEmitter::EmitterSettings teapotSmokeSettings;
            teapotSmokeSettings.SpawnAreaBoundingBox                = vaOrientedBoundingBox( vaVector3( 1.5f, 0.0f, 0.4f+0.05f ), vaVector3( 0.1f, 0.1f, 0.1f ), vaMatrix3x3::Identity );
            teapotSmokeSettings.SpawnAreaType                       = vaSimpleParticleEmitter::SAT_BoundingBox;
            teapotSmokeSettings.SpawnFrequencyPerSecond             = 400.0f * spawnMultiplier;
            teapotSmokeSettings.SpawnSize                           = 0.1f;
            teapotSmokeSettings.SpawnSizeRandomAddSub               = 0.1f;
            teapotSmokeSettings.SpawnSizeChange                     = 0.05f;
            teapotSmokeSettings.SpawnVelocity                       = vaVector3( 0.0f, 0.0f, 1.2f );
            teapotSmokeSettings.SpawnVelocityRandomAddSub           = vaVector3( 0.2f, 0.2f, 0.2f );
            teapotSmokeSettings.SpawnLife                           = 4.0f;
            teapotSmokeSettings.SpawnLifeRandomAddSub               = 4.0f;
            teapotSmokeSettings.SpawnColor                          = vaVector4( 0.5f, 2.0f, 0.5f, 0.6f );  // nice flourescent green smoke
            teapotSmokeSettings.SpawnColorRandomAddSub              = vaVector4( 0.0f, 0.0f, 0.0f, 0.0f );
            teapotSmokeSettings.SpawnAngleRandomAddSub              = VA_PIf * 3.0f;
            teapotSmokeSettings.SpawnAngularVelocityRandomAddSub    = VA_PIf * 0.5f;
            teapotSmokeSettings.SpawnAffectedByWindK                = 1.0f;

            // teapot smoke emitter
            globals->ParticleEmitter0 = globals->ParticleSystem->CreateEmitter( L"TeapotSmoke" );
            globals->ParticleEmitter0->Settings = teapotSmokeSettings;

            //
            vaSimpleParticleEmitter::EmitterSettings smokeSettings;
            smokeSettings.SpawnAreaType                     = vaSimpleParticleEmitter::SAT_BoundingBox;
            smokeSettings.SpawnFrequencyPerSecond           = 45.0f * spawnMultiplier;
            smokeSettings.SpawnSize                         = 0.1f;
            smokeSettings.SpawnSizeRandomAddSub             = 0.1f;
            smokeSettings.SpawnSizeChange                   = 0.1f;
            smokeSettings.SpawnVelocityRandomAddSub         = vaVector3( 0.5f, 0.5f, 0.0f );
            smokeSettings.SpawnLife                         = 15.0f;
            smokeSettings.SpawnLifeRandomAddSub             = 6.0f;
            smokeSettings.SpawnColor                        = vaVector4( 0.3f, 0.3f, 0.3f, 0.9f );
            smokeSettings.SpawnColorRandomAddSub            = vaVector4( 0.05f, 0.05f, 0.05f, 0.0f );
            smokeSettings.SpawnAngleRandomAddSub            = VA_PIf;
            smokeSettings.SpawnAngularVelocityRandomAddSub  = VA_PIf * 0.5f;

            // ground smoke
            for( int i = 0; i < globals->BlockCount; i++ )
            {
                float smokeDistMult = 0.85f;
                smokeSettings.SpawnAreaBoundingBox = vaOrientedBoundingBox( vaVector3( smokeDistMult * globals->CubesPlacementRadius * cosf( 2*VA_PIf * (i/(float)(globals->BlockCount-1)) ), smokeDistMult * globals->CubesPlacementRadius * (float)sin(-2*VA_PIf * (i/(float)(globals->BlockCount-1))), -1.0f), 
                                                                            vaVector3( 0.5f, 0.5f, 1.0f ), vaMatrix3x3::Identity );
                smokeSettings.SpawnVelocity                 = { 0.0f, 0.0f, 0.2f }; //vaVector3::ComponentMul( (vaVector3::RandomNormal() + vaVector3( 0, 0, 1 )).Normalized(), { 1.0f, 1.0f, 0.2f } );
                smokeSettings.SpawnAffectedByGravityK       = 0.003f;
                shared_ptr<vaSimpleParticleEmitter> emitter = globals->ParticleSystem->CreateEmitter( );
                emitter->Settings = smokeSettings;
            }

            globals->ParticleSystem->Settings().Wind = vaVector3( 0.0f, 0.1f, 0.0f );

            // initialize with contents so we don't have to wait for the smoke to fill-up
#ifdef _DEBUG
            for( int i = 0; i < 10; i++ )
#else
            for( int i = 0; i < 1000; i++ )
#endif
            {
                globals->ParticleSystem->Tick( 0.03f );
                //globals->ParticleSystem->Sort( globals->Camera->GetPosition( ), true );
            }


        }

        // Materials
        {
            // just for lights
            globals->ShinySphereMaterial = renderDevice.GetMaterialManager().CreateRenderMaterial();
            globals->ShinySphereMaterial->SetupFromPreset( "Legacy" );
            // this is to make the balls shiny
            globals->ShinySphereMaterial->SetInputSlot( "Emissive", vaVector4( 0.2f, 0.2f, 0.2f, 1.0f ), true, true );
            auto ssms = globals->ShinySphereMaterial->GetMaterialSettings();
            ssms.SpecialEmissiveLight = true;
            globals->ShinySphereMaterial->SetMaterialSettings(ssms);
            globals->MeshSphere->SetMaterial( globals->ShinySphereMaterial );
            

            globals->ParticleAlbedoAlphaTexture = vaTexture::CreateFromImageFile( renderDevice, vaCore::GetExecutableDirectory() + L"Media\\smoke_basic.dds", vaTextureLoadFlags::Default );
            globals->ParticleNormalmapTexture   = vaTexture::CreateFromImageFile( renderDevice, vaCore::GetExecutableDirectory() + L"Media\\smoke_basic_nm.dds", vaTextureLoadFlags::Default, vaResourceBindSupportFlags::ShaderResource, vaTextureContentsType::NormalsXY_UNORM );

            auto initParticleMaterial = [ g = &(*globals) ]( vaRenderMaterial & material, Globals::PSC complexity, bool volumetric )
            {
                material.SetInputSlot( "Albedo", vaVector4( 1.0f, 1.0f, 1.0f, 1.0f ), true, true );
                material.SetTextureNode( "AlbedoTexture", *globals->ParticleAlbedoAlphaTexture, vaStandardSamplerType::AnisotropicWrap );
                material.ConnectInputSlotWithNode( "Albedo", "AlbedoTexture" );

                if( complexity == Globals::ParticleShaderComplexity_High )
                {
                    material.SetInputSlot( "Normalmap", vaVector4( 0.0f, 0.0f, 1.0f, 0.0f ), false, false );
                    material.SetTextureNode( "NormalmapTexture", *globals->ParticleAlbedoAlphaTexture, vaStandardSamplerType::AnisotropicWrap );
                    material.ConnectInputSlotWithNode( "Normalmap", "NormalmapTexture" );
                }

                // this is to fake in some subsurface scattering
                material.SetInputSlot( "SubsurfaceScatterHack", 0.3f, false, false );

                auto matSettings = material.GetMaterialSettings();
                auto shSettings = material.GetShaderSettings();

                matSettings.FaceCull                = vaFaceCull::None;
                matSettings.AdvancedSpecularShader  = false;
                matSettings.ReceiveShadows          = complexity == Globals::ParticleShaderComplexity_High;
                // matSettings.AlphaTest               = true;
                // matSettings.AlphaTestThreshold      = 0.005f;
                // matSettings.Transparent             = true;
                matSettings.LayerMode = vaLayerMode::Transparent;

                if( complexity == Globals::ParticleShaderComplexity_Low )
                    shSettings.BaseMacros.push_back( { "SIMPLE_PARTICLES_NO_LIGHTING", "" } );
                if( volumetric )
                    shSettings.BaseMacros.push_back( { "SIMPLE_PARTICLES_VOLUMETRIC_DEPTH", "" } );

                if( complexity == Globals::ParticleShaderComplexity_High )
                    shSettings.BaseMacros.push_back( { "SIMPLE_PARTICLES_FANCY_VOLUME_COMPUTE", "" } );

                //shSettings.VS_Standard              = std::make_pair( "vaSimpleParticles.hlsl",   "SimpleParticleVS"     );   <- VS not required for particles, handled by the vaSimpleParticles
                shSettings.GS_Standard              = std::make_pair( "vaSimpleParticles.hlsl",   "SimpleParticleGS"     );
                shSettings.PS_Forward               = std::make_pair( "vaSimpleParticles.hlsl", "SimpleParticlePS"      );

                //shSettings.PS_Forward ...
                material.SetMaterialSettings(matSettings);
                material.SetShaderSettings(shSettings);
            };

            for( int i = 0; i < _countof(globals->ParticleMaterials); i++ )
            {
                globals->ParticleMaterials[i] = renderDevice.GetMaterialManager().CreateRenderMaterial();
                initParticleMaterial( *globals->ParticleMaterials[i], (Globals::PSC)i, false );
                globals->VolumetricParticleMaterials[i] = renderDevice.GetMaterialManager().CreateRenderMaterial();
                initParticleMaterial( *globals->VolumetricParticleMaterials[i], (Globals::PSC)i, true );
            }
        }

        // Misc
        {
            globals->ZoomTool = std::make_shared<vaZoomTool>( renderDevice );
            globals->ImageCompareTool = std::make_shared<vaImageCompareTool>( renderDevice );
        }

        // UI
        globals->UIPanel = std::make_shared<vaUISimplePanel>( [g = &(*globals)] ( vaApplicationBase & app ) 
        {
            app;
#ifdef VA_IMGUI_INTEGRATION_ENABLED
            ImGui::TextColored( { 1,0,0,1 }, "WARNING: PARTICLE SHADING IS BROKEN" );
            ImGui::Text( "(broken during viewspace->worldspace conversion)" );
            ImGui::Text( "(it needs fixing to PBR style shading anyway)" );
            ImGui::Separator();
            ImGui::Checkbox( "Pause", &g->Pause );
            ImGui::Checkbox( "Rotate camera", &g->CameraRotate );
            ImGui::Separator();
            ImGui::Text( "Particle count: %d", (int)g->ParticleSystem->GetParticles().size() );
            ImGui::Text( "Particle shader complexity: " );
            ImGui::Indent(); ImGuiEx_ListBox( "#Particle shader complexity", (int&)g->ParticleShaderComplexity, {"Low", "Medium", "High" }, -1, true );                 ImGui::Unindent();
            ImGui::Text( "Particle draw technique: " );

            auto prevParticleDrawTechnique = g->ParticleDrawTechnique;
            ImGui::Indent(); ImGuiEx_ListBox( "#Particle draw technique", (int&)g->ParticleDrawTechnique, 
                {"Basic", "Offscreen full resolution (for debugging)", "Offscreen 1/2 x 1/2 resolution", "Offscreen 1/4 x 1/4 resolution", "VRS 2x2", "VRS 4x4" }, -1, true );  ImGui::Unindent();
          
            while( !app.GetRenderDevice().GetCapabilities().VariableShadingRate.Tier1 &&
                (g->ParticleDrawTechnique == Globals::ParticleDrawTechnique_VRS2x2 || g->ParticleDrawTechnique == Globals::ParticleDrawTechnique_VRS4x4) )
            {
                VA_WARN( "'Variable Shading Rate Tier 1' not supported by the device." );
                g->ParticleDrawTechnique = prevParticleDrawTechnique;
                prevParticleDrawTechnique = Globals::ParticleDrawTechnique_Basic;
            }
            if( g->ParticleDrawTechnique != Globals::ParticleDrawTechnique_Basic )
            {
                ImGui::Text( "Offscreen depth downsample filter: " );
                ImGui::Indent(); ImGuiEx_ListBox( "#DepthFilterMode", (int&)g->DepthDownsampleFilter, {"Closest", "Farthest", "Linear Average" }, -1, true );                 ImGui::Unindent();

                ImGui::Text( "Offscreen color upsample filter: " );
                ImGui::Indent(); ImGuiEx_ListBox( "#ColorFilterMode", (int&)g->OffscreenUpsampleFilter, {"Debug, no filter", "Simple Bilinear", "Depth Aware Bilinear" }, -1, true );                 ImGui::Unindent();
            }

            ImGui::Text( "Render target format:" );
            ImGui::Indent( ); 
            int prevRTFormat = g->RenderTargetFormat;
            if( ImGuiEx_ListBox( "#RenderTargetFormat", (int&)g->RenderTargetFormat, { "R11G11B10_FLOAT", "R16G16B16A16_FLOAT" }, -1, true ) && prevRTFormat != g->RenderTargetFormat )
                g->PreTonemapRT = nullptr; // needs refresh!
            ImGui::Unindent( );
            
            
            ImGui::Separator();
            ImGui::Text( "Debugging options:" );
            ImGui::Indent(); 
            ImGui::Checkbox( "Volumetric particles", &g->ParticleEnableVolumetric );
            ImGui::Checkbox( "Wireframe", &g->Wireframe );// g->Wireframe = false; // not implemented yet
            ImGui::Unindent(); 
            ImGui::Separator();
            ImGui::TextColored( {0,0,0,1}, "(started tracking TODOs here - 4th March)");
            ImGui::TextColored( {0,1,0,1}, "TODO: (done) volumetric shadows fix");
            ImGui::TextColored( {0,1,0,1}, "TODO: (done) volumetric option");
            ImGui::TextColored( {0,1,0,1}, "TODO: (done) low res render & compositing");
            ImGui::TextColored( {0,1,0,1}, "TODO: (done) depth downsampling - use closest or furthest sample choice");
            ImGui::TextColored( {0,1,0,1}, "TODO: (done) VRS paths");
            ImGui::TextColored( {0,1,0,1}, "TODO: (done) low res render upsample options - debug no blend, bicubic filter, etc.");
            ImGui::TextColored( {0,1,0,1}, "TODO: (done) (maybe custom depth-aware / depth-weihgted bilinear upsample? should work?)");
            ImGui::TextColored( {0,1,0,1}, "TODO: (done) perf counters around important bits");
            ImGui::TextColored( {1,0,0,1}, "TODO: correctness for low res offscreen & VRS and odd & not mod of 4 resolutions");
            ImGui::TextColored( {1,0,0,1}, "TODO: wireframe");
            ImGui::TextColored( {0,1,0,1}, "TODO: (done) choice of RT format for PreTonemapRT");
            ImGui::TextColored( {1,0,0,1}, "TODO: choice of RT format for Offscreen RT");
            ImGui::TextColored( {1,0,0,1}, "TODO: particle placement determinism at initialization for quality comparisons");
            ImGui::TextColored( {1,0,0,1}, "TODO: bicubic depth-aware / depth-weihgted upsample would be really nice!!");
#endif
        },
            "Sample", 0, true, vaUIPanel::DockLocation::DockedLeft );
        return;
    }
    else if( applicationState == vaApplicationState::ShuttingDown )
    {
        globals = nullptr;
        return;
    }
    assert( applicationState == vaApplicationState::Running );

    if( !globals->Pause )
    {
        globals->AnimTime += deltaTime;
        if( globals->CameraRotate )
            globals->CameraAnimTime += deltaTime;
    }

    // if( globals->AnimTime + deltaTime > 4.0f )
    //     globals->Pause = true;

    double lightMoveTime = 0.0f;
    globals->ShinyLightRed->Position  = 20.0f * vaVector3{ (float)cos(-0.2*lightMoveTime), (float)sin(-0.2*lightMoveTime), 1.0f };
    globals->ShinyLightBlue->Position  = 20.0f * vaVector3{ (float)cos(-0.2*lightMoveTime + VA_PI*0.8), (float)sin(-0.2*lightMoveTime + VA_PI * 0.8), 1.0f };

    globals->MeshList.Start( );

    globals->MeshList.Insert( globals->MeshSphere, vaMatrix4x4::Translation( globals->ShinyLightRed->Position ) );
    globals->MeshList.Insert( globals->MeshSphere, vaMatrix4x4::Translation( globals->ShinyLightBlue->Position ) );

    for( int i = 0; i < globals->BlockCount; i++ )
    {
//        globals->MeshList.Insert( globals->MeshBox, vaMatrix4x4::Translation( globals->CubesPlacementRadius * cosf( 2 * VA_PIf * ( i / (float)( globals->BlockCount - 1 ) ) ), globals->CubesPlacementRadius* (float)sin( -2 * VA_PIf * ( i / (float)( globals->BlockCount - 1 ) ) ), 4.0f ) );
        globals->MeshList.Insert( globals->MeshBox, vaMatrix4x4::Translation( globals->CubesPlacementRadius * cosf( 2 * VA_PIf * ( i / (float)( globals->BlockCount - 1 ) ) ), globals->CubesPlacementRadius * (float)sin( -2 * VA_PIf * ( i / (float)( globals->BlockCount - 1 ) ) ), 4.0f ) );
    }
    globals->MeshList.Insert( globals->MeshTeapot, vaMatrix4x4::Identity );
    globals->MeshList.Insert( globals->MeshPlane, vaMatrix4x4::Translation( 0, 0, -2.2f ) );

    globals->MeshList.Stop( );

    auto backbufferTex = renderDevice.GetCurrentBackbufferTexture( );

    // Create/update depth
    if( globals->DepthBuffer == nullptr || globals->DepthBuffer->GetSize() != backbufferTex->GetSize() || globals->DepthBuffer->GetSampleCount() != backbufferTex->GetSampleCount() )
        globals->DepthBuffer = vaTexture::Create2D( renderDevice, vaResourceFormat::R32_TYPELESS, backbufferTex->GetSizeX(), backbufferTex->GetSizeY(), 1, 1, 1, vaResourceBindSupportFlags::DepthStencil | vaResourceBindSupportFlags::ShaderResource, vaResourceAccessFlags::Default, vaResourceFormat::R32_FLOAT, vaResourceFormat::Unknown, vaResourceFormat::D32_FLOAT );

    // Create/update surfaces required for particles
    {
        int offDepthResX, offDepthResY;
        int offColorResX = 0, offColorResY = 0;
        switch( globals->ParticleDrawTechnique )
        {
        case( Globals::ParticleDrawTechnique_Basic ):
            offDepthResX = backbufferTex->GetSizeX(); offDepthResY = backbufferTex->GetSizeY();
            break;
        case( Globals::ParticleDrawTechnique_OffscreenFullRes ):
            offDepthResX = backbufferTex->GetSizeX(); offDepthResY = backbufferTex->GetSizeY();
            offColorResX = backbufferTex->GetSizeX(); offColorResY = backbufferTex->GetSizeY();
            break;
        case( Globals::ParticleDrawTechnique_OffscreenHalfByHalfRes ):
            offDepthResX = (backbufferTex->GetSizeX()+1)/2; offDepthResY = (backbufferTex->GetSizeY()+1)/2;
            offColorResX = (backbufferTex->GetSizeX()+1)/2; offColorResY = (backbufferTex->GetSizeY()+1)/2;
            break;
        case( Globals::ParticleDrawTechnique_OffscreenQuarterByQuarterRes ):
            offDepthResX = (backbufferTex->GetSizeX()+3)/4; offDepthResY = (backbufferTex->GetSizeY()+3)/4;
            offColorResX = (backbufferTex->GetSizeX()+3)/4; offColorResY = (backbufferTex->GetSizeY()+3)/4;
            break;
        case( Globals::ParticleDrawTechnique_VRS2x2 ):
            offDepthResX = (backbufferTex->GetSizeX()+1)/2; offDepthResY = (backbufferTex->GetSizeY()+1)/2;
            break;
        case( Globals::ParticleDrawTechnique_VRS4x4 ):
            offDepthResX = (backbufferTex->GetSizeX()+3)/4; offDepthResY = (backbufferTex->GetSizeY()+3)/4;
            break;
        default: assert( false );  offDepthResX = offDepthResY = offColorResX = offColorResY = 0; break;
        }                          
        if( globals->ParticlesInputViewspaceDepths == nullptr || globals->ParticlesInputViewspaceDepths->GetSizeX() != offDepthResX || globals->ParticlesInputViewspaceDepths->GetSizeY() != offDepthResY )
            globals->ParticlesInputViewspaceDepths = vaTexture::Create2D( renderDevice, vaResourceFormat::R16_FLOAT, offDepthResX, offDepthResY, 1, 1, 1, vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::ShaderResource );
        
        if( offColorResX != 0 && offColorResY != 0 )
        {
            if( globals->ParticlesOffscreenColor == nullptr || globals->ParticlesOffscreenColor->GetSizeX() != offColorResX || globals->ParticlesOffscreenColor->GetSizeY() != offColorResY )
                globals->ParticlesOffscreenColor = vaTexture::Create2D( renderDevice, vaResourceFormat::R16G16B16A16_FLOAT, offColorResX, offColorResY, 1, 1, 1, vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::ShaderResource );
        }
        else
            globals->ParticlesOffscreenColor = nullptr;
    }

    // Create/update offscreen render target
    if( globals->PreTonemapRT == nullptr || globals->PreTonemapRT->GetSizeX() != backbufferTex->GetSizeX() || globals->PreTonemapRT->GetSizeY() != backbufferTex->GetSizeY() )
    {
        globals->PreTonemapRT   = vaTexture::Create2D( renderDevice, (globals->RenderTargetFormat==0)?(vaResourceFormat::R11G11B10_FLOAT):(vaResourceFormat::R16G16B16A16_FLOAT), 
            backbufferTex->GetSizeX(), backbufferTex->GetSizeY(), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::UnorderedAccess );
        globals->PostTonemapRT  = vaTexture::Create2D( renderDevice, vaResourceFormat::R11G11B10_FLOAT, backbufferTex->GetSizeX(), backbufferTex->GetSizeY(), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::UnorderedAccess );
    }


    vaRenderDeviceContext & mainContext = *renderDevice.GetMainContext();


    // setup and rotate camera
    globals->Camera->SetViewport( vaViewport( renderDevice.GetCurrentBackbufferTexture()->GetWidth(), renderDevice.GetCurrentBackbufferTexture()->GetHeight() ) );
    if( globals->CameraRotate )
    {
        globals->Camera->SetPosition( 17.0f * vaVector3{ (float)cos(0.05*globals->CameraAnimTime), (float)sin(0.05*globals->CameraAnimTime), 0.2f } );
        globals->Camera->SetOrientationLookAt( { 0, 0, -1.0f } );
        if( globals->Camera->GetAttachedController() != nullptr )
            globals->Camera->AttachController( nullptr );
    }
    else
    {
        if( globals->Camera->GetAttachedController() != globals->CameraFreeFlightController )
            globals->Camera->AttachController( globals->CameraFreeFlightController );
    }
    globals->Camera->Tick( deltaTime, true );

    // globals->MeshListStatic->SetSortSettings( true, globals->Camera->GetPosition() );
    // globals->MeshListDynamic->SetSortSettings( true, globals->Camera->GetPosition() );

    {
        VA_TRACE_CPU_SCOPE( ParticlesTick );
        globals->ParticleSystem->Tick( (globals->Pause)?(0.0f):(deltaTime) );
    }
    {
        VA_TRACE_CPU_SCOPE( ParticlesSort );
        globals->ParticleSystem->Sort( globals->Camera->GetPosition( ), true );
    }

    // tick lighting
    globals->Lighting->Tick( deltaTime );

    application.TickUI( *globals->Camera );

    // Do the rendering tick and present 
    renderDevice.BeginFrame( deltaTime );

    // this has to be called before starting any rendering to setup exposure and any related params
    globals->Camera->PreRenderTick( *renderDevice.GetMainContext( ), deltaTime );

    vaRenderOutputs offscreenOutputs = vaRenderOutputs::FromRTDepth( globals->PreTonemapRT, globals->DepthBuffer );

    //currentBackbuffer->ClearRTV( mainContext, vaVector4( 0.0f, 0.0f, 0.0f, 0.0f ) );
    offscreenOutputs.DepthStencil->ClearDSV( mainContext, true, globals->Camera->GetUseReversedZ( ) ? ( 0.0f ) : ( 1.0f ), false, 0 );

    // update shadows
    shared_ptr<vaShadowmap> queuedShadowmap = globals->Lighting->GetNextHighestPriorityShadowmapForRendering( );
    if( queuedShadowmap != nullptr )
    {
        queuedShadowmap->Draw( mainContext, globals->MeshList );
    }

    // needed for more rendering of more complex items - in this case skybox requires it only to get the camera
    vaDrawAttributes drawAttributes( *globals->Camera, vaDrawAttributes::OutputType::Forward, vaDrawAttributes::RenderFlags::None, globals->Lighting.get() );

    // opaque skybox
    globals->Skybox->Draw( mainContext, offscreenOutputs, drawAttributes );

    // draw meshes
    renderDevice.GetMeshManager().Draw( mainContext, offscreenOutputs, drawAttributes, globals->MeshList, vaBlendMode::Opaque, vaRenderMeshDrawFlags::EnableDepthTest | vaRenderMeshDrawFlags::EnableDepthWrite );
    //renderDevice.GetMeshManager().Draw( mainContext, offscreenOutputs, drawAttributes, globals->MeshListDynamic, vaBlendMode::Opaque, vaRenderMeshDrawFlags::EnableDepthTest | vaRenderMeshDrawFlags::EnableDepthWrite );

    // choose particle shaders/material
    globals->ParticleShaderComplexity = (Globals::PSC)vaMath::Clamp( (int)globals->ParticleShaderComplexity, 0, 2 );
    globals->ParticleSystem->SetMaterial( ((globals->ParticleEnableVolumetric)?(globals->VolumetricParticleMaterials):(globals->ParticleMaterials))[(int)globals->ParticleShaderComplexity] );

    // draw transparent particles
    {
        VA_TRACE_CPUGPU_SCOPE( ParticlesAll, mainContext );

        bool offscreenRendering = globals->ParticleDrawTechnique == Globals::ParticleDrawTechnique_OffscreenFullRes || globals->ParticleDrawTechnique == Globals::ParticleDrawTechnique_OffscreenHalfByHalfRes || globals->ParticleDrawTechnique == Globals::ParticleDrawTechnique_OffscreenQuarterByQuarterRes;

        {
            if( globals->ParticleDrawTechnique == Globals::ParticleDrawTechnique_OffscreenHalfByHalfRes || globals->ParticleDrawTechnique == Globals::ParticleDrawTechnique_VRS2x2 )
                renderDevice.GetPostProcess().DepthToViewspaceLinearDownsample2x2( mainContext, drawAttributes, globals->ParticlesInputViewspaceDepths, globals->DepthBuffer, globals->DepthDownsampleFilter );
            else if( globals->ParticleDrawTechnique == Globals::ParticleDrawTechnique_OffscreenQuarterByQuarterRes || globals->ParticleDrawTechnique == Globals::ParticleDrawTechnique_VRS4x4 )
                renderDevice.GetPostProcess().DepthToViewspaceLinearDownsample4x4( mainContext, drawAttributes, globals->ParticlesInputViewspaceDepths, globals->DepthBuffer, globals->DepthDownsampleFilter );
            else 
                renderDevice.GetPostProcess().DepthToViewspaceLinear( mainContext, drawAttributes, globals->ParticlesInputViewspaceDepths, globals->DepthBuffer );
        }

        vaRenderOutputs currentOutputs = renderDevice.GetCurrentBackbuffer();
        if( offscreenRendering )
        {
            currentOutputs.SetRenderTarget( globals->ParticlesOffscreenColor, nullptr, true );
            globals->ParticlesOffscreenColor->ClearRTV( mainContext, vaVector4(0,0,0,0.0) );
        }

        vaShadingRate particleShadingRate = vaShadingRate::ShadingRate1X1;
        if( globals->ParticleDrawTechnique == Globals::ParticleDrawTechnique_VRS2x2 )
            particleShadingRate = vaShadingRate::ShadingRate2X2;
        else if( globals->ParticleDrawTechnique == Globals::ParticleDrawTechnique_VRS4x4 )
            particleShadingRate = vaShadingRate::ShadingRate4X4;

        {
            vaDrawAttributes prtDrawContext( *globals->Camera, vaDrawAttributes::OutputType::Forward, vaDrawAttributes::RenderFlags::None, globals->Lighting.get() );
            if( globals->ParticleEnableVolumetric )
            {
                globals->ParticleSystem->Draw( mainContext, currentOutputs, prtDrawContext, globals->ParticlesInputViewspaceDepths, (offscreenRendering)?(vaBlendMode::OffscreenAccumulate):(vaBlendMode::AlphaBlend), particleShadingRate );
            }
            else
                globals->ParticleSystem->Draw( mainContext, currentOutputs, prtDrawContext, nullptr, (offscreenRendering)?(vaBlendMode::OffscreenAccumulate):(vaBlendMode::AlphaBlend), particleShadingRate );
        }

        if( offscreenRendering )
        {
            if( globals->OffscreenUpsampleFilter == Globals::OffscreenUpsampleFilter_SimpleBilinear || globals->OffscreenUpsampleFilter == Globals::OffscreenUpsampleFilter_Debug )
                renderDevice.StretchRect( mainContext, backbufferTex, globals->ParticlesOffscreenColor, vaVector4(0.0f, 0.0f, (float)globals->ParticlesOffscreenColor->GetSizeX(), (float)globals->ParticlesOffscreenColor->GetSizeY()),
                                                    vaVector4(0.0f, 0.0f, (float)globals->PreTonemapRT->GetSizeX(), (float)globals->PreTonemapRT->GetSizeY()), true, 
                                                    (globals->OffscreenUpsampleFilter == Globals::OffscreenUpsampleFilter_Debug)?(vaBlendMode::Opaque):(vaBlendMode::PremultAlphaBlend) );
            else if( globals->OffscreenUpsampleFilter == Globals::OffscreenUpsampleFilter_DepthAwareBilinear )
                renderDevice.GetPostProcess().SmartOffscreenUpsampleComposite( mainContext, renderDevice.GetCurrentBackbuffer(), drawAttributes, globals->ParticlesOffscreenColor, globals->ParticlesInputViewspaceDepths, globals->DepthBuffer );
            else { assert( false ); }

        }
    }

    if( globals->Wireframe )
    {
        vaDrawAttributes wireframeDrawAttributes( *globals->Camera, vaDrawAttributes::OutputType::Forward, vaDrawAttributes::RenderFlags::SetZOffsettedProjMatrix | vaDrawAttributes::RenderFlags::DebugWireframePass );
        renderDevice.GetMeshManager().Draw( mainContext, renderDevice.GetCurrentBackbuffer(), wireframeDrawAttributes, globals->MeshList, vaBlendMode::Opaque, vaRenderMeshDrawFlags::EnableDepthTest | vaRenderMeshDrawFlags::DepthTestIncludesEqual );
        globals->ParticleSystem->Draw( mainContext, renderDevice.GetCurrentBackbuffer(), wireframeDrawAttributes, nullptr, vaBlendMode::AlphaBlend );
    }

    // tonemap & copy to post-tonemap RT
    /*drawResults |=*/ globals->Tonemap->TickAndApplyCameraPostProcess( mainContext, *globals->Camera, globals->PostTonemapRT, globals->PreTonemapRT );

    // image comparison tool (doesn't do anything unless used from UI)
    globals->ImageCompareTool->RenderTick( mainContext, globals->PostTonemapRT );

    // draw zoomtool if needed (doesn't do anything unless used from UI)
    globals->ZoomTool->Draw( mainContext, globals->PostTonemapRT );

    // copy contents of the offscreen RT to the backbuffer
    mainContext.CopySRVToRTV( backbufferTex, globals->PostTonemapRT );

    // update and draw imgui
    application.DrawUI( *renderDevice.GetMainContext( ), renderDevice.GetCurrentBackbuffer( ), globals->DepthBuffer );

    globals->MeshList.Reset( );

    // present the frame, flip the buffers, etc
    renderDevice.EndAndPresentFrame( (application.GetVsync())?(1):(0) );
#endif
}

#include "Core/Misc/vaPoissonDiskGenerator.h"
void Sample17_PoissonDiskGenerator( vaRenderDevice& renderDevice, vaApplicationBase& application, float deltaTime, vaApplicationState applicationState )
{
    struct Globals
    {
        shared_ptr<vaTexture>                       OffscreenRT;
        shared_ptr<vaUISimplePanel>                 UIPanel;

        float                                       PoissonDiskMinSeparation        = 0.53f;
        std::vector<vaVector3>                      PoissonDisk;
        std::vector<vaVector3>                      PoissonDiskPostProcessed;
        float                                       PoissonDiskRotScore             = 0.0f;
        float                                       PoissonDiskPOWModifier          = 1.0f;

        int                                         PoissonDiskTargetSampleCount    = 8;
        bool                                        PoissonDiskAutoSearch           = false;
        int64                                       PoissonDiskAutoSearchAttempts   = 0;
    };
    static shared_ptr<Globals> globals;

    auto searchStep = [ ]( Globals& g, bool onlyUpdateIfBetter, bool updateMinSep )
    {
        std::vector<vaVector2> diskRaw;
        vaPoissonDiskGenerator::SampleCircle( { 0,0 }, 1.0f, g.PoissonDiskMinSeparation, diskRaw );

        if( updateMinSep && (diskRaw.size( ) != g.PoissonDiskTargetSampleCount) )
        {
            float diff = ( diskRaw.size( ) > g.PoissonDiskTargetSampleCount ) ? ( diskRaw.size( ) / (float)g.PoissonDiskTargetSampleCount ) : ( g.PoissonDiskTargetSampleCount / (float)diskRaw.size( ) );
            diff = std::pow(diff, 0.002f);
            g.PoissonDiskMinSeparation = ( diskRaw.size( ) > g.PoissonDiskTargetSampleCount ) ? ( g.PoissonDiskMinSeparation * diff ) : ( g.PoissonDiskMinSeparation / diff );
        }

        if( diskRaw.size() == 0 )
            return;

        std::vector<vaVector3> diskSorted;

        diskSorted.clear( );
        for( auto& p : diskRaw )
            diskSorted.push_back( { p.x, p.y, p.Length( ) } );
        std::sort( diskSorted.begin( ), diskSorted.end( ), [ ]( const vaVector3& left, const vaVector3& right ) { return left.z < right.z; } );

        float rotScore = 0;
        float threshold = 1.0f / (float)(diskSorted.size());
        for( int i = 0; i < (int)diskSorted.size(); i++ )
        {
            if( i == 0 )
                rotScore += diskSorted[i].z*diskSorted[i].z;
            else 
            {
                float diff = (diskSorted[i].z - diskSorted[i-1].z) - threshold;
                rotScore += diff*diff;
            }

            // additional scoring for the last - we want it also to be at the edge
            if( i == ( diskSorted.size( ) - 1 ) )
                rotScore += ( diskSorted[i].z - 1 ) * ( diskSorted[i].z - 1 );
        }
        //rotScore += (diskSorted.back().z-1)*(diskSorted.back().z-1);

        if( onlyUpdateIfBetter )
        {
            g.PoissonDiskAutoSearchAttempts++;
            if( diskSorted.size() != g.PoissonDiskTargetSampleCount )
                return;
            if( rotScore >= g.PoissonDiskRotScore )
                return;
        }
        g.PoissonDisk = diskSorted;
        g.PoissonDiskRotScore = rotScore;
    };

    if( applicationState == vaApplicationState::Initializing )
    {
        assert( globals == nullptr );
        globals = std::make_shared<Globals>( );

        // UI
        globals->UIPanel = std::make_shared<vaUISimplePanel>( [&searchStep, g = &( *globals )]( vaApplicationBase& )
        {
#ifdef VA_IMGUI_INTEGRATION_ENABLED
            ImGui::InputFloat( "Minimum separation", &g->PoissonDiskMinSeparation, 0.01f, 0.1f, "%.5f" );
            g->PoissonDiskMinSeparation = vaMath::Clamp( g->PoissonDiskMinSeparation, 0.001f, 0.8f );
            if( !g->PoissonDiskAutoSearch && ImGui::Button( "Generate one set" ) )
                searchStep( *g, false, false );
            ImGui::Separator();

            ImGui::InputInt( "Required sample count", &g->PoissonDiskTargetSampleCount );
            if( ImGui::Checkbox( "Auto search", &g->PoissonDiskAutoSearch ) )
            {
                if( g->PoissonDiskAutoSearch )
                {
                    g->PoissonDiskAutoSearchAttempts = 0;
                    g->PoissonDiskRotScore = std::numeric_limits<float>::infinity();
                }
            }
            ImGui::Separator();
            ImGui::Text( "Current count: %d", (int)g->PoissonDisk.size() );
            ImGui::Text( "Current rotatability score: %f", g->PoissonDiskRotScore );
            ImGui::Text( "Auto search count: %dk", (int)(g->PoissonDiskAutoSearchAttempts / 1024) );
            ImGui::Separator( );
            ImGui::InputFloat( "Post-process POW mod", &g->PoissonDiskPOWModifier, 0.1f, 0.2f, "%.2f" );
            g->PoissonDiskPOWModifier = vaMath::Clamp( g->PoissonDiskPOWModifier, 0.1f, 4.0f );
            ImGui::Separator( );
            if( g->PoissonDisk.size( ) > 0 )
            {
                if( ImGui::Button( "Save disk.h header" ) )
                {
                    string report;
                    report += vaStringTools::Format( "// " );
                    report += vaStringTools::Format( "// Generated by Vanilla Sample17_PoissonDiskGenerator!\r\n");
                    report += vaStringTools::Format( "// Samples are also optimized to minimize overlap during rotation and sorted from\r\n");
                    report += vaStringTools::Format( "// center to outer ones; .z is length(.xy)\r\n");
                    report += vaStringTools::Format( "// POW modifier used: %.3f\r\n", g->PoissonDiskPOWModifier );
                    report += vaStringTools::Format( "// " );
                    report += vaStringTools::Format( "\r\n" );
                    report += vaStringTools::Format( "#define VA_POISSON_DISK_SAMPLE_COUNT  (%d)\r\n", (int)g->PoissonDisk.size() );
                    report += vaStringTools::Format( "\r\n" );
                    report += vaStringTools::Format( "static const float3 g_poissonDisk[VA_POISSON_DISK_SAMPLE_COUNT] = \r\n" );
                    report += vaStringTools::Format( "{ \r\n" );
                    for( int i = 0; i < (int)g->PoissonDisk.size( ); i++ )
                    {
                        report += vaStringTools::Format( "    %+.8f, %+.8f, %+.8f", g->PoissonDiskPostProcessed[i].x, g->PoissonDiskPostProcessed[i].y, g->PoissonDiskPostProcessed[i].z );
                        report += (i == (int)(g->PoissonDisk.size()-1))?("\r\n"):(",\r\n");
                    }
                    report += vaStringTools::Format( "}; \r\n" );
                    report += vaStringTools::Format( "\r\n" );


                    string outFileName = vaStringTools::SimpleNarrow( vaCore::GetExecutableDirectory( ) ) + vaStringTools::Format( "vaPoissonDisk%d.h", (int)g->PoissonDisk.size() );
                    vaFileStream fileOut;
                    if( !fileOut.Open( outFileName, FileCreationMode::Create, FileAccessMode::Write ) )
                    {
                        VA_LOG_ERROR( "Could not open tracing report file '%s'", outFileName.c_str( ) );
                        return;
                    }
                    if( !fileOut.WriteTXT( report ) )
                    {
                        VA_LOG_ERROR( "Could not write tracing report to '%s'", outFileName.c_str( ) );
                        return;
                    };
                    VA_LOG_SUCCESS( "poisson disk dumped to '%s'", outFileName.c_str( ) );
                }
            }
#endif
        },
            "PoissonDiskGenerator", 0, true, vaUIPanel::DockLocation::DockedLeft );
        return;
    }
    else if( applicationState == vaApplicationState::ShuttingDown )
    {
        globals = nullptr;
        return;
    }
    assert( applicationState == vaApplicationState::Running );

    if( globals->PoissonDiskAutoSearch )
    {
        for( int i = 0; i < 100; i++ )
            searchStep( *globals, true, true );
    }

    // post-process
    globals->PoissonDiskPostProcessed.resize( globals->PoissonDisk.size() );
    for( int i = 0; i < globals->PoissonDisk.size( ); i++ )
    {
        float l     = globals->PoissonDisk[i].AsVec2().Length( );
        float lp    = std::powf( l, globals->PoissonDiskPOWModifier );
        globals->PoissonDiskPostProcessed[i] = globals->PoissonDisk[i] * (lp/l);
    }

    auto backbufferTex = renderDevice.GetCurrentBackbufferTexture( );
    vaRenderDeviceContext& mainContext = *renderDevice.GetMainContext( );

    application.TickUI( );

    // Do the rendering tick and present 
    renderDevice.BeginFrame( deltaTime );

    auto currentBackbuffer = renderDevice.GetCurrentBackbuffer( );

    // // Create/update offscreen render target
    // if( globals->OffscreenRT == nullptr || globals->OffscreenRT->GetSizeX( ) != renderDevice.GetCurrentBackbuffer( )->GetSizeX( ) || globals->OffscreenRT->GetSizeY( ) != renderDevice.GetCurrentBackbuffer( )->GetSizeY( ) )
    //     globals->OffscreenRT = vaTexture::Create2D( renderDevice, vaResourceFormat::R11G11B10_FLOAT, renderDevice.GetCurrentBackbuffer( )->GetSizeX( ), renderDevice.GetCurrentBackbuffer( )->GetSizeY( ), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget );

    backbufferTex->ClearRTV( mainContext, vaVector4( 0.5f, 0.4f, 0.5f, 0.0f ) );

    float displayRadius = std::min( backbufferTex->GetWidth( ), backbufferTex->GetHeight( ) ) * 0.4f;
    vaVector2 center( (float)backbufferTex->GetWidth( ) / 2.0f, (float)backbufferTex->GetHeight( ) / 2.0f );

    renderDevice.GetCanvas2D( ).DrawCircle( center.x, center.y, displayRadius, 0xFF000000 );
    renderDevice.GetCanvas2D( ).DrawCircle( center.x, center.y, displayRadius - 0.5f, 0xFF000000 );
    renderDevice.GetCanvas2D( ).DrawCircle( center.x, center.y, displayRadius + 0.5f, 0xFF000000 );
    renderDevice.GetCanvas2D( ).DrawCircle( center.x, center.y, 1.5f, 0xFFFF0000 );
    renderDevice.GetCanvas2D( ).DrawCircle( center.x, center.y, 2.0f, 0xFFFF0000 );

    float rectExtents = 3.0f;
    for( int i = 0; i < globals->PoissonDiskPostProcessed.size( ); i++ )
    {
        vaVector2 pos = center + globals->PoissonDiskPostProcessed[i].AsVec2( ) * displayRadius;
        renderDevice.GetCanvas2D( ).FillRectangle( pos.x - rectExtents, pos.y - rectExtents, rectExtents * 2 + 1, rectExtents * 2 + 1, 0xFF00FF00 );
        renderDevice.GetCanvas2D( ).DrawCircle( center.x, center.y, globals->PoissonDiskPostProcessed[i].z * displayRadius, 0xA0008000 );
    }

    // update and draw imgui
    application.DrawUI( *renderDevice.GetMainContext( ), renderDevice.GetCurrentBackbuffer(), nullptr );

    // present the frame, flip the buffers, etc
    renderDevice.EndAndPresentFrame( ( application.GetVsync( ) ) ? ( 1 ) : ( 0 ) );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Based on "Practical Hash-based Owen Scrambling", Brent Burley, Walt Disney Animation Studios
// With simplifications/optimizations taken out from https://www.shadertoy.com/view/wlyyDm# (relevant reddit thread:
// https://www.reddit.com/r/GraphicsProgramming/comments/l1go2r/owenscrambled_sobol_02_sequences_shadertoy/)
// This simplification uses Laine-Kerras permutation for the 1st dimension and Sobol' only for second, to achieve
// a good low 2D discrepancy (2 dimensional stratification).
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint directions[32] = {
    0x80000000u, 0xc0000000u, 0xa0000000u, 0xf0000000u,
    0x88000000u, 0xcc000000u, 0xaa000000u, 0xff000000u,
    0x80800000u, 0xc0c00000u, 0xa0a00000u, 0xf0f00000u,
    0x88880000u, 0xcccc0000u, 0xaaaa0000u, 0xffff0000u,
    0x80008000u, 0xc000c000u, 0xa000a000u, 0xf000f000u,
    0x88008800u, 0xcc00cc00u, 0xaa00aa00u, 0xff00ff00u,
    0x80808080u, 0xc0c0c0c0u, 0xa0a0a0a0u, 0xf0f0f0f0u,
    0x88888888u, 0xccccccccu, 0xaaaaaaaau, 0xffffffffu
    };

uint sobol(uint index) 
{
    uint X = 0u;
    for (int bit = 0; bit < 32; bit++) {
        uint mask = (index >> bit) & 1u;
        X ^= mask * directions[bit];
    }
    return X;
}

// uint hash_combine(uint seed, uint v) {
//     return seed ^ (v + (seed << 6) + (seed >> 2));
// }
// 
// uint hash(uint x) {
//     // finalizer from murmurhash3
//     x ^= x >> 16;
//     x *= 0x85ebca6bu;
//     x ^= x >> 13;
//     x *= 0xc2b2ae35u;
//     x ^= x >> 16;
//     return x;
// }

uint reverse_bits(uint x) 
{
    x = (((x & 0xaaaaaaaau) >> 1) | ((x & 0x55555555u) << 1));
    x = (((x & 0xccccccccu) >> 2) | ((x & 0x33333333u) << 2));
    x = (((x & 0xf0f0f0f0u) >> 4) | ((x & 0x0f0f0f0fu) << 4));
    x = (((x & 0xff00ff00u) >> 8) | ((x & 0x00ff00ffu) << 8));
    return ((x >> 16) | (x << 16));
}
uint laine_karras_permutation(uint x, uint seed) 
{
    x += seed;
    x ^= x * 0x6c50b47cu;
    x ^= x * 0xb82f1e52u;
    x ^= x * 0xc7afe638u;
    x ^= x * 0x8d22f6e6u;
    return x;
}
uint nested_uniform_scramble_base2(uint x, uint seed) 
{
    x = reverse_bits(x);
    x = laine_karras_permutation(x, seed);
    x = reverse_bits(x);
    return x;
}

vaVector2 shuffled_scrambled_sobol_pt( uint32 index, uint32 seed ) 
{
    uint shuffle_seed = vaMath::Hash32Combine( seed, 0 );
    uint x_seed = vaMath::Hash32Combine( seed, 1 );
    uint y_seed = vaMath::Hash32Combine( seed, 2 );

    uint shuffled_index = nested_uniform_scramble_base2(index, shuffle_seed);

    uint x = reverse_bits(shuffled_index);
    uint y = sobol(shuffled_index);
    x = nested_uniform_scramble_base2(x, x_seed);
    y = nested_uniform_scramble_base2(y, y_seed);

    float S = float(1.0/(float)(1ull<<32));
    return {x * S, y * S};
}

vaVector2 sys_random( std::mt19937 & rnd )
{
    std::uniform_real_distribution<float> dist(0, 1.0f);
    return {dist(rnd), dist(rnd)};
}

vaVector2 hash_random( uint32 index, uint32 seed ) 
{
    seed = vaMath::Hash32Combine( seed, index );
    uint x = vaMath::Hash32( seed );
    uint y = vaMath::Hash32( x );
    float S = float(1.0/(float)(1ull<<32));
    return {x * S, y * S};
}

void Sample18_Burley2020Scrambling( vaRenderDevice & renderDevice, vaApplicationBase& application, float deltaTime, vaApplicationState applicationState )
{
    struct ShaderConstants  // if changing, make sure to change the shader definition too
    {
        uint        Seed;
        uint        Count;
        uint        Padding0;
        uint        Padding1;
        vaVector2   TopLeft;
        vaVector2   Size;
    };

    struct Globals
    {
        shared_ptr<vaTexture>                       OffscreenRT;
        shared_ptr<vaUISimplePanel>                 UIPanel;

        int32                                       Type        = 2;        // 0 random, 0 hash_random, 2 hash owen scrambled sobol
        int32                                       Count       = 1024;
        int32                                       Seed        = 0;

        shared_ptr<vaComputeShader>                 ComputeShader;
        shared_ptr<vaConstantBuffer>                ConstantBuffer;


    };
    static shared_ptr<Globals> globals;

    auto lambda = [ ]( Globals& g )
    {
        g; 
return 0;
    };

    if( applicationState == vaApplicationState::Initializing )
    {
        assert( globals == nullptr );
        globals = std::make_shared<Globals>( );

        globals->ConstantBuffer = vaConstantBuffer::Create<ShaderConstants>( renderDevice, "constants" );

        globals->ComputeShader  = renderDevice.CreateModule<vaComputeShader>(); // get the platform dependent object
        globals->ComputeShader->CompileFromBuffer( 
            "#include \"vaNoise.hlsl\"                                                                                      \n"
            "struct ShaderConstants { uint Seed; uint Count; uint Padding0; uint Padding1; float2 TopLeft; float2 Size; };  \n"
            "cbuffer Sample18Consts : register(b0) { ShaderConstants g_consts ; }                                           \n"
            "                                                                                                               \n"
            "[numthreads( 8, 8, 1 )]                                                                                        \n"
            "void main( uint2 dispatchThreadID : SV_DispatchThreadID )                                                      \n"
            "{                                                                                                              \n"
            "   uint index = dispatchThreadID.y * (8*1024) + dispatchThreadID.x;                                            \n"
            "   if( index >= g_consts.Count )                                                                               \n"
            "       return;                                                                                                 \n"
            //"   float2 pt = burley_shuffled_scrambled_sobol_pt( index, g_consts.Seed );                                  \n"
            "   float2 pt = LDSample2D( index, g_consts.Seed );                                                             \n"
            "   DebugDraw2DCircle( g_consts.TopLeft + pt * g_consts.Size, 2.0, float4( 0, 0.7, 1, 1 ) );                    \n"
            "   DebugDraw2DCircle( g_consts.TopLeft + pt * g_consts.Size, 1.0, float4( 0, 0.7, 1, 1 ) );                    \n"
            "}                                                                                                              \n"
            , "main", {}, true );

        // UI
        globals->UIPanel = std::make_shared<vaUISimplePanel>( [&lambda, g = &( *globals )]( vaApplicationBase& )
        {
#ifdef VA_IMGUI_INTEGRATION_ENABLED
            ImGuiEx_Combo( "Distribution type", g->Type, { "Random", "HashRandom", "ShuffledScrambledSobol CPU", "ShuffledScrambledSobol GPU" } ); g->Type = vaMath::Clamp( g->Type, 0, 3  );

            if( g->Type == 0 )
                ImGui::Text("std::mt19937 random" );
            if( g->Type == 1 )
                ImGui::Text("Hash-based random" );
            else if( g->Type == 2 )
                ImGui::Text("'Practical Hash-based Owen Scrambling', Brent Burley Walt Disney Animation Studios, CPU version" );
            else if( g->Type == 3 )
                ImGui::Text("'Practical Hash-based Owen Scrambling', Brent Burley Walt Disney Animation Studios, GPU version" );

            ImGui::InputInt( "Count",       &g->Count );        g->Count        = vaMath::Clamp( g->Count, 0,       200000 );
            ImGui::InputInt( "Seed",        &g->Seed );
#endif
        },
            "PoissonDiskGenerator", 0, true, vaUIPanel::DockLocation::DockedLeft );
        return;
    }
    else if( applicationState == vaApplicationState::ShuttingDown )
    {
        globals = nullptr;
        return;
    }
    assert( applicationState == vaApplicationState::Running );


    auto backbufferTex = renderDevice.GetCurrentBackbufferTexture( );
    vaRenderDeviceContext& mainContext = *renderDevice.GetMainContext( );

    application.TickUI( );

    // Do the rendering tick and present 
    renderDevice.BeginFrame( deltaTime );

    auto currentBackbuffer = renderDevice.GetCurrentBackbuffer( );

    // // Create/update offscreen render target
    // if( globals->OffscreenRT == nullptr || globals->OffscreenRT->GetSizeX( ) != renderDevice.GetCurrentBackbuffer( )->GetSizeX( ) || globals->OffscreenRT->GetSizeY( ) != renderDevice.GetCurrentBackbuffer( )->GetSizeY( ) )
    //     globals->OffscreenRT = vaTexture::Create2D( renderDevice, vaResourceFormat::R11G11B10_FLOAT, renderDevice.GetCurrentBackbuffer( )->GetSizeX( ), renderDevice.GetCurrentBackbuffer( )->GetSizeY( ), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget );

    backbufferTex->ClearRTV( mainContext, vaVector4( 0.4f, 0.3f, 0.3f, 0.0f ) );

    float displayRadius = std::min( backbufferTex->GetWidth( ), backbufferTex->GetHeight( ) ) * 0.4f;
    vaVector2 center( (float)backbufferTex->GetWidth( ) / 2.0f, (float)backbufferTex->GetHeight( ) / 2.0f );
    vaVector2 topleft = center - vaVector2( displayRadius, displayRadius );

    renderDevice.GetCanvas2D( ).DrawRectangle( {center.x - displayRadius, center.y - displayRadius}, {center.x + displayRadius, center.y + displayRadius}, 0xFF000000 );
    renderDevice.GetCanvas2D( ).DrawRectangle( {center.x - displayRadius-1, center.y - displayRadius-1}, {center.x + displayRadius+1, center.y + displayRadius+1}, 0xFF000000 );

    if( globals->Type == 3 )
    {
        // update constants
        ShaderConstants consts = { };
        consts.Count    = globals->Count;
        consts.Seed     = globals->Seed;
        consts.TopLeft  = topleft;
        consts.Size     = {displayRadius*2, displayRadius*2};
        globals->ConstantBuffer->Upload(*renderDevice.GetMainContext(), consts);

        // run compute shader
        {
            vaComputeItem computeItem;
            computeItem.ComputeShader       = globals->ComputeShader;
            computeItem.ConstantBuffers[0]  = globals->ConstantBuffer;
            computeItem.SetDispatch( 1024, 1024, 1 );
            renderDevice.GetMainContext()->ExecuteSingleItem( computeItem, vaRenderOutputs(), nullptr );
        }
    }
    else
    {
        std::mt19937 e2(globals->Seed); //rd());

        for( int i = 0; i < globals->Count; i++ )
        {
            vaVector2 pt = {0,0};
            if( globals->Type == 0 )
                pt = sys_random( e2 );
            else if( globals->Type == 1 )
                pt = hash_random( i, globals->Seed );
            else if( globals->Type == 2 )
                pt = shuffled_scrambled_sobol_pt( i, globals->Seed );
            else { assert( false ); }

            renderDevice.GetCanvas2D( ).DrawCircle( topleft + pt * displayRadius * 2.0f, 2.0f , 0xFF00FF00 );
            renderDevice.GetCanvas2D( ).DrawCircle( topleft + pt * displayRadius * 2.0f, 1.0f , 0xFF00FF00 );
        }
    }

    // update and draw imgui
    application.DrawUI( *renderDevice.GetMainContext( ), renderDevice.GetCurrentBackbuffer(), nullptr );

    // present the frame, flip the buffers, etc
    renderDevice.EndAndPresentFrame( ( application.GetVsync( ) ) ? ( 1 ) : ( 0 ) );
}