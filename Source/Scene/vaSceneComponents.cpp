///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Rendering/vaRenderMesh.h"
#include "Rendering/vaRenderMaterial.h"

#include "Core/System/vaFileTools.h"

#include "vaSceneComponents.h"
#include "vaSceneComponentsUI.h"
#include "vaSceneSystems.h"

#include "Rendering/vaDebugCanvas.h"

using namespace Vanilla;

using namespace Vanilla::Scene;

bool Relationship::IsValid( entt::registry & registry )          
{ 
    if( Parent != entt::null && !registry.valid( Parent ) ) 
        return false;
    if( FirstChild != entt::null && !registry.valid( FirstChild ) )
        return false;
    if( PrevSibling != entt::null && !registry.valid( PrevSibling ) )
        return false;
    if( NextSibling != entt::null && !registry.valid( NextSibling ) )
        return false;
    if( Parent == entt::null && (PrevSibling != entt::null || NextSibling != entt::null) )
        return false;
    if( (FirstChild != entt::null && ChildrenCount == 0) || (FirstChild == entt::null && ChildrenCount != 0) )
        return false;
    if( Depth > c_MaxDepthValue )
        return false;
    return true;
}

bool WorldBounds::Update( const entt::registry & registry, entt::entity entity ) noexcept
{
    assert( registry.any_of<WorldBounds>( entity ) );
    const TransformWorld* transformWorld = registry.try_get<Scene::TransformWorld>( entity );

    const CustomBoundingBox * CustomBoundingBox = registry.try_get<Scene::CustomBoundingBox>( entity );
    const RenderMesh * renderMesh = registry.try_get<Scene::RenderMesh>( entity );

    AABB = vaBoundingBox::Degenerate;

    if( CustomBoundingBox == nullptr && renderMesh == nullptr)
    {
        assert( false ); // why is WorldBounds still created? should have been cleared by AutoEmplaceDestroy<WorldBounds>?
        return false;
    }

    if( CustomBoundingBox != nullptr )
        AABB = *CustomBoundingBox;

    bool allUpdated = true;

    if( renderMesh != nullptr )
    {
        AABB = renderMesh->GetAABB();
        if( AABB == vaBoundingBox::Degenerate )   // still dirty? maybe loading - let's just wait.
            allUpdated = false;
    }

    if( transformWorld != nullptr )
    {
        // Our bounding box, oriented in world space
        vaOrientedBoundingBox oobb = vaOrientedBoundingBox( AABB, *transformWorld );
        //vaDebugCanvas3D::GetInstance().DrawBox( oobb, 0xFF000000, 0x20FF8080 );
        AABB = oobb.ComputeEnclosingAABB( );
        BS = vaBoundingSphere::FromOBB( oobb );
    }
    else
    {
        BS = vaBoundingSphere::FromAABB( AABB );
    }


    return allUpdated;
}

void TransformLocal::Reset( entt::registry & registry, entt::entity entity )
{
    static_cast<vaMatrix4x4&>( *this ) = vaMatrix4x4::Identity;
    Scene::SetTransformDirtyRecursiveSafe( registry, entity );
}

vaBoundingBox RenderMesh::GetAABB( ) const
{
    auto renderMesh = GetMeshFP();
    if( renderMesh != nullptr )
        return renderMesh->GetAABB();
    else
        return vaBoundingBox::Degenerate;
}

RenderMesh::RenderMesh( const vaGUID & meshID, const vaGUID & overrideMaterialID )
    : MeshUID( meshID ), OverrideMaterialUID( overrideMaterialID )
{

}

shared_ptr<vaRenderMesh> RenderMesh::GetMesh( ) const 
{ 
    return vaUIDObjectRegistrar::Find<vaRenderMesh>( MeshUID ); 
}

vaFramePtr<vaRenderMesh> RenderMesh::GetMeshFP( ) const 
{ 
    return vaUIDObjectRegistrar::FindFP<vaRenderMesh>( MeshUID ); 
}

//uint32 UIMoveTool::s_uniqueID = 0;

void IBLProbe::SetImportFilePath( const string& importFilePath, bool updateEnabled )
{
    if( updateEnabled )
        Enabled = importFilePath != "";

    ImportFilePath = vaFileTools::GetAbsolutePath( importFilePath );

    if( importFilePath == "" )
        return;

    string mediaPath = vaCore::GetMediaRootDirectoryNarrow( );
    auto subPath = ImportFilePath.find( mediaPath );
    if( subPath != string::npos )
    {
        ImportFilePath = ImportFilePath.substr( mediaPath.length( ) );
    }
}


void LightBase::ValidateColor( )
{
    Color       = vaVector3::ComponentMax( vaVector3( 0.0f, 0.0f, 0.0f ), Color );
    Intensity   = vaMath::Clamp( Intensity, 0.0f, VA_FLOAT_HIGHEST );
    FadeFactor  = vaMath::Clamp( FadeFactor, 0.0f, 1.0f );
}

void LightAmbient::Validate( entt::registry &, entt::entity )
{
    ValidateColor();
}

// void LightDirectional::Validate( entt::registry &, entt::entity )
// {
//     ValidateColor( );
// 
//     AngularRadius   = vaMath::Clamp( AngularRadius, 0.0f, VA_PIf / 10.0f );
//     HaloSize        = vaMath::Clamp( HaloSize, 0.0f, 1000.0f );
//     HaloFalloff     = vaMath::Clamp( HaloFalloff, 0.0f, VA_FLOAT_HIGHEST );
// }

void LightPoint::Validate( entt::registry &, entt::entity )
{
    ValidateColor( );

    Size            = vaMath::Max( 1e-5f, Size );
    Range           = vaMath::Max( 1e-5f, Range );
    SpotInnerAngle  = vaMath::Clamp( SpotInnerAngle, 0.0f, VA_PIf );
    SpotOuterAngle  = vaMath::Clamp( SpotOuterAngle, SpotInnerAngle, VA_PIf );
}

void FogSphere::Validate( ) 
{ 
    RadiusInner     = vaMath::Max( RadiusInner, 0.0f ); 
    RadiusOuter     = vaMath::Clamp( RadiusOuter, RadiusInner, 100000000.0f ); 
    BlendCurvePow   = vaMath::Clamp( BlendCurvePow, 0.001f, 1000.0f ); 
    BlendMultiplier = vaMath::Clamp( BlendMultiplier, 0.0f, 1.0f ); }

void SkyboxTexture::Validate( ) 
{ 
    ColorMultiplier = vaMath::Clamp( ColorMultiplier, 0.0f, 10000.0f ); 
}


void RenderCamera::FromCameraBase( entt::registry & registry, entt::entity entity, const vaCameraBase & source )
{
    registry; entity; source;
}

void RenderCamera::ToCameraBase( const entt::registry & registry, entt::entity entity, vaCameraBase & destination )
{
    registry; entity; destination;
}
