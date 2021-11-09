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

#include "vaRenderDeviceDX12.h"

#include "Rendering/DirectX/vaTextureDX12.h"
#include "Rendering/DirectX/vaRenderDeviceContextDX12.h"
 
#include "Rendering/DirectX/vaShaderDX12.h"
 
#include "Rendering/DirectX/vaGPUTimerDX12.h"

#include "Rendering/DirectX/vaRenderBuffersDX12.h"

#include "Core/vaUI.h"
#include "IntegratedExternals/vaImguiIntegration.h"

#include "Core/vaProfiler.h"

#include "Core/vaInput.h"

#include "Objbase.h"

#include "Rendering/vaTextureHelpers.h"

#ifdef VA_TASKFLOW_INTEGRATION_ENABLED
#include "IntegratedExternals/vaTaskflowIntegration.h"
#endif


#ifdef VA_IMGUI_INTEGRATION_ENABLED
#include "IntegratedExternals/imgui/backends/imgui_impl_dx12.h"
#include "IntegratedExternals/imgui/backends/imgui_impl_win32.h"
#endif

#pragma warning ( disable : 4238 )  //  warning C4238: nonstandard extension used: class rvalue used as lvalue

#ifdef VA_USE_PIX3
#define USE_PIX
#pragma warning ( push )
#pragma warning ( disable: 4100 )
#include <pix3.h>
#pragma warning ( pop )
#pragma comment(lib, "WinPixEventRuntime.lib")
#endif

using namespace Vanilla;

#define ALLOW_DXGI_FULLSCREEN

namespace
{
    const DXGI_FORMAT                           c_DefaultBackbufferFormat       = DXGI_FORMAT_R8G8B8A8_UNORM;
    const DXGI_FORMAT                           c_DefaultBackbufferFormatRTV    = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

#ifdef ALLOW_DXGI_FULLSCREEN
    const uint32                                c_DefaultSwapChainFlags         = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
#else
    const uint32                                c_DefaultSwapChainFlags         = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING; 
#endif
    const D3D_FEATURE_LEVEL                     c_RequiredFeatureLevel          = D3D_FEATURE_LEVEL_12_1;

    typedef HRESULT( WINAPI * LPCREATEDXGIFACTORY2 )( UINT Flags, REFIID, void ** );

    static HMODULE                              s_hModDXGI = NULL;
    static LPCREATEDXGIFACTORY2                 s_DynamicCreateDXGIFactory2 = nullptr;
    static HMODULE                              s_hModD3D12 = NULL;
    static PFN_D3D12_CREATE_DEVICE              s_DynamicD3D12CreateDevice = nullptr;
}

void vaRenderDeviceDX12::RegisterModules( )
{
    void RegisterShaderDX12( );
    RegisterShaderDX12( );

    void RegisterBuffersDX12( );
    RegisterBuffersDX12( );
    
    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX12, vaTexture, vaTextureDX12 );
    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX12, vaGPUContextTracer, vaGPUContextTracerDX12 );

    void RegisterRenderMeshDX12( );
    RegisterRenderMeshDX12( );

    void RegisterRenderMaterialDX12( );
    RegisterRenderMaterialDX12( );

    //void RegisterPrimitiveShapeRendererDX12( );
    //RegisterPrimitiveShapeRendererDX12( );

    void RegisterGBufferDX12( );
    RegisterGBufferDX12( );

    void RegisterLightingDX12( );
    RegisterLightingDX12( );

    void RegisterCMAA2DX12( );
    RegisterCMAA2DX12( );

    void RegisterRaytracingDX12( );
    RegisterRaytracingDX12( );
}

vaRenderDeviceDX12::vaRenderDeviceDX12( const string & preferredAdapterNameID, const std::vector<wstring> & shaderSearchPaths ) : vaRenderDevice( ),
    m_nullCBV( *this ),
    m_nullSRV( *this ),
    m_nullUAV( *this ),
    m_nullBufferUAV( *this ),
    m_nullRTV( *this ),
    m_nullDSV( *this ),
    m_nullSamplerView( *this )
{
    m_preferredAdapterNameID = preferredAdapterNameID;

    assert( IsRenderThread() );
    static bool modulesRegistered = false;
    if( !modulesRegistered )
    {
        modulesRegistered = true;
        RegisterModules( );
    }

    if( !Initialize( shaderSearchPaths ) )
        return;
    InitializeBase( );
    m_valid = true;

    // handle initialization callbacks - the only issue is that frame has not really been started and there's no swap chain 
    // so we might get in trouble potentially with something but so far looks ok
    {
        BeginFrame( 0.0f );
        EndAndPresentFrame( );
    }
}

void vaRenderDeviceDX12::StartShuttingDown( )
{
//    assert( !m_shuttingDown );
//    m_shuttingDown = true;

    e_DeviceAboutToBeDestroyed.Invoke( );

    ReleaseSwapChainRelatedObjects( );

    BeginFrame( 0.0f );
    if( ExecuteBeginFrameCallbacks( ) )
    {
        VA_WARN( "vaRenderDeviceDX12::Deinitialize() - there were some m_beginFrameCallbacks calls; this likely means that some resources were created just before shutdown which is probably safe but inefficient" );
    }
    { std::unique_lock mutexLock( m_beginFrameCallbacksMutex ); m_beginFrameCallbacksDisable = true; }
    EndAndPresentFrame( );

    vaRenderDevice::StartShuttingDown( );
}

vaRenderDeviceDX12::~vaRenderDeviceDX12( void )
{
    assert( IsRenderThread() );
    assert( !m_frameStarted );

    vaFramePtrStatic::Cleanup( );

    if( !m_valid )
        return;

    // did we miss to call these? some logic could be broken but lambdas shouldn't hold any references anyway so it might be safe!
    { std::unique_lock mutexLock( m_beginFrameCallbacksMutex ); assert( m_beginFrameCallbacks.size() == 0 ); }

    SyncGPU( true );

    ImGuiDestroy( );

    // context gets nuked here!
    m_mainDeviceContext = nullptr;

    PSOCachesClearAll( );

    // make sure GPU is not executing anything from us anymore and call & clear all callbacks
    SyncGPU( true );

    DeinitializeBase( );

    if( m_fullscreenState != vaFullscreenState::Windowed )
        SetWindowed( );

    // // have to release these first!
    // m_mainDeviceContext->SetRenderTarget( nullptr, nullptr, false );
    ReleaseSwapChainRelatedObjects( );
    m_swapChain = nullptr;

    m_nullCBV.SafeRelease();
    m_nullSRV.SafeRelease();
    m_nullUAV.SafeRelease();
    m_nullBufferUAV.SafeRelease();
    m_nullRTV.SafeRelease();
    m_nullDSV.SafeRelease();
    m_nullSamplerView.SafeRelease();

    // one last time but clear all - as there's a queue for each frame/swapchain 
    SyncGPU( true );

    // and nuke the command queue
    m_fence.Reset();
    CloseHandle(m_fenceEvent);
    m_commandQueue = nullptr;

    m_defaultGraphicsRootSignature.Reset();
    m_defaultComputeRootSignature.Reset();

    // for( int i = 0; i < vaRenderDevice::c_BackbufferCount; i++ )
    //     for( int j = 0; j < c_defaultTransientDescriptorHeapsPerFrame; j++ )
    //         m_defaultTransientDescriptorHeaps[j][i].Deinitialize();
    m_defaultDescriptorHeaps.clear();
    m_defaultDescriptorHeapsInitialized = false;
    m_transientDescAllocator.Deinitialize();

    // we can call them all safely hopefully.
    Deinitialize( );

    // just a sanity check
    {
        std::unique_lock mutexLock(m_GPUFrameFinishedCallbacksMutex);
#ifdef _DEBUG
        for( int i = 0; i < _countof(m_GPUFrameFinishedCallbacks); i++ )
        {
            auto & callbacks = m_GPUFrameFinishedCallbacks[m_currentFrameFlipIndex];
            assert( callbacks.size() == 0 ) ;
        }
#endif
    }

}

string FormatAdapterID( const DXGI_ADAPTER_DESC3 & desc )
{
    string strID = vaStringTools::SimpleNarrow(wstring(desc.Description)) + vaStringTools::Format( " (%#010x)", desc.SubSysId );
    //std::replace( strID.begin( ), strID.end( ), ' ', '_' );
    return strID;
}

void EnsureDirectXAPILoaded( )
{
    if( s_hModDXGI == NULL )
    {
        s_hModDXGI = LoadLibrary( L"dxgi.dll" );
        if( s_hModDXGI == NULL )
            VA_ERROR( L"Unable to load dxgi.dll; Vista SP2, Win7 or above required" );
    }

    if( s_DynamicCreateDXGIFactory2 == nullptr && s_hModDXGI != NULL )
    {
        s_DynamicCreateDXGIFactory2 = (LPCREATEDXGIFACTORY2)GetProcAddress( s_hModDXGI, "CreateDXGIFactory2" );
        if( s_DynamicCreateDXGIFactory2 == nullptr )
            VA_ERROR( L"Unable to create CreateDXGIFactory1 proc; Vista SP2, Win7 or above required" );
    }

    if( s_hModD3D12 == NULL )
    {
        s_hModD3D12 = LoadLibrary( L"d3d12.dll" );
        if( s_hModD3D12 == NULL )
            VA_ERROR( L"Unable to load d3d12.dll; please install the latest DirectX." );
    }

    if( s_DynamicD3D12CreateDevice == NULL && s_hModD3D12 != NULL )
    {
        s_DynamicD3D12CreateDevice = (PFN_D3D12_CREATE_DEVICE)GetProcAddress( s_hModD3D12, "D3D12CreateDevice" );
        if( s_DynamicD3D12CreateDevice == NULL )
            VA_ERROR( L"D3D11CreateDevice proc not found" );
    }
}

/*
// Helper function for acquiring the first available hardware adapter that supports Direct3D 12.
// If no such adapter can be found, *ppAdapter will be set to nullptr.
static void GetHardwareAdapter(IDXGIFactory5* pFactory, IDXGIAdapter4** ppAdapter)
{
    ComPtr<IDXGIAdapter4> adapter;
    *ppAdapter = nullptr;

    IDXGIAdapter1 * adapter1Ptr;
    for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(adapterIndex, &adapter1Ptr); ++adapterIndex)
    {
        ComPtr<IDXGIAdapter1>(adapter1Ptr).As(&adapter);
        SAFE_RELEASE( adapter1Ptr );

        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            // Don't select the Basic Render Driver adapter.
            // If you want a software adapter, pass in "/warp" on the command line.
            continue;
        }

        // Check to see if the adapter supports Direct3D 12, but don't create the
        // actual device yet.
        if (SUCCEEDED(s_DynamicD3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
        {
            break;
        }
    }

    *ppAdapter = adapter.Detach();
}
*/

#define VERIFY_SUCCEEDED( x ) { if( FAILED(x) ) { assert( false ); } }

void vaRenderDeviceDX12::DeviceRemovedHandler( )
{
    ComPtr<ID3D12DeviceRemovedExtendedData> pDred;
    VERIFY_SUCCEEDED( m_device->QueryInterface( IID_PPV_ARGS( &pDred ) ) );

    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT DredAutoBreadcrumbsOutput;
    D3D12_DRED_PAGE_FAULT_OUTPUT DredPageFaultOutput;
    VERIFY_SUCCEEDED( pDred->GetAutoBreadcrumbsOutput( &DredAutoBreadcrumbsOutput ) );
    VERIFY_SUCCEEDED( pDred->GetPageFaultAllocationOutput( &DredPageFaultOutput ) );

    // Custom processing of DRED data can be done here.
    // Produce telemetry...
    // Log information to console...
    // break into a debugger...
}

bool vaRenderDeviceDX12::Initialize( const std::vector<wstring> & shaderSearchPaths )
{
    assert( IsRenderThread() );
    assert( !m_frameStarted );

    EnsureDirectXAPILoaded( );

    HRESULT hr = S_OK;
    UINT dxgiFactoryFlags = 0;

#if defined(VA_D3D12_USE_DEBUG_LAYER)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();

            // Enable additional debug layers.
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
        else
            { assert( false ); }

#if defined( VA_D3D12_USE_DEBUG_LAYER_GPU_VALIDATION ) // this is really slow so only do it occasionally 
        ComPtr<ID3D12Debug3> debugController3;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController3))))
        {
            debugController3->SetEnableGPUBasedValidation( TRUE );
        }
        else
            { assert( false ); }
#endif 

#if defined( VA_D3D12_USE_DEBUG_LAYER_DRED )
        ComPtr<ID3D12DeviceRemovedExtendedDataSettings> pDredSettings;
        if( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &pDredSettings ) ) ) )
        {
            // Turn on AutoBreadcrumbs and Page Fault reporting
            pDredSettings->SetAutoBreadcrumbsEnablement( D3D12_DRED_ENABLEMENT_FORCED_ON );
            pDredSettings->SetPageFaultEnablement( D3D12_DRED_ENABLEMENT_FORCED_ON );
        }
        else
            { assert( false ); }
#endif
    }
#endif
    // Create DXGI factory
    {
        hr = s_DynamicCreateDXGIFactory2( dxgiFactoryFlags, IID_PPV_ARGS(&m_DXGIFactory) );
        if( FAILED( hr ) )
            VA_ERROR( L"Unable to create DXGIFactory; Your Windows 10 probably needs updating" );
    }

    
    // create IDXGIAdapter1 based on m_preferredAdapterNameID
    ComPtr<IDXGIAdapter4> adapter;
    {
        bool useWarpDevice = m_preferredAdapterNameID == "WARP";

        if (useWarpDevice)
        {
            hr = m_DXGIFactory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
            if( FAILED( hr ) )
            {
                VA_ERROR( L"Unable to create WARP device" );
                adapter = nullptr;
            }
        }
        
        if( adapter == nullptr )
        {
            int i = 0;
            IDXGIAdapter1* adapterTemp;
            ComPtr<IDXGIAdapter4> adapterCandidate;
            while( m_DXGIFactory->EnumAdapters1(i, &adapterTemp) != DXGI_ERROR_NOT_FOUND )
            { 
	            i++; 
                hr = ComPtr<IDXGIAdapter1>(adapterTemp).As(&adapterCandidate);
                SAFE_RELEASE( adapterTemp );

                if( SUCCEEDED( hr ) )
                {
                    DXGI_ADAPTER_DESC3 desc;
                    adapterCandidate->GetDesc3( &desc );

                    // only hardware devices enumerated
                    if( (desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) != 0 )
                        continue;

                    // check feature level
                    if( FAILED( D3D12CreateDevice(adapterCandidate.Get(), c_RequiredFeatureLevel, _uuidof(ID3D12Device3), nullptr) ) )
                        continue;

                    // use first good by default
                    if( adapter == nullptr )
                    {
                        adapter = adapterCandidate;
                    }

                    if( FormatAdapterID(desc) == m_preferredAdapterNameID )
                    {
                        adapter = adapterCandidate;
                        break;
                    }
                }
            } 
        }

        V_RETURN( s_DynamicD3D12CreateDevice(
                adapter.Get(),
                c_RequiredFeatureLevel,
                IID_PPV_ARGS(&m_device)
                ));
    }

    // collect device capabilities
    {
        // reset
        m_caps = vaRenderDeviceCapabilities();

        // this figures out shader model required
        string shaderModelRequiredString = vaShaderDX12::GetSMVersionStatic();
        assert( shaderModelRequiredString == "6_3" ); // did this change? change the test below!
        D3D12_FEATURE_DATA_SHADER_MODEL shaderModelRequired = {D3D_SHADER_MODEL_6_3};
        D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = shaderModelRequired;
        if( FAILED( m_device->CheckFeatureSupport( D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof( shaderModel ) ) ) 
            || (shaderModel.HighestShaderModel != shaderModelRequired.HighestShaderModel ) )
        {
            VA_ERROR( "Sorry, this application requires a GPU/driver that supports shader model %s - it is possible that a driver update could fix this.", shaderModelRequiredString.c_str() );
            return false;
        }

        // Check Barycentrics support
        D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3;
        if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &options3, sizeof(options3))))
        {
            m_caps.Other.BarycentricsSupported = options3.BarycentricsSupported == TRUE;
        }

        // Check raytracing support
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5;
        if( SUCCEEDED( m_device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof( options5 ) ) ) )
        {
            m_caps.Raytracing.Supported = options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0;
        }

#if defined(NTDDI_WIN10_19H1) || defined(NTDDI_WIN10_RS6)
        // Check that VRS Tier 1 is supported
        D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6;
        if( SUCCEEDED( m_device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS6, &options6, sizeof( options6 ) ) ) )
        {
            m_caps.VariableShadingRate.Tier1 = options6.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_1;
            m_caps.VariableShadingRate.Tier2 = options6.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2;
            m_caps.VariableShadingRate.AdditionalShadingRatesSupported = options6.AdditionalShadingRatesSupported == TRUE;
            m_caps.VariableShadingRate.PerPrimitiveShadingRateSupportedWithViewportIndexing = options6.AdditionalShadingRatesSupported == TRUE;
            m_caps.VariableShadingRate.ShadingRateImageTileSize = options6.ShadingRateImageTileSize;
        }
#endif
    }

    // Describe and create the command queue.
    // command queue is part of vaRenderDevice, command lists are part of vaRenderContext
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        V_RETURN( m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)) );
    }

    {
        // Disable some annoying warnings
        ComPtr<ID3D12InfoQueue> d3dInfoQueue;
        hr = m_device.As(&d3dInfoQueue);
        if( SUCCEEDED( hr ) )
        {
            D3D12_MESSAGE_ID hide [] =
            {
                D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
                D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
                D3D12_MESSAGE_ID_EXECUTECOMMANDLISTS_GPU_WRITTEN_READBACK_RESOURCE_MAPPED,  // not sure why this even exists since the use case is totally legit (see https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-map#advanced-usage-models, or comments in https://docs.microsoft.com/en-us/windows/win32/direct3d12/readback-data-using-heaps)
                // D3D12_MESSAGE_ID_COPY_DESCRIPTORS_INVALID_RANGES,                   // <- false positives; remove once bug in debugging layer gets fixed
                // D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,                             // for the following warning (that happens in DirectXTexD3D12.cpp line 743 for example): D3D12 WARNING: ID3D12Resource1::ID3D12Resource::Map: pReadRange is NULL and the heap page property is WRITE_BACK. This can be an indicator of inefficiencies that will result on mobile systems, as the range should be as tight as possible. But, it is also possible the range must encompass the whole subresource.A NULL pReadRange parameter indicates all resource memory will be read by the CPU.  [ EXECUTION WARNING #930: MAP_INVALID_NULLRANGE]
                //D3D12_MESSAGE_ID_COMMAND_LIST_STATIC_DESCRIPTOR_RESOURCE_DIMENSION_MISMATCH // for mapping null descriptors 
                // TODO: Add more message IDs here as needed 
            };
            D3D12_INFO_QUEUE_FILTER filter;
            memset(&filter, 0, sizeof(filter));
            filter.DenyList.NumIDs = _countof(hide);
            filter.DenyList.pIDList = hide;
            hr = d3dInfoQueue->AddStorageFilterEntries(&filter);
            assert( SUCCEEDED( hr ) );

            d3dInfoQueue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_CORRUPTION, true );
            d3dInfoQueue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_ERROR, true );
            d3dInfoQueue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_WARNING, true );
        }
    }

    V( m_device->SetName( L"MainDevice" ) );
    V( m_commandQueue->SetName( L"MainDeviceCommandQueue" ) );

    // Default descriptor heaps
    {
        m_defaultDescriptorHeaps.resize( (int)D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES+1 );
        //                                                                         ^ this is small hack
        // this +1 is the hack to satisfy ClearUAVs and similar which require that "descriptor must not be in a shader-visible descriptor heap." 
        // (see https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-clearunorderedaccessviewuint)

        // To avoid following error, keep the desc count below 1 mil:
        // "device supports D3D12_RESOURCE_BINDING_TIER_3 which supports shader visible descriptor heap size larger than 1000000, but this may fail to create on some TIER_3 hardware.[STATE_CREATION WARNING #1013: CREATE_DESCRIPTOR_HEAP_LARGE_NUM_DESCRIPTORS]
        int CBV_SRV_UAV_PersistentCount     = 200 * 1000;
        int CBV_SRV_UAV_TransientCount      = 500 * 1000;       // a.k.a. dynamic, per-frame

        for( int i = 0; i < m_defaultDescriptorHeaps.size(); i++ )
        {
            D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
            heapDesc.Type = (D3D12_DESCRIPTOR_HEAP_TYPE)i;
            if( i == D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES )
                heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;    // special case, just for CPU-readable CBV_SRV_UAVs
            heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            std::wstring name = L"";
            int reserveCapacity = 0;
            switch( heapDesc.Type )
            {
            case ( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ) : 
                if( i != D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES )
                {
                    heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                    heapDesc.NumDescriptors = CBV_SRV_UAV_PersistentCount + CBV_SRV_UAV_TransientCount;           // this is 10x the amount needed for the supersampled reference with 10000-ish draw calls
                    name                    = L"DefaultPersistentHeap_CBV_SRV_UAV";
                    reserveCapacity         = CBV_SRV_UAV_TransientCount;
                }
                else
                {
                    heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; // this means it's CPU readable
                    heapDesc.NumDescriptors = CBV_SRV_UAV_PersistentCount;           // I have no idea how many are needed but given that it's UAV-specific, probably less than the generic one above
                    name                    = L"DefaultPersistentHeap_CBV_SRV_UAV_CPUREADABLE";
                }
                break;
            case ( D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ) :
                heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; //no longer using this heap for shader-visible
                heapDesc.NumDescriptors = 128;
                name                    = L"DefaultPersistentHeap_Sampler";
                break;
            case ( D3D12_DESCRIPTOR_HEAP_TYPE_RTV ) :
                heapDesc.NumDescriptors = 4 * 1024;             // these can get pretty large with a lot of render-to-texture usage during data processing
                name                    = L"DefaultPersistentHeap_RTV";
                break;
            case ( D3D12_DESCRIPTOR_HEAP_TYPE_DSV ) :
                heapDesc.NumDescriptors = 1 * 1024;             // these can get pretty large with a lot of render-to-texture usage during data processing
                name                    = L"DefaultPersistentHeap_DSV";
                break;
            default:
                assert( false ); // new type added?
                break;
            }
            m_defaultDescriptorHeaps[i].Initialize( *this, heapDesc, reserveCapacity );

            V( m_defaultDescriptorHeaps[i].GetHeap()->SetName( name.c_str() ) ); 
        }
        m_defaultDescriptorHeapsInitialized = true;

        m_transientDescAllocator.Initialize( &m_defaultDescriptorHeaps[(int)D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV], CBV_SRV_UAV_TransientCount );
    }


    {
        DXGI_ADAPTER_DESC3 adapterDesc3;
        adapter->GetDesc3( &adapterDesc3 );

        string name = vaStringTools::SimpleNarrow( wstring( adapterDesc3.Description ) );
        // std::replace( name.begin( ), name.end( ), ' ', '_' );

        m_adapterNameShort  = name;
        m_adapterNameID     = FormatAdapterID(adapterDesc3); //vaStringTools::Format( L"%s-%08X_%08X", name.c_str( ), adapterDesc1.DeviceId, adapterDesc1.Revision );
        m_adapterVendorID   = adapterDesc3.VendorId;


        if( (adapterDesc3.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) != 0 )
            m_adapterNameID = "WARP";

        m_adapterLUIDLow    = adapterDesc3.AdapterLuid.LowPart;
        m_adapterLUIDHigh   = adapterDesc3.AdapterLuid.HighPart;

        VA_LOG( "vaRenderDeviceDX12::Initialize - created adapter %s - %s", m_adapterNameShort.c_str(), m_adapterNameID.c_str() );
    }

    // synchronization objects
    {
        V( m_device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence) ) );
        V( m_fence->SetName( L"MainDeviceFence" ) );
        m_lastFenceValue = 0;
        for( int i = 0; i < _countof(m_fenceValues); i++ )
        	m_fenceValues[i] = 0;

        // Create an event handle to use for frame synchronization.
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr)
        {
            V( HRESULT_FROM_WIN32(GetLastError()) );
        }
    }

    // // stats
    // {
    //     D3D12_QUERY_HEAP_DESC queryHeapDesc;
    //     queryHeapDesc.Count     = 1;
    //     queryHeapDesc.NodeMask  = 0;
    //     queryHeapDesc.Type      = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
    //     m_device->CreateQueryHeap( &queryHeapDesc, IID_PPV_ARGS(&m_statsHeap) );
    //     m_statsHeap->SetName( L"Stats heap" );
    // 
    //     for( int i = 0; i < _countof(m_statsReadbackBuffers); i++ )
    //         m_statsReadbackBuffers[i] = shared_ptr<vaRenderBufferDX12>( new vaRenderBufferDX12( *this, sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS ), vaResourceAccessFlags::CPURead | vaResourceAccessFlags::CPUReadManuallySynced ) );
    // }

    // Shader manager
    {
        m_shaderManager = shared_ptr<vaShaderManager>( new vaDirectX12ShaderManager( *this ) );
        for( auto s : shaderSearchPaths ) m_shaderManager->RegisterShaderSearchPath( s );
    }

    // null descriptors
    {
        m_nullCBV.CreateNull();
        m_nullSRV.CreateNull();
        m_nullUAV.CreateNull(D3D12_UAV_DIMENSION_TEXTURE1D);
        m_nullBufferUAV.CreateNull(D3D12_UAV_DIMENSION_BUFFER);
        m_nullRTV.CreateNull();
        m_nullDSV.CreateNull();
        m_nullSamplerView.CreateNull();
    }

        // Create the root signature.
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

        // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

        if( FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))) )
        {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }

        // Ok, here's how we're going to do this.
        //
        // * First come constants. These are 'root CBVs'. There's 10 at the moment but this is way more than actually needed. 
        // * Then come UAVs. These are single item tables for each UAV. In the future, if this ever becomes an issue, we can split 
        //   them into two tables: global (for vaShaderItemGlobals) and 'less global' (for vaGraphicsItem and vaComputeItem).
        // * Then come SRVs. These will be bindless.

        D3D12_ROOT_DESCRIPTOR_FLAGS  rootDescFlags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
        D3D12_DESCRIPTOR_RANGE_FLAGS descRangeFlags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;     // D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE

        constexpr int padding = 0; //10;
        constexpr int rootParameterCount = DefaultRootSignatureParams::TotalParameters;

        CD3DX12_ROOT_PARAMETER1     rootParameters[rootParameterCount+padding];
        CD3DX12_DESCRIPTOR_RANGE1   rootRanges[rootParameterCount];         // these are unused for CBVs

        // Constants  (direct CBV descriptors: https://docs.microsoft.com/en-us/windows/win32/direct3d12/using-descriptors-directly-in-the-root-signature)
        for( int i = 0; i < DefaultRootSignatureParams::GlobalDirectCBVCount; i++ )
            rootParameters[DefaultRootSignatureParams::GlobalDirectCBVBase + i].InitAsConstantBufferView ( i+DefaultRootSignatureParams::GlobalDirectCBVSlotBase,  0, rootDescFlags, D3D12_SHADER_VISIBILITY_ALL );
        for( int i = 0; i < DefaultRootSignatureParams::PerDrawDirectCBVCount; i++ )
            rootParameters[DefaultRootSignatureParams::PerDrawDirectCBVBase + i].InitAsConstantBufferView( i+DefaultRootSignatureParams::PerDrawDirectCBVSlotBase, 0, rootDescFlags, D3D12_SHADER_VISIBILITY_ALL );

        // Global UAVs/SRVs, all in one root parameter
        CD3DX12_DESCRIPTOR_RANGE1 rangesGlobalUAVSRVs[3];
        rangesGlobalUAVSRVs[0].Init( D3D12_DESCRIPTOR_RANGE_TYPE_UAV, array_size(vaShaderItemGlobals::UnorderedAccessViews),    DefaultRootSignatureParams::GlobalUAVSlotBase, 0, descRangeFlags,   DefaultRootSignatureParams::DescriptorOffsetGlobalUAV );
        rangesGlobalUAVSRVs[1].Init( D3D12_DESCRIPTOR_RANGE_TYPE_UAV, vaRenderOutputs::c_maxUAVs,                               DefaultRootSignatureParams::OutputsUAVSlotBase, 0, descRangeFlags,  DefaultRootSignatureParams::DescriptorOffsetOutputsUAV );
        rangesGlobalUAVSRVs[2].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, array_size( vaShaderItemGlobals::ShaderResourceViews ),   DefaultRootSignatureParams::GlobalSRVSlotBase, 0, descRangeFlags,   DefaultRootSignatureParams::DescriptorOffsetGlobalSRV );

        rootParameters[DefaultRootSignatureParams::GlobalUAVSRVBase].InitAsDescriptorTable( countof(rangesGlobalUAVSRVs), &rangesGlobalUAVSRVs[0], D3D12_SHADER_VISIBILITY_ALL );

        for( int i = 0; i < DefaultRootSignatureParams::PerDrawSRVCount; i++ )
        {
            int index = DefaultRootSignatureParams::PerDrawSRVBase+i;
            rootRanges[index].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, i + DefaultRootSignatureParams::PerDrawSRVSlotBase, 0, descRangeFlags );
            rootParameters[index].InitAsDescriptorTable( 1, &rootRanges[index], D3D12_SHADER_VISIBILITY_ALL );
        }

        // raytracing struct
        rootParameters[DefaultRootSignatureParams::RaytracingStructDirectSRV].InitAsShaderResourceView( SHADERGLOBAL_SRV_SLOT_RAYTRACING_ACCELERATION, 0 );

        rootParameters[DefaultRootSignatureParams::InstanceIndexDirectUINT32].InitAsConstants( 1, SHADER_INSTANCE_INDEX_ROOT_CONSTANT_SLOT, 0 );

        rootParameters[DefaultRootSignatureParams::GenericRootConstDirectUINT32].InitAsConstants( 1, SHADER_GENERIC_ROOT_CONSTANT_SLOT, 0 );

        rootRanges[DefaultRootSignatureParams::Bindless1SRVBase].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, DefaultRootSignatureParams::Bindless1SRVSlotBase, DefaultRootSignatureParams::Bindless1SRVRegSpace, descRangeFlags );
        rootParameters[DefaultRootSignatureParams::Bindless1SRVBase].InitAsDescriptorTable( 1, &rootRanges[DefaultRootSignatureParams::Bindless1SRVBase], D3D12_SHADER_VISIBILITY_ALL );
        rootRanges[DefaultRootSignatureParams::Bindless2SRVBase].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, DefaultRootSignatureParams::Bindless2SRVSlotBase, DefaultRootSignatureParams::Bindless2SRVRegSpace, descRangeFlags );
        rootParameters[DefaultRootSignatureParams::Bindless2SRVBase].InitAsDescriptorTable( 1, &rootRanges[DefaultRootSignatureParams::Bindless2SRVBase], D3D12_SHADER_VISIBILITY_ALL );

        // root constants - not used at the moment except for padding 
        for( int i = 0; i < padding; i++ )
            rootParameters[i+DefaultRootSignatureParams::TotalParameters].InitAsConstants( 4, SHADER_GENERIC_ROOT_CONSTANT_SLOT+1+i, 0, D3D12_SHADER_VISIBILITY_ALL );

        D3D12_STATIC_SAMPLER_DESC defaultSamplers[7];
        vaDirectXTools12::FillSamplerStatePointClamp(         defaultSamplers[0] );
        vaDirectXTools12::FillSamplerStatePointWrap(          defaultSamplers[1] );
        vaDirectXTools12::FillSamplerStateLinearClamp(        defaultSamplers[2] );
        vaDirectXTools12::FillSamplerStateLinearWrap(         defaultSamplers[3] );
        vaDirectXTools12::FillSamplerStateAnisotropicClamp(   defaultSamplers[4] );
        vaDirectXTools12::FillSamplerStateAnisotropicWrap(    defaultSamplers[5] );
        vaDirectXTools12::FillSamplerStateShadowCmp(          defaultSamplers[6] );

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, _countof(defaultSamplers), defaultSamplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        if( FAILED( D3DX12SerializeVersionedRootSignature( &rootSignatureDesc, featureData.HighestVersion, &signature, &error ) ) )
        {
            wstring errorMsg = vaStringTools::SimpleWiden( (char*)error->GetBufferPointer( ) );
            VA_ERROR( L"Error serializing versioned root signature: \n %s", errorMsg.c_str() );
        }
        V( m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_defaultGraphicsRootSignature)) );
        V( m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_defaultComputeRootSignature)) );
        V( m_defaultGraphicsRootSignature->SetName( L"DefaultGraphicsRootSignature" ) );
        V( m_defaultComputeRootSignature->SetName( L"DefaultGraphicsRootSignature" ) );
    }

    // device contexts
    {
        assert( m_nonWorkerRenderContextCount == 0 );

        // there can be only one main device context
        std::shared_ptr< vaRenderDeviceContextDX12 > mainContext( new vaRenderDeviceContextDX12( *this, 0 ) );
        m_nonWorkerRenderContextCount = 1;
        m_mainDeviceContext = mainContext;

        int availableThreads, workerCount;
        GetMultithreadingParams( availableThreads, workerCount );
        SetMultithreadingParams( availableThreads );
    }

    e_DeviceFullyInitialized.Invoke( *this );

    return true;
}

void vaRenderDeviceDX12::SetMultithreadingParams( int workerCount )
{
    // make sure we've finished all rendering related to existing workers (since allocators and other structs will get deleted below)
    SyncGPU( true );

    // this removes and deletes all existing workers
    AsFullDX12(*m_mainDeviceContext).SetWorkers( {}, m_workersUseBundleCommandLists );

    // make sure any resources held by workers that were delay-released get destroyed (not needed probably)
    SyncGPU( true );

#if  defined(VA_TASKFLOW_INTEGRATION_ENABLED)   // enable multithreading / worker contexts
    std::vector<shared_ptr<vaRenderDeviceContextWorkerDX12>> workers;

    for( int i = 0; i < workerCount; i++ )
        workers.push_back( std::shared_ptr< vaRenderDeviceContextWorkerDX12 >( new vaRenderDeviceContextWorkerDX12( *this, m_nonWorkerRenderContextCount+i, std::dynamic_pointer_cast<vaRenderDeviceContextDX12>(m_mainDeviceContext), m_workersUseBundleCommandLists ) ) );

    AsFullDX12( *m_mainDeviceContext ).SetWorkers( workers, m_workersUseBundleCommandLists );

    m_multithreadedWorkerCount  = workerCount;
#else
    assert( workerCount == 1 );
    m_multithreadedWorkerCount  = 1;
#endif

}

void vaRenderDeviceDX12::UIMenuHandler( vaApplicationBase& app )
{
    vaRenderDevice::UIMenuHandler( app );

    int threadsAvailable, workerCount;
    GetMultithreadingParams( threadsAvailable, workerCount );

#ifdef VA_IMGUI_INTEGRATION_ENABLED
    static int uiWorkerCount    = workerCount;
    static bool uiWorkerBundles = m_workersUseBundleCommandLists;
    if( ImGui::BeginMenu( "Threading", "" ) )
    {
        if( threadsAvailable <= 1 )
        {
            ImGui::Text( "Threading disabled, no CPU threads available" );
        }
        else
        {
            ImGui::PushItemWidth( ImGui::CalcTextSize("SPACEFORCTRL").x);
            ImGui::InputInt( vaStringTools::Format("Worker count (default %d)", threadsAvailable).c_str(), &uiWorkerCount );
            ImGui::Checkbox( "Use 'bundle' command lists", &uiWorkerBundles );
            ImGui::PopItemWidth();
            uiWorkerCount = vaMath::Clamp( uiWorkerCount, 1, c_maxWorkers );
            if( workerCount != uiWorkerCount || m_workersUseBundleCommandLists != uiWorkerBundles )
            {
                if( ImGui::Button( "Apply changes", {-1,0} ) )
                {
                    m_workersUseBundleCommandLists = uiWorkerBundles;
                    SetMultithreadingParams( uiWorkerCount );
                }
            }
        }

        ImGui::EndMenu();
    }
    else
    {
        uiWorkerCount   = workerCount;
        uiWorkerBundles = m_workersUseBundleCommandLists;
    }
#endif
}


void vaRenderDeviceDX12::Deinitialize( )
{
    assert( IsRenderThread() );
    assert( !m_frameStarted );

    if( m_device != nullptr )
    {
        // no idea how to do this for D3D12 yet
        // more on http://seanmiddleditch.com/direct3d-11-debug-api-tricks/
#if 0//_DEBUG
        ID3D11Debug* debugDevice = nullptr;
        HRESULT hr = m_device->QueryInterface( __uuidof( ID3D11Debug ), reinterpret_cast<void**>( &debugDevice ) );
        assert( SUCCEEDED( hr ) );
        debugDevice->ReportLiveDeviceObjects( D3D11_RLDO_DETAIL );
        SAFE_RELEASE( debugDevice );
#endif

        m_adapterNameShort  = "";
        m_adapterNameID     = "";
        m_adapterVendorID   = 0;
        m_device.Reset();
        m_DXGIFactory.Reset();
        m_hwnd = 0;
        m_currentFrameFlipIndex = 0;
    }
}

void vaRenderDeviceDX12::CreateSwapChain( int width, int height, HWND hwnd, vaFullscreenState fullscreenState )
{
    assert( IsRenderThread() );
    assert( !m_frameStarted );

    m_swapChainTextureSize.x = width;
    m_swapChainTextureSize.y = height;
    m_hwnd = hwnd;

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount       = vaRenderDeviceDX12::c_SwapChainBufferCount;
    swapChainDesc.Width             = width;
    swapChainDesc.Height            = height;
    swapChainDesc.Format            = c_DefaultBackbufferFormat;
    swapChainDesc.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_BACK_BUFFER;
    swapChainDesc.SwapEffect        = DXGI_SWAP_EFFECT_FLIP_DISCARD; // DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL
    swapChainDesc.Flags             = c_DefaultSwapChainFlags;
    swapChainDesc.SampleDesc.Count  = 1;

//#ifdef ALLOW_DXGI_FULLSCREEN
//    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreenDesc = {};
//    fullscreenDesc.RefreshRate      = {0,0};
//    fullscreenDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
//    fullscreenDesc.Scaling          = DXGI_MODE_SCALING_UNSPECIFIED;
//    fullscreenDesc.Windowed         = !fullscreen;
//#endif
//
//    m_fullscreen = fullscreen;

    HRESULT hr;

    ComPtr<IDXGISwapChain1> swapChain;
    hr = m_DXGIFactory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
        hwnd,
        &swapChainDesc,
//#ifdef ALLOW_DXGI_FULLSCREEN
//        &fullscreenDesc,
//#else
        nullptr,
//#endif
        nullptr,
        &swapChain
        );
    V( hr );

    // stop automatic alt+enter, we'll handle it manually
    {
        hr = m_DXGIFactory->MakeWindowAssociation( hwnd, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER );
    }

    V( swapChain.As( &m_swapChain ) );

    // now switch to fullscreen if we are in fullscreen
    assert( fullscreenState != vaFullscreenState::Unknown );
    if( fullscreenState == vaFullscreenState::Fullscreen )
        ResizeSwapChain( m_swapChainTextureSize.x, m_swapChainTextureSize.y, fullscreenState );
    else
        m_fullscreenState = fullscreenState;

    CreateSwapChainRelatedObjects( );
    ImGuiCreate( );
}

void vaRenderDeviceDX12::CreateSwapChainRelatedObjects( )
{
    assert( IsRenderThread() );
    assert( !m_frameStarted );

    HRESULT hr;

    DXGI_SWAP_CHAIN_DESC scdesc;
    V( m_swapChain->GetDesc( &scdesc ) );

    m_renderTargets.clear();
    m_renderTargets.resize(scdesc.BufferCount);

    VA_LOG( "(Re)creating SwapChain, %dx%d, buffer count: %d", m_swapChainTextureSize.x, m_swapChainTextureSize.y, scdesc.BufferCount );

    // Create a RTV and a command allocator for each frame.
    for( int i = 0; i < (int)scdesc.BufferCount; i++ )
    {
        ComPtr<ID3D12Resource> renderTarget;
        m_swapChain->GetBuffer( i, IID_PPV_ARGS(&renderTarget) );

        D3D12_RESOURCE_DESC resDesc = renderTarget->GetDesc( );
        assert( resDesc.Width == m_swapChainTextureSize.x );
            
        // D3D12_RENDER_TARGET_VIEW_DESC desc;
        // VA_VERIFY( vaDirectXTools12::FillRenderTargetViewDesc( desc, m_renderTargets[i].Get(), c_DefaultBackbufferFormatSRV ), "Unable to initialize render target view desc" );
        // 
        // m_renderTargetViews[i].Create( m_renderTargets[i].Get(), desc );

        m_renderTargets[i] = std::shared_ptr< vaTexture >( vaTextureDX12::CreateWrap( *this, renderTarget.Get(), vaResourceFormat::Automatic, VAFormatFromDXGI(c_DefaultBackbufferFormatRTV) ) );
        m_renderTargets[i]->SetName( vaStringTools::Format( "BackbufferColor_%d", i ) );
    }

    m_currentSwapChainBufferIndex = m_swapChain->GetCurrentBackBufferIndex( );
}

void vaRenderDeviceDX12::ReleaseSwapChainRelatedObjects( )
{
    assert( IsRenderThread() );
    assert( !m_frameStarted );
    
    m_renderTargets.clear( );

    assert( m_currentBackbuffer.RenderTargetCount == 0 && m_currentBackbuffer.RenderTargets[0] == nullptr && m_currentBackbuffer.DepthStencil == nullptr );
    for( int i = 0; i < m_currentBackbuffer.c_maxUAVs; i++ )
        { assert( m_currentBackbuffer.UnorderedAccessViews[i] == nullptr ); }
    m_currentBackbuffer.Reset();

    // this releases any residual pointers and allows for correct swap chain resize
    vaFramePtrStatic::Cleanup( );

    SyncGPU( true );

    m_swapChainTextureSize = {0,0};
}

void vaRenderDeviceDX12::SetWindowed( )
{
    m_fullscreenState = vaFullscreenState::Windowed;

#ifdef ALLOW_DXGI_FULLSCREEN
    if( m_swapChain != nullptr )
    {
        HRESULT hr = m_swapChain->SetFullscreenState( false, NULL );
        if( FAILED( hr ) )
        {
            VA_WARN( L"Error in a call to m_swapChain->SetFullscreenState( false ) [%x]", hr );
        }
    }
#endif
}

bool vaRenderDeviceDX12::ResizeSwapChain( int width, int height, vaFullscreenState fullscreenState )
{
    assert( IsRenderThread() );
    assert( !m_frameStarted );
    assert( fullscreenState != vaFullscreenState::Unknown );

    if( width < 8 || height < 8 )
    { 
        VA_WARN( "vaRenderDeviceDX12::ResizeSwapChain request not valid (%d, %d)", width, height );
        return false; 
    }

    if( m_swapChain == nullptr ) return false;

    if( (int)m_swapChainTextureSize.x == width && (int)m_swapChainTextureSize.y == height && m_fullscreenState == fullscreenState )
        return false;

    ReleaseSwapChainRelatedObjects( );

    m_swapChainTextureSize.x = width;
    m_swapChainTextureSize.y = height;
    m_fullscreenState = fullscreenState;

    HRESULT hr;

#ifdef ALLOW_DXGI_FULLSCREEN
    hr = m_swapChain->SetFullscreenState( m_fullscreenState == vaFullscreenState::Fullscreen, NULL );
    if( FAILED( hr ) )
    {
        VA_WARN( L"Error in a call to m_swapChain->SetFullscreenState [%x]", hr );
        if( m_fullscreenState != vaFullscreenState::Windowed )
        {
            m_fullscreenState = vaFullscreenState::FullscreenBorderless;
            VA_WARN( L"Falling back to borderless fullscreen", hr );
        }
    }
#endif

    hr = m_swapChain->ResizeBuffers( vaRenderDeviceDX12::c_SwapChainBufferCount, width, height, DXGI_FORMAT_UNKNOWN, c_DefaultSwapChainFlags );
    if( FAILED( hr ) )
    {
        assert( false );
        //VA_ERROR( L"Error trying to m_swapChain->ResizeBuffers" );
        return false;
    }

    CreateSwapChainRelatedObjects( );

    // handle initialization/destruction callbacks
    {
        BeginFrame( 0.0f );
        EndAndPresentFrame( );
    }

    return true;
}

void vaRenderDeviceDX12::BeginFrame( float deltaTime )
{
    assert( IsRenderThread() );

#ifdef VA_D3D12_FORCE_IMMEDIATE_SYNC
    SyncGPU( false );
#endif

    // SyncAndAdvanceFrame
    {
        VA_TRACE_CPU_SCOPE( SyncOldestGPUFrame );
        //vaGPUTimerManagerDX12::GetInstance().PIXBeginEvent( L"Wait on sync" );

        // Update the frame index to the new one we'll use to render the next frame.
        m_currentFrameFlipIndex = (m_currentFrameFlipIndex+1) % vaRenderDevice::c_BackbufferCount;

        m_timeSpanCPUGPUSyncStalls = 0;

        double timeNow      = vaCore::TimeFromAppStart();
        m_timeSpanCPUFrame  = std::max( 0.0, timeNow - m_timeBeforeSync );
        m_timeBeforeSync    = timeNow;
            
        // Wait for the oldest frame to be done on the GPU.
        SyncGPUFrame( c_BackbufferCount );
        // let the allocator know that we've synced to the older frame(s) and it can drop the oldest barrier(s)!
        m_transientDescAllocator.NextFrame( );
            
        m_timeSpanCPUGPUSync    = std::max( 0.0, vaCore::TimeFromAppStart( ) - m_timeBeforeSync );

        //vaGPUTimerManagerDX12::GetInstance().PIXEndEvent( ); // L"End wait on sync"

        {
            VA_TRACE_CPU_SCOPE( GPUFrameFinishedCallbacks );
            ExecuteGPUFrameFinishedCallbacks( true );
        }
    }

    PSOCachesClearUnusedTick( );

    vaRenderDevice::BeginFrame( deltaTime );

    // PrepareNextTransientDescriptorHeap( );

    m_mainDeviceContext->BeginFrame( );

    // execute begin frame callbacks - mostly initialization stuff that requires a command list (main context)
    ExecuteBeginFrameCallbacks();

    ExecuteAsyncBeginFrameCallbacks( deltaTime );

    e_AfterBeginFrame( *this, deltaTime );

    //AsDX12( *m_mainDeviceContext ).GetCommandList( )->BeginQuery( m_statsHeap.Get( ), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0 );
}

void vaRenderDeviceDX12::EndAndPresentFrame( int vsyncInterval )
{
    e_BeforeEndFrame( *this );

    {
        VA_TRACE_CPU_SCOPE( PresentTransitions );

        assert( IsRenderThread() );

        // remove all cached outputs so we can present
        AsFullDX12( *m_mainDeviceContext ).CommitOutputsRaw( vaRenderTypeFlags::Graphics, vaRenderOutputs( ) );

        // Indicate that the back buffer will now be used to present.
        //vaGPUTimerManagerDX12::GetInstance().PIXBeginEvent( L"TransitionToPresentAndFlush" );
        if( m_swapChain != nullptr && GetCurrentBackbufferTexture( ) != nullptr )
            AsDX12( *GetCurrentBackbufferTexture( ) ).TransitionResource( AsDX12( *m_mainDeviceContext ), D3D12_RESOURCE_STATE_PRESENT );
        //vaGPUTimerManagerDX12::GetInstance().PIXEndEvent(); // L"TransitionToPresentAndFlush"
    }

    m_mainDeviceContext->EndFrame( );

    {
        // we can only start this scope here because m_mainDeviceContext->EndFrame( ) above triggers a custom scope and we want to avoid ours overlapping with it
        // VA_TRACE_CPU_SCOPE( Present );

        if( m_swapChain != nullptr )
        {
#ifdef VA_USE_PIX3
            PIXScopedEvent( GetCommandQueue( ).Get( ), PIX_COLOR_INDEX( 1 ), "Present" );
#endif

            //vaGPUTimerManagerDX12::GetInstance().PIXBeginEvent( L"Present!" );
    #ifdef ALLOW_DXGI_FULLSCREEN
            BOOL isFullscreen = false;
            m_swapChain->GetFullscreenState( &isFullscreen, nullptr );
            if( m_fullscreenState == vaFullscreenState::Fullscreen && !isFullscreen )
            {
                m_fullscreenState = vaFullscreenState::Windowed;
                VA_WARN( "Fullscreen state changed by external factors (alt+tab or an unexpected issue), readjusting..." );
            }
            else
    #endif
            { 
                // Just make sure we haven't messed something up
                assert( m_currentSwapChainBufferIndex == (( m_swapChain != nullptr ) ? ( m_swapChain->GetCurrentBackBufferIndex( ) ) : ( 0 )) );

                //VA_TRACE_CPU_SCOPE( Present );

                DXGI_PRESENT_PARAMETERS pp; memset( &pp, 0, sizeof( pp ) );
                double timeNow = vaCore::TimeFromAppStart( );
                HRESULT hr = m_swapChain->Present1( (UINT)vsyncInterval, (isFullscreen == 0 && vsyncInterval == 0)?(DXGI_PRESENT_ALLOW_TEARING):(0), &pp );
                m_timeSpanCPUPresent = vaCore::TimeFromAppStart( ) - timeNow;
                if( FAILED( hr ) )
                {
                    // only asserting here to allow for debugging, otherwise it should be handled correctly below (but never tested, don't know how)
                    assert( false );

                    if( hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_HUNG )
                        DeviceRemovedHandler();

                    VA_WARN( "Present failed" );
                }

                // Update the swap chain buffer index to the new one we'll use to render the next frame into.
                m_currentSwapChainBufferIndex = ( m_swapChain != nullptr ) ? ( m_swapChain->GetCurrentBackBufferIndex( ) ) : ( 0 );
            }
        }
        m_mainDeviceContext->PostPresent( );


        {
            VA_TRACE_CPU_SCOPE( SignalFrameFence );
#ifdef VA_USE_PIX3
            PIXScopedEvent( GetCommandQueue( ).Get( ), PIX_COLOR_INDEX( 1 ), "SignalFrameFence" );
#endif

            HRESULT hr;
            // Schedule a signal command in the queue for this frame (the one being presented).
            m_lastFenceValue++; // advance the fence!
            m_fenceValues[m_currentFrameFlipIndex] = m_lastFenceValue;
            V( m_commandQueue->Signal( m_fence.Get( ), m_lastFenceValue ) );

        }

#ifdef VA_D3D12_FORCE_IMMEDIATE_SYNC
        SyncGPU( false );
#endif

        //vaGPUTimerManagerDX12::GetInstance().PIXBeginEvent( L"vaRenderDevice::EndAndPresentFrame" );
        vaRenderDevice::EndAndPresentFrame( vsyncInterval );
        //vaGPUTimerManagerDX12::GetInstance().PIXEndEvent( ); // L"vaRenderDevice::EndAndPresentFrame"
    }

//#ifdef VA_USE_PIX3
//    PIXEndEvent( GetCommandQueue( ).Get( ) ); //, PIX_COLOR_INDEX( 1 ), "GPUFrame" );
//#endif
}

void vaRenderDeviceDX12::ExecuteAtBeginFrame( const std::function<void( vaRenderDeviceDX12 & device )> & callback )
{ 
    assert( !m_beginFrameCallbacksDisable );
    if( m_beginFrameCallbacksDisable )
        return;

    std::unique_lock mutexLock(m_beginFrameCallbacksMutex);

    // int flipIndex = m_currentFrameFlipIndex;
    // if( !m_frameStarted )    // if frame isn't started yet, defer to next frame
    //     flipIndex = ( flipIndex + 1 ) % c_BackbufferCount;

    m_beginFrameCallbacks.push_back( callback ); 
}

bool vaRenderDeviceDX12::ExecuteBeginFrameCallbacks( )
{
    VA_TRACE_CPU_SCOPE( BeginFrameCallbacks );

    assert( IsRenderThread() );
    assert( !m_GPUFrameFinishedCallbacksExecuting );    // recursive call to this method is illegal
    assert( !m_beginFrameCallbacksExecuting );          // recursive call to this method is illegal
    m_beginFrameCallbacksExecuting = true;

    bool hadAnyCallbacks = false;

    std::unique_lock mutexLock(m_beginFrameCallbacksMutex);

    auto & callbacks = m_beginFrameCallbacks;
    while( callbacks.size() > 0 ) 
    {
        auto callback = callbacks.back();
        callbacks.pop_back();
        
        // callbacks are allowed to add more callbacks to the list so unlock the mutex!
        mutexLock.unlock();
        callback( *this );
        mutexLock.lock();
        hadAnyCallbacks = true;
    }

    if( hadAnyCallbacks )
        AsFullDX12( *m_mainDeviceContext ).ExecuteCommandList( );

    m_beginFrameCallbacksExecuting = false;
    return hadAnyCallbacks;
}

void vaRenderDeviceDX12::ExecuteAfterCurrentGPUFrameDone( const std::function<void( vaRenderDeviceDX12 & device )> & callback )
{
    assert( m_commandQueue != nullptr );

    std::unique_lock mutexLock(m_GPUFrameFinishedCallbacksMutex);
    m_GPUFrameFinishedCallbacks[m_currentFrameFlipIndex].push_back( callback );
}

void vaRenderDeviceDX12::ExecuteAfterCurrentGPUFrameDone( const std::vector< std::function<void( vaRenderDeviceDX12& device )> >& callbacks )
{
    assert( m_commandQueue != nullptr );

    assert( m_frameStarted );

    std::unique_lock mutexLock( m_GPUFrameFinishedCallbacksMutex );
    for( int i = 0; i < callbacks.size(); i++ )
        m_GPUFrameFinishedCallbacks[m_currentFrameFlipIndex].push_back( callbacks[i] );
}


bool vaRenderDeviceDX12::ExecuteGPUFrameFinishedCallbacks( bool oldestFrameOnly )
{
    VA_TRACE_CPU_SCOPE( EndFrameCallbacks );

    assert( IsRenderThread() );
    assert( !m_beginFrameCallbacksExecuting );          // recursive call to this method is illegal
    assert( !m_GPUFrameFinishedCallbacksExecuting );    // recursive call to this method is illegal
    m_GPUFrameFinishedCallbacksExecuting = true;

    bool hadAnyCallbacks = false;

    std::unique_lock mutexLock( m_GPUFrameFinishedCallbacksMutex );
    for( int i = 0; i < ((oldestFrameOnly)?(1):(c_BackbufferCount)); i++ )
    {
        auto & callbacks = m_GPUFrameFinishedCallbacks[(m_currentFrameFlipIndex+i)%c_BackbufferCount];
        while( callbacks.size() > 0 ) 
        {
            auto callback = callbacks.back();
            callbacks.pop_back();
        
            // callbacks are allowed to add more callbacks to the list so unlock the mutex!
            mutexLock.unlock();
            callback( *this );
            mutexLock.lock();
            hadAnyCallbacks = true;
        }
    }
    m_GPUFrameFinishedCallbacksExecuting = false;
    return hadAnyCallbacks;
}

void vaRenderDeviceDX12::SyncGPUFrame( int age )
{
    if( age == 0 )
    {
        SyncGPU( false );
        return;
    }

    assert( age <= c_BackbufferCount );
    uint64 fenceSyncValue = m_fenceValues[( m_currentFrameFlipIndex - age + c_BackbufferCount ) % c_BackbufferCount];

    uint64 fenceCompletedValue = m_fence->GetCompletedValue( );
    if( fenceCompletedValue < fenceSyncValue )
    {
        VA_TRACE_CPU_SCOPE( CPU_GPU_Sync );
        HRESULT hr;
        V( m_fence->SetEventOnCompletion( fenceSyncValue, m_fenceEvent ) );
        WaitForSingleObjectEx( m_fenceEvent, INFINITE, FALSE );
    }
}

// Wait for pending GPU work to complete.
void vaRenderDeviceDX12::SyncGPU( bool executeAfterFrameDoneCallbacks )
{
    assert( IsRenderThread() );
    HRESULT hr;

    // Schedule a Signal command in the queue.
    m_lastFenceValue++;
    V( m_commandQueue->Signal(m_fence.Get(), m_lastFenceValue) );

    // Wait until the fence has been processed.
    UINT64 completedFenceValue = m_fence->GetCompletedValue( );
    if( completedFenceValue < m_lastFenceValue )
    {
        V( m_fence->SetEventOnCompletion( m_lastFenceValue, m_fenceEvent ) );
        WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
    }

    if( executeAfterFrameDoneCallbacks )
        ExecuteGPUFrameFinishedCallbacks( false );

    // Let the allocator know that we've synced all frames and it can drop all barriers!
    // Might not be the best place for this but oh well.
    for( int i = 0; i < c_BackbufferCount; i++ )
        m_transientDescAllocator.NextFrame( );
}

void vaRenderDeviceDX12::BindDefaultDescriptorHeaps( ID3D12GraphicsCommandList * commandList )
{
    //std::shared_lock lock(m_bindDefaultDescriptorHeapsMutex);   // <- the way this is used right now, the mutex is unneccessary 
    assert( m_frameStarted );

    assert( m_defaultDescriptorHeaps.size() >= (int)D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES );
    ID3D12DescriptorHeap* ppHeaps[(int)D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES+1]; 
    int actualCount = 0;
    for( int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; i++ )
    {
        if( m_defaultDescriptorHeaps[i].GetHeap() == nullptr )
            continue;
        if( (m_defaultDescriptorHeaps[i].GetDesc().Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) == 0 )
            continue;
        ppHeaps[actualCount++] = m_defaultDescriptorHeaps[i].GetHeap().Get();
    }
    //ppHeaps[actualCount++] = m_defaultTransientDescriptorHeaps[m_defaultTransientDescriptorHeapsCurrentIndex][m_currentFrameFlipIndex].GetHeap().Get();
    assert( actualCount <= _countof(ppHeaps) );
    commandList->SetDescriptorHeaps( actualCount, ppHeaps );
}

vaRenderDeviceDX12::DescriptorHeap::~DescriptorHeap( )
{ 
    assert( m_device != nullptr );
    assert( m_device->IsRenderThread() );

    assert( m_allocatedCount - (int)m_freed.size() == m_reservedCapacity );
}

void vaRenderDeviceDX12::DescriptorHeap::Initialize( vaRenderDeviceDX12 & _device, const D3D12_DESCRIPTOR_HEAP_DESC & desc, int reservedCapacity )
{
    assert( _device.IsRenderThread() );
    assert( m_device == nullptr );
    m_device = &_device;

    m_reservedCapacity = reservedCapacity;
    m_allocatedCount = reservedCapacity;
    m_capacity = desc.NumDescriptors;
    m_heapDesc = desc;
    auto device = _device.GetPlatformDevice( ).Get();
 
    HRESULT hr;
    V( device->CreateDescriptorHeap( &desc, IID_PPV_ARGS(&m_heap) ) );

    m_descriptorSize = device->GetDescriptorHandleIncrementSize( desc.Type );

    m_heapCPUStart = m_heap->GetCPUDescriptorHandleForHeapStart();
    m_heapGPUStart = m_heap->GetGPUDescriptorHandleForHeapStart();
}

bool  vaRenderDeviceDX12::DescriptorHeap::Allocate( int & outIndex, D3D12_CPU_DESCRIPTOR_HANDLE & outCPUHandle, D3D12_GPU_DESCRIPTOR_HANDLE & outGPUHandle )
{ 
    assert( m_device != nullptr );
    std::unique_lock mutexLock( m_mutex->Get() );

    if( m_freed.size() == 0 && m_allocatedCount >= m_capacity ) 
    {
        VA_ERROR( "Ran out of DescriptorHeap space (capacity is %d) - consider initializing with a bigger heap type %d or fixing it 'properly' - check comments around vaRenderDevice::SyncAndFlush", m_capacity, m_heapDesc.Type );
        assert( false );
        return false; 
    }

    // do we have one already freed? return that one
    if( m_freed.size() > 0 ) 
    { 
        int ret = m_freed.back(); 
        m_freed.pop_back(); 
        outIndex = ret;
    }
    else // or allocate the new one
    {
        outIndex = m_allocatedCount++; 
    }

    outCPUHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE( m_heap->GetCPUDescriptorHandleForHeapStart(), outIndex, m_descriptorSize );

    //if( (m_heapDesc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0 )
        outGPUHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE( m_heap->GetGPUDescriptorHandleForHeapStart(), outIndex, m_descriptorSize );
    //else
    //    outGPUHandle = D3D12_GPU_DESCRIPTOR_HANDLE{0};

    return true;
}

void vaRenderDeviceDX12::DescriptorHeap::Release( int index )    
{
    assert( m_device != nullptr );
    assert( index >= 0 && index < m_allocatedCount );

    std::unique_lock mutexLock( m_mutex->Get() );

    // no defrag, really dumb way but it works
    m_freed.push_back(index); 
}

vaRenderDeviceDX12::DescriptorHeap * vaRenderDeviceDX12::GetDescriptorHeap( D3D12_DESCRIPTOR_HEAP_TYPE type )
{
    assert( m_device != nullptr );

    if( type < D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || type > D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES || type >= m_defaultDescriptorHeaps.size() )
        { assert( false ); return nullptr; }

    assert( m_defaultDescriptorHeapsInitialized );

    return &m_defaultDescriptorHeaps[(int)type];
}

void vaRenderDeviceDX12::ImGuiCreate( )
{
    assert( IsRenderThread() );
    vaRenderDevice::ImGuiCreate();

#ifdef VA_IMGUI_INTEGRATION_ENABLED
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 1;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        VERIFY_SUCCEEDED( m_device.Get()->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_imgui_SRVDescHeap ) ) );
    }

    ImGui_ImplWin32_Init( GetHWND( ) );
    ImGui_ImplDX12_Init( m_device.Get(), c_BackbufferCount, c_DefaultBackbufferFormatRTV, 
        m_imgui_SRVDescHeap.Get(), m_imgui_SRVDescHeap->GetCPUDescriptorHandleForHeapStart( ), m_imgui_SRVDescHeap->GetGPUDescriptorHandleForHeapStart( ) );
    ImGui_ImplDX12_CreateDeviceObjects( );
#endif
}
void vaRenderDeviceDX12::ImGuiDestroy( )
{
    assert( IsRenderThread() );
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    ImGui_ImplDX12_InvalidateDeviceObjects();
    ImGui_ImplDX12_Shutdown( );
    ImGui_ImplWin32_Shutdown( );
    m_imgui_SRVDescHeap.Reset();
#endif
    vaRenderDevice::ImGuiDestroy();
}

void vaRenderDeviceDX12::ImGuiNewFrame( )
{
    assert( IsRenderThread() );
    assert( !m_imguiFrameStarted ); // forgot to call ImGuiEndFrameAndRender? 
    //assert( ImGuiIsVisible( ) );
    m_imguiFrameStarted = true;

#ifdef VA_IMGUI_INTEGRATION_ENABLED
    ImGui::GetIO( ).DeltaTime = m_lastDeltaTime;

//            // hacky mouse handling, but good for now
//            bool dontTouchMyCursor = vaInputMouseBase::GetCurrent( )->IsCaptured( );

    // Feed inputs to dear imgui, start new frame
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    ImGuizmo::SetRect( 0, 0, ImGui::GetIO( ).DisplaySize.x, ImGui::GetIO( ).DisplaySize.y );
#endif
}

void vaRenderDeviceDX12::ImGuiEndFrameAndRender( const vaRenderOutputs & renderOutputs, vaRenderDeviceContext & renderContext )
{
    assert( &renderContext == GetMainContext() ); renderContext; // at the moment only main context supported

    assert( IsRenderThread() );
    assert( m_imguiFrameStarted ); // forgot to call ImGuiNewFrame? you must not do that!
    //assert( ImGuiIsVisible( ) );

#ifdef VA_IMGUI_INTEGRATION_ENABLED
    ImGui::Render();

    // let's render imgui anyway to allow for using internal imgui draw functionality like vaDebugCanvas::DrawText does
    // if( vaUIManager::GetInstance().IsVisible() )
    {
        {
            VA_TRACE_CPUGPU_SCOPE( ImGuiRender, renderContext );

            // unfortunately this is a limitation with the current DirectX12 implementation, but can be fixed when needed
            assert( renderOutputs.RenderTargets[0] == GetCurrentBackbufferTexture() );

            ID3D12GraphicsCommandList * commandList = AsDX12( *m_mainDeviceContext ).GetCommandList().Get();

            AsDX12(renderContext).BindDefaultStates( );
            AsFullDX12(renderContext).CommitOutputsRaw( vaRenderTypeFlags::Graphics, renderOutputs );
            ID3D12DescriptorHeap * descHeap = m_imgui_SRVDescHeap.Get();
            commandList->SetDescriptorHeaps( 1, &descHeap );
            ImGui_ImplDX12_RenderDrawData( ImGui::GetDrawData(), commandList );
            
            // AsDX12( *m_mainDeviceContext ).Flush( );
            AsDX12( *m_mainDeviceContext ).BindDefaultStates( );
        }

        GetTextureTools().UIDrawImages( *m_mainDeviceContext, renderOutputs );
    }
#endif

    ImGuiEndFrame( );
}

template< typename PSOType, typename MutexType >
static void CleanPSOCache( vaRenderDeviceDX12 & device, MutexType & mtx, vaMemoryBuffer & startKey, std::unordered_map<vaMemoryBuffer, PSOType*, vaPSOKeyDataHasher> & PSOCache )
{
    std::unique_lock mutexLock( mtx );

    const int64 currentFrameIndex     = device.GetCurrentFrameIndex();
    const int itemsToVisit          = 2;

    // !!WARNING!! this must be higher than the c_BackbufferCount - there's no other guarantees that the data isn't going to be in use on the GPU side
    const int unusedAgeThreshold    = 50000; // at 120fps this is 6 minutes - but this is just a placeholder, probably not a valid strategy for cleaning up this cache
    assert( unusedAgeThreshold > vaRenderDevice::c_BackbufferCount );

    if( PSOCache.size() == 0 )
        return;

    // get start iterator
    auto & it = (startKey.GetData() == nullptr)?(PSOCache.end()):(PSOCache.find( startKey ));
    if( it == PSOCache.end() )
        it = PSOCache.begin();

    int clearedCount = 0;

    float remainingStepCount = (float)std::min( itemsToVisit, (int)PSOCache.size() );
    do 
    {
        // std::unique_lock PSOLock( it->second->Mutex( ) );
        if( currentFrameIndex - it->second->GetLastUsedFrame( ) > unusedAgeThreshold )
        {
            auto ptr = it->second;
            it = PSOCache.erase( it );
            // PSOLock.unlock();
            delete ptr;
            remainingStepCount += 0.8f;   // if there was something to erase, just continue erasing but not indefinitely so we don't cause a frame spike
        }
        else
            it++;

        remainingStepCount -= 1.0f;
        if( it == PSOCache.end() && PSOCache.size() > 0 )
            it = PSOCache.begin();
    } while ( remainingStepCount > 0.0f && PSOCache.size() > 0 );

    if( it != PSOCache.end() )
        startKey = it->first;
    else
        startKey.Clear();

    if( clearedCount > 0 )
        VA_WARN( "Cleared %d old PSOs", clearedCount );

}

void vaRenderDeviceDX12::PSOCachesClearAll( )
{
    // clear PSO cache (these hold shader blobs)
    {
        std::unique_lock glock( m_graphicsPSOCacheMutex );
        for( auto& it : m_graphicsPSOCache )
            delete it.second;;
        m_graphicsPSOCache.clear( );
    }
    {
        std::unique_lock plock( m_computePSOCacheMutex );
        for( auto& it : m_computePSOCache )
            delete it.second;
        m_computePSOCache.clear( );
    }
    {
        std::unique_lock plock( m_raytracePSOCacheMutex );
        for( auto& it : m_raytracePSOCache )
            delete it.second;
        m_raytracePSOCache.clear( );
    }

}

void vaRenderDeviceDX12::PSOCachesClearUnusedTick( )
{
    if( m_PSOCachesClearOrder == 0 )
        CleanPSOCache( *this, m_graphicsPSOCacheMutex, m_graphicsPSOCacheCleanupLastKey, m_graphicsPSOCache );
    else if( m_PSOCachesClearOrder == 1 )
        CleanPSOCache( *this, m_computePSOCacheMutex, m_computePSOCacheCleanupLastKey, m_computePSOCache );
    else if( m_PSOCachesClearOrder == 2 )
        CleanPSOCache( *this, m_raytracePSOCacheMutex, m_raytracePSOCacheCleanupLastKey, m_raytracePSOCache );
    m_PSOCachesClearOrder = (m_PSOCachesClearOrder+1)%3;
}

template< typename PSOType, typename PSODescType, int scratchBufferSize, typename MutexType, typename LocalCacheType >
static PSOType * FindOrCreatePipelineStateTemplated( vaRenderDeviceDX12 & device, MutexType & mtx, const PSODescType & psoDesc, std::unordered_map<vaMemoryBuffer, PSOType*, vaPSOKeyDataHasher> & PSOCache, ID3D12RootSignature * rootSignature, LocalCacheType * localCache )
{
    alignas(sizeof(uint64)) byte scratchBuffer[scratchBufferSize];

    //vaMemoryStream scratchStream( scratchBuffer, scratchBufferSize );
    //psoDesc.FillKey( scratchStream );
    int contentsSize = psoDesc.FillKeyFast( scratchBuffer );
    assert( contentsSize <= scratchBufferSize );
    vaMemoryBuffer tempKey( scratchBuffer, contentsSize, vaMemoryBuffer::InitType::View ); // doesn't take the ownership (no allocations)!
    
    PSOType * retPSO = nullptr;

    // before we do anything else, try local cache
    if( localCache != nullptr )
    {
        //VA_TRACE_CPU_SCOPE( LOCAL_CACHE );

        auto localPSO = localCache->Find( tempKey );
        if( localPSO != nullptr  )
            return retPSO = *localPSO;  // early out!
    }

    {
        // first lock with shared (read-only), assuming we'll find the entry (most common case)
        std::shared_lock sharedContainerLock( mtx );

        auto sharedIt = PSOCache.find( tempKey );
        if( sharedIt != PSOCache.end( ) )
        {
            retPSO = sharedIt->second;
        }
        else
        {
#ifdef _DEBUG
            sharedIt = PSOCache.end( ); // just make sure no one is using it after the line below
#endif
            // "upgrade" to unique lock - since there's no actual upgrade possible, we've got to unlock shared, lock unique and search again just in case
            // other thread adds the item we're looking for just between our unlock->lock
            sharedContainerLock.unlock();
            std::unique_lock uniqueContainerLock( mtx );

            // even though we couldn't find it in the first attempt above, another thread might have added it while we were upgrading the lock to unique!
            auto uniqueIt = PSOCache.find( tempKey );
            if( uniqueIt != PSOCache.end() )
            {
                retPSO = uniqueIt->second;
            }
            else
            {
                retPSO = new PSOType( psoDesc );

                memcpy( retPSO->KeyStorage( ), tempKey.GetData(), tempKey.GetSize() );

                // grab the unique lock to the data before adding to container - now nobody can pass sharedPSOLock below until PSO creation is done...
                // std::unique_lock createPSOLock( retPSO->Mutex( ) );

                // (add ourselves into container)
                PSOCache.insert( std::make_pair( std::move(vaMemoryBuffer( retPSO->KeyStorage( ), tempKey.GetSize(), vaMemoryBuffer::InitType::View )), retPSO ) );

                // unlocking before CreatePSO can give us a lot of perf
                uniqueContainerLock.unlock();

                // this is a potentially very lengthy call, and it's blocking everyone else but it happens rarely
                retPSO->CreatePSO( device, rootSignature );

// Not sure if we want the code below; it was added for the case when there's s a problem with PSO descriptor creation, where fixing it doesn't cause the cache key to change
// so it never gets recompiled even if the source problem was corrected. On the other hand, this is probably a serious problem (not just a shader compile error for ex.) 
// which needs proper fixing anyway.
#if 0
                // compile failed? remove from the cache right now. 
                if( retPSO->GetPSO( ) == nullptr )
                {
                	uniqueContainerLock.lock();
                    uniqueIt = PSOCache.find( tempKey );
                    if( uniqueIt != PSOCache.end( ) && uniqueIt->second->GetPSO( ) == nullptr )
                    {
                        // Other threads could have picked it up and used it, so defer the destruction. Doesn't actually need to be deferred
                        // until the GPU fence - just until the end of the frame - but let's just reuse the mechanism.
                        device.ExecuteAfterCurrentGPUFrameDone( [ptrToDelete = uniqueIt->second]( vaRenderDeviceDX12 &  )
                        { 
                            delete ptrToDelete;
                        } );
                        PSOCache.erase( uniqueIt );

                        return nullptr;
                    }
                    else
                    {
                        assert( false ); // not sure if this is valid - debug if happens
                    }
                }
#else
                assert( retPSO->GetPSO( ) != nullptr );
#endif
            }
        }
    }
    assert( retPSO != nullptr );

    retPSO->SetLastUsedFrame( device.GetCurrentFrameIndex( ) );

    if( localCache != nullptr )
        localCache->Insert( vaMemoryBuffer( retPSO->KeyStorage( ), tempKey.GetSize(), vaMemoryBuffer::InitType::View ), retPSO );

    return retPSO;
}

vaGraphicsPSODX12 * vaRenderDeviceDX12::FindOrCreateGraphicsPipelineState( const vaGraphicsPSODescDX12 & psoDesc, LocalGraphicsPSOCacheType * localCache )
{
    //VA_TRACE_CPU_SCOPE( FindOrCreateGraphicsPSO );

    return FindOrCreatePipelineStateTemplated<vaGraphicsPSODX12, vaGraphicsPSODescDX12, vaGraphicsPSODX12::c_keyStorageSize>( *this, m_graphicsPSOCacheMutex, psoDesc, m_graphicsPSOCache, GetDefaultGraphicsRootSignature(), localCache );
}

vaComputePSODX12 * vaRenderDeviceDX12::FindOrCreateComputePipelineState( const vaComputePSODescDX12 & psoDesc, LocalComputePSOCacheType * localCache )
{
    //VA_TRACE_CPU_SCOPE( FindOrCreateComputePSO );

    return FindOrCreatePipelineStateTemplated<vaComputePSODX12, vaComputePSODescDX12, vaComputePSODX12::c_keyStorageSize>( *this, m_computePSOCacheMutex, psoDesc, m_computePSOCache, GetDefaultComputeRootSignature(), localCache );
}

vaRaytracePSODX12 * vaRenderDeviceDX12::FindOrCreateRaytracePipelineState( const vaRaytracePSODescDX12 & psoDesc, LocalRaytracePSOCacheType * localCache )
{
    //VA_TRACE_CPU_SCOPE( FindOrCreateRaytracePSO );

    return FindOrCreatePipelineStateTemplated<vaRaytracePSODX12, vaRaytracePSODescDX12, vaRaytracePSODX12::c_keyStorageSize>( *this, m_raytracePSOCacheMutex, psoDesc, m_raytracePSOCache, GetDefaultComputeRootSignature(), localCache );
}

// void vaRenderDeviceDX12::ReleasePipelineState( shared_ptr<vaComputePSODX12> & pso )
// {
//     assert( IsRenderThread( ) ); // upgrade to thread-safe in the future - warning, see below on std::swap, that needs to be done in a thread-safe manner
// 
//     shared_ptr<vaComputePSODX12>* sptrToDispose = new shared_ptr<vaComputePSODX12>( nullptr );
//     sptrToDispose->swap( pso );
// 
//     if( ( *sptrToDispose ) != nullptr )
//     {
//         // Let the resource be removed when we can guarantee GPU has finished using it
//         ExecuteAfterCurrentGPUFrameDone(
//             [sptrToDispose]( vaRenderDeviceDX12& device )
//         {
//             device;
//             assert( ( *sptrToDispose ) != nullptr );
//             ( *sptrToDispose )->m_safelyReleased = true;   // the only place it's safe to delete it from
//             delete sptrToDispose;
//         } );
//     }
//     else
//         delete sptrToDispose;
// }

void vaRenderDeviceDX12::StaticEnumerateAdapters( std::vector<pair<string, string>> & outAdapters )
{
    outAdapters;
    EnsureDirectXAPILoaded( );

    HRESULT hr = S_OK;

    ComPtr<IDXGIFactory5> dxgiFactory;
    UINT dxgiFactoryFlags = 0;
    hr = s_DynamicCreateDXGIFactory2( dxgiFactoryFlags, IID_PPV_ARGS(&dxgiFactory) );
    {
        if( FAILED( hr ) )
        {
            VA_ERROR( L"Unable to create DXGIFactory; Your Windows 10 probably needs updating" );
            return;
        }
    }

    ComPtr<IDXGIAdapter4> adapter;

    UINT i = 0; 

    IDXGIAdapter1* adapterTemp;
    while( dxgiFactory->EnumAdapters1(i, &adapterTemp) != DXGI_ERROR_NOT_FOUND )
    { 
	    i++; 
        hr = ComPtr<IDXGIAdapter1>(adapterTemp).As(&adapter);
        SAFE_RELEASE( adapterTemp );

        if( SUCCEEDED( hr ) )
        {
            DXGI_ADAPTER_DESC3 desc;
            adapter->GetDesc3( &desc );

            // only hardware devices enumerated
            if( (desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) != 0 )
                continue;

            // check feature level
            if( FAILED( D3D12CreateDevice(adapter.Get(), c_RequiredFeatureLevel, _uuidof(ID3D12Device), nullptr) ) )
                continue;

            outAdapters.push_back( make_pair( StaticGetAPIName(), FormatAdapterID(desc) ) );
        }
    }

    outAdapters.push_back( make_pair( StaticGetAPIName(), "WARP" ) );

//    hr = dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
//    if( !FAILED( hr ) )
//    {
//        if( SUCCEEDED( hr ) )
//        {
//            DXGI_ADAPTER_DESC3 desc;
//            adapter->GetDesc3( &desc );
//
//            // check for feature level should go here
//
//            outAdapters.push_back( make_pair( StaticGetAPIName(), FormatAdapterID(desc) ) );
//        }
//    }

}

// TransientDescriptorAllocator

void vaRenderDeviceDX12::TransientDescriptorAllocator::Initialize( DescriptorHeap * backingHeap, int capacity )
{
    m_backingHeap           = backingHeap;
    m_capacity              = capacity;
    m_backingHeapCPUStart   = backingHeap->GetHeap()->GetCPUDescriptorHandleForHeapStart();
    m_backingHeapGPUStart   = backingHeap->GetHeap()->GetGPUDescriptorHandleForHeapStart();
    m_descriptorSize        = backingHeap->GetDescriptorSize();
    for( int i = 0; i < countof(m_frameBarriers); i++ ) 
        m_frameBarriers[i] = 0;
}

int vaRenderDeviceDX12::TransientDescriptorAllocator::Allocate( int size )
{
    if( size >= m_capacity / 2 )
    {
        assert( false );
        return -1;
    }

    // need to loop around (doing 'm_head + size > m_capacity' would be valid w.r.t. capacity but breaks logic with barriers)
    if( m_head + size >= m_capacity )
    {
        // check if we're skipping any barriers while looping around, and return -1 if we are
        for( int i = m_syncAge; i < countof( m_frameBarriers ); i++ )
            if( m_head < m_frameBarriers[i] || m_frameBarriers[i] == 0 ) 
            {
                 return -1;
            }
        // loop around
        m_head = 0;
    }
    // check if we're going over any barriers and return -1 if we are
    for( int i = m_syncAge; i < countof( m_frameBarriers ); i++ )
        if( m_head < m_frameBarriers[i] && (m_head+size) >= m_frameBarriers[i] )
        {
             return -1;
        }

    int allocatedIndex = m_head;
    m_head += size;
    return allocatedIndex;
}

void vaRenderDeviceDX12::TransientDescriptorAllocator::NextFrame( )
{
    for( int i = countof(m_frameBarriers)-2; i >= 0 ; i-- ) 
        m_frameBarriers[i] = m_frameBarriers[i+1];
    m_frameBarriers[countof(m_frameBarriers)-1] = m_head % m_capacity;
    m_syncAge = 0;
}

int vaRenderDeviceDX12::TransientDescHeapAllocate( int size )
{
    int allocatedIndex = m_transientDescAllocator.Allocate( size );

    bool hadSync = false;
    while( allocatedIndex == -1 )
    {
        AsFullDX12( *m_mainDeviceContext ).Flush( );
        m_transientDescAllocator.SyncAgeIncrement( );
        SyncGPUFrame( c_BackbufferCount - m_transientDescAllocator.SyncAge() );
        hadSync = true;
        allocatedIndex = m_transientDescAllocator.Allocate( size );
    }
    if( hadSync )
        VA_WARN( "Ran out of transient heap space for this frame and having to sync GPU - this should not happen if performance is important (but is fine functionally - for ex, no prob for SS reference)" );

    assert( allocatedIndex != -1 );
    return allocatedIndex;
}
