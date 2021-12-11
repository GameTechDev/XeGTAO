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

#include "Rendering/DirectX/vaSceneRaytracingDX12.h"

#include "Rendering/vaSceneRenderer.h"

#include "Rendering/vaRenderMesh.h"
#include "Rendering/vaRenderMaterial.h"

#include "Rendering/DirectX/vaDirectXTools.h"

//#include "DirectXRaytracingHelper.h"

#include "Rendering/DirectX/vaRenderMeshDX12.h"

#include "Scene/vaScene.h"

using namespace Vanilla;

vaSceneRaytracingDX12::vaSceneRaytracingDX12( const vaRenderingModuleParams & params ) : 
    vaSceneRaytracing( params )
{
}

vaSceneRaytracingDX12::~vaSceneRaytracingDX12( )
{
}

void vaSceneRaytracingDX12::PostRenderCleanupInternal( ) 
{
}

void vaSceneRaytracingDX12::PreRenderUpdateInternal( vaRenderDeviceContext & renderContext, const std::unordered_set<vaFramePtr<vaRenderMesh>> & meshes, const std::unordered_set<vaFramePtr<vaRenderMaterial>> & materials )
{
    VA_TRACE_CPUGPU_SCOPE( SceneRaytracingUpdate, renderContext );

    auto NULLBARRIER = CD3DX12_RESOURCE_BARRIER::UAV( nullptr );

    m_instanceDescsDX12CPU.clear();

    if( m_instanceCount == 0 )
        return;

    // materials handled in vaRenderMaterialManagerDX12
    materials;

    // Build all geometries 
    for( vaFramePtr<vaRenderMesh> meshPtr : meshes )
    {
        //GeometryItem & geom = m_geometryList[i];
        vaRenderMesh & mesh = *meshPtr; // *geom.Mesh;

        // // render mesh internal data might need update - this has to be done using unique lock so nobody else does it at the same time
        // std::shared_lock meshLock( mesh.Mutex( ) );

        vaRenderMeshDX12 & mesh12 = AsDX12( mesh );

        if( mesh12.RT_BLASDataDirty() )
        {
            auto & scratchResource = GetScratch( mesh12.RT_PrebuildInfo().ScratchDataSizeInBytes );
            mesh12.RT_CreateBLASDataIfNeeded();

            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC & bottomLevelBuildDesc = mesh12.RT_BLASBuildDesc( );

            vaRenderBuffer & indexBuffer = *mesh12.GetGPUIndexBuffer();
            vaRenderBuffer & vertexBuffer = *mesh12.GetGPUVertexBuffer();
            AsDX12(indexBuffer ).TransitionResource( AsDX12(renderContext), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
            AsDX12(vertexBuffer).TransitionResource( AsDX12(renderContext), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );

            bottomLevelBuildDesc.ScratchAccelerationStructureData   = AsDX12(*scratchResource).GetGPUVirtualAddress( );
            bottomLevelBuildDesc.DestAccelerationStructureData      = AsDX12(*mesh12.RT_BLASData()).GetGPUVirtualAddress( );
        
            AsDX12(renderContext).GetCommandList()->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc, 0, nullptr);

            // Since a single scratch resource is reused, put a barrier in-between each call.
            // PEFORMANCE tip: use separate scratch memory per BLAS build to allow a GPU driver to overlap build calls.
            AsDX12(renderContext).GetCommandList()->ResourceBarrier( 1, &NULLBARRIER ); // CD3DX12_RESOURCE_BARRIER::UAV( bottomLevelAS.GetResource( ) )
            mesh12.RT_SetBLASDataDirty( false );
        }
    }

    m_instanceDescsDX12CPU.resize( m_instanceCount );

    assert( m_instanceStorage != nullptr );
    vaRenderInstance * globalInstances = m_instanceStorage->GetInstanceArray( );
    assert( m_instanceStorage->GetInstanceMaxCount() == m_instanceList.size() );
    assert( m_instanceStorage->GetInstanceMaxCount() >= m_instanceCount );

    for( uint32 i = 0; i < m_instanceCount; i++ )
    {
        vaRenderInstance & instanceGlobal   = globalInstances[i];
        InstanceItem & instanceLocal        = m_instanceList[i];

        D3D12_RAYTRACING_INSTANCE_DESC & instanceDesc = m_instanceDescsDX12CPU[i];

        const vaMatrix4x4 & worldTransform  = instanceLocal.Transform;
        
        instanceDesc.AccelerationStructure  = AsDX12(*AsDX12(*instanceGlobal.Mesh).RT_BLASData()).GetGPUVirtualAddress();
        instanceDesc.Flags                  = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;

        if( !instanceGlobal.Material->IsAlphaTested( ) && !instanceGlobal.Material->IsNEETranslucent() )
            instanceDesc.Flags |= D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;

        vaWindingOrder frontFaceWinding = instanceGlobal.Mesh->GetFrontFaceWindingOrder();

        // non standard culling - let's invert winding so we can cull it
        if( instanceGlobal.Material->GetFaceCull() == vaFaceCull::Front )
            frontFaceWinding = (frontFaceWinding == vaWindingOrder::Clockwise)?(vaWindingOrder::CounterClockwise):(vaWindingOrder::Clockwise);

        // non standard winding order for the material - let DXR know
        if( frontFaceWinding == vaWindingOrder::CounterClockwise )
            instanceDesc.Flags |= D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE;

        // disable culling (overrides TraceRay flags)
        if( instanceGlobal.Material->GetFaceCull() == vaFaceCull::None )
            instanceDesc.Flags |= D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;


        // Hit group index determined by material global index!
        int callableShaderTableIndex = instanceGlobal.Material->GetCallableShaderTableIndex();
        assert( callableShaderTableIndex != -1 );
        instanceDesc.InstanceContributionToHitGroupIndex = callableShaderTableIndex;
        
        // Also exposing material global index as InstanceID here - it's used for computing callable shader index but this can be avoided if
        // needed (by reading it off instance constants - tiny bit more costly).
        instanceDesc.InstanceID                 = instanceGlobal.Material->GetGlobalIndex();

        assert( instanceLocal.InstanceIndex == i );
        instanceDesc.InstanceMask               = 1;

        // // TEMP TEMP TEMP - DISABLE TRANSPARENCIES FOR NOW
        // if( instanceGlobal.Material->IsTransparent() )
        //     instanceDesc.InstanceMask = 0;
        // // TEMP TEMP TEMP - DISABLE TRANSPARENCIES FOR NOW

        for( int r = 0; r < 3; r++ )
            for( int c = 0; c < 4; c++ )
                instanceDesc.Transform[r][c]    = worldTransform(c, r);
    }
    vaRenderDeviceDX12& device12 = AsDX12( GetRenderDevice( ) );

    shared_ptr<vaUploadBufferDX12> & instanceUploadBuffer   = m_instanceDescsDX12GPU[m_currentBackbuffer];
    shared_ptr<vaRenderBuffer> & TLASBuffer                 = m_topLevelAccelerationStructure[m_currentBackbuffer];
    uint64 instanceUploadBufferSize = vaMath::Align( std::max( 1024ULL, sizeof(D3D12_RAYTRACING_INSTANCE_DESC)*m_instanceDescsDX12CPU.size() ), 1024 );
    
    // allocate instance buffer if needed!
    if( instanceUploadBuffer == nullptr || instanceUploadBuffer->Size() < instanceUploadBufferSize )
        instanceUploadBuffer = std::make_shared<vaUploadBufferDX12>( device12, nullptr, instanceUploadBufferSize, L"RT_InstanceDescs" );

    // copy instances to GPU-readable memory
    if( m_instanceDescsDX12CPU.size() > 0 )
        memcpy( instanceUploadBuffer->MappedData(), m_instanceDescsDX12CPU.data(), sizeof(D3D12_RAYTRACING_INSTANCE_DESC)*m_instanceDescsDX12CPU.size() );

    // Get required sizes for an acceleration structure.
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS topLevelInputs = {};
    topLevelInputs.DescsLayout      = D3D12_ELEMENTS_LAYOUT_ARRAY;
    topLevelInputs.Flags            = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE; // D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD; 
    topLevelInputs.NumDescs         = (UINT)m_instanceDescsDX12CPU.size();
    topLevelInputs.Type             = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    topLevelInputs.InstanceDescs    = instanceUploadBuffer->GetGPUVirtualAddress( );

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo = {};
    device12.GetPlatformDevice()->GetRaytracingAccelerationStructurePrebuildInfo( &topLevelInputs, &topLevelPrebuildInfo );
    auto & scratchResource = GetScratch( topLevelPrebuildInfo.ScratchDataSizeInBytes );

    // allocate TLAS buffer if needed!
    if( TLASBuffer == nullptr || TLASBuffer->GetDataSize( ) < topLevelPrebuildInfo.ResultDataMaxSizeInBytes )
        TLASBuffer = vaRenderBuffer::Create( GetRenderDevice(), vaMath::Align( topLevelPrebuildInfo.ResultDataMaxSizeInBytes, 1024 ), 1, vaRenderBufferFlags::RaytracingAccelerationStructure, "RT_TopLevelAccelerationStructure" );

    if( m_nullAccelerationStructure == nullptr )
        m_nullAccelerationStructure = vaRenderBuffer::Create( GetRenderDevice(), 0, 1, vaRenderBufferFlags::RaytracingAccelerationStructure, "RT_NullAccelerationStructure" );

    // Top Level Acceleration Structure desc
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelBuildDesc = {};
    {
        topLevelBuildDesc.Inputs = topLevelInputs;
        topLevelBuildDesc.DestAccelerationStructureData     = AsDX12(*TLASBuffer).GetGPUVirtualAddress( );
        topLevelBuildDesc.ScratchAccelerationStructureData  = AsDX12(*scratchResource).GetGPUVirtualAddress( );
    }

    // Build acceleration structure.
    AsDX12( renderContext ).GetCommandList( )->ResourceBarrier( 1, &NULLBARRIER ); // CD3DX12_RESOURCE_BARRIER::UAV( bottomLevelAS.GetResource( ) )
    AsDX12( renderContext ).GetCommandList()->BuildRaytracingAccelerationStructure( &topLevelBuildDesc, 0, nullptr );
    AsDX12( renderContext ).GetCommandList( )->ResourceBarrier( 1, &NULLBARRIER ); // CD3DX12_RESOURCE_BARRIER::UAV( bottomLevelAS.GetResource( ) )

    //device12.GetPlatformDevice()
}



void RegisterRaytracingDX12( )
{
    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX12, vaSceneRaytracing, vaSceneRaytracingDX12 );
}
