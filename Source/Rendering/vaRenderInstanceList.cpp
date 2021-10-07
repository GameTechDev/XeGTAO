///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaRenderInstanceList.h"

#include "vaRenderMesh.h"
#include "vaRenderMaterial.h"

#include "vaSceneLighting.h"

#include "vaRenderBuffers.h"

#include <future>

//#undef VA_TASKFLOW_INTEGRATION_ENABLED

#ifdef VA_TASKFLOW_INTEGRATION_ENABLED
#include "IntegratedExternals/vaTaskflowIntegration.h"
#endif

namespace Vanilla
{
    class vaRenderInstanceListSorterInstance
    {
    private:

        std::vector<pair<int, float>>            m_sortDistances;          // first is a sort group - items first get sorted by it and then by distance
        std::vector<int>                         m_sortedIndices;

        vaRenderInstanceList::SortSettings     m_settings;
        vaRenderInstanceList &                 m_parent;
        int                                 m_sessionID         = -1;

#ifdef VA_TASKFLOW_INTEGRATION_ENABLED
        tf::Taskflow                        m_sortTaskFlow;
        std::future<void>                   m_sortFuture;
#endif
        bool                                m_finished = false;

    public:
        vaRenderInstanceListSorterInstance( vaRenderInstanceList & parent ) : m_parent( parent ) { }
        ~vaRenderInstanceListSorterInstance( ) { assert( m_sessionID == -1 ); }

        bool                                Initialize( const vaRenderInstanceList::SortSettings & sortSettings, int sessionID );
        bool                                Start( );
        int                                 Finish( const std::vector<int> * & sortedIndices );

        void                                Reset( )                
        {
            assert( vaThreading::IsMainThread( ) ); 
#ifdef VA_TASKFLOW_INTEGRATION_ENABLED
            if( !m_finished && m_sortFuture.valid() )
                m_sortFuture.wait( );
            m_sortFuture = std::future<void>();
            m_sortTaskFlow.clear();
#endif
            m_finished = false;
            m_sessionID = -1; 
            m_sortedIndices.clear(); 
            m_sortDistances.clear(); 
            m_settings = vaRenderInstanceList::SortSettings(); 
        }
    };

}

using namespace Vanilla;

bool vaRenderInstanceListSorterInstance::Initialize( const vaRenderInstanceList::SortSettings& sortSettings, int sessionID )
{
    assert( vaThreading::IsMainThread( ) );
    assert( m_sessionID == -1 );    // invalid sorting setup - please fix
    m_sessionID = sessionID;

    if( sortSettings.SortByDistanceToPoint && sortSettings.ReferencePoint.x == std::numeric_limits<float>::infinity( ) )
    {
        assert( false ); // you haven't updated sortSettings.ReferencePoint?
        Reset( );
        return false;
    }
    m_settings = sortSettings;
    return true;
}

bool vaRenderInstanceListSorterInstance::Start(  )
{
    auto drawList = m_parent.GetItems();

#ifdef VA_TASKFLOW_INTEGRATION_ENABLED
    assert( !m_sortFuture.valid() );
#endif

    // data sizes!
    m_sortDistances.resize( drawList.second );
    m_sortedIndices.resize( drawList.second );

    auto sortComp = [ This = this ] ( int ia, int ib ) -> bool
    {
        // first sort by special type
        if( This->m_sortDistances[ia].first != This->m_sortDistances[ib].first )
            return This->m_sortDistances[ia].first < This->m_sortDistances[ib].first;
        else // then by distance
            return ( This->m_settings.FrontToBack ) ? ( This->m_sortDistances[ia].second < This->m_sortDistances[ib].second ) : ( This->m_sortDistances[ia].second > This->m_sortDistances[ib].second );
    };

    auto sortCallback = [ drawList, renderInstances = m_parent.GetGlobalInstanceArray(), &sortedIndices = m_sortedIndices, &sortDistances = m_sortDistances, &settings = m_settings]( )
    {
#ifndef VA_TASKFLOW_INTEGRATION_ENABLED
        VA_TRACE_CPU_SCOPE( RenderSelectionSort_Prepare );
#endif
        for( uint32 i = 0; i < drawList.second; i++ )
        {
            // auto & itemLocal    = drawList.first[i];
            auto & itemGlobal   = renderInstances[drawList.first[i].InstanceIndex];

            assert( itemGlobal.Material != nullptr );    // not allowed anymore

            int sortGroup = 0;

            // special decal case - they get rendered with opaque but go after all non-decal stuff has rendered regardless of FrontToBack/BackToFront sort order 
            if( itemGlobal.Flags.IsDecal )
                sortGroup = vaMath::Clamp( itemGlobal.Material->GetMaterialSettings( ).DecalSortOrder, -65536, 65536 ) + 100000;
            //else if( settings.SortByVRSType )
            //    sortGroup = (int)item.Properties.ShadingRate;
            //assert( itemA.Transform == itemLocal.Transform );

            //sortDistances[i] = { sortGroup, ( vaVector3::TransformCoord( itemGlobal.Mesh->GetAABB( ).Center( ), itemGlobal.Transform ) - settings.ReferencePoint ).Length( ) };
            sortDistances[i] = { sortGroup, itemGlobal.DistanceFromRef };
            sortedIndices[i] = i;
        }
    };

#ifdef VA_TASKFLOW_INTEGRATION_ENABLED
    tf::Task prepareTask = m_sortTaskFlow.emplace( sortCallback ).name( "RenderSelectionSortPrepare" );
    tf::Task sortTask = m_sortTaskFlow.sort(  m_sortedIndices.begin( ), m_sortedIndices.end( ), std::move(sortComp) ).name( "RenderSelectionSort" );
    prepareTask.precede(sortTask);
    m_sortFuture = vaTF::Executor().run( m_sortTaskFlow );
#else
    sortCallback( );
    {
        VA_TRACE_CPU_SCOPE( RenderSelectionSort_Sort );
        std::sort( sortedIndices.begin( ), sortedIndices.end( ), sortComp );
    }
#endif

    return true;
}

int vaRenderInstanceListSorterInstance::Finish( const std::vector<int> * & sortedIndices )
{
    assert( vaThreading::IsMainThread( ) );
    assert( m_sessionID != -1 );    // invalid sorting setup - please fix

    if( !m_finished )
    {
#ifdef VA_TASKFLOW_INTEGRATION_ENABLED
        assert( m_sortFuture.valid() );
        m_sortFuture.wait();
        m_sortTaskFlow.clear();
#endif
        m_finished = true;
    }
    assert( m_sortedIndices.size( ) == m_sortDistances.size( ) ); // some kind of serious bug - please fix
   
    sortedIndices = &m_sortedIndices;

    return m_sessionID;
}

int vaRenderInstanceList::s_instanceCounter = 0;

vaRenderInstanceList::vaRenderInstanceList( ) : m_instanceID( s_instanceCounter )
{
    assert( vaThreading::IsMainThread( ) );

    s_instanceCounter++;
    // This class is not supposed to be instantiated "too frequently" (i.e. every frame) but created and reused - if this is what's happening then there's a bug.
    // If this isn't what's happening then please update this with some smarter heuristic (such as allow more creations over time or whatever).
    assert( s_instanceCounter < 1000 );

    Reset( );
}

vaRenderInstanceList::~vaRenderInstanceList( ) 
{ 
    assert( vaThreading::IsMainThread( ) );
    Reset( ); 
}

void vaRenderInstanceList::StartCollecting( const shared_ptr<vaRenderInstanceStorage> & instanceStorage )
{
    VA_TRACE_CPU_SCOPE( RenderSelectionStart );

    assert( m_ready );      // forgot to call Reset from last frame?
    m_ready         = false;
    assert( !m_started );

    m_started       = true;
    m_selectResults = (uint32)vaDrawResultFlags::None;

    m_list->StartAppending();

    m_instanceStorage = instanceStorage;
}

void vaRenderInstanceList::StopCollecting( )
{
    // VA_TRACE_CPU_SCOPE( RenderSelectionStop );
    
    assert( !m_ready );      // no idea how this could happen - a bug.

    bool prevStarted = m_started.exchange( false );
    assert( prevStarted ); prevStarted;

    //m_instanceShaderConstantsArray = m_instanceStorage->GetShaderConstantsUploadArray();
    m_instanceArray                = m_instanceStorage->GetInstanceArray();
    m_instanceMaxCount             = m_instanceStorage->GetInstanceMaxCount();

    bool startedCorrectly = m_list->StartConsuming( );
    assert( startedCorrectly ); startedCorrectly;

    // Start executing all active sorters
    for( int i = 0; i < m_activeSorters.size( ); i++ )
        m_activeSorters[i]->Start( );
}

void vaRenderInstanceList::Reset( )
{
    VA_TRACE_CPU_SCOPE( RenderSelectionReset );

    assert( vaThreading::IsMainThread( ) );
    assert( !m_started ); // forgot to Stop?
    if( m_started )
        StopCollecting( );
    for( int i = 0; i < m_activeSorters.size(); i++ )
    {
        m_activeSorters[i]->Reset( );
        m_inactiveSorters.push_back( m_activeSorters[i] );
    }
    m_activeSorters.clear();

    // m_list->Clear(); <- happens on transition to 'appending'

    //m_instanceShaderConstantsArray  = nullptr;
    m_instanceArray                 = nullptr;
    m_instanceMaxCount              = 0;
    m_instanceStorage               = nullptr;

    m_resetCounter++;
    m_ready = true;
}

vaRenderInstanceList::SortHandle vaRenderInstanceList::ScheduleSort( const SortSettings & settings )
{
    assert( vaThreading::IsMainThread( ) );
    assert( !m_started );   // can call Sort between selection Start/Stop calls
    assert( m_ready );      // forgot to call Reset?
    if( m_started || !m_ready )
        return EmptySortHandle;

    shared_ptr<vaRenderInstanceListSorterInstance> sortInstance;
    if( m_inactiveSorters.size( ) > 0 )
    {
        sortInstance = m_inactiveSorters.back();
        m_inactiveSorters.pop_back();
    }
    else
        sortInstance = std::make_shared< vaRenderInstanceListSorterInstance >( *this );

    if( !sortInstance->Initialize( settings, m_resetCounter ) )
    {
        assert( false );    // some error starting sort - please fix
        m_inactiveSorters.push_back( sortInstance );
        return EmptySortHandle;
    }

    m_activeSorters.push_back( sortInstance );
    return (vaRenderInstanceList::SortHandle)((int64(m_instanceID) << 32) | (m_activeSorters.size()-1));
}

const std::vector<int> * vaRenderInstanceList::GetSortIndices( SortHandle sortHandle ) const
{
    VA_TRACE_CPU_SCOPE( GetSortIndices );
    assert( vaThreading::IsMainThread( ) );
    if( sortHandle == EmptySortHandle )
        return nullptr;
    int parentID = (0xFFFFFFFF00000000 & sortHandle) >> 32;
    if( parentID != m_instanceID )
    {
        // error - SortHandle not valid
        assert( false );
        return nullptr;
    }
    int sortInstanceIndex = 0xFFFFFFFF & sortHandle;
    if( sortInstanceIndex  < 0 || sortInstanceIndex  >= m_activeSorters.size( ) )
    {
        // error - SortHandle not valid
        assert( false );
        return nullptr;
    }
    shared_ptr<vaRenderInstanceListSorterInstance> sortInstance = m_activeSorters[sortInstanceIndex];
    const std::vector<int> * sortedIndices = nullptr;
    if( sortInstance->Finish( sortedIndices ) != m_resetCounter )
    {
        // mismatch in ID - this sort handle is probably outdated
        assert( false );
        return nullptr;
    }
    return sortedIndices;
}

vaRenderInstanceList::FilterSettings vaRenderInstanceList::FilterSettings::FrustumCull( const vaCameraBase & camera )
{
    FilterSettings ret;
    ret.FrustumPlanes.resize( 6 );
    camera.CalcFrustumPlanes( &ret.FrustumPlanes[0] );
    return ret;
}

// depends on the shadowmap type
vaRenderInstanceList::FilterSettings vaRenderInstanceList::FilterSettings::ShadowmapCull( const vaShadowmap & shadowmap )
{
    FilterSettings ret;
    shadowmap.SetToRenderSelectionFilter( ret );
    return ret;
}

vaRenderInstanceList::FilterSettings vaRenderInstanceList::FilterSettings::EnvironmentProbeCull( const Scene::IBLProbe & probeData )
{
    FilterSettings ret;

    probeData;
    // make a frustum cube based on
    // probeData.Position
    // probeData.ClipFar

    return ret;
}

void vaRenderInstanceList::Insert( uint32 instanceIndex, vaShadingRate shadingRate )
{
    if( !m_started || m_ready )
    {
        assert( false );    // this isn't valid - you've called Sort or started rendering or forgot to Reset?
        return;
    }
    m_list->Append( { instanceIndex, shadingRate } );
}

const shared_ptr<vaRenderBuffer> & vaRenderInstanceList::GetGlobalInstanceRenderBuffer( ) const      
{ 
    return m_instanceStorage->GetInstanceRenderBuffer(); 
}

vaRenderInstanceStorage::vaRenderInstanceStorage( const vaRenderingModuleParams & params )
    : vaRenderingModule( params )
{

}
vaRenderInstanceStorage::~vaRenderInstanceStorage( )
{

}

const shared_ptr<vaRenderBuffer> & vaRenderInstanceStorage::GetRenderConstants( ) const
{
    assert( GetRenderDevice().IsRenderThread() );

    // Skipped on StartWriting/StopAndUpload this frame?
    assert( m_lastFrameIndex == GetRenderDevice().GetCurrentFrameIndex() );

    assert( !m_started );
    assert( m_stopped );

    return m_renderConstants;
}

void vaRenderInstanceStorage::StartWriting( uint32 instanceMaxCount )
{
    // started without previously stopping?
    assert( !m_started );
    m_started = true;
    m_stopped = false;

    // do we have enough space? also, support instanceMaxCount of 0
    if( ((int64)m_instanceMaxCount < (int64)instanceMaxCount) || (m_instanceMaxCount == 0) )
    {
        m_instanceMaxCount = vaMath::Align( std::max( 1U, instanceMaxCount ), 1024 );

        m_renderConstants = vaRenderBuffer::Create<ShaderInstanceConstants>( GetRenderDevice(), m_instanceMaxCount, vaRenderBufferFlags::None, "InstancesConstantBuffer" );
        for( int i = 0; i < vaRenderDevice::c_BackbufferCount; i++ )
            m_uploadConstants[i] = vaRenderBuffer::Create<ShaderInstanceConstants>( GetRenderDevice(), m_instanceMaxCount, vaRenderBufferFlags::Upload, "InstancesUploadConstantBuffer" );
    }

    m_mappedUploadShaderInstanceConstants = static_cast<ShaderInstanceConstants*>(m_uploadConstants[m_currentBackbuffer]->GetMappedData( ));
    m_instances.resize( instanceMaxCount );
}

void vaRenderInstanceStorage::StopAndUpload( vaRenderDeviceContext & renderContext, uint32 instanceCount )
{
    assert( GetRenderDevice().IsRenderThread() );
    //VA_TRACE_CPUGPU_SCOPE( InstanceSTorageStopAndUpload, renderContext );

    m_mappedUploadShaderInstanceConstants = nullptr;
    m_instances.clear();

    // due to resource management, one vaRenderInstanceStorage instance can only handle being used once per frame; this restriction could be removed if need be.
    assert( m_lastFrameIndex < GetRenderDevice().GetCurrentFrameIndex() );
    m_lastFrameIndex    = GetRenderDevice().GetCurrentFrameIndex();

    // Skipped on StartWriting this frame?
    assert( m_lastFrameIndex == GetRenderDevice().GetCurrentFrameIndex() );
    // Started and not already stopped?
    assert( m_started && !m_stopped );
    assert( instanceCount <= m_instanceMaxCount );

    if( instanceCount > 0 )
        m_renderConstants->CopyFrom( renderContext, *m_uploadConstants[m_currentBackbuffer], instanceCount * sizeof(ShaderInstanceConstants) );

    // advance to next for writing
    m_currentBackbuffer = (m_currentBackbuffer+1)%vaRenderDevice::c_BackbufferCount;
    m_started = false;
    m_stopped = true;
}

void vaRenderInstance::WriteToShaderConstants( ShaderInstanceConstants & outConstants ) const
{
    // !!Warning!! !!Warning!! !!Warning!! !!Warning!! !!Warning!! !!Warning!! !!Warning!! !!Warning!! !!Warning!!
    // The 'outConstant' is likely pointing to UPLOAD heap so never read from it below, as it could be very slow
    // !!Warning!! !!Warning!! !!Warning!! !!Warning!! !!Warning!! !!Warning!! !!Warning!! !!Warning!! !!Warning!!

    outConstants.World                  = vaMatrix4x3(Transform);

    // since we now support non-uniform scale, we need the 'normal matrix' to keep normals correct 
    // (for more info see : https://www.scratchapixel.com/lessons/mathematics-physics-for-computer-graphics/geometry/transforming-normals or http://www.lighthouse3d.com/tutorials/glsl-12-tutorial/the-normal-matrix/ )
    vaMatrix4x4 normalWorld             = Transform.FastTransformInversed( ).Transposed( );
    normalWorld.Row( 0 ).w              = 0.0f; normalWorld.Row( 1 ).w = 0.0f; normalWorld.Row( 2 ).w = 0.0f;
    normalWorld.Row( 3 ).x              = 0.0f; normalWorld.Row( 3 ).y = 0.0f; normalWorld.Row( 3 ).z = 0.0f; normalWorld.Row( 3 ).w = 1.0f;
    outConstants.NormalWorld            = vaMatrix4x3(normalWorld);

    outConstants.OriginInfo             = OriginInfo;

    outConstants.EmissiveMultiplier     = EmissiveMul;

    outConstants.MaterialGlobalIndex    = Material->GetGlobalIndex( );
    outConstants.MeshGlobalIndex        = Mesh->GetGlobalIndex( );
    outConstants.EmissiveAddPacked      = Pack_R10G10B10FLOAT_A2_UNORM( EmissiveAdd );
    outConstants.Flags                  = 0;
    if( Material->IsTransparent( ) )
        outConstants.Flags |= VA_INSTANCE_FLAG_TRANSPARENT;
}

vaRenderInstanceSimple::vaRenderInstanceSimple( const shared_ptr<vaRenderMesh> & mesh, const vaMatrix4x4 & transform ) 
{ 
    SetDefaults(); 
    Transform   = transform; 
    Mesh        = mesh; 
    Material    = mesh->GetMaterial();
    if( Material == nullptr )
        Material = mesh->GetRenderDevice( ).GetMaterialManager( ).GetDefaultMaterial( );
}

vaRenderInstanceSimple::vaRenderInstanceSimple( const shared_ptr<vaRenderMesh> & mesh,const vaMatrix4x4 & transform, const shared_ptr<vaRenderMaterial> & overrideMaterial, vaShadingRate shadingRate, const vaVector4 & emissiveAdd, const vaVector3 & emissiveMul, float meshLOD )
{ 
    SetDefaults();
    Transform   = transform;
    Mesh        = mesh;
    Material    = overrideMaterial;
    ShadingRate = shadingRate;
    EmissiveAdd = emissiveAdd;
    EmissiveMul = emissiveMul;
    MeshLOD     = meshLOD;
    if( Material == nullptr )
        Material = mesh->GetRenderDevice( ).GetMaterialManager( ).GetDefaultMaterial( );
}

void vaRenderInstanceSimple::SetDefaults( )
{
    Transform                   = vaMatrix4x4::Identity;
    Mesh                        = nullptr;
    Material                    = nullptr;
    ShadingRate                 = vaShadingRate::ShadingRate1X1;        // per-draw-call shading rate
    EmissiveAdd                 = vaVector4( 0.0f, 0.0f, 0.0f, 1.0f );  // for debugging visualization (default means "do not override"); used for highlights, wireframe, lights, etc; rgb is added, alpha multiplies the original; for ex: " finalColor.rgb = finalColor.rgb * g_instance.EmissiveAdd.a + g_instance.EmissiveAdd.rgb; "
    EmissiveMul                 = vaVector3( 1.0f, 1.0f, 1.0f );
    MeshLOD                     = 0.0f;
    OriginInfo.SceneID          = DrawOriginInfo::NullSceneRuntimeID;
    OriginInfo.EntityID         = DrawOriginInfo::NullSceneEntityID;
    OriginInfo.MeshAssetID      = DrawOriginInfo::NullAssetID;
    OriginInfo.MaterialAssetID  = DrawOriginInfo::NullAssetID;
    Flags.IsDecal               = false;
    Flags.IsWireframe           = false;
}