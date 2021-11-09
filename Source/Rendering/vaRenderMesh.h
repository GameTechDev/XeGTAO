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
#include "Core/vaUI.h"
#include "Core/vaContainers.h"

#include "vaRendering.h"
#include "vaRenderInstanceList.h"

#include "vaTriangleMesh.h"
#include "vaTexture.h"

#include "Rendering/Shaders/vaSharedTypes.h"

#include <unordered_set>


// vaRenderMesh and vaRenderMeshManager are a generic render mesh implementation


// if materialSettings.Transparent and materialSettings.FaceCull is not vaFaceCull::None, draw transparencies twice - first back and then front faces
// (this actually isn't the best way to do this - I think it would be much better to draw all back faces with depth write on and then front faces after as a two pass solution)
// #define VA_AUTO_TWO_PASS_TRANSPARENCIES_ENABLED 

namespace Vanilla
{
    struct vaRenderOutputs;
    class vaRenderMeshManager;
    class vaRenderMaterial;

    class vaRenderMesh : public vaAssetResource, public vaRenderingModule
    {
    public:

        // only standard mesh storage supported at the moment
        struct StandardVertex
        {
            // first 16 bytes
            vaVector3   Position;
            uint32_t    Color;

            // next 16 bytes, .w not encoded - can be reused for something else. Should probably be compressed to 16bit floats on the rendering side.
            vaVector4   Normal;

            // next 8 bytes, first set of UVs; could maybe be compressed to 16bit floats, not sure
            vaVector2   TexCoord0;

            // next 8 bytes, second set of UVs; could maybe be compressed to 16bit floats, not sure
            vaVector2   TexCoord1;

            StandardVertex( ) { }
            explicit StandardVertex( const vaVector3 & position ) : Position( position ), Normal( vaVector4( 0, 1, 0, 0 ) ), Color( 0xFF808080 ), TexCoord0( 0, 0 ), TexCoord1( 0, 0 ) {}
            StandardVertex( const vaVector3 & position, const uint32_t & color ) : Position( position ), Normal( vaVector4( 0, 1, 0, 0 ) ), TexCoord0( 0, 0 ), TexCoord1( 0, 0 ), Color( color ) { }
            StandardVertex( const vaVector3 & position, const vaVector4 & normal, const uint32_t & color ) : Position( position ), Normal( normal ), Color( color ), TexCoord0( 0, 0 ), TexCoord1( 0, 0 ) { }
            StandardVertex( const vaVector3 & position, const vaVector4 & normal, const vaVector2 & texCoord0, const uint32_t & color ) : Position( position ), Normal( normal ), TexCoord0( texCoord0 ), TexCoord1( 0, 0 ), Color( color ) { }
            StandardVertex( const vaVector3 & position, const vaVector4 & normal, const vaVector2 & texCoord0, const vaVector2 & texCoord1, const uint32_t & color ) : Position( position ), Normal( normal ), TexCoord0( texCoord0 ), TexCoord1( texCoord1 ), Color( color ) { }

            // StandardVertex( const StandardVertexOld & old ) : Position( old.Position ), Color( old.Color ), Normal( old.Normal ), TexCoord0( old.TexCoord0 ), TexCoord1( old.TexCoord1 ) { }

            bool operator ==( const StandardVertex & cmpAgainst ) const
            {
                return      ( Position == cmpAgainst.Position ) && ( Normal == cmpAgainst.Normal ) && ( TexCoord0 == cmpAgainst.TexCoord0 ) && ( TexCoord1 == cmpAgainst.TexCoord1 ) && ( Color == cmpAgainst.Color );
            }
            
            static bool IsDuplicate( const StandardVertex & left, const StandardVertex & right ) { return left == right; }
        };

        // <unused atm>
        struct StandardVertexAnimationPart
        {
            uint32      Indices;    // (8888_UINT)
            uint32      Weights;    // (8888_UNORM)
        };

        struct LODPart
        {
            static constexpr int                        MaxLODParts         = 16;

            int                                         IndexStart          = 0;
            int                                         IndexCount          = 0;

            // Distance to switch to next LOD level, relative to AABB size on the screen (1.0 is when AABB/BS roughly fills the screen). This is also affected by at least vaCameraBase::GetLODSettings 'ReferenceScale'.
            // See vaRenderMesh::FindLOD
            float                                       SwapToNextDistance  = std::numeric_limits<float>::infinity();

            LODPart( ) { };
            LODPart( int indexStart, int indexCount, float swapToNextDistance ) : IndexStart( indexStart ), IndexCount( indexCount ), SwapToNextDistance( swapToNextDistance ) { }

            bool                                        Serialize( vaXMLSerializer & serializer );
        };

        // This type was used for mesh storage before getting merged into vaRenderMesh, so it's here just for backward compatibility in a couple of
        // places until I get rid of it all!
        typedef vaTriangleMesh<StandardVertex>          StandardTriangleMesh;

    protected:
        //vaTT_Trackee< vaRenderMesh * >                  m_trackee;
        vaRenderMeshManager &                           m_renderMeshManager;

        vaWindingOrder                                  m_frontFaceWinding;
        //bool                                            m_tangentBitangentValid;

        // CPU triangle mesh data
        std::vector<StandardVertex>                     m_vertices;     
        std::vector<uint32>                             m_indices;      // no strips, just a regular indexed triangle list

        // GPU triangle mesh data
        //vaAutoRMI< vaIndexBuffer >                      m_indexBuffer;
        //vaTypedVertexBufferWrapper<StandardVertex>      m_vertexBuffer;
        shared_ptr<vaRenderBuffer>                      m_indexBuffer;
        shared_ptr<vaRenderBuffer>                      m_vertexBuffer;

        vaGUID                                          m_materialID;               // used during loading - could be moved into a separate structure and disposed of after loading
        bool                                            m_gpuDataDirty              = true;

// in case vaUIDObjectRegistrar::FindFP for materials ever becomes prohibitively expensive, try using the cache below
//#define VA_RENDER_MATERIAL_USE_CACHED_FP
#ifdef VA_RENDER_MATERIAL_USE_CACHED_FP
        mutable vaAtomicLCFramePtr<vaRenderMaterial>    m_materialCachedFP;
#endif

        std::vector<LODPart>                            m_LODParts;
        float                                           m_overrideLODLevel              = -1.0f;    // if you want to force the LOD level, to always be this; if not, use -1
        int64                                           m_overrideLODLevelLastAppTickID = -1;   // used to make sure m_overrideLODLevel only works if set this or last frame

        float                                           m_LODDistanceOffsetAdd      = 0.0f;
        float                                           m_LODDistanceOffsetMul      = 1.0f;

        vaBoundingBox                                   m_boundingBox;              // local bounding box around the mesh (includes all LODs)
        vaBoundingSphere                                m_boundingSphere;           // same as ^ :)

        int                                             m_globalIndex               = -1;
        ShaderMeshConstants                             m_lastShaderConstants;      // effectively last uploaded shader constants

    protected:
        friend class vaRenderMeshManager;
        vaRenderMesh( const vaRenderingModuleParams & params );
    public:
        virtual ~vaRenderMesh( );

    public:
        //const wstring &                                 GetName( ) const                                    { return m_name; };

        vaRenderMeshManager &                           GetManager( ) const                                 { return m_renderMeshManager; }
        //int                                             GetListIndex( ) const                               { return m_trackee.GetIndex( ); }

        // every time there's any change on this, Mutex needs to be locked ( std::unique_lock lock( m_mutex );  )
        std::vector<StandardVertex> &                   Vertices( )                                         { return m_vertices; }
        std::vector<uint32>     &                       Indices( )                                          { return m_indices; }
        const std::vector<StandardVertex> &             Vertices( ) const                                   { return m_vertices; }
        const std::vector<uint32>     &                 Indices( )  const                                   { return m_indices; }


        const vaBoundingBox &                           GetAABB( ) const                                    { return m_boundingBox; }
        const vaBoundingSphere &                        GetBS( ) const                                      { return m_boundingSphere; }

        const std::vector<LODPart> &                    GetLODParts( ) const                                { assert( m_LODParts.size()<= LODPart::MaxLODParts ); return m_LODParts; }
        float                                           GetOverrideLODLevel( ) const                        { return m_overrideLODLevel; }
        bool                                            HasOverrideLODLevel( int64 applicationTickID )const { return m_overrideLODLevelLastAppTickID >= applicationTickID; }
        
        // see code for more info on LODRangeFactor
        float                                           FindLOD( float LODRangeFactor );

        shared_ptr<vaRenderMaterial>                    GetMaterial( ) const;
        vaFramePtr<vaRenderMaterial>                    GetMaterialFP( ) const;
        const vaGUID &                                  GetMaterialID( ) const                              { return m_materialID; }
        void                                            SetMaterial( const vaGUID & materialID );
        void                                            SetMaterial( const shared_ptr<vaRenderMaterial> & m );

        int                                             GetGlobalIndex( ) const                             { return m_globalIndex; }


        vaWindingOrder                                  GetFrontFaceWindingOrder( ) const                   { return m_frontFaceWinding; }
        void                                            SetFrontFaceWindingOrder( vaWindingOrder winding )  { m_frontFaceWinding = winding; }

        //bool                                            GetTangentBitangentValid( ) const                   { return m_tangentBitangentValid; }
        //void                                            SetTangentBitangentValid( bool value )              { m_tangentBitangentValid = value; }

        void                                            UpdateAABB( );
        void                                            RebuildNormals( int lodFrom = 0, int lodCount = 0, float mergeSharedMaxAngle = 0.0f );

        // These below are somewhat half-baked - they'll do the most basic version of what they say they'll do
        void                                            Transform( const vaMatrix4x4 & transform );
        void                                            Extrude( const vaBoundingBox & area, const string & newMeshAssetName );
        void                                            ReCenter( );
        void                                            MergeSimilarVerts( int lodFrom = 0, int lodCount = 0, float distanceThreshold = 0.0001f, float angleThreshold = VA_PIf * 0.25f );
        void                                            TNTesselate( );

        void                                            ClearLODs( );
        void                                            RebuildLODs( float maxRelativePosError, float normalRebuildMergeSharedMaxAngle = 0.0f );

        bool                                            SaveAPACK( vaStream & outStream ) override;
        bool                                            LoadAPACK( vaStream & inStream ) override;
        bool                                            SerializeUnpacked( vaXMLSerializer & serializer, const string & assetFolder ) override;

        //virtual void                                    ReconnectDependencies( );
        virtual void                                    RegisterUsedAssetPacks( std::function<void( const vaAssetPack & )> registerFunction ) override;
        void                                            EnumerateUsedAssets( const std::function<void(vaAsset * asset)> & callback );

        //vaRenderDevice &                                GetRenderDevice() const;

        // Access to GPU data
        const shared_ptr< vaRenderBuffer > &            GetGPUIndexBuffer( ) const  { return m_indexBuffer; }
        const shared_ptr< vaRenderBuffer > &            GetGPUVertexBuffer( ) const { return m_vertexBuffer; }
        vaFramePtr<vaRenderBuffer>                      GetGPUIndexBufferFP( )      { return m_indexBuffer;  }
        vaFramePtr<vaRenderBuffer>                      GetGPUVertexBufferFP( )     { return m_vertexBuffer; }

        bool                                            PreRenderUpdate( vaRenderDeviceContext & renderContext );

        // startTrackingUIDObject parameter: if creating from non-main thread, do not automatically start tracking; rather finish all loading/creation and then manually register with UIDOBject database (ptr->UIDObject_Track())
        //
        // create mesh with normals, with provided vertices & indices
        static shared_ptr<vaRenderMesh>                 Create( vaRenderDevice& device, const vaMatrix4x4& transform, const std::vector<vaVector3>& vertices, const std::vector<vaVector3>& normals, const std::vector<vaVector2>& texcoords0, const std::vector<vaVector2>& texcoords1, const std::vector<uint32>& indices, vaWindingOrder frontFaceWinding /*= vaWindingOrder::CounterClockwise*/, const vaGUID& uid = vaCore::GUIDCreate( ), bool startTrackingUIDObject = true );
        // create mesh with provided triangle mesh, winding order and material
        static shared_ptr<vaRenderMesh>                 Create( shared_ptr<StandardTriangleMesh> & triMesh, vaWindingOrder frontFaceWinding = vaWindingOrder::CounterClockwise, const vaGUID & materialID = vaGUID::Null, const vaGUID & uid = vaCore::GUIDCreate(), bool startTrackingUIDObject = true );
        // create mesh based on provided vaRenderMesh but without creating new triMesh or material or etc.
        static shared_ptr<vaRenderMesh>                 CreateShallowCopy( const vaRenderMesh & copy, const vaGUID & uid = vaCore::GUIDCreate( ), bool startTrackingUIDObject = true );

        // these use vaStandardShapes::Create* functions and create shapes with center in (0, 0, 0) and each vertex magnitude of 1 (normalized), except where specified otherwise, and then transformed by the provided transform
        static shared_ptr<vaRenderMesh>                 CreatePlane( vaRenderDevice & device, const vaMatrix4x4 & transform, float sizeX = 1.0f, float sizeY = 1.0f, bool doubleSided = false, const vaGUID & uid = vaCore::GUIDCreate() );
        static shared_ptr<vaRenderMesh>                 CreateGrid( vaRenderDevice & device, const vaMatrix4x4 & transform, int dimX, int dimY, float sizeX = 1.0f, float sizeY = 1.0f, const vaVector2 & uvOffsetMul = vaVector2( 1, 1 ), const vaVector2 & uvOffsetAdd = vaVector2( 0, 0 ), const vaGUID & uid = vaCore::GUIDCreate() );
        static shared_ptr<vaRenderMesh>                 CreateTetrahedron( vaRenderDevice & device, const vaMatrix4x4 & transform, bool shareVertices, const vaGUID & uid = vaCore::GUIDCreate() );
        static shared_ptr<vaRenderMesh>                 CreateCube( vaRenderDevice & device, const vaMatrix4x4 & transform, bool shareVertices, float edgeHalfLength = 0.7071067811865475f, const vaGUID & uid = vaCore::GUIDCreate() );
        static shared_ptr<vaRenderMesh>                 CreateOctahedron( vaRenderDevice & device, const vaMatrix4x4 & transform, bool shareVertices, const vaGUID & uid = vaCore::GUIDCreate() );
        static shared_ptr<vaRenderMesh>                 CreateIcosahedron( vaRenderDevice & device, const vaMatrix4x4 & transform, bool shareVertices, const vaGUID & uid = vaCore::GUIDCreate() );
        static shared_ptr<vaRenderMesh>                 CreateDodecahedron( vaRenderDevice & device, const vaMatrix4x4 & transform, bool shareVertices, const vaGUID & uid = vaCore::GUIDCreate() );
        static shared_ptr<vaRenderMesh>                 CreateSphere( vaRenderDevice & device, const vaMatrix4x4 & transform, int tessellationLevel, bool shareVertices, const vaGUID & uid = vaCore::GUIDCreate() );
        static shared_ptr<vaRenderMesh>                 CreateCylinder( vaRenderDevice & device, const vaMatrix4x4 & transform, float height, float radiusBottom, float radiusTop, int tessellation, bool openTopBottom, bool shareVertices, const vaGUID & uid = vaCore::GUIDCreate() );
        static shared_ptr<vaRenderMesh>                 CreateTeapot( vaRenderDevice & device, const vaMatrix4x4 & transform, const vaGUID & uid = vaCore::GUIDCreate() );

        static std::vector<struct vaVertexInputElementDesc>    GetStandardInputLayout( );

    protected:
        //////////////////////////////////////////////////////////////////////////
        // Mutex needs locking when you use these!
        // every time Vertices or Indices are changed, this needs to be set!
        void                                            MeshSetGPUDataDirty( );
        void                                            MeshReset( );
        // this resets LODs to 1, resets AABBs
        void                                            MeshSet( const std::vector<StandardVertex> & vertices, const std::vector<uint32> & indices );
        void                                            MeshGenerateNormals( vaWindingOrder windingOrder, int indexFrom, int indexCount, float mergeSharedMaxAngle = 0.0f );
        
        // don't forget to "std::unique_lock lock( m_mutex ); m_gpuDataDirty = true;"!
        template< typename CompareCallableType >
        inline void                                     MeshAddTriangleMergeDuplicates( const StandardVertex & v0, const StandardVertex & v1, const StandardVertex & v2, CompareCallableType && isDuplicate, int searchBackRange = -1 );
        //
        // TEMP TEMP TEMP
        virtual void                                    UpdateGPURTData( vaRenderDeviceContext & renderContext ) { renderContext; assert( false ); };
        // TEMP TEMP TEMP
        //////////////////////////////////////////////////////////////////////////

    public:
        virtual bool                                    UIPropertiesDraw( vaApplicationBase& application ) override;
        vaAssetType                                     GetAssetType( ) const override                      { return vaAssetType::RenderMesh; }
    };

    enum class vaRenderMeshDrawFlags : uint32
    {
        None                    = 0,
        // SkipTransparencies      = ( 1 << 0 ),           //                                      should really go to vaRenderInstanceListCullFlags instead of here
        // SkipNonTransparencies   = ( 1 << 1 ),           // a.k.a only draw shadow casters       should really go to vaRenderInstanceListCullFlags instead of here
        EnableDepthTest         = ( 1 << 2 ),
        InvertDepthTest         = ( 1 << 3 ),
        EnableDepthWrite        = ( 1 << 4 ),
        DepthTestIncludesEqual  = ( 1 << 5 ),
        SkipNonShadowCasters    = ( 1 << 6 ),           //                                      should really go to vaRenderInstanceListCullFlags instead of here
        DepthTestEqualOnly      = ( 1 << 7 ),           // only draw if exact value already in the depth buffer
        // SkipNoDepthPrePass      = ( 1 << 7 ),           // skip all meshes/materials flagged with NoDepthPrePass
        // ReverseDrawOrder        = ( 1 << 8 ),           // reverse mesh list draw order - if sorted; by default sorting is front to back so reversing means draw back to front
        DisableVRS              = ( 1 << 9 ),           // disable variable rate shading - always use 1x1 (useful for depth prepass)
    };

    BITFLAG_ENUM_CLASS_HELPER( vaRenderMeshDrawFlags );

    class vaRenderMeshManager : public vaRenderingModule, public vaUIPanel
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );

    public:

    protected:
        //vaTT_Tracker< vaRenderMesh * >                  m_renderMeshes;
        vaSparseArray< vaRenderMesh * >                 m_meshes;

        bool                                            m_isDestructing;

        struct PerWorkerData
        {
            //shared_ptr<vaTypedConstantBufferWrapper< ShaderInstanceConstants, true >>
            //                                            ConstantBuffer;

            struct MeshCacheEntry
            {
                vaRenderMesh::LODPart           LODParts[vaRenderMesh::LODPart::MaxLODParts];
                vaFramePtr<vaRenderBuffer>      VertexBuffer;
                vaFramePtr<vaRenderBuffer>      IndexBuffer;
                bool                            FrontCounterClockwise;
                int                             LODPartCount;
            };

            struct MaterialCacheEntry
            {
                vaRenderMaterialData            RenderData;
            };

            // this is the vaGraphicsItem that gets reused; some parts related to the mesh or material will not be re-filled if the CachedMaterial / CachedMesh are the same!
            vaGraphicsItem                              GraphicsItem;
            
            static int constexpr                        HashBucketSize  = 32;   // cache has a size of 3232 elements - seems like a good balance
            static int constexpr                        HashBucketCount = 101;  // cache has a size of 3232 elements - seems like a good balance
            vaHashedCircularCache< const vaRenderMesh *, MeshCacheEntry, HashBucketSize, HashBucketCount, vaMurmurPtrHasher<const vaRenderMesh *> >
                                                        MeshCache;
            vaHashedCircularCache< const vaRenderMaterial *, MaterialCacheEntry, HashBucketSize, HashBucketCount, vaMurmurPtrHasher<const vaRenderMaterial *> >
                                                        MaterialCache;

                                                        PerWorkerData( )                    { Reset(); }
            void                                        Reset( )
            {
                GraphicsItem        = vaGraphicsItem();     // reset any pointers we held!
#ifdef _DEBUG
                MeshCache.Reset( (vaRenderMesh*)nullptr, MeshCacheEntry{} );
                MaterialCache.Reset( (vaRenderMaterial*)nullptr, MaterialCacheEntry{} );
#else
                MeshCache.Reset( );
                MaterialCache.Reset( );
#endif
            }

            void                                        CacheAddMesh( const vaRenderMesh * mesh, MeshCacheEntry && entry )
            {
                MeshCache.Insert( mesh, std::move(entry) );
            }
        };

        std::vector<PerWorkerData>                      m_perWorkerData;

        // //// used only for DrawSingle
        // //vaTypedConstantBufferWrapper< ShaderInstanceConstants, true >
        // //                                                m_constantBuffer;
        // shared_ptr<vaRenderBuffer>                      m_simpleConstantBuffers;

        // meshes useful for general debugging
        shared_ptr<vaRenderMesh>                        m_unitSphere        = nullptr;

        int                                             m_constantBufferMaxCount        = 65535;
        shared_ptr<vaRenderBuffer>                      m_constantBuffer;

    public:
//        friend class vaRenderingCore;
        vaRenderMeshManager( const vaRenderingModuleParams & params );
        virtual ~vaRenderMeshManager( );

    private:
        friend class vaRenderMesh;
        const shared_ptr<vaRenderBuffer> &              GetGlobalConstantBuffer( ) const                                        { return m_constantBuffer; }

        // Make sure you've locked mutex when accessing this: std::shared_lock managerLock( renderMaterialManager.Mutex() );
        vaSparseArray< vaRenderMesh * > &               Meshes( )                                                               { return m_meshes; }

    public:
        // Make sure you've locked mutex when accessing this: std::shared_lock managerLock( renderMaterialManager.Mutex() );
        const vaSparseArray< vaRenderMesh * > &         Meshes( ) const                                                         { return m_meshes; }

    public:
#if 0 // this is gone to reduce complexity
        // very simple single instance draw - slow but does the job
        virtual vaDrawResultFlags                       DrawSimple( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, vaRenderMaterialShaderType shaderType, const vaDrawAttributes & drawAttributes, vaBlendMode blendMode, vaRenderMeshDrawFlags drawFlags, const std::vector<vaRenderInstanceSimple> & instances );
#endif

        virtual vaDrawResultFlags                       Draw( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, vaRenderMaterialShaderType shaderType, const vaDrawAttributes & drawAttributes, const vaRenderInstanceList & selection, vaBlendMode blendMode, vaRenderMeshDrawFlags drawFlags, 
                                                                vaRenderInstanceList::SortHandle sortHandle = vaRenderInstanceList::EmptySortHandle );

        //vaTT_Tracker< vaRenderMesh * > *                GetRenderMeshTracker( )                                                 { return &m_renderMeshes; }

        shared_ptr<vaRenderMesh>                        CreateRenderMesh( const vaGUID & uid = vaCore::GUIDCreate(), bool startTrackingUIDObject = true );

    public:
        void                                            UpdateAndSetToGlobals( /*vaRenderDeviceContext & renderContext,*/ vaShaderItemGlobals & shaderItemGlobals/*, const vaDrawAttributes & drawAttributes*/ );

    public:
        shared_ptr<vaRenderMesh>                        UnitSphere( );

    protected:
        friend vaRenderDevice;
        void                                            PostCreateInitialize( );

        virtual string                                  UIPanelGetDisplayName( ) const override                                 { return "Meshes"; } //vaStringTools::Format( "vaRenderMaterialManager (%d meshes)", m_renderMaterials.size( ) ); }
        virtual void                                    UIPanelTick( vaApplicationBase & application ) override;
    };


    template< typename CompareCallableType >
    inline void vaRenderMesh::MeshAddTriangleMergeDuplicates( const StandardVertex & v0, const StandardVertex & v1, const StandardVertex & v2, CompareCallableType && isDuplicate, int searchBackRange )
    {
        assert( GetRenderDevice( ).IsRenderThread( ) );
        int lookFrom = ( searchBackRange == -1 ) ? ( 0 ) : ( std::max( 0, (int)Vertices( ).size( ) - searchBackRange ) );
        vaTriangleMeshTools::AddTriangle_MergeDuplicates( Vertices( ), Indices( ), v0, v1, v2, isDuplicate, lookFrom );
        
        // removed - now responsibility of the caller
        // std::unique_lock lock( m_mutex );
        // m_gpuDataDirty = true;
    }

}