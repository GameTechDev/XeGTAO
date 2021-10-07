///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaSceneLighting.h"

#include "Rendering/vaRenderingIncludes.h"

#include "vaTexture.h"

#include "Rendering/vaRenderBuffers.h"

#include "Rendering/vaRenderGlobals.h"

#include "Rendering/Shaders/vaSharedTypes.h"

#include "Rendering/vaTextureHelpers.h"

#include "IntegratedExternals/vaImguiIntegration.h"

#include "Rendering/vaRenderMesh.h"
#include "Rendering/vaRenderMaterial.h"

#include "Scene/vaScene.h"
#include "Scene/vaSceneSystems.h"

#include "Rendering/vaDebugCanvas.h"

#include "Core/vaApplicationBase.h"
#include "Core/vaInput.h"

using namespace Vanilla;


vaSceneLighting::MainWorkNode::MainWorkNode( vaSceneLighting & lighting, vaScene & scene ) 
    : Lighting( lighting ), Scene( scene ), BoundsView( scene.Registry().view<const Scene::WorldBounds>( ) ),
    vaSceneAsync::WorkNode( "UpdateSceneLighting", {"bounds_done_marker"}, {"renderlists_done_marker"},
        Scene::AccessPermissions::ExportPairLists<
        const Scene::WorldBounds, const Scene::TransformWorld, const Scene::RenderMesh, const Scene::EmissiveMaterialDriver, 
        const Scene::LightPoint, const Scene::Name, const Scene::Relationship, const Scene::IgnoreByIBLTag> () )
{ 
}

void vaSceneLighting::MainWorkNode::ExecutePrologue( float deltaTime, int64 applicationTickIndex )
{ 
    ApplicationTickIndex = applicationTickIndex; DeltaTime = deltaTime; 
}

std::pair<uint, uint> vaSceneLighting::MainWorkNode::ExecuteNarrow( const uint32 pass, vaSceneAsync::ConcurrencyContext & ) 
{
    assert( pass == 0 ); pass; 
    // Lighting.UpdateFromScene( Scene, DeltaTime, ApplicationTickIndex );
    return {0,0};
}

void vaSceneLighting::MainWorkNode::ExecuteWide( const uint32 pass, const uint32 itemBegin, const uint32 itemEnd, vaSceneAsync::ConcurrencyContext & )
{
    pass;itemBegin;itemEnd;
}


vaSceneLighting::vaSceneLighting( const vaRenderingModuleParams & params ) : vaRenderingModule( vaRenderingModuleParams(params) ), 
    m_constantBuffer( vaConstantBuffer::Create<ShaderLightingConstants>( params.RenderDevice, "ShaderLightingConstants" ) ),
    // m_applyDirectionalAmbientPS( params ),
    // m_applyDirectionalAmbientShadowedPS( params ),
    vaUIPanel( "Lighting", 0, !VA_MINIMAL_UI_BOOL, vaUIPanel::DockLocation::DockedLeftBottom )
{
    m_debugInfo = "Lighting";

//    // just setup some basic lights
//    m_lights.push_back( std::make_shared<vaLight>( vaLight::MakeAmbient( "DefaultAmbient", vaVector3( 0.3f, 0.3f, 1.0f ), 0.1f ) ) );
//    m_lights.push_back( std::make_shared<vaLight>( vaLight::MakeDirectional( "DefaultDirectional", vaVector3( 1.0f, 1.0f, 0.9f ), 1.0f, vaVector3( 0.0f, -1.0f, -1.0f ).Normalized() ) ) );

    m_pointLightBuffer  = vaRenderBuffer::Create<ShaderLightPoint>( GetRenderDevice( ), ShaderLightPoint::MaxPointLights, vaRenderBufferFlags::None, "PointLightBuffer" );
    m_lightTreeBuffer   = vaRenderBuffer::Create<ShaderLightTreeNode>( GetRenderDevice( ), ShaderLightPoint::MaxPointLights, vaRenderBufferFlags::None, "LightTreeBuffer" );

    m_localIBLProbe     = std::make_shared<vaIBLProbe>( GetRenderDevice( ) );
    m_distantIBLProbe   = std::make_shared<vaIBLProbe>( GetRenderDevice( ) );
}

vaSceneLighting::~vaSceneLighting( )
{

}

void vaSceneLighting::SetScene( const shared_ptr<class vaScene> & scene )
{
    Reset( );

    if( m_scene == scene )
        return;

    // this actually disconnects work nodes
    m_asyncWorkNodes.clear();

    if( scene == nullptr )
        return;

    m_asyncWorkNodes.push_back( std::make_shared<MainWorkNode>( *this, *scene ) );

    for( auto & node : m_asyncWorkNodes )
        scene->Async().AddWorkNode( node );

}

bool vaSceneLighting::AllocateShadowStorage( const shared_ptr<vaCubeShadowmap> & shadowmap, int& outTextureIndex, shared_ptr<vaTexture>& outTextureArray )
{
    assert( shadowmap != nullptr ); assert( &shadowmap->GetLighting( ) == this ); assert( shadowmap->GetStorageTextureIndex( ) == -1 );

    for( int i = 0; i < _countof( m_shadowCubeArrayCurrentUsers ); i++ )
    {
        // slot not in use (either never used or weak_ptr pointing to deleted object)
        if( m_shadowCubeArrayCurrentUsers[i].lock( ) == nullptr )
        {
            m_shadowCubeArrayCurrentUsers[i] = shadowmap;
            outTextureIndex = i;
            outTextureArray = m_shadowCubeArrayTexture;
            return true;
        }
    }
    outTextureIndex = -1;
    outTextureArray = nullptr;
    VA_WARN( "We ran out of cubemap storage for shadows - use fewer shadow-casting lights or upgrade the logic here to pick more important ones." );
    return false;
}

bool vaSceneLighting::AllocateShadowStorageTextureIndex( const shared_ptr<vaShadowmap> & shadowmap, int & outTextureIndex, shared_ptr<vaTexture> & outTextureArray )
{
    assert( shadowmap != nullptr );
    assert( &shadowmap->GetLighting() == this );
    assert( shadowmap->GetStorageTextureIndex() == -1 );
    
    auto cubeShadow = std::dynamic_pointer_cast<vaCubeShadowmap>( shadowmap );

    if( cubeShadow != nullptr )
    {
        for( int i = 0; i < _countof(m_shadowCubeArrayCurrentUsers); i++ )
        {
            // slot not in use (either never used or weak_ptr pointing to deleted object)
            if( m_shadowCubeArrayCurrentUsers[i].lock( ) == nullptr )
            {
                m_shadowCubeArrayCurrentUsers[i] = shadowmap;
                outTextureIndex = i;
                outTextureArray = m_shadowCubeArrayTexture;
                return true;
            }
        }
        return false;
    }
    assert( false ); 
    return false;
}

shared_ptr<vaCubeShadowmap> vaSceneLighting::FindShadowmapForPointLight( entt::entity entity )
{
    // this could be a map of specific but let's do it this way for now
    for( int i = 0; i < m_shadowmaps.size( ); i++ )
    {
        if( m_shadowmaps[i]->GetEntity() == entity )
            return std::dynamic_pointer_cast<vaCubeShadowmap>( m_shadowmaps[i] );
    }
    return nullptr;
}

shared_ptr<vaShadowmap> vaSceneLighting::FindShadowmapForDirectionalLight( entt::entity entity )
{
    entity;
    assert( false ); // not implemented
    return nullptr;
}

void vaSceneLighting::UpdateShaderConstants( vaRenderDeviceContext & renderContext, const vaDrawAttributes & drawAttributes )
{
    ShaderLightingConstants consts;
    assert( drawAttributes.Settings.WorldBase == m_worldBase );
    consts.FogCenter            = ( m_collectedFogSphere.UseCustomCenter ) ? ( m_collectedFogSphere.Center - drawAttributes.Settings.WorldBase ) : ( vaVector3( 0.0f, 0.0f, 0.0f ) );
    consts.FogEnabled           = m_collectedFogSphere.Enabled?1:0;
    consts.FogColor             = m_collectedFogSphere.Color;
    consts.FogRadiusInner       = m_collectedFogSphere.RadiusInner;
    consts.FogRadiusOuter       = m_collectedFogSphere.RadiusOuter;
    consts.FogBlendCurvePow     = m_collectedFogSphere.BlendCurvePow;
    consts.FogBlendMultiplier   = m_collectedFogSphere.BlendMultiplier;
    consts.FogRange             = m_collectedFogSphere.RadiusOuter - m_collectedFogSphere.RadiusInner;
    
    
    //if( m_envmapTexture != nullptr )
    //{
    //    consts.EnvmapEnabled    = 1;
    //    consts.EnvmapMultiplier = m_envmapColorMultiplier;
    //    consts.EnvmapRotation   = vaMatrix4x4( m_envmapRotation );
    //}
    //else
    //{
        consts.EnvmapEnabled    = 0;
        consts.EnvmapMultiplier = 0.0f;
        consts.EnvmapRotation   = vaMatrix4x4::Identity;
    //}

    if( m_AOTexture != nullptr )
    {
        consts.AOMapEnabled     = (m_AOTexture->GetResourceFormat() == vaResourceFormat::R32_UINT)?(2):(1);
        consts.AOMapTexelSize   = vaVector2( 1.0f / m_AOTexture->GetWidth(), 1.0f / m_AOTexture->GetHeight() );
    }
    else
    {
        consts.AOMapEnabled     = 0;
        consts.AOMapTexelSize   = { 0, 0 };
    }

    consts.Dummy0                   = 0;
    consts.Dummy1                   = 0;
    consts.Dummy2                   = 0;

    // float preExposureMultiplier = drawAttributes.Camera.GetPreExposureMultiplier( true );

    // consts.AmbientLightIntensity    = vaVector4( m_collectedAmbientLightIntensity * preExposureMultiplier, 0.0f );

    consts.LightCountPoint    = (uint)m_sortedPointLights.size();
    assert( consts.LightCountPoint < ShaderLightPoint::MaxPointLights );
    consts.LightCountPoint    = std::min( consts.LightCountPoint,   ShaderLightPoint::MaxPointLights );

    // since sin(x) is close to x for very small x values then this actually works good enough
    consts.ShadowCubeDepthBiasScale             = m_shadowCubeDepthBiasScale / (float)m_shadowCubeResolution;
    consts.ShadowCubeFilterKernelSize           = m_shadowCubeFilterKernelSize / (float)m_shadowCubeResolution * 2.0f; // is this correct? basically approx cube sampling direction in .xy (if face is z) that moves by 1 pixel, roughly?
    consts.ShadowCubeFilterKernelSizeUnscaled   = m_shadowCubeFilterKernelSize;

    consts.AmbientLightFromDistantIBL   = {0,0,0,0};

    memset( &consts.LocalIBL, 0, sizeof( consts.LocalIBL ) );
    memset( &consts.DistantIBL, 0, sizeof( consts.DistantIBL ) );
    if( !drawAttributes.Settings.DisableGI )
    {
        if( m_localIBLProbe != nullptr )
            m_localIBLProbe->UpdateShaderConstants( drawAttributes, consts.LocalIBL );
        if( m_distantIBLProbe != nullptr )
        {
            m_distantIBLProbe->UpdateShaderConstants( drawAttributes, consts.DistantIBL );

            consts.AmbientLightFromDistantIBL = vaVector4( m_distantIBLProbe->GetContentsData( ).AmbientColor * m_distantIBLProbe->GetContentsData( ).AmbientColorIntensity, 0.0f );
        }
    }

    consts.LightTreeTotalElements       = (int)m_lightTree.size();
    consts.LightTreeDepth               = m_lightTreeDepth;
    consts.LightTreeBottomLevelSize     = m_lightTreeBottomLevelSize;
    consts.LightTreeBottomLevelOffset   = m_lightTreeBottomLevelOffset;

    m_constantBuffer->Upload( renderContext, consts );
}

void vaSceneLighting::UpdateAndSetToGlobals( vaRenderDeviceContext & renderContext, vaShaderItemGlobals & shaderItemGlobals, const vaDrawAttributes & drawAttributes )
{
    assert( drawAttributes.Lighting == this );

    // forgot to call SetWorldBase before Tick before this? :) there's an order requirement here, sorry - need to clean this up
    assert( drawAttributes.Settings.WorldBase == m_worldBase );

    UpdateShaderConstants( renderContext, drawAttributes );

    assert( shaderItemGlobals.ConstantBuffers[ LIGHTINGGLOBAL_CONSTANTSBUFFERSLOT ] == nullptr );
    shaderItemGlobals.ConstantBuffers[ LIGHTINGGLOBAL_CONSTANTSBUFFERSLOT ] = m_constantBuffer;

    // assert( shaderItemGlobals.ShaderResourceViews[ SHADERGLOBAL_LIGHTING_ENVMAP_TEXTURESLOT ] == nullptr );
    // shaderItemGlobals.ShaderResourceViews[ SHADERGLOBAL_LIGHTING_ENVMAP_TEXTURESLOT ] = m_envmapTexture;

    assert( shaderItemGlobals.ShaderResourceViews[SHADERGLOBAL_LIGHTING_CUBE_SHADOW_TEXTURESLOT] == nullptr );
    shaderItemGlobals.ShaderResourceViews[SHADERGLOBAL_LIGHTING_CUBE_SHADOW_TEXTURESLOT] = m_shadowCubeArrayTexture;

    assert( shaderItemGlobals.ShaderResourceViews[SHADERGLOBAL_AOMAP_TEXTURESLOT] == nullptr );
    shaderItemGlobals.ShaderResourceViews[SHADERGLOBAL_AOMAP_TEXTURESLOT] = m_AOTexture;

    if( !drawAttributes.Settings.DisableGI )
    {
        if( m_localIBLProbe != nullptr )
            m_localIBLProbe->SetToGlobals( shaderItemGlobals, 0 );
        if( m_distantIBLProbe != nullptr )
            m_distantIBLProbe->SetToGlobals( shaderItemGlobals, 1 );
    }

    shaderItemGlobals.ShaderResourceViews[LIGHTINGGLOBAL_SIMPLELIGHTS_SLOT] = m_pointLightBuffer;

    shaderItemGlobals.ShaderResourceViews[LIGHTINGGLOBAL_LIGHT_TREE_SLOT] = m_lightTreeBuffer;

    if( m_renderBuffersDirty )
    {
        // float preExposureMultiplier = drawAttributes.Camera.GetPreExposureMultiplier( true );

        if( m_sortedPointLights.size() > 0 )
        {
            m_lightTreeBuffer->Upload( renderContext, m_lightTree );
            m_pointLightBuffer->Upload( renderContext, m_sortedPointLights );
        }
        m_renderBuffersDirty = false;
    }
}

shared_ptr<vaShadowmap> vaSceneLighting::GetNextHighestPriorityShadowmapForRendering( )
{
    shared_ptr<vaShadowmap> ret = nullptr;
    float highestFoundAge       = 0.0f;

    for( int i = 0; i < m_shadowmaps.size( ); i++ )
    {
        if( m_shadowmaps[i]->GetDataAge( ) > highestFoundAge )
        {
            ret             = m_shadowmaps[i];
            highestFoundAge = m_shadowmaps[i]->GetDataAge( );
        }
    }
    return ret;
}

void vaSceneLighting::DestroyShadowmapTextures( )
{
    assert( m_shadowmapTexturesCreated );
    assert( false ); // not implemented yet - should clean up links

    //m_shadowCubeDepthTexture = nullptr;
    m_shadowCubeArrayTexture = nullptr;
    
    m_shadowmapTexturesCreated = false;
}

void vaSceneLighting::CreateShadowmapTextures( )
{
    assert( !m_shadowmapTexturesCreated );

    //vaTexture::SetNextCreateFastClearDSV( m_shadowCubeDepthFormat, 0.0f, 0 );
    //m_shadowCubeDepthTexture = vaTexture::Create2D( GetRenderDevice(), m_shadowCubeDepthFormat, m_shadowCubeResolution, m_shadowCubeResolution, 1, 6, 1, vaResourceBindSupportFlags::DepthStencil,
    //    vaResourceAccessFlags::Default, vaResourceFormat::Unknown, vaResourceFormat::Unknown, m_shadowCubeDepthFormat, vaResourceFormat::Unknown, vaTextureFlags::Cubemap, vaTextureContentsType::DepthBuffer );
    //
    //vaTexture::SetNextCreateFastClearRTV( m_shadowCubeFormat, vaVector4( 10000.0f, 10000.0f, 10000.0f, 10000.0f ) );
    //m_shadowCubeArrayTexture = vaTexture::Create2D( GetRenderDevice(), m_shadowCubeFormat, m_shadowCubeResolution, m_shadowCubeResolution, 1, 6*m_shadowCubeMapCount, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget,
    //    vaResourceAccessFlags::Default, m_shadowCubeFormat, m_shadowCubeFormat, vaResourceFormat::Unknown, vaResourceFormat::Unknown, vaTextureFlags::Cubemap, vaTextureContentsType::LinearDepth );

#if 1
    auto cubeResFormat  = vaResourceFormat::R16_TYPELESS;
    auto cubeSRVFormat  = vaResourceFormat::R16_UNORM;
    auto cubeDSVFormat  = vaResourceFormat::D16_UNORM;
#else
    auto cubeResFormat  = vaResourceFormat::R32_TYPELESS;
    auto cubeSRVFormat  = vaResourceFormat::R32_FLOAT;
    auto cubeDSVFormat  = vaResourceFormat::D32_FLOAT;
#endif

    vaTexture::SetNextCreateFastClearDSV( cubeDSVFormat, 0.0f, 0 );
    m_shadowCubeArrayTexture = vaTexture::Create2D( GetRenderDevice(), cubeResFormat, m_shadowCubeResolution, m_shadowCubeResolution, 1, 6*m_shadowCubeMapCount, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::DepthStencil,
        vaResourceAccessFlags::Default, cubeSRVFormat, vaResourceFormat::Unknown, cubeDSVFormat, vaResourceFormat::Unknown, vaTextureFlags::Cubemap, vaTextureContentsType::DepthBuffer );

    m_shadowmapTexturesCreated = true;
}

void vaSceneLighting::Reset( )
{
    m_debugInfo                     = "Reseted";
    m_collectedFogSphere            = {};
    m_localIBLProbe->Reset();
    m_localIBLProbePendingData      = {};
    m_distantIBLProbe->Reset();
    m_distantIBLProbePendingData    = {};
    m_AOTexture                     = nullptr;
    m_worldBase                     = { 0, 0, 0 };  
    // m_collectedAmbientLightIntensity= { 0, 0, 0 };
    m_collectedPointLightEntities.clear();
    m_collectedPointLights.clear();
    m_collectedPointLightsMetadata.clear();
    m_collectedPointLightsSortIndices.clear();
    m_sortedPointLights.clear();
}

void vaShadowmap::Tick( float deltaTime ) 
{ 
    deltaTime;
//    auto & light = m_light.lock( ); 
//    if( light == nullptr )
//        return;
//
//    vaVector3 newLightPos = light->Position;
//
//    bool hasChanges = m_includeDynamicObjects;
//    if( !m_lastLightState.NearEqual( *light ) )
//    {
//        m_lastLightState = *light;
//        hasChanges = true;
//    }
//
//    if( hasChanges ) 
//        m_dataDirtyTime += deltaTime; 
}

void vaCubeShadowmap::Tick( float deltaTime )
{
    vaSceneLighting & lighting = m_lightingSystem;
    
    // find texture storage if available
    if( m_storageTextureIndex == -1 )
    {
        int outTextureIndex;
        shared_ptr<vaTexture> outTextureArray;
        if( lighting.AllocateShadowStorageTextureIndex( this->shared_from_this( ), outTextureIndex, outTextureArray ) )
        {
            m_storageTextureIndex = outTextureIndex;

            // const shared_ptr<vaTexture> & cubeDepth = lighting->GetCubemapDepthTexture( );
            // assert( cubeDepth != nullptr );

            m_cubemapArraySRV = vaTexture::CreateView( outTextureArray, vaResourceBindSupportFlags::ShaderResource, outTextureArray->GetSRVFormat(), vaResourceFormat::Unknown, vaResourceFormat::Unknown, vaResourceFormat::Unknown, 
                    vaTextureFlags::Cubemap | vaTextureFlags::CubemapButArraySRV, 0, -1, outTextureIndex*6, 6 );

            for( int i = 0; i < 6; i++ )
            {
                m_cubemapSliceDSVs[i] = vaTexture::CreateView( outTextureArray, vaResourceBindSupportFlags::DepthStencil, vaResourceFormat::Unknown, vaResourceFormat::Unknown, outTextureArray->GetDSVFormat(), vaResourceFormat::Unknown, 
                    vaTextureFlags::None, 0, 1, outTextureIndex*6+i, 1 );
                // m_cubemapSliceDSVs[i] = vaTexture::CreateView( cubeDepth, vaResourceBindSupportFlags::DepthStencil, vaResourceFormat::Unknown, vaResourceFormat::Unknown, cubeDepth->GetDSVFormat(), vaResourceFormat::Unknown, 
                //     vaTextureFlags::Cubemap, 0, -1, i, 1 );
            }

        }
        else
        {
            // ran out of space? oh well, just skip this one
            m_storageTextureIndex = -1;
        }
    }

    vaShadowmap::Tick( deltaTime );
}

void vaCubeShadowmap::Tick( float deltaTime, const ShaderLightPoint & lightPoint )
{
    vaSceneLighting & lighting = m_lightingSystem;

    // find texture storage if available
    if( m_storageTextureIndex == -1 )
    {
        int outTextureIndex;
        shared_ptr<vaTexture> outTextureArray;
        if( lighting.AllocateShadowStorage( std::static_pointer_cast<vaCubeShadowmap>(this->shared_from_this( )), outTextureIndex, outTextureArray ) )
        {
            m_storageTextureIndex = outTextureIndex;

            m_cubemapArraySRV = vaTexture::CreateView( outTextureArray, vaResourceBindSupportFlags::ShaderResource, outTextureArray->GetSRVFormat( ), vaResourceFormat::Unknown, vaResourceFormat::Unknown, vaResourceFormat::Unknown,
                vaTextureFlags::Cubemap | vaTextureFlags::CubemapButArraySRV, 0, -1, outTextureIndex * 6, 6 );

            for( int i = 0; i < 6; i++ )
            {
                m_cubemapSliceDSVs[i] = vaTexture::CreateView( outTextureArray, vaResourceBindSupportFlags::DepthStencil, vaResourceFormat::Unknown, vaResourceFormat::Unknown, outTextureArray->GetDSVFormat( ), vaResourceFormat::Unknown,
                    vaTextureFlags::None, 0, 1, outTextureIndex * 6 + i, 1 );
                // m_cubemapSliceDSVs[i] = vaTexture::CreateView( cubeDepth, vaResourceBindSupportFlags::DepthStencil, vaResourceFormat::Unknown, vaResourceFormat::Unknown, cubeDepth->GetDSVFormat(), vaResourceFormat::Unknown, 
                //     vaTextureFlags::Cubemap, 0, -1, i, 1 );
            }
        }
        else
            m_storageTextureIndex = -1; // ran out of space? oh well, just skip this one
        Invalidate( );
    }

    bool hasChanges = m_includeDynamicObjects;
    if( !vaVector3::NearEqual( m_lightPosition, lightPoint.Position ) || !vaMath::NearEqual( m_lightSize, lightPoint.Size ) || !vaMath::NearEqual( m_lightRange, lightPoint.Range ) )
    {
        m_lightPosition = lightPoint.Position;
        m_lightSize     = lightPoint.Size;
        m_lightRange    = lightPoint.Range;
        hasChanges      = true;
    }

    if( hasChanges )
        m_dataDirtyTime += deltaTime;
}

void vaSceneLighting::UIPanelTick( vaApplicationBase & application )
{
    application;
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    if( ImGui::CollapsingHeader( "Light tree" ) )
    {
        ImGui::Text( "Total lights: %d", (int)m_sortedPointLights.size() );
        ImGui::Text( "Light tree depth: %d", (int)m_lightTreeDepth );
        ImGui::Checkbox( "Show debug visualization", &m_debugVizLTEnable );
        ImGui::Checkbox( "Show debug visualization text", &m_debugVizLTTextEnable );
        ImGui::InputInt( "Level highlight", &m_debugVizLTHighlightLevel );
        ImGui::Checkbox( "Single traversal test", &m_debugVizLTTraversalTest );
        if( m_debugVizLTTraversalTest )
        {
            const std::vector<CursorHoverInfo>& cursorHoverInfo = GetRenderDevice().GetRenderGlobals().GetCursorHoverInfo( );
            if( application.GetInputMouse( ) != nullptr && application.GetInputMouse( )->IsKeyClicked(MK_Left) && cursorHoverInfo.size() > 0 )
                m_debugVizLTTraversalRefPt = cursorHoverInfo.back().WorldspacePos;

            VA_GENERIC_RAII_SCOPE( ImGui::Indent();, ImGui::Unindent(); );
            ImGui::InputInt( "Test count", &m_debugVizLTTraversalCount ); m_debugVizLTTraversalCount = vaMath::Clamp( m_debugVizLTTraversalCount, 0, 65536 );
            ImGui::InputInt( "Random Seed", (int*)&m_debugVizLTTraversalSeed );
        }
    }

    if( ImGui::CollapsingHeader( "Shadowmaps" ) )
    {
        ImGui::Text( "Shadowmaps: %d", (int)m_shadowmaps.size() );
        vaUIPropertiesItem * ptrsToDisplay[4096];
        int countToShow = std::min( (int)m_shadowmaps.size( ), (int)_countof( ptrsToDisplay ) );
        for( int i = 0; i < countToShow; i++ ) ptrsToDisplay[i] = m_shadowmaps[i].get( );

        int currentShadowmap = -1;
        for( int i = 0; i < countToShow; i++ )
        {
            if( m_UI_SelectedShadow.lock( ) == m_shadowmaps[i] )
                currentShadowmap = i;
            ptrsToDisplay[i] = m_shadowmaps[i].get( );
        }

        vaUIPropertiesItem::DrawList( application, "Shadowmaps", ptrsToDisplay, countToShow, currentShadowmap, 0.0f, 90, 140.0f + ImGui::GetContentRegionAvail( ).x );
        if( currentShadowmap >= 0 && currentShadowmap < countToShow )
            m_UI_SelectedShadow = m_shadowmaps[currentShadowmap];

        ImGui::Text("Shadowmap offset settings");
        bool changed = false;
        /*changed |= */ImGui::InputFloat( "CubeDepthBiasScale" , &m_shadowCubeDepthBiasScale, 0.05f );
        /*changed |= */ImGui::InputFloat( "CubeFilterKernelSize" , &m_shadowCubeFilterKernelSize, 0.1f );
        if( changed )
        {
            for( const shared_ptr<vaShadowmap> & shadowmap : m_shadowmaps )
            {
                shadowmap->Invalidate();
            }
        }
    }
#endif
}

static string GetEntityName( const weak_ptr<vaScene> & scene, entt::entity entity )
{
    auto s = scene.lock();
    if( s == nullptr )
        return "";
    return s->GetName( entity );
}

vaShadowmap::vaShadowmap( vaRenderDevice & device, vaSceneLighting & lightingSystem, const weak_ptr<vaScene> & scene, entt::entity entity ) 
    : vaRenderingModule( vaRenderingModuleParams( device ) ), vaUIPanel( "SM", 0, false, vaUIPanel::DockLocation::DockedLeftBottom, "ShadowMaps" ), 
    m_lightingSystem( lightingSystem ), 
    m_scene( scene ), 
    m_entity( entity ), 
    m_entityName( GetEntityName(scene, entity) ) 
{

}

vaCubeShadowmap::vaCubeShadowmap( vaRenderDevice & device, vaSceneLighting & lightingSystem, const weak_ptr<vaScene> & scene, entt::entity entity ) : vaShadowmap( device, lightingSystem, scene, entity )
{
    Invalidate();
}

void vaCubeShadowmap::UIPanelTick( vaApplicationBase & )
{
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    GetRenderDevice().GetTextureTools().UITickImGui( m_cubemapArraySRV );
#endif
}

shared_ptr<vaShadowmap> vaShadowmap::CreateDirectional( vaRenderDevice & device, vaSceneLighting & lightingSystem, const weak_ptr<vaScene> & scene, entt::entity entity )
{
    device; lightingSystem; scene; entity;
    assert( false ); // not yet implemented
    return nullptr;
}

shared_ptr<vaCubeShadowmap> vaShadowmap::CreatePoint( vaRenderDevice & device, vaSceneLighting & lightingSystem, const weak_ptr<vaScene> & scene, entt::entity entity )
{
    return std::make_shared<vaCubeShadowmap>( device, lightingSystem, scene, entity );
}

void vaCubeShadowmap::SetToRenderSelectionFilter( vaRenderInstanceList::FilterSettings & filter ) const
{
    filter;
    // make a frustum cube based on
    // Position
    // ClipFar
}

vaDrawResultFlags vaCubeShadowmap::Draw( vaRenderDeviceContext & renderContext, vaRenderInstanceList & renderSelection )
{
    if( m_storageTextureIndex == -1 )
        return vaDrawResultFlags::UnspecifiedError;
 
    vaCameraBase cameraFrontCubeFace;

    // not sure why is this assert here but smaller value might not work - figure out if this is actually correct
    assert( m_lightSize > 0.001f );

    cameraFrontCubeFace.SetYFOV( 90.0f / 180.0f * VA_PIf );
    cameraFrontCubeFace.SetNearPlaneDistance( m_lightSize );
    cameraFrontCubeFace.SetFarPlaneDistance( m_lightRange );
    cameraFrontCubeFace.SetViewport( vaViewport( m_cubemapSliceDSVs[0]->GetSizeX(), m_cubemapSliceDSVs[0]->GetSizeY() ) );
    cameraFrontCubeFace.SetPosition( m_lightPosition );

    vaDrawResultFlags drawResults = vaDrawResultFlags::None;

    shared_ptr<vaTexture> * destinationCubeDSVs = m_cubemapSliceDSVs;
    //shared_ptr<vaTexture> * destinationCubeRTVs = m_cubemapArrayRTVs;
    {
        VA_TRACE_CPUGPU_SCOPE( CubemapDepthOnly, renderContext );

        vaRenderOutputs outputs;

        vaVector3 position = cameraFrontCubeFace.GetPosition( );
        vaCameraBase tempCamera = cameraFrontCubeFace;

        // draw all 6 faces - this should get optimized to GS in the future
        for( int i = 0; i < 6; i++ )
        {
            // I hope this clears just the single slice on all HW
            destinationCubeDSVs[i]->ClearDSV( renderContext, true, cameraFrontCubeFace.GetUseReversedZ( ) ? ( 0.0f ) : ( 1.0f ), false, 0 );
            // destinationCubeRTVs[i]->ClearRTV( renderContext, vaVector4( 10000.0f, 10000.0f, 10000.0f, 10000.0f ) );

            vaVector3 lookAtDir, upVec;

            // see https://msdn.microsoft.com/en-us/library/windows/desktop/bb204881(v=vs.85).aspx
            switch( i )
            {
            case 0: // positive x (+y up)
                lookAtDir   = vaVector3( 1.0f, 0.0f, 0.0f );
                upVec       = vaVector3( 0.0f, 1.0f, 0.0f );
                break;
            case 1: // negative x (+y up)
                lookAtDir   = vaVector3( -1.0f, 0.0f, 0.0f );
                upVec       = vaVector3( 0.0f, 1.0f, 0.0f );
                break;
            case 2: // positive y (-z up)
                lookAtDir   = vaVector3( 0.0f, 1.0f, 0.0f );
                upVec       = vaVector3( 0.0f, 0.0f, -1.0f );
                break;
            case 3: // negative y (z up)
                lookAtDir   = vaVector3( 0.0f, -1.0f, 0.0f );
                upVec       = vaVector3( 0.0f, 0.0f, 1.0f );
                break;
            case 4: // positive z (y up)
                lookAtDir   = vaVector3( 0.0f, 0.0f, 1.0f );
                upVec       = vaVector3( 0.0f, 1.0f, 0.0f );
                break;
            case 5: // negative z (y up)
                lookAtDir   = vaVector3( 0.0f, 0.0f, -1.0f );
                upVec       = vaVector3( 0.0f, 1.0f, 0.0f );
                break;
            }

            tempCamera.SetOrientationLookAt( position + lookAtDir, upVec );
            tempCamera.Tick( 0, false );
        
            vaDrawAttributes drawAttributes( tempCamera, vaDrawAttributes::RenderFlags::None );
            //drawAttributes.ViewspaceDepthOffsets = lightingSystem->GetShadowCubeViewspaceDepthOffsets();

            //renderContext.SetRenderTarget( nullptr, destinationCubeDepth, true );
            outputs.SetRenderTarget( nullptr, destinationCubeDSVs[i], true );

            drawResults |= GetRenderDevice().GetMeshManager().Draw( renderContext, outputs, vaRenderMaterialShaderType::DepthOnly, drawAttributes, renderSelection, vaBlendMode::Opaque, vaRenderMeshDrawFlags::EnableDepthTest | vaRenderMeshDrawFlags::EnableDepthWrite | vaRenderMeshDrawFlags::SkipNonShadowCasters );
        }
    }

    if( drawResults == vaDrawResultFlags::None )
        SetUpToDate();
    return drawResults;
}

std::pair<shared_ptr<vaIBLProbe>, Scene::IBLProbe> vaSceneLighting::GetNextHighestPriorityIBLProbeForRendering( )
{
    if( m_localIBLProbe != nullptr && m_localIBLProbePendingData.Enabled && m_localIBLProbe->GetContentsData( ) != m_localIBLProbePendingData )
        return {m_localIBLProbe, m_localIBLProbePendingData}; 
    if( m_distantIBLProbe != nullptr && m_distantIBLProbePendingData.Enabled && m_distantIBLProbe->GetContentsData( ) != m_distantIBLProbePendingData )
        return { m_distantIBLProbe, m_distantIBLProbePendingData };
    return std::make_pair<shared_ptr<vaIBLProbe>, Scene::IBLProbe>( nullptr, {} );
}

bool vaSceneLighting::HasPendingVisualDependencies( )
{
    if( GetNextHighestPriorityShadowmapForRendering() != nullptr )
        return true;
    if( GetNextHighestPriorityIBLProbeForRendering().first != nullptr )
        return true;
    return false;
}

void vaSceneLighting::UpdateFromScene( vaScene & scene, float deltaTime, int64 tickCounter )
{
    tickCounter;

    // Handle distant IBL
    bool hadDistantIBL = false;
    m_distantIBLProbePendingData.Enabled = false;
    scene.Registry( ).view<Scene::DistantIBLProbe, Scene::TransformWorld>( ).each( [&]( const Scene::DistantIBLProbe & probe, const Scene::TransformWorld & world )
    {   world;
        assert( !hadDistantIBL );
        hadDistantIBL = true;
        m_distantIBLProbePendingData = probe;
    } );
    if( !m_distantIBLProbePendingData.Enabled )
        m_distantIBLProbe->Reset();

    // Handle local IBL
    bool hadLocalIBL = false;
    m_localIBLProbePendingData.Enabled = false;
    scene.Registry( ).view<Scene::LocalIBLProbe, Scene::TransformWorld>( ).each( [&]( const Scene::LocalIBLProbe & probe, const Scene::TransformWorld & world )
    {   world;
        assert( !hadLocalIBL );
        hadLocalIBL = true;
        m_localIBLProbePendingData = probe;
    } );
    if( !m_localIBLProbePendingData.Enabled )
        m_localIBLProbe->Reset();

    // Handle fog
    m_collectedFogSphere = {};  // reset
    scene.Registry( ).view<Scene::FogSphere, Scene::TransformWorld>( ).each( [ & ]( const Scene::FogSphere & fogSphere, const Scene::TransformWorld & world )
    {
        if( !fogSphere.Enabled )
            return;
        assert( !m_collectedFogSphere.Enabled );   // multiple enabled fog spheres at the same time? that's not supported (yet)!

        world; // <- should transform pos with this
        m_collectedFogSphere = fogSphere;
    } );

    // Handle ambient light (just add all to this one, nothing more needed)
    m_collectedAmbientLightIntensity = {0,0,0};
    scene.Registry( ).view<Scene::LightAmbient>( ).each( [ & ]( entt::entity entity, const Scene::LightAmbient & ambient )
    {
        m_collectedAmbientLightIntensity += ambient.Color * (ambient.Intensity * ambient.FadeFactor); entity;
    } );

    // // Collect directional lights
    // m_collectedDirectionalLights.clear();
    // scene.Registry( ).view<Scene::LightDirectional, Scene::TransformWorld>( ).each( [ & ]( entt::entity entity, const Scene::LightDirectional & directional, const Scene::TransformWorld & world )
    // {
    //     // disabled 
    //     if( directional.FadeFactor == 0 || directional.Intensity == 0 )
    //         return;
    // 
    //     // not supported (yet)
    //     assert( !directional.CastShadows );
    //     entity;
    //     ShaderLightDirectional light;
    //     light.Color                 = directional.Color;
    //     light.Intensity             = directional.Intensity * directional.FadeFactor;
    //     vaColor::NormalizeLuminance( light.Color, light.Intensity );
    //     light.Direction             = world.GetAxisX().Normalized();
    //     light.Dummy1                = 0;
    //     if( directional.AngularRadius == 0.0f )
    //         light.SunAreaLightParams = { 0,0,0,-1 };
    //     else
    //     {
    //         // originally from Filament (View.cpp)
    //         vaVector4& sun = light.SunAreaLightParams;
    //         sun.x = std::cosf( directional.AngularRadius );
    //         sun.y = std::sinf( directional.AngularRadius );
    //         sun.z = 1.0f / ( std::cosf( directional.AngularRadius * directional.HaloSize ) - sun.x );
    //         sun.w = directional.HaloFalloff;
    //     }
    // 
    //     if( m_collectedDirectionalLights.size( ) >= ShaderLightDirectional::MaxLights )
    //         VA_WARN( "Max number of directional lights (%d) reached, some will be ignored", (int)ShaderLightDirectional::MaxLights );
    //     else
    //         m_collectedDirectionalLights.push_back( {entity, light} );
    // } );

    // Collect point/spot lights
    m_collectedPointLights.clear();
    m_collectedPointLightsMetadata.clear();
    m_collectedPointLightEntities.clear();
    m_collectedPointLightsSortIndices.clear();
    m_sortedPointLights.clear();

    vaVector3 lightPosMin = {FLT_MAX, FLT_MAX, FLT_MAX};
    vaVector3 lightPosMax = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    scene.Registry( ).view<Scene::LightPoint, Scene::TransformWorld>( ).each( [ & ]( entt::entity entity, const Scene::LightPoint & point, const Scene::TransformWorld & world )
    {
        // disabled 
        if( point.FadeFactor == 0 || point.Intensity == 0 )
            return;

        // not supported (yet)
        // assert( !point.CastShadows );
        entity;
        ShaderLightPoint light;
        light.Color                 = point.Color;
        light.Intensity             = point.Intensity * point.FadeFactor;
        light.Position              = world.GetTranslation( ) - m_worldBase;
        light.Direction             = world.GetAxisX().Normalized();
        light.Size                  = std::max( VA_EPSf, point.Size );
        light.RTSizeModifier        = point.RTSizeModifier;
        light.Range                 = point.Range;
        light.SpotInnerAngle        = point.SpotInnerAngle;
        light.SpotOuterAngle        = point.SpotOuterAngle;
        vaColor::NormalizeLuminance( light.Color, light.Intensity );

        // point lights are handled as spotlights
        if( light.SpotInnerAngle == 0 && light.SpotOuterAngle == 0 )
        {
            light.SpotInnerAngle = VA_PIf;
            light.SpotOuterAngle = VA_PIf;
        }

        light.CubeShadowIndex       = -1;

        lightPosMin = vaVector3::ComponentMin( lightPosMin, light.Position );
        lightPosMax = vaVector3::ComponentMax( lightPosMax, light.Position );
        

        if( m_collectedPointLights.size() >= ShaderLightPoint::MaxPointLights )
        {
            VA_WARN( "Max number of spot lights (%d) reached, some will be ignored", (int)ShaderLightPoint::MaxPointLights );
        }
        else
        {
            m_collectedPointLights.push_back( light );
            m_collectedPointLightEntities.push_back( point.CastShadows?(entity):(entt::null) );
        }
    } );

    // if no lights in scene, create a dummy light to avoid having to handle this case in shaders
    if( m_collectedPointLights.size() == 0 )
    {
        ShaderLightPoint light;
        light.Color                 = vaVector3(1,1,1);
        light.Intensity             = 0.0f;
        light.Position              = vaVector3(0,0,0);
        light.Direction             = vaVector3(0,0,1);
        light.Size                  = 1.0f;
        light.RTSizeModifier        = 0;
        light.Range                 = 0;
        light.SpotInnerAngle        = 0;
        light.SpotOuterAngle        = 0;
        m_collectedPointLights.push_back( light );
        m_collectedPointLightEntities.push_back( entt::null );
    }

    // initialize meta-data
    m_collectedPointLightsMetadata.resize( m_collectedPointLights.size() );
    m_collectedPointLightsSortIndices.resize( m_collectedPointLights.size() );
    for( int i = 0; i < (int)m_collectedPointLightsSortIndices.size(); i++ ) m_collectedPointLightsSortIndices[i] = i;

    // pre-process lights
    {
        vaVector3 size      = lightPosMax - lightPosMin;
        //vaVector3 center    = lightPosMin + size * 0.5f;
        float extent = std::max( std::max( size.x, size.y ), size.z );
        float scale = (extent == 0) ? (0.0f) : (1.0f / extent);

        // todo: revisit this, read https://github.com/Forceflow/libmorton
        auto morton3D = [](unsigned int x)
        {
            x &= 0x000003ff;                  // x = ---- ---- ---- ---- ---- --98 7654 3210
            x = (x ^ (x << 16)) & 0xff0000ff; // x = ---- --98 ---- ---- ---- ---- 7654 3210
            x = (x ^ (x << 8)) & 0x0300f00f;  // x = ---- --98 ---- ---- 7654 ---- ---- 3210
            x = (x ^ (x << 4)) & 0x030c30c3;  // x = ---- --98 ---- 76-- --54 ---- 32-- --10
            x = (x ^ (x << 2)) & 0x09249249;  // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
            return x;
        };

        // generate Morton order based on the position inside the unit cube
        for( size_t i = 0; i < m_collectedPointLights.size(); i++ )
        {
            const vaVector3 & position = m_collectedPointLights[i].Position;

            int x = int( (position.x - lightPosMin[0]) * scale * 1023.0f + 0.5f);
            int y = int( (position.y - lightPosMin[1]) * scale * 1023.0f + 0.5f);
            int z = int( (position.z - lightPosMin[2]) * scale * 1023.0f + 0.5f);

            m_collectedPointLightsMetadata[i].MortonCode = morton3D(x) | (morton3D(y) << 1) | (morton3D(z) << 2);
        }

        std::sort( m_collectedPointLightsSortIndices.begin(), m_collectedPointLightsSortIndices.end(), 
            [&meta=m_collectedPointLightsMetadata] ( uint32 left, uint32 right ) { return meta[left].MortonCode < meta[right].MortonCode; } );

        // this copy could be avoided if we just use indices but it's easier this way for now, and a GPU version will make this obsolete anyhow
        assert( m_sortedPointLights.size() == 0 );
        for( size_t i = 0; i < m_collectedPointLightsSortIndices.size(); i++ )
            m_sortedPointLights.push_back( m_collectedPointLights[m_collectedPointLightsSortIndices[i]] );

        // tree build goes here
        {
            // perfect binary tree, starting with single top node
            const int lightCount            = (int)m_sortedPointLights.size();
            m_lightTreeDepth                = vaMath::Log2( std::max( 0, lightCount*2 - 1 ) ) + 1;
            const int treeBottomLevelSize   = (1 << (m_lightTreeDepth-1));
            const int treeBottomLevelOffset = treeBottomLevelSize;
            m_lightTree.resize( treeBottomLevelSize + treeBottomLevelSize );

            // fill in the whole of the bottom tree level and use dummmy (bogus) nodes to fill up to 2^n elements
            for( int i = treeBottomLevelOffset; i < treeBottomLevelOffset+treeBottomLevelSize; i++ )
            {
                int lightIndex = i-treeBottomLevelOffset;
                assert( lightIndex >= 0 && lightIndex < m_lightTree.size() );

                ShaderLightTreeNode & node = m_lightTree[i];

                // valid light, initialize
                if( lightIndex < lightCount )
                {
                    ShaderLightPoint & light = m_sortedPointLights[lightIndex];
                    node.Center             = light.Position;
                    node.UncertaintyRadius  = 0.0f;
                    node.IntensitySum       = light.Intensity * vaColor::LinearToLuminance( light.Color );
                    node.RangeAvg           = std::max( VA_EPSf, light.Range );          // this is the range beyond which this attenuates to 0; clamp to VA_EPSf to avoid singularities
                    node.SizeAvg            = light.Size;           // this is the light size which prevents singularities
                }
                else
                    node.SetDummy( );
            }

            // compute remaining tree levels
            for( int level = m_lightTreeDepth-2; level >= 0; level-- )
            {
                int levelCount  = 1 << level;
                int levelOffset = levelCount;
                for( int i = levelOffset; i < levelOffset + levelCount; i++ )
                {
                    ShaderLightTreeNode & node = m_lightTree[i];

                    ShaderLightTreeNode & subNodeL = m_lightTree[i*2+0];
                    ShaderLightTreeNode & subNodeR = m_lightTree[i*2+1];
                    if( subNodeL.IsDummy() )
                        node = subNodeR;
                    else if( subNodeR.IsDummy() )
                        node = subNodeL;
                    else
                    {   // merge
                        node.IntensitySum   = subNodeL.IntensitySum + subNodeR.IntensitySum;
#if 1   // true bounding sphere merge
                        vaBoundingSphere sm = vaBoundingSphere::Merge( vaBoundingSphere( subNodeL.Center, subNodeL.UncertaintyRadius ), vaBoundingSphere( subNodeR.Center, subNodeR.UncertaintyRadius ) );
                        node.Center             = sm.Center;
                        node.UncertaintyRadius  = sm.Radius;
#else   // a hack that doesn't really work well
                        node.Center = (subNodeL.IntensitySum*subNodeL.Center +    subNodeR.IntensitySum*subNodeR.Center) / node.IntensitySum;
                        node.UncertaintyRadius = std::max( (node.Center - subNodeL.Center).Length() + subNodeL.UncertaintyRadius, (node.Center - subNodeR.Center).Length() + subNodeR.UncertaintyRadius );
#endif

#if 0   // average
                        node.RangeAvg       = (subNodeL.RangeAvg + subNodeR.RangeAvg) * 0.5f;
                        node.SizeAvg        = (subNodeL.SizeAvg + subNodeR.SizeAvg) * 0.5f;
#elif 1 // intensity-weighted average
                        node.RangeAvg       = (subNodeL.IntensitySum*subNodeL.RangeAvg +    subNodeR.IntensitySum*subNodeR.RangeAvg) / node.IntensitySum;
                        node.SizeAvg        = (subNodeL.IntensitySum*subNodeL.SizeAvg +     subNodeR.IntensitySum*subNodeR.SizeAvg)  / node.IntensitySum;
                        //node.RangeAvg       = vaMath::Lerp( node.RangeAvg, std::max(subNodeL.RangeAvg,subNodeR.RangeAvg), 0.2f );
                        //node.SizeAvg        = vaMath::Lerp( node.SizeAvg , std::max(subNodeL.SizeAvg,subNodeR.SizeAvg), 0.1f );

#elif 0 // sqr_intensity-weighted average
                        float wl = std::sqrt(subNodeL.IntensitySum);
                        float wr = std::sqrt(subNodeR.IntensitySum); 
                        float ws = wl+wr;
                        node.RangeAvg       = (wl*subNodeL.RangeAvg +    wr*subNodeR.RangeAvg) / ws;
                        node.SizeAvg        = (wl*subNodeL.SizeAvg +     wr*subNodeR.SizeAvg)  / ws;
#endif
                    }
                }
            }
        }

        if( m_debugVizLTEnable )
        {
            auto & canvas2D = GetRenderDevice().GetCanvas2D( ); canvas2D;
            auto & canvas3D = GetRenderDevice().GetCanvas3D( );
            m_debugVizLTHighlightLevel = vaMath::Clamp( m_debugVizLTHighlightLevel, -1, m_lightTreeDepth-1 );

            for( int level = 0; level < m_lightTreeDepth; level++ )
            {
                float alpha = (m_debugVizLTHighlightLevel == level)?( 0.5f ):( 0.02f );
                vaVector4 color = {0.2f, 0.2f, 1.0f, alpha};
                if( level == m_lightTreeDepth-1 )
                    color = {1.0f, 1.0f, 1.0f, alpha};

                int levelCount  = 1 << level;
                int levelOffset = levelCount;
                for( int i = levelOffset; i < levelOffset + levelCount; i++ )
                {
                    ShaderLightTreeNode & node = m_lightTree[i];

                    canvas3D.DrawSphere( vaBoundingSphere(node.Center, node.SizeAvg), 0, color.ToBGRA() );

                    if( level == m_lightTreeDepth-1 )
                        canvas3D.DrawAxis( node.Center, node.SizeAvg );

                    if( m_debugVizLTHighlightLevel == level )
                    {
                        canvas3D.DrawSphere( vaBoundingSphere(node.Center, node.UncertaintyRadius+node.SizeAvg), 0, vaVector4(1,0,0,0.5f).ToBGRA() );

                        if( m_debugVizLTTextEnable )
                        {
                            canvas2D.DrawText3D( canvas3D.GetLastCamera(), node.Center, {0,0}, 0xFFFFFFFF, 0x00000000, "Node %d, level %d", i, level );
                            canvas2D.DrawText3D( canvas3D.GetLastCamera(), node.Center, {0,16}, 0xFFFFFFFF, 0x00000000, "UncertaintyRadius: %.2f, IntensitySum: %.2f, RangeMax: %.2f, SizeMin: %.2f", node.UncertaintyRadius, node.IntensitySum, node.RangeAvg, node.SizeAvg );
                        }
                    }

                    if( 
                        //( m_debugVizLTHighlightLevel == level || m_debugVizLTHighlightLevel == level + 1 ) && 
                        ( level < ( m_lightTreeDepth - 1 ) ) )
                    {
                        vaVector4 colorArrow = {0.5f, 0.5f, 0.5f, 0.01f};
                        if( level == m_debugVizLTHighlightLevel-1 )
                            colorArrow = {1.0f, 1.0f, 0.3f, 0.5f};
                        else if( level == m_debugVizLTHighlightLevel )
                            colorArrow = {0.3f, 1.0f, 1.0f, 0.5f};
                        ShaderLightTreeNode & subNodeL = m_lightTree[i*2+0];
                        ShaderLightTreeNode & subNodeR = m_lightTree[i*2+1];
                        if( subNodeL.IsDummy() )
                            canvas3D.DrawArrow( node.Center, subNodeL.Center, 0.01f, colorArrow.ToBGRA(), colorArrow.ToBGRA(), colorArrow.ToBGRA() );
                        if( subNodeR.IsDummy() )
                            canvas3D.DrawArrow( node.Center, subNodeR.Center, 0.01f, colorArrow.ToBGRA(), colorArrow.ToBGRA(), colorArrow.ToBGRA() );
                    }
                }
            }
        }

        m_lightTreeBottomLevelSize   = (1 << (m_lightTreeDepth-1));
        m_lightTreeBottomLevelOffset = m_lightTreeBottomLevelSize;

        if( m_debugVizLTTraversalTest && m_debugVizLTTraversalRefPt.x != FLT_MAX )
        {
            auto & canvas2D = GetRenderDevice().GetCanvas2D( ); canvas2D;
            auto & canvas3D = GetRenderDevice().GetCanvas3D( );
            canvas3D.DrawSphere( vaBoundingSphere( m_debugVizLTTraversalRefPt, 0.1f), 0, vaVector4(0,1,0,1).ToBGRA() );

            // simple baseline importance sampling based on intensity only
            auto traverseBaseline = []( int lightCount, const std::vector<ShaderLightTreeNode> & tree, int treeDepth, const vaVector3 & pos, vaRandom & rnd ) -> int
            {
                pos; // not used
                const int treeBottomLevelSize   = (1 << (treeDepth-1));
                const int treeBottomLevelOffset = treeBottomLevelSize;
                const float intensitySumAll = tree[1].IntensitySum;

                float nextRnd   = rnd.NextFloat() * intensitySumAll;
                float sumSoFar  = 0.0f;
                
                for( int nodeIndex = treeBottomLevelOffset; nodeIndex < (treeBottomLevelOffset+treeBottomLevelSize); nodeIndex++ )
                {
                    sumSoFar += tree[nodeIndex].IntensitySum;
                    if( sumSoFar >= nextRnd )
                        return std::min( lightCount-1, nodeIndex - treeBottomLevelOffset );
                }
                return lightCount-1;
            };

            // most optimal reference (importance sampling based on actual weight)
            auto traverseReference = []( int lightCount, const std::vector<ShaderLightTreeNode> & tree, int treeDepth, const vaVector3 & pos, vaRandom & rnd ) -> int
            {
                const int treeBottomLevelSize   = (1 << (treeDepth-1));
                const int treeBottomLevelOffset = treeBottomLevelSize;
                
                float weightSum = 0.0f;
                for( int nodeIndex = treeBottomLevelOffset; nodeIndex < (treeBottomLevelOffset+treeBottomLevelSize); nodeIndex++ )
                    weightSum += tree[nodeIndex].Weight( pos );

                float nextRnd   = rnd.NextFloat() * weightSum;
                float sumSoFar  = 0.0f;
                for( int nodeIndex = treeBottomLevelOffset; nodeIndex < (treeBottomLevelOffset+treeBottomLevelSize); nodeIndex++ )
                {
                    sumSoFar += tree[nodeIndex].Weight( pos );
                    if( sumSoFar >= nextRnd )
                        return std::min( lightCount-1, nodeIndex - treeBottomLevelOffset );
                }
                return lightCount-1;
            };

            auto traverseDevelopment = []( int lightCount, const std::vector<ShaderLightTreeNode> & tree, int treeDepth, const vaVector3 & pos, vaRandom & rnd ) -> int
            {
                const int treeBottomLevelSize   = (1 << (treeDepth-1));
                const int treeBottomLevelOffset = treeBottomLevelSize;

                int nodeIndex = 1;
                for( int depth = 0; depth < treeDepth-1; depth++ )
                {
                    const ShaderLightTreeNode & subNodeL = tree[nodeIndex*2+0];
                    const ShaderLightTreeNode & subNodeR = tree[nodeIndex*2+1];

                    if( subNodeL.IsDummy() )
                    {
                        nodeIndex = nodeIndex*2+1;     // left is dummy, pick right
                        assert( false ); // hey left should never be dummy
                        continue;
                    } 
                    else if( subNodeR.IsDummy() )
                    {
                        nodeIndex = nodeIndex*2+0;     // right is dummy, pick left
                        continue;
                    }
                    float weightL = subNodeL.Weight( pos );
                    float weightR = subNodeR.Weight( pos );
                    float weightSum = weightL+weightR;
                    if( weightSum == 0 )
                    {
                        //assert( false );
                        return -1;
                    }
                    float lr = weightL / (weightSum);
                    float nextRnd = rnd.NextFloat();
                    if( nextRnd <= lr )
                        nodeIndex = nodeIndex*2+0;     // pick left
                    else
                        nodeIndex = nodeIndex*2+1;     // pick right
                }
                return std::min( lightCount-1, nodeIndex - treeBottomLevelOffset );
            };

            std::vector<int> hitCountsBaseline, hitCountsReference, hitCountsDevelopment;

            int fails = 0;

            auto traverseMany = [&]( std::vector<int> & output, int count, std::function<int(int lightCount, const std::vector<ShaderLightTreeNode> & tree, int treeDepth, const vaVector3 & pos, vaRandom & rnd)> traversalFunc )
            {
                vaRandom rnd( m_debugVizLTTraversalSeed );
                output.resize( m_sortedPointLights.size(), 0 );

                for( int i = 0; i < count; i++ )
                {
                    int index = traversalFunc( (int)m_sortedPointLights.size(), m_lightTree, m_lightTreeDepth, m_debugVizLTTraversalRefPt, rnd );
                    if( index == -1 )
                        fails++;
                    else
                        output[index]++;
                }
            };

            traverseMany( hitCountsBaseline,        m_debugVizLTTraversalCount, traverseBaseline );
            traverseMany( hitCountsReference,       m_debugVizLTTraversalCount, traverseReference );
            traverseMany( hitCountsDevelopment,     m_debugVizLTTraversalCount, traverseDevelopment );

            vaVector4 colorArrow = {1.0f, 0.0f, 1.0f, 0.01f};
            //canvas3D.DrawArrow( m_debugVizLTTraversalRefPt, m_sortedPointLights[index].Position, 0.03f, 0, colorArrow.ToBGRA(), colorArrow.ToBGRA() );

            if( fails > 0 )
                canvas2D.DrawText3D( canvas3D.GetLastCamera( ), m_debugVizLTTraversalRefPt, {0,0}, 0xFFFFFFFF, 0xFF000000, "Failed searches: %d", fails );

            float MSEBaseline       = 0;
            float MSEDevelopment    = 0;
            const float lightCount = (float)m_sortedPointLights.size();
            for( int i = 0; i < m_sortedPointLights.size(); i++ )
            {
                float be = (float)hitCountsBaseline[i]     - (float)hitCountsReference[i];
                float de = (float)hitCountsDevelopment[i]  - (float)hitCountsReference[i];
                MSEBaseline     += vaMath::Sq(be / lightCount);
                MSEDevelopment  += vaMath::Sq(de / lightCount);
            }

            canvas2D.DrawText3D( canvas3D.GetLastCamera( ), m_debugVizLTTraversalRefPt, {0, 16}, 0xFFFFFFFF, 0xFF000000, "MSEBaseline: %f", MSEBaseline );
            canvas2D.DrawText3D( canvas3D.GetLastCamera( ), m_debugVizLTTraversalRefPt, {0, 32}, 0xFFFFFFFF, 0xFF000000, "MSEDevelopment: %f", MSEDevelopment );

            for( int i = 0; i < m_sortedPointLights.size(); i++ )
            {
                canvas2D.DrawText3D( canvas3D.GetLastCamera( ), m_sortedPointLights[i].Position, {0, 0}, 0xFFFFFFFF, 0xFF000000, "b: %d", hitCountsBaseline[i] );
                canvas2D.DrawText3D( canvas3D.GetLastCamera( ), m_sortedPointLights[i].Position, {0,16}, 0xFF00FF00, 0xFF000000, "r: %d", hitCountsReference[i] );
                canvas2D.DrawText3D( canvas3D.GetLastCamera( ), m_sortedPointLights[i].Position, {0,32}, 0xFF0000FF, 0xFF000000, "d: %d", hitCountsDevelopment[i] );
                canvas2D.DrawText3D( canvas3D.GetLastCamera( ), m_sortedPointLights[i].Position, {0,48}, 0xFFFF0000, 0xFF000000, "de: %d", hitCountsReference[i]-hitCountsDevelopment[i] );
            }
        }

#if 0 // nicm_debugVizLTHighlightLevele debug viz!
        vaVector3 positionPrev = {0,0,0};
        for( size_t i = 0; i < m_collectedPointLightsSortIndices.size(); i++ )
        {
            vaVector3 position = m_collectedPointLights[m_collectedPointLightsSortIndices[i]].Position + m_worldBase;

            vaVector4 color = {0.5f, 1.0f, 1.0f, 0.5f};
            if( i > 0 )
                canvas3D.DrawArrow( positionPrev, position, 0.02f, 0x80000000, color.ToBGRA(), color.ToBGRA() );
            canvas2D.DrawText3D( canvas3D.GetLastCamera(), position, {0,0}, 0xFFFFFFFF, 0x00000000, "index: %d", i );

            positionPrev = position;
        }
#endif

    }

    // update shadows
    {
        VA_TRACE_CPU_SCOPE( vaSceneLighting_Tick );

        if( !m_shadowmapTexturesCreated )
            CreateShadowmapTextures( );

        // create shadowmaps for lights that need shadows; if already there, don't re-create, but if shadowmap exists
        // without a corresponding light then remove it (no pooling yet but probably not needed since textures are
        // held by vaSceneLighting anyways)
        for( int i = 0; i < m_shadowmaps.size( ); i++ )
            m_shadowmaps[i]->InUse( ) = false;
        for( int i = 0; i < m_sortedPointLights.size( ); i++ )
        {
            entt::entity entity = m_collectedPointLightEntities[ m_collectedPointLightsSortIndices[i] ];
            if( entity == entt::null )
                continue;
            ShaderLightPoint & light = m_sortedPointLights[i];
            if( light.Intensity < VA_EPSf )
                continue;

            shared_ptr<vaCubeShadowmap> cubeShadow = FindShadowmapForPointLight( entity );
            if( cubeShadow == nullptr )
            {
                cubeShadow = vaShadowmap::CreatePoint( GetRenderDevice( ), *this, {}, entity );
                assert( cubeShadow != nullptr );
                m_shadowmaps.push_back( cubeShadow );
            }
            cubeShadow->InUse( ) = true;

            cubeShadow->Tick( deltaTime, light );

            light.CubeShadowIndex = (float)cubeShadow->GetStorageTextureIndex( );
        }

        // if not in use, remove - not optimal but hey good enough for now
        for( int j = (int)m_shadowmaps.size( ) - 1; j >= 0; j-- )
        {
            if( !m_shadowmaps[j]->InUse( ) )
                m_shadowmaps.erase( m_shadowmaps.begin( ) + j );
        }
    }
    // above should happen in pre_render_selections

    m_renderBuffersDirty = true;
}

