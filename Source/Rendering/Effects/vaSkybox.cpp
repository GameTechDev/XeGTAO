/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Vanilla Codebase, Copyright (c) Filip Strugar.
// Contents of this file are distributed under MIT license (https://en.wikipedia.org/wiki/MIT_License)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Rendering/Effects/vaSkybox.h"

#include "Scene/vaScene.h"

using namespace Vanilla;

vaSkybox::vaSkybox( const vaRenderingModuleParams & params ) : vaRenderingModule( params ), m_constantsBuffer( params ),
    m_vertexShader( params ), 
    m_pixelShader( params )
{ 
//    assert( vaRenderingCore::IsInitialized() );

    std::vector<vaVertexInputElementDesc> inputElements;
    inputElements.push_back( { "SV_Position", 0, vaResourceFormat::R32G32B32_FLOAT, 0, vaVertexInputElementDesc::AppendAlignedElement, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );

    m_vertexShader->CreateShaderAndILFromFile( "vaSkybox.hlsl", "SkyboxVS", inputElements, vaShaderMacroContaner{}, false );
    m_pixelShader->CreateShaderFromFile( "vaSkybox.hlsl", "SkyboxPS", vaShaderMacroContaner{}, false );

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

vaSkybox::~vaSkybox( )
{
}

void vaSkybox::UpdateFromScene( vaScene & scene, float deltaTime, int64 applicationTickIndex )
{
    deltaTime; applicationTickIndex;

    bool found = false;

    scene.Registry( ).view<Scene::SkyboxTexture, Scene::TransformWorld>( ).each( [ & ]( const Scene::SkyboxTexture & skybox, const Scene::TransformWorld & world )
    {
        if( !skybox.Enabled )
            return;
        assert( !found );   // multiple enabled fog spheres at the same time? that's not supported (yet)!

        assert( skybox.UID == vaGUID::Null ); // not implemented yet
        if( skybox.Path == "" )
            return;

        found = true;
        m_settings.ColorMultiplier      = skybox.ColorMultiplier;
        m_settings.Rotation             = world.GetRotationMatrix3x3( ).Transposed();
        if( m_cubemap == nullptr || m_cubemapPath != skybox.Path )
        {
            m_cubemap = vaTexture::CreateFromImageFile( GetRenderDevice(), vaStringTools::SimpleNarrow( vaCore::GetExecutableDirectory( ) ) + skybox.Path, vaTextureLoadFlags::Default );
            m_cubemapPath = skybox.Path;
        }
    } );

    if( !found )
    {
        Disable();
    }
}


void vaSkybox::UpdateConstants( vaDrawAttributes & drawAttributes, ShaderSkyboxConstants & consts )
{
    vaMatrix4x4 view = drawAttributes.Camera.GetViewMatrix( );
    vaMatrix4x4 proj = drawAttributes.Camera.GetProjMatrix( );

    view.Row(3) = vaVector4( 0.0f, 0.0f, 0.0f, 1.0f );

    vaMatrix4x4 viewProj = view * proj;

    consts.ProjToWorld = viewProj.Inversed( );

    consts.CubemapRotate = vaMatrix4x4( m_settings.Rotation );

    consts.ColorMul = vaVector4( m_settings.ColorMultiplier, m_settings.ColorMultiplier, m_settings.ColorMultiplier, 1.0f );
}

vaDrawResultFlags vaSkybox::Draw( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, vaDrawAttributes & drawAttributes )
{
    if( m_cubemap == nullptr )
        return vaDrawResultFlags::UnspecifiedError;

    ShaderSkyboxConstants consts;
    UpdateConstants( drawAttributes, consts );
    m_constantsBuffer.Upload( renderContext, consts );

    vaGraphicsItem renderItem;
    m_renderDevice.FillFullscreenPassGraphicsItem( renderItem, drawAttributes.Camera.GetUseReversedZ( ) );

    renderItem.ConstantBuffers[ SKYBOX_CONSTANTSBUFFERSLOT ]    = m_constantsBuffer;
    renderItem.ShaderResourceViews[ SKYBOX_TEXTURE_SLOT0 ]      = m_cubemap;

    renderItem.VertexShader         = m_vertexShader.get();
    renderItem.PixelShader          = m_pixelShader.get();
    renderItem.DepthEnable          = true;
    renderItem.DepthWriteEnable     = false;
    renderItem.DepthFunc            = ( drawAttributes.Camera.GetUseReversedZ() )?( vaComparisonFunc::GreaterEqual ):( vaComparisonFunc::LessEqual );

    return renderContext.ExecuteSingleItem( renderItem, renderOutputs, &drawAttributes );
}
