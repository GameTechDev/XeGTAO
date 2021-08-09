///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Core/vaCoreIncludes.h"

#include "vaRendering.h"

#include "vaRenderBuffers.h"

namespace Vanilla
{
    class vaTriangleMeshTools
    {
        vaTriangleMeshTools( ) { }

    public:

        template< typename VertexType >
        static inline int AddVertex( std::vector<VertexType> & outVertices, const VertexType & vert )
        {
            outVertices.push_back( vert );
            return (int)outVertices.size( ) - 1;
        }

        template< typename VertexType >
        static inline int FindOrAdd( std::vector<VertexType> & vertices, const VertexType & vert, int searchBackRange )
        {
            for( int i = (int)( vertices.size( ) ) - 1; ( i >= 0 ) && ( i >= (int)vertices.size( ) - searchBackRange ); i-- )
            {
                if( vertices[i] == vert )
                    return i;
            }

            vertices.push_back( vert );
            return (int)( vertices.size( ) ) - 1;
        }

        template< typename VertexType, typename CompareCallableType >
        static inline int FindOrAdd( std::vector<VertexType> & vertices, const VertexType & vert, int searchBackRange, CompareCallableType && isDuplicate )
        {
            const int beg = (int)( vertices.size( ) ) - 1;
            const int end =  (int)vertices.size( ) - searchBackRange;
            for( int i = beg; ( i >= 0 ) && ( i >= end ); i-- )
            {
                if( isDuplicate( vertices[i], vert ) )
                    return i;
            }

            vertices.push_back( vert );
            return (int)( vertices.size( ) ) - 1;
        }

        static inline void AddTriangle( std::vector<uint32> & outIndices, int a, int b, int c )
        {
            assert( ( a >= 0 ) && ( b >= 0 ) && ( c >= 0 ) );
            outIndices.push_back( a );
            outIndices.push_back( b );
            outIndices.push_back( c );
        }

        template< typename VertexType >
        static inline void AddTriangle( std::vector<VertexType> & outVertices, std::vector<uint32> & outIndices, const VertexType & v0, const VertexType & v1, const VertexType & v2 )
        {
            int i0 = AddVertex( outVertices, v0 );
            int i1 = AddVertex( outVertices, v1 );
            int i2 = AddVertex( outVertices, v2 );

            AddTriangle( outIndices, i0, i1, i2 );
        }

        template< typename VertexType >
        static inline void AddTriangle_MergeSamePositionVertices( std::vector<VertexType> & outVertices, std::vector<uint32> & outIndices, const VertexType & v0, const VertexType & v1, const VertexType & v2, int vertexMergingLookFromVertexOffset = 0 )
        {
            int i0 = FindOrAdd( outVertices, v0, (int)outVertices.size() - vertexMergingLookFromVertexOffset );
            int i1 = FindOrAdd( outVertices, v1, (int)outVertices.size() - vertexMergingLookFromVertexOffset );
            int i2 = FindOrAdd( outVertices, v2, (int)outVertices.size() - vertexMergingLookFromVertexOffset );

            AddTriangle( outIndices, i0, i1, i2 );
        }

        template< typename VertexType, typename CompareCallableType >
        static inline void AddTriangle_MergeDuplicates( std::vector<VertexType> & outVertices, std::vector<uint32> & outIndices, const VertexType & v0, const VertexType & v1, const VertexType & v2, CompareCallableType && isDuplicate, int vertexMergingLookFromVertexOffset = 0 )
        {
            int i0 = FindOrAdd( outVertices, v0, (int)outVertices.size() - vertexMergingLookFromVertexOffset, isDuplicate );
            int i1 = FindOrAdd( outVertices, v1, (int)outVertices.size() - vertexMergingLookFromVertexOffset, isDuplicate );
            int i2 = FindOrAdd( outVertices, v2, (int)outVertices.size() - vertexMergingLookFromVertexOffset, isDuplicate );

            AddTriangle( outIndices, i0, i1, i2 );
        }

        // This adds quad triangles in strip order ( (0, 0), (1, 0), (0, 1), (1, 1) ) - so swap the last two if doing clockwise/counterclockwise
        // (this is a bit inconsistent with AddPentagon below)
        static inline void AddQuad( std::vector<uint32> & outIndices, int i0, int i1, int i2, int i3 )
        {
            outIndices.push_back( i0 );
            outIndices.push_back( i1 );
            outIndices.push_back( i2 );
            outIndices.push_back( i1 );
            outIndices.push_back( i3 );
            outIndices.push_back( i2 );
        }

        // This adds quad triangles in strip order ( (0, 0), (1, 0), (0, 1), (1, 1) ) - so swap the last two if doing clockwise/counterclockwise
        // (this is a bit inconsistent with AddPentagon below)
        template< typename VertexType >
        static inline void AddQuad( std::vector<VertexType> & outVertices, std::vector<uint32> & outIndices, const VertexType & v0, const VertexType & v1, const VertexType & v2, const VertexType & v3 )
        {
            int i0 = AddVertex( outVertices, v0 );
            int i1 = AddVertex( outVertices, v1 );
            int i2 = AddVertex( outVertices, v2 );
            int i3 = AddVertex( outVertices, v3 );

            AddQuad( outIndices, i0, i1, i2, i3 );
        }

        // This adds triangles in clockwise, fan-like order
        // (this is a bit inconsistent with AddQuad above)
        static inline void AddPentagon( std::vector<uint32> & outIndices, int i0, int i1, int i2, int i3, int i4 )
        {
            AddTriangle( outIndices, i0, i1, i2 );
            AddTriangle( outIndices, i0, i2, i3 );
            AddTriangle( outIndices, i0, i3, i4 );
        }

        // This adds triangles in clockwise, fan-like order
        // (this is a bit inconsistent with AddQuad above)
        template< typename VertexType >
        static inline void AddPentagon( std::vector<VertexType> & outVertices, std::vector<uint32> & outIndices, const VertexType & v0, const VertexType & v1, const VertexType & v2, const VertexType & v3, const VertexType & v4 )
        {
            int i0 = AddVertex( outVertices, v0 );
            int i1 = AddVertex( outVertices, v1 );
            int i2 = AddVertex( outVertices, v2 );
            int i3 = AddVertex( outVertices, v3 );
            int i4 = AddVertex( outVertices, v4 );

            AddTriangle( outIndices, i0, i1, i2 );
            AddTriangle( outIndices, i0, i2, i3 );
            AddTriangle( outIndices, i0, i3, i4 );
        }

        template< typename VertexType >
        static inline void TransformPositions( std::vector<VertexType> & vertices, const vaMatrix4x4 & transform )
        {
            for( int i = 0; i < (int)vertices.size( ); i++ )
                vertices[i].Position = vaVector3::TransformCoord( vertices[i].Position, transform );
        }

        static inline void GenerateNormals( std::vector<vaVector3> & outNormals, const std::vector<vaVector3> & vertices, const std::vector<uint32> & indices, vaWindingOrder windingOrder, int indexFrom = 0, int indexCount = -1, bool fixBrokenNormals = true, float mergeSharedMaxAngle = 0.0f )
        {
            bool counterClockwise = windingOrder == vaWindingOrder::CounterClockwise;
            
            assert( outNormals.size() == vertices.size() );
            if( indexCount == -1 )
                indexCount = (int)indices.size( );

            const int vertexCount = (int)vertices.size();

            for( int i = 0; i < vertexCount; i++ )
                outNormals[i] = vaVector3( 0, 0, 0 );

            for( int i = indexFrom; i < indexFrom+indexCount; i += 3 )
            {
                const vaVector3 & a = vertices[indices[i + 0]];
                const vaVector3 & b = vertices[indices[i + 1]];
                const vaVector3 & c = vertices[indices[i + 2]];

                vaVector3 norm;
                if( counterClockwise )
                    norm = vaVector3::Cross( c - a, b - a );
                else
                    norm = vaVector3::Cross( b - a, c - a );

                float triAreaX2 = norm.Length( );
                if( triAreaX2 < VA_EPSf ) 
                {
                    if( !fixBrokenNormals )
                        continue;

                    if( triAreaX2 != 0.0f )
                        norm /= triAreaX2 * 10000.0f;
                }

                // don't normalize, leave it weighted by area
                outNormals[indices[i + 0]] += norm;
                outNormals[indices[i + 1]] += norm;
                outNormals[indices[i + 2]] += norm;
            }

            // Optional normals merge (softening)
            if( mergeSharedMaxAngle > 0 )
            {
                float dotThreshold = std::cosf( mergeSharedMaxAngle );
                std::vector<vaVector3> mergeVals;
                mergeVals.resize( vertexCount, {0,0,0} );
                for( int i = 0; i < vertexCount; i++ )
                    for( int j = i+1; j < vertexCount; j++ )
                    {
                        const vaVector3 & pi = vertices[i];
                        const vaVector3 & pj = vertices[j];
                        if( pi != pj )
                            continue;
                        const vaVector3 & ni = outNormals[i];
                        const vaVector3 & nj = outNormals[j];
                        if( vaVector3::Dot( ni.Normalized(), nj.Normalized() ) > dotThreshold )
                        {
                            mergeVals[i] += nj;
                            mergeVals[j] += ni;
                        }
                    }

                for( int i = 0; i < vertexCount; i++ )
                    outNormals[i] += mergeVals[i];
            }

            for( int i = 0; i < vertexCount; i++ )
            {
                float length = outNormals[i].Length();

                if( length < VA_EPSf )
                    outNormals[i] = vaVector3( 0.0f, 0.0f, (fixBrokenNormals)?(1.0f):(0.0f) );
                else
                    outNormals[i] *= 1.0f / length;
            }
        }

        static inline void MergeNormalsForEqualPositions( std::vector<vaVector3>& inOutNormals, const std::vector<vaVector3>& vertices, float epsilon = VA_EPSf )
        {
            std::vector<vaVector3> normalsCopy( inOutNormals );
            assert( inOutNormals.size() == vertices.size() );
            for( int i = 0; i < (int)vertices.size( ); i++ )
                for( int j = i+1; j < (int)vertices.size( ); j++ )
                {
                    // if( i == j )
                    //     continue;
                    if( vaVector3::NearEqual( vertices[i], vertices[j], epsilon ) )
                    {
                        inOutNormals[i] += normalsCopy[j];
                        inOutNormals[j] += normalsCopy[i];
                    }
                }
            for( int i = 0; i < (int)vertices.size( ); i++ )
                inOutNormals[i] = inOutNormals[i].Normalized();
        }

        // based on http://www.terathon.com/code/tangent.html
        static void GenerateTangents( std::vector<vaVector4> & outTangents, const std::vector<vaVector3> & vertices, const std::vector<vaVector3> & normals, const std::vector<vaVector2> & UVs, const std::vector<uint32> & indices ) 
        {
            assert( false ); // code not tested

            assert( outTangents.size( ) == vertices.size( ) );

            std::vector<vaVector3> tempTans;
            tempTans.resize( vertices.size() * 2, vaVector3( 0.0f, 0.0f, 0.0f ) );

            vaVector3 * tan1 = &tempTans[0];
            vaVector3 * tan2 = &tempTans[vertices.size()];

            assert( (indices.size() % 3) == 0 );
            int triangleCount = (int)indices.size() / 3;

            for( long a = 0; a < triangleCount; a++ )
            {
                long i1 = indices[a*3+0];
                long i2 = indices[a*3+1];
                long i3 = indices[a*3+2];

                const vaVector3 & v1 = vertices[i1];
                const vaVector3 & v2 = vertices[i2];
                const vaVector3 & v3 = vertices[i3];

                const vaVector2 & w1 = UVs[i1];
                const vaVector2 & w2 = UVs[i2];
                const vaVector2 & w3 = UVs[i3];

                float x1 = v2.x - v1.x;
                float x2 = v3.x - v1.x;
                float y1 = v2.y - v1.y;
                float y2 = v3.y - v1.y;
                float z1 = v2.z - v1.z;
                float z2 = v3.z - v1.z;

                float s1 = w2.x - w1.x;
                float s2 = w3.x - w1.x;
                float t1 = w2.y - w1.y;
                float t2 = w3.y - w1.y;

                float r = 1.0F / ( s1 * t2 - s2 * t1 );
                vaVector3 sdir( ( t2 * x1 - t1 * x2 ) * r, ( t2 * y1 - t1 * y2 ) * r, ( t2 * z1 - t1 * z2 ) * r );
                vaVector3 tdir( ( s1 * x2 - s2 * x1 ) * r, ( s1 * y2 - s2 * y1 ) * r, ( s1 * z2 - s2 * z1 ) * r );

                tan1[i1] += sdir;
                tan1[i2] += sdir;
                tan1[i3] += sdir;

                tan2[i1] += tdir;
                tan2[i2] += tdir;
                tan2[i3] += tdir;
            }

            for( long a = 0; a < vertices.size(); a++ )
            {
                const vaVector3 & n = normals[a];
                const vaVector3 & t = tan1[a];

                // Gram-Schmidt orthogonalize
                outTangents[a] = vaVector4( ( t - n * vaVector3::Dot( n, t ) ).Normalized( ), 1.0f );

                // Calculate handedness
                outTangents[a].w = (vaVector3::Dot( vaVector3::Cross( n, t ), tan2[a] ) < 0.0f) ? ( -1.0f ) : ( 1.0f );
            }
        }


        template< typename VertexType >
        static inline vaBoundingBox CalculateBounds( const std::vector<VertexType> & vertices )
        {
            vaVector3 bmin( VA_FLOAT_HIGHEST, VA_FLOAT_HIGHEST, VA_FLOAT_HIGHEST ), bmax( VA_FLOAT_LOWEST, VA_FLOAT_LOWEST, VA_FLOAT_LOWEST );

            for( size_t i = 0; i < vertices.size( ); i++ )
            {
                bmin = vaVector3::ComponentMin( bmin, vertices[i].Position );
                bmax = vaVector3::ComponentMax( bmax, vertices[i].Position );
            }

            return vaBoundingBox( bmin, bmax - bmin );
        }

        template< typename VertexType >
        static inline void CalculateBounds( const std::vector<VertexType> & vertices, vaBoundingBox & outBox, vaBoundingSphere & outSphere )
        {
            vaVector3 bmin( VA_FLOAT_HIGHEST, VA_FLOAT_HIGHEST, VA_FLOAT_HIGHEST ), bmax( VA_FLOAT_LOWEST, VA_FLOAT_LOWEST, VA_FLOAT_LOWEST );

            for( size_t i = 0; i < vertices.size( ); i++ )
            {
                bmin = vaVector3::ComponentMin( bmin, vertices[i].Position );
                bmax = vaVector3::ComponentMax( bmax, vertices[i].Position );
            }

            outBox = vaBoundingBox( bmin, bmax - bmin );

            float maxDistSq = 0.0f;
            outSphere.Center = outBox.Center(); 
            for( size_t i = 0; i < vertices.size( ); i++ )
                maxDistSq = std::max( maxDistSq, (vertices[i].Position - outSphere.Center).LengthSq() );
            outSphere.Radius = std::sqrtf( maxDistSq );

            // TODO: upgrade bounding sphere to Ritter's https://en.wikipedia.org/wiki/Bounding_sphere
            // example from miniengine/https://github.com/DQLin/RealTimeStochasticLightcuts:
            //void computeRitterBoundingSphere( )
            //{
            //    glm::vec3 x = meshes[0].vertices[0].Position;
            //    float maxDist = 0, yi = 0, yj = 0, zi = 0, zj = 0;
            //    for( int i = 0; i < meshes.size( ); i++ )
            //    {
            //        aabb meshBounds;
            //        for( int j = 0; j < meshes[i].vertices.size( ); j++ )
            //        {
            //            meshBounds.Union( meshes[i].vertices[j].Position );
            //            float dist = length( meshes[i].vertices[j].Position - x );
            //            if( dist > maxDist )
            //            {
            //                maxDist = dist;
            //                yi = i;
            //                yj = j;
            //            }
            //        }
            //        meshes[i].boundingBox = meshBounds;
            //    }
            //    maxDist = 0;
            //    glm::vec3 y = meshes[yi].vertices[yj].Position;
            //    for( int i = 0; i < meshes.size( ); i++ )
            //    {
            //        for( int j = 0; j < meshes[i].vertices.size( ); j++ )
            //        {
            //            float dist = length( meshes[i].vertices[j].Position - y );
            //            if( dist > maxDist )
            //            {
            //                maxDist = dist;
            //                zi = i;
            //                zj = j;
            //            }
            //        }
            //    }
            //    glm::vec3 z = meshes[zi].vertices[zj].Position;
            //    glm::vec3 center( 0.5f * ( y + z ) );
            //    float radius = 0.5f * length( y - z );
            //    for( int i = 0; i < meshes.size( ); i++ )
            //    {
            //        for( int j = 0; j < meshes[i].vertices.size( ); j++ )
            //        {
            //            float dist = length( meshes[i].vertices[j].Position - center );
            //            if( dist > radius )
            //            {
            //                glm::vec3 extra = meshes[i].vertices[j].Position;
            //                center = center + 0.5f * ( dist - radius ) * normalize( extra - center );
            //                radius = 0.5 * ( dist + radius );
            //            }
            //        }
            //    }
            //    scene_sphere_pos = center;
            //    scene_sphere_radius = radius;
            //}
        }

        template< typename VertexType >
        static inline void ConcatenatePositionOnlyMesh( std::vector<VertexType> & outVertices, std::vector<uint32> & outIndices, std::vector<vaVector3> & inVertices, std::vector<uint32> & inIndices )
        {
            uint32 startingVertex = (uint32)outVertices.size();

            for( int i = 0; i < inVertices.size(); i++ )
            {
                VertexType nv;
                nv.Position = inVertices[i];
                outVertices.push_back( nv );
            }
            for( int i = 0; i < inIndices.size( ); i++ )
                outIndices.push_back( inIndices[i] + startingVertex );
        }

    };

    template< typename VertexType >
    class vaTriangleMesh : public vaRenderingModule
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );

    protected:
        // CPU data
        std::vector<VertexType>             m_vertices;     
        std::vector<uint32>                 m_indices;      // no strips, just a regular indexed triangle list

        // GPU data
        //vaAutoRMI< vaIndexBuffer >          m_indexBuffer;
        //vaTypedVertexBufferWrapper< VertexType >
        //                                    m_vertexBuffer;
        shared_ptr<vaRenderBuffer>          m_indexBuffer;
        shared_ptr<vaRenderBuffer>          m_vertexBuffer;


    private:
        bool                                m_gpuDataDirty  = true;

    private:


    public:
        vaTriangleMesh( const vaRenderingModuleParams & params ) : vaRenderingModule( params )
        {
//            assert( vaRenderingCore::IsInitialized() );
        }

    public:
        virtual     ~vaTriangleMesh( )    { }

        // if manipulating these directly, make sure to call SetDataDirty; this is not designed for dynamic buffers at the moment with regards to performance but it's functional
        std::vector<VertexType> &           Vertices( )                 { return m_vertices; }
        std::vector<uint32>     &           Indices( )                  { return m_indices; }
        virtual void                        SetDataDirty( )             { std::unique_lock lock( m_mutex ); m_gpuDataDirty = true; }

        const shared_ptr< vaRenderBuffer >& GetGPUIndexBuffer( ) const  { return m_indexBuffer; }
        const shared_ptr< vaRenderBuffer >& GetGPUVertexBuffer( )       { return m_vertexBuffer; }
        vaFramePtr<vaRenderBuffer>          GetGPUIndexBufferFP( )      { return m_indexBuffer; }
        vaFramePtr<vaRenderBuffer>          GetGPUVertexBufferFP( )     { return m_vertexBuffer; }

        void        Reset( ) //const std::shared_lock & meshLock )
        {
            assert( GetRenderDevice( ).IsRenderThread( ) );
            m_vertices.clear( );
            m_indices.clear( );
            std::unique_lock lock( m_mutex );
            m_gpuDataDirty = true;
        }

        int                                 AddVertex( const VertexType & vert )
        {
            assert( GetRenderDevice( ).IsRenderThread( ) );
            return vaTriangleMeshTools::AddVertex( Vertices(), vert );
            std::unique_lock lock( m_mutex );
            m_gpuDataDirty = true;
        }

        void                                AddTriangle( int a, int b, int c )
        {
            assert( GetRenderDevice( ).IsRenderThread( ) );
            vaTriangleMeshTools::AddTriangle( Indices(), a, b, c );
            std::unique_lock lock( m_mutex );
            m_gpuDataDirty = true;
        }

        template< typename VertexType >
        inline void                         AddQuad( const VertexType & v0, const VertexType & v1, const VertexType & v2, const VertexType & v3 )
        {
            assert( GetRenderDevice( ).IsRenderThread( ) );
            vaTriangleMeshTools::AddQuad( Vertices(), Indices(), v0, v1, v2, v3 );
            std::unique_lock lock( m_mutex );
            m_gpuDataDirty = true;
        }

        template< typename VertexType >
        inline void                         AddTriangle( const VertexType & v0, const VertexType & v1, const VertexType & v2 )
        {
            assert( GetRenderDevice( ).IsRenderThread( ) );
            vaTriangleMeshTools::AddTriangle( Vertices(), Indices(), v0, v1, v2 );
            std::unique_lock lock( m_mutex );
            m_gpuDataDirty = true;
        }

        template< typename VertexType, typename CompareCallableType >
        inline void                         AddTriangleMergeDuplicates( const VertexType & v0, const VertexType & v1, const VertexType & v2, CompareCallableType && isDuplicate, int searchBackRange = -1 )
        {
            assert( GetRenderDevice( ).IsRenderThread( ) );
            int lookFrom = (searchBackRange==-1)?(0):(std::max(0, (int)Vertices().size()-searchBackRange ));
            vaTriangleMeshTools::AddTriangle_MergeDuplicates( Vertices(), Indices(), v0, v1, v2, isDuplicate, lookFrom );
            std::unique_lock lock( m_mutex );
            m_gpuDataDirty = true;
        }

        //template< typename VertexType >
        inline vaBoundingBox                CalculateBounds( )
        {
            assert( GetRenderDevice( ).IsRenderThread( ) );
            return vaTriangleMeshTools::CalculateBounds( Vertices() );
        }

        void                                GenerateNormals( vaWindingOrder windingOrder, int indexFrom, int indexCount, float mergeSharedMaxAngle = 0.0f )
        {
            assert( GetRenderDevice( ).IsRenderThread( ) );

            vector<vaVector3> positions;
            vector<vaVector3> normals;
            positions.resize( m_vertices.size( ) );
            normals.resize( m_vertices.size( ) );

            const int vertexCount = (int)m_vertices.size( );
            for( int i = 0; i < vertexCount; i++ )
                positions[i] = m_vertices[i].Position;

            vaTriangleMeshTools::GenerateNormals( normals, positions, m_indices, windingOrder, indexFrom, indexCount, true, mergeSharedMaxAngle );

            for( int i = indexFrom; i < indexFrom+indexCount; i++ )
                m_vertices[m_indices[i]].Normal.AsVec3( ) = normals[m_indices[i]];

            std::unique_lock lock( m_mutex );
            m_gpuDataDirty = true;
        }

        template< typename MutexType >
        void                                UpdateGPUDataIfNeeded( vaRenderDeviceContext & renderContext, std::shared_lock<MutexType> & meshLock )
        {
            assert( meshLock.mutex() == &m_mutex ); meshLock;
            assert( !renderContext.IsWorker( ) );

            const bool ownedLock = meshLock.owns_lock( );
            if( !ownedLock )
                meshLock.lock( );

            renderContext;
            if( m_gpuDataDirty )
            {
                // "upgrade" lock
                meshLock.unlock();
                std::unique_lock uniqueLock( m_mutex );

                // could've been updated by another thread
                if( m_gpuDataDirty )
                {
                    if( m_indices.size( ) > 0 )
                    {
                        if( m_indexBuffer->GetElementCount( ) != m_indices.size( ) )
                            m_indexBuffer->Create( (uint64)m_indices.size( ), vaResourceFormat::R32_UINT, vaRenderBufferFlags::VertexIndexBuffer, "IndexBuffer" );
                        m_indexBuffer->Upload( renderContext, m_indices );
                    }
                    else
                        m_indexBuffer->Destroy( );

                    if( m_vertices.size( ) > 0 )
                    {
                        if( m_vertexBuffer->GetElementCount( ) != m_vertices.size( ) )
                            m_vertexBuffer->Create<StandardVertex>( (uint64)m_vertices.size( ), vaRenderBufferFlags::VertexIndexBuffer, "VertexBuffer" );
                        m_vertexBuffer->Upload( renderContext, m_vertices );
                    }
                    else
                        m_vertexBuffer->Destroy( );

                    m_gpuDataDirty = false;
                }
                // m_vertexBufferCachedFP.Reset();
                // m_indexBufferCachedFP.Reset();
            }
            if( !ownedLock )
                meshLock.unlock( );
        }

    };
}