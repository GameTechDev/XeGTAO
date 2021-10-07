///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaRenderMesh.h"

#include "Rendering/vaRenderDeviceContext.h"

#include "Rendering/vaRenderMaterial.h"

#include "Rendering/vaDebugCanvas.h"

#include "Rendering/vaStandardShapes.h"

#include "Core/System/vaFileTools.h"

#include "Core/vaXMLSerialization.h"

#include "Core/vaApplicationBase.h"

#include "IntegratedExternals/vaImguiIntegration.h"

#include "IntegratedExternals/vaMeshoptimizerIntegration.h"

#include "Rendering/vaAssetPack.h"

using namespace Vanilla;

//vaRenderMeshManager & renderMeshManager, const vaGUID & uid

const int c_renderMeshFileVersion = 4;


vaRenderMesh::vaRenderMesh( const vaRenderingModuleParams & params ) 
    :   vaRenderingModule( params ),
        m_renderMeshManager( *static_cast<vaRenderMeshManager*>(params.UserParam0) ),
        vaAssetResource( *static_cast<vaGUID*>(params.UserParam1) )
        //m_trackee( static_cast<vaRenderMeshManager*>(params.UserParam0)->GetRenderMeshTracker( ), this )
        //m_indexBuffer( params.RenderDevice ), 
        //m_vertexBuffer( params.RenderDevice )
{
    m_frontFaceWinding          = vaWindingOrder::CounterClockwise;
    m_boundingBox               = vaBoundingBox::Degenerate;
    m_boundingSphere            = vaBoundingSphere::Degenerate;
    //m_tangentBitangentValid     = true;

    m_indexBuffer               = GetRenderDevice().CreateModule<vaRenderBuffer>();
    m_vertexBuffer              = GetRenderDevice().CreateModule<vaRenderBuffer>();

    {
        std::unique_lock managerLock( m_renderMeshManager.Mutex() );
        m_globalIndex = m_renderMeshManager.Meshes().Insert( this );
    }

    m_lastShaderConstants.Invalidate();
}

vaRenderMesh::~vaRenderMesh( )
{
    {
        std::unique_lock managerLock( m_renderMeshManager.Mutex() );
        m_renderMeshManager.Meshes().Remove( m_globalIndex );
    }
}

shared_ptr<vaRenderMaterial> vaRenderMesh::GetMaterial( ) const
{
    if( m_materialID.IsNull() )
        return m_renderMeshManager.GetRenderDevice().GetMaterialManager().GetDefaultMaterial( );
    else
        return vaUIDObjectRegistrar::Find<vaRenderMaterial>( m_materialID ); 
}

vaFramePtr<vaRenderMaterial> vaRenderMesh::GetMaterialFP( ) const
{
    vaFramePtr<vaRenderMaterial> ret;

#ifdef VA_RENDER_MATERIAL_USE_CACHED_FP
    ret = m_materialCachedFP.load( );
    if( ret.Valid() )
        return ret;
#endif

    if( m_materialID.IsNull( ) )
        ret = vaFramePtr<vaRenderMaterial>( m_renderMeshManager.GetRenderDevice( ).GetMaterialManager( ).GetDefaultMaterial( ) );
    else
        ret = vaUIDObjectRegistrar::FindFP<vaRenderMaterial>( m_materialID );
    
#ifdef VA_RENDER_MATERIAL_USE_CACHED_FP
    m_materialCachedFP.store( ret );
#endif
    
    return ret;
}

void vaRenderMesh::SetMaterial( const vaGUID & materialID )
{
    m_materialID = materialID;
#ifdef VA_RENDER_MATERIAL_USE_CACHED_FP
    m_materialCachedFP.store( vaFramePtr<vaRenderMaterial>{} );
#endif
}

void vaRenderMesh::SetMaterial( const shared_ptr<vaRenderMaterial> & m )
{
#ifdef VA_RENDER_MATERIAL_USE_CACHED_FP
    m_materialCachedFP.store( vaFramePtr<vaRenderMaterial>{} );
#endif
    if( m == nullptr )
    {
        m_materialID = vaGUID::Null;
        return;
    }
    assert( m->UIDObject_GetUID( ) != vaCore::GUIDNull( ) );
    m_materialID = m->UIDObject_GetUID( );
}

void vaRenderMesh::MeshReset( )
{
    assert( GetRenderDevice( ).IsRenderThread( ) );
    m_vertices.clear( );
    m_indices.clear( );
    MeshSetGPUDataDirty( );
}

void vaRenderMesh::MeshSet( const std::vector<StandardVertex> & vertices, const std::vector<uint32> & indices )
{
    Vertices()      = vertices;
    Indices()       = indices;
    MeshSetGPUDataDirty( );

    UpdateAABB( );
    m_LODParts.resize( 1 );
    m_LODParts[0].IndexStart = 0;
    m_LODParts[0].IndexCount = (int)Indices( ).size( );
}

void vaRenderMesh::MeshGenerateNormals( vaWindingOrder windingOrder, int indexFrom, int indexCount, float mergeSharedMaxAngle )
{
    assert( GetRenderDevice( ).IsRenderThread( ) );

    std::vector<vaVector3> positions;
    std::vector<vaVector3> normals;
    positions.resize( m_vertices.size( ) );
    normals.resize( m_vertices.size( ) );

    const int vertexCount = (int)m_vertices.size( );
    for( int i = 0; i < vertexCount; i++ )
        positions[i] = m_vertices[i].Position;

    vaTriangleMeshTools::GenerateNormals( normals, positions, m_indices, windingOrder, indexFrom, indexCount, true, mergeSharedMaxAngle );

    for( int i = indexFrom; i < indexFrom + indexCount; i++ )
        m_vertices[m_indices[i]].Normal.AsVec3( ) = normals[m_indices[i]];

    //std::unique_lock lock( m_mutex ); <- must be set by the caller
    MeshSetGPUDataDirty( );
}

bool vaRenderMesh::PreRenderUpdate( vaRenderDeviceContext & renderContext )
{
    std::unique_lock uniqueLock( m_mutex );
    assert( !renderContext.IsWorker() );    // uploading the constant buffer requires the master thread

    // could've been updated by another thread
    if( m_gpuDataDirty )
    {
        string parentAssetName = "System";
        if( GetParentAsset( ) != nullptr )
            parentAssetName = GetParentAsset( )->Name( );

        if( m_indices.size( ) > 0 )
        {
            if( m_indexBuffer->GetElementCount() != m_indices.size() )
                m_indexBuffer->Create( (uint64)m_indices.size( ), vaResourceFormat::R32_UINT, vaRenderBufferFlags::VertexIndexBuffer | vaRenderBufferFlags::ForceByteAddressBufferViews, parentAssetName + "_" + "IndexBuffer" );
            m_indexBuffer->Upload( renderContext, m_indices );
        }
        else
        {
            assert( false ); // decide on whether you want to Destroy - it will leave the bindless indices in the constants incorrect
            m_indexBuffer->Destroy( );
        }

        if( m_vertices.size( ) > 0 )
        {
            if( m_vertexBuffer->GetElementCount( ) != m_vertices.size( ) )
                m_vertexBuffer->Create<StandardVertex>( (uint64)m_vertices.size( ), vaRenderBufferFlags::VertexIndexBuffer | vaRenderBufferFlags::ForceByteAddressBufferViews, parentAssetName + "_" + "VertexBuffer" );
            m_vertexBuffer->Upload( renderContext, m_vertices );
        }
        else
        {
            assert( false ); // decide on whether you want to Destroy - it will leave the bindless indices in the constants incorrect
            m_vertexBuffer->Destroy( );
        }

        m_gpuDataDirty = false;

        // TEMP TEMP TEMP
        UpdateGPURTData( renderContext );
        // TEMP TEMP TEMP

        // Remember when adding anything new here that it will only get updated if m_gpuDataDirty - so make sure to make it dirty on change!
        m_lastShaderConstants.IndexBufferBindlessIndex  = m_indexBuffer->GetSRVBindlessIndex( nullptr );
        m_lastShaderConstants.VertexBufferBindlessIndex = m_vertexBuffer->GetSRVBindlessIndex( nullptr );
        m_lastShaderConstants.FrontFaceIsClockwise      = m_frontFaceWinding == vaWindingOrder::Clockwise;
        m_lastShaderConstants.Dummy1 = 0;

        m_renderMeshManager.GetGlobalConstantBuffer()->UploadSingle<ShaderMeshConstants>( renderContext, m_lastShaderConstants, m_globalIndex );
    }
    return !m_gpuDataDirty;
}

void vaRenderMesh::UpdateAABB( ) 
{ 
    if( Vertices().size() > 0 ) 
    {
        vaTriangleMeshTools::CalculateBounds( Vertices(), m_boundingBox, m_boundingSphere );
    }
    else 
    {
        m_boundingBox = vaBoundingBox::Degenerate; 
        m_boundingSphere = vaBoundingSphere::Degenerate; 
    }
}

void vaRenderMesh::RebuildNormals( int lodFrom, int lodCount, float mergeSharedMaxAngle )
{
    if( lodCount == 0  )
        lodCount = (int)m_LODParts.size();

    for( int i = lodFrom; i < std::min(lodFrom+lodCount, (int)m_LODParts.size()); i++ )
        MeshGenerateNormals( m_frontFaceWinding, m_LODParts[i].IndexStart, m_LODParts[i].IndexCount, mergeSharedMaxAngle );

    MeshSetGPUDataDirty( );
}

void vaRenderMesh::MergeSimilarVerts( int lodFrom, int lodCount, float distanceThreshold, float angleThreshold )
{
    auto closeEnough = [posThresholdSq{ vaMath::Sq( distanceThreshold ) }, dotThreshold{ std::cosf( angleThreshold ) }]( const vaRenderMesh::StandardVertex& left, const vaRenderMesh::StandardVertex& right )
    {
        if( ( left.Position - right.Position ).LengthSq( ) > posThresholdSq )
            return false;
        if( vaVector3::Dot( left.Normal.AsVec3( ), right.Normal.AsVec3( ) ) < dotThreshold )
            return false;
        return true;
    };

    auto srcVertices = Vertices( );
    auto srcIndices = Indices( );

    VA_LOG( "Input number of vertices: %d, indices: %d", srcVertices.size( ), srcIndices.size( ) );

    MeshReset( );

    assert( lodFrom == 0 );
    assert( lodCount == 0 );
    if( lodCount == 0 )
        lodCount = (int)m_LODParts.size( );

    for( int li = lodFrom; li < std::min( lodFrom + lodCount, (int)m_LODParts.size( ) ); li++ )
    {
        LODPart & lodPart = m_LODParts[li];

        vaTimerLogScope timer( "mesh" );
        for( int i = lodPart.IndexStart; i < lodPart.IndexStart+lodPart.IndexCount; i += 3 )
        {
            const vaRenderMesh::StandardVertex a = srcVertices[srcIndices[i + 0]];
            const vaRenderMesh::StandardVertex b = srcVertices[srcIndices[i + 1]];
            const vaRenderMesh::StandardVertex c = srcVertices[srcIndices[i + 2]];

            MeshAddTriangleMergeDuplicates( a, b, c, closeEnough, -1 );
        }
    }

    VA_LOG( "Output number of vertices: %d, indices: %d", Vertices( ).size( ), Indices( ).size( ) );

    MeshSetGPUDataDirty( );
}

void vaRenderMesh::Transform( const vaMatrix4x4 & transform )
{
    auto & vertices = Vertices( );
    for( int i = 0; i < vertices.size( ); i++ )
        vertices[i].Position = vaVector3::TransformCoord( vertices[i].Position, transform );

    UpdateAABB( );
    MeshSetGPUDataDirty( );
}

void vaRenderMesh::ReCenter( )
{
    UpdateAABB( );

    if( m_boundingBox.Center( ) != vaVector3( 0, 0, 0 ) )
        Transform( vaMatrix4x4::Translation( -m_boundingBox.Center() ) );
}

void vaRenderMesh::Extrude( const vaBoundingBox & area, const string & newMeshAssetName )
{
    auto newRenderMesh = CreateShallowCopy( *this, vaGUID::Create( ), true );

    const auto & srcVertices = Vertices( );
    auto & srcIndices = Indices( );

    shared_ptr<vaRenderMesh::StandardTriangleMesh> newTriMesh = std::make_shared<vaRenderMesh::StandardTriangleMesh>( GetRenderDevice( ) );

    int extrudedTriangles = 0;

    for( int i = 0; i < (int)srcIndices.size( ); i += 3 )
    {
        if( srcIndices[i + 0] == srcIndices[i + 1] || srcIndices[i + 0] == srcIndices[i + 2 ] )
            continue;

        const StandardVertex a = srcVertices[srcIndices[i + 0]];
        const StandardVertex b = srcVertices[srcIndices[i + 1]];
        const StandardVertex c = srcVertices[srcIndices[i + 2]];

        if( area.PointInside( a.Position ) && area.PointInside( b.Position ) && area.PointInside( c.Position ) )
        {
            srcIndices[i + 0] = 0;
            srcIndices[i + 1] = 0;
            srcIndices[i + 2] = 0;

            newTriMesh->AddTriangleMergeDuplicates( a, b, c, StandardVertex::IsDuplicate, -1 );

            extrudedTriangles++;
        }
    }

    if( extrudedTriangles == 0 )
    {
        VA_LOG_WARNING( "Could not find any triangles to extrude" );
        return;
    }

    VA_LOG_SUCCESS( "Extruded %d triangles", extrudedTriangles );
    newRenderMesh->MeshSet( newTriMesh->Vertices(), newTriMesh->Indices() );
    newRenderMesh->ReCenter( );

    if( GetParentAsset( ) != nullptr )
    {
        string name = GetParentAsset( )->GetAssetPack( ).FindSuitableAssetName( newMeshAssetName, true );
        GetParentAsset( )->GetAssetPack( ).Add( newRenderMesh, name, true );
        VA_LOG_SUCCESS( "New mesh '%s' added to the asset pack '%s'!", name.c_str(), GetParentAsset()->GetAssetPack().GetName().c_str() );
    }

    MeshSetGPUDataDirty( );
    UpdateAABB( );
}

void vaRenderMesh::TNTesselate( )
{
    if( m_LODParts.size() == 0 || m_LODParts[0].IndexCount == 0 )
    {
        VA_WARN( "No input mesh" );
        return;
    }
    auto & origVertices  = Vertices( );
    auto & origIndices   = Indices( );

    std::vector<StandardVertex>  newVertices;
    std::vector<uint32>          newIndices;

    //shared_ptr<vaRenderMesh::StandardTriangleMesh> newTriMesh = std::make_shared<vaRenderMesh::StandardTriangleMesh>( GetRenderDevice( ) );

    int newTriangles = 0;

    struct PnPatch
    {
        vaVector3 b210;
        vaVector3 b120;
        vaVector3 b021;
        vaVector3 b012;
        vaVector3 b102;
        vaVector3 b201;
        vaVector3 b111;
        vaVector3 n110;
        vaVector3 n011;
        vaVector3 n101;
    };

    // see https://github.com/martin-pr/possumwood/wiki/Geometry-Shader-Tessellation-using-PN-Triangles and http://onrendering.blogspot.com/2011/12/tessellation-on-gpu-curved-pn-triangles.html
    // for reference code

    int indexFrom = m_LODParts[0].IndexStart; int indexTo = indexFrom + m_LODParts[0].IndexCount;
    for( int i = indexFrom; i < indexTo; i += 3 )
    {
        if( origIndices[i + 0] == origIndices[i + 1] || origIndices[i + 0] == origIndices[i + 2] )
            continue;

        const StandardVertex & a = origVertices[origIndices[i + 0]];
        const StandardVertex & b = origVertices[origIndices[i + 1]];
        const StandardVertex & c = origVertices[origIndices[i + 2]];
        vaVector3 pos[3]    = { a.Position, b.Position, c.Position };
        vaVector3 norm[3]   = { a.Normal.AsVec3(), b.Normal.AsVec3(), c.Normal.AsVec3() };

        float w12 = vaVector3::Dot( pos[1] - pos[0], norm[0] );
        float w21 = vaVector3::Dot( pos[0] - pos[1], norm[1] );
        float w23 = vaVector3::Dot( pos[2] - pos[1], norm[1] );
        float w32 = vaVector3::Dot( pos[1] - pos[2], norm[2] );
        float w31 = vaVector3::Dot( pos[0] - pos[2], norm[2] );
        float w13 = vaVector3::Dot( pos[2] - pos[0], norm[0] );

        //auto vij = [&]( int i, int j )
        //{
        //    vaVector3 Pj_minus_Pi = pos[j] - pos[i];
        //    vaVector3 Ni_plus_Nj = norm[i] + norm[j];
        //    return 2.0f * vaVector3::Dot( Pj_minus_Pi, Ni_plus_Nj ) / vaVector3::Dot( Pj_minus_Pi, Pj_minus_Pi );
        //};
        auto nv = [&]( vaVector3 p1, vaVector3 n1, vaVector3 p2, vaVector3 n2 ) 
        {
            return 2.0f * vaVector3::Dot( p2 - p1, n1 + n2 ) / vaVector3::Dot( p2 - p1, p2 - p1 );
        };

        PnPatch coeffs;
        coeffs.b210 = ( 2 * pos[0] + pos[1] - w12 * norm[0] ) / 3.0;
        coeffs.b120 = ( 2 * pos[1] + pos[0] - w21 * norm[1] ) / 3.0;
        coeffs.b021 = ( 2 * pos[1] + pos[2] - w23 * norm[1] ) / 3.0;
        coeffs.b012 = ( 2 * pos[2] + pos[1] - w32 * norm[2] ) / 3.0;
        coeffs.b102 = ( 2 * pos[2] + pos[0] - w31 * norm[2] ) / 3.0;
        coeffs.b201 = ( 2 * pos[0] + pos[2] - w13 * norm[0] ) / 3.0;

        vaVector3 E = ( coeffs.b210 + coeffs.b120 + coeffs.b021 + coeffs.b012 + coeffs.b102 + coeffs.b201 ) / 6.0;
        vaVector3 V = ( pos[0] + pos[1] + pos[2] ) / 3.0;
        coeffs.b111 = E + ( E - V ) / 2.0;

//        coeffs.n110 = norm[0] + norm[1] - vij( 0, 1 ) * ( pos[1] - pos[0] );
//        coeffs.n011 = norm[1] + norm[2] - vij( 1, 2 ) * ( pos[2] - pos[1] );
//        coeffs.n101 = norm[2] + norm[0] - vij( 2, 0 ) * ( pos[0] - pos[2] );
        coeffs.n110 = vaVector3::Normalize( norm[0] + norm[1] - nv( pos[0], norm[0], pos[1], norm[1] ) * ( pos[1] - pos[0] ) );
        coeffs.n011 = vaVector3::Normalize( norm[1] + norm[2] - nv( pos[1], norm[1], pos[2], norm[2] ) * ( pos[2] - pos[1] ) );
        coeffs.n101 = vaVector3::Normalize( norm[2] + norm[0] - nv( pos[2], norm[2], pos[0], norm[0] ) * ( pos[0] - pos[2] ) );


        auto evaluateVertex = [ ]( const StandardVertex & a, const StandardVertex & b, const StandardVertex & c, const PnPatch & coeffs, float bu, float bv, float bw )
        {
            const float uTessAlpha = 1.0f;

            StandardVertex out;
            out.Normal.w = 0.0f;
            out.Color = 0xffffffff;
            assert( a.Color == 0xffffffff && b.Color == 0xffffffff && c.Color == 0xffffffff );

            // just barycentric interpolation for texcoords
            out.TexCoord0   = bw * a.TexCoord0 + bu * b.TexCoord0 + bv * c.TexCoord0;
            out.TexCoord1   = bw * a.TexCoord1 + bu * b.TexCoord1 + bv * c.TexCoord1;
            //out.Color       = vaVector3:: bw * a.Color + bu * b.TexCoord1 + bv * c.TexCoord1;

            vaVector3 uvw{ bu,bv,bw };
            vaVector3 uvwSquared = uvw * uvw;
            vaVector3 uvwCubed = uvwSquared * uvw;

            vaVector3 baryNormal = bw * a.Normal.AsVec3() + bu * b.Normal.AsVec3() + bv * c.Normal.AsVec3();
            vaVector3 pnNormal =  a.Normal.AsVec3() * uvwSquared[2]
                                + b.Normal.AsVec3() * uvwSquared[0]
                                + c.Normal.AsVec3() * uvwSquared[1]
                                + coeffs.n110 * uvw[2] * uvw[0]
                                + coeffs.n011 * uvw[0] * uvw[1]
                                + coeffs.n101 * uvw[2] * uvw[1];
            out.Normal.AsVec3() = (uTessAlpha * pnNormal + ( 1.0 - uTessAlpha ) * baryNormal).Normalized();

            vaVector3 baryPos = bw * a.Position + bu * b.Position + bv * c.Position;
            uvwSquared *= 3.0; // save some computations
            // compute PN position
            vaVector3 pnPos = a.Position * uvwCubed[2]
                            + b.Position * uvwCubed[0]
                            + c.Position * uvwCubed[1]
                            + coeffs.b210 * uvwSquared[2] * uvw[0]
                            + coeffs.b120 * uvwSquared[0] * uvw[2]
                            + coeffs.b201 * uvwSquared[2] * uvw[1]
                            + coeffs.b021 * uvwSquared[0] * uvw[1]
                            + coeffs.b102 * uvwSquared[1] * uvw[2]
                            + coeffs.b012 * uvwSquared[1] * uvw[0]
                            + coeffs.b111 * 6.0 * uvw[0] * uvw[1] * uvw[2];
            out.Position = ( uTessAlpha * pnPos + ( 1.0 - uTessAlpha ) * baryPos );

            return out;
        };

        StandardVertex d = evaluateVertex( a, b, c, coeffs, 0.5f, 0.0f, 0.5f );
        StandardVertex e = evaluateVertex( a, b, c, coeffs, 0.5f, 0.5f, 0.0f );
        StandardVertex f = evaluateVertex( a, b, c, coeffs, 0.0f, 0.5f, 0.5f );

        vaTriangleMeshTools::AddTriangle_MergeDuplicates( newVertices, newIndices, a, d, f, StandardVertex::IsDuplicate, 0 );
        vaTriangleMeshTools::AddTriangle_MergeDuplicates( newVertices, newIndices, d, e, f, StandardVertex::IsDuplicate, 0 );
        vaTriangleMeshTools::AddTriangle_MergeDuplicates( newVertices, newIndices, d, b, e, StandardVertex::IsDuplicate, 0 );
        vaTriangleMeshTools::AddTriangle_MergeDuplicates( newVertices, newIndices, e, c, f, StandardVertex::IsDuplicate, 0 );
        newTriangles += 4;
    }

    VA_LOG_SUCCESS( "Tessellated from %d to %d triangles", (int)origIndices.size()/3, newTriangles );
    
#if 0 // replace option
    MeshSet( newTriMesh );
#else // insert as new LOD option
    // insert, shift & update LOD parts
    m_LODParts.insert( m_LODParts.begin(), LODPart( 0, newTriangles*3, 2.0f ) );
    for( int i = 1; i < m_LODParts.size( ); i++ )
    {
        m_LODParts[i].SwapToNextDistance *= 2.0f;
        m_LODParts[i].IndexStart += newTriangles*3;
    }
    // insert & shift new vertices
    origVertices.insert( origVertices.begin(), newVertices.begin(), newVertices.end() );
    // insert & shift new indices
    origIndices.insert( origIndices.begin(), newIndices.begin(), newIndices.end() );
    // update new indices to match new vertices
    for( int i = newTriangles*3; i < (int)origIndices.size(); i++ )
        origIndices[i] += (int)newVertices.size();
#endif

    assert( m_LODParts.size()<= LODPart::MaxLODParts ); 

    MeshSetGPUDataDirty( );
    UpdateAABB( );
}

void vaRenderMesh::ClearLODs( )
{
    assert( m_LODParts.size( ) > 0 );
    if( m_LODParts.size( ) == 0 )
        return;

    auto & vertices = Vertices();
    auto & indices  = Indices();

    assert( m_LODParts[0].IndexStart == 0 );
    indices.resize( m_LODParts[0].IndexCount );

    // in case we already have LODs, drop them
    m_LODParts.resize( 1 );
    uint32 maxVertUsed = 0;
    assert( m_LODParts[0].IndexStart == 0 );    // below stuff assumes IndexStart == 0
    for( int i = m_LODParts[0].IndexStart; i < m_LODParts[0].IndexStart + m_LODParts[0].IndexCount; i++ )
        maxVertUsed = std::max( maxVertUsed, indices[i] );
    assert( maxVertUsed + 1 <= vertices.size( ) );
    vertices.resize( maxVertUsed + 1 );

    MeshSetGPUDataDirty( );
    UpdateAABB();
}

void vaRenderMesh::RebuildLODs( float maxRelativePosError, float normalRebuildMergeSharedMaxAngle )
{
    assert( m_LODParts.size() > 0 );
    if( m_LODParts.size() == 0 )
        return;

    auto & vertices = Vertices();
    auto & indices  = Indices();

    ClearLODs();

    //
    const float stepTriReduce       = 0.25f;    // how many triangles to attempt to drop every step
    const float stepTriReduceMin    = vaMath::Lerp( stepTriReduce, 1.0f, 0.7f );     // stop if failed to drop below this for next step
    const float stepRangeIncrease   = 1.0f / stepTriReduce;
    const float maxError            = m_boundingBox.Size.Length() * maxRelativePosError;
    const int stopTriCount          = 64;  // doesn't make sense to go lower - we're going to be heavily CPU bound so it doesn't save anything

    // transition starting point - just a guess <shrug>
    m_LODParts[0].SwapToNextDistance = 1.0f / std::sqrtf(stepTriReduce);

    std::vector<StandardVertex> srcVertices = vertices;
    std::vector<uint32>         srcIndices  = indices;

    int currentLOD = 1;
    do
    {
        LODPart & prevLOD = m_LODParts[currentLOD - 1];
        size_t targetCount              = (size_t)(srcIndices.size() * stepTriReduce);
        size_t targetCountAcceptable    = (size_t)(srcIndices.size() * stepTriReduceMin);

        std::vector<uint32> lod( srcIndices.size() );
        lod.resize( meshopt_simplify( &lod[0], &srcIndices[0], srcIndices.size(),
            &srcVertices[0].Position.x, srcVertices.size( ), sizeof( StandardVertex ), targetCount, maxError ) );

        // exit conditions
        if( lod.size() > targetCountAcceptable || lod.size() < stopTriCount*3 )
            break;

        LODPart nextLOD;
        nextLOD.IndexStart          = (int)indices.size();
        nextLOD.SwapToNextDistance  = prevLOD.SwapToNextDistance * std::sqrtf(stepRangeIncrease);
        int startVertex             = (int)vertices.size();
        for( int i = 0; i < (int)lod.size(); i += 3 )
        {
            const StandardVertex a = srcVertices[lod[i+0]];
            const StandardVertex b = srcVertices[lod[i+1]];
            const StandardVertex c = srcVertices[lod[i+2]];

            int lookBackRange = std::min( 1024, ((int)vertices.size()-startVertex) );
            MeshAddTriangleMergeDuplicates( a, b, c, vaRenderMesh::StandardVertex::IsDuplicate, lookBackRange );
        }
        nextLOD.IndexCount = (int)indices.size() - nextLOD.IndexStart;
        assert( nextLOD.IndexCount > 0 );

        srcVertices.resize( vertices.size() - startVertex );
        srcIndices.resize( nextLOD.IndexCount );
        for( int i = 0; i < srcVertices.size(); i++ )
            srcVertices[i] = vertices[startVertex+i];
        for( int i = 0; i < srcIndices.size( ); i++ )
        {
            srcIndices[i] = indices[nextLOD.IndexStart+i] - startVertex;
            assert( srcIndices[i] >= 0 && srcIndices[i] < srcVertices.size() );
        }

        m_LODParts.push_back( nextLOD );

        RebuildNormals( currentLOD, 1, normalRebuildMergeSharedMaxAngle );

        currentLOD++;
    } while( true );

    assert( m_LODParts.size( ) <= LODPart::MaxLODParts );

    MeshSetGPUDataDirty( );
    UpdateAABB( );
}

float vaRenderMesh::FindLOD( float LODRangeFactor )
{
    LODRangeFactor = LODRangeFactor * m_LODDistanceOffsetMul + m_LODDistanceOffsetAdd;
    // (this doc might be old - check out scene selection code just in case)
    // to compute LOD scaling factor: 
    // 1.) get rough bounding sphere and do approx projection to screen (valid only at screen center but we want that - don't want LODs to change as we turn around)
    // 2.) we then use 1 / screen projected to compute LODRangeFactor which is effectively "1 / boundsScreenYSize" and use that to find the closest LOD!
    // 3.) also further scale by filter.LODReferenceScale which can be (but doesn't have to be) resolution dependent!
    if( m_LODParts.size() <= 1 || LODRangeFactor < m_LODParts.front().SwapToNextDistance )
        return 0.0f;
    if( LODRangeFactor >= m_LODParts.back().SwapToNextDistance )
        return float( m_LODParts.size( ) - 1 );

    // go in reverse because it's more likely there's a lot of objects in the distance than nearby?
    for( int i = (int)m_LODParts.size()-2; i >= 0; i-- )
    {
        if( (LODRangeFactor >= m_LODParts[i].SwapToNextDistance) && (LODRangeFactor < m_LODParts[i+1].SwapToNextDistance) )
        {
            float wholePart = float(i);
            float fractPart = vaMath::Smoothstep( vaMath::Saturate( (LODRangeFactor - m_LODParts[i].SwapToNextDistance) / (m_LODParts[i+1].SwapToNextDistance-m_LODParts[i].SwapToNextDistance) ) );
            return wholePart + fractPart;
        }
    }
    return float(m_LODParts.size()-1);
}

void vaRenderMesh::MeshSetGPUDataDirty( ) 
{ 
    assert( !UIDObject_IsTracked() || !FramePtr_MaybeActive( ) );
    m_gpuDataDirty = true; 
}


vaRenderMeshManager::vaRenderMeshManager( const vaRenderingModuleParams & params ) : 
    vaRenderingModule( params ), 
    vaUIPanel( "RenderMeshManager", 0, false, vaUIPanel::DockLocation::DockedLeftBottom )
//    , m_constantBuffer( params )
{
    m_isDestructing = false;
    // m_renderMeshes.SetAddedCallback( std::bind( &vaRenderMeshManager::RenderMeshesTrackeeAddedCallback, this, std::placeholders::_1 ) );
    // m_renderMeshes.SetBeforeRemovedCallback( std::bind( &vaRenderMeshManager::RenderMeshesTrackeeBeforeRemovedCallback, this, std::placeholders::_1, std::placeholders::_2 ) );
    
    // m_simpleConstantBuffers = vaRenderBuffer::Create<ShaderInstanceConstants>( GetRenderDevice(), 1024, vaRenderBufferFlags::None, "SimpleInstancesConstantBuffer" );

    m_constantBuffer = vaRenderBuffer::Create<ShaderMeshConstants>( GetRenderDevice(), m_constantBufferMaxCount, vaRenderBufferFlags::None, "ShaderMeshConstants" );
}

void vaRenderMeshManager::PostCreateInitialize( )
{
    // creates instance so any Scene referrering to it can find it; if faster startup times needed remove and handle on demand instead, somehow
    UnitSphere( );
}

vaRenderMeshManager::~vaRenderMeshManager( )
{
    m_unitSphere = nullptr;

    m_isDestructing = true;
    //m_renderMeshesMap.clear();

    {
        std::unique_lock managerLock( Mutex() );
        for( int i = (int)m_meshes.PackedArray().size()-1; i>=0; i-- )
            m_meshes.At(m_meshes.PackedArray()[i])->UIDObject_Untrack();

        // this must absolutely be true as they contain direct reference to this object
        assert( m_meshes.Count() == 0 );
    }
}

shared_ptr<vaRenderMesh> vaRenderMeshManager::CreateRenderMesh( const vaGUID & uid, bool startTrackingUIDObject ) 
{ 
    shared_ptr<vaRenderMesh> ret = GetRenderDevice().CreateModule<vaRenderMesh>( (void*)this, (void*)&uid );

    if( startTrackingUIDObject )
    {
        assert( vaThreading::IsMainThread( ) ); // warning, potential bug - don't automatically start tracking if adding from another thread; rather finish initialization completely and then manually call UIDObject_Track
        ret->UIDObject_Track( );                // needs to be registered to be visible/searchable by various systems such as rendering
    }

    return ret;
}

void vaRenderMeshManager::UpdateAndSetToGlobals( /*vaRenderDeviceContext & renderContext,*/ vaShaderItemGlobals & shaderItemGlobals/*, const vaDrawAttributes & drawAttributes*/ )
{   
    //renderContext; drawAttributes;
    assert( shaderItemGlobals.ShaderResourceViews[SHADERGLOBAL_MESH_CONSTANTBUFFERS_TEXTURESLOT] == nullptr );
    shaderItemGlobals.ShaderResourceViews[SHADERGLOBAL_MESH_CONSTANTBUFFERS_TEXTURESLOT] = m_constantBuffer;
}

void vaRenderMeshManager::UIPanelTick( vaApplicationBase & )
{
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    static int selected = 0;
    ImGui::BeginChild( "left pane", ImVec2( 150, 0 ), true );
    for( int i = 0; i < 7; i++ )
    {
        char label[128];
        sprintf_s( label, _countof( label ), "MyObject %d", i );
        if( ImGui::Selectable( label, selected == i ) )
            selected = i;
    }
    ImGui::EndChild( );
    ImGui::SameLine( );

    // right
    ImGui::BeginGroup( );
    ImGui::BeginChild( "item view", ImVec2( 0, -ImGui::GetFrameHeightWithSpacing( ) ) ); // Leave room for 1 line below us
    ImGui::Text( "MyObject: %d", selected );
    ImGui::Separator( );
    ImGui::TextWrapped( "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. " );
    ImGui::EndChild( );
    ImGui::BeginChild( "buttons" );
    if( ImGui::Button( "Revert" ) ) { }
    ImGui::SameLine( );
    if( ImGui::Button( "Save" ) ) { }
    ImGui::EndChild( );
    ImGui::EndGroup( );
#endif
}

bool vaRenderMesh::SaveAPACK( vaStream & outStream )
{
    VERIFY_TRUE_RETURN_ON_FALSE( outStream.WriteValue<int32>( c_renderMeshFileVersion ) );
    VERIFY_TRUE_RETURN_ON_FALSE( outStream.WriteValue<int32>( (int32)m_frontFaceWinding ) );
    VERIFY_TRUE_RETURN_ON_FALSE( outStream.WriteValueVector<uint32>( Indices() ) );
    VERIFY_TRUE_RETURN_ON_FALSE( outStream.WriteValueVector<StandardVertex>( Vertices() ) );
    VERIFY_TRUE_RETURN_ON_FALSE( SaveUIDObjectUID( outStream, vaUIDObjectRegistrar::Find<vaRenderMaterial>( m_materialID ) ) );
    VERIFY_TRUE_RETURN_ON_FALSE( outStream.WriteValueVector<LODPart>( m_LODParts ) );
    VERIFY_TRUE_RETURN_ON_FALSE( outStream.WriteValue<vaBoundingBox>( m_boundingBox ) );
    VERIFY_TRUE_RETURN_ON_FALSE( outStream.WriteValue<vaBoundingSphere>( m_boundingSphere ) );
    VERIFY_TRUE_RETURN_ON_FALSE( outStream.WriteValue<float>( m_LODDistanceOffsetAdd ) );
    VERIFY_TRUE_RETURN_ON_FALSE( outStream.WriteValue<float>( m_LODDistanceOffsetMul ) );

    return true;
}

bool vaRenderMesh::LoadAPACK( vaStream & inStream )
{
    int32 fileVersion = 0;
    VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<int32>( fileVersion ) );
    
    if( fileVersion == 3 )
    {
        VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<int32>( reinterpret_cast<int32&>(m_frontFaceWinding) ) );
        VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValueVector<uint32>( Indices() ) );
        VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValueVector<StandardVertex>( Vertices() ) );
        int32 partCount = 0; VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<int32>( partCount ) ); assert( partCount <= 1 );
        m_LODParts.resize(1);
        VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<int32>( m_LODParts[0].IndexStart ) );
        VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<int32>( m_LODParts[0].IndexCount ) );
        m_LODParts[0].SwapToNextDistance = std::numeric_limits<float>::max();
        VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<vaGUID>( m_materialID ) );
        VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<vaBoundingBox>( m_boundingBox ) );

        vaBoundingBox aabb;
        vaTriangleMeshTools::CalculateBounds( Vertices( ), aabb, m_boundingSphere );
        // assert( aabb == m_boundingBox );
    }
    else if( fileVersion == 4 )
    {
        VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<int32>( (int32&)m_frontFaceWinding ) );
        VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValueVector<uint32>( Indices( ) ) );
        VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValueVector<StandardVertex>( Vertices( ) ) );
        VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<vaGUID>( m_materialID ) ); 
#ifdef VA_RENDER_MATERIAL_USE_CACHED_FP
        m_materialCachedFP.store( vaFramePtr<vaRenderMaterial>{} );
#endif
        m_LODParts.clear();
        VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValueVector<LODPart>( m_LODParts ) );
        VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<vaBoundingBox>( m_boundingBox ) );
        VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<vaBoundingSphere>( m_boundingSphere ) );
        VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<float>( m_LODDistanceOffsetAdd ) );
        VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<float>( m_LODDistanceOffsetMul ) );
    }
    else
    {
        VA_LOG( L"vaRenderMesh::Load(): unsupported file version" );
        return false;
    }

    MeshSetGPUDataDirty( );

    return true;
}

bool vaRenderMesh::LODPart::Serialize( vaXMLSerializer & serializer )
{
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<int32>( "IndexStart", IndexStart ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<int32>( "IndexCount", IndexCount ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float>( "SwapToNextDistance", SwapToNextDistance ) );
    return true;
}

bool vaRenderMesh::SerializeUnpacked( vaXMLSerializer & serializer, const string & assetFolder )
{
    assetFolder;
    int32 fileVersion = c_renderMeshFileVersion;
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<int32>( "FileVersion", fileVersion ) );
    VERIFY_TRUE_RETURN_ON_FALSE( fileVersion == c_renderMeshFileVersion );


    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<int32>( "FrontFaceWinding", reinterpret_cast<int32&>(m_frontFaceWinding) ) );
    //VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<bool>( "TangentBitangentValid", m_tangentBitangentValid ) );

    if( serializer.IsReading() )
        MeshReset();

    int32 indexCount = (int32)Indices().size();
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<int32>( "IndexCount", indexCount ) );
    if( serializer.IsReading() )
    {
        Indices().resize( indexCount );
        vaFileTools::ReadBuffer( assetFolder + "/Indices.bin", &Indices()[0], sizeof(uint32) * Indices().size() );
    }
    else if( serializer.IsWriting( ) )
    {
        vaFileTools::WriteBuffer( assetFolder + "/Indices.bin", &Indices()[0], sizeof(uint32) * Indices().size() );
    }
    else { assert( false ); return false; }

    int32 vertexCount = (int32)Vertices().size();
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<int32>( "VertexCount", vertexCount ) );
    if( serializer.IsReading() )
    {
        Vertices().resize( vertexCount );
        vaFileTools::ReadBuffer( assetFolder + "/Vertices.bin", &Vertices()[0], sizeof(vaRenderMesh::StandardVertex) * Vertices().size() );
    }
    else if( serializer.IsWriting( ) )
    {
        vaFileTools::WriteBuffer( assetFolder + "/Vertices.bin", &Vertices()[0], sizeof(vaRenderMesh::StandardVertex) * Vertices().size() );
    }
    else { assert( false ); return false; }
        
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaVector3>( "AABBMin", m_boundingBox.Min ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaVector3>( "AABBSize", m_boundingBox.Size ) );

    bool hasSphere = true;
    hasSphere &= serializer.Serialize<vaVector3>( "BSCenter", m_boundingSphere.Center );
    hasSphere &= serializer.Serialize<float>( "BSRadius", m_boundingSphere.Radius );
    if( serializer.IsReading() && !hasSphere )
    {
        vaBoundingBox aabb;
        vaTriangleMeshTools::CalculateBounds( Vertices( ), aabb, m_boundingSphere );
        assert( aabb == m_boundingBox );
    }

    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaGUID>( "MaterialID", m_materialID ) );
    if( serializer.IsWriting( ) )
    {
        shared_ptr<vaRenderMaterial> material = vaUIDObjectRegistrar::Find<vaRenderMaterial>( m_materialID );
        assert( ( ( material == nullptr ) ? ( vaCore::GUIDNull( ) ) : ( material->UIDObject_GetUID( ) ) ) == m_materialID );
    }

    serializer.SerializeArray<LODPart>( "LODParts", m_LODParts );

    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float>( "LODDistanceOffsetAdd", m_LODDistanceOffsetAdd ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float>( "LODDistanceOffsetMul", m_LODDistanceOffsetMul ) );

    assert( m_LODParts.size( ) <= LODPart::MaxLODParts );
    if( m_LODParts.size() > LODPart::MaxLODParts )
        m_LODParts.resize(LODPart::MaxLODParts);

    return true;
}

void vaRenderMesh::RegisterUsedAssetPacks( std::function<void( const vaAssetPack & )> registerFunction )
{
    vaAssetResource::RegisterUsedAssetPacks( registerFunction );
    //for( auto subPart : m_parts )
    {
        shared_ptr<vaRenderMaterial> material = vaUIDObjectRegistrar::Find<vaRenderMaterial>( m_materialID );
        if( material != nullptr )
        {
            material->RegisterUsedAssetPacks( registerFunction );
        }
        else
        {
            // asset is missing?
            assert( m_materialID == vaCore::GUIDNull() );
        }
    }
}

void vaRenderMesh::EnumerateUsedAssets( const std::function<void(vaAsset * asset)> & callback )
{
    callback( GetParentAsset( ) );
    shared_ptr<vaRenderMaterial> material = vaUIDObjectRegistrar::Find<vaRenderMaterial>( m_materialID );
    if( material != nullptr )
    {
        material->EnumerateUsedAssets( callback );
    }
    else
    {
        // asset is missing?
        assert( m_materialID == vaCore::GUIDNull( ) );
    }
}

// create mesh with provided triangle mesh, winding order and material
shared_ptr<vaRenderMesh> vaRenderMesh::Create( shared_ptr<StandardTriangleMesh> & triMesh, vaWindingOrder frontFaceWinding, const vaGUID & materialID, const vaGUID & uid, bool startTrackingUIDObject )
{
    shared_ptr<vaRenderMesh> mesh = triMesh->GetRenderDevice().GetMeshManager().CreateRenderMesh( uid, false );
    if( mesh == nullptr )
    {
        assert( false );
        return nullptr;
    }

    mesh->MeshSet( triMesh->Vertices(), triMesh->Indices() );
    mesh->SetFrontFaceWindingOrder( frontFaceWinding );
    mesh->SetMaterial( materialID );

    if( startTrackingUIDObject )
    {
        assert( vaThreading::IsMainThread( ) ); // warning, potential bug - don't automatically start tracking if adding from another thread; rather finish initialization completely and then manually call UIDObject_Track
        mesh->UIDObject_Untrack();    // needs to be registered to be visible/searchable by various systems such as rendering
    }

    return mesh;
}

shared_ptr<vaRenderMesh> vaRenderMesh::CreateShallowCopy( const vaRenderMesh & copy, const vaGUID& uid, bool startTrackingUIDObject )
{
    shared_ptr<vaRenderMesh> mesh = copy.GetManager().CreateRenderMesh( uid, false );
    if( mesh == nullptr )
    {
        assert( false );
        return nullptr;
    }

    mesh->MeshSet( copy.Vertices(), copy.Indices() );
    mesh->SetFrontFaceWindingOrder( copy.GetFrontFaceWindingOrder() );
    mesh->SetMaterial( copy.GetMaterial() );

    if( startTrackingUIDObject )
    {
        assert( vaThreading::IsMainThread( ) ); // warning, potential bug - don't automatically start tracking if adding from another thread; rather finish initialization completely and then manually call UIDObject_Track
        mesh->UIDObject_Track( );               // needs to be registered to be visible/searchable by various systems such as rendering);    // needs to be registered to be visible/searchable by various systems such as rendering
    }

    return mesh;
}

shared_ptr<vaRenderMesh> vaRenderMesh::Create( vaRenderDevice & device, const vaMatrix4x4 & transform, const std::vector<vaVector3> & vertices, const std::vector<vaVector3> & normals, const std::vector<vaVector2> & texcoords0, const std::vector<vaVector2> & texcoords1, const std::vector<uint32> & indices, vaWindingOrder frontFaceWinding, const vaGUID & uid, bool startTrackingUIDObject )
{
    assert( ( vertices.size( ) == normals.size( ) ) && ( vertices.size( ) == texcoords0.size( ) ) && ( vertices.size( ) == texcoords1.size( ) ) );

    std::vector<vaRenderMesh::StandardVertex> newVertices( vertices.size( ) );

    for( int i = 0; i < (int)vertices.size( ); i++ )
    {
        vaRenderMesh::StandardVertex & svert = newVertices[i];
        svert.Position = vaVector3::TransformCoord( vertices[i], transform );
        svert.Color = 0xFFFFFFFF;
        svert.Normal = vaVector4( vaVector3::TransformNormal( normals[i], transform ), 0.0f );
        svert.TexCoord0 = texcoords0[i];
        svert.TexCoord1 = texcoords1[i];
    }

    shared_ptr<vaRenderMesh> mesh = device.GetMeshManager().CreateRenderMesh( uid, false );
    if( mesh == nullptr )
    {
        assert( false );
        return nullptr;
    }
    mesh->MeshSet( newVertices, indices );
    mesh->SetMaterial( vaGUID::Null );
    mesh->SetFrontFaceWindingOrder( frontFaceWinding );

    if( startTrackingUIDObject )
    {
        assert( vaThreading::IsMainThread( ) ); // warning, potential bug - don't automatically start tracking if adding from another thread; rather finish initialization completely and then manually call UIDObject_Track
        mesh->UIDObject_Track( );               // needs to be registered to be visible/searchable by various systems such as rendering );
    }

    return mesh;
}

#define RMC_DEFINE_DATA                     \
    std::vector<vaVector3>  vertices;            \
    std::vector<vaVector3>  normals;             \
    std::vector<vaVector2>  texcoords0;          \
    std::vector<vaVector2>  texcoords1;          \
    std::vector<uint32>     indices; 

#define RMC_RESIZE_NTTT                     \
    normals.resize( vertices.size() );      \
    texcoords0.resize( vertices.size( ) );  \
    texcoords1.resize( vertices.size( ) );  \

shared_ptr<vaRenderMesh> vaRenderMesh::CreatePlane( vaRenderDevice & device, const vaMatrix4x4 & transform, float sizeX, float sizeY, bool doubleSided, const vaGUID & uid )
{
    RMC_DEFINE_DATA;

    vaStandardShapes::CreatePlane( vertices, indices, sizeX, sizeY, doubleSided );
    vaWindingOrder windingOrder = vaWindingOrder::CounterClockwise;

    RMC_RESIZE_NTTT;

    vaTriangleMeshTools::GenerateNormals( normals, vertices, indices, windingOrder );

    for( size_t i = 0; i < vertices.size( ); i++ )
    {
        texcoords0[i] = vaVector2( vertices[i].x / sizeX / 2.0f + 0.5f, vertices[i].y / sizeY / 2.0f + 0.5f );
        texcoords1[i] = texcoords0[i];
    }

    return Create( device, transform, vertices, normals, texcoords0, texcoords1, indices, vaWindingOrder::CounterClockwise, uid );
}

shared_ptr<vaRenderMesh> vaRenderMesh::CreateGrid( vaRenderDevice & device, const vaMatrix4x4 & transform, int dimX, int dimY, float sizeX, float sizeY, const vaVector2 & uvOffsetMul, const vaVector2 & uvOffsetAdd, const vaGUID & uid )
{
    RMC_DEFINE_DATA;

    vaStandardShapes::CreateGrid( vertices, indices, dimX, dimY, sizeX, sizeY );
    vaWindingOrder windingOrder = vaWindingOrder::CounterClockwise;

    RMC_RESIZE_NTTT;

    vaTriangleMeshTools::GenerateNormals( normals, vertices, indices, windingOrder );

    for( size_t i = 0; i < vertices.size( ); i++ )
    {
        // texel-to-pixel mapping
        texcoords0[i] = vaVector2( vertices[i].x / sizeX + 0.5f, vertices[i].y / sizeY + 0.5f );
        // custom UV
        texcoords1[i] = vaVector2( vaVector2::ComponentMul( texcoords0[i], uvOffsetMul ) + uvOffsetAdd );
    }

    return Create( device, transform, vertices, normals, texcoords0, texcoords1, indices, vaWindingOrder::CounterClockwise, uid );
}

// dummy texture coords
void FillDummyTT( const std::vector<vaVector3> & vertices, const std::vector<vaVector3> & normals, std::vector<vaVector2> & texcoords0, std::vector<vaVector2> & texcoords1 )
{
    normals;
    for( size_t i = 0; i < vertices.size( ); i++ )
    {
        texcoords0[i] = vaVector2( vertices[i].x / 2.0f + 0.5f, vertices[i].y / 2.0f + 0.5f );
        texcoords1[i] = vaVector2( 0.0f, 0.0f );
    }
}

shared_ptr<vaRenderMesh> vaRenderMesh::CreateTetrahedron( vaRenderDevice & device, const vaMatrix4x4 & transform, bool shareVertices, const vaGUID & uid )
{
    RMC_DEFINE_DATA;

    vaStandardShapes::CreateTetrahedron( vertices, indices, shareVertices );
    vaWindingOrder windingOrder = vaWindingOrder::Clockwise;

    RMC_RESIZE_NTTT;

    vaTriangleMeshTools::GenerateNormals( normals, vertices, indices, windingOrder );

    FillDummyTT( vertices, normals, texcoords0, texcoords1 );

    return Create( device, transform, vertices, normals, texcoords0, texcoords1, indices, windingOrder, uid );
}

shared_ptr<vaRenderMesh> vaRenderMesh::CreateCube( vaRenderDevice & device, const vaMatrix4x4 & transform, bool shareVertices, float edgeHalfLength, const vaGUID & uid )
{
    RMC_DEFINE_DATA;

    vaStandardShapes::CreateCube( vertices, indices, shareVertices, edgeHalfLength );
    vaWindingOrder windingOrder = vaWindingOrder::Clockwise;

    RMC_RESIZE_NTTT;

    vaTriangleMeshTools::GenerateNormals( normals, vertices, indices, windingOrder );

    FillDummyTT( vertices, normals, texcoords0, texcoords1 );

    return Create( device, transform, vertices, normals, texcoords0, texcoords1, indices, windingOrder, uid );
}

shared_ptr<vaRenderMesh> vaRenderMesh::CreateOctahedron( vaRenderDevice & device, const vaMatrix4x4 & transform, bool shareVertices, const vaGUID & uid )
{
    RMC_DEFINE_DATA;

    vaStandardShapes::CreateOctahedron( vertices, indices, shareVertices );
    vaWindingOrder windingOrder = vaWindingOrder::Clockwise;

    RMC_RESIZE_NTTT;

    vaTriangleMeshTools::GenerateNormals( normals, vertices, indices, windingOrder );

    FillDummyTT( vertices, normals, texcoords0, texcoords1 );

    return Create( device, transform, vertices, normals, texcoords0, texcoords1, indices, windingOrder, uid );
}

shared_ptr<vaRenderMesh> vaRenderMesh::CreateIcosahedron( vaRenderDevice & device, const vaMatrix4x4 & transform, bool shareVertices, const vaGUID & uid )
{
    RMC_DEFINE_DATA;

    vaStandardShapes::CreateIcosahedron( vertices, indices, shareVertices );
    vaWindingOrder windingOrder = vaWindingOrder::Clockwise;

    RMC_RESIZE_NTTT;

    vaTriangleMeshTools::GenerateNormals( normals, vertices, indices, windingOrder );

    FillDummyTT( vertices, normals, texcoords0, texcoords1 );

    return Create( device, transform, vertices, normals, texcoords0, texcoords1, indices, windingOrder, uid );
}

shared_ptr<vaRenderMesh> vaRenderMesh::CreateDodecahedron( vaRenderDevice & device, const vaMatrix4x4 & transform, bool shareVertices, const vaGUID & uid )
{
    RMC_DEFINE_DATA;

    vaStandardShapes::CreateDodecahedron( vertices, indices, shareVertices );
    vaWindingOrder windingOrder = vaWindingOrder::Clockwise;

    RMC_RESIZE_NTTT;

    vaTriangleMeshTools::GenerateNormals( normals, vertices, indices, windingOrder );

    FillDummyTT( vertices, normals, texcoords0, texcoords1 );

    return Create( device, transform, vertices, normals, texcoords0, texcoords1, indices, windingOrder, uid );
}

shared_ptr<vaRenderMesh> vaRenderMesh::CreateSphere( vaRenderDevice & device, const vaMatrix4x4 & transform, int tessellationLevel, bool shareVertices, const vaGUID & uid )
{
    RMC_DEFINE_DATA;

    vaStandardShapes::CreateSphereUVWrapped( vertices, indices, texcoords0, tessellationLevel, shareVertices );
    vaWindingOrder windingOrder = vaWindingOrder::Clockwise;

    normals.resize( vertices.size() );  // RMC_RESIZE_NTTT;
    texcoords1.resize( vertices.size(), vaVector2( 0.0f, 0.0f ) );

    vaTriangleMeshTools::GenerateNormals( normals, vertices, indices, windingOrder );

    if( shareVertices )
        vaTriangleMeshTools::MergeNormalsForEqualPositions( normals, vertices );

    // FillDummyTT( vertices, normals, texcoords0, texcoords1 );

    return Create( device, transform, vertices, normals, texcoords0, texcoords1, indices, windingOrder, uid );
}

shared_ptr<vaRenderMesh> vaRenderMesh::CreateCylinder( vaRenderDevice & device, const vaMatrix4x4 & transform, float height, float radiusBottom, float radiusTop, int tessellation, bool openTopBottom, bool shareVertices, const vaGUID & uid )
{
    RMC_DEFINE_DATA;

    vaStandardShapes::CreateCylinder( vertices, indices, height, radiusBottom, radiusTop, tessellation, openTopBottom, shareVertices );
    vaWindingOrder windingOrder = vaWindingOrder::Clockwise;

    RMC_RESIZE_NTTT;

    vaTriangleMeshTools::GenerateNormals( normals, vertices, indices, windingOrder );

    FillDummyTT( vertices, normals, texcoords0, texcoords1 );

    return Create( device, transform, vertices, normals, texcoords0, texcoords1, indices, windingOrder, uid );
}

shared_ptr<vaRenderMesh> vaRenderMesh::CreateTeapot( vaRenderDevice & device, const vaMatrix4x4 & transform, const vaGUID & uid )
{
    RMC_DEFINE_DATA;

    vaStandardShapes::CreateTeapot( vertices, indices );
    vaWindingOrder windingOrder = vaWindingOrder::Clockwise;

    RMC_RESIZE_NTTT;

    vaTriangleMeshTools::GenerateNormals( normals, vertices, indices, windingOrder );

    FillDummyTT( vertices, normals, texcoords0, texcoords1 );

    return Create( device, transform, vertices, normals, texcoords0, texcoords1, indices, windingOrder, uid );
}

std::vector<vaVertexInputElementDesc> vaRenderMesh::GetStandardInputLayout( )
{
    std::vector<vaVertexInputElementDesc> inputElements;
    inputElements.push_back( { "SV_Position", 0,    vaResourceFormat::R32G32B32_FLOAT,       0, vaVertexInputElementDesc::AppendAlignedElement, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );
    inputElements.push_back( { "COLOR", 0,          vaResourceFormat::R8G8B8A8_UNORM,        0, vaVertexInputElementDesc::AppendAlignedElement, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );
    inputElements.push_back( { "NORMAL", 0,         vaResourceFormat::R32G32B32A32_FLOAT,    0, vaVertexInputElementDesc::AppendAlignedElement, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );
    inputElements.push_back( { "TEXCOORD", 0,       vaResourceFormat::R32G32B32A32_FLOAT,    0, vaVertexInputElementDesc::AppendAlignedElement, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );
    return inputElements;
}

bool vaRenderMesh::UIPropertiesDraw( vaApplicationBase & application )
{
    bool hadChanges = false;

    // Use fixed width for labels (by passing a negative value), the rest goes to widgets. We choose a width proportional to our font size.
    VA_GENERIC_RAII_SCOPE( ImGui::PushItemWidth( ImGui::GetFontSize( ) * 18 );, ImGui::PopItemWidth(); );

    std::unique_lock lock( Mutex() );

    application;
    ImGui::Text( "Number of vertices: %d", (int)Vertices( ).size( ) );
    ImGui::Text( "Number of triangles:  %d", (int)Indices( ).size( ) / 3 );

    ImGui::Separator( );

    if( ImGui::CollapsingHeader( "Level(s) of detail" ) )
    {
        {
            ImGui::Text( "Selection settings (runtime)", (int)m_LODParts.size( ) );

            VA_GENERIC_RAII_SCOPE( ImGui::Indent( ); , ImGui::Unindent( ); );
            float prevVal = m_LODDistanceOffsetMul;
            ImGui::InputFloat( "Distance swap multiplier", &m_LODDistanceOffsetMul, 0.1f, 1.0f );
            m_LODDistanceOffsetMul = vaMath::Clamp( m_LODDistanceOffsetMul, 0.0f, 1000.0f );
            hadChanges |= prevVal != m_LODDistanceOffsetMul;
        }

        ImGui::Separator();

        {
            ImGui::Text( "Editing", (int)m_LODParts.size( ) );

            VA_GENERIC_RAII_SCOPE( ImGui::Indent( );, ImGui::Unindent( ); );

            static float s_maxPosError = 0.0005f;
            ImGui::InputFloat( "Max pos error", &s_maxPosError, 0.001f, 0.01f, "%.4f" );
            s_maxPosError = vaMath::Clamp( s_maxPosError, 0.0f, 0.1f );

            static float s_mergeAngleThresholdRN = 20.0f;
            ImGui::InputFloat( "Smoothen normals angle threshold", &s_mergeAngleThresholdRN, 1.0f, 10.0f );
            s_mergeAngleThresholdRN = vaMath::Clamp( s_mergeAngleThresholdRN, 0.0f, 180.0f );

            if( ImGui::Button( "(Re)build LODs" ) )
            {
                RebuildLODs( s_maxPosError, s_mergeAngleThresholdRN / 180.0f * VA_PIf );
                hadChanges = true;
            }

            if( ImGui::Button( "Clear LODs" ) )
            {
                ClearLODs( );
                hadChanges = true;
            }
        }

        ImGui::Separator();

        {
            ImGui::Text( "Current LOD meshes (%d)", (int)m_LODParts.size( ) );

            VA_GENERIC_RAII_SCOPE( ImGui::Indent( ); , ImGui::Unindent( ); );

            for( int i = 0; i < m_LODParts.size( ); i++ )
            {
                const auto& lodPart = m_LODParts[i];
                string label = vaStringTools::Format( "LOD %02d, triangles: %d", i, lodPart.IndexCount / 3 );
                bool open = ImGui::CollapsingHeader( label.c_str( ) );
                if( ImGui::IsItemHovered( ) )
                {
                    m_overrideLODLevel = (float)i;
                    m_overrideLODLevelLastAppTickID = application.GetCurrentTickIndex() + 1;
                }
                if( open )
                {
                    VA_GENERIC_RAII_SCOPE( ImGui::Indent( ); , ImGui::Unindent( ); );

                    ImGui::Text( "Index start:        %d", lodPart.IndexStart );
                    ImGui::Text( "Index count:        %d", lodPart.IndexCount );
                    ImGui::Text( "Swap-to-next-dist:  %.2f", lodPart.SwapToNextDistance );

                    m_overrideLODLevel = (float)i;
                    m_overrideLODLevelLastAppTickID = application.GetCurrentTickIndex() + 1;
                }
            }
        }
    }

    ImGui::Separator();

    ImGui::Text( "AA Bounding box:" );
    ImGui::Text( "  min{%.2f,%.2f,%.2f}, size{%.2f,%.2f,%.2f}", m_boundingBox.Min.x, m_boundingBox.Min.y, m_boundingBox.Min.z, m_boundingBox.Size.x, m_boundingBox.Size.y, m_boundingBox.Size.z );

    ImGui::Separator( );

    if( ImGui::Combo( "Front face winding order", (int*)&m_frontFaceWinding, "Undefined\0Clockwise\0CounterClockwise\0\0" ) )
    {
        hadChanges = true;
        m_frontFaceWinding = (vaWindingOrder)vaMath::Clamp( (int)m_frontFaceWinding, 1, 2 );
        m_gpuDataDirty = true;
    }
    ImGui::Text( "(TODO: add 'force wireframe' here)");

    ImGui::Separator();
    if( ImGui::CollapsingHeader( "Mesh tools", ImGuiTreeNodeFlags_Framed /*| ImGuiTreeNodeFlags_DefaultOpen*/ ) )
    {
        if( ImGui::CollapsingHeader( "Normals", ImGuiTreeNodeFlags_Framed /*| ImGuiTreeNodeFlags_DefaultOpen*/ ) )
        {
            static float s_mergeAngleThresholdRN = 20.0f;
            ImGui::InputFloat( "Smoothen normals angle threshold", &s_mergeAngleThresholdRN, 1.0f, 10.0f );
            s_mergeAngleThresholdRN = vaMath::Clamp( s_mergeAngleThresholdRN, 0.0f, 180.0f );
            if( ImGui::Button( "Rebuild normals", {-1, 0 } ) )
            {
                RebuildNormals( 0, 0, s_mergeAngleThresholdRN / 180.0f * VA_PIf );
                hadChanges = true;
            }
        }
        if( ImGui::CollapsingHeader( "Transform", ImGuiTreeNodeFlags_Framed /*| ImGuiTreeNodeFlags_DefaultOpen*/ ) )
        {
            static vaMatrix4x4 transform = vaMatrix4x4::Identity;
            ImGuiEx_Transform( "Transform", transform, false, false );
            if( ImGui::Button( "Apply", {-1, 0 } ) )
            {
                Transform( transform );

                hadChanges = true;
            }
        }

        if( ImGui::CollapsingHeader( "Extrude", ImGuiTreeNodeFlags_Framed /*| ImGuiTreeNodeFlags_DefaultOpen*/ ) )
        {
            ImGui::Text( "Warning: this is very experimental / rudimentary. Doesn't actually \nremove vertices from the original, just makes them degenerate.\n" );

            static vaBoundingBox area( {-1.0f, -1.0f, -1.0f }, { 2.0f, 2.0f, 2.0f } );
            ImGui::InputFloat3( "Box Min", &area.Min.x );
            ImGui::InputFloat3( "Box Size", &area.Size.x );
            
            vaDebugCanvas3D & canvas3D = application.GetRenderDevice( ).GetCanvas3D( );
            canvas3D.DrawBox( area, 0x80202020, 0x30808010 );

            if( ImGui::Button( "Apply", { -1, 0 } ) )
            {
                Extrude( area, "extruded_mesh_00" );

                hadChanges = true;
            }
        }

        if( ImGui::CollapsingHeader( "Tessellate", ImGuiTreeNodeFlags_Framed /*| ImGuiTreeNodeFlags_DefaultOpen*/ ) )
        {
            ImGui::Text( "Warning: this is very experimental / rudimentary, uses CurvedPNTriangle approach" );
            ImGui::Text( "It will take LOD0, tessellate it into a 4x more detailed mesh and shift all" );
            ImGui::Text( "LODs by one place and make it a new LOD0" );

            if( ImGui::Button( "Apply", { -1, 0 } ) )
            {
                TNTesselate();

                hadChanges = true;
            }
        }

        // ImGui::Separator();
        //
        // static float s_mergePosDiffThreshold    = 0.0001f;
        // static float s_mergeAngleThreshold      = 15.0f;
        // if( ImGui::Button( "Merge similar verts (experimental, also clears LODs)" ) )
        // {
        //     ClearLODs( );
        //     MergeSimilarVerts( 0, 0, s_mergePosDiffThreshold, s_mergeAngleThreshold / 180.0f * VA_PIf );
        //     hadChanges = true;
        // }
        // ImGui::InputFloat( "Distance threshold", &s_mergePosDiffThreshold, 0.01f, 0.01f, "%.5f" );
        // s_mergePosDiffThreshold = vaMath::Clamp( s_mergePosDiffThreshold, 0.0f, 1000.0f );
        // ImGui::InputFloat( "Angle threshold", &s_mergeAngleThreshold, 1.0f, 10.0f );
        // s_mergeAngleThreshold = vaMath::Clamp( s_mergeAngleThreshold, 0.0f, 180.0f );
        // ImGui::Text( "Warning: this doesn't really work with UVs - that needs to be added too" );
    }
    ImGui::Separator( );

    hadChanges |= vaAssetPackManager::UIAssetLinkWidget<vaAssetRenderMaterial>( "material_asset", m_materialID ); 
#ifdef VA_RENDER_MATERIAL_USE_CACHED_FP
    m_materialCachedFP.store( vaFramePtr<vaRenderMaterial>{} );
#endif

    return hadChanges;
}

#if 0 // this is gone to reduce complexity
vaDrawResultFlags vaRenderMeshManager::DrawSimple( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, vaRenderMaterialShaderType shaderType, const vaDrawAttributes & _drawAttributes, vaBlendMode blendMode, vaRenderMeshDrawFlags drawFlags, const std::vector<vaRenderInstanceSimple> & instances )
{
    VA_TRACE_CPUGPU_SCOPE( DrawSingle, renderContext );

    vaDrawAttributes drawAttributes = _drawAttributes;

    // If using materials then lighting is needed (at least to set empty lighting constant buffer) as shaders expect it; if this is a problem, it can be
    // fixed but a fix is required or there will be crashes (such as when trying to access g_LocalIBLReflectionsMap). If you don't believe me (I'm talking
    // to future myself here :P), jere's the GPU-BASED VALIDATION report from one of the related crashes:
    // D12 ERROR: GPU-BASED VALIDATION: Draw, Uninitialized descriptor accessed: Descriptor Heap Index To DescriptorTableStart: [54180], Descriptor Heap Index FromTableStart: [16], Register Type: D3D12_DESCRIPTOR_RANGE_TYPE_SRV, Index of Descriptor Range: 2, Shader Stage: PIXEL, Root Parameter Index: [6], Draw Index: [1194], Shader Code: C:\Work\INTC_SHARE\vanilla\Source\Rendering\Shaders/Filament\light_indirect.va.fs(113,79-79), Asm Instruction Range: [0x2b-0xffffffff], Asm Operand Index: [0], Command List: 0x00000157F79C5EF0:'MasterList', SRV/UAV/CBV Descriptor Heap: 0x00000157E50E5ED0:'DefaultPersistentHeap_CBV_SRV_UAV', Sampler Descriptor Heap: 0x00000157E5251E40:'DefaultPersistentHeap_Sampler', Pipeline State: 0x0000015861715990:'Unnamed ID3D12PipelineState Object',  [ EXECUTION ERROR #938: GPU_BASED_VALIDATION_DESCRIPTOR_UNINITIALIZED]
    assert( drawAttributes.Lighting != nullptr || shaderType == vaRenderMaterialShaderType::DepthOnly );

    vaDrawResultFlags drawResults = vaDrawResultFlags::None;

    if( instances.size() > m_simpleConstantBuffers->GetElementCount() )
    {
        assert( false );    // that many instances not supported - submit as smaller batches or upgrade stuff here (why are you even doing this? it's going to be slow)
        return vaDrawResultFlags::UnspecifiedError;
    }

    assert( drawAttributes.BaseGlobals.ShaderResourceViews[SHADERGLOBAL_INSTANCE_CONSTANTBUFFERS_TEXTURESLOT] == nullptr );
    drawAttributes.BaseGlobals.ShaderResourceViews[SHADERGLOBAL_INSTANCE_CONSTANTBUFFERS_TEXTURESLOT] = m_simpleConstantBuffers;

    for( int instanceIndex = 0; instanceIndex < instances.size(); instanceIndex++ )
    {
        const vaRenderInstanceSimple & instance = instances[instanceIndex];
        auto mesh = instance.Mesh;

        if( mesh == nullptr )
            { assert( false ); return vaDrawResultFlags::UnspecifiedError; }

        bool skipNonShadowCasters   = (drawFlags & vaRenderMeshDrawFlags::SkipNonShadowCasters  )   != 0;
        bool enableDepthTest        = (drawFlags & vaRenderMeshDrawFlags::EnableDepthTest       )   != 0;
        bool invertDepthTest        = (drawFlags & vaRenderMeshDrawFlags::InvertDepthTest       )   != 0;
        bool enableDepthWrite       = (drawFlags & vaRenderMeshDrawFlags::EnableDepthWrite      )   != 0;
        bool depthTestIncludesEqual = (drawFlags & vaRenderMeshDrawFlags::DepthTestIncludesEqual)   != 0;
        bool depthTestEqualOnly     = (drawFlags & vaRenderMeshDrawFlags::DepthTestEqualOnly    )   != 0;

        bool depthEnable            = enableDepthTest || enableDepthWrite;
        bool useReversedZ           = (invertDepthTest)?(!drawAttributes.Camera.GetUseReversedZ()):(drawAttributes.Camera.GetUseReversedZ());

        vaComparisonFunc depthFunc  = vaComparisonFunc::Always;
        if( enableDepthTest )
        {
            if( !depthTestEqualOnly )
                depthFunc               = ( depthTestIncludesEqual ) ? ( ( useReversedZ )?( vaComparisonFunc::GreaterEqual ):( vaComparisonFunc::LessEqual ) ):( ( useReversedZ )?( vaComparisonFunc::Greater ):( vaComparisonFunc::Less ) );
            else
                depthFunc               = vaComparisonFunc::Equal;
        }

        vaGraphicsItem renderItem;

        renderItem.BlendMode        = blendMode;
        renderItem.DepthFunc        = depthFunc;
        renderItem.Topology         = vaPrimitiveTopology::TriangleList;
        renderItem.DepthEnable      = depthEnable;
        renderItem.DepthWriteEnable = enableDepthWrite;
        //renderItem.ConstantBuffers[SHADERINSTANCE_CONSTANTSBUFFERSLOT] = m_constantBuffer.GetBuffer();
        renderItem.InstanceIndex    = instanceIndex;

        
        // should probably be modifiable by the material as well?
        renderItem.ShadingRate = ( ( drawFlags & vaRenderMeshDrawFlags::DisableVRS ) == 0 ) ? ( instance.ShadingRate ) : ( vaShadingRate::ShadingRate1X1 );

        bool isTransparent = false;
        bool isWireframe = ( ( drawAttributes.RenderFlagsAttrib & vaDrawAttributes::RenderFlags::DebugWireframePass ) != 0 );

        //bool isTransparent = false;
        vaExecuteItemFlags executeItemFlags = vaExecuteItemFlags::None;

        // Mesh part
        {
            mesh->PreRenderUpdate( renderContext );

            // read lock
            std::shared_lock meshLock( mesh->Mutex( ) );

            renderItem.VertexBuffer                 = mesh->GetGPUVertexBufferFP( );
            renderItem.IndexBuffer                  = mesh->GetGPUIndexBufferFP( );
            renderItem.FrontCounterClockwise        = mesh->GetFrontFaceWindingOrder( ) == vaWindingOrder::CounterClockwise;
                
            const std::vector<vaRenderMesh::LODPart> & LODParts = mesh->GetLODParts();
            const vaRenderMesh::LODPart & LODPart = LODParts[(int)instance.MeshLOD];

            renderItem.SetDrawIndexed( LODPart.IndexCount, LODPart.IndexStart, 0 );
        }

        // Material part
        {
            vaRenderMaterialData materialData;

            if( !instance.Material->PreRenderUpdate( renderContext ) )
                return drawResults | vaDrawResultFlags::AssetsStillLoading;

            // read lock
            std::shared_lock materialLock( instance.Material->Mutex( ) );

            if( !instance.Material->SetToRenderData( materialData, drawResults, shaderType, materialLock ) )
                return drawResults | vaDrawResultFlags::AssetsStillLoading;

            auto RenderData       = materialData;
            //auto MaterialAssetID  = ( material->GetParentAsset( ) == nullptr ) ? ( 0xFFFFFFFF ) : ( static_cast<uint32>( material->GetParentAsset( )->RuntimeIDGet( ) ) );
            //auto ShowAsSelected   = material->GetUIShowSelectedFrameIndex( ) >= currentFrameCounter;

            isWireframe     |=  RenderData.IsWireframe;
            isTransparent   =   RenderData.IsTransparent;
            //showAsSelected  |=  ShowAsSelected;

            if(     (renderItem.VertexShader   == RenderData.VertexShader )
                &&  (renderItem.GeometryShader == RenderData.GeometryShader)
                &&  (renderItem.HullShader     == RenderData.HullShader)
                &&  (renderItem.DomainShader   == RenderData.DomainShader)
                &&  (renderItem.PixelShader    == RenderData.PixelShader) )
                executeItemFlags |= vaExecuteItemFlags::ShadersUnchanged;
            RenderData.Apply( renderItem );

            if( skipNonShadowCasters )
            {
                // this is not good at all for multithreading <shrug>
                if( !RenderData.CastShadows )
                    return vaDrawResultFlags::None;
            }
        }

        // update per-instance constants
        {
            ShaderInstanceConstants instanceConsts;

            instance.WriteToShaderConstants( instanceConsts );

            // we have a constant buffer per thread/worker - that's why this is threadsafe
            // m_constantBuffer.Upload( renderContext, instanceConsts );
            m_simpleConstantBuffers->UploadSingle( renderContext, instanceConsts, instanceIndex );
        }

        renderItem.FillMode                 = (isWireframe)?(vaFillMode::Wireframe):(vaFillMode::Solid);

        drawResults |= renderContext.ExecuteSingleItem( renderItem, renderOutputs, &drawAttributes );
    }
    return drawResults;
}
#endif

vaDrawResultFlags vaRenderMeshManager::Draw( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, vaRenderMaterialShaderType shaderType, const vaDrawAttributes & _drawAttributes, const vaRenderInstanceList & selection, vaBlendMode blendMode, vaRenderMeshDrawFlags drawFlags, 
    vaRenderInstanceList::SortHandle sortHandle/*, GlobalCustomizerType globalCustomizer*/ )
{
    VA_TRACE_CPUGPU_SCOPE( DrawMeshes, renderContext );

    vaDrawAttributes drawAttributes = _drawAttributes;

    // If using materials then lighting is needed (at least to set empty lighting constant buffer) as shaders expect it; if this is a problem, it can be
    // fixed but a fix is required or there will be crashes (such as when trying to access g_LocalIBLReflectionsMap). If you don't believe me (I'm talking
    // to future myself here :P), jere's the GPU-BASED VALIDATION report from one of the related crashes:
    // D12 ERROR: GPU-BASED VALIDATION: Draw, Uninitialized descriptor accessed: Descriptor Heap Index To DescriptorTableStart: [54180], Descriptor Heap Index FromTableStart: [16], Register Type: D3D12_DESCRIPTOR_RANGE_TYPE_SRV, Index of Descriptor Range: 2, Shader Stage: PIXEL, Root Parameter Index: [6], Draw Index: [1194], Shader Code: C:\Work\INTC_SHARE\vanilla\Source\Rendering\Shaders/Filament\light_indirect.va.fs(113,79-79), Asm Instruction Range: [0x2b-0xffffffff], Asm Operand Index: [0], Command List: 0x00000157F79C5EF0:'MasterList', SRV/UAV/CBV Descriptor Heap: 0x00000157E50E5ED0:'DefaultPersistentHeap_CBV_SRV_UAV', Sampler Descriptor Heap: 0x00000157E5251E40:'DefaultPersistentHeap_Sampler', Pipeline State: 0x0000015861715990:'Unnamed ID3D12PipelineState Object',  [ EXECUTION ERROR #938: GPU_BASED_VALIDATION_DESCRIPTOR_UNINITIALIZED]
    assert( drawAttributes.Lighting != nullptr || shaderType == vaRenderMaterialShaderType::DepthOnly || shaderType == vaRenderMaterialShaderType::RichPrepass );

    //int64 currentFrameCounter = GetRenderDevice( ).GetCurrentFrameIndex( );

    // bool skipTransparencies     = (drawFlags & vaRenderMeshDrawFlags::SkipTransparencies    )   != 0;
    // bool skipNonTransparencies  = (drawFlags & vaRenderMeshDrawFlags::SkipNonTransparencies )   != 0;
    bool skipNonShadowCasters   = (drawFlags & vaRenderMeshDrawFlags::SkipNonShadowCasters  )   != 0;
    //bool skipNoDepthPrePass     = (drawFlags & vaRenderMeshDrawFlags::SkipNoDepthPrePass )      != 0;
    
    bool enableDepthTest        = (drawFlags & vaRenderMeshDrawFlags::EnableDepthTest       )   != 0;
    bool invertDepthTest        = (drawFlags & vaRenderMeshDrawFlags::InvertDepthTest       )   != 0;
    bool enableDepthWrite       = (drawFlags & vaRenderMeshDrawFlags::EnableDepthWrite      )   != 0;
    bool depthTestIncludesEqual = (drawFlags & vaRenderMeshDrawFlags::DepthTestIncludesEqual)   != 0;
    bool depthTestEqualOnly     = (drawFlags & vaRenderMeshDrawFlags::DepthTestEqualOnly)   != 0;

    bool depthEnable            = enableDepthTest || enableDepthWrite;
    bool useReversedZ           = (invertDepthTest)?(!drawAttributes.Camera.GetUseReversedZ()):(drawAttributes.Camera.GetUseReversedZ());

    vaComparisonFunc depthFunc  = vaComparisonFunc::Always;
    if( enableDepthTest )
    {
        if( !depthTestEqualOnly )
            depthFunc               = ( depthTestIncludesEqual ) ? ( ( useReversedZ )?( vaComparisonFunc::GreaterEqual ):( vaComparisonFunc::LessEqual ) ):( ( useReversedZ )?( vaComparisonFunc::Greater ):( vaComparisonFunc::Less ) );
        else
            depthFunc               = vaComparisonFunc::Equal;
    }

    // set up thingies common to all render items (we could actually set fewer if not all worker threads are used, but for now - who cares)
    {
        assert( m_renderDevice.GetTotalContextCount( ) > 0 );
        if( m_perWorkerData.size() != m_renderDevice.GetTotalContextCount() )
        {
            m_perWorkerData.resize( m_renderDevice.GetTotalContextCount( ) );
            // for( int i = 0; i < m_renderDevice.GetTotalContextCount( ); i++ )
            //     m_perWorkerData[i].ConstantBuffer = std::make_shared< vaTypedConstantBufferWrapper< ShaderInstanceConstants, true > >( m_renderDevice, nullptr, i );
        }

        //VA_TRACE_CPU_SCOPE( Setup );
        for( int i = 0; i < m_perWorkerData.size(); i++ )
        {
            auto & workerData = m_perWorkerData[i];
            // these are shared for all draw calls
            workerData.GraphicsItem.BlendMode        = blendMode;
            workerData.GraphicsItem.DepthFunc        = depthFunc;
            workerData.GraphicsItem.Topology         = vaPrimitiveTopology::TriangleList;
            workerData.GraphicsItem.DepthEnable      = depthEnable;
            workerData.GraphicsItem.DepthWriteEnable = enableDepthWrite;
            //workerData.GraphicsItem.ConstantBuffers[SHADERINSTANCE_CONSTANTSBUFFERSLOT] = *workerData.ConstantBuffer;
        }
        assert( drawAttributes.BaseGlobals.ShaderResourceViews[SHADERGLOBAL_INSTANCE_CONSTANTBUFFERS_TEXTURESLOT] == nullptr );
        drawAttributes.BaseGlobals.ShaderResourceViews[SHADERGLOBAL_INSTANCE_CONSTANTBUFFERS_TEXTURESLOT] = selection.GetGlobalInstanceRenderBuffer();
    }

    auto list                           = selection.GetItems();
    auto sortIndices                    = selection.GetSortIndices( sortHandle );
    const vaRenderInstance * globalList = selection.GetGlobalInstanceArray( );      // indexed by item.InstanceIndex

    if( sortIndices != nullptr ) 
    {
        if ( (*sortIndices).size() != list.second )
        {
            // this is a crash bug
            assert( false );
            return vaDrawResultFlags::UnspecifiedError;
        }
    }

    // We can capture local context for the lambda this way to reduce std::function causing allocations; it's safe because
    // we won't leave this function scope until all workers finish!
    struct ManualContextCapture
    {
        std::pair< const vaRenderInstanceList::Item *, size_t > List;
        const vaRenderInstance *                    GlobalList;
        const std::vector< int > *                  SortIndices;
        vaDrawAttributes                            DrawAttributes;
        std::vector<PerWorkerData> &                WorkerDataArray;
        //GlobalCustomizerType &                      GlobalCustomizer;
        bool                                        SkipNonShadowCasters;
        vaRenderMeshDrawFlags                       DrawFlags;
        vaRenderMaterialShaderType                  ShaderType;
        //int64                                       CurrentFrameCounter;
    } globalContext = { list, globalList, sortIndices, drawAttributes, m_perWorkerData, /*globalCustomizer,*/ skipNonShadowCasters, drawFlags, shaderType/*, currentFrameCounter*/ };

    /*vaRenderDeviceContext::GraphicsItemCallback*/
    auto callback = 
        [ &globalContext ]
        ( int index, vaRenderDeviceContext & workerRenderContext ) noexcept -> vaDrawResultFlags 
    {
        int ii = index;
        if( globalContext.SortIndices != nullptr )
            ii = (*globalContext.SortIndices)[index];//listSortState.SortedIndices[(reverseOrder)?(list.Count()-1-index):(index)];
        
        // inputs
        const vaRenderInstanceList::Item & localInstance= globalContext.List.first[ii];
        const vaRenderInstance & globalInstance         = globalContext.GlobalList[ localInstance.InstanceIndex ];

        // this reduces copying around
        auto & workerData = globalContext.WorkerDataArray[workerRenderContext.GetInstanceIndex()];
        vaGraphicsItem & renderItem                 = workerData.GraphicsItem;
        //ShaderInstanceConstants & instanceConsts    = workerData.InstanceConsts;
        
        // should probably be modifiable by the material as well?
        renderItem.ShadingRate = ( ( globalContext.DrawFlags & vaRenderMeshDrawFlags::DisableVRS ) == 0 ) ? ( localInstance.ShadingRate ) : ( vaShadingRate::ShadingRate1X1 );

        vaDrawResultFlags drawResults = vaDrawResultFlags::None;
        bool isTransparent = false;
        bool isWireframe = ( ( globalContext.DrawAttributes.RenderFlagsAttrib & vaDrawAttributes::RenderFlags::DebugWireframePass ) != 0 );
        //bool isTransparent = false;
        vaExecuteItemFlags executeItemFlags = vaExecuteItemFlags::None;

        // if the vaFramePtr assert is firing here, it means you could be reusing the render list from previous frame - that's no longer allowed
        if( globalInstance.Mesh == nullptr || globalInstance.Material == nullptr )
            { assert( false ); return vaDrawResultFlags::UnspecifiedError; }

        renderItem.InstanceIndex = localInstance.InstanceIndex;

        renderItem.GenericRootConst = ii;   // this is purely for testing/debugging - feel free to use the GenericRootConst for something else

        // Mesh part
        {
            //VA_TRACE_CPU_SCOPE( MESHSTUFF );
            const PerWorkerData::MeshCacheEntry * cacheEntry = workerData.MeshCache.Find( globalInstance.Mesh.get() );
            if( cacheEntry == nullptr )
            {
                vaRenderMesh & mesh = *globalInstance.Mesh;

                // TODO: replace this lock with a global mesh lock
                // render mesh internal data might need update - this has to be done using unique lock so nobody else does it at the same time
                std::shared_lock meshLock( mesh.Mutex( ) );

                // if( workerRenderContext.IsWorker() )
                //     mesh.UpdateGPUDataIfNeeded( *workerRenderContext.GetMaster(), meshLock );
                // else
                //     mesh.UpdateGPUDataIfNeeded( workerRenderContext, meshLock );

                PerWorkerData::MeshCacheEntry * newCacheEntry = workerData.MeshCache.Insert( globalInstance.Mesh.get() );
                cacheEntry = newCacheEntry;
                
                newCacheEntry->VertexBuffer             = mesh.GetGPUVertexBufferFP( );
                newCacheEntry->IndexBuffer              = mesh.GetGPUIndexBufferFP( );
                newCacheEntry->FrontCounterClockwise    = mesh.GetFrontFaceWindingOrder( ) == vaWindingOrder::CounterClockwise;

                const std::vector<vaRenderMesh::LODPart> & LODParts = mesh.GetLODParts();
                newCacheEntry->LODPartCount             = std::min( (int)LODParts.size(), vaRenderMesh::LODPart::MaxLODParts );
                memcpy( newCacheEntry->LODParts, LODParts.data(), sizeof(vaRenderMesh::LODPart)*newCacheEntry->LODPartCount );
            }

            renderItem.VertexBuffer = cacheEntry->VertexBuffer;
            renderItem.IndexBuffer  = cacheEntry->IndexBuffer;
            renderItem.FrontCounterClockwise = cacheEntry->FrontCounterClockwise;

            const vaRenderMesh::LODPart & LODPart = cacheEntry->LODParts[(int)globalInstance.MeshLOD];
            renderItem.SetDrawIndexed( LODPart.IndexCount, LODPart.IndexStart, 0 );
        }

        // Material part
        {
            const PerWorkerData::MaterialCacheEntry * cacheEntry = workerData.MaterialCache.Find( globalInstance.Material.get( ) );
            if( cacheEntry == nullptr )
            {
                const vaFramePtr<vaRenderMaterial> material = globalInstance.Material;
            
                // read lock
                std::shared_lock materialLock( material->Mutex( ) );

                vaRenderMaterialData materialData;
                // lock might get upgraded to unique here if needed!
                if( !material->SetToRenderData( materialData, drawResults, globalContext.ShaderType, materialLock ) )
                    return drawResults | vaDrawResultFlags::AssetsStillLoading;

                PerWorkerData::MaterialCacheEntry * newCacheEntry = workerData.MaterialCache.Insert( globalInstance.Material.get( ) );
                cacheEntry = newCacheEntry;
                newCacheEntry->RenderData       = materialData;
            }

            isWireframe     |= cacheEntry->RenderData.IsWireframe;
            isTransparent   = cacheEntry->RenderData.IsTransparent;
            //showAsSelected  |= cacheEntry->ShowAsSelected;

            if(     (renderItem.VertexShader   == cacheEntry->RenderData.VertexShader )
                &&  (renderItem.GeometryShader == cacheEntry->RenderData.GeometryShader)
                &&  (renderItem.HullShader     == cacheEntry->RenderData.HullShader)
                &&  (renderItem.DomainShader   == cacheEntry->RenderData.DomainShader)
                &&  (renderItem.PixelShader    == cacheEntry->RenderData.PixelShader) )
                executeItemFlags |= vaExecuteItemFlags::ShadersUnchanged;
            cacheEntry->RenderData.Apply( renderItem );

            if( globalContext.SkipNonShadowCasters )
            {
                // this is not good at all for multithreading <shrug>
                if( !cacheEntry->RenderData.CastShadows )
                    return vaDrawResultFlags::None;
            }

//            renderItem.DepthWriteEnable
        }

        renderItem.FillMode                 = (isWireframe)?(vaFillMode::Wireframe):(vaFillMode::Solid);

#ifdef VA_AUTO_TWO_PASS_TRANSPARENCIES_ENABLED
        if( materialSettings.Transparent && materialSettings.FaceCull != vaFaceCull::None )
        {
            renderItem.CullMode = ( materialSettings.FaceCull == vaFaceCull::Front ) ? ( vaFaceCull::Back ) : ( vaFaceCull::Front );
            drawResults |= drawContext.RenderDeviceContext.ExecuteItem( renderItem );
            renderItem.CullMode = ( materialSettings.FaceCull == vaFaceCull::Front ) ? ( vaFaceCull::Front ) : ( vaFaceCull::Back );
            drawResults |= drawContext.RenderDeviceContext.ExecuteItem( renderItem );
        }
        else
#endif
        return drawResults | workerRenderContext.ExecuteItem( renderItem, executeItemFlags );
    };

    vaAssertSits( callback );

    auto retVal = renderContext.ExecuteGraphicsItemsConcurrent( (int)list.second, renderOutputs, &drawAttributes, callback );
    
    {
        //VA_TRACE_CPU_SCOPE( RESET );
        for( int i = 0; i < m_perWorkerData.size( ); i++ )
            m_perWorkerData[i].Reset();
    }

    return retVal;
}

shared_ptr<vaRenderMesh> vaRenderMeshManager::UnitSphere( )
{
    if( m_unitSphere == nullptr )
        m_unitSphere = vaRenderMesh::CreateSphere( GetRenderDevice(), vaMatrix4x4::Identity, 2, true, vaGUID("ee76827b-f32d-43f2-9cbf-9ea587b0c74d") );

    return m_unitSphere;
}