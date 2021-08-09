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

#include "vaRenderMeshDX12.h"

#include "Rendering/DirectX/vaRenderDeviceContextDX12.h"
#include "Rendering/DirectX/vaRenderBuffersDX12.h"

#include "Rendering/vaAssetPack.h"

using namespace Vanilla;

void vaRenderMeshDX12::SetParentAsset( vaAsset * asset )
{
    vaAssetResource::SetParentAsset(asset); 
    if( asset != nullptr )
    {
        // SetName( asset->Name() ); 
    }
}

void vaRenderMeshDX12::RT_CreateBLASDataIfNeeded( )
{
    string parentAssetName = "System";
    if( GetParentAsset( ) != nullptr )
        parentAssetName = GetParentAsset( )->Name( );

    if( m_RT_BLASData == nullptr || m_RT_BLASData->GetDataSize( ) < m_RT_prebuildInfo.ResultDataMaxSizeInBytes )
        m_RT_BLASData = vaRenderBuffer::Create( GetRenderDevice( ), m_RT_prebuildInfo.ResultDataMaxSizeInBytes, 1, vaRenderBufferFlags::RaytracingAccelerationStructure, parentAssetName + "_" + "RT_MeshBLAS" );
}

void vaRenderMeshDX12::UpdateGPURTData( vaRenderDeviceContext & renderContext )
{
    renderContext;
    assert( !renderContext.IsWorker( ) );
    assert( !m_gpuDataDirty );

    if( !GetRenderDevice().GetCapabilities().Raytracing.Supported )
        return;

    D3D12_RAYTRACING_GEOMETRY_DESC & desc = m_RT_desc;
    desc.Type   = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    desc.Flags  = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;          // control for opaque/non-opaque on instance level because this is controlled by materials and can (theoretically, although not sure frequent in practice) be different on the same mesh

    desc.Triangles.Transform3x4 = {};
    desc.Triangles.IndexFormat  = DXGI_FORMAT_R32_UINT;
    desc.Triangles.IndexBuffer  = AsDX12(*m_indexBuffer).GetResource()->GetGPUVirtualAddress();
    desc.Triangles.IndexCount   = m_LODParts[0].IndexCount;
    desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    desc.Triangles.VertexCount  = (UINT)m_vertices.size();
    desc.Triangles.VertexBuffer = D3D12_GPU_VIRTUAL_ADDRESS_AND_STRIDE{ AsDX12(*m_vertexBuffer).GetGPUVirtualAddress(), sizeof(StandardVertex) };

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC & bottomLevelBuildDesc = m_RT_BLASBuildDesc;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS & bottomLevelInputs = bottomLevelBuildDesc.Inputs;
    bottomLevelInputs.Type              = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    bottomLevelInputs.DescsLayout       = D3D12_ELEMENTS_LAYOUT_ARRAY;
    bottomLevelInputs.Flags             = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;// | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    bottomLevelInputs.NumDescs          = 1;
    bottomLevelInputs.pGeometryDescs    = &desc;

    auto deviceDX12 = AsDX12(GetRenderDevice()).GetPlatformDevice().Get();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO & prebuildInfo = m_RT_prebuildInfo;
    deviceDX12->GetRaytracingAccelerationStructurePrebuildInfo( &bottomLevelInputs, &prebuildInfo );

    bottomLevelBuildDesc.SourceAccelerationStructureData = {0};
    //if( m_isBuilt && m_allowUpdate && m_updateOnBuild )
    //{
    //    bottomLevelInputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
    //    bottomLevelBuildDesc.SourceAccelerationStructureData = m_accelerationStructure->GetGPUVirtualAddress( );
    //}

    bottomLevelBuildDesc.ScratchAccelerationStructureData   = { 0 }; // INITIALIZE LATER!!
    bottomLevelBuildDesc.DestAccelerationStructureData      = { 0 }; // INITIALIZE LATER!!

    // request BLAS rebuild
    m_RT_BLASDataDirty = true;
}

void RegisterRenderMeshDX12( )
{
    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX12, vaRenderMesh, vaRenderMeshDX12 );
    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX12, vaRenderMeshManager, vaRenderMeshManagerDX12 );
}

