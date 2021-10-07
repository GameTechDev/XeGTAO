///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaRenderDeviceContextDX12.h"
#include "vaTextureDX12.h"

#include "vaRenderBuffersDX12.h"

#include "Rendering/vaTextureHelpers.h"

#include "Rendering/DirectX/vaSceneRaytracingDX12.h"
#include "Rendering/DirectX/vaRenderMaterialDX12.h"

#ifdef VA_TASKFLOW_INTEGRATION_ENABLED
#include "IntegratedExternals/vaTaskflowIntegration.h"
#endif

// Attempt to solve this warning from PIX: 
// At one point during the capture there were over 2000001 shader - visible CBV / SRV / UAV descriptors in all CBV / SRV / UAV descriptor heaps.There were also over 128 shader - visible sampler descriptors at one( possibly different ) point during the capture.
// On some hardware, having more than 1 million CBV / SRV / UAV descriptors or more than 2048 samplers across all descriptor heaps can make it expensive to switch between descriptor heaps.PIX has detected that the application switched between descriptor heaps at least once during an ExecuteCommandLists( ) call in this capture.
// Consider reducing the number of descriptors in the application to avoid the risk of this.
// #define FLUSH_ON_DESCRIPTOR_HEAP_CHANGE

// #include "Rendering/DirectX/vaRenderGlobalsDX11.h"

// #include "vaResourceFormatsDX11.h"

#ifdef _DEBUG
#define VA_SET_UNUSED_DESC_TO_NULL
#endif


using namespace Vanilla;

// // used to make Gather using UV slightly off the border (so we get the 0,0 1,0 0,1 1,1 even if there's a minor calc error, without adding the half pixel offset)
// static const float  c_minorUVOffset = 0.00006f;  // less than 0.5/8192

vaRenderDeviceContextBaseDX12::vaRenderDeviceContextBaseDX12( vaRenderDevice & device, const shared_ptr<vaRenderDeviceContextDX12> & master, int instanceIndex, bool useBundles )
    : vaRenderDeviceContext( device, master, instanceIndex ), m_deviceDX12( AsDX12(device) ), m_useBundles( useBundles )
{ 
    //m_localGraphicsPSOCache.max_load_factor(0.5f);

    HRESULT hr;
    bool workerContext = master != nullptr;
    wstring commandListName = (workerContext)?(L"WorkerList"):(L"MasterList");
    if( workerContext )
        commandListName += vaStringTools::Format( L"%02d", instanceIndex );

    if( m_useBundles ) { assert( workerContext ); } // only worker contexts can be bundle type

    D3D12_COMMAND_LIST_TYPE commandListType;

    if( m_useBundles )
        commandListType = D3D12_COMMAND_LIST_TYPE_BUNDLE;
    else
        commandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;

    auto& d3d12Device = AsDX12( device ).GetPlatformDevice( );

    // Create command allocator for each frame.
    for( uint i = 0; i < vaRenderDeviceDX12::c_BackbufferCount; i++ )
    {
        V( d3d12Device->CreateCommandAllocator( commandListType, IID_PPV_ARGS( &m_commandAllocators[i] ) ) );

        wstring name = commandListName + vaStringTools::Format( L"Allocator%d", i );
        V( m_commandAllocators[i]->SetName( name.c_str( ) ) );
    }

    uint32 currentFrame = AsDX12( GetRenderDevice( ) ).GetCurrentFrameFlipIndex( );

    // Create the command list.
    ComPtr<ID3D12GraphicsCommandList>  commandList;
    V( d3d12Device->CreateCommandList( 0, commandListType, m_commandAllocators[currentFrame].Get( ), nullptr, IID_PPV_ARGS( &commandList ) ) );
    commandList.As( &m_commandList );
    assert( commandList != nullptr );
    V( m_commandList->SetName( commandListName.c_str( ) ) );

    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
    V( m_commandList->Close( ) );
}

vaRenderDeviceContextBaseDX12::~vaRenderDeviceContextBaseDX12( )
{ 
    m_commandList.Reset( );

    for( uint i = 0; i < vaRenderDeviceDX12::c_BackbufferCount; i++ )
        m_commandAllocators[i].Reset( );
}

void vaRenderDeviceContextDX12::PreAllocateTransientDescriptors( )
{
    assert( vaThreading::IsMainThread() );

    if( m_nextTransientDesc_GlobalUAVs == -1 || m_nextTransientDesc_OutputsUAVs == -1 || m_nextTransientDesc_GlobalSRVs == -1 )
        m_nextTransientDesc_Globals = -1;
    if( m_nextTransientDesc_Globals != -1 )
        return;

    // beware, this can trigger flush and sync!
    m_nextTransientDesc_Globals = m_deviceDX12.TransientDescHeapAllocate( vaRenderDeviceDX12::DefaultRootSignatureParams::GlobalUAVSRVRangeSize );

    // these are now just offsets used for copying descriptors - don't set them individually as a root parameter, it's all set through one above (m_nextTransientDesc_Globals)
    m_nextTransientDesc_GlobalUAVs  = m_nextTransientDesc_Globals + vaRenderDeviceDX12::DefaultRootSignatureParams::DescriptorOffsetGlobalUAV;
    m_nextTransientDesc_OutputsUAVs = m_nextTransientDesc_Globals + vaRenderDeviceDX12::DefaultRootSignatureParams::DescriptorOffsetOutputsUAV;
    m_nextTransientDesc_GlobalSRVs  = m_nextTransientDesc_Globals + vaRenderDeviceDX12::DefaultRootSignatureParams::DescriptorOffsetGlobalSRV;

    // share these with workers - these are all the same for all workers so the only thing they
    // need to do is bind the heaps, no need to fill them up!
    for( int i = 0; i < m_workersActive; i++ )
    {
        m_workers[i]->m_nextTransientDesc_Globals       = m_nextTransientDesc_Globals;
        m_workers[i]->m_nextTransientDesc_GlobalSRVs    = m_nextTransientDesc_GlobalSRVs;
        m_workers[i]->m_nextTransientDesc_GlobalUAVs    = m_nextTransientDesc_GlobalUAVs;
        m_workers[i]->m_nextTransientDesc_OutputsUAVs   = m_nextTransientDesc_OutputsUAVs;
    }
}

void vaRenderDeviceContextBaseDX12::CommitTransientDescriptors( )
{
    assert( m_itemsStarted != vaRenderTypeFlags::None );

    D3D12_GPU_DESCRIPTOR_HANDLE baseDesc = m_deviceDX12.TransientDescHeapComputeGPUHandle( m_nextTransientDesc_Globals );
    if( ( m_itemsStarted & vaRenderTypeFlags::Graphics ) != 0 )
        m_commandList->SetGraphicsRootDescriptorTable( vaRenderDeviceDX12::DefaultRootSignatureParams::GlobalUAVSRVBase, baseDesc );
    if( ( m_itemsStarted & vaRenderTypeFlags::Compute ) != 0 )
        m_commandList->SetComputeRootDescriptorTable( vaRenderDeviceDX12::DefaultRootSignatureParams::GlobalUAVSRVBase, baseDesc );
    m_nextTransientDesc_Globals     = -1;
    m_nextTransientDesc_GlobalUAVs  = -1;
    m_nextTransientDesc_OutputsUAVs = -1;
    m_nextTransientDesc_GlobalSRVs  = -1;

    // bindless!
    D3D12_GPU_DESCRIPTOR_HANDLE bindlessDesc = m_deviceDX12.GetBindlessDescHeapGPUHandle();
    if( ( m_itemsStarted & vaRenderTypeFlags::Graphics ) != 0 )
    {
        m_commandList->SetGraphicsRootDescriptorTable( vaRenderDeviceDX12::DefaultRootSignatureParams::Bindless1SRVBase, bindlessDesc );
        m_commandList->SetGraphicsRootDescriptorTable( vaRenderDeviceDX12::DefaultRootSignatureParams::Bindless2SRVBase, bindlessDesc );
    }

    if( ( m_itemsStarted & vaRenderTypeFlags::Compute ) != 0 )
    {
        m_commandList->SetComputeRootDescriptorTable( vaRenderDeviceDX12::DefaultRootSignatureParams::Bindless1SRVBase, bindlessDesc );
        m_commandList->SetComputeRootDescriptorTable( vaRenderDeviceDX12::DefaultRootSignatureParams::Bindless2SRVBase, bindlessDesc );
    }
}

void vaRenderDeviceContextBaseDX12::BindDefaultStates( )
{
    assert( m_commandListReady );
    m_deviceDX12.BindDefaultDescriptorHeaps( m_commandList.Get() );

    m_commandList->SetGraphicsRootSignature( m_deviceDX12.GetDefaultGraphicsRootSignature()  );
    m_commandList->SetComputeRootSignature( m_deviceDX12.GetDefaultComputeRootSignature() );

    // // some other default states
    // FLOAT defBlendFactor[4] = { 1, 1, 1, 1 };
    // m_commandList->OMSetBlendFactor( defBlendFactor ); // If you pass NULL, the runtime uses or stores a blend factor equal to { 1, 1, 1, 1 }.
    // m_commandList->OMSetStencilRef( 0 );

    m_currentTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    m_commandList->IASetPrimitiveTopology( m_currentTopology );

    m_currentIndexBuffer    = nullptr;
    m_commandList->IASetIndexBuffer( nullptr );
    m_currentVertexBuffer   = nullptr;
    m_commandList->IASetVertexBuffers( 0, 0, nullptr );

    m_currentPSO            = nullptr;

// 1x1 is the default
    m_currentShadingRate = D3D12_SHADING_RATE_1X1;
    const vaRenderDeviceCapabilities & caps = GetRenderDevice().GetCapabilities();
    if( m_commandList != nullptr && caps.VariableShadingRate.Tier1 )
        m_commandList->RSSetShadingRate( m_currentShadingRate, nullptr );

    // this is not needed for worker thread with bundles I think?
    ResetCachedOutputs( );
}

void vaRenderDeviceContextBaseDX12::ResetAndInitializeCommandList( int currentFrame )
{
    if( !IsWorker() )
    {
        assert( m_itemsStarted == vaRenderTypeFlags::None );
    }
    assert( !m_commandListReady );
    HRESULT hr;
    {
        VA_TRACE_CPU_SCOPE( Reset );
    V( m_commandList->Reset( m_commandAllocators[currentFrame].Get(), nullptr ) );
    }
    m_commandListReady = true;

    BindDefaultStates();
}

void vaRenderDeviceContextDX12::ExecuteCommandList( )
{
    assert( GetRenderDevice().IsRenderThread() );
    assert( m_itemsStarted == vaRenderTypeFlags::None );
    assert( GetRenderDevice( ).IsFrameStarted( ) );

    HRESULT hr;
    uint32 currentFrame = m_deviceDX12.GetCurrentFrameFlipIndex( ) ;

    assert( m_commandListReady );
    V( m_commandList->Close() );
    m_commandListReady = false;

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_deviceDX12.GetCommandQueue()->ExecuteCommandLists( _countof(ppCommandLists), ppCommandLists );
    m_itemsSubmittedAfterLastExecute = 0;

#ifdef VA_D3D12_USE_DEBUG_LAYER_DRED
    hr = AsDX12( GetRenderDevice() ).GetPlatformDevice()->GetDeviceRemovedReason( );
    if( hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_HUNG )
        AsDX12( GetRenderDevice() ).DeviceRemovedHandler( );
#endif

    ResetAndInitializeCommandList( currentFrame );

    // these are no longer valid
    m_nextTransientDesc_GlobalSRVs  = -1;
    m_nextTransientDesc_GlobalUAVs  = -1;
    m_nextTransientDesc_OutputsUAVs = -1;
    for( int i = 0; i < m_workersActive; i++ )
    {
        m_workers[i]->m_nextTransientDesc_GlobalSRVs    = -1;
        m_workers[i]->m_nextTransientDesc_GlobalUAVs    = -1;
        m_workers[i]->m_nextTransientDesc_OutputsUAVs   = -1;
    }
}

void vaRenderDeviceContextDX12::Flush( )
{
    assert( !IsWorker() );
    ExecuteCommandList();
}

void vaRenderDeviceContextBaseDX12::CommitOutputsRaw( vaRenderTypeFlags typeFlags, const vaRenderOutputs & outputs )
{
    typeFlags;
    D3D12_CPU_DESCRIPTOR_HANDLE     RTVs[vaRenderOutputs::c_maxRTs];
    //ID3D11UnorderedAccessView * UAVs[ c_maxUAVs];
    D3D12_CPU_DESCRIPTOR_HANDLE     DSV = { 0 };
    int numRTVs = 0;
    for( int i = 0; i < vaRenderOutputs::c_maxRTs; i++ )
    {
        RTVs[i] = { 0 };
        if( outputs.RenderTargets[i] != nullptr )
        {
            assert( ( typeFlags & vaRenderTypeFlags::Graphics ) != 0 );
            const vaRenderTargetViewDX12* rtv = AsDX12( *outputs.RenderTargets[i] ).GetRTV( );
            if( rtv != nullptr && rtv->IsCreated( ) )
            {
                AsDX12( *outputs.RenderTargets[i] ).TransitionResource( *this, D3D12_RESOURCE_STATE_RENDER_TARGET );
                RTVs[i] = rtv->GetCPUHandle( );
                numRTVs = i + 1;
            }
            else
            {
                assert( false );
            }    // error, texture has no rtv but set as a render target
        }
    }

    D3D12_CPU_DESCRIPTOR_HANDLE* pDSV = nullptr;
    if( outputs.DepthStencil != nullptr )
    {
        assert( ( typeFlags & vaRenderTypeFlags::Graphics ) != 0 );
        const vaDepthStencilViewDX12* dsv = AsDX12( *outputs.DepthStencil ).GetDSV( );
        if( dsv != nullptr && dsv->IsCreated( ) )
        {
            AsDX12( *outputs.DepthStencil ).TransitionResource( *this, D3D12_RESOURCE_STATE_DEPTH_WRITE );
            DSV = dsv->GetCPUHandle( );
            pDSV = &DSV;
        }
        else
        {
            assert( false );
        }    // error, texture has no dsv but set as a render target
    }

    const vaViewport& vavp = outputs.Viewport;

    D3D12_VIEWPORT viewport;
    viewport.TopLeftX = (float)vavp.X;
    viewport.TopLeftY = (float)vavp.Y;
    viewport.Width = (float)vavp.Width;
    viewport.Height = (float)vavp.Height;
    viewport.MinDepth = vavp.MinDepth;
    viewport.MaxDepth = vavp.MaxDepth;

    D3D12_RECT rect;
    if( vavp.ScissorRectEnabled )
    {
        rect.left = vavp.ScissorRect.left;
        rect.top = vavp.ScissorRect.top;
        rect.right = vavp.ScissorRect.right;
        rect.bottom = vavp.ScissorRect.bottom;
    }
    else
    {
        // set the scissor to viewport size, for rasterizer states that have it enabled
        rect.left = vavp.X;
        rect.top = vavp.Y;
        rect.right = vavp.Width + vavp.X;
        rect.bottom = vavp.Height + vavp.Y;
    }

    m_commandList->OMSetRenderTargets( numRTVs, RTVs, FALSE, pDSV );
    m_commandList->RSSetViewports( 1, &viewport );
    m_commandList->RSSetScissorRects( 1, &rect );
}

void vaRenderDeviceContextBaseDX12::CommitOutputs( const vaRenderOutputs & outputs )
{
    assert( m_commandListReady );

    // if( outputs == m_currentOutputs )
    //     return;
    m_currentOutputs = outputs;

    CommitOutputsRaw( m_itemsStarted, outputs );

    // Transitions & setup UAVs! Don't do this for the worker contexts because the main one will fill up the transient descriptors and workers will just select it
    if( m_itemsStarted != vaRenderTypeFlags::None && !IsWorker( ) )
    {
        assert( m_nextTransientDesc_OutputsUAVs != -1 );
        auto& d3d12Device = m_deviceDX12.GetPlatformDevice( );
        const vaUnorderedAccessViewDX12& nullUAV = m_deviceDX12.GetNullUAV( ); nullUAV;
        assert( m_nextTransientDesc_OutputsUAVs != -1 );
        for( uint32 i = 0; i < vaRenderOutputs::c_maxUAVs; i++ )
        {
            if( outputs.UnorderedAccessViews[i] != nullptr )
            {
                vaShaderResourceDX12& res = AsDX12( *outputs.UnorderedAccessViews[i] );
                const vaUnorderedAccessViewDX12* uav = res.GetUAV( );
                if( uav != nullptr )
                {
                    res.TransitionResource( *this, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
                    d3d12Device->CopyDescriptorsSimple( 1, m_deviceDX12.TransientDescHeapComputeCPUHandle( m_nextTransientDesc_OutputsUAVs + i ), uav->GetCPUReadableCPUHandle( ), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
                    continue;
                }
                else
                {
                    VA_WARN( "Texture set to vaRenderOutput::UAVs but UAV is nullptr?" );
                    assert( false );    // this is a bug that needs fixing
                }
            }
#ifdef VA_SET_UNUSED_DESC_TO_NULL
            d3d12Device->CopyDescriptorsSimple( 1, m_deviceDX12.TransientDescHeapComputeCPUHandle( m_nextTransientDesc_OutputsUAVs + i ), nullUAV.GetCPUReadableCPUHandle( ), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
#endif
        }
    }
}

void vaRenderDeviceContextBaseDX12::CommitGlobals( vaRenderTypeFlags typeFlags, const vaShaderItemGlobals& shaderGlobals )
{
    //////////////////////////////////////////////////////////////////////////
    // set descriptor tables and prepare for copying
    // auto [gpuHeapSRVUAV, descHeapBaseIndexSRVUAV] = AllocateSRVUAVHeapDescriptors( vaRenderDeviceDX12::ExtendedRootSignatureIndexRanges::SRVUAVTotalCount );
    // if( gpuHeapSRVUAV == nullptr )
    //     { assert( false ); return; }
    // //////////////////////////////////////////////////////////////////////////
    // //
    // int descHeapSRVOffset = descHeapBaseIndexSRVUAV + vaRenderDeviceDX12::ExtendedRootSignatureIndexRanges::SRVBase;
    // int descHeapUAVOffset = descHeapBaseIndexSRVUAV + vaRenderDeviceDX12::ExtendedRootSignatureIndexRanges::UAVBase;
    //////////////////////////////////////////////////////////////////////////

    auto & d3d12Device = m_deviceDX12.GetPlatformDevice( );

#ifdef VA_SET_UNUSED_DESC_TO_NULL
    // const vaConstantBufferViewDX12& nullCBV = m_deviceDX12.GetNullCBV( );
    const vaShaderResourceViewDX12& nullSRV = m_deviceDX12.GetNullSRV( );
    const vaUnorderedAccessViewDX12& nullUAV = m_deviceDX12.GetNullUAV( ); nullUAV;
    // const vaSamplerViewDX12& nullSamplerView = m_deviceDX12.GetNullSamplerView( ); nullSamplerView;
#endif

    // Global constant buffers
    for( int i = 0; i < array_size( shaderGlobals.ConstantBuffers ); i++ )
    {
        if( shaderGlobals.ConstantBuffers[i] != nullptr )
        {
            D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = AsDX12( *shaderGlobals.ConstantBuffers[i] ).GetGPUBufferLocation( );
            if( ( typeFlags & vaRenderTypeFlags::Graphics ) != 0 )
                m_commandList->SetGraphicsRootConstantBufferView( vaRenderDeviceDX12::DefaultRootSignatureParams::GlobalDirectCBVBase + i, gpuAddr );
            if( ( typeFlags & vaRenderTypeFlags::Compute ) != 0 )
                m_commandList->SetComputeRootConstantBufferView( vaRenderDeviceDX12::DefaultRootSignatureParams::GlobalDirectCBVBase + i, gpuAddr );
            continue;
        }
#ifdef VA_SET_UNUSED_DESC_TO_NULL
        if( ( typeFlags & vaRenderTypeFlags::Graphics ) != 0 )
            m_commandList->SetGraphicsRootConstantBufferView( vaRenderDeviceDX12::DefaultRootSignatureParams::GlobalDirectCBVBase + i, D3D12_GPU_VIRTUAL_ADDRESS{ 0 } );//nullCBV.GetDesc( ).BufferLocation;
        if( ( typeFlags & vaRenderTypeFlags::Compute ) != 0 )
            m_commandList->SetComputeRootConstantBufferView( vaRenderDeviceDX12::DefaultRootSignatureParams::GlobalDirectCBVBase + i, D3D12_GPU_VIRTUAL_ADDRESS{ 0 } );//nullCBV.GetDesc( ).BufferLocation;
#endif
    }

    if( !IsWorker() )   // already set by the main context
    {
        // Global unordered access views
        assert( m_nextTransientDesc_GlobalUAVs != -1 );
        for( int i = 0; i < array_size( vaShaderItemGlobals::UnorderedAccessViews ); i++ )
        {
            if( shaderGlobals.UnorderedAccessViews[i] != nullptr )
            {
                vaShaderResourceDX12& res = AsDX12( *shaderGlobals.UnorderedAccessViews[i] );
                const vaUnorderedAccessViewDX12* uav = res.GetUAV( );
                if( uav != nullptr )
                {
                    res.TransitionResource( *this, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
                    d3d12Device->CopyDescriptorsSimple( 1, m_deviceDX12.TransientDescHeapComputeCPUHandle( m_nextTransientDesc_GlobalUAVs + i ), uav->GetCPUReadableCPUHandle( ), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
                    continue;
                }
                else
                {
                    VA_WARN( "Shader resource set to shaderGlobals but UAV is nullptr?" );
                    assert( false );    // this is a bug that needs fixing
                }
            }
#ifdef VA_SET_UNUSED_DESC_TO_NULL
            d3d12Device->CopyDescriptorsSimple( 1, m_deviceDX12.TransientDescHeapComputeCPUHandle( m_nextTransientDesc_GlobalUAVs + i ), nullUAV.GetCPUReadableCPUHandle( ), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
#endif
        }

        // Global shader resource views
        assert( m_nextTransientDesc_GlobalSRVs != -1 );
        for( int i = 0; i < array_size( shaderGlobals.ShaderResourceViews ); i++ )
        {
            if( shaderGlobals.ShaderResourceViews[i] != nullptr )
            {
                vaShaderResourceDX12& res = AsDX12( *shaderGlobals.ShaderResourceViews[i] );
                const vaShaderResourceViewDX12* srv = res.GetSRV( );
                if( srv != nullptr )
                {
                    res.TransitionResource( *this, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
                    d3d12Device->CopyDescriptorsSimple( 1, m_deviceDX12.TransientDescHeapComputeCPUHandle( m_nextTransientDesc_GlobalSRVs + i ), srv->GetCPUReadableCPUHandle( ), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
                    continue;
                }
                else
                {
                    VA_WARN( "Shader resource set to shaderGlobals but SRV is nullptr?" );
                    assert( false );    // this is a bug that needs fixing
                }
            }
    #ifdef VA_SET_UNUSED_DESC_TO_NULL
            d3d12Device->CopyDescriptorsSimple( 1, m_deviceDX12.TransientDescHeapComputeCPUHandle( m_nextTransientDesc_GlobalSRVs + i ), nullSRV.GetCPUReadableCPUHandle( ), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    #endif
        }

        if( shaderGlobals.RaytracingAcceleationStructSRV != nullptr )
            m_commandList->SetComputeRootShaderResourceView( vaRenderDeviceDX12::DefaultRootSignatureParams::RaytracingStructDirectSRV, AsDX12(*shaderGlobals.RaytracingAcceleationStructSRV).GetGPUVirtualAddress() );//nullCBV.GetDesc( ).BufferLocation;
    }
}

void vaRenderDeviceContextDX12::BeginItems( vaRenderTypeFlags typeFlags, const vaRenderOutputs * renderOutputs, const vaShaderItemGlobals & shaderGlobals )
{
    assert( GetRenderDevice( ).IsRenderThread( ) );

    VA_TRACE_CPU_SCOPE( BeginItems );

    // beware, this can trigger flush and sync! and flush and sync clears all of these, which is why we loop
    PreAllocateTransientDescriptors( );

    vaRenderDeviceContext::BeginItems( typeFlags, renderOutputs, shaderGlobals );

    assert( m_itemsStarted != vaRenderTypeFlags::None );
    assert( m_itemsStarted == typeFlags );

    // Outputs
    if( renderOutputs != nullptr )
        CommitOutputs( *renderOutputs );

    CommitGlobals( typeFlags, shaderGlobals );

    if( !m_workersUseBundles )
    {
        for( int i = 0; i < m_workersActive; i++ )
            m_workers[i]->DeferredSetGlobals( shaderGlobals );
    }

    CommitTransientDescriptors();
}

void vaRenderDeviceContextDX12::EndItems( )
{
    assert( GetRenderDevice().IsRenderThread() );
    assert( m_itemsStarted != vaRenderTypeFlags::None );
    vaRenderDeviceContext::EndItems();

    // clear it up so we don't keep any references
    m_scratchPSODesc= vaGraphicsPSODescDX12( );
    m_currentIndexBuffer = nullptr;
    m_commandList->IASetIndexBuffer( nullptr );
    m_currentVertexBuffer = nullptr;
    m_commandList->IASetVertexBuffers( 0, 0, nullptr );
    m_currentPSO = nullptr;

    /// TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP 
    m_currentSceneRaytracing = nullptr;
    /// TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP TEMP 

    // UnsetSceneGlobals( m_currentSceneDrawContext );
    // m_currentSceneDrawContext = nullptr;

    assert( m_itemsStarted == vaRenderTypeFlags::None );
    assert( m_commandListReady );

    if( m_itemsSubmittedAfterLastExecute > c_flushAfterItemCount )
        Flush();
}

vaDrawResultFlags vaRenderDeviceContextBaseDX12::ExecuteItem( const vaGraphicsItem & renderItem, vaExecuteItemFlags flags )
{
    // perhaps too fine grained to provide useful info but costly to run
    // VA_TRACE_CPU_SCOPE( ExecuteGraphicsItem );
    m_itemsSubmittedAfterLastExecute++;

//    auto & d3d12Device      = m_deviceDX12.GetPlatformDevice( );
    const vaRenderDeviceCapabilities & caps = GetRenderDevice().GetCapabilities();
    // if( m_currentSceneDrawContext != nullptr )
    // { assert( AsDX12(&m_currentSceneDrawContext->RenderDeviceContext) == this ); }

    // re-add this if needed
    // // Graphics item (at the moment) requires at least some outputs to work
    // if( !(m_currentOutputs.RenderTargetCount > 0 || m_currentOutputs.DepthStencil != nullptr || m_currentOutputs.UAVCount > 0) )
    //     { assert( false ); return vaDrawResultFlags::UnspecifiedError; }

    // ExecuteTask can only be called in between BeginTasks and EndTasks - call ExecuteSingleItem 
    assert( (m_itemsStarted & vaRenderTypeFlags::Graphics) != 0 );
    if( (m_itemsStarted & vaRenderTypeFlags::Graphics) == 0 )
        return vaDrawResultFlags::UnspecifiedError;

    // this is an unique index of the instance ('scene object' or etc.) which can be used to figure out anything about it (mesh, material, etc.)
    m_commandList->SetGraphicsRoot32BitConstant( vaRenderDeviceDX12::DefaultRootSignatureParams::InstanceIndexDirectUINT32, renderItem.InstanceIndex, 0 );

    // a single uint root const useful for any purpose
    m_commandList->SetGraphicsRoot32BitConstant( vaRenderDeviceDX12::DefaultRootSignatureParams::GenericRootConstDirectUINT32, renderItem.GenericRootConst, 0 );
    
   
#ifdef VA_SET_UNUSED_DESC_TO_NULL
    const vaConstantBufferViewDX12 & nullCBV        = m_deviceDX12.GetNullCBV        (); nullCBV;
    const vaShaderResourceViewDX12 & nullSRV        = m_deviceDX12.GetNullSRV        (); nullSRV;
    const vaUnorderedAccessViewDX12& nullUAV        = m_deviceDX12.GetNullUAV        (); nullUAV;
//    const vaSamplerViewDX12        & nullSamplerView= m_deviceDX12.GetNullSamplerView(); nullSamplerView;
#endif

    // Constants
    for( int i = 0; i < array_size( renderItem.ConstantBuffers ); i++ )
    {
        if( renderItem.ConstantBuffers[i] != nullptr )
        {
            D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = AsDX12( *renderItem.ConstantBuffers[i] ).GetGPUBufferLocation( );
            m_commandList->SetGraphicsRootConstantBufferView( vaRenderDeviceDX12::DefaultRootSignatureParams::PerDrawDirectCBVBase + i, gpuAddr );
        }
#ifdef VA_SET_UNUSED_DESC_TO_NULL
        else
            m_commandList->SetGraphicsRootConstantBufferView( vaRenderDeviceDX12::DefaultRootSignatureParams::PerDrawDirectCBVBase + i, D3D12_GPU_VIRTUAL_ADDRESS{0} );
#endif
    }

    // Shader resource views
    for( int i = 0; i < array_size( renderItem.ShaderResourceViews ); i++ )
    {
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = { 0 };
        if( renderItem.ShaderResourceViews[i] != nullptr )
        {
            vaShaderResourceDX12& res = AsDX12( *renderItem.ShaderResourceViews[i] );
            const vaShaderResourceViewDX12* srv = res.GetSRV( );
            if( srv != nullptr )
            {
                res.TransitionResource( *this, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
                m_commandList->SetGraphicsRootDescriptorTable( vaRenderDeviceDX12::DefaultRootSignatureParams::PerDrawSRVBase + i, srv->GetGPUHandle( ) );
                continue;
            }
            else
                VA_WARN( "Texture set to renderItem but SRV is nullptr?" );
        }
#ifdef VA_SET_UNUSED_DESC_TO_NULL
        m_commandList->SetGraphicsRootDescriptorTable( vaRenderDeviceDX12::DefaultRootSignatureParams::PerDrawSRVBase + i, nullSRV.GetGPUHandle() );
#endif
    }

    // VA_TRACE_CPU_SCOPE( ExecuteGraphicsItem2 );

    vaGraphicsPSODescDX12 & psoDesc = m_scratchPSODesc;

    // draw call must always have a vertex shader (still, I guess) - check whether we've ever cached anything to begin with!
    if( psoDesc.VSUniqueContentsID == -1 )
        flags &= ~vaExecuteItemFlags::ShadersUnchanged;

    if( (flags & vaExecuteItemFlags::ShadersUnchanged) == 0 )
    {
        psoDesc.PartialReset( );

        // perhaps too fine grained to provide useful info but costly to run
        // VA_TRACE_CPU_SCOPE( Shaders )

        // must have a vertex shader at least
        if( renderItem.VertexShader == nullptr || renderItem.VertexShader->IsEmpty() )
            { psoDesc.InvalidateCache(); assert( false ); return vaDrawResultFlags::UnspecifiedError; }

        vaShader::State shState;
        if( (shState = AsDX12(*renderItem.VertexShader).GetShader( psoDesc.VSBlob, psoDesc.VSInputLayout, psoDesc.VSUniqueContentsID ) ) != vaShader::State::Cooked )
        {
            psoDesc.InvalidateCache(); 
            assert( shState != vaShader::State::Empty ); // trying to render with empty compute shader & this happened between here and the check few lines above? this is VERY weird and possibly a bug
            return (shState == vaShader::State::Uncooked)?(vaDrawResultFlags::ShadersStillCompiling):(vaDrawResultFlags::UnspecifiedError);
        }

        // vaShader::State::Empty and vaShader::State::Cooked are both ok but we must abort for uncooked!
        if( renderItem.PixelShader != nullptr && AsDX12(*renderItem.PixelShader).GetShader( psoDesc.PSBlob, psoDesc.PSUniqueContentsID ) == vaShader::State::Uncooked )
        { psoDesc.InvalidateCache(); return vaDrawResultFlags::ShadersStillCompiling; }
        if( renderItem.GeometryShader != nullptr && AsDX12(*renderItem.GeometryShader).GetShader( psoDesc.GSBlob, psoDesc.GSUniqueContentsID ) == vaShader::State::Uncooked )
        { psoDesc.InvalidateCache(); return vaDrawResultFlags::ShadersStillCompiling; }
        if( renderItem.HullShader != nullptr && AsDX12(*renderItem.HullShader).GetShader( psoDesc.HSBlob, psoDesc.HSUniqueContentsID ) == vaShader::State::Uncooked )
        { psoDesc.InvalidateCache(); return vaDrawResultFlags::ShadersStillCompiling; }
        if( renderItem.DomainShader != nullptr && AsDX12(*renderItem.DomainShader).GetShader( psoDesc.DSBlob, psoDesc.DSUniqueContentsID ) == vaShader::State::Uncooked )
        { psoDesc.InvalidateCache(); return vaDrawResultFlags::ShadersStillCompiling; }
    }

    psoDesc.BlendMode               = renderItem.BlendMode;
    psoDesc.FillMode                = renderItem.FillMode;
    psoDesc.CullMode                = renderItem.CullMode;
    psoDesc.FrontCounterClockwise   = renderItem.FrontCounterClockwise;
    psoDesc.DepthEnable             = renderItem.DepthEnable;
    psoDesc.DepthWriteEnable        = renderItem.DepthWriteEnable;
    psoDesc.DepthFunc               = renderItem.DepthFunc;
    psoDesc.Topology                = renderItem.Topology;

    //////////////////////////////////////////////////////////////////////////
    // TODO: all of this can be precomputed in CommitOutputs
    int sampleCount = 1;
    if( m_currentOutputs.RenderTargets[0] != nullptr )
        sampleCount = m_currentOutputs.RenderTargets[0]->GetSampleCount( );
    else if( m_currentOutputs.DepthStencil != nullptr )
        sampleCount = m_currentOutputs.DepthStencil->GetSampleCount( );
    // else { assert( false ); }   // no render targets? no depth either? hmm?
    psoDesc.SampleDescCount         = sampleCount;
    psoDesc.MultisampleEnable       = sampleCount > 1;
    psoDesc.NumRenderTargets        = m_currentOutputs.RenderTargetCount;
    for( int i = 0; i < _countof(psoDesc.RTVFormats); i++ )
        psoDesc.RTVFormats[i]       = ( m_currentOutputs.RenderTargets[i] != nullptr )?(m_currentOutputs.RenderTargets[i]->GetRTVFormat()):(vaResourceFormat::Unknown);
    psoDesc.DSVFormat               = ( m_currentOutputs.DepthStencil != nullptr )?(m_currentOutputs.DepthStencil->GetDSVFormat()):(vaResourceFormat::Unknown);
    //////////////////////////////////////////////////////////////////////////

    // VA_TRACE_CPU_SCOPE( ExecuteGraphicsItem4 );

    // TOPOLOGY
    D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    switch( renderItem.Topology )
    {   case vaPrimitiveTopology::PointList:        topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;        break;
        case vaPrimitiveTopology::LineList:         topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;         break;
        case vaPrimitiveTopology::TriangleList:     topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;     break;
        case vaPrimitiveTopology::TriangleStrip:    topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;    break;
        default: assert( false ); break;
    }
    if( topology != m_currentTopology )
    {
        m_commandList->IASetPrimitiveTopology( topology );
        m_currentTopology = topology;
    }

    // VA_TRACE_CPU_SCOPE( ExecuteGraphicsItem41 );
    
    {
        // perhaps too fine grained to provide useful info but costly to run
        // VA_TRACE_CPU_SCOPE( IndexBuffers )
        if( m_currentIndexBuffer != renderItem.IndexBuffer )
        {
            m_currentIndexBuffer = renderItem.IndexBuffer;
            if( renderItem.IndexBuffer != nullptr )
            {
                D3D12_INDEX_BUFFER_VIEW bufferView = { AsDX12(*renderItem.IndexBuffer).GetGPUVirtualAddress(), (UINT)AsDX12(*renderItem.IndexBuffer).GetSizeInBytes(), AsDX12(*renderItem.IndexBuffer).GetFormat() };
                AsDX12(*renderItem.IndexBuffer).TransitionResource( *this, D3D12_RESOURCE_STATE_INDEX_BUFFER );
                m_commandList->IASetIndexBuffer( &bufferView );
            }
            else
                m_commandList->IASetIndexBuffer( nullptr );
        }

        if( m_currentVertexBuffer != renderItem.VertexBuffer )
        {
            m_currentVertexBuffer = renderItem.VertexBuffer;
            if( renderItem.VertexBuffer != nullptr )
            {
                D3D12_VERTEX_BUFFER_VIEW bufferView = { AsDX12( *renderItem.VertexBuffer ).GetGPUVirtualAddress( ), (UINT)AsDX12( *renderItem.VertexBuffer ).GetSizeInBytes( ), AsDX12( *renderItem.VertexBuffer ).GetStrideInBytes() };
                AsDX12( *renderItem.VertexBuffer ).TransitionResource( *this, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER );
                m_commandList->IASetVertexBuffers( 0, 1, &bufferView );
            }
            else
                m_commandList->IASetVertexBuffers( 0, 0, nullptr );
        }
    }

    // VA_TRACE_CPU_SCOPE( ExecuteGraphicsItem42 );

    //// these are the defaults set in BindDefaultStates - change this code if they need changing per-draw-call
    //FLOAT defBlendFactor[4] = { 1, 1, 1, 1 };
    //m_commandList->OMSetBlendFactor( defBlendFactor ); // If you pass NULL, the runtime uses or stores a blend factor equal to { 1, 1, 1, 1 }.
    //m_commandList->OMSetStencilRef( 0 );

    if( m_commandList != nullptr && caps.VariableShadingRate.Tier1 )
    {
        // perhaps too fine grained to provide useful info but costly to run
        // VA_TRACE_CPU_SCOPE( ShadingRate )
        D3D12_SHADING_RATE shadingRate = D3D12_SHADING_RATE_1X1;
        switch( renderItem.ShadingRate )
        {
        case vaShadingRate::ShadingRate1X1:        shadingRate = D3D12_SHADING_RATE_1X1;        break;
        case vaShadingRate::ShadingRate1X2:        shadingRate = D3D12_SHADING_RATE_1X2;        break;
        case vaShadingRate::ShadingRate2X1:        shadingRate = D3D12_SHADING_RATE_2X1;        break;
        case vaShadingRate::ShadingRate2X2:        shadingRate = D3D12_SHADING_RATE_2X2;        break;
        case vaShadingRate::ShadingRate2X4:        shadingRate = D3D12_SHADING_RATE_2X4;        break;
        case vaShadingRate::ShadingRate4X2:        shadingRate = D3D12_SHADING_RATE_4X2;        break;
        case vaShadingRate::ShadingRate4X4:        shadingRate = D3D12_SHADING_RATE_4X4;        break;
        default: assert( false ); break;
        }
        if( !GetRenderDevice( ).GetCapabilities( ).VariableShadingRate.AdditionalShadingRatesSupported )
        {
            if( shadingRate == D3D12_SHADING_RATE_2X4 || shadingRate == D3D12_SHADING_RATE_4X2 || shadingRate == D3D12_SHADING_RATE_4X4 )
                shadingRate = D3D12_SHADING_RATE_1X1;
        }
        if( m_currentShadingRate != shadingRate )
        {
            m_commandList->RSSetShadingRate( shadingRate, nullptr );
            m_currentShadingRate = shadingRate;
        }
    }

    ID3D12PipelineState * pso = nullptr;

    {
        // VA_TRACE_CPU_SCOPE( PSOSearch )
        vaGraphicsPSODX12 * psoOuter = m_deviceDX12.FindOrCreateGraphicsPipelineState( psoDesc, 
#ifdef VA_DX12_USE_LOCAL_PSO_CACHE
            &m_localGraphicsPSOCache
#else
            nullptr 
#endif
        );
        pso = (psoOuter!=nullptr)?(psoOuter->GetPSO()):(nullptr);
    }
    if( pso == nullptr )
        return vaDrawResultFlags::ShadersStillCompiling;

    if( m_currentPSO != pso )
    {
        m_commandList->SetPipelineState( pso );
        m_currentPSO = pso;
    }

    // VA_TRACE_CPU_SCOPE( ExecuteGraphicsItem6 );
   
    bool continueWithDraw = true;
    //if( renderItem.PreDrawHook != nullptr )
    //     continueWithDraw = renderItem.PreDrawHook( renderItem, *this );

    if( continueWithDraw )
    {
        // perhaps too fine grained to provide useful info but costly to run
        // VA_TRACE_CPU_SCOPE( Draw )

        switch( renderItem.DrawType )
        {
        case( vaGraphicsItem::DrawType::DrawSimple ): 
            m_commandList->DrawInstanced( renderItem.DrawSimpleParams.VertexCount, 1, renderItem.DrawSimpleParams.StartVertexLocation, 0 );
            break;
        case( vaGraphicsItem::DrawType::DrawIndexed ): 
            m_commandList->DrawIndexedInstanced( renderItem.DrawIndexedParams.IndexCount, 1, renderItem.DrawIndexedParams.StartIndexLocation, renderItem.DrawIndexedParams.BaseVertexLocation, 0 );
            break;
        default:
            assert( false );
            break;
        }
    }

    // if( renderItem.PostDrawHook != nullptr )
    //     renderItem.PostDrawHook( renderItem, *this );

    //// for caching - not really needed for now
    //// m_lastRenderItem = renderItem;
    return vaDrawResultFlags::None;
}

vaDrawResultFlags vaRenderDeviceContextBaseDX12::ExecuteItem( const vaComputeItem & computeItem, vaExecuteItemFlags flags )
{
    // No threads will be dispatched, because at least one of {ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ} is 0. This is probably not intentional?
    assert( computeItem.DispatchParams.ThreadGroupCountX != 0 && computeItem.DispatchParams.ThreadGroupCountY != 0 && computeItem.DispatchParams.ThreadGroupCountZ != 0 );

    flags;
//    VA_TRACE_CPU_SCOPE( ExecuteComputeItem );
    m_itemsSubmittedAfterLastExecute++;

    assert( GetRenderDevice().IsRenderThread() );

    // ExecuteTask can only be called in between BeginTasks and EndTasks - call ExecuteSingleItem 
    assert( (m_itemsStarted & vaRenderTypeFlags::Compute) != 0 );
    if( (m_itemsStarted & vaRenderTypeFlags::Compute) == 0 )
        return vaDrawResultFlags::UnspecifiedError;

    // must have compute shader at least
    if( computeItem.ComputeShader == nullptr || computeItem.ComputeShader->IsEmpty() )
        { assert( false ); return vaDrawResultFlags::UnspecifiedError; }

    // there is no instance index during compute shading!
    m_commandList->SetComputeRoot32BitConstant( vaRenderDeviceDX12::DefaultRootSignatureParams::InstanceIndexDirectUINT32, 0xFFFFFFFF, 0 );

    // a single uint root const useful for any purpose
    m_commandList->SetComputeRoot32BitConstant( vaRenderDeviceDX12::DefaultRootSignatureParams::GenericRootConstDirectUINT32, computeItem.GenericRootConst, 0 );

    vaComputePSODescDX12 psoDesc;

    vaShader::State shState;
    if( ( shState = AsDX12(*computeItem.ComputeShader).GetShader( psoDesc.CSBlob, psoDesc.CSUniqueContentsID ) ) != vaShader::State::Cooked )
    {
        assert( shState != vaShader::State::Empty ); // trying to render with empty compute shader & this happened between here and the check few lines above? this is VERY weird and possibly a bug
        return (shState == vaShader::State::Uncooked)?(vaDrawResultFlags::ShadersStillCompiling):(vaDrawResultFlags::UnspecifiedError);
    }

#ifdef VA_SET_UNUSED_DESC_TO_NULL
    const vaShaderResourceViewDX12 & nullSRV        = m_deviceDX12.GetNullSRV        ();
#endif

    // Constants
    for( int i = 0; i < array_size( computeItem.ConstantBuffers ); i++ )
    {
        if( computeItem.ConstantBuffers[i] != nullptr )
        {
            D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = AsDX12( *computeItem.ConstantBuffers[i] ).GetGPUBufferLocation( );
            m_commandList->SetComputeRootConstantBufferView( vaRenderDeviceDX12::DefaultRootSignatureParams::PerDrawDirectCBVBase + i, gpuAddr );
        }
#ifdef VA_SET_UNUSED_DESC_TO_NULL
        else
            m_commandList->SetComputeRootConstantBufferView( vaRenderDeviceDX12::DefaultRootSignatureParams::PerDrawDirectCBVBase + i, D3D12_GPU_VIRTUAL_ADDRESS{0} );
#endif
    }

    // Shader resource views
    for( int i = 0; i < array_size( computeItem.ShaderResourceViews ); i++ )
    {
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = { 0 };
        if( computeItem.ShaderResourceViews[i] != nullptr )
        {
            vaShaderResourceDX12& res = AsDX12( *computeItem.ShaderResourceViews[i] );
            const vaShaderResourceViewDX12* srv = res.GetSRV( );
            if( srv != nullptr )
            {
                res.TransitionResource( *this, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
                m_commandList->SetComputeRootDescriptorTable( vaRenderDeviceDX12::DefaultRootSignatureParams::PerDrawSRVBase + i, srv->GetGPUHandle( ) );
                continue;
            }
            else
                VA_WARN( "Texture set to renderItem but SRV is nullptr?" );
        }
#ifdef VA_SET_UNUSED_DESC_TO_NULL
        m_commandList->SetComputeRootDescriptorTable( vaRenderDeviceDX12::DefaultRootSignatureParams::PerDrawSRVBase + i, nullSRV.GetGPUHandle() );
#endif
    }

    vaComputePSODX12 * psoOuter = m_deviceDX12.FindOrCreateComputePipelineState( psoDesc );
    ID3D12PipelineState * pso = (psoOuter!=nullptr)?(psoOuter->GetPSO()):(nullptr);
    if( pso == nullptr )
    {
        // this isn't valid for compute shader calls at the moment - figure out why it happened
        assert( false );
        return vaDrawResultFlags::ShadersStillCompiling;
    }

    m_commandList->SetPipelineState( pso );

    {
        auto NULLBARRIER = CD3DX12_RESOURCE_BARRIER::UAV( nullptr );
        if( computeItem.GlobalUAVBarrierBefore )
            m_commandList->ResourceBarrier(1, &NULLBARRIER );

        switch( computeItem.ComputeType )
        {
        case( vaComputeItem::Dispatch ): 
            m_commandList->Dispatch( computeItem.DispatchParams.ThreadGroupCountX, computeItem.DispatchParams.ThreadGroupCountY, computeItem.DispatchParams.ThreadGroupCountZ );
            break;
        case( vaComputeItem::DispatchIndirect ): 
            // assert( computeItem.DispatchIndirectParams.BufferForArgs != nullptr );
            assert( false ); // not yet implemented
            // see: https://docs.microsoft.com/en-us/windows/desktop/direct3d12/indirect-drawing and https://docs.microsoft.com/en-us/windows/desktop/api/d3d12/nf-d3d12-id3d12graphicscommandlist-executeindirect
            // m_deviceContext->DispatchIndirect( computeItem.DispatchIndirectParams.BufferForArgs->SafeCast<vaShaderResourceDX11*>()->GetBuffer(), computeItem.DispatchIndirectParams.AlignedOffsetForArgs );
            break;
        default:
            assert( false );
            break;
        }

        if( computeItem.GlobalUAVBarrierAfter )
            m_commandList->ResourceBarrier( 1, &NULLBARRIER );
    }
   
    return vaDrawResultFlags::None;
}

void vaRenderDeviceContextDX12::BeginRaytraceItems( const vaRenderOutputs & renderOutputs, const vaDrawAttributes * drawAttributes ) 
{ 
    assert( m_workersActive == 0 ); 
    assert( drawAttributes != nullptr );
    assert( drawAttributes->Raytracing != nullptr );
    m_currentSceneRaytracing = dynamic_cast<vaSceneRaytracingDX12*>( drawAttributes->Raytracing );
    assert( m_currentSceneRaytracing != nullptr );
    return vaRenderDeviceContext::BeginRaytraceItems( renderOutputs, drawAttributes );
}

vaDrawResultFlags vaRenderDeviceContextBaseDX12::ExecuteItem( const vaRaytraceItem & raytraceItem, vaExecuteItemFlags flags )
{
    // No threads will be dispatched, because at least one of {ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ} is 0. This is probably not intentional?
    assert( raytraceItem.DispatchWidth != 0 && raytraceItem.DispatchHeight != 0 && raytraceItem.DispatchDepth != 0 );

    flags;
    //    VA_TRACE_CPU_SCOPE( ExecuteRaytraceItem );
    m_itemsSubmittedAfterLastExecute++;

    assert( GetRenderDevice( ).IsRenderThread( ) );

    // ExecuteTask can only be called in between BeginTasks and EndTasks - call ExecuteSingleItem 
    if( ( ( m_itemsStarted & vaRenderTypeFlags::Compute ) == 0 ) && ( ( m_itemsStarted & vaRenderTypeFlags::Raytrace ) == 0 ) )
    {
        assert( false );
        return vaDrawResultFlags::UnspecifiedError;
    }

    // Since we can't know in advance whether the compiling shaders are part of the requested PSO and recompiling raytracing PSOs is horribly
    // costly, let's just wait until all shaders are 'settled'.
    if( vaShader::GetNumberOfCompilingShaders( ) > 0 )
        return vaDrawResultFlags::ShadersStillCompiling;

    // there is no instance index during raytracing!
    m_commandList->SetComputeRoot32BitConstant( vaRenderDeviceDX12::DefaultRootSignatureParams::InstanceIndexDirectUINT32, 0xFFFFFFFF, 0 );

    // a single uint root const useful for any purpose
    m_commandList->SetComputeRoot32BitConstant( vaRenderDeviceDX12::DefaultRootSignatureParams::GenericRootConstDirectUINT32, raytraceItem.GenericRootConst, 0 );

    vaRaytracePSODescDX12 psoDesc;

    psoDesc.ItemSLEntryRayGen       = vaStringTools::SimpleWiden(raytraceItem.RayGen         );              assert( raytraceItem.RayGen          .size() < vaRaytracePSODescDX12::c_maxNameBufferSize );    assert( raytraceItem.RayGen != "" );
    psoDesc.ItemSLEntryMiss         = vaStringTools::SimpleWiden(raytraceItem.Miss           );              assert( raytraceItem.Miss            .size() < vaRaytracePSODescDX12::c_maxNameBufferSize );
    psoDesc.ItemSLEntryMissSecondary= vaStringTools::SimpleWiden(raytraceItem.MissSecondary  );              assert( raytraceItem.MissSecondary   .size() < vaRaytracePSODescDX12::c_maxNameBufferSize );
    psoDesc.ItemSLEntryAnyHit       = vaStringTools::SimpleWiden(raytraceItem.AnyHit    );                   assert( raytraceItem.AnyHit          .size() < vaRaytracePSODescDX12::c_maxNameBufferSize );
    psoDesc.ItemSLEntryClosestHit   = vaStringTools::SimpleWiden(raytraceItem.ClosestHit);                   assert( raytraceItem.ClosestHit      .size() < vaRaytracePSODescDX12::c_maxNameBufferSize );

    // can have either shader item librasry entry or shader material library entry for these
    if( raytraceItem.AnyHit == "" )
        { psoDesc.ItemMaterialAnyHit = vaStringTools::SimpleWiden(raytraceItem.MaterialAnyHit  );              assert( psoDesc.ItemMaterialAnyHit              .size() < vaRaytracePSODescDX12::c_maxNameBufferSize ); }
    else
        { assert( raytraceItem.MaterialAnyHit == "" ); } // most likely a bug in user code - you can either have a singular AnyHit or per-material ItemMaterialAnyHit but not both
    if( raytraceItem.ClosestHit == "" )
        { psoDesc.ItemMaterialClosestHit = vaStringTools::SimpleWiden(raytraceItem.MaterialClosestHit );       assert( psoDesc.ItemMaterialClosestHit          .size() < vaRaytracePSODescDX12::c_maxNameBufferSize ); }
    else
        { assert( raytraceItem.MaterialClosestHit == "" ); } // most likely a bug in user code - you can either have a singular ClosestHit or per-material ItemMaterialClosestHit but not both

    psoDesc.ItemMaterialCallable     = vaStringTools::SimpleWiden(raytraceItem.ShaderEntryMaterialCallable );           assert( psoDesc.ItemMaterialCallable            .size() < vaRaytracePSODescDX12::c_maxNameBufferSize );
    psoDesc.ItemMaterialMissCallable = vaStringTools::SimpleWiden(raytraceItem.MaterialMissCallable );       assert( psoDesc.ItemMaterialMissCallable        .size() < vaRaytracePSODescDX12::c_maxNameBufferSize );

    assert( psoDesc.ItemSLEntryRayGen != L"" );                                                 // we always need a raygen shader!
    assert( psoDesc.ItemSLEntryMiss != L"" );                                                   // we always need a miss shader!
    assert( psoDesc.ItemSLEntryAnyHit != L"" || psoDesc.ItemMaterialAnyHit != L"" );            // we always need a anyhit shader!
    assert( psoDesc.ItemSLEntryClosestHit != L"" || psoDesc.ItemMaterialClosestHit != L"" );    // we always need a closesthit shader!

    vaShader::State shState;
    if( ( shState = AsDX12( *raytraceItem.ShaderLibrary ).GetShader( psoDesc.ItemSLBlob, psoDesc.ItemSLUniqueContentsID ) ) != vaShader::State::Cooked )
    {
        assert( shState != vaShader::State::Empty ); // trying to render with empty compute shader & this happened between here and the check few lines above? this is VERY weird and possibly a bug
        return ( shState == vaShader::State::Uncooked ) ? ( vaDrawResultFlags::ShadersStillCompiling ) : ( vaDrawResultFlags::UnspecifiedError );
    }

    psoDesc.MaterialsSLUniqueContentsID = AsDX12(m_deviceDX12.GetMaterialManager()).GetCallablesTableID();
    psoDesc.MaxRecursionDepth = raytraceItem.MaxRecursionDepth;     assert( psoDesc.MaxRecursionDepth > 0 );
    psoDesc.MaxPayloadSize = raytraceItem.MaxPayloadSize;           assert( raytraceItem.MaxPayloadSize > 0 );

#ifdef VA_SET_UNUSED_DESC_TO_NULL
    const vaShaderResourceViewDX12 & nullSRV = m_deviceDX12.GetNullSRV( );
#endif

    // Constants
    for( int i = 0; i < array_size( raytraceItem.ConstantBuffers ); i++ )
    {
        if( raytraceItem.ConstantBuffers[i] != nullptr )
        {
            D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = AsDX12( *raytraceItem.ConstantBuffers[i] ).GetGPUBufferLocation( );
            m_commandList->SetComputeRootConstantBufferView( vaRenderDeviceDX12::DefaultRootSignatureParams::PerDrawDirectCBVBase + i, gpuAddr );
        }
#ifdef VA_SET_UNUSED_DESC_TO_NULL
        else
            m_commandList->SetComputeRootConstantBufferView( vaRenderDeviceDX12::DefaultRootSignatureParams::PerDrawDirectCBVBase + i, D3D12_GPU_VIRTUAL_ADDRESS{ 0 } );
#endif
    }

    // Shader resource views
    for( int i = 0; i < array_size( raytraceItem.ShaderResourceViews ); i++ )
    {
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = { 0 };
        if( raytraceItem.ShaderResourceViews[i] != nullptr )
        {
            vaShaderResourceDX12& res = AsDX12( *raytraceItem.ShaderResourceViews[i] );
            const vaShaderResourceViewDX12* srv = res.GetSRV( );
            if( srv != nullptr )
            {
                res.TransitionResource( *this, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
                m_commandList->SetComputeRootDescriptorTable( vaRenderDeviceDX12::DefaultRootSignatureParams::PerDrawSRVBase + i, srv->GetGPUHandle( ) );
                continue;
            }
            else
                VA_WARN( "Texture set to renderItem but SRV is nullptr?" );
        }
#ifdef VA_SET_UNUSED_DESC_TO_NULL
        m_commandList->SetComputeRootDescriptorTable( vaRenderDeviceDX12::DefaultRootSignatureParams::PerDrawSRVBase + i, nullSRV.GetGPUHandle( ) );
#endif
    }

    vaRaytracePSODX12 * psoOuter = m_deviceDX12.FindOrCreateRaytracePipelineState( psoDesc );
    vaRaytracePSODX12::Inner * pso = (psoOuter!=nullptr)?(psoOuter->GetPSO()):(nullptr);
    if( pso == nullptr )
    {
        // this is OK for raytracing PSOs
        return vaDrawResultFlags::ShadersStillCompiling;
    }

    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};

    // Since each shader table has only one shader record, the stride is same as the size.
    dispatchDesc.HitGroupTable.StartAddress             = AsDX12(*pso->HitGroupShaderTable).GetGPUVirtualAddress( );
    dispatchDesc.HitGroupTable.SizeInBytes              = AsDX12(*pso->HitGroupShaderTable).GetDesc( ).Width;
    dispatchDesc.HitGroupTable.StrideInBytes            = pso->HitGroupShaderTableStride;
    dispatchDesc.MissShaderTable.StartAddress           = AsDX12(*pso->MissShaderTable).GetGPUVirtualAddress( );
    dispatchDesc.MissShaderTable.SizeInBytes            = AsDX12(*pso->MissShaderTable).GetDesc( ).Width;
    dispatchDesc.MissShaderTable.StrideInBytes          = pso->MissShaderTableStride;
    dispatchDesc.RayGenerationShaderRecord.StartAddress = AsDX12(*pso->RayGenShaderTable).GetGPUVirtualAddress( );
    dispatchDesc.RayGenerationShaderRecord.SizeInBytes  = AsDX12(*pso->RayGenShaderTable).GetDesc( ).Width;
    dispatchDesc.CallableShaderTable.StartAddress       = ( pso->CallableShaderTable != nullptr ) ? ( AsDX12(*pso->CallableShaderTable).GetGPUVirtualAddress( ) ) : ( 0 );
    dispatchDesc.CallableShaderTable.SizeInBytes        = ( pso->CallableShaderTable != nullptr ) ? ( AsDX12(*pso->CallableShaderTable).GetDesc( ).Width        ) : ( 0 );
    dispatchDesc.CallableShaderTable.StrideInBytes      = ( pso->CallableShaderTable != nullptr ) ? ( pso->CallableShaderTableStride                            ) : ( 0 );
    dispatchDesc.Width                                  = raytraceItem.DispatchWidth;
    dispatchDesc.Height                                 = raytraceItem.DispatchHeight;
    dispatchDesc.Depth                                  = raytraceItem.DispatchDepth;
    m_commandList->SetPipelineState1( pso->PSO.Get( ) );

    auto NULLBARRIER = CD3DX12_RESOURCE_BARRIER::UAV( nullptr );
    if( raytraceItem.GlobalUAVBarrierBefore )
        m_commandList->ResourceBarrier( 1, &NULLBARRIER );

    m_commandList->DispatchRays( &dispatchDesc );

    if( raytraceItem.GlobalUAVBarrierAfter )
        m_commandList->ResourceBarrier( 1, &NULLBARRIER );

    return (pso->Incomplete)?(vaDrawResultFlags::ShadersStillCompiling):(vaDrawResultFlags::None);
}

void vaRenderDeviceContextBaseDX12::BeginFrame( )
{
    assert( GetRenderDevice( ).IsRenderThread( ) );
    assert( m_itemsStarted == vaRenderTypeFlags::None );
    assert( !m_commandListReady );
    
    // these are no longer valid
    m_nextTransientDesc_GlobalSRVs      = -1;
    m_nextTransientDesc_GlobalUAVs      = -1;
    m_nextTransientDesc_OutputsUAVs     = -1;

    uint32 currentFrame = m_deviceDX12.GetCurrentFrameFlipIndex( );

    HRESULT hr;

    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.
    V( m_commandAllocators[currentFrame]->Reset( ) );

#ifdef VA_D3D12_USE_DEBUG_LAYER_DRED
    if( hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_HUNG )
        m_deviceDX12.DeviceRemovedHandler( );
#endif

    if( !IsWorker() )
    {
        ResetAndInitializeCommandList( m_deviceDX12.GetCurrentFrameFlipIndex( ) );
    }

    vaRenderDeviceContext::BeginFrame( );
}

void vaRenderDeviceContextBaseDX12::EndFrame( )
{
    vaRenderDeviceContext::EndFrame( );

    assert( GetRenderDevice( ).IsRenderThread( ) );
    assert( m_commandListReady || IsWorker() );
    assert( m_itemsStarted == vaRenderTypeFlags::None );

    m_localGraphicsPSOCache.Reset();
}

void vaRenderDeviceContextWorkerDX12::BeginFrame( )
{
    vaRenderDeviceContextBaseDX12::BeginFrame( );
}

void vaRenderDeviceContextWorkerDX12::EndFrame( )
{
    vaRenderDeviceContextBaseDX12::EndFrame( );
    assert( m_localGPUFrameFinishedCallbacks.size() == 0 );
}

void vaRenderDeviceContextDX12::BeginFrame( )
{
    vaRenderDeviceContextBaseDX12::BeginFrame( );

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.

    for( int i = 0; i < m_workers.size(); i++ )
        m_workers[i]->BeginFrame();
}

void vaRenderDeviceContextDX12::EndFrame( )
{
    {
        VA_TRACE_CPU_SCOPE( WorkerContextsEndFrame );
        for( int i = 0; i < m_workers.size( ); i++ )
            m_workers[i]->EndFrame( );
    }

    vaRenderDeviceContextBaseDX12::EndFrame( );

    {
        // we can only start this scope here because vaRenderDeviceContextBaseDX12::EndFrame( ) above triggers a custom scope and we want to avoid ours overlapping with it
        // VA_TRACE_CPU_SCOPE( MainContextEndFrame );

        assert( GetRenderDevice( ).IsRenderThread( ) );
        assert( m_commandListReady );
        assert( m_itemsStarted == vaRenderTypeFlags::None );

        uint32 currentFrame = m_deviceDX12.GetCurrentFrameFlipIndex( ); currentFrame;

        HRESULT hr;
        {
            VA_TRACE_CPU_SCOPE( CommandListClose );
            V( m_commandList->Close( ) );
        }

        {
            // VA_TRACE_CPU_SCOPE( CommandListExecute );
            // Execute the command list.
            ID3D12CommandList* ppCommandLists[] = { m_commandList.Get( ) };
            m_deviceDX12.GetCommandQueue( )->ExecuteCommandLists( _countof( ppCommandLists ), ppCommandLists );
            m_itemsSubmittedAfterLastExecute = 0;
        }

    #ifdef VA_D3D12_USE_DEBUG_LAYER_DRED
        hr = m_deviceDX12.GetPlatformDevice( )->GetDeviceRemovedReason( );
        if( hr == DXGI_ERROR_DEVICE_REMOVED )
            m_deviceDX12.DeviceRemovedHandler( );
    #endif

        m_commandListReady = false;
    }
}

void vaRenderDeviceContextDX12::PostPresent( )
{
    assert( m_itemsSubmittedAfterLastExecute == 0 );

    // Quick re-open of the command list to allow for perf tracing data collection
    HRESULT hr;
    uint32 currentFrame = AsDX12( GetRenderDevice( ) ).GetCurrentFrameFlipIndex( );
    V( m_commandList->Reset( m_commandAllocators[currentFrame].Get( ), nullptr ) );
    m_commandListReady = true;
    
    vaRenderDeviceContext::PostPresent( );
    
    // Close and execute command list - this one only contains perf tracing stuff
    V( m_commandList->Close( ) );
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get( ) };
    m_deviceDX12.GetCommandQueue( )->ExecuteCommandLists( _countof( ppCommandLists ), ppCommandLists );
    m_commandListReady = false;
}

void vaRenderDeviceContextDX12::QueueResourceStateTransition( const vaFramePtr<vaShaderResourceDX12> & resource, int workerIndex, D3D12_RESOURCE_STATES target, uint32 subResIndex )
{
    assert( subResIndex == -1 );    // subresources not supported for this
    assert( m_workersActive > 0 );
    std::unique_lock lock(m_resourceTransitionQueueMutex);

    auto it = m_resourceTransitionQueue.find( resource );
    if( it == m_resourceTransitionQueue.end( ) )
        m_resourceTransitionQueue.emplace( std::pair<vaFramePtr<vaShaderResourceDX12>, ResourceStateTransitionItem>( resource, {workerIndex, target, subResIndex} ) );
    else
    {
        const ResourceStateTransitionItem & data = it->second;
        if( data.Target != target || data.SubResIndex != subResIndex )
        {
            // we've got a serious problemo - trying to change resource type to a different type from two different places - this isn't going to work, find the bug and fix it
            assert( false );
        }
    }
}

// // this version returns the pre-allocated descriptors since this is expensive to do on every call!
// std::pair< vaRenderDeviceDX12::TransientGPUDescriptorHeap*, int > vaRenderDeviceContextWorkerDX12::AllocateSRVUAVHeapDescriptors( int numberOfDescriptors )
// {
//     assert( m_preAllocatedSRVUAVHeapCount >= numberOfDescriptors );
//     assert( m_preAllocatedSRVUAVHeap != nullptr );
// 
//     std::pair< vaRenderDeviceDX12::TransientGPUDescriptorHeap*, int > retVal = { m_preAllocatedSRVUAVHeap, m_preAllocatedSRVUAVHeapBase };
// 
//     m_preAllocatedSRVUAVHeapBase += numberOfDescriptors;
//     m_preAllocatedSRVUAVHeapCount -= numberOfDescriptors;
//     
//     return retVal;
// }

void vaRenderDeviceContextWorkerDX12::CommitOutputs( const vaRenderOutputs & outputs )
{
    if( m_useBundles )
        m_currentOutputs = outputs;                                 // bundles inherit output, so this is set only for internal tracking / validation
    else
        vaRenderDeviceContextBaseDX12::CommitOutputs( outputs );
}

void vaRenderDeviceContextWorkerDX12::PreWorkPrepareMainThread( int workItemCount )
{
    workItemCount;
}

void vaRenderDeviceContextWorkerDX12::PreWorkPrepareWorkerThread( int workItemCount )
{
    workItemCount;
    ResetAndInitializeCommandList( m_deviceDX12.GetCurrentFrameFlipIndex( ) );
    m_itemsStarted = GetMasterDX12( )->m_itemsStarted;      // mostly for asserting/tracking
    CommitOutputs( GetMasterDX12( )->m_currentOutputs );

    if( m_hasGlobals )
    {
        CommitGlobals( m_itemsStarted, m_deferredGlobals );
#ifdef _DEBUG        
        m_deferredGlobals = {};
#endif
        m_hasGlobals = false;
    }

    CommitTransientDescriptors( );
}

void vaRenderDeviceContextWorkerDX12::PostWorkCleanupWorkerThread( )
{
    if( m_localGPUFrameFinishedCallbacks.size( ) > 0 )
    {
        AsDX12(GetRenderDevice()).ExecuteAfterCurrentGPUFrameDone( m_localGPUFrameFinishedCallbacks );
        m_localGPUFrameFinishedCallbacks.clear();
    }
    assert( !m_commandListReady );
    m_currentIndexBuffer = nullptr;
    m_currentVertexBuffer = nullptr;
    m_currentPSO = nullptr;

    // clear these up so we don't keep any references
    m_scratchPSODesc = vaGraphicsPSODescDX12( );
}

void vaRenderDeviceContextWorkerDX12::PostWorkCleanupMainThread( )
{
    assert( GetRenderDevice( ).IsRenderThread( ) );
    assert( !m_commandListReady );
}

vaDrawResultFlags vaRenderDeviceContextDX12::ExecuteGraphicsItemsConcurrent( int itemCount, const vaRenderOutputs& renderOutputs, const vaDrawAttributes* drawAttributes, const GraphicsItemCallback& callback )
{
    VA_TRACE_CPU_SCOPE( ExecuteGraphicsItemsConcurrent );

    assert( itemCount >= 0 );
    if( itemCount <= 0 )
        return vaDrawResultFlags::None;

    const uint32 currentFrame = m_deviceDX12.GetCurrentFrameFlipIndex( ); currentFrame;

    //////////////////////////////////////////////////////////////////////////
    // compute number of workers / tasks
    const int minTasksPerWorker = 64;
    assert( m_workersActive == 0 );
    m_workersActive = std::min( (int)m_workers.size(), (itemCount+minTasksPerWorker-1) / minTasksPerWorker );
    
    //////////////////////////////////////////////////////////////////////////
    // in case only 1 worker needed, no need to go through all the complexity below, just fall back to the single-threaded approach
    if( m_workersActive <= 1 )
    {
        m_workersActive = 0;
        return vaRenderDeviceContext::ExecuteGraphicsItemsConcurrent( itemCount, renderOutputs, drawAttributes, callback );
    }

    //////////////////////////////////////////////////////////////////////////
    // initialize worker contexts
    vaDrawResultFlags ret = vaDrawResultFlags::None;

    const int batchCount = ( itemCount + c_maxItemsPerBeginEnd - 1 ) / c_maxItemsPerBeginEnd;
    const int itemsPerBatch = (itemCount + batchCount - 1) / batchCount; itemsPerBatch;
    for( int batch = 0; batch < batchCount; batch++ )
    {
        // // option a: use max and what's left for the last
        // const int batchItemFrom = batch * c_maxItemsPerBeginEnd; const int batchItemCount = std::min( itemCount - batchItemFrom, c_maxItemsPerBeginEnd );
        // option b: divide equally for each batch
        const int batchItemFrom = batch * itemsPerBatch; const int batchItemCount = std::min( itemCount - batchItemFrom, itemsPerBatch );

        //
        BeginGraphicsItems( renderOutputs, drawAttributes );
        //
        const int tasksPerWorker = ( batchItemCount + m_workersActive - 1 ) / m_workersActive;
        for( int w = 0; w < m_workersActive; w++ )
        {
            int itemFirst   = batchItemFrom + w * tasksPerWorker;
            int itemLast    = batchItemFrom + std::min( ( w + 1 ) * tasksPerWorker - 1, batchItemCount - 1 );
            m_workers[w]->PreWorkPrepareMainThread( itemLast - itemFirst + 1 );
        }
        //
        //////////////////////////////////////////////////////////////////////////
        // set up the worker callback function
        auto workerFunction = [ tasksPerWorker, batchItemFrom, batchItemCount, &workers = m_workers, &workerDrawResults = m_workerDrawResults, &callback ]( int w ) noexcept
        {
            const int itemFirst   = batchItemFrom + w * tasksPerWorker;
            const int itemLast    = batchItemFrom + std::min( ( w + 1 ) * tasksPerWorker - 1, batchItemCount - 1 );
            workerDrawResults[w] = vaDrawResultFlags::None;

            {
                VA_TRACE_CPU_SCOPE( PrepareWorker );
                workers[w]->PreWorkPrepareWorkerThread( itemLast - itemFirst + 1 );
            }

            {
                VA_TRACE_CPU_SCOPE( ExecWorkerItems );
                for( int i = itemFirst; i <= itemLast; i++ )
                {
                    // VA_TRACE_CPU_SCOPE( ExecWorkerItem );
                    workerDrawResults[w] |= callback( i, *workers[w] );
                }
            }

            {
                // perhaps too fine grained to provide useful info but costly to run
                VA_TRACE_CPU_SCOPE( CommandListClose );
                workers[w]->GetCommandList( ).Get( )->Close( );
            }
            workers[w]->m_itemsStarted = vaRenderTypeFlags::None;
            workers[w]->m_commandListReady = false;
            workers[w]->PostWorkCleanupWorkerThread( );
        };
        //////////////////////////////////////////////////////////////////////////


        //////////////////////////////////////////////////////////////////////////
        // !!! MULTITHREADED PART STARTS !!!
        // going wide!
        {
            VA_TRACE_CPU_SCOPE( GoWide );

     #if !defined(VA_TASKFLOW_INTEGRATION_ENABLED)
          // just single-threaded loop!
          for( int w = 0; w < m_workersActive; w++ )
              workerFunction( w );
    #else
    #if 0   // tf style
            tf::Taskflow workFlow( "bosmang" );
            workFlow.parallel_for( 0, int(m_workersActive-1), 1, workerFunction, 1Ui64 ).second;
            auto workFlowFuture = vaTF::GetInstance( ).Executor( ).run( workFlow );
    #else   // our tf style
            auto workFlowFuture = vaTF::parallel_for( 0, int(m_workersActive-1), workerFunction, 1, "RenderListBuild" );
    #endif
            // busy ourselves with 1 job
            workerFunction( m_workersActive - 1 );
            // wait for everything else to finish
            workFlowFuture.wait( );
    #endif
        }
        // !!! MULTITHREADED PART ENDS !!!
        //////////////////////////////////////////////////////////////////////////

        //////////////////////////////////////////////////////////////////////////
        // apply deferred resource transitions
        {
            VA_TRACE_CPU_SCOPE( DeferredResourceTransitions );
            // these can get collected
            std::unique_lock lock( m_resourceTransitionQueueMutex ); // <- this isn't needed but leaving it here to avoid any confusion
            for( auto it = m_resourceTransitionQueue.begin( ); it != m_resourceTransitionQueue.end( ); it++ )
                it->first->TransitionResource( *this, it->second.Target );
            m_resourceTransitionQueue.clear();
        }

        //////////////////////////////////////////////////////////////////////////
        // commit all!
        //
        {
            VA_TRACE_CPU_SCOPE( CommitAll );
            HRESULT hr; hr;

            // submitting work is different based on whether we use bundles or direct command list workers
            if( m_workersUseBundles )
            {
                {
                    VA_TRACE_CPU_SCOPE( ExecuteAllBundles );
                    for( int w = 0; w < m_workersActive; w++ )
                    {
                        //VA_TRACE_CPU_SCOPE( ExecuteBundle );
                        m_commandList->ExecuteBundle( m_workers[w]->GetCommandList( ).Get( ) );
                        ret |= m_workerDrawResults[w]; m_workerDrawResults[w] = vaDrawResultFlags::None;
                    }
                }
                {
                    VA_TRACE_CPU_SCOPE( PostWorkCleanup );
                    for( int w = 0; w < m_workersActive; w++ )
                    {
                        //VA_TRACE_CPU_SCOPE( PostWorkCleanup );
                        m_workers[w]->PostWorkCleanupMainThread( );
                    }
                }
            }
            else
            {
                // our main command list is filled up, close it
                V( m_commandList->Close( ) );
                m_commandListReady = false;

                // first execute the main command list (among other things it contains all transitions required so it has to be first) and then all workers
                ID3D12CommandList * commandLists[vaRenderDeviceDX12::c_maxWorkers+1];
                int commandListsCount = 1 + (int64)m_workersActive;
                assert( commandListsCount <= countof(commandLists) );
                commandLists[0] = m_commandList.Get( );
                for( int w = 0; w < m_workersActive; w++ )
                {
                    commandLists[(int64)w+1] = m_workers[w]->GetCommandList().Get();
                    ret |= m_workerDrawResults[w]; m_workerDrawResults[w] = vaDrawResultFlags::None;
                }

                m_deviceDX12.GetCommandQueue( )->ExecuteCommandLists( commandListsCount, commandLists );
                auto bkp = m_itemsStarted; m_itemsStarted = vaRenderTypeFlags::None; // to avoid asserts
                ResetAndInitializeCommandList( currentFrame );
                m_itemsStarted = bkp; // to avoid asserts
            }

        #ifdef VA_D3D12_USE_DEBUG_LAYER_DRED
            hr = m_deviceDX12.GetPlatformDevice( )->GetDeviceRemovedReason( );
            if( hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_HUNG )
                m_deviceDX12.DeviceRemovedHandler( );
        #endif
        }

        for( int w = 0; w < m_workersActive; w++ )
        {
            m_itemsSubmittedAfterLastExecute += m_workers[w]->m_itemsSubmittedAfterLastExecute;
            m_workers[w]->m_itemsSubmittedAfterLastExecute = 0;
        }

        // minor cleanup
        EndItems( );
    }

    m_workersActive = 0;

    return ret;
}

void vaRenderDeviceContextDX12::SetWorkers( const std::vector<shared_ptr<vaRenderDeviceContextWorkerDX12>> & workers, bool workersUseBundles ) 
{ 
    assert( !m_deviceDX12.IsFrameStarted() );
    m_workers = workers; 
    m_workersUseBundles = workersUseBundles;
    m_workerDrawResults.resize( workers.size( ), vaDrawResultFlags::None ); 
}
