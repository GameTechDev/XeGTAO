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

#include "Rendering/vaRenderDevice.h"

// #include "Scene/vaCameraBase.h"
// 
// #include "Core/vaUIDObject.h"
// 
// #include "Core/Misc/vaProfiler.h"
// 
// #include "Rendering/Shaders/vaSharedTypes.h"

namespace Vanilla
{
    namespace Scene
    {
        struct IBLProbe;
    }

    class vaRenderMesh;
    class vaRenderMaterial;

    class vaRenderInstanceStorage;

    // These are what remains when SceneInstances get selected; they get prepared for rendering later, along with the 
    // associated meshes, materials, etc.
    // A vaRenderInstanceList can hold additional per-instance data specific to it for additional granularity (like
    // shading rate or 'EmissiveAdd' color modifier.
    struct vaRenderInstance
    {
        vaMatrix4x4                     Transform;
        vaVector4                       EmissiveAdd;            // vaVector4( 0.0f, 0.0f, 0.0f, 1.0f );  // for debugging visualization (default means "do not override"); used for highlights, wireframe, lights, etc; rgb is added, alpha multiplies the original; for ex: " finalColor.rgb = finalColor.rgb * g_instance.EmissiveAdd.a + g_instance.EmissiveAdd.rgb; "
        DrawOriginInfo                  OriginInfo;
        vaFramePtr<vaRenderMesh>        Mesh;
        vaFramePtr<vaRenderMaterial>    Material;
        vaVector3                       EmissiveMul;

        float                           DistanceFromRef;        // world distance from LOD reference point (which is usually just main camera position)
        float                           MeshLOD;                // not sure if this should be per-list or per-instance
        struct _Flags 
        {
            unsigned        IsDecal     : 1;
            unsigned        IsWireframe : 1;
        }                               Flags;

        void WriteToShaderConstants( ShaderInstanceConstants & outConstants ) const;
    };

    // This is a version of vaRenderInstance for manual use - this path is not yet finished.
    struct vaRenderInstanceSimple : public vaRenderInstance
    {
        vaShadingRate                   ShadingRate         = vaShadingRate::ShadingRate1X1;        // per-draw-call shading rate

        vaRenderInstanceSimple( const shared_ptr<vaRenderMesh> & mesh, const vaMatrix4x4 & transform );
        vaRenderInstanceSimple( const shared_ptr<vaRenderMesh> & mesh,const vaMatrix4x4 & transform, const shared_ptr<vaRenderMaterial> & overrideMaterial, vaShadingRate shadingRate, const vaVector4 & emissiveAdd, const vaVector3 & emissiveMul, float meshLOD );
        void SetDefaults( );
    };

    // Contains per-frame selection of mainly vaRenderMesh/vaRenderMaterial items (see 'struct Item') but it can be 
    // used to handle other stuff like terrain blocks, billboards, etc.
    // Not intended for stuff like particles.
    struct vaRenderInstanceList
    {
        // any size reductions here will help a lot!
        struct Item
        {
            // reference to global list item
            uint32                          InstanceIndex;

            // additional customizations - not always needed
            vaShadingRate                   ShadingRate;        // vaShadingRate::ShadingRate1X1;        // per-draw-call shading rate
        };

        // contains culling and sorting information that should be honored when filling up the vaRenderInstanceList
        struct FilterSettings
        {
            vaBoundingSphere                BoundingSphereFrom      = vaBoundingSphere::Degenerate; //( { 0, 0, 0 }, 0.0f );
            vaBoundingSphere                BoundingSphereTo        = vaBoundingSphere::Degenerate; //( { 0, 0, 0 }, 0.0f );
            std::vector<vaPlane>            FrustumPlanes;
            
            FilterSettings( ) { }

            // settings for frustum culling for a regular draw based on a given camera
            static FilterSettings           FrustumCull( const vaCameraBase & camera );
            static FilterSettings           ShadowmapCull( const vaShadowmap & shadowmap );
            static FilterSettings           EnvironmentProbeCull( const Scene::IBLProbe & probeData );
        };

        struct SortSettings
        {
            // All materials of type vaLayerMode::Decal are a special case and always sorted before any others, and sorted by their 'decal order', ignoring any other sort references.

            vaVector3                       ReferencePoint          = vaVector3( std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity() );
            bool                            SortByDistanceToPoint   = false;        // useful for cubemaps and etc; probably not fully correct for transparencies? (would need sorting by distance to the plane?)
            bool                            FrontToBack             = true;         // front to back for opaque/depth pre-pass, back to front for transparencies is usual

            //bool                            SortByVRSType           = false;        // this will force sorting by shading rate first

            inline bool operator == ( const SortSettings & other ) const    { return 0 == memcmp( this, &other, sizeof( SortSettings ) ); }
            inline bool operator != ( const SortSettings & other ) const    { return !(*this==other); }

            //static SortSettings             Standard( bool sortByVRSType ) { SortSettings ret; ret.SortByVRSType = sortByVRSType; return ret; }
            static SortSettings             Standard( const vaVector3 & referencePoint, bool frontToBack/*, bool sortByVRSType*/ ) { SortSettings ret; ret.SortByDistanceToPoint = true; ret.ReferencePoint = referencePoint; ret.FrontToBack = frontToBack; /*ret.SortByVRSType = sortByVRSType;*/ return ret; }
            static SortSettings             Standard( const vaCameraBase & camera, bool frontToBack/*, bool sortByVRSType*/ ) { SortSettings ret; ret.SortByDistanceToPoint = true; ret.ReferencePoint = camera.GetPosition(); ret.FrontToBack = frontToBack; /*ret.SortByVRSType = sortByVRSType;*/ return ret; }
        };

        typedef uint64 SortHandle;
        static const SortHandle             EmptySortHandle         = uint64(-1L);

    private:
        static int                          s_instanceCounter;
        const int                           m_instanceID;
        int                                 m_resetCounter          = 0;

        bool                                m_ready                 = false;            // ready to start
        atomic_bool                         m_started               = false;            // started, ready to collect data

        unique_ptr<vaAppendConsumeList< Item >>         
                                            m_list                  = std::make_unique<vaAppendConsumeList< Item >>();

        // valid from StartCollecting to Reset
        shared_ptr<vaRenderInstanceStorage> m_instanceStorage;
        // valid from StopCollecting to Reset, indexed using 'Item::InstanceIndex'
        friend class vaRenderInstanceListSorterInstance;
        //ShaderInstanceConstants *           m_instanceShaderConstantsArray;
        const vaRenderInstance *            m_instanceArray;
        int                                 m_instanceMaxCount;


        std::vector<shared_ptr<class vaRenderInstanceListSorterInstance>>
                                            m_activeSorters;
        std::vector<shared_ptr<class vaRenderInstanceListSorterInstance>>
                                            m_inactiveSorters;

        std::atomic_uint32_t                m_selectResults;


    public:
                                            vaRenderInstanceList( );
                                            ~vaRenderInstanceList( );

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // Global state control
        //
        // Start collection - this enables Insert calls but disables Sort calls
        void                                StartCollecting( const shared_ptr<vaRenderInstanceStorage> & instanceStorage );
        // Finish collection - this starts sorters (if any) and enables using the list through Count/GetItems/ResultFlags/GetSortIndices
        void                                StopCollecting( );
        void                                Reset( );

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // Sorting can only be scheduled before Start and if the list was Reset
        SortHandle                          ScheduleSort( const SortSettings & settings );

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // Thread-safe insertion/reporting (allowed between StartCollecting and StopCollecting)
        // shadingRateOffset gets combined with material shading rate offset and, based on material horizontal/vertical preference converted into actual shading rate 
        // version that takes the material off the mesh, if possible, and has defaults for everything else - super-simple
        void                                Insert( uint32 instanceIndex, vaShadingRate shadingRate );

        void                                Report( vaDrawResultFlags flags )           { m_selectResults.fetch_or( (uint32)flags ); }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // Get status/results (valid only after StopCollecting and before Reset)
        size_t                              Count( ) const noexcept                     { if( m_ready ) return 0; assert( !m_started ); return m_list->Count(); }
        std::pair<const Item*, size_t>      GetItems( ) const                           { if( m_ready ) return { nullptr, 0 }; assert( !m_started ); return m_list->GetItemsUnsafe( ); };
        vaDrawResultFlags                   ResultFlags( ) const                        { assert( !m_started ); return (vaDrawResultFlags)m_selectResults.load(); }
        // Return the sorted array of indices (wait on sort finish if needed)
        const std::vector<int> *            GetSortIndices( SortHandle sortHandle ) const;
        const vaRenderInstance *            GetGlobalInstanceArray( ) const             { return m_instanceArray; }
        const shared_ptr<vaRenderBuffer> &  GetGlobalInstanceRenderBuffer( ) const;
    };

    // This stores data for all instances (can be used by multiple vaRenderInstanceList-s). The data is:
    //  - List of vaRenderInstance-s that can be accessed by vaRenderInstanceList or mesh renderer
    //  - GPU buffer with (write-only) shader constants (ShaderInstanceConstants) used to draw the above instances
    // It is used by vaSceneRenderInstanceProcessor to convert scene items into renderable items. Could be used separately too.
    // It can only manage ONE 'pass' per frame due to GPU sync. Use multiple instances of vaRenderInstanceStorage if needed, 
    // or upgrade to using more upload constants buffers.
    class vaRenderInstanceStorage : public vaRenderingModule
    {
        // these is the buffer that is read by the GPU
        shared_ptr<vaRenderBuffer>          m_renderConstants;

        // these are the buffers that are written into by the CPU and uploaded to m_frameConstants once per frame
        shared_ptr<vaRenderBuffer>          m_uploadConstants[vaRenderDevice::c_BackbufferCount];

        // current buffers capacity in instances
        uint32                              m_instanceMaxCount  = 0;

        int64                               m_lastFrameIndex    = -1;
        int                                 m_currentBackbuffer = 0;
        std::atomic_bool                    m_started           = false;                    // set on StartWriting
        bool                                m_stopped           = false;                    // set on StopAndUpload - GetInstanceArray data can be used all until it's cleared

        ShaderInstanceConstants *           m_mappedUploadShaderInstanceConstants = nullptr;
        std::vector<vaRenderInstance>       m_instances;


    protected:
        VA_RENDERING_MODULE_MAKE_FRIENDS( );
        vaRenderInstanceStorage( const vaRenderingModuleParams & params );
    public:
        ~vaRenderInstanceStorage( );

        // can be called from the non-master thread (but only once per StopAndUpload)
        void                                StartWriting( uint32 instanceMaxCount );
        ShaderInstanceConstants *           GetShaderConstantsUploadArray( ) const          { assert(m_started || m_stopped); return m_mappedUploadShaderInstanceConstants; }
        vaRenderInstance *                  GetInstanceArray( )                             { assert(m_started || m_stopped); return m_instances.data(); }
        uint32                              GetInstanceMaxCount( ) const                    { assert(m_started || m_stopped); return m_instanceMaxCount; }
        const shared_ptr<vaRenderBuffer> &  GetInstanceRenderBuffer( ) const                { return m_renderConstants; }

        // can only called from the main thread and caller must ensure StartWriting has completed
        void                                StopAndUpload( vaRenderDeviceContext & renderContext, uint32 instanceCount );

        const shared_ptr<vaRenderBuffer> &  GetRenderConstants( ) const;
    };

}
