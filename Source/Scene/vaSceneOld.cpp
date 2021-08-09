///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaSceneOld.h"

#if 0

#include "Rendering/vaAssetPack.h"

#include "Rendering/vaDebugCanvas.h"

#include "Core/System/vaFileTools.h"

#include "IntegratedExternals/vaImguiIntegration.h"

#include "Rendering/vaRenderMesh.h"
#include "Rendering/vaRenderMaterial.h"

#include "Core/vaApplicationBase.h"

#include <set>



using namespace Vanilla;
using namespace Vanilla::Scene;


vaSceneObject::vaSceneObject( )
{ 
}
vaSceneObject::~vaSceneObject( )                   
{
    // children/parents must be disconnected!
    assert( m_parent == nullptr );
    assert( m_children.size() == 0 );
}

vaMatrix4x4 vaSceneObject::GetWorldTransform( ) const
{
#ifdef _DEBUG
    shared_ptr<vaSceneOld> scene = GetScene();
    assert( scene != nullptr );
    assert( m_lastSceneTickIndex == scene->GetTickIndex( ) );   // for whatever reason, you're getting stale data
#endif
    return m_computedWorldTransform;
}

void vaSceneObject::FindClosestRecursive( const vaVector3 & worldLocation, shared_ptr<vaSceneObject> & currentClosest, float & currentDistance )
{
    // can't get any closer than this
    if( currentDistance == 0.0f )
        return;

    for( int i = 0; i < m_children.size( ); i++ )
    {
        m_children[i]->FindClosestRecursive( worldLocation, currentClosest, currentDistance );
    }

    float distance = GetGlobalAABB().NearestDistanceToPoint( worldLocation );

    if( distance < currentDistance )
    {
        currentDistance = distance;
        currentClosest = this->shared_from_this();
    }
}

void vaSceneObject::TickRecursive( vaSceneOld & scene, float deltaTime )
{
    assert( m_scene.lock() == scene.shared_from_this() );

    assert( !m_destroyedButNotYetRemovedFromScene );
    assert( !m_createdButNotYetAddedToScene );

    // Update transforms (OK to rely on parent's transforms, they've already been updated)
    if( m_parent == nullptr )
        m_computedWorldTransform = GetLocalTransform();
    else
        m_computedWorldTransform = GetLocalTransform() * m_parent->GetWorldTransform();

    if( (m_computedLocalBoundingBox == vaBoundingBox::Degenerate || m_computedLocalBoundingBoxIncomplete ) && m_renderMeshes.size( ) > 0 )
    {
        UpdateLocalBoundingBox();
    }

    // Update tick index (transforms ok, but beware, bounding boxes not yet)
    m_lastSceneTickIndex = scene.GetTickIndex();

    // Our bounding box, oriented in world space
    vaOrientedBoundingBox oobb = vaOrientedBoundingBox( m_computedLocalBoundingBox, m_computedWorldTransform );
    //vaDebugCanvas3D::GetInstance().DrawBox( oobb, 0xFF000000, 0x20FF8080 );
    m_computedGlobalBoundingBox = oobb.ComputeEnclosingAABB();
    //vaDebugCanvas3D::GetInstance().DrawBox( m_computedGlobalBoundingBox, 0xFF000000, 0x20FF8080 );

    for( int i = 0; i < m_children.size( ); i++ )
    {
        m_children[i]->TickRecursive( scene, deltaTime );
        m_computedGlobalBoundingBox = vaBoundingBox::Combine( m_computedGlobalBoundingBox, m_children[i]->GetGlobalAABB() );
    }
}

shared_ptr<vaRenderMesh> vaSceneObject::GetRenderMesh( int index ) const
{
    if( index < 0 || index >= m_renderMeshes.size( ) )
    {
        assert( false );
        return nullptr;
    }
    return vaUIDObjectRegistrar::Find<vaRenderMesh>( m_renderMeshes[index] );
}

void vaSceneObject::AddRenderMeshRef( const shared_ptr<vaRenderMesh> & renderMesh )    
{ 
    const vaGUID & uid = renderMesh->UIDObject_GetUID();
    assert( std::find( m_renderMeshes.begin(), m_renderMeshes.end(), uid ) == m_renderMeshes.end() ); 
    m_renderMeshes.push_back( uid ); 
    //m_cachedRenderMeshes.push_back( renderMesh );
    //assert( m_cachedRenderMeshes.size() == m_renderMeshes.size() );
    m_computedLocalBoundingBox = vaBoundingBox::Degenerate;
}

bool vaSceneObject::RemoveRenderMeshRef( int index )
{
    //assert( m_cachedRenderMeshes.size( ) == m_renderMeshes.size( ) );
    if( index < 0 || index >= m_renderMeshes.size( ) )
    {
        assert( false );
        return false;
    }

    m_renderMeshes[index] = m_renderMeshes.back();
    m_renderMeshes.pop_back();
    //m_cachedRenderMeshes[index] = m_cachedRenderMeshes.back( );
    //m_cachedRenderMeshes.pop_back( );
    m_computedLocalBoundingBox = vaBoundingBox::Degenerate;
    return true;
}

bool vaSceneObject::RemoveRenderMeshRef( const shared_ptr<vaRenderMesh> & renderMesh ) 
{ 
    //assert( m_cachedRenderMeshes.size() == m_renderMeshes.size() );
    const vaGUID & uid = renderMesh->UIDObject_GetUID();
    int indexRemoved = vector_find_and_remove( m_renderMeshes, uid );
    if( indexRemoved == -1 )
    {
        assert( false );
        return false;
    }
    //if( indexRemoved < (m_cachedRenderMeshes.size()-1) )
    //    m_cachedRenderMeshes[indexRemoved] = m_cachedRenderMeshes.back();
    // m_cachedRenderMeshes.pop_back();
    m_computedLocalBoundingBox = vaBoundingBox::Degenerate;
    return true;
}

bool vaSceneObject::Serialize( vaXMLSerializer & serializer )
{
    auto scene = m_scene.lock();
    assert( scene != nullptr );

    // element opened by the parent, just fill in attributes

    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<string>( "Name", m_name ) );

    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaVector4>( "TransformR0", m_localTransform.Row(0) ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaVector4>( "TransformR1", m_localTransform.Row(1) ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaVector4>( "TransformR2", m_localTransform.Row(2) ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaVector4>( "TransformR3", m_localTransform.Row(3) ) );

    // VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaVector3>( "AABBMin", m_boundingBox.Min ) );
    // VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaVector3>( "AABBSize", m_boundingBox.Size ) );

    assert( serializer.GetVersion() > 0 );
    serializer.SerializeArray( "RenderMeshes", m_renderMeshes );

    VERIFY_TRUE_RETURN_ON_FALSE( scene->SerializeObjectsRecursive( serializer, "ChildObjects", m_children, this->shared_from_this() ) );

    if( serializer.IsReading( ) )
    {
        m_lastSceneTickIndex = -1;
        m_computedWorldTransform = vaMatrix4x4::Identity;
        m_computedLocalBoundingBox = vaBoundingBox::Degenerate;
        m_computedGlobalBoundingBox = vaBoundingBox::Degenerate;
        // m_cachedRenderMeshes.resize( m_renderMeshes.size() );

        m_localTransform.Row(0).w = 0.0f;
        m_localTransform.Row(1).w = 0.0f;
        m_localTransform.Row(2).w = 0.0f;
        m_localTransform.Row(3).w = 1.0f;

        SetLocalTransform( m_localTransform );
    }

    return true;
}

void vaSceneObject::MarkAsDestroyed( bool markChildrenAsWell )
{
    // already destroyed? this shouldn't ever happen - avoid at all cost
    assert( !IsDestroyed() );

    m_destroyedButNotYetRemovedFromScene = true;
    if( markChildrenAsWell )
    {
        for( int i = 0; i < (int)m_children.size( ); i++ )
            m_children[i]->MarkAsDestroyed( true );
    }
}

void vaSceneObject::RegisterChildAdded( const shared_ptr<vaSceneObject> & child )
{
    assert( std::find( m_children.begin(), m_children.end(), child ) == m_children.end() );

    m_children.push_back( child );
}

void vaSceneObject::RegisterChildRemoved( const shared_ptr<vaSceneObject> & child )
{
    bool allOk = vector_find_and_remove( m_children, child ) != -1;
    assert( allOk ); // if this fires, there's a serious error somewhere!
    allOk;
}

void vaSceneObject::SetParent( const shared_ptr<vaSceneObject> & parent )
{
    if( parent == m_parent )
    {
        // nothing to change, nothing to do
        return;
    }
    auto scene = m_scene.lock();
    assert( scene != nullptr );
    if( scene == nullptr )
        return;

    // changing parent? then unregister us from previous (either root or another object)
    if( m_parent != nullptr )
        m_parent->RegisterChildRemoved( this->shared_from_this() );
    else
        scene->RegisterRootObjectRemoved( this->shared_from_this() );

    // removing parent? just register as a root object
    if( parent == nullptr )
    {
        m_parent = nullptr;
        scene->RegisterRootObjectAdded( this->shared_from_this() );
        return;
    }

    // make sure we're not setting up a no circular dependency or breaking the max node tree depth
    const int c_maxObjectTreeDepth = 32;
    bool allOk = false;
    shared_ptr<vaSceneObject> pp = parent;
    for( int i = 0; i < c_maxObjectTreeDepth; i++ )
    {
        if( pp == nullptr )
        {
            allOk = true;
            break;
        }
        if( pp.get( ) == this )
        {
            VA_LOG_ERROR( "vaSceneObject::SetParent failed - circular dependency detected" );
            allOk = false;
            return;
        }
        pp = pp->GetParent();
    }
    if( !allOk )
    {
        VA_LOG_ERROR( "vaSceneObject::SetParent failed - tree depth over the limit of %d", c_maxObjectTreeDepth );
        return;
    }
    m_parent = parent;
    m_parent->RegisterChildAdded( this->shared_from_this() );
}

// static vaBoundingSphere g_cullOutsideBS( vaVector3( 0, 0, 0 ), 0.0f );
// static vaBoundingBox g_cullInsideBox( vaVector3( 0, 0, 0 ), vaVector3( 0, 0, 0 ) );
// static bool g_doCull = false;
// static int64 g_isInside = 0;
// static int64 g_isOutside = 0;

vaDrawResultFlags vaSceneObject::SelectForRendering( vaRenderInstanceList * opaqueList, vaRenderInstanceList * transparentList, const vaRenderInstanceList::FilterSettings & filter, const SelectionFilterCallback & customFilter )
{
    vaDrawResultFlags drawResults = vaDrawResultFlags::None;

    // first and easy one, global filter by frustum planes
    if( filter.FrustumPlanes.size() > 0 )
        if( m_computedGlobalBoundingBox.IntersectFrustum( filter.FrustumPlanes ) == vaIntersectType::Outside )
            return vaDrawResultFlags::None;

    vaMatrix4x4 worldTransform = GetWorldTransform( );
    for( int i = 0; i < m_renderMeshes.size(); i++ )
    {
        auto renderMesh = GetRenderMesh(i);
        if( renderMesh == nullptr )
        {
            drawResults |= vaDrawResultFlags::AssetsStillLoading;
            continue;
        }
        // resolve material here - both easier to manage and faster (when this gets parallelized)
        auto renderMaterial = renderMesh->GetMaterial();
        if( renderMaterial == nullptr )
        {
            drawResults |= vaDrawResultFlags::AssetsStillLoading;
            renderMaterial = renderMesh->GetManager().GetRenderDevice().GetMaterialManager().GetDefaultMaterial( );
        }

        vaOrientedBoundingBox obb = vaOrientedBoundingBox::FromAABBAndTransform( renderMesh->GetAABB(), worldTransform );

        if( filter.FrustumPlanes.size() > 0 )
            if( obb.IntersectFrustum( filter.FrustumPlanes ) == vaIntersectType::Outside )
                continue;

        int baseShadingRate = 0;
        vaVector4 emissiveAdd = {0,0,0,1};
        bool doSelect = ( customFilter != nullptr )?( customFilter( *this, worldTransform, obb, *renderMesh, *renderMaterial, baseShadingRate, emissiveAdd ) ):( true );
        if( !doSelect )
            continue;

        vaShadingRate finalShadingRate = renderMaterial->ComputeShadingRate( baseShadingRate );

        vaRenderInstanceList::ItemProperties properties{ finalShadingRate, emissiveAdd, 0.0f };
        vaRenderInstanceList::ItemIdentifiers identifiers{ vaRenderInstanceList::SceneRuntimeIDNull, vaRenderInstanceList::SceneEntityIDNull };
        if( renderMaterial->IsTransparent() )
        {
            if( transparentList != nullptr ) 
                transparentList->Insert( vaFramePtr<vaRenderMesh>(renderMesh), vaFramePtr<vaRenderMaterial>(renderMaterial), worldTransform, properties, identifiers );
        }
        else
        {
            if( opaqueList != nullptr )
                opaqueList->Insert( vaFramePtr<vaRenderMesh>(renderMesh), vaFramePtr<vaRenderMaterial>(renderMaterial), worldTransform, properties, identifiers );
        }
    }
    return drawResults;
}

void vaSceneObject::RegisterUsedAssetPacks( std::function<void( const vaAssetPack & )> registerFunction )
{
    for( int i = 0; i < m_renderMeshes.size(); i++ )
    {
        auto renderMesh = GetRenderMesh(i);
        if( renderMesh != nullptr && renderMesh->GetParentAsset() != nullptr )
            registerFunction( renderMesh->GetParentAsset()->GetAssetPack() );
    }
}

void vaSceneObject::UpdateLocalBoundingBox( )
{
    //assert( m_cachedRenderMeshes.size() == m_renderMeshes.size() );
    m_computedLocalBoundingBox = vaBoundingBox::Degenerate;
    m_computedGlobalBoundingBox = vaBoundingBox::Degenerate;
    m_computedLocalBoundingBoxIncomplete = false;
    for( int i = 0; i < m_renderMeshes.size(); i++ )
    {
        auto renderMesh = GetRenderMesh(i);
        if( renderMesh != nullptr )
            m_computedLocalBoundingBox = vaBoundingBox::Combine( m_computedLocalBoundingBox, renderMesh->GetAABB() );
        else
            m_computedLocalBoundingBoxIncomplete = true;
    }
}

void vaSceneObject::UIPropertiesItemTick( vaApplicationBase &, bool , bool  )
{
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    auto scene = m_scene.lock();
    assert( scene != nullptr );

    if( ImGui::Button( "Rename" ) )
        ImGuiEx_PopupInputStringBegin( "Rename object", m_name );
    ImGuiEx_PopupInputStringTick( "Rename object", m_name );

    ImGui::SameLine();
    if( ImGui::Button( "Delete" ) )
    {
        scene->DestroyObject( this->shared_from_this(), false, true );
        return;
    }


    bool transChanged = false;
    vaMatrix4x4 localTransform = m_localTransform;
    transChanged |= ImGui::InputFloat4( "LocalTransformRow0", &localTransform.m[0][0] );
    transChanged |= ImGui::InputFloat4( "LocalTransformRow1", &localTransform.m[1][0] );
    transChanged |= ImGui::InputFloat4( "LocalTransformRow2", &localTransform.m[2][0] );
    transChanged |= ImGui::InputFloat4( "LocalTransformRow3", &localTransform.m[3][0] );

    if( transChanged )
    {
        SetLocalTransform( localTransform );
    }

    /*
    vaVector3 raxis; float rangle;
    m_rotation.ToAxisAngle( raxis, rangle );
    rangle = rangle * 180.0f / VA_PIf;
    bool changed =  ImGui::InputFloat3( "Rotation axis", &raxis.x );
    changed |=      ImGui::InputFloat( "Rotation angle", &rangle, 5.0f, 30.0f, 3 );
    if( changed )
        m_rotation = vaQuaternion::RotationAxis( raxis, rangle * VA_PIf / 180.0f );
        */

    string parentName = (m_parent == nullptr)?("none"):(m_parent->GetName().c_str() );
    ImGui::Text( "Parent: %s", parentName.c_str() );
    ImGui::Text( "Child object count: %d", m_children.size() );

    ImGui::Text( "Render mesh count: %d", m_renderMeshes.size() );
    if( ImGui::BeginChild( "rendermeshlist", { 0, ImGui::GetFontSize() * 8 }, true ) )
    {
        for( int meshIndex = 0; meshIndex < m_renderMeshes.size(); meshIndex++ )
        {
            vaGUID & meshID = m_renderMeshes[meshIndex];
            string prefix = vaStringTools::Format( "%03d - ", meshIndex );
            if( meshID == vaGUID::Null )
            {
                ImGui::Selectable( (prefix + "[null GUID]").c_str(), false, ImGuiSelectableFlags_Disabled );
                continue;
            }

            auto renderMesh = GetRenderMesh( meshIndex );
            if( renderMesh == nullptr ) // maybe still loading
            {
                ImGui::Selectable( ( prefix + "[not found]" ).c_str( ), false, ImGuiSelectableFlags_Disabled );
                continue;
            }

            vaAsset * assetPtr = renderMesh->GetParentAsset();
            if( assetPtr == nullptr )
            {
                ImGui::Selectable( ( prefix + "[not an asset]" ).c_str( ), false, ImGuiSelectableFlags_Disabled );
                continue;
            }
            shared_ptr<vaAsset> asset = assetPtr->GetSharedPtr( );
            if( asset == nullptr )
            {
                ImGui::Selectable( ( prefix + "[error acquiring shared_ptr]" ).c_str( ), false, ImGuiSelectableFlags_Disabled );
                continue;
            }
            
            if( ImGui::Selectable( (prefix + assetPtr->Name( )).c_str( ), false, ImGuiSelectableFlags_AllowDoubleClick ) )
            {
                if( ImGui::IsMouseDoubleClicked( 0 ) )
                    vaUIManager::GetInstance( ).SelectPropertyItem( asset );
            }
            
            if( asset->GetResource( ) != nullptr && ImGui::IsItemHovered( ) )
                asset->GetResource( )->SetUIShowSelectedFrameIndex( asset->GetAssetPack().GetRenderDevice().GetCurrentFrameIndex( ) + 1 );
        }
    }
    ImGui::EndChild( );

#endif
}

int vaSceneObject::SplitMeshes( const std::vector<vaPlane> & splitPlanes, std::vector<shared_ptr<vaRenderMesh>> & outOldMeshes, std::vector<shared_ptr<vaRenderMesh>> & outNewMeshes, const int minTriangleCountThreshold, const std::vector<shared_ptr<vaRenderMesh>> & candidateMeshes, bool splitIfIntersectingTriangles )
{
    int totalSplitCount = 0;
    int splitCount;

    vaMatrix4x4 worldTransform = GetWorldTransform();
    std::vector<uint32> newIndicesLeft, newIndicesRight;

    do 
    {
        splitCount = 0;
        for( int meshIndex = 0; meshIndex < m_renderMeshes.size( ); meshIndex++ )
        {
            auto renderMesh = GetRenderMesh(meshIndex);
            assert( renderMesh != nullptr ); // still loading?
            if( renderMesh == nullptr )
                continue;

            // if has candidate filter, use that but also consider any newly created a candidate because they must have inherited the original one
            if( candidateMeshes.size() > 0 )
            {
                bool found = false;
                for( int cmi = 0; cmi < candidateMeshes.size() && !found; cmi++ )
                    found |= candidateMeshes[cmi] == renderMesh;
                for( int cmi = 0; cmi < outNewMeshes.size() && !found; cmi++ )
                    found |= outNewMeshes[cmi] == renderMesh;
                if( !found )
                    continue;
            }

            const std::vector<vaRenderMesh::StandardVertex> & vertices   = renderMesh->Vertices();
            const std::vector<uint32> & indices                          = renderMesh->Indices();

            for( int planeIndex = 0; planeIndex < splitPlanes.size(); planeIndex++ )
            {
                const vaPlane & plane = splitPlanes[planeIndex];
                newIndicesLeft.clear();
                newIndicesRight.clear();

                for( int triIndex = 0; triIndex < indices.size( ); triIndex += 3 )
                {
                    const vaRenderMesh::StandardVertex & a = vertices[ indices[ triIndex + 0 ] ];
                    const vaRenderMesh::StandardVertex & b = vertices[ indices[ triIndex + 1 ] ];
                    const vaRenderMesh::StandardVertex & c = vertices[ indices[ triIndex + 2 ] ];
                    vaVector3 posA = vaVector3::TransformCoord( a.Position, worldTransform );
                    vaVector3 posB = vaVector3::TransformCoord( b.Position, worldTransform );
                    vaVector3 posC = vaVector3::TransformCoord( c.Position, worldTransform );
                    
                    float signA = vaMath::Sign( plane.DotCoord( posA ) );
                    float signB = vaMath::Sign( plane.DotCoord( posB ) );
                    float signC = vaMath::Sign( plane.DotCoord( posC ) );

                    int direction = 0;  // 0 means no split, -1 means goes to the left, 1 means goes to the right

                    // must all be on one or the other side, otherwise don't split
                    if( !splitIfIntersectingTriangles )
                    {
                        if( signA != signB || signA != signC )
                            direction = 0;
                        else if( signA < 0 && signB < 0 && signC < 0 )
                            direction = -1;
                        else
                        {
                            assert( signA >= 0 && signB >= 0 && signC >= 0 );
                            direction = 1;
                        }
                    }
                    else // will split towards the one with the most
                    {
                        direction = ((signA+signB+signC+1)<0)?(-1):(1);
                    }

                    if( direction == 0 )
                    {
                        //vaDebugCanvas3D::GetInstance().DrawTriangle( posA, posB, posC, 0xFFFF0000, 0x80FF0000 );
                        newIndicesLeft.clear();
                        newIndicesRight.clear();
                        break;
                    }
                    else if( direction == -1 )
                    {
                        //vaDebugCanvas3D::GetInstance().DrawTriangle( posA, posB, posC, 0xFF00FF00, 0x8000FF00 );
                        newIndicesLeft.push_back( indices[ triIndex + 0 ] );
                        newIndicesLeft.push_back( indices[ triIndex + 1 ] );
                        newIndicesLeft.push_back( indices[ triIndex + 2 ] );
                    }
                    else
                    {
                        assert( direction == 1 );

                        //vaDebugCanvas3D::GetInstance().DrawTriangle( posA, posB, posC, 0xFF0000FF, 0x800000FF );
                        newIndicesRight.push_back( indices[ triIndex + 0 ] );
                        newIndicesRight.push_back( indices[ triIndex + 1 ] );
                        newIndicesRight.push_back( indices[ triIndex + 2 ] );
                    }
                }
                if( newIndicesLeft.size( ) > (minTriangleCountThreshold*3) && newIndicesRight.size( ) > (minTriangleCountThreshold*3) )
                {
                    vaAsset * originalRenderMeshAsset = renderMesh->GetParentAsset();

                    VA_LOG( "Splitting mesh '%s' (%d triangles) with a plane %d into left part with %d triangles and right part with %d triangles",
                        originalRenderMeshAsset->Name().c_str(), (int)renderMesh->Indices().size()/3, planeIndex, (int)newIndicesLeft.size()/3, (int)newIndicesRight.size()/3 );

                    // do the split!
                    shared_ptr<vaRenderMesh::StandardTriangleMesh> newMeshLeft    = std::make_shared<vaRenderMesh::StandardTriangleMesh>( renderMesh->GetRenderDevice() );
                    shared_ptr<vaRenderMesh::StandardTriangleMesh> newMeshRight   = std::make_shared<vaRenderMesh::StandardTriangleMesh>( renderMesh->GetRenderDevice() );
                    
                    // fill up our 'left' and 'right' meshes
                    for( int triIndex = 0; triIndex < newIndicesLeft.size( ); triIndex += 3 )
                    {
                        const vaRenderMesh::StandardVertex & a = vertices[ newIndicesLeft[ triIndex + 0 ] ];
                        const vaRenderMesh::StandardVertex & b = vertices[ newIndicesLeft[ triIndex + 1 ] ];
                        const vaRenderMesh::StandardVertex & c = vertices[ newIndicesLeft[ triIndex + 2 ] ];
                        newMeshLeft->AddTriangleMergeDuplicates<vaRenderMesh::StandardVertex>( a, b, c, vaRenderMesh::StandardVertex::IsDuplicate, 512 );
                    }
                    for( int triIndex = 0; triIndex < newIndicesRight.size( ); triIndex += 3 )
                    {
                        const vaRenderMesh::StandardVertex & a = vertices[ newIndicesRight[ triIndex + 0 ] ];
                        const vaRenderMesh::StandardVertex & b = vertices[ newIndicesRight[ triIndex + 1 ] ];
                        const vaRenderMesh::StandardVertex & c = vertices[ newIndicesRight[ triIndex + 2 ] ];
                        newMeshRight->AddTriangleMergeDuplicates<vaRenderMesh::StandardVertex>( a, b, c, vaRenderMesh::StandardVertex::IsDuplicate, 512 );
                    }

                    // replace the current mesh with the left and the right split parts

                    // increment the counter
                    splitCount++;

                    // create new meshes
                    shared_ptr<vaRenderMesh> newRenderMeshLeft  = vaRenderMesh::Create( newMeshLeft, renderMesh->GetFrontFaceWindingOrder(), renderMesh->GetMaterialID() );
                    shared_ptr<vaRenderMesh> newRenderMeshRight = vaRenderMesh::Create( newMeshRight, renderMesh->GetFrontFaceWindingOrder(), renderMesh->GetMaterialID() );

                    // add them to the original asset pack so they can get saved
                    originalRenderMeshAsset->GetAssetPack().Add( newRenderMeshLeft, originalRenderMeshAsset->Name() + "_l", true );
                    originalRenderMeshAsset->GetAssetPack().Add( newRenderMeshRight, originalRenderMeshAsset->Name() + "_r", true );
                    
                    // for external tracking (& removal if needed)
                    outOldMeshes.push_back( renderMesh );
                    outNewMeshes.push_back( newRenderMeshLeft );
                    outNewMeshes.push_back( newRenderMeshRight );

                    // remove the current one and add the splits
                    RemoveRenderMeshRef( meshIndex );
                    AddRenderMeshRef( newRenderMeshLeft );
                    AddRenderMeshRef( newRenderMeshRight );
                    
                    // ok now reset the search to the beginning so we can re-do the split meshes if there's more splitting to be done
                    meshIndex--;
                    break;
                }
            }
        }

        totalSplitCount += splitCount;
    } while ( splitCount > 0 );

    return totalSplitCount;
}

void vaSceneObject::ReplaceMaterialsWithXXX( )
{
    string prefix = "xxx_";

    for( int i = 0; i < m_renderMeshes.size( ); i++ )
    {
        auto renderMesh = GetRenderMesh( i );
        auto renderMaterial = (renderMesh != nullptr)?(renderMesh->GetMaterial()):(nullptr);
        vaAsset * renderMaterialAsset = (renderMaterial != nullptr)?(renderMaterial->GetParentAsset()):(nullptr);
        if( renderMaterialAsset != nullptr )
        {
            string name = renderMaterialAsset->Name();
            if( name.substr(0, prefix.length()) == prefix )
            {
                VA_LOG( "Skipping material '%s'...", name.c_str() );
                continue;
            }
            VA_LOG( "Searching for material replacement for '%s'...", name.c_str() );
            vaAssetPackManager & mgr = renderMaterial->GetRenderDevice().GetAssetPackManager( );
            
            shared_ptr<vaAsset> replacementAsset = mgr.FindAsset( prefix+name );
            if( replacementAsset == nullptr )
            {
                VA_LOG_WARNING( "   replacement not found!" );
                continue;
            }
            if( replacementAsset->Type != vaAssetType::RenderMaterial )
            {
                VA_LOG_WARNING( "   replacement found but wrong type!" );
                continue;
            }
            VA_LOG_SUCCESS("   replacement found!" );
            renderMesh->SetMaterial( replacementAsset->GetResource<vaRenderMaterial>() );
        }
        else
        {
            assert( false );
        }
    }
}

void vaSceneObject::EnumerateUsedAssets( const std::function<void(vaAsset * asset)> & callback )
{
    for( int i = 0; i < m_renderMeshes.size( ); i++ )
    {
        auto renderMesh = GetRenderMesh( i );
        if( renderMesh != nullptr && renderMesh->GetParentAsset( ) != nullptr )
            renderMesh->EnumerateUsedAssets( callback );
    }
}


vaSceneOld::vaSceneOld( ) : vaUIPanel( "SceneOld", 1, true, vaUIPanel::DockLocation::DockedLeft, "SceneOlds" )
{
    Clear();
}

vaSceneOld::vaSceneOld( const string & name ) : vaUIPanel( "Scene", 1, true, vaUIPanel::DockLocation::DockedLeft, "Scenes"  )
{
    Clear( );
    m_name = name;
}

vaSceneOld::~vaSceneOld( )
{
    // this might be ok or might not be ok - you've got some 
    assert( m_deferredObjectActions.size() == 0 );

    Clear();
}

void vaSceneOld::Clear( )
{
    m_deferredObjectActions.clear();
    DestroyAllObjects( );
    ApplyDeferredObjectActions();
    m_name  = "";
    m_lights.clear();
    assert( m_allObjects.size() == 0 );
    assert( m_rootObjects.size() == 0 );

    m_tickIndex = 0;
    m_sceneTime = 0.0;

    assert( !m_isInTick );
    m_isInTick = false;
}

shared_ptr<vaSceneObject> vaSceneOld::CreateObject( const shared_ptr<vaSceneObject> & parent )
{
    return CreateObject( "Unnamed", vaMatrix4x4::Identity, parent );
}

shared_ptr<vaSceneObject> vaSceneOld::CreateObject( const string & name, const vaMatrix4x4 & localTransform, const shared_ptr<vaSceneObject> & parent )
{
    m_deferredObjectActions.push_back( DeferredObjectAction( std::make_shared<vaSceneObject>( ), parent, vaSceneOld::DeferredObjectAction::AddObject ) );

    m_deferredObjectActions.back().Object->SetName( name );
    m_deferredObjectActions.back().Object->SetScene( this->shared_from_this() );
    m_deferredObjectActions.back().Object->SetLocalTransform( localTransform );

    return m_deferredObjectActions.back().Object;
}

shared_ptr<vaSceneObject> vaSceneOld::CreateObject( const string & name, const vaVector3 & localScale, const vaQuaternion & localRot, const vaVector3 & localPos, const shared_ptr<vaSceneObject> & parent )
{
    return CreateObject( name, vaMatrix4x4::FromScaleRotationTranslation( localScale, localRot, localPos ), parent );
}

void vaSceneOld::DestroyObject( const shared_ptr<vaSceneObject> & ptr, bool destroyChildrenRecursively, bool applyOwnTransformToChildren )
{
    assert( ptr->GetScene() != nullptr );
    if( applyOwnTransformToChildren )
    {
        // doesn't make sense to apply transform to children if you're destroying them as well
        assert( !destroyChildrenRecursively );
    }

    // mark the object as no longer functional
    ptr->MarkAsDestroyed( destroyChildrenRecursively );

    if( destroyChildrenRecursively )
    {
        m_deferredObjectActions.push_back( DeferredObjectAction( ptr, nullptr, vaSceneOld::DeferredObjectAction::RemoveObjectAndChildren ) );
    }
    else
    {
        // we can do this right now
        if( applyOwnTransformToChildren )
        {
            const vaMatrix4x4 & parentTransform = ptr->GetLocalTransform();
            const std::vector<shared_ptr<vaSceneObject>> & children = ptr->GetChildren();
            for( int i = 0; i < (int)children.size(); i++ )
            {
                const vaMatrix4x4 newTransform = parentTransform * children[i]->GetLocalTransform();
                children[i]->SetLocalTransform( newTransform );
            }
        }
        m_deferredObjectActions.push_back( DeferredObjectAction( ptr, nullptr, vaSceneOld::DeferredObjectAction::RemoveObject ) );
    }

    //assert( m_deferredObjectActions.back().Object.unique() );
}

void vaSceneOld::DestroyAllObjects( )
{
    m_deferredObjectActions.push_back( DeferredObjectAction( nullptr, nullptr, vaSceneOld::DeferredObjectAction::RemoveAllObjects ) );
}

void vaSceneOld::RegisterRootObjectAdded( const shared_ptr<vaSceneObject> & object )
{
    if( m_isInTick )
    {
        assert( false );    // Can't call this while in tick
        return;
    }

    assert( std::find( m_rootObjects.begin(), m_rootObjects.end(), object ) == m_rootObjects.end() );
    m_rootObjects.push_back( object );
}

void vaSceneOld::RegisterRootObjectRemoved( const shared_ptr<vaSceneObject> & object )
{
    if( m_isInTick )
    {
        assert( false );    // Can't call this while in tick
        return;
    }

    bool allOk = vector_find_and_remove( m_rootObjects, object ) != -1;
    assert( allOk ); // if this fires, there's a serious error somewhere!
    allOk;
}

void vaSceneOld::ApplyDeferredObjectActions( )
{
    if( m_isInTick )
    {
        assert( false );    // Can't call this while in tick
        return;
    }

    for( int i = 0; i < m_deferredObjectActions.size( ); i++ )
    {
        const DeferredObjectAction & mod = m_deferredObjectActions[i];

        if( mod.Action == vaSceneOld::DeferredObjectAction::RemoveAllObjects )
        {
            for( int j = 0; j < (int)m_allObjects.size(); j++ )
                m_allObjects[j]->NukeGraph( );
            m_rootObjects.clear();

            for( int j = 0; j < m_allObjects.size( ); j++ )
            {
                // this pointer should be unique now. otherwise you're holding a pointer somewhere although the object was destroyed - is this
                // intended? if there's a case where this is intended, think it through in detail - there's a lot of potential for unforeseen 
                // consequences from accessing vaSceneObject-s in this state.
                assert( m_allObjects[i].use_count() == 1 ); // this assert is not entirely correct for multithreaded scenarios so beware
            }
            m_allObjects.clear();
        }
        else if( mod.Action == vaSceneOld::DeferredObjectAction::AddObject )
        {
            // by default in root, will be removed on "add parent" - this can be optimized out if desired (no need to call if parent != nullptr) but 
            // it leaves the object in a broken state temporarily and SetParent needs to get adjusted and maybe something else too
            m_rootObjects.push_back( mod.Object );

            mod.Object->SetParent( mod.ParentObject );

            m_allObjects.push_back( mod.Object );

            mod.Object->SetAddedToScene();
        }
        else if( mod.Action == vaSceneOld::DeferredObjectAction::RemoveObject || mod.Action == vaSceneOld::DeferredObjectAction::RemoveObjectAndChildren )
        {
            bool destroyChildrenRecursively = mod.Action == vaSceneOld::DeferredObjectAction::RemoveObjectAndChildren;
            // they should automatically remove themselves from the list as they get deleted or reattached to this object's parent
            if( !destroyChildrenRecursively )
            {
                const std::vector<shared_ptr<vaSceneObject>> & children = mod.Object->GetChildren();

                while( children.size() > 0 )
                {
                    // we >have< to make this a temp, as the SetParent modifies the containing array!!
                    shared_ptr<vaSceneObject> temp = children[0];
                    assert( this->shared_from_this() == temp->GetScene() );
                    temp->SetParent( mod.Object->GetParent() );
                }
            }
            DestroyObjectImmediate( mod.Object, destroyChildrenRecursively );
        }
    }

    m_deferredObjectActions.clear();
}

void vaSceneOld::DestroyObjectImmediate( const shared_ptr<vaSceneObject> & obj, bool recursive )
{
    assert( obj->IsDestroyed() );           // it must have already been destroyed
    //assert( obj->GetScene() == nullptr );   // it must have been already set to null
    obj->SetParent( nullptr );

    obj->SetScene( nullptr );

    bool allOk = vector_find_and_remove( m_rootObjects, obj ) != -1;
    assert( allOk );
    allOk = vector_find_and_remove( m_allObjects, obj ) != -1;
    assert( allOk );

    if( recursive )
    {
        const std::vector<shared_ptr<vaSceneObject>> & children = obj->GetChildren();
        while( obj->GetChildren().size( ) > 0 )
        {
            // we >have< to make this a temp, as the DestroyObject modifies both the provided reference and the containing array!!
            shared_ptr<vaSceneObject> temp = children[0];
            DestroyObjectImmediate( temp, true );
        }
    }
    
    // this pointer should be unique now. otherwise you're holding a pointer somewhere although the object was destroyed - is this
    // intended? if there's a case where this is intended, think it through in detail - there's a lot of potential for unforeseen 
    // consequences from accessing vaSceneObject-s in this state.
    assert( obj.use_count() == 1 ); // this assert is not entirely correct for multithreaded scenarios so beware
}

void vaSceneOld::Tick( float deltaTime )
{
    VA_TRACE_CPU_SCOPE( vaScene_Tick );
    ApplyDeferredObjectActions();

    if( deltaTime > 0 )
    {
        m_sceneTime += deltaTime;

        assert( !m_isInTick );
        m_isInTick = true;
        m_tickIndex++;
        for( int i = 0; i < m_rootObjects.size( ); i++ )
        {
            m_rootObjects[i]->TickRecursive( *this, deltaTime );
        }
        assert( m_isInTick );
        m_isInTick = false;

        ApplyDeferredObjectActions();

        m_UI_MouseClickIndicatorRemainingTime = vaMath::Max( m_UI_MouseClickIndicatorRemainingTime - deltaTime, 0.0f );
    }
}

void vaSceneOld::ApplyToLighting( vaSceneLighting & lighting )
{
    VA_TRACE_CPU_SCOPE( vaScene_ApplyToLighting );
    lighting.SetLights( m_lights );
    // lighting.SetEnvmap( m_envmapTexture, m_envmapRotation, m_envmapColorMultiplier );
    lighting.FogSettings() = m_fog;
}

bool vaSceneOld::SerializeObjectsRecursive( vaXMLSerializer & serializer, const string & name, std::vector<shared_ptr<vaSceneObject>> & objectList, const shared_ptr<vaSceneObject> & parent )
{
    if( m_isInTick )
    {
        assert( false );    // Can't call this while in tick
        return false;
    }

    VERIFY_TRUE_RETURN_ON_FALSE( serializer.SerializeOpenChildElement( name.c_str() ) );

    uint32 count = (uint32)objectList.size();
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<uint32>( "count", count ) );

    if( serializer.IsReading() )
    {
        assert( objectList.size() == 0 );
        // objectList.resize( count );
    }

    string elementName;
    for( uint32 i = 0; i < count; i++ )
    {
        elementName = vaStringTools::Format( "%s_%d", name.c_str(), i );
        VERIFY_TRUE_RETURN_ON_FALSE( serializer.SerializeOpenChildElement( elementName.c_str() ) );

        shared_ptr<vaSceneObject> obj = nullptr;
        if( serializer.IsReading() )
            obj = CreateObject( parent );
        else
            obj = objectList[i];

        obj->Serialize( serializer );

        VERIFY_TRUE_RETURN_ON_FALSE( serializer.SerializePopToParentElement( elementName.c_str() ) );
    }

    // if( serializer.IsReading() )
    // {
    //     // should have count additions
    //     // assert( m_deferredObjectActions.size() == count );
    // }

    VERIFY_TRUE_RETURN_ON_FALSE( serializer.SerializePopToParentElement( name.c_str() ) );

    return true;
}

bool vaSceneOld::Serialize( vaXMLSerializer & serializer, bool mergeToExistingIfLoading )
{
    if( m_isInTick )
    {
        assert( false );    // Can't call this while in tick
        return false;
    }

    if( serializer.IsWriting( ) && mergeToExistingIfLoading )
    {
        assert( false ); // this combination makes no sense
        mergeToExistingIfLoading = false;
    }

    // calling Serialize with non-applied changes? probably a bug somewhere (if not, just call ApplyDeferredObjectActions() before getting here)
    assert( m_deferredObjectActions.size() == 0 );
    if( m_deferredObjectActions.size() != 0 )
        return false;

    if( serializer.SerializeOpenChildElement( "VanillaScene" ) )
    {
        if( serializer.IsReading() && !mergeToExistingIfLoading )
            Clear();

        string mergingName;
        if( mergeToExistingIfLoading )
        {
            VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<string>( "Name", mergingName ) );
            //vaVector3 mergingAmbientLight;
            //VERIFY_TRUE_RETURN_ON_FALSE( serializer.OldSerializeValue( "AmbientLight", mergingAmbientLight ) );
            std::vector<shared_ptr<vaLight>> mergingLights;
            
            if( serializer.GetVersion() > 0 )
                VERIFY_TRUE_RETURN_ON_FALSE( serializer.SerializeArray( "Lights", mergingLights ) );
            else
                { assert( false ); } 
            
            m_lights.insert( m_lights.end(), mergingLights.begin(), mergingLights.end() );

            //serializer.Serialize<string>( "DistantIBLPath", m_IBLProbeDistantImagePath );
        }
        else
        {
            VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<string>( "Name", m_name ) );
            /*VERIFY_TRUE_RETURN_ON_FALSE(*/ serializer.Serialize<vaGUID>( "UID", m_UID, vaGUID::Create( ) );// );
            //VERIFY_TRUE_RETURN_ON_FALSE( serializer.OldSerializeValue( "AmbientLight", m_lightAmbient ) );
            if( serializer.GetVersion() > 0 )
                VERIFY_TRUE_RETURN_ON_FALSE( serializer.SerializeArray( "Lights", m_lights ) );
            else
                { assert( false ); } 
            //serializer.SerializeArray( "Inputs", "Item", m_lights );

            //serializer.Serialize<string>( "DistantIBLPath", m_IBLProbeDistantImagePath );
        }

        VERIFY_TRUE_RETURN_ON_FALSE( SerializeObjectsRecursive( serializer, "RootObjects", m_rootObjects, nullptr ) );

        VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaXMLSerializable>( "FogSphere", m_fog ) );
        ///*VERIFY_TRUE_RETURN_ON_FALSE(*/ m_fog.Serialize( serializer );

        /*VERIFY_TRUE_RETURN_ON_FALSE*/( serializer.Serialize( "IBLProbeLocal", m_IBLProbeLocal ) );
        /*VERIFY_TRUE_RETURN_ON_FALSE*/( serializer.Serialize( "IBLProbeDistant", m_IBLProbeDistant ) );
        // serializer.Serialize<string>( "IBLProbeDistantImagePath", m_IBLProbeDistantImagePath );

        bool ok = serializer.SerializePopToParentElement( "VanillaScene" );
        assert( ok ); 

        if( serializer.IsReading() && ok )
        {
            return PostLoadInit();
        }

        return ok;
    }
    else
    {
        VA_LOG_WARNING( L"Unable to load scene, unexpected contents." );
    }

    return false;
}

bool vaSceneOld::PostLoadInit( )
{
    ApplyDeferredObjectActions( );

    return true;
}

// void vaSceneOld::UpdateUsedPackNames( )
// {
//     m_assetPackNames.clear();
//     for( auto sceneObject : m_allObjects )
//     {
//         sceneObject->RegisterUsedAssetPacks( std::bind( &vaSceneOld::RegisterUsedAssetPackNameCallback, this, std::placeholders::_1 ) );
//     }
// }

void vaSceneOld::RegisterUsedAssetPackNameCallback( const vaAssetPack & assetPack )
{
    for( size_t i = 0; i < m_assetPackNames.size(); i++ )
    {
        if( vaStringTools::ToLower( m_assetPackNames[i] ) == vaStringTools::ToLower( assetPack.GetName() ) )
        {
            // found, all is good
            return;
        }
    }
    // not found, add to list
    m_assetPackNames.push_back( assetPack.GetName() );
}

void vaSceneOld::RemoveAllUnusedAssets( const std::vector<shared_ptr<vaAssetPack>>& assetPacks )
{
    std::set<vaAsset*> allAssets;
    for( size_t i = 0; i < assetPacks.size(); i++ )
    {
        vaAssetPack & pack = *assetPacks[i];
        assert( !pack.IsBackgroundTaskActive( ) );
        std::unique_lock<mutex> assetStorageMutexLock( pack.GetAssetStorageMutex( ) );

        for( size_t j = 0; j < pack.Count( false ); j++ )
            allAssets.insert( pack.AssetAt( j, false ).get() );
    }

    auto removeFromSet = [ &allAssets ]( vaAsset* asset )
    {
        if( asset != nullptr )
            allAssets.erase( asset );
    };

    for( auto sceneObject : m_allObjects )
        sceneObject->EnumerateUsedAssets( removeFromSet );

    for( auto asset : allAssets )
        asset->GetAssetPack().Remove( asset, true );
}


// void vaSceneOld::LoadAndConnectAssets( )
// {
// }

#ifdef VA_IMGUI_INTEGRATION_ENABLED

static shared_ptr<vaSceneObject> ImGuiDisplaySceneObjectTreeRecursive( const std::vector<shared_ptr<vaSceneObject>> & elements, const shared_ptr<vaSceneObject> & selectedObject )
{
    ImGuiTreeNodeFlags defaultFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;

    shared_ptr<vaSceneObject> ret = nullptr;

    for( int i = 0; i < elements.size(); i++ )
    {
        ImGuiTreeNodeFlags nodeFlags = defaultFlags | ((elements[i] == selectedObject)?(ImGuiTreeNodeFlags_Selected):(0)) | ((elements[i]->GetChildren().size() == 0)?(ImGuiTreeNodeFlags_Leaf):(0));
        bool nodeOpen = ImGui::TreeNodeEx( elements[i]->UIPropertiesItemGetUniqueID().c_str(), nodeFlags, elements[i]->GetName().c_str() );
                
        if( ImGui::IsItemClicked() )
        {
            ret = elements[i];
        }

        if( nodeOpen )
        {
            if( elements[i]->GetChildren().size() > 0 )
            {
                shared_ptr<vaSceneObject> retRec = ImGuiDisplaySceneObjectTreeRecursive( elements[i]->GetChildren(), selectedObject );
                if( retRec != nullptr )
                    ret = retRec;
            }

            ImGui::TreePop();
        }
    }
    return ret;
}
#endif

bool vaSceneOld::Save( const wstring & fileName )
{
    // calling Save with non-applied changes? probably a bug somewhere (if not, just call ApplyDeferredObjectActions() before getting here)
    assert( m_deferredObjectActions.size() == 0 );

    vaFileStream fileOut;
    if( fileOut.Open( fileName, FileCreationMode::OpenOrCreate, FileAccessMode::Write ) )
    {
        vaXMLSerializer serializer;

        VA_LOG( L"Writing '%s'.", fileName.c_str() );

        if( !Serialize( serializer, false ) )
        {
            VA_LOG_WARNING( L"Error while serializing the scene" );
            fileOut.Close();
            return false;
        }

        serializer.WriterSaveToFile( fileOut );
        fileOut.Close();
        VA_LOG( L"Scene successfully saved to '%s'.", fileName.c_str() );
        return true;
    }
    else
    {
        VA_LOG_WARNING( L"Unable to save scene to '%s', file error.", fileName.c_str() );
        return false;
    }
}

bool vaSceneOld::Load( const wstring & fileName, bool mergeToExisting )
{
    // calling Load with non-applied changes? probably a bug somewhere (if not, just call ApplyDeferredObjectActions() before getting here)
    assert( m_deferredObjectActions.size() == 0 );

    if( m_isInTick )
    {
        assert( false );    // Can't call this while in tick
        return false;
    }

    vaFileStream fileIn;
    if( vaFileTools::FileExists( fileName ) && fileIn.Open( fileName, FileCreationMode::Open ) )
    {
        vaXMLSerializer serializer( fileIn );
        fileIn.Close();

        if( serializer.IsReading( ) )
        {
            VA_LOG( L"Reading '%s'.", fileName.c_str() );

            if( !Serialize( serializer, mergeToExisting ) )
            {
                VA_LOG_WARNING( L"Error while serializing the scene" );
                fileIn.Close();
                return false;
            }
            VA_LOG( L"Scene successfully loaded from '%s', running PostLoadInit()", fileName.c_str() );
            return true;
        }
        else
        { 
            VA_LOG_WARNING( L"Unable to parse xml file '%s', file error.", fileName.c_str() );
            return false;
        }
    }
    else
    {
        VA_LOG_WARNING( L"Unable to load scene from '%s', file error.", fileName.c_str() );
        return false;
    }
}

void vaSceneOld::UIPanelTick( vaApplicationBase & application )
{
    application;
#ifdef VA_IMGUI_INTEGRATION_ENABLED

    ImGui::PushItemWidth( 200.0f );

    if( ImGui::Button( " Rename " ) )
        ImGuiEx_PopupInputStringBegin( "Rename scene", m_name );
    if( ImGuiEx_PopupInputStringTick( "Rename scene", m_name ) )
    {
        //m_name = vaStringTools::ToLower( m_name );
        VA_LOG( "Scene name changed to '%s'", m_name.c_str() );
    }

    ImGui::SameLine();

    if( ImGui::Button( " Delete all contents " ) )
    {
        Clear();
        return;
    }

    if( ImGui::Button( " Save As... " ) )
    {
        wstring fileName = vaFileTools::SaveFileDialog( L"", vaCore::GetExecutableDirectory(), L".xml scene files\0*.xml\0\0" );
        if( fileName != L"" )
        {
            if( vaFileTools::SplitPathExt( fileName ) == L"" ) // if no extension, add .xml
                fileName += L".xml";
            Save( fileName );
        }
    }

    ImGui::SameLine();
    if( ImGui::Button( " Load... " ) )
    {
        wstring fileName = vaFileTools::OpenFileDialog( L"", vaCore::GetExecutableDirectory(), L".xml scene files\0*.xml\0\0" );
        Load( fileName, false );
    }
    ImGui::SameLine();
    if( ImGui::Button( " Load and merge... " ) )
    {
        wstring fileName = vaFileTools::OpenFileDialog( L"", vaCore::GetExecutableDirectory(), L".xml scene files\0*.xml\0\0" );
        Load( fileName, true );
    }

    ImGui::Separator();

#if 0
    if( ImGui::Button( " Replace materials with xxx_" ) )
    {
        for( auto sceneObject : m_allObjects )
            sceneObject->ReplaceMaterialsWithXXX();
    }

    if( ImGui::Button( " Remove all assets from all asset packs not used by this scene" ) )
    {
        RemoveAllUnusedAssets( application.GetRenderDevice().GetAssetPackManager().GetAllAssetPacks() );
    }

    ImGui::Separator( );
#endif

    if( m_allObjects.size() > 0 )
    {
        ImGui::Text( "Scene objects: %d", m_allObjects.size() );

        float uiListHeight = 120.0f;
        float uiPropertiesHeight = 180.0f;

        shared_ptr<vaSceneObject> selectedObject = m_UI_SelectedObject.lock();

        if( m_UI_ShowObjectsAsTree )
        {
            std::vector<shared_ptr<vaSceneObject>> elements = m_rootObjects;
            if( ImGui::BeginChild( "TreeFrame", ImVec2( 0.0f, uiListHeight ), true ) )
            {
                shared_ptr<vaSceneObject> objectClicked = ImGuiDisplaySceneObjectTreeRecursive( m_rootObjects, selectedObject );
                if( objectClicked != nullptr )
                {
                    if( selectedObject != objectClicked )
                        selectedObject = objectClicked;
                    else
                        selectedObject = nullptr;
                    m_UI_SelectedObject = selectedObject;
                }
            }
            ImGui::EndChild();

            if( ImGui::BeginChild( "PropFrame", ImVec2( 0.0f, uiPropertiesHeight ), true ) )
            {
                if( selectedObject != nullptr )
                {
                    ImGui::PushID( selectedObject->UIPropertiesItemGetUniqueID( ).c_str( ) );
                    selectedObject->UIPropertiesItemTick( application, false, false );
                    ImGui::PopID();
                }
                else
                {
                    ImGui::TextColored( ImVec4( 0.5f, 0.5f, 0.5f, 1.0f ), "Select an item to display properties" );
                }
            }
            ImGui::EndChild();
        }
        else
        {
            vaUIPropertiesItem * ptrsToDisplay[ 65536 ]; // if this ever becomes not enough, change DrawList into a template and make it accept allObjects directly...
            int countToShow = std::min( (int)m_allObjects.size(), (int)_countof(ptrsToDisplay) );
        
            int currentObject = -1;
            for( int i = 0; i < countToShow; i++ ) 
            {
                if( m_UI_SelectedObject.lock() == m_allObjects[i] )
                    currentObject = i;
                ptrsToDisplay[i] = m_allObjects[i].get();
            }

            vaUIPropertiesItem::DrawList( application, "Objects", ptrsToDisplay, countToShow, currentObject, 0.0f, uiListHeight, uiPropertiesHeight );

            if( currentObject >= 0 && currentObject < countToShow )
                m_UI_SelectedObject = m_allObjects[currentObject];
            else
                m_UI_SelectedObject.reset();
        }
    }
    else
    {
        ImGui::Text( "No objects" );
    }

    ImGui::Separator();

    ImGui::Text( "Scene lights: %d", m_lights.size() );

    if( m_lights.size() > 0 )
    {
        vaUIPropertiesItem * ptrsToDisplay[ 4096 ];
        int countToShow = std::min( (int)m_lights.size(), (int)_countof(ptrsToDisplay) );
        for( int i = 0; i < countToShow; i++ ) ptrsToDisplay[i] = m_lights[i].get();

        int currentLight = -1;
        for( int i = 0; i < countToShow; i++ ) 
        {
            if( m_UI_SelectedLight.lock() == m_lights[i] )
                currentLight = i;
            ptrsToDisplay[i] = m_lights[i].get();
        }

        vaUIPropertiesItem::DrawList( application, "Lights", ptrsToDisplay, countToShow, currentLight, 0.0f, 90, 200 );
        if( currentLight >= 0 && currentLight < countToShow )
            m_UI_SelectedLight = m_lights[currentLight];

        if( currentLight >= 0 && currentLight < m_lights.size() )
        {
            if( ImGui::Button( "Duplicate" ) )
            {
                m_lights.push_back( std::make_shared<vaLight>( *m_lights[currentLight] ) );
                m_lights.back()->Name += "_new";
                m_UI_SelectedLight = m_lights.back();
            }
            ImGui::SameLine();
            if( ImGui::Button( "Delete" ) )
            {
                m_lights.erase( m_lights.begin() + currentLight );
            }
        }
    }
    else
    {
        ImGui::Text( "No lights" );
    }
    if( ImGui::Button( "Add light" ) )
    {
        m_lights.push_back( std::make_shared<vaLight>( vaLight::MakePoint( "NewLight", 0.2f, vaVector3( 0, 0, 0 ), 0.0f, vaVector3( 0, 0, 0 ) ) ) );
    }
    ImGui::Checkbox( "Debug draw scene lights", &m_UI_ShowLights );
    
    ImGui::Separator();
    
    ImGui::Text( "Scene cleanup tools:" );

    if( ImGui::Button( "Remove redundant hierarchy" ) )
    {
        for( int i = 0; i < m_allObjects.size(); i++ )
        {
            const shared_ptr<vaSceneObject> & obj = m_allObjects[i];
            if( obj->GetRenderMeshCount( ) == 0 && obj->GetChildren().size() == 1 )
            {
                DestroyObject( obj, false, true );
            }
        }
    }
    if( ImGui::Button( "Remove missing render mesh references" ) )
    {
        for( int i = 0; i < m_allObjects.size( ); i++ )
        {
            const shared_ptr<vaSceneObject> & obj = m_allObjects[i];
            for( int j = obj->GetRenderMeshCount( ) - 1; j >= 0; j-- )
            {
                if( obj->GetRenderMesh( j ) == nullptr )
                {
                    obj->RemoveRenderMeshRef( j );
                    VA_LOG( "Removed mesh %d from scene object '%s'", j, obj->GetName().c_str() );
                }
            }
        }
    }

    m_fog.UIPropertiesItemTickCollapsable( application, false, false );

    ImGui::Separator( );

    // IBL properties
    {
        vaDebugCanvas3D& canvas3D = application.GetRenderDevice( ).GetCanvas3D( ); canvas3D;

        string idPrefix = m_UID.ToString( ) + "_IBL_";

        Scene::IBLProbe * probes[] = { &m_IBLProbeLocal, &m_IBLProbeDistant };
        string probeNames[]         = { "Local IBL", "Distant IBL" };
        for( int i = 0; i < countof( probes ); i++ )
        {
            Scene::IBLProbe & probe = *probes[i];
            const string & probeName = probeNames[i];

            ImGui::Text( probeName.c_str() );
            switch( ImGuiEx_SameLineSmallButtons( probeName.c_str(), { "[props]" } ) )
            {
            case( -1 ): break;
            case( 0 ):
            {
                string uniqueID = idPrefix + std::to_string(i);
                if( vaUIManager::GetInstance( ).FindTransientPropertyItem( uniqueID, true ) == nullptr )
                {
                    auto uiContext = std::make_shared<vaIBLProbe::UIContext>( shared_from_this( ) );

                    vaUIManager::GetInstance( ).CreateTransientPropertyItem( uniqueID, m_name + " : " + probeName,
                        [ &probe, uniqueID, probeName ]( vaApplicationBase& application, const shared_ptr<void>& drawContext ) -> bool
                    {
                        auto uiContext = std::static_pointer_cast<vaIBLProbe::UIContext>( drawContext ); assert( uiContext != nullptr );
                        auto aliveToken = ( uiContext != nullptr ) ? ( uiContext->AliveToken.lock( ) ) : ( shared_ptr<void>( ) );
                        if( !aliveToken )
                            return false;

                        vaDebugCanvas3D& canvas3D = application.GetRenderDevice( ).GetCanvas3D( ); canvas3D;

                        if( ImGui::InputText( "Input file", &probe.ImportFilePath ) )
                            probe.SetImportFilePath( probe.ImportFilePath, false );
                        ImGui::SameLine( );
                        if( ImGui::Button( "..." ) )
                        {
                            string fileName = vaFileTools::OpenFileDialog( probe.ImportFilePath, vaCore::GetExecutableDirectoryNarrow( ) );
                            if( fileName != "" )
                                probe.SetImportFilePath( probe.ImportFilePath, false );
                        }

                        ImGui::Separator();

                        // capture position
#if 0 // disabled due to changes in the MoveRotateScaleWidget tool
                        {
                            vaMatrix4x4 probeMat = vaMatrix4x4::FromTranslation( probe.Position );
                            bool mrsWidgetActive = vaUIManager::GetInstance( ).MoveRotateScaleWidget( uniqueID+"c", probeName + " [capture position]", probeMat );
                            if( mrsWidgetActive )
                            {
                                ImGui::Text( "<MRSWidget Active>" );
                                probe.Position = probeMat.GetTranslation( );
                                canvas3D.DrawSphere( probe.Position, 0.1f, 0xFF00FF00 );
                            }
                            else
                            {
                                vaVector3 pos = probe.Position;
                                if( ImGui::InputFloat3( "Translate", &pos.x, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue ) )
                                    probe.Position = pos;
                            }
                        }
#endif

                        ImGui::Checkbox( "Use OBB geometry proxy", &probe.UseGeometryProxy );

                        if( probe.UseGeometryProxy )
                        {
                            vaMatrix4x4 probeMat = probe.GeometryProxy.ToScaledTransform( );

                            // activate move rotate scale widget
#if 0 // disabled due to changes in the MoveRotateScaleWidget tool
                            bool mrsWidgetActive = vaUIManager::GetInstance( ).MoveRotateScaleWidget( uniqueID+"p", probeName + " [geometry proxy]", probeMat );
                            if( mrsWidgetActive )
                            {
                                ImGui::Text( "<MRSWidget Active>" );

                                probe.GeometryProxy = vaOrientedBoundingBox::FromScaledTransform( probeMat );
                                canvas3D.DrawBox( probe.GeometryProxy, 0xFF00FF00, 0x10808000 );
                            }
                            else
                            {
                                if( ImGuiEx_Transform( uniqueID.c_str( ), probeMat, false, false ) )
                                    probe.GeometryProxy = vaOrientedBoundingBox::FromScaledTransform( probeMat );
                            }
#endif

                            if( ImGui::Button( "Set capture center to geometry proxy center", {-1, 0} ) )
                                probe.Position = probe.GeometryProxy.Center;
                        }

                        if( ImGui::CollapsingHeader( "Local to Global transition region", ImGuiTreeNodeFlags_Framed /*| ImGuiTreeNodeFlags_DefaultOpen*/ ) )
                        {
                            vaMatrix4x4 transform = probe.FadeOutProxy.ToScaledTransform( );
#if 0 // disabled due to changes in the MoveRotateScaleWidget tool
                            bool mrsWidgetActive = vaUIManager::GetInstance( ).MoveRotateScaleWidget( uniqueID + "lgtr", "Local to global IBL transition region", transform );
                            if( mrsWidgetActive )
                            {
                                ImGui::Text( "<MRSWidget Active>" );

                                probe.FadeOutProxy = vaOrientedBoundingBox::FromScaledTransform( transform );
                                canvas3D.DrawBox( probe.FadeOutProxy, 0xFF00FF00, 0x10008080 );
                            }
                            else
                            {
                                if( ImGuiEx_Transform( ( uniqueID + "lgtr" ).c_str( ), transform, false, false ) )
                                    probe.FadeOutProxy = vaOrientedBoundingBox::FromScaledTransform( transform );
                            }
#endif
                        }

                        ImGui::Separator();

                        vaVector3 colorSRGB = vaVector3::LinearToSRGB( probe.AmbientColor );
                        if( ImGui::ColorEdit3( "Ambient Color", &colorSRGB.x, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float ) )
                            probe.AmbientColor = vaVector3::SRGBToLinear( colorSRGB );

                        ImGui::InputFloat( "Ambient Color Intensity", &probe.AmbientColorIntensity );


                        return true;
                    }, uiContext );
                }

            }; break;
            default: assert( false ); break;
            }

        }

        ImGui::Separator( );
    }

    DrawUI( application.GetUICamera( ), application.GetRenderDevice( ).GetCanvas2D( ), application.GetRenderDevice( ).GetCanvas3D( ) );

    ImGui::PopItemWidth( );
#endif
}

void vaSceneOld::DrawUI( const vaCameraBase & camera, vaDebugCanvas2D & canvas2D, vaDebugCanvas3D & canvas3D )
{
    VA_TRACE_CPU_SCOPE( vaScene_DrawUI );
    canvas2D;
    camera;

    float pulse = 0.5f * (float)vaMath::Sin( m_sceneTime * VA_PIf * 2.0f ) + 0.5f;

    auto selectedObject = m_UI_SelectedObject.lock();
    if( selectedObject != nullptr )
    {
        canvas3D.DrawBox( selectedObject->GetGlobalAABB(), vaVector4::ToBGRA( 0.0f, 0.0f + pulse, 0.0f, 1.0f ), vaVector4::ToBGRA( 0.5f, 0.5f, 0.5f, 0.1f ) );
    }

    if( m_UI_MouseClickIndicatorRemainingTime > 0 )
    {
        float factor = vaMath::Sin( (1.0f - m_UI_MouseClickIndicatorRemainingTime / m_UI_MouseClickIndicatorTotalTime) * VA_PIf );
        canvas3D.DrawSphere( m_UI_MouseClickIndicator, factor * 0.1f, 0xA0000000, 0x2000FF00 );
    }
    
    if( m_UI_ShowLights )
    {
        string idPrefix = m_UID.ToString( ) + "_Lights_";

        for( int i = 0; i < (int)m_lights.size( ); i++ )
        {
            if( m_lights[i]->Type == vaLight::Type::Ambient || m_lights[i]->Type == vaLight::Type::Directional )
                continue;

            string uniqueID = idPrefix + std::to_string( reinterpret_cast<uint64>( m_lights[i].get() ) );

            vaVector3 vecRight  = vaVector3::Cross( m_lights[i]->Up, m_lights[i]->Direction );
            if( vecRight.LengthSq() < VA_EPSf ) 
                vecRight = vaVector3::Cross( vaVector3( 0, 1, 0 ), m_lights[i]->Direction );
            if( vecRight.LengthSq( ) < VA_EPSf )
                vecRight = vaVector3::Cross( vaVector3( 1, 0, 0 ), m_lights[i]->Direction );

            vaVector3 vecUp     = vaVector3::Cross( m_lights[i]->Direction, vecRight ).Normalized( );
            vaMatrix3x3 lightRotation = vaMatrix3x3( m_lights[i]->Direction.Normalized(), vecRight, vecUp );
            vaMatrix4x4 lightTransform = vaMatrix4x4::FromRotationTranslation( lightRotation, m_lights[i]->Position );

#if 0 // disabled due to changes in the MoveRotateScaleWidget tool
            vaUIManager::GetInstance().MoveRotateScaleWidget( uniqueID, m_lights[i]->Name + "(light)", lightTransform );
#endif
        }

        for( int i = 0; i < (int)m_lights.size(); i++ )
        {
            const vaLight & light = *m_lights[i];

            vaVector4 sphereColor = vaVector4( 0.5f, 0.5f, 0.5f, 0.1f );
            vaVector4 wireframeColor;
            switch( light.Type )
            {
                case( vaLight::Type::Ambient ):     wireframeColor = vaVector4( 0.5f, 0.5f, 0.5f, 1.0f ); break;
                case( vaLight::Type::Directional ): wireframeColor = vaVector4( 1.0f, 1.0f, 0.0f, 1.0f ); break;
                case( vaLight::Type::Point ):       wireframeColor = vaVector4( 0.0f, 1.0f, 0.0f, 1.0f ); break;
                case( vaLight::Type::Spot ):        wireframeColor = vaVector4( 0.0f, 0.0f, 1.0f, 1.0f ); break;
                default: assert( false );
            }

            float sphereSize = light.Size;

            bool isSelected = m_lights[i] == m_UI_SelectedLight.lock();
            if( isSelected )
                sphereColor.w += pulse * 0.2f - 0.09f;

            canvas3D.DrawSphere( light.Position, sphereSize, vaVector4::ToBGRA( wireframeColor ), vaVector4::ToBGRA( sphereColor ) );

            if( light.Type == vaLight::Type::Directional || light.Type == vaLight::Type::Spot )
                canvas3D.DrawLine( light.Position, light.Position + 2.0f * light.Direction * sphereSize, 0xFF000000 );

            if( light.Type == vaLight::Type::Spot && isSelected )
            {
                vaVector3 lightUp = vaVector3::Cross( vaVector3( 0.0f, 0.0f, 1.0f ), light.Direction );
                if( lightUp.Length() < 1e-3f )
                    lightUp = vaVector3::Cross( vaVector3( 0.0f, 1.0f, 0.0f ), light.Direction );
                lightUp = lightUp.Normalized();

                vaVector3 coneInner = vaVector3::TransformNormal( light.Direction, vaMatrix3x3::RotationAxis( lightUp, light.SpotInnerAngle ) );
                vaVector3 coneOuter = vaVector3::TransformNormal( light.Direction, vaMatrix3x3::RotationAxis( lightUp, light.SpotOuterAngle ) );

                float coneRadius = 0.1f + light.Range; //light.EffectiveRadius();

                const int lineCount = 50;
                for( int j = 0; j < lineCount; j++ )
                {
                    float angle = j / (float)(lineCount-1) * VA_PIf * 2.0f;
                    vaVector3 coneInnerR = vaVector3::TransformNormal( coneInner, vaMatrix3x3::RotationAxis( light.Direction, angle ) );
                    vaVector3 coneOuterR = vaVector3::TransformNormal( coneOuter, vaMatrix3x3::RotationAxis( light.Direction, angle ) );
                    canvas3D.DrawLine( light.Position, light.Position + coneRadius * sphereSize * coneInnerR, 0xFFFFFF00 );
                    canvas3D.DrawLine( light.Position, light.Position + coneRadius * sphereSize * coneOuterR, 0xFFFF0000 );
                }
            }

        }
    }
}

void vaSceneOld::SetSkybox( vaRenderDevice & device, const string & texturePath, const vaMatrix3x3 & rotation, float colorMultiplier )
{
    shared_ptr<vaTexture> skyboxTexture = vaTexture::CreateFromImageFile( device, vaStringTools::SimpleNarrow(vaCore::GetExecutableDirectory()) + texturePath, vaTextureLoadFlags::Default );
    if( skyboxTexture == nullptr )
    {
        VA_LOG_WARNING( "vaSceneOld::SetSkybox - unable to load '%'", texturePath.c_str() );
    }
    else
    {
        m_skyboxTexturePath     = texturePath;
        m_skyboxTexture         = skyboxTexture;
        m_skyboxRotation        = rotation;
        m_skyboxColorMultiplier = colorMultiplier;
    }
}

//void vaSceneOld::SetEnvmap( vaRenderDevice & device, const string & texturePath, const vaMatrix3x3 & rotation, float colorMultiplier )
//{
//    shared_ptr<vaTexture> skyboxTexture = vaTexture::CreateFromImageFile( device, vaStringTools::SimpleNarrow( vaCore::GetExecutableDirectory( ) ) + texturePath, vaTextureLoadFlags::Default );
//    if( skyboxTexture == nullptr )
//    {
//        VA_LOG_WARNING( "vaSceneOld::SetSkybox - unable to load '%'", texturePath.c_str( ) );
//    }
//    else
//    {
//        m_envmapTexturePath = texturePath;
//        m_envmapTexture = skyboxTexture;
//        m_envmapRotation = rotation;
//        m_envmapColorMultiplier = colorMultiplier;
//    }
//}

vaDrawResultFlags vaSceneOld::SelectForRendering( vaRenderInstanceList * opaqueList, vaRenderInstanceList * transparentList, const vaRenderInstanceList::FilterSettings & filter, const SelectionFilterCallback & customFilter )
{
    VA_TRACE_CPU_SCOPE( vaScene_SelectForRendering );

    if( opaqueList != nullptr )
        opaqueList->Start( );
    if( transparentList != nullptr )
        transparentList->Start( );

    for( auto object : m_allObjects )
        object->SelectForRendering( opaqueList, transparentList, filter, customFilter );
    //renderSelection.MeshList.Insert()

    vaDrawResultFlags drawResults = vaDrawResultFlags::None;
    if( opaqueList != nullptr )
    {
        opaqueList->Stop();
        drawResults |= opaqueList->ResultFlags();
    }
    if( transparentList != nullptr )
    {
        transparentList->Stop();
        drawResults |= opaqueList->ResultFlags( );
    }

    //g_doCull = false;
    return drawResults;
}

std::vector<shared_ptr<vaSceneObject>> vaSceneOld::FindObjects( std::function<bool(vaSceneObject&obj)> searchCriteria )
{
    std::vector<shared_ptr<vaSceneObject>> ret;

    for( auto object : m_allObjects )
    {
        if( searchCriteria( *object ) )
            ret.push_back( object );
    }

    return std::move(ret);
}

void vaSceneOld::OnMouseClick( const vaVector3 & worldClickLocation )
{
    worldClickLocation;
#if 0
    m_UI_MouseClickIndicatorRemainingTime = m_UI_MouseClickIndicatorTotalTime;
    m_UI_MouseClickIndicator = worldClickLocation;

    shared_ptr<vaSceneObject> closestHitObj = nullptr;
    float maxHitDist = 0.5f;
    for( auto rootObj : m_rootObjects )
        rootObj->FindClosestRecursive( worldClickLocation, closestHitObj, maxHitDist );

    VA_LOG( "vaSceneOld - mouse clicked at (%.2f, %.2f, %.2f) world position.", worldClickLocation.x, worldClickLocation.y, worldClickLocation.z );

    // if same, just de-select
    if( closestHitObj != nullptr && m_UI_SelectedObject.lock() == closestHitObj )
        closestHitObj = nullptr;
    
    auto previousSel = m_UI_SelectedObject.lock();
    if( previousSel != nullptr )
    {
        VA_LOG( "vaSceneOld - deselecting object '%s'", previousSel->GetName().c_str() );
    }

    if( closestHitObj != nullptr )
    {
        VA_LOG( "vaSceneOld - selecting object '%s'", closestHitObj->GetName().c_str() );
    }

    m_UI_SelectedObject = closestHitObj;
#endif
}

shared_ptr<vaSceneObject> vaSceneOld::CreateObjectWithSystemMesh( vaRenderDevice & device, const string & systemMeshName, const vaMatrix4x4 & transform )
{
    auto renderMeshAsset = device.GetAssetPackManager().GetDefaultPack()->Find( systemMeshName );
    if( renderMeshAsset == nullptr )
    {
        VA_WARN( "InsertSystemMeshByName failed - can't find system asset '%s'", systemMeshName.c_str() );
        return nullptr;
    }
    shared_ptr<vaRenderMesh> renderMesh = dynamic_cast<vaAssetRenderMesh*>(renderMeshAsset.get())->GetRenderMesh();

    auto sceneObject = CreateObject( "obj_" + systemMeshName, transform );
    sceneObject->AddRenderMeshRef( renderMesh );
    return sceneObject;
}

std::vector<shared_ptr<vaSceneObject>> vaSceneOld::InsertAllPackMeshesToSceneAsObjects( vaSceneOld & scene, vaAssetPack & pack, const vaMatrix4x4 & transform )
{
    vaVector3 scale, translation; vaQuaternion rotation; 
    transform.Decompose( scale, rotation, translation ); 

    std::vector<shared_ptr<vaSceneObject>> addedObjects;
    
    assert( !pack.IsBackgroundTaskActive() );
    std::unique_lock<mutex> assetStorageMutexLock( pack.GetAssetStorageMutex() );

    for( size_t i = 0; i < pack.Count( false ); i++ )
    {
        auto asset = pack.AssetAt( i, false );
        if( asset->Type == vaAssetType::RenderMesh )
        {
            shared_ptr<vaRenderMesh> renderMesh = dynamic_cast<vaAssetRenderMesh*>(asset.get())->GetRenderMesh();
            if( renderMesh == nullptr )
                continue;
            auto sceneObject = scene.CreateObject( "obj_" + asset->Name(), transform );
            sceneObject->AddRenderMeshRef( renderMesh );
            addedObjects.push_back( sceneObject );
        }
    }
    return addedObjects;
}

int vaSceneOld::SplitMeshes( const std::vector<vaPlane> & splitPlanes, std::vector<shared_ptr<vaRenderMesh>> & outOldMeshes, std::vector<shared_ptr<vaRenderMesh>> & outNewMeshes, const int minTriangleCountThreshold, const std::vector<shared_ptr<vaRenderMesh>> & candidateMeshes, bool splitIfIntersectingTriangles )
{
    // use example:
    // std::vector<shared_ptr<vaRenderMesh>> oldMeshes;
    // std::vector<shared_ptr<vaRenderMesh>> newMeshes;
    // m_currentScene->SplitMeshes( choicePlanes, oldMeshes, newMeshes );
    // for( int i = 0; i < oldMeshes.size(); i++ )
    //     oldMeshes[i]->GetParentAsset()->GetAssetPack().Remove( oldMeshes[i]->GetParentAsset(), true );


    int totalSplitCount = 0;
    int splitCount;

    do 
    {
        splitCount = 0;
        for( auto object : m_allObjects )
            splitCount += object->SplitMeshes( splitPlanes, outOldMeshes, outNewMeshes, minTriangleCountThreshold, candidateMeshes, splitIfIntersectingTriangles );

        totalSplitCount += splitCount;
    } while ( splitCount > 0 );

    return totalSplitCount;
}

void vaSceneObject::ToNewRecursive( vaScene & scene, vaSceneObject * parentObj, entt::entity parentEntity )
{
    vaGUID singleMesh = (m_renderMeshes.size( ) == 1)?(m_renderMeshes[0]):(vaGUID::Null);

    entt::entity thisEntity = scene.CreateEntity( m_name, m_localTransform, parentEntity, singleMesh );

    if( m_renderMeshes.size() > 1 )
    {
        for( int i = 0; i < m_renderMeshes.size( ); i++ )
            scene.CreateEntity( vaStringTools::Format("mesh_%04d", i), vaMatrix4x4::Identity, thisEntity, m_renderMeshes[i] );
    }

    for( int i = 0; i < m_children.size( ); i++ )
        m_children[i]->ToNewRecursive( scene, parentObj, thisEntity );
}

shared_ptr<vaScene> vaSceneOld::ToNew( ) const
{
    shared_ptr<vaScene> scene = std::make_shared<vaScene>( m_name );


    entt::entity globalsParent = scene->CreateEntity( "Globals" );

    if( m_fog.Enabled )
    {
        Scene::FogSphere fogSphere;
        fogSphere.Enabled               = m_fog.Enabled;
        fogSphere.UseCustomCenter       = m_fog.UseCustomCenter;
        fogSphere.Center                = m_fog.Center;
        fogSphere.Color                 = m_fog.Color;
        fogSphere.RadiusInner           = m_fog.RadiusInner;
        fogSphere.RadiusOuter           = m_fog.RadiusOuter;
        fogSphere.BlendCurvePow         = m_fog.BlendCurvePow;
        fogSphere.BlendMultiplier       = m_fog.BlendMultiplier;
        entt::entity fogEntity = scene->CreateEntity( "Fog", vaMatrix4x4::FromTranslation(fogSphere.Center), globalsParent );
        scene->Registry().emplace<Scene::FogSphere>( fogEntity, fogSphere );
    }

    if( m_skyboxTexturePath != "" )
    {
        Scene::SkyboxTexture skyboxTexture;
        skyboxTexture.Path              = m_skyboxTexturePath;
        skyboxTexture.ColorMultiplier   = m_skyboxColorMultiplier;
        skyboxTexture.UID               = vaGUID::Null;
        skyboxTexture.Enabled           = true;
        entt::entity skyboxEntity = scene->CreateEntity( "Skybox", vaMatrix4x4(m_skyboxRotation.Transposed()), globalsParent );
        scene->Registry( ).emplace<Scene::SkyboxTexture>( skyboxEntity, skyboxTexture );
    }

    if( m_IBLProbeDistant.Enabled )
    {
        Scene::DistantIBLProbe probe; static_cast<Scene::IBLProbe&>(probe) = m_IBLProbeDistant;
        entt::entity probeEntity = scene->CreateEntity( "DistantIBLProbe", vaMatrix4x4::FromTranslation( probe.Position ), globalsParent );
        scene->Registry( ).emplace<Scene::DistantIBLProbe>( probeEntity, probe );
    }

    if( m_IBLProbeLocal.Enabled )
    {
        Scene::LocalIBLProbe probe; static_cast<Scene::IBLProbe&>( probe ) = m_IBLProbeLocal;
        entt::entity probeEntity = scene->CreateEntity( "LocalIBLProbe", vaMatrix4x4::FromTranslation( probe.Position ), globalsParent );
        scene->Registry( ).emplace<Scene::LocalIBLProbe>( probeEntity, probe );
    }

    for( size_t i = 0; i < m_rootObjects.size( ); i++ )
    {
        m_rootObjects[i]->ToNewRecursive( *scene );
    }

    entt::entity lightsParent = scene->CreateEntity( "Lights" );

    for( size_t i = 0; i < m_lights.size( ); i++ )
    {
        const vaLight & light = *m_lights[i];

        vaMatrix3x3 rot = vaMatrix3x3::Identity;
        if( light.Type != vaLight::Type::Ambient )
        {
            rot.Row(0) = light.Direction;
            rot.Row(1) = vaVector3::Cross( light.Up, light.Direction );
            rot.Row(2) = light.Up;
            if( rot.Row(1).Length() < 0.99f )
                vaVector3::ComputeOrthonormalBasis( light.Direction, rot.Row(1), rot.Row(2) );
        }
        
        entt::entity lightEntity = scene->CreateEntity( /*vaStringTools::Format("light_%04d", i)*/light.Name, vaMatrix4x4::FromRotationTranslation( rot, light.Position ), lightsParent );

        switch( light.Type )
        {
        case( vaLight::Type::Ambient ):
        {
            auto & newLight         = scene->Registry().emplace<Scene::LightAmbient>( lightEntity );
            newLight.Color          = light.Color;
            newLight.Intensity      = light.Intensity;
            newLight.FadeFactor     = (light.Enabled)?(1.0f):(0.0f);
        } break;
        case( vaLight::Type::Directional ):
        {
            auto & newLight         = scene->Registry().emplace<Scene::LightDirectional>( lightEntity );
            newLight.Color          = light.Color;
            newLight.Intensity      = light.Intensity;
            newLight.FadeFactor     = (light.Enabled)?(1.0f):(0.0f);
            newLight.AngularRadius  = light.AngularRadius;
            newLight.HaloSize       = light.HaloSize;
            newLight.HaloFalloff    = light.HaloFalloff;
            newLight.CastShadows    = light.CastShadows;
        } break;
        case( vaLight::Type::Point ):
        {
            auto & newLight         = scene->Registry().emplace<Scene::LightPoint>( lightEntity );
            newLight.Color          = light.Color;
            newLight.Intensity      = light.Intensity;
            newLight.FadeFactor     = (light.Enabled)?(1.0f):(0.0f);
            newLight.Size           = light.Size;
            newLight.Range          = light.Range;
            newLight.SpotInnerAngle = 0.0f;
            newLight.SpotOuterAngle = 0.0f;
            newLight.CastShadows    = light.CastShadows;
        } break;
        case( vaLight::Type::Spot ):
        {
            auto & newLight         = scene->Registry().emplace<Scene::LightPoint>( lightEntity );
            newLight.Color          = light.Color;
            newLight.Intensity      = light.Intensity;
            newLight.FadeFactor     = (light.Enabled)?(1.0f):(0.0f);
            newLight.Size           = light.Size;
            newLight.Range          = light.Range;
            newLight.SpotInnerAngle = light.SpotInnerAngle;
            newLight.SpotOuterAngle = light.SpotOuterAngle;
            newLight.CastShadows    = light.CastShadows;
        } break;
        default: assert( false );
        }

    }

    return scene;
}
#endif