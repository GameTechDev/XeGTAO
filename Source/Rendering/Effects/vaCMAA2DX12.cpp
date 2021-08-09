///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: Apache-2.0 OR MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Core/vaCoreIncludes.h"

#include "Rendering/vaRenderingIncludes.h"
#include "Rendering/DirectX/vaRenderDeviceContextDX12.h"
#include "Rendering/Shaders/vaSharedTypes.h"
#include "Rendering/DirectX/vaTextureDX12.h"

#include "vaCMAA2.h"

#pragma warning ( disable : 4238 )  //  warning C4238: nonstandard extension used: class rvalue used as lvalue


namespace Vanilla
{
    struct ResourceViewHelperDX12
    {
        const int                           HeapIndex;
        const D3D12_DESCRIPTOR_HEAP_TYPE    Type        = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        D3D12_CPU_DESCRIPTOR_HANDLE         CPUHandle   = { 0 };
        D3D12_GPU_DESCRIPTOR_HANDLE         GPUHandle   = { 0 };
        bool                                Null        = true;

        ResourceViewHelperDX12( int index ) : HeapIndex( index ) { }

        void                                Reset( )    { Null = true; }
        bool                                IsNull( )   { return Null; }

        void                                UpdateHandles( ID3D12DescriptorHeap * heap, int handleSize )
        {
            CPUHandle    = CD3DX12_CPU_DESCRIPTOR_HANDLE( heap->GetCPUDescriptorHandleForHeapStart(), HeapIndex, handleSize );
            GPUHandle    = CD3DX12_GPU_DESCRIPTOR_HANDLE( heap->GetGPUDescriptorHandleForHeapStart(), HeapIndex, handleSize );
        }
    };

    struct InputResourceHelperDX12
    {
        ID3D12Resource *                Source                  = nullptr;
        DXGI_FORMAT                     SRVFormat               = DXGI_FORMAT_UNKNOWN;
        D3D12_RESOURCE_STATES           BeforeState             = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES           AfterState              = D3D12_RESOURCE_STATE_COMMON;
        InputResourceHelperDX12( ) { }
        InputResourceHelperDX12( ID3D12Resource * source ) : Source( source ) { }
    };

    // VA-specific helper initializer for InputResourceHelperDX12
    struct vaInputResourceHelperDX12 : InputResourceHelperDX12
    {
        vaRenderDeviceContextDX12 &     Context;
        shared_ptr<vaTexture>           Texture;

        vaInputResourceHelperDX12( vaRenderDeviceContextDX12 & context, const shared_ptr<vaTexture> & texture, D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState ) : Context(context), Texture(texture), InputResourceHelperDX12( (texture!=nullptr)?(AsDX12(*texture).GetResource()):(nullptr) ) 
        { 
            // just make sure we're really in this state - if not, transition to it for correctness (potential perf issue)
            if( Texture != nullptr )
            {
                SRVFormat = DXGIFormatFromVA( texture->GetSRVFormat() );
                AsDX12(*Texture).TransitionResource( context, beforeState ); 
            }
            BeforeState = beforeState;
            AfterState  = afterState;
        }
        ~vaInputResourceHelperDX12( )
        {
            if( Texture != nullptr )
            {
                AsDX12(*Texture).AdoptResourceState( Context, AfterState ); 
            }

        }
    };

    class vaCMAA2DX12 : public vaCMAA2
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );
    private:

        ////////////////////////////////////////////////////////////////////////////////////
        // SHADERS
        //
        // Main shaders
        vaAutoRMI<vaComputeShader>      m_CSEdgesColor2x2;
        vaAutoRMI<vaComputeShader>      m_CSProcessCandidates;
        vaAutoRMI<vaComputeShader>      m_CSDeferredColorApply2x2;
        //
        // Helper shaders for DispatchIndirect
        vaAutoRMI<vaComputeShader>      m_CSComputeDispatchArgs;
        //
        // Debugging view shader
        vaAutoRMI<vaComputeShader>      m_CSDebugDrawEdges;
        //
        // this is to allow PSO rebuild on shader at-recompile-runtime
        int64                           m_CSEdgesColor2x2ShaderContentsID           = -1;
        int64                           m_CSProcessCandidatesShaderContentsID       = -1;
        int64                           m_CSDeferredColorApply2x2ShaderContentsID   = -1;
        int64                           m_CSComputeDispatchArgsShaderContentsID     = -1;
        int64                           m_CSDebugDrawEdgesShaderContentsID          = -1;
        //
        ////////////////////////////////////////////////////////////////////////////////////

        ////////////////////////////////////////////////////////////////////////////////////
        // GLOBAL SETTINGS
        int                             m_textureResolutionX    = 0;
        int                             m_textureResolutionY    = 0;
        int                             m_textureSampleCount    = 0;
        DXGI_FORMAT                     m_textureSRVFormat      = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT                     m_textureUAVFormat      = DXGI_FORMAT_UNKNOWN;
        //
        bool                            m_extraSharpness        = false;
        int                             m_qualityPreset         = -1;
        //
        int                             m_consecutiveResourceUpdateCounter = 0;         // for debugging
        ////////////////////////////////////////////////////////////////////////////////////

        // previous call's external inputs - used to figure out if we need to re-create dependencies (not using weak ptrs because no way to distinguish between null and expired - I think at least?)
        shared_ptr<vaTexture>           m_externalInOutColor;
        shared_ptr<vaTexture>           m_externalOptionalInLuma; 
        shared_ptr<vaTexture>           m_externalInColorMS;
        shared_ptr<vaTexture>           m_externalInColorMSComplexityMask;

        ////////////////////////////////////////////////////////////////////////////////////
        // 'static' DirectX12 resources
        ComPtr<ID3D12RootSignature>     m_rootSignature;
        ComPtr<ID3D12CommandSignature>  m_commandSignature;
        // 'dynamic' DirectX12 resources
        ComPtr<ID3D12DescriptorHeap>    m_descHeap;
        int                             m_descHeapHandleSize;
        static const int                m_descHeapCapacity      = 12;
        static const int                c_numSRVRootParams      = 4;
        static const int                c_numUAVRootParams      = 8;

        ////////////////////////////////////////////////////////////////////////////////////
        // IN/OUT BUFFER VIEWS
        ResourceViewHelperDX12          m_inoutColorReadonlySRV;
        ResourceViewHelperDX12          m_inoutColorWriteonlyUAV;
        //
        ResourceViewHelperDX12          m_inLumaReadonlySRV;
        //
        ResourceViewHelperDX12          m_inColorMSReadonlySRV;
        ResourceViewHelperDX12          m_inColorMSComplexityMaskReadonlySRV;
        //
        ////////////////////////////////////////////////////////////////////////////////////

        ////////////////////////////////////////////////////////////////////////////////////
        // WORKING BUFFERS
        //
        // This texture stores the edges output by EdgeColor2x2CS
        ComPtr<ID3D12Resource>          m_workingEdgesResource;
        ResourceViewHelperDX12          m_workingEdgesUAV;
        //
        // This buffer stores potential shapes for further processing, filled in EdgesColor2x2CS and read/used by ProcessCandidatesCS; each element is a pixel location encoded as (pixelPos.x << 16) | pixelPos.y
        ComPtr<ID3D12Resource>          m_workingShapeCandidatesResource;
        ResourceViewHelperDX12          m_workingShapeCandidatesUAV;
        //
        // This buffer stores a list of pixel coordinates (locations) that contain one or more anti-aliased color values generated in ProcessCanidatesCS; coordinates are in 2x2 quad locations (instead of simple per-pixel) for memory usage reasons; this is used used by DeferredColorApply2x2CS
        ComPtr<ID3D12Resource>          m_workingDeferredBlendLocationListResource;
        ResourceViewHelperDX12          m_workingDeferredBlendLocationListUAV;
        //
        // This buffer contains per-location linked lists with the actual anti-aliased color values.
        ComPtr<ID3D12Resource>          m_workingDeferredBlendItemListResource;
        ResourceViewHelperDX12          m_workingDeferredBlendItemListUAV;
        //
        // This buffer contains per-location linked list heads (pointing to 'workingDeferredBlendItemList') (to add to confusion, it's all in 2x2-sized chunks to reduce memory usage)
        ComPtr<ID3D12Resource>          m_workingDeferredBlendItemListHeadsResource;
        ResourceViewHelperDX12          m_workingDeferredBlendItemListHeadsUAV;
        //
        // Global counters & info for setting up DispatchIndirect
        ComPtr<ID3D12Resource>          m_workingControlBufferResource;
        ResourceViewHelperDX12          m_workingControlBufferUAV;
        // DispatchIndirect/ExecuteIndirect buffer
        ComPtr<ID3D12Resource>          g_workingExecuteIndirectBufferResource;
        ResourceViewHelperDX12          g_workingExecuteIndirectBufferUAV;
        //
        bool                            m_firstRun = false;
        //
        ////////////////////////////////////////////////////////////////////////////////////

        ////////////////////////////////////////////////////////////////////////////////////
        // DX12-specific 
        // 
        ComPtr<ID3D12PipelineState>     m_PSOEdgesColorPass;
        ComPtr<ID3D12PipelineState>     m_PSOProcessCandidatesPass;
        ComPtr<ID3D12PipelineState>     m_PSODeferredColorApplyPass;
        ComPtr<ID3D12PipelineState>     m_PSOComputeDispatchArgsPass;
        ComPtr<ID3D12PipelineState>     m_PSODebugDrawEdgesPass;
        ////////////////////////////////////////////////////////////////////////////////////


    protected:
        explicit vaCMAA2DX12( const vaRenderingModuleParams & params );
        ~vaCMAA2DX12( );

    private:
        virtual vaDrawResultFlags       Draw( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & inoutColor, const shared_ptr<vaTexture> & optionalInLuma ) override;
        virtual vaDrawResultFlags       DrawMS( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & inoutColor, const shared_ptr<vaTexture> & inColorMS, const shared_ptr<vaTexture> & inColorMSComplexityMask ) override;
        virtual void                    CleanupTemporaryResources( ) override;

    private:
        bool                            UpdateResources( ID3D12Device * deviceDX12, const InputResourceHelperDX12 & inOutColor, const InputResourceHelperDX12 & optionalInLuma, const InputResourceHelperDX12 & inColorMS, const InputResourceHelperDX12 & inColorMSComplexityMask );
        void                            UpdateInputViewDescriptors( ID3D12Device * deviceDX12, const InputResourceHelperDX12 & inOutColor, const InputResourceHelperDX12 & optionalInLuma, const InputResourceHelperDX12 & inColorMS, const InputResourceHelperDX12 & inColorMSComplexityMask );
        bool                            UpdatePSOs( );  // Framework-specific shader handling to enable recompilation at runtime 
        void                            Reset( );

    private:
        vaDrawResultFlags               Execute( vaRenderDeviceContext & renderContext, ID3D12GraphicsCommandList* commandList, const InputResourceHelperDX12 & inOutColor, const InputResourceHelperDX12 & optionalInLuma, const InputResourceHelperDX12 & inColorMS, const InputResourceHelperDX12 & inColorMSComplexityMask );

    private:
        // various helpers
        void                            CreateShaderResourceView( ID3D12Device * deviceDX12, ResourceViewHelperDX12 & outResView, ID3D12Resource * resource, const D3D12_SHADER_RESOURCE_VIEW_DESC & desc );
        void                            CreateUnorderedAccessView( ID3D12Device * deviceDX12, ResourceViewHelperDX12 & outResView, ID3D12Resource * resource, ID3D12Resource * counterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC & desc );
        void                            CreateTexture2DAndViews( ID3D12Device * deviceDX12, DXGI_FORMAT format, int width, int height, ComPtr<ID3D12Resource> & outResource, ResourceViewHelperDX12 * outSRV, ResourceViewHelperDX12 * outUAV, bool allowShaderAtomics );
        void                            CreateBufferAndViews( ID3D12Device * deviceDX12, const D3D12_RESOURCE_DESC & desc, ComPtr<ID3D12Resource> & outResource, ResourceViewHelperDX12 * outSRV, ResourceViewHelperDX12 * outUAV, uint structByteStride, bool allowShaderAtomics, bool rawView );
    };

}

using namespace Vanilla;

static const bool c_useTypedUAVStores = false;


vaCMAA2DX12::vaCMAA2DX12( const vaRenderingModuleParams & params ) : vaCMAA2( params ), 
        m_CSEdgesColor2x2( params.RenderDevice ),
        m_CSProcessCandidates( params.RenderDevice ),
        m_CSDeferredColorApply2x2( params.RenderDevice ),
        m_CSComputeDispatchArgs( params.RenderDevice ),
        m_CSDebugDrawEdges( params.RenderDevice ),

        // descriptor indices are pre-assigned and fixed
        m_inoutColorReadonlySRV                 ( 0 ),
        m_inColorMSComplexityMaskReadonlySRV    ( 1 ),
        m_inColorMSReadonlySRV                  ( 2 ),
        m_inLumaReadonlySRV                     ( 3 ),
        m_inoutColorWriteonlyUAV                ( c_numSRVRootParams+0 ),
        m_workingEdgesUAV                       ( c_numSRVRootParams+1 ),
        m_workingShapeCandidatesUAV             ( c_numSRVRootParams+2 ),
        m_workingDeferredBlendLocationListUAV   ( c_numSRVRootParams+3 ),
        m_workingDeferredBlendItemListUAV       ( c_numSRVRootParams+4 ),
        m_workingDeferredBlendItemListHeadsUAV  ( c_numSRVRootParams+5 ),
        m_workingControlBufferUAV               ( c_numSRVRootParams+6 ),
        g_workingExecuteIndirectBufferUAV       ( c_numSRVRootParams+7 )
{
    params; // unreferenced

    Reset( );

    HRESULT hr;

    auto const & deviceDX12 = AsDX12( params.RenderDevice ).GetPlatformDevice();

    // root signature
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

        // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
        if( FAILED( deviceDX12->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)) ) )
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

        CD3DX12_ROOT_PARAMETER1 rootParameters[1];
        CD3DX12_DESCRIPTOR_RANGE1 rootRanges[2];
        //c_numSRVRootParams
        
        rootRanges[0].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, c_numSRVRootParams, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND );
        rootRanges[1].Init( D3D12_DESCRIPTOR_RANGE_TYPE_UAV, c_numUAVRootParams, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND );

        rootParameters[0].InitAsDescriptorTable( _countof(rootRanges), rootRanges, D3D12_SHADER_VISIBILITY_ALL );

        D3D12_STATIC_SAMPLER_DESC defaultSamplers[1];
        defaultSamplers[0] = CD3DX12_STATIC_SAMPLER_DESC( 0, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP );

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, _countof(defaultSamplers), defaultSamplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        if( FAILED( D3DX12SerializeVersionedRootSignature( &rootSignatureDesc, featureData.HighestVersion, &signature, &error ) ) )
        {
            wstring errorMsg = vaStringTools::SimpleWiden( (char*)error->GetBufferPointer( ) );
            VA_ERROR( L"Error serializing versioned root signature: \n %s", errorMsg.c_str() );
        }
        V( deviceDX12->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)) );
        V( m_rootSignature->SetName( L"CMAA2RootSignature" ) );
    }

    // Create the command signature used for indirect drawing.
    {
        // Each command consists of a CBV update and a DrawInstanced call.
        D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[1] = {};
        argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

        D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
        commandSignatureDesc.pArgumentDescs     = argumentDescs;
        commandSignatureDesc.NumArgumentDescs   = _countof(argumentDescs);
        commandSignatureDesc.ByteStride         = sizeof(D3D12_DISPATCH_ARGUMENTS);
        commandSignatureDesc.NodeMask           = 0;

        V( deviceDX12->CreateCommandSignature(&commandSignatureDesc, /*m_rootSignature.Get()*/nullptr, IID_PPV_ARGS(&m_commandSignature)) );
        m_commandSignature->SetName( L"CMAA2CommandSignature" );
    }
}

vaCMAA2DX12::~vaCMAA2DX12( )
{
    CleanupTemporaryResources();

    AsDX12( GetRenderDevice() ).SafeReleaseAfterCurrentGPUFrameDone( m_rootSignature, false );  // 'false' because DirectX will actually reuse an existing root signature object if it finds an identical one so ref count might not be 1
    AsDX12( GetRenderDevice() ).SafeReleaseAfterCurrentGPUFrameDone( m_commandSignature );
}

void vaCMAA2DX12::Reset( )
{
    m_textureResolutionX            = 0;
    m_textureResolutionY            = 0;
    m_textureSampleCount            = 0;
    m_textureSRVFormat              = DXGI_FORMAT_UNKNOWN;
}

// helper functions

void vaCMAA2DX12::CreateShaderResourceView( ID3D12Device * deviceDX12, ResourceViewHelperDX12 & outResView, ID3D12Resource * resource, const D3D12_SHADER_RESOURCE_VIEW_DESC & desc )
{
    assert( outResView.IsNull() );
    assert( m_descHeap != nullptr );
    assert( outResView.HeapIndex >= 0 && outResView.HeapIndex < m_descHeapCapacity );
    outResView.Null = false;

    deviceDX12->CreateShaderResourceView( resource, &desc, outResView.CPUHandle );
}

void vaCMAA2DX12::CreateUnorderedAccessView( ID3D12Device * deviceDX12, ResourceViewHelperDX12 & outResView, ID3D12Resource * resource, ID3D12Resource * counterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC & desc )
{
    assert( outResView.IsNull() );
    assert( m_descHeap != nullptr );
    assert( outResView.HeapIndex >= 0 && outResView.HeapIndex < m_descHeapCapacity );
    outResView.Null = false;

    deviceDX12->CreateUnorderedAccessView( resource, counterResource, &desc, outResView.CPUHandle );
}

static bool FillShaderResourceViewDesc( D3D12_SHADER_RESOURCE_VIEW_DESC & outDesc, ID3D12Resource * resource, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, int mipSliceMin = 0, int mipSliceCount = -1, int arraySliceMin = 0, int arraySliceCount = -1 )
{
    assert( mipSliceMin >= 0 );
    assert( arraySliceMin >= 0 );
    assert( arraySliceCount >= -1 );    // -1 means all

    outDesc = { };

    D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();

    outDesc.Format                  = (format == DXGI_FORMAT_UNKNOWN)?(resourceDesc.Format):(format);
    outDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    if( resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D )
    {
        if( mipSliceCount == -1 )
            mipSliceCount = resourceDesc.MipLevels-mipSliceMin;
        if( arraySliceCount == -1 )
            arraySliceCount = resourceDesc.DepthOrArraySize-arraySliceMin;

        assert( mipSliceMin >= 0 && (UINT)mipSliceMin < resourceDesc.MipLevels );
        assert( mipSliceMin+mipSliceCount > 0 && (UINT)mipSliceMin+mipSliceCount <= resourceDesc.MipLevels );
        assert( arraySliceMin >= 0 && (UINT)arraySliceMin < resourceDesc.DepthOrArraySize );
        assert( arraySliceMin+arraySliceCount > 0 && (UINT)arraySliceMin+arraySliceCount <= resourceDesc.DepthOrArraySize );

        if( resourceDesc.SampleDesc.Count > 1 )
            outDesc.ViewDimension = ( resourceDesc.DepthOrArraySize == 1 ) ? ( D3D12_SRV_DIMENSION_TEXTURE2DMS ) : ( D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY );
        else
            outDesc.ViewDimension = ( resourceDesc.DepthOrArraySize == 1 ) ? ( D3D12_SRV_DIMENSION_TEXTURE2D ) : ( D3D12_SRV_DIMENSION_TEXTURE2DARRAY );

        if( outDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D )
        {
            outDesc.Texture2D.MostDetailedMip       = mipSliceMin;
            outDesc.Texture2D.MipLevels             = mipSliceCount;
            outDesc.Texture2D.PlaneSlice            = 0;
            outDesc.Texture2D.ResourceMinLODClamp   = 0.0f;
            assert( arraySliceMin == 0 );
            assert( arraySliceCount == 1 );
        }
        else if( outDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2DARRAY )
        {
            outDesc.Texture2DArray.MostDetailedMip      = mipSliceMin;
            outDesc.Texture2DArray.MipLevels            = mipSliceCount;
            outDesc.Texture2DArray.FirstArraySlice      = arraySliceMin;
            outDesc.Texture2DArray.ArraySize            = arraySliceCount;
            outDesc.Texture2DArray.PlaneSlice           = 0;
            outDesc.Texture2DArray.ResourceMinLODClamp  = 0.0f;
        }
        else if( outDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2DMS )
        {
            outDesc.Texture2DMS.UnusedField_NothingToDefine = 42;
            assert( mipSliceMin == 0 );
            assert( mipSliceCount == 1 );
            assert( arraySliceMin == 0 );
            assert( arraySliceCount == 1 );
        }
        else if( outDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY )
        {
            assert( mipSliceMin == 0 );
            assert( arraySliceCount == 1 );
            outDesc.Texture2DMSArray.FirstArraySlice    = arraySliceMin;
            outDesc.Texture2DMSArray.ArraySize          = arraySliceCount;
        }
        else { assert( false ); }
        return true;
    }
    else
    {
        assert( false ); // resource not recognized; additional code might be needed above
        return false;
    }
}

static bool FillUnorderedAccessViewDesc( D3D12_UNORDERED_ACCESS_VIEW_DESC & outDesc, ID3D12Resource * resource, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, int mipSliceMin = 0, int arraySliceMin = 0, int arraySliceCount = -1 )
{
    assert( mipSliceMin >= 0 );
    assert( arraySliceMin >= 0 );
    assert( arraySliceCount >= -1 );    // -1 means all

    D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();
    outDesc.Format  = (format == DXGI_FORMAT_UNKNOWN)?(resourceDesc.Format):(format);
        
    if( resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D )
    {
        if( arraySliceCount == -1 )
            arraySliceCount = resourceDesc.DepthOrArraySize - arraySliceMin;

        assert( mipSliceMin >= 0 && (UINT)mipSliceMin < resourceDesc.MipLevels );
        assert( arraySliceMin >= 0 && (UINT)arraySliceMin < resourceDesc.DepthOrArraySize );
        assert( arraySliceMin + arraySliceCount > 0 && (UINT)arraySliceMin + arraySliceCount <= resourceDesc.DepthOrArraySize );

        outDesc.ViewDimension = ( resourceDesc.DepthOrArraySize == 1 ) ? ( D3D12_UAV_DIMENSION_TEXTURE2D ) : ( D3D12_UAV_DIMENSION_TEXTURE2DARRAY );

        if( outDesc.ViewDimension == D3D12_UAV_DIMENSION_TEXTURE2D )
        {
            outDesc.Texture2D.MipSlice              = mipSliceMin;
            outDesc.Texture2D.PlaneSlice            = 0;
            assert( arraySliceMin == 0 );
        }
        else if( outDesc.ViewDimension == D3D12_UAV_DIMENSION_TEXTURE2DARRAY )
        {
            outDesc.Texture2DArray.MipSlice         = mipSliceMin;
            outDesc.Texture2DArray.FirstArraySlice  = arraySliceMin;
            outDesc.Texture2DArray.ArraySize        = arraySliceCount;
            outDesc.Texture2DArray.PlaneSlice       = 0;
        }
        else { assert( false ); }
        return true;
    } 

    assert( false ); // resource not recognized; additional code might be needed above
    return false;
}

void vaCMAA2DX12::CreateTexture2DAndViews( ID3D12Device * deviceDX12, DXGI_FORMAT format, int width, int height, ComPtr<ID3D12Resource> & outResource, ResourceViewHelperDX12 * outSRV, ResourceViewHelperDX12 * outUAV, bool allowShaderAtomics )
{
    // Describe and create a Texture2D.
    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.MipLevels           = 1;
    textureDesc.Format              = format;
    textureDesc.Width               = width;
    textureDesc.Height              = height;
    textureDesc.Flags               = (outUAV!=nullptr)?(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS):(D3D12_RESOURCE_FLAG_NONE);
    textureDesc.DepthOrArraySize    = 1;
    textureDesc.SampleDesc.Count    = 1;
    textureDesc.SampleDesc.Quality  = 0;
    textureDesc.Dimension           = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    D3D12_HEAP_TYPE heapType        = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_HEAP_FLAGS heapFlags      = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES; allowShaderAtomics; // | ((allowShaderAtomics)?(D3D12_HEAP_FLAG_ALLOW_SHADER_ATOMICS):(D3D12_HEAP_FLAG_NONE));
    CD3DX12_HEAP_PROPERTIES heapProps(heapType);

    HRESULT hr;
    V( deviceDX12->CreateCommittedResource(
        &heapProps,
        heapFlags,
        &textureDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, //D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&outResource) ) );
    outResource->SetName( L"CMAA2WorkingTexture" );
    
    if( outSRV != nullptr )
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
        FillShaderResourceViewDesc( srvDesc, outResource.Get() );
        CreateShaderResourceView( deviceDX12, *outSRV, outResource.Get(), srvDesc );
    }
    if( outUAV != nullptr )
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;
        FillUnorderedAccessViewDesc( uavDesc, outResource.Get() );
        CreateUnorderedAccessView( deviceDX12, *outUAV, outResource.Get(), nullptr, uavDesc );
    }
}

                                                                           
void vaCMAA2DX12::CreateBufferAndViews( ID3D12Device * deviceDX12, const D3D12_RESOURCE_DESC & desc, ComPtr<ID3D12Resource> & outResource, ResourceViewHelperDX12 * outSRV, ResourceViewHelperDX12 * outUAV, uint structByteStride, bool allowShaderAtomics, bool rawView )
{
    D3D12_HEAP_TYPE heapType        = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_HEAP_FLAGS heapFlags      = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES; allowShaderAtomics; // | ((allowShaderAtomics)?(D3D12_HEAP_FLAG_ALLOW_SHADER_ATOMICS):(D3D12_HEAP_FLAG_NONE));
    CD3DX12_HEAP_PROPERTIES heapProps(heapType);

    HRESULT hr;
    V( deviceDX12->CreateCommittedResource(
        &heapProps,
        heapFlags,
        &desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,  //D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&outResource) ) );
    outResource->SetName( L"CMAA2WorkingBuffer" );
    
    if( outSRV != nullptr )
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
        srvDesc.Format                      = (rawView)?(DXGI_FORMAT_R32_TYPELESS):(desc.Format);
        srvDesc.Shader4ComponentMapping     = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension               = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement         = 0;
        srvDesc.Buffer.NumElements          = (UINT)(desc.Width / structByteStride);
        srvDesc.Buffer.StructureByteStride  = (rawView)?(0):(structByteStride);
        srvDesc.Buffer.Flags                = (rawView)?(D3D12_BUFFER_SRV_FLAG_RAW):(D3D12_BUFFER_SRV_FLAG_NONE);
        CreateShaderResourceView( deviceDX12, *outSRV, outResource.Get(), srvDesc );
    }
    if( outUAV != nullptr )
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;
        uavDesc.Format                      = (rawView)?(DXGI_FORMAT_R32_TYPELESS):(desc.Format);
        uavDesc.ViewDimension               = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement         = 0;
        uavDesc.Buffer.NumElements          = (UINT)(desc.Width / structByteStride);
        uavDesc.Buffer.StructureByteStride  = (rawView)?(0):(structByteStride);
        uavDesc.Buffer.CounterOffsetInBytes = 0;
        uavDesc.Buffer.Flags                = (rawView)?(D3D12_BUFFER_UAV_FLAG_RAW):(D3D12_BUFFER_UAV_FLAG_NONE);
        CreateUnorderedAccessView( deviceDX12, *outUAV, outResource.Get(), nullptr, uavDesc );
    }
}

void vaCMAA2DX12::CleanupTemporaryResources( )
{
    // Replace with your version of deferred safe release or a sync point
    AsDX12( GetRenderDevice() ).SafeReleaseAfterCurrentGPUFrameDone( m_descHeap ); m_descHeapHandleSize = 0;
    AsDX12( GetRenderDevice() ).SafeReleaseAfterCurrentGPUFrameDone( m_workingEdgesResource );
    AsDX12( GetRenderDevice() ).SafeReleaseAfterCurrentGPUFrameDone( m_workingShapeCandidatesResource );
    AsDX12( GetRenderDevice() ).SafeReleaseAfterCurrentGPUFrameDone( m_workingDeferredBlendLocationListResource );
    AsDX12( GetRenderDevice() ).SafeReleaseAfterCurrentGPUFrameDone( m_workingDeferredBlendItemListResource );
    AsDX12( GetRenderDevice() ).SafeReleaseAfterCurrentGPUFrameDone( m_workingDeferredBlendItemListHeadsResource );
    AsDX12( GetRenderDevice() ).SafeReleaseAfterCurrentGPUFrameDone( m_workingControlBufferResource );
    AsDX12( GetRenderDevice() ).SafeReleaseAfterCurrentGPUFrameDone( g_workingExecuteIndirectBufferResource );
    AsDX12( GetRenderDevice() ).SafeReleaseAfterCurrentGPUFrameDone( m_PSOEdgesColorPass );
    AsDX12( GetRenderDevice() ).SafeReleaseAfterCurrentGPUFrameDone( m_PSOProcessCandidatesPass );
    AsDX12( GetRenderDevice() ).SafeReleaseAfterCurrentGPUFrameDone( m_PSODeferredColorApplyPass );
    AsDX12( GetRenderDevice() ).SafeReleaseAfterCurrentGPUFrameDone( m_PSOComputeDispatchArgsPass );
    AsDX12( GetRenderDevice() ).SafeReleaseAfterCurrentGPUFrameDone( m_PSODebugDrawEdgesPass );

    m_workingShapeCandidatesUAV.Reset();
    m_workingDeferredBlendLocationListUAV.Reset();
    m_workingDeferredBlendItemListUAV.Reset();
    m_workingControlBufferUAV.Reset();
    g_workingExecuteIndirectBufferUAV.Reset();
    m_inoutColorReadonlySRV.Reset();
    m_inoutColorWriteonlyUAV.Reset();
    m_inLumaReadonlySRV.Reset();
    m_inColorMSReadonlySRV.Reset();
    m_inColorMSComplexityMaskReadonlySRV.Reset();
    m_workingEdgesUAV.Reset();
    m_workingDeferredBlendItemListHeadsUAV.Reset();

    Reset();
}

static bool CheckUAVTypedStoreFormatSupport( ID3D12Device* device, DXGI_FORMAT format )
{
    D3D12_FEATURE_DATA_FORMAT_SUPPORT   capsFormatSupport   = { format };
    //D3D11_FEATURE_DATA_FORMAT_SUPPORT2  capsFormatSupport2  = { format };
    HRESULT hr;
    hr = device->CheckFeatureSupport( D3D12_FEATURE_FORMAT_SUPPORT, &capsFormatSupport, sizeof( capsFormatSupport ) );
    assert( SUCCEEDED( hr ) );
    //hr = device->CheckFeatureSupport( D3D12_FEATURE_FORMAT_SUPPORT2, &capsFormatSupport2, sizeof( capsFormatSupport2 ) );
    //assert( SUCCEEDED( hr ) );

    bool typed_unordered_access_view    = (capsFormatSupport.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) != 0;     // Format can be used for an unordered access view.

    bool uav_typed_store                = (capsFormatSupport.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0;            // Format supports a typed store.

    return typed_unordered_access_view && uav_typed_store;
}

static bool IsFloat( DXGI_FORMAT val )
{
    switch( val )
    {
    case DXGI_FORMAT_R32G32B32A32_FLOAT      : return true;
    case DXGI_FORMAT_R32G32B32_FLOAT         : return true;
    case DXGI_FORMAT_R16G16B16A16_FLOAT      : return true;
    case DXGI_FORMAT_R32G32_FLOAT            : return true;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT    : return true;
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: return true;
    case DXGI_FORMAT_R11G11B10_FLOAT         : return true;
    case DXGI_FORMAT_R16G16_FLOAT            : return true;
    case DXGI_FORMAT_D32_FLOAT               : return true;
    case DXGI_FORMAT_R32_FLOAT               : return true;
    case DXGI_FORMAT_R16_FLOAT               : return true;
    default: return false; break;
    }
}

static bool IsSRGB( DXGI_FORMAT val )
{
    switch( val )
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:        return true;
    case DXGI_FORMAT_BC1_UNORM_SRGB:             return true;
    case DXGI_FORMAT_BC2_UNORM_SRGB:             return true;
    case DXGI_FORMAT_BC3_UNORM_SRGB:             return true;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:        return true;
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:        return true;
    case DXGI_FORMAT_BC7_UNORM_SRGB:             return true;
    default: return false; break;
    }
}

static DXGI_FORMAT StripSRGB( DXGI_FORMAT val )
{
    switch( val )
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_BC1_UNORM_SRGB:             return DXGI_FORMAT_BC1_UNORM;
    case DXGI_FORMAT_BC2_UNORM_SRGB:             return DXGI_FORMAT_BC2_UNORM;
    case DXGI_FORMAT_BC3_UNORM_SRGB:             return DXGI_FORMAT_BC3_UNORM;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:        return DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_BC7_UNORM_SRGB:             return DXGI_FORMAT_BC7_UNORM;
    default: return val;
    }
}

bool vaCMAA2DX12::UpdateResources( ID3D12Device * deviceDX12, const InputResourceHelperDX12 & inOutColor, const InputResourceHelperDX12 & optionalInLuma, const InputResourceHelperDX12 & inColorMS, const InputResourceHelperDX12 & inColorMSComplexityMask )
{
    assert( inOutColor.Source != nullptr );

    D3D12_RESOURCE_DESC inOutColorDesc  = inOutColor.Source->GetDesc();

    D3D12_RESOURCE_DESC inColorMSDesc   = {}; 
    if( inColorMS.Source != nullptr ) 
    {
        inColorMSDesc = inColorMS.Source->GetDesc();
        assert( inOutColorDesc.Width    == inColorMSDesc.Width );
        assert( inOutColorDesc.Height   == inColorMSDesc.Height );
    }
    else
    {
        inColorMSDesc.DepthOrArraySize = 1;
    }

    // all is fine, no need to update anything
    if(    m_qualityPreset                     == m_settings.QualityPreset
        && m_extraSharpness                    == m_settings.ExtraSharpness
        && m_textureResolutionX                == (int)inOutColorDesc.Width
        && m_textureResolutionY                == (int)inOutColorDesc.Height
        && m_textureSampleCount                == (int)inColorMSDesc.DepthOrArraySize
        && m_textureSRVFormat                  == inOutColor.SRVFormat )
    {
        m_consecutiveResourceUpdateCounter = 0;
        return true;
    }

    m_consecutiveResourceUpdateCounter++;
    
    // it appears the resources keep being updated for each call - this is probably a bug and should be fixed
    assert( m_consecutiveResourceUpdateCounter < 16 );

    CleanupTemporaryResources();

    m_qualityPreset                     = m_settings.QualityPreset;
    m_extraSharpness                    = m_settings.ExtraSharpness;
    m_textureResolutionX                = (int)inOutColorDesc.Width;
    m_textureResolutionY                = (int)inOutColorDesc.Height;
    m_textureSampleCount                = (int)inColorMSDesc.DepthOrArraySize;
    m_textureSRVFormat                  = inOutColor.SRVFormat;
    assert( (inColorMS.Source == nullptr) == (m_textureSampleCount == 1) );

    HRESULT hr;

    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc;
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        heapDesc.NumDescriptors = m_descHeapCapacity;
        heapDesc.NodeMask       = 0;

        V( deviceDX12->CreateDescriptorHeap( &heapDesc, IID_PPV_ARGS(&m_descHeap) ) );
        m_descHeapHandleSize = deviceDX12->GetDescriptorHandleIncrementSize( heapDesc.Type );
        m_inoutColorReadonlySRV.UpdateHandles( m_descHeap.Get(), m_descHeapHandleSize );
        m_inColorMSComplexityMaskReadonlySRV.UpdateHandles( m_descHeap.Get(), m_descHeapHandleSize );
        m_inColorMSReadonlySRV.UpdateHandles( m_descHeap.Get(), m_descHeapHandleSize );
        m_inLumaReadonlySRV.UpdateHandles( m_descHeap.Get(), m_descHeapHandleSize );
        m_inoutColorWriteonlyUAV.UpdateHandles( m_descHeap.Get(), m_descHeapHandleSize );
        m_workingEdgesUAV.UpdateHandles( m_descHeap.Get(), m_descHeapHandleSize );
        m_workingShapeCandidatesUAV.UpdateHandles( m_descHeap.Get(), m_descHeapHandleSize );
        m_workingDeferredBlendLocationListUAV.UpdateHandles( m_descHeap.Get(), m_descHeapHandleSize );
        m_workingDeferredBlendItemListUAV.UpdateHandles( m_descHeap.Get(), m_descHeapHandleSize );
        m_workingDeferredBlendItemListHeadsUAV.UpdateHandles( m_descHeap.Get(), m_descHeapHandleSize );
        m_workingControlBufferUAV.UpdateHandles( m_descHeap.Get(), m_descHeapHandleSize );
        g_workingExecuteIndirectBufferUAV.UpdateHandles( m_descHeap.Get(), m_descHeapHandleSize );
    }

    if( inColorMS.Source != nullptr )
    {
        assert( inColorMS.SRVFormat != DXGI_FORMAT_UNKNOWN );
        assert( optionalInLuma.Source == nullptr );
    }

    if( optionalInLuma.Source != nullptr )
    {
        assert( inColorMS.Source == nullptr );
    }

    assert( inOutColor.SRVFormat != DXGI_FORMAT_UNKNOWN );

#if !defined(CMAA2_PACK_SINGLE_SAMPLE_EDGE_TO_HALF_WIDTH) || !defined(CMAA2_CS_INPUT_KERNEL_SIZE_X) || !defined(CMAA2_CS_INPUT_KERNEL_SIZE_Y)
#error Forgot to include CMAA2.hlsl?
#endif        

    std::vector< pair< string, string > > shaderMacros;

    shaderMacros.push_back( { "CMAA2_STATIC_QUALITY_PRESET", vaStringTools::Format("%d", m_qualityPreset) } );
    shaderMacros.push_back( { "CMAA2_EXTRA_SHARPNESS", vaStringTools::Format("%d", (m_extraSharpness)?(1):(0) ) } );

    if( m_textureSampleCount != 1 )
        shaderMacros.push_back( { "CMAA_MSAA_SAMPLE_COUNT", vaStringTools::Format("%d", m_textureSampleCount) } );

    // support for various color format combinations
    {
        DXGI_FORMAT srvFormat = inOutColor.SRVFormat;
        DXGI_FORMAT srvFormatStrippedSRGB = StripSRGB( inOutColor.SRVFormat );

        // Assume we don't support typed UAV store-s for our combination of inputs/outputs - reset if we do
        bool convertToSRGBOnOutput  = IsSRGB( inOutColor.SRVFormat );

        bool hdrFormat = IsFloat( inOutColor.SRVFormat );
        bool uavStoreTyped = false;
        bool uavStoreTypesUnormFloat = false;

        // if we support direct writes to this format - excellent, just create an UAV on it and Bob's your uncle
        if( CheckUAVTypedStoreFormatSupport( deviceDX12, srvFormat ) )
        {
            m_textureUAVFormat      = srvFormat;
            convertToSRGBOnOutput   = false;      // no conversion will be needed as the GPU supports direct typed UAV store
            uavStoreTyped           = true;
            uavStoreTypesUnormFloat = !IsFloat( inOutColor.SRVFormat ); // "unorm float" semantic needed if not float
        }
        // maybe just sRGB UAV store is not supported?
        else if( CheckUAVTypedStoreFormatSupport( deviceDX12, srvFormatStrippedSRGB ) )
        {
            m_textureUAVFormat      = srvFormatStrippedSRGB;
            uavStoreTyped           = true;
            uavStoreTypesUnormFloat = !IsFloat( inOutColor.SRVFormat ); // "unorm float" semantic needed if not float
        }
        // ok we have to encode manually
        else
        {
            m_textureUAVFormat      = DXGI_FORMAT_R32_UINT;

            // the need for pre-store sRGB conversion already accounted for above by 'convertToSRGBOnOutput'
            switch( srvFormatStrippedSRGB )
            {
            case DXGI_FORMAT_R8G8B8A8_UNORM:    shaderMacros.push_back( std::pair<std::string, std::string>( "CMAA2_UAV_STORE_UNTYPED_FORMAT", "1" ) );     break;
            case DXGI_FORMAT_R10G10B10A2_UNORM: shaderMacros.push_back( std::pair<std::string, std::string>( "CMAA2_UAV_STORE_UNTYPED_FORMAT", "2" ) );     break;
            default: 
                assert( false ); // add support
                CleanupTemporaryResources();
                return false;
            }
        }

        // force manual conversion to sRGB before write
        shaderMacros.push_back( std::pair<std::string, std::string>( "CMAA2_UAV_STORE_TYPED", ( uavStoreTyped ) ? ( "1" ) : ( "0" ) ) );
        shaderMacros.push_back( std::pair<std::string, std::string>( "CMAA2_UAV_STORE_TYPED_UNORM_FLOAT", ( uavStoreTypesUnormFloat ) ? ( "1" ) : ( "0" ) ) );
        shaderMacros.push_back( std::pair<std::string, std::string>( "CMAA2_UAV_STORE_CONVERT_TO_SRGB", ( convertToSRGBOnOutput ) ? ( "1" ) : ( "0" ) ) );
        shaderMacros.push_back( std::pair<std::string, std::string>( "CMAA2_SUPPORT_HDR_COLOR_RANGE", ( hdrFormat ) ? ( "1" ) : ( "0" ) ) );
    }

    if( optionalInLuma.Source != nullptr )
        shaderMacros.push_back( std::pair<std::string, std::string>( "CMAA2_EDGE_DETECTION_LUMA_PATH", "2" ) );

    // create all temporary storage buffers
    {
        const int resX = (int)inOutColorDesc.Width;
        const int resY = (int)inOutColorDesc.Height;

        DXGI_FORMAT edgesFormat;
        int edgesResX = resX;
#if CMAA2_PACK_SINGLE_SAMPLE_EDGE_TO_HALF_WIDTH // adds more ALU but reduces memory use for edges by half by packing two 4 bit edge info into one R8_UINT texel - helps on all HW except at really low res
        if( m_textureSampleCount == 1 ) edgesResX = ( edgesResX + 1 ) / 2;
#endif // CMAA2_PACK_SINGLE_SAMPLE_EDGE_TO_HALF_WIDTH

        switch( m_textureSampleCount )
        {
        case( 1 ): edgesFormat = DXGI_FORMAT_R8_UINT;   break;
        case( 2 ): edgesFormat = DXGI_FORMAT_R8_UINT;   break;
        case( 4 ): edgesFormat = DXGI_FORMAT_R16_UINT;   break;
        case( 8 ): edgesFormat = DXGI_FORMAT_R32_UINT;   break;
        default: assert( false ); edgesFormat = DXGI_FORMAT_UNKNOWN;
        }

        CreateTexture2DAndViews( deviceDX12, edgesFormat, edgesResX, resY, m_workingEdgesResource, nullptr, &m_workingEdgesUAV, false );
        // m_workingEdges = vaTexture::Create2D( GetRenderDevice( ), edgesFormat, edgesResX, resY, 1, 1, 1, vaResourceBindSupportFlags::UnorderedAccess );

        CreateTexture2DAndViews( deviceDX12, DXGI_FORMAT_R32_UINT, ( resX + 1 ) / 2, ( resY + 1 ) / 2, m_workingDeferredBlendItemListHeadsResource, nullptr, &m_workingDeferredBlendItemListHeadsUAV, true );
        // m_workingDeferredBlendItemListHeads = vaTexture::Create2D( GetRenderDevice( ), vaResourceFormat::R32_UINT, ( resX + 1 ) / 2, ( resY + 1 ) / 2, 1, 1, 1, vaResourceBindSupportFlags::UnorderedAccess );

#if 0
        // completely safe version even with very high noise
        int requiredCandidatePixels = resX * resY * m_textureSampleCount;                   // enough in all cases
        int requiredDeferredColorApplyBuffer = resX * resY * m_textureSampleCount;          // enough in all cases
        int requiredListHeadsPixels = ( resX * resY + 3 ) / 4;                                // enough in all cases - this covers the whole screen since list is in 2x2 quads
#else
        // 99.99% safe version that uses less memory but will start running out of storage in extreme cases (and start ignoring edges in a non-deterministic way)
        // on an average scene at ULTRA preset only 1/4 of below is used but we leave 4x margin for extreme cases like full screen dense foliage
        int requiredCandidatePixels = resX * resY / 4 * m_textureSampleCount;
        int requiredDeferredColorApplyBuffer = resX * resY / 2 * m_textureSampleCount;
        int requiredListHeadsPixels = ( resX * resY + 3 ) / 6;
#endif

        // Create buffer for storing a list of all pixel candidates to process (potential AA shapes, both simple and complex)
        {
            D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer( requiredCandidatePixels * sizeof( UINT ), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );
            CreateBufferAndViews( deviceDX12, desc, m_workingShapeCandidatesResource, nullptr, &m_workingShapeCandidatesUAV, sizeof( UINT ), false, false );
        }

        // Create buffer for storing linked list of all output values to blend
        {
            D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer( requiredDeferredColorApplyBuffer * sizeof( UINT ) * 2, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );
            CreateBufferAndViews( deviceDX12, desc, m_workingDeferredBlendItemListResource, nullptr, &m_workingDeferredBlendItemListUAV, sizeof( UINT ) * 2, false, false );
        }

        // Create buffer for storing a list of coordinates of linked list heads quads, to allow for combined processing in the last step
        {
            D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer( requiredListHeadsPixels * sizeof( UINT ), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );
            CreateBufferAndViews( deviceDX12, desc, m_workingDeferredBlendLocationListResource, nullptr, &m_workingDeferredBlendLocationListUAV, sizeof( UINT ), false, false );
        }

        // Control buffer (always the same size, doesn't need re-creating but oh well)
        {
            D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer( 16 * sizeof( UINT ), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );
            CreateBufferAndViews( deviceDX12, desc, m_workingControlBufferResource, nullptr, &m_workingControlBufferUAV, sizeof( UINT ), true, true );
        }

        // Separate execute-indirect buffer (always the same size, doesn't need re-creating but oh well)
        {
            D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer( 4 * sizeof( UINT ), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );
            CreateBufferAndViews( deviceDX12, desc, g_workingExecuteIndirectBufferResource, nullptr, &g_workingExecuteIndirectBufferUAV, sizeof( UINT ), true, true );
        }

        m_firstRun = true;
    }

    // Update shaders to match input/output format permutations based on support
    {
#if 0
        // We can check feature support for this but there's no point really since DX should fall back to 32 if min16float not supported anyway
        D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT capsMinPrecisionSupport;
        hr = device->CheckFeatureSupport( D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT, &capsMinPrecisionSupport, sizeof( capsMinPrecisionSupport ) );
        if( capsMinPrecisionSupport.AllOtherShaderStagesMinPrecision != 0 )
            shaderMacros.push_back( std::pair<std::string, std::string>( "CMAA2_USE_HALF_FLOAT_PRECISION", "1" ) );
        else
            shaderMacros.push_back( std::pair<std::string, std::string>( "CMAA2_USE_HALF_FLOAT_PRECISION", "0" ) );
#endif

        m_CSEdgesColor2x2->CreateShaderFromFile(            "vaCMAA2.hlsl", "EdgesColor2x2CS", shaderMacros, false );
        m_CSProcessCandidates->CreateShaderFromFile(        "vaCMAA2.hlsl", "ProcessCandidatesCS", shaderMacros, false );
        m_CSDeferredColorApply2x2->CreateShaderFromFile(    "vaCMAA2.hlsl", "DeferredColorApply2x2CS", shaderMacros, false );
        m_CSComputeDispatchArgs->CreateShaderFromFile(      "vaCMAA2.hlsl", "ComputeDispatchArgsCS", shaderMacros, false );
        m_CSDebugDrawEdges->CreateShaderFromFile(           "vaCMAA2.hlsl", "DebugDrawEdgesCS", shaderMacros, false );

        m_CSEdgesColor2x2ShaderContentsID         = -1;
        m_CSProcessCandidatesShaderContentsID     = -1;
        m_CSDeferredColorApply2x2ShaderContentsID = -1;
        m_CSComputeDispatchArgsShaderContentsID   = -1;
        m_CSDebugDrawEdgesShaderContentsID        = -1;
    }

    UpdateInputViewDescriptors( deviceDX12, inOutColor, optionalInLuma, inColorMS, inColorMSComplexityMask );

    return true;
}

void vaCMAA2DX12::UpdateInputViewDescriptors( ID3D12Device * deviceDX12, const InputResourceHelperDX12 & inOutColor, const InputResourceHelperDX12 & optionalInLuma, const InputResourceHelperDX12 & inColorMS, const InputResourceHelperDX12 & inColorMSComplexityMask )
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;

    FillShaderResourceViewDesc( srvDesc, inOutColor.Source, inOutColor.SRVFormat );
    CreateShaderResourceView( deviceDX12, m_inoutColorReadonlySRV, inOutColor.Source, srvDesc );

    FillUnorderedAccessViewDesc( uavDesc, inOutColor.Source, m_textureUAVFormat );
    CreateUnorderedAccessView( deviceDX12, m_inoutColorWriteonlyUAV, inOutColor.Source, nullptr, uavDesc );

    if( inColorMS.Source != nullptr )
    {
        FillShaderResourceViewDesc( srvDesc, inColorMS.Source, inColorMS.SRVFormat );
        CreateShaderResourceView( deviceDX12, m_inColorMSReadonlySRV, inColorMS.Source, srvDesc );

        if( inColorMSComplexityMask.Source != nullptr )
        {
            FillShaderResourceViewDesc( srvDesc, inColorMSComplexityMask.Source, inColorMSComplexityMask.SRVFormat );
            CreateShaderResourceView( deviceDX12, m_inColorMSComplexityMaskReadonlySRV, inColorMSComplexityMask.Source, srvDesc );
        }
        else
            m_inColorMSComplexityMaskReadonlySRV.Reset();
    }
    else
    {
        m_inColorMSReadonlySRV.Reset();
        m_inColorMSComplexityMaskReadonlySRV.Reset();
    }

    if( optionalInLuma.Source != nullptr )
    {
        FillShaderResourceViewDesc( srvDesc, optionalInLuma.Source, optionalInLuma.SRVFormat );
        CreateShaderResourceView( deviceDX12, m_inLumaReadonlySRV, optionalInLuma.Source, srvDesc );
    }
    else
        m_inLumaReadonlySRV.Reset();
}

// Framework-specific shader handling to enable recompilation at runtime 
static void UpdatePSOIfNeeded( vaRenderDevice & device, ID3D12RootSignature * rootSignature, bool & allOk, const shared_ptr<vaComputeShader> & shader, int64 & prevShaderUniqueContentsID, ComPtr<ID3D12PipelineState> & pso )
{
    auto const & deviceDX12 = AsDX12( device ).GetPlatformDevice();

    vaFramePtr<vaShaderDataDX12> shaderBlob; int64 shaderUniqueContentsID; 

    vaShader::State shaderState = AsDX12( *shader ).GetShader( shaderBlob, shaderUniqueContentsID );

    // if shader is cooked (compiled) and same ID as before, we're cool
    if( shaderState == vaShader::State::Cooked && shaderUniqueContentsID == prevShaderUniqueContentsID )
        return;

    // if shader is not cooked or the ID changed (shader recompiled for example) then delete the PSO
    AsDX12( device ).SafeReleaseAfterCurrentGPUFrameDone( pso );

    // if shader is cooked, create the PSO
    if( shaderState == vaShader::State::Cooked )
    {
        prevShaderUniqueContentsID = shaderUniqueContentsID;
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc;
        desc.pRootSignature = rootSignature;
        desc.NodeMask       = 0;
        desc.CachedPSO      = { nullptr, 0 };
        desc.Flags          = D3D12_PIPELINE_STATE_FLAG_NONE;
        desc.CS = { shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize() };
        deviceDX12->CreateComputePipelineState( &desc, IID_PPV_ARGS(&pso) );
    }
    else
    {
        // shader not cooked - all is not ok
        allOk = false;
    }
}

// Framework-specific shader handling to enable recompilation at runtime 
bool vaCMAA2DX12::UpdatePSOs( )
{
    // got to wait until all shaders compiled so that they can be used to create PSOs
    m_CSEdgesColor2x2->WaitFinishIfBackgroundCreateActive();
    m_CSProcessCandidates->WaitFinishIfBackgroundCreateActive();
    m_CSDeferredColorApply2x2->WaitFinishIfBackgroundCreateActive();
    m_CSComputeDispatchArgs->WaitFinishIfBackgroundCreateActive();
    m_CSDebugDrawEdges->WaitFinishIfBackgroundCreateActive();

    bool allOk = true;

    UpdatePSOIfNeeded( GetRenderDevice(), m_rootSignature.Get(), allOk, m_CSEdgesColor2x2,          m_CSEdgesColor2x2ShaderContentsID,          m_PSOEdgesColorPass );
    UpdatePSOIfNeeded( GetRenderDevice(), m_rootSignature.Get(), allOk, m_CSProcessCandidates,      m_CSProcessCandidatesShaderContentsID,      m_PSOProcessCandidatesPass );
    UpdatePSOIfNeeded( GetRenderDevice(), m_rootSignature.Get(), allOk, m_CSDeferredColorApply2x2,  m_CSDeferredColorApply2x2ShaderContentsID,  m_PSODeferredColorApplyPass );
    UpdatePSOIfNeeded( GetRenderDevice(), m_rootSignature.Get(), allOk, m_CSComputeDispatchArgs,    m_CSComputeDispatchArgsShaderContentsID,    m_PSOComputeDispatchArgsPass );
    UpdatePSOIfNeeded( GetRenderDevice(), m_rootSignature.Get(), allOk, m_CSDebugDrawEdges,         m_CSDebugDrawEdgesShaderContentsID,         m_PSODebugDrawEdgesPass );

    return allOk;
}

vaDrawResultFlags vaCMAA2DX12::Execute( vaRenderDeviceContext & renderContext, ID3D12GraphicsCommandList* commandList, const InputResourceHelperDX12 & inOutColor, const InputResourceHelperDX12 & optionalInLuma, const InputResourceHelperDX12 & inColorMS, const InputResourceHelperDX12 & inColorMSComplexityMask )
{
    renderContext;
    commandList->SetComputeRootSignature( m_rootSignature.Get() );
    ID3D12DescriptorHeap * descHeaps[1] = { m_descHeap.Get() };
    commandList->SetDescriptorHeaps( _countof(descHeaps), descHeaps );
    commandList->SetComputeRootDescriptorTable( 0, m_descHeap->GetGPUDescriptorHandleForHeapStart() );

    // multisample surface case
    if( m_textureSampleCount != 1  )
    {
        assert( !m_inColorMSReadonlySRV.IsNull() );

        if( inColorMS.BeforeState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE )
            commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition( inColorMS.Source, inColorMS.BeforeState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) );

        // also set multisample complexity mask SRV, if provided
        if( !m_inColorMSComplexityMaskReadonlySRV.IsNull() )
        {
            if( inColorMSComplexityMask.BeforeState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE )
                commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition( inColorMSComplexityMask.Source, inColorMSComplexityMask.BeforeState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) );
        }

        // we shouldn't be using this in MSAA case
        assert( m_inLumaReadonlySRV.IsNull() );

        // we're only writing into color
        if( inOutColor.BeforeState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS )
            commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition( inOutColor.Source, inOutColor.BeforeState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
    }
    else
    {
        if( inOutColor.BeforeState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE )
            commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition( inOutColor.Source, inOutColor.BeforeState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) );

        if( !m_inLumaReadonlySRV.IsNull() )
        {
            assert( optionalInLuma.Source != nullptr );
            if( optionalInLuma.BeforeState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE )
                commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition( optionalInLuma.Source, inOutColor.BeforeState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) );
        }
    }

    // We have to clear m_workingControlBufferResource during the first run so just execute second ComputeDispatchArgs that does it anyway, 
    // there are no bad side-effects from this and it saves creating another shader/pso
    // Reminder: consider using ID3D12CommandList2::WriteBufferImmediate instead.
    if( m_firstRun )
    {
        m_firstRun = false;
        VA_TRACE_CPUGPU_SCOPE( ClearControlBuffer, renderContext );
        commandList->SetPipelineState( m_PSOComputeDispatchArgsPass.Get() );
        commandList->Dispatch( 2, 1, 1 );
        commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV( m_workingControlBufferResource.Get() ) );
    }

    // first pass edge detect
    {
        VA_TRACE_CPUGPU_SCOPE( DetectEdges2x2, renderContext );
        int csOutputKernelSizeX = CMAA2_CS_INPUT_KERNEL_SIZE_X - 2;
        int csOutputKernelSizeY = CMAA2_CS_INPUT_KERNEL_SIZE_Y - 2;
        int threadGroupCountX   = ( m_textureResolutionX + csOutputKernelSizeX * 2 - 1 ) / (csOutputKernelSizeX * 2);
        int threadGroupCountY   = ( m_textureResolutionY + csOutputKernelSizeY * 2 - 1 ) / (csOutputKernelSizeY * 2);

        commandList->SetPipelineState( m_PSOEdgesColorPass.Get() );
        commandList->Dispatch( threadGroupCountX, threadGroupCountY, 1 );
    }

    // Although we only need a barrier for m_workingControlBufferResource for the next pass, technically we will need
    // one for m_workingEdgesResource, m_workingShapeCandidatesResource and m_workingDeferredBlendItemListHeadsResource
    // between 'edge detect' and 'process candidates', so just do a nullptr UAV barrier here to avoid any ambiguity.
    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV( nullptr ) );

    // Set up for the first DispatchIndirect
    {
        VA_TRACE_CPUGPU_SCOPE( ComputeDispatchArgs1CS, renderContext );
        commandList->SetPipelineState( m_PSOComputeDispatchArgsPass.Get() );
        commandList->Dispatch( 2, 1, 1 );
        commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition( g_workingExecuteIndirectBufferResource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT ) );
    }

    // Ensure the actual item count loaded from m_workingControlBufferResource is correct; in practice never noticed any
    // issues without it but leave it in for correctness.
    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV( m_workingControlBufferResource.Get() ) );

    // Process shape candidates DispatchIndirect
    {
        VA_TRACE_CPUGPU_SCOPE( ProcessCandidates, renderContext );

        commandList->SetPipelineState( m_PSOProcessCandidatesPass.Get() );
        commandList->ExecuteIndirect( m_commandSignature.Get(), 1, g_workingExecuteIndirectBufferResource.Get(), 0, nullptr, 0 );
        commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition( g_workingExecuteIndirectBufferResource.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
    }

    // Same as before the previous ComputeDispatchArgs - saves us from doing bunch of other barriers later too.
    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV( nullptr ) );

    // Set up for the second DispatchIndirect
    {
        VA_TRACE_CPUGPU_SCOPE( ComputeDispatchArgs2CS, renderContext );
        commandList->SetPipelineState( m_PSOComputeDispatchArgsPass.Get() );
        commandList->Dispatch( 1, 2, 1 );
        commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition( g_workingExecuteIndirectBufferResource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT ) );
    }

    // Ensure the actual item count loaded from m_workingControlBufferResource is correct; in practice never noticed any
    // issues without it but leave it in for correctness.
    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV( m_workingControlBufferResource.Get() ) );

    // Writing the final outputs using the UAV; in case of MSAA path, the D3D12_RESOURCE_STATE_UNORDERED_ACCESS is already set so only do it in the non-MSAA case.
    if( m_textureSampleCount == 1 )
        commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition( inOutColor.Source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );

    // Resolve & apply blended colors
    {
        VA_TRACE_CPUGPU_SCOPE( DeferredColorApply, renderContext );
        commandList->SetPipelineState( m_PSODeferredColorApplyPass.Get() );
        commandList->ExecuteIndirect( m_commandSignature.Get(), 1, g_workingExecuteIndirectBufferResource.Get(), 0, nullptr, 0 );
        commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition( g_workingExecuteIndirectBufferResource.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );
    }

    // For debugging
    if( m_debugShowEdges )
    {
        VA_TRACE_CPUGPU_SCOPE( DebugDrawEdges, renderContext );

        commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV( nullptr ) );

        int tgcX = ( m_textureResolutionX + 16 - 1 ) / 16;
        int tgcY = ( m_textureResolutionY + 16 - 1 ) / 16;

        commandList->SetPipelineState( m_PSODebugDrawEdgesPass.Get() );
        commandList->Dispatch( tgcX, tgcY, 1 );
    }

    // set 'after' resource states
    {
        if( inOutColor.AfterState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS )
            commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition( inOutColor.Source, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, inOutColor.AfterState ) );

        if( !m_inLumaReadonlySRV.IsNull() && optionalInLuma.AfterState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE )
            commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition( optionalInLuma.Source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, optionalInLuma.AfterState ) );

        if( !m_inColorMSReadonlySRV.IsNull() && inColorMS.AfterState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE )
            commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition( inColorMS.Source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, inColorMS.AfterState ) );

        if( !m_inColorMSComplexityMaskReadonlySRV.IsNull() && inColorMSComplexityMask.AfterState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE )
            commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition( inColorMSComplexityMask.Source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, inColorMSComplexityMask.AfterState ) );
    }

    return vaDrawResultFlags::None;
}

// These two Draw/DrawMS function should contain all framework-specific "glue" required to run CMAA2 DX12; everything else is mostly DX12 code
// (except shader compilation and the safely freeing of DX12 objects).
vaDrawResultFlags vaCMAA2DX12::DrawMS( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & inoutColor, const shared_ptr<vaTexture> & inColorMS, const shared_ptr<vaTexture> & inColorMSComplexityMask )
{
    // Track the external inputs so we can re-create if they change - even if formats and sizes are the same we'll have to update view descriptors.
    // This one is a bit tricky: just by looking at whether input texture ID3D12Resource ptr and/or ID3D12Resource::GetDesc changed we cannot determine for 
    // certain that the texture was not re-created (as it could get the same ptr), which would invalidate all our view descriptors looking into it. So we 
    // track framework-specific shared_ptr-s (which are guaranteed to change if something changed) and reset on change.
    if (    m_externalInOutColor                != inoutColor
         || m_externalOptionalInLuma            != nullptr
         || m_externalInColorMS                 != inColorMS
         || m_externalInColorMSComplexityMask   != inColorMSComplexityMask )
    {
        CleanupTemporaryResources();
        m_externalInOutColor                = inoutColor;
        m_externalOptionalInLuma            = nullptr;
        m_externalInColorMS                 = inColorMS;
        m_externalInColorMSComplexityMask   = inColorMSComplexityMask;
    }

    // AsDX12( renderContext ).CommitRenderTargetsDepthStencilUAVs( );

    vaDrawResultFlags renderResults;
    {
        vaInputResourceHelperDX12 rhIOColor( AsFullDX12(renderContext), inoutColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
        vaInputResourceHelperDX12 rhOILuma( AsFullDX12(renderContext), nullptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON );
        vaInputResourceHelperDX12 rhIColorMS( AsFullDX12(renderContext), inColorMS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
        vaInputResourceHelperDX12 rhIColorComplexityMask( AsFullDX12( renderContext ), inColorMSComplexityMask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );

        if( !UpdateResources( AsDX12( GetRenderDevice() ).GetPlatformDevice().Get(), rhIOColor, rhOILuma, rhIColorMS, rhIColorMS ) || !UpdatePSOs() )
        {
            // abort any resource transitions!
            rhIOColor.Texture = nullptr; rhOILuma.Texture = nullptr; rhIColorMS.Texture = nullptr; rhIColorComplexityMask.Texture = nullptr;
            assert( false );
            return vaDrawResultFlags::UnspecifiedError;
        }

        renderResults = Execute( renderContext, AsDX12( renderContext ).GetCommandList().Get(), rhIOColor, rhOILuma, rhIColorMS, rhIColorMS );
    }
    AsDX12( renderContext ).BindDefaultStates(); // Re-bind descriptor heaps, root signatures, viewports, scissor rects and render targets if any

    return renderResults;
}

// These two Draw/DrawMS function should contain all framework-specific "glue" required to run CMAA2 DX12; everything else is mostly DX12 code
// (except shader compilation and the safely freeing of DX12 objects).
vaDrawResultFlags vaCMAA2DX12::Draw( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & inoutColor, const shared_ptr<vaTexture> & optionalInLuma )
{
    // Track the external inputs so we can re-create if they change - even if formats and sizes are the same we'll have to update view descriptors.
    // This one is a bit tricky: just by looking at whether input texture ID3D12Resource ptr and/or ID3D12Resource::GetDesc changed we cannot determine for 
    // certain that the texture was not re-created (as it could get the same ptr), which would invalidate all our view descriptors looking into it. So we 
    // track framework-specific shared_ptr-s (which are guaranteed to change if something changed) and reset on change.
    if (    m_externalInOutColor                != inoutColor
         || m_externalOptionalInLuma            != optionalInLuma
         || m_externalInColorMS                 != nullptr
         || m_externalInColorMSComplexityMask   != nullptr )
    {
        CleanupTemporaryResources();
        m_externalInOutColor                = inoutColor;
        m_externalOptionalInLuma            = optionalInLuma;
        m_externalInColorMS                 = nullptr;
        m_externalInColorMSComplexityMask   = nullptr;
    }

    // AsDX12( renderContext ).CommitRenderTargetsDepthStencilUAVs( );

    vaDrawResultFlags renderResults;
    {
        vaInputResourceHelperDX12 rhIOColor( AsFullDX12(renderContext), inoutColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
        vaInputResourceHelperDX12 rhOILuma( AsFullDX12(renderContext), optionalInLuma, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
        vaInputResourceHelperDX12 rhIColorMS( AsFullDX12(renderContext), nullptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON );
        vaInputResourceHelperDX12 rhIColorComplexityMask( AsFullDX12( renderContext ), nullptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON );

        if( !UpdateResources( AsDX12( GetRenderDevice() ).GetPlatformDevice().Get(), rhIOColor, rhOILuma, rhIColorMS, rhIColorMS ) || !UpdatePSOs() )
        {
            // abort any resource transitions!
            rhIOColor.Texture = nullptr; rhOILuma.Texture = nullptr; rhIColorMS.Texture = nullptr; rhIColorComplexityMask.Texture = nullptr;
            // assert( false );
            return vaDrawResultFlags::UnspecifiedError;
        }
        renderResults = Execute( renderContext, AsDX12( renderContext ).GetCommandList().Get(), rhIOColor, rhOILuma, rhIColorMS, rhIColorMS );
    }
    AsDX12( renderContext ).BindDefaultStates(); // Re-bind descriptor heaps, root signatures, viewports, scissor rects and render targets if any
 
    return renderResults;
}

void RegisterCMAA2DX12( )
{
    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX12, vaCMAA2, vaCMAA2DX12 );
}