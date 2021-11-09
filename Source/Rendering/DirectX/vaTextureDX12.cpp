///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaTextureDX12.h"

#include "Rendering/DirectX/vaRenderDeviceContextDX12.h"

#include "IntegratedExternals/DirectXTex/DirectXTex/DirectXTex.h"

#include "Core/System/vaFileTools.h"

#include "Core/vaProfiler.h"

#include <iterator>

#if !defined(WINAPI_FAMILY) || (WINAPI_FAMILY != WINAPI_FAMILY_PHONE_APP) || (_WIN32_WINNT > _WIN32_WINNT_WIN8)
#include <wincodec.h>
#endif
#include "IntegratedExternals/DirectXTex/DirectXTex/DirectXTex.h"
#include "IntegratedExternals/DirectXTex/ScreenGrab/ScreenGrab12.h"

#pragma warning ( disable : 4238 )  //  warning C4238: nonstandard extension used: class rvalue used as lvalue

using namespace Vanilla;

shared_ptr<vaTexture> vaTextureDX12::CreateWrap( vaRenderDevice & renderDevice, ID3D12Resource * resource, vaResourceFormat srvFormat, vaResourceFormat rtvFormat, vaResourceFormat dsvFormat, vaResourceFormat uavFormat, vaTextureContentsType contentsType )
{
    D3D12_RESOURCE_DESC desc = resource->GetDesc();
   
    vaResourceFormat resourceFormat = VAFormatFromDXGI(desc.Format);

    vaTextureFlags flags = vaTextureFlags::None; // this should be deduced from something probably, but not needed at the moment

    shared_ptr<vaTexture> newTexture = renderDevice.CreateModule<vaTexture, vaTextureConstructorParams>( vaCore::GUIDCreate( ) );
    AsDX12(*newTexture).Initialize( BindFlagsVAFromDX12(desc.Flags), vaResourceAccessFlags::Default, resourceFormat, srvFormat, rtvFormat, dsvFormat, uavFormat, flags, 0, -1, 0, -1, contentsType );

    vaTextureDX12 * newDX12Texture = newTexture->SafeCast<vaTextureDX12*>();

    newDX12Texture->SetResource( resource, D3D12_RESOURCE_STATE_COMMON );
    newDX12Texture->ProcessResource( false, false );

    return newTexture;
}

vaTextureDX12::vaTextureDX12( const vaRenderingModuleParams & params ) : vaTexture( params ), 
    m_srv( AsDX12(params.RenderDevice) ), m_rtv( AsDX12(params.RenderDevice) ), m_dsv( AsDX12(params.RenderDevice) ), m_uav( AsDX12(params.RenderDevice) )
{
}

vaTextureDX12::~vaTextureDX12( )
{ 
    Destroy( );
}

void vaTextureDX12::SetName( const string & name )
{
    if( m_resource != nullptr )
    {
        m_wname = vaStringTools::SimpleWiden( name );
        m_resource->SetName( m_wname.c_str() );
    }
}

void vaTextureDX12::Destroy( )
{
    assert( !IsMapped() );
    if( m_resource != nullptr ) 
    {
        if( m_viewedOriginal == nullptr )
            m_rsth.RSTHDetach( m_resource.Get() );
        else
            { int dbg = 0; dbg++; }
        AsDX12( GetRenderDevice() ).SafeReleaseAfterCurrentGPUFrameDone( m_resource, false );
        m_srv.SafeRelease();
        m_rtv.SafeRelease();
        m_dsv.SafeRelease();
        m_uav.SafeRelease();
        // reset the keep-alive ptr as resources got destroyed - all weak_ptr-s pointing to this will become invalid from now!
        m_smartThis = std::make_shared<vaTexture*>(this);
    }
    if( m_sharedApiHandle != 0 )
    {
        ::CloseHandle( m_sharedApiHandle );
        m_sharedApiHandle = 0;
    }
}

bool vaTextureDX12::Import( void * buffer, uint64 bufferSize, vaTextureLoadFlags loadFlags, vaResourceBindSupportFlags bindFlags, vaTextureContentsType contentsType )
{
    if( bufferSize <= 4 )
    {
        assert( false ); // ?
        return false;
    }

    Destroy( );

    bool dontAutogenerateMIPs = true; //( loadFlags & vaTextureLoadFlags::AutogenerateMIPsIfMissing ) == 0;
    if( !dontAutogenerateMIPs )
    {
        assert( false ); // no mipmap autogen on dx12
        bindFlags |= vaResourceBindSupportFlags::RenderTarget;   // have to add render target support for when auto generating MIPs
    }

    ID3D12Resource * outResource;
    std::vector<D3D12_SUBRESOURCE_DATA> outSubresources;
    std::unique_ptr<byte[]> outData;
    bool outIsCubemap;

    if( !vaDirectXTools12::LoadTexture( AsDX12(GetRenderDevice()).GetPlatformDevice().Get(), buffer, bufferSize, loadFlags, bindFlags, outResource, outSubresources, outData, outIsCubemap ) )
    {
        VA_WARN( L"vaTextureDX12::Import - error loading texture from a buffer!" );
        return false;
    }

    SetResource( outResource, D3D12_RESOURCE_STATE_COPY_DEST ); // LoadTexture creates them as COPY_DEST 
    SAFE_RELEASE( outResource );
    m_contentsType = contentsType;
    m_bindSupportFlags = bindFlags;
    m_flags |= (outIsCubemap)?(vaTextureFlags::Cubemap):(vaTextureFlags::None);
    ProcessResource( false, true );

    std::vector<vaTextureSubresourceData> outSubresourcesVA;
    std::transform( outSubresources.begin(), outSubresources.end(), std::back_inserter(outSubresourcesVA),
        [](D3D12_SUBRESOURCE_DATA & in) -> vaTextureSubresourceData { return {in.pData, in.RowPitch, in.SlicePitch}; });
    InternalUpdateSubresources( 0, outSubresourcesVA );

    return true;
}

bool vaTextureDX12::Import( const wstring & storageFilePath, vaTextureLoadFlags loadFlags, vaResourceBindSupportFlags binds, vaTextureContentsType contentsType )
{
    std::shared_ptr<vaMemoryStream> fileContents;

    wstring usedPath;

    // try asset paths
    usedPath = vaFileTools::FindLocalFile( storageFilePath );

    // found? try load and return!
    if( vaFileTools::FileExists( usedPath ) )
    {
        fileContents = vaFileTools::LoadMemoryStream( usedPath.c_str() );
        return Import( fileContents->GetBuffer(), fileContents->GetLength(), loadFlags, binds, contentsType );
    }
    else
    {
        vaFileTools::EmbeddedFileData embeddedFile = vaFileTools::EmbeddedFilesFind( vaStringTools::SimpleNarrow( L"textures:\\" + storageFilePath ).c_str( ) );
        if( embeddedFile.HasContents( ) )
            return Import( embeddedFile.MemStream->GetBuffer(), embeddedFile.MemStream->GetLength(), loadFlags, binds, contentsType );
    }

    VA_WARN( L"vaTextureDX12::Import - unable to find or load '%s' texture file!", storageFilePath.c_str( ) );

    return false;
}

void vaTextureDX12::InternalUpdateFromRenderingCounterpart( bool notAllBindViewsNeeded, bool dontResetFlags, bool isCubemap )
{
    if( m_resource == nullptr )
    {
        assert( false );
        return;
    }

    D3D12_RESOURCE_DESC desc = m_resource->GetDesc();

    HRESULT hr;
    D3D12_HEAP_PROPERTIES heapProps; D3D12_HEAP_FLAGS heapFlags;
    V( m_resource->GetHeapProperties( &heapProps, &heapFlags ) );

    if( !dontResetFlags )
    {   
        m_accessFlags = vaResourceAccessFlags::Default;
        switch( heapProps.Type )
        {
        case( D3D12_HEAP_TYPE_DEFAULT	) : m_accessFlags = vaResourceAccessFlags::Default;     break;
        case( D3D12_HEAP_TYPE_UPLOAD	) : m_accessFlags = vaResourceAccessFlags::CPUWrite;    break;
        case( D3D12_HEAP_TYPE_READBACK  ) : m_accessFlags = vaResourceAccessFlags::CPURead;     break;
        case( D3D12_HEAP_TYPE_CUSTOM	) : assert( false ); break; // not implemented
        default: assert( false ); break;
        }
    }
    else
    {
        switch( heapProps.Type )
        {
        case( D3D12_HEAP_TYPE_DEFAULT	) : assert( m_accessFlags == vaResourceAccessFlags::Default );          break;
        case( D3D12_HEAP_TYPE_UPLOAD	) : assert( ( m_accessFlags & vaResourceAccessFlags::CPUWrite) != 0 );  break;
        case( D3D12_HEAP_TYPE_READBACK  ) : assert( (m_accessFlags & vaResourceAccessFlags::CPURead) != 0 );    break;
        case( D3D12_HEAP_TYPE_CUSTOM	) : assert( false ); break; // not implemented
        default: assert( false ); break;
        }
        if( ( (m_accessFlags & vaResourceAccessFlags::CPUReadManuallySynced) != 0 ) )
            { assert( (m_accessFlags & vaResourceAccessFlags::CPURead) != 0 ); }
    }

   
    if( !dontResetFlags )
        m_flags     = vaTextureFlags::None;
    m_type      = vaTextureType::Unknown; 

    if( m_mappableTextureInfo != nullptr ) assert( m_accessFlags != vaResourceAccessFlags::Default );
    if( m_accessFlags != vaResourceAccessFlags::Default )
    {
        assert( m_mappableTextureInfo != nullptr );
        MappableTextureInfo & mappableInfo = *m_mappableTextureInfo;
        desc = mappableInfo.CopyableResDesc;
    }

    if( m_resourceFormat != vaResourceFormat::Automatic )
    { assert( m_resourceFormat == VAFormatFromDXGI(desc.Format) ); }
    m_resourceFormat    = VAFormatFromDXGI(desc.Format);

    switch( desc.Dimension )
    {
        case D3D12_RESOURCE_DIMENSION_UNKNOWN:
        {
            assert( false );   
         }  break;
        case D3D12_RESOURCE_DIMENSION_BUFFER:
        {
            assert( false ); // not implemented

            // dx11Texture->m_buffer = vaDirectXTools11::QueryResourceInterface<ID3D11Buffer>( dx11Texture->GetResource(), IID_ID3D11Buffer );        
            // assert( dx11Texture->m_buffer != NULL );
            // if( dx11Texture->m_buffer == NULL )
            // {
            //     dx11Texture->Destroy();
            //     return;
            // }
            // m_type = vaTextureType::Buffer;
            // 
            // D3D11_BUFFER_DESC desc;
            // dx11Texture->m_buffer->GetDesc( &desc );
            // 
            // m_sizeX             = desc.ByteWidth;
            // m_sizeY             = desc.StructureByteStride;
            // 
            // Usage               = desc.Usage;
            // BindFlags           = desc.BindFlags;
            // CPUAccessFlags      = desc.CPUAccessFlags;
            // MiscFlags           = desc.MiscFlags;
        }  break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
        {
            m_type              = vaTextureType::Texture1D;
            m_sizeX             = (int)desc.Width;
            m_mipLevels         = desc.MipLevels;
            m_sizeY             = 1; 
            m_sizeZ             = 1;
            m_sampleCount       = 1;
            m_arrayCount        = desc.DepthOrArraySize;
            
            // If we support mapping, build the mapping data
            if( m_accessFlags != vaResourceAccessFlags::Default )
            {
                assert( (m_sizeY == 1) && (m_sizeZ == 1) && (m_arrayCount == 1) && (m_sampleCount == 1) ); // Support for mapping/unmapping (not all possible permutations supported yet)
                int bytesPerPixel = vaResourceFormatHelpers::GetPixelSizeInBytes( m_resourceFormat );
                assert( bytesPerPixel != 0 );

                if( bytesPerPixel > 0 )
                {
                    m_mappedData.resize( m_mipLevels );

                    int sizeX = m_sizeX;
                    int sizeY = m_sizeY;
                    for( int i = 0; i < m_mipLevels; i++ )
                    {
                        m_mappedData[i].SizeX          = sizeX;
                        m_mappedData[i].SizeY          = sizeY;
                        m_mappedData[i].BytesPerPixel  = bytesPerPixel;
                        m_mappedData[i].RowPitch       = 0;
                        m_mappedData[i].SizeInBytes    = 0;
                        m_mappedData[i].DepthPitch     = 0;
                        m_mappedData[i].Buffer         = nullptr;
        
                        // shouldn't ever be zero - too many mips? that should've been handled before!
                        if( i != ( m_mipLevels - 1 ) )
                        {
                            assert( ( sizeX % 2 ) == 0 );
                            assert( ( sizeY % 2 ) == 0 );
                            sizeX = sizeX / 2;
                            sizeY = sizeY / 2;
                        }
                    }
                }
            }


        }  break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
        {
            m_type              = vaTextureType::Texture2D;
            m_sizeX             = (int)desc.Width;
            m_sizeY             = desc.Height;
            m_mipLevels         = desc.MipLevels;
            m_sizeZ             = 1;
            m_sampleCount       = desc.SampleDesc.Count;
            //                  = desc.SampleDesc.Quality;
            m_arrayCount        = desc.DepthOrArraySize;

            // If we support mapping, build the mapping data
            if( m_accessFlags != vaResourceAccessFlags::Default )
            {
                assert( (m_sizeZ == 1) && (m_arrayCount == 1) && (m_sampleCount == 1) );  // Support for arrays can be added but not for MS
                int bytesPerPixel = vaResourceFormatHelpers::GetPixelSizeInBytes( m_resourceFormat );
                assert( bytesPerPixel != 0 );

                if( bytesPerPixel > 0 )
                {
                    m_mappedData.resize( m_mipLevels );

                    int sizeX = m_sizeX;
                    int sizeY = m_sizeY;
                    for( int i = 0; i < m_mipLevels; i++ )
                    {
                        m_mappedData[i].SizeX           = sizeX;
                        m_mappedData[i].SizeY           = sizeY;
                        m_mappedData[i].BytesPerPixel   = bytesPerPixel;
                        m_mappedData[i].RowPitch       = 0;
                        m_mappedData[i].SizeInBytes    = 0;
                        m_mappedData[i].DepthPitch     = 0;
                        m_mappedData[i].Buffer         = nullptr;
        
                        // shouldn't ever be zero - too many mips? that should've been handled before!
                        if( i != ( m_mipLevels - 1 ) )
                        {
                            assert( ( sizeX % 2 ) == 0 );
                            assert( ( sizeY % 2 ) == 0 );
                            sizeX = sizeX / 2;
                            sizeY = sizeY / 2;
                        }
                    }
                }
            }

        }  break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
        {
            m_type              = vaTextureType::Texture3D;
            m_sizeX             = (int)desc.Width;
            m_sizeY             = desc.Height;
            m_sizeZ             = desc.DepthOrArraySize;
            m_mipLevels         = desc.MipLevels;
            m_sampleCount       = 1;
            m_arrayCount        = 1;

            // If we support mapping, build the mapping data
            if( m_accessFlags != vaResourceAccessFlags::Default )
            {
                assert( false );    // not implemented yet
            }
        }  break;
        default:
        {
            m_type = vaTextureType::Unknown; assert( false );
        }  break;
    }

    // At the moment 3D textures can't have sub-views - it's all or nothing; there's no reason for this other than a little refactoring
    // that's required - m_arrayCount and m_sizeZ must become m_sizeZ_or_arrayCount and everything else must be converted accordingly
    assert( GetType() != vaTextureType::Texture3D || (m_viewedArraySlice == 0 && m_viewedArraySliceCount == -1 && m_arrayCount == 1) );
    if(m_type == vaTextureType::Texture3D)
    {
        m_viewedArraySlice = 0;
        m_viewedArraySliceCount = m_arrayCount = m_sizeZ;
    }

    // -1 means all above min
    if( m_viewedMipSliceCount == -1 )
        m_viewedMipSliceCount = GetMipLevels() - m_viewedMipSlice;
    if( m_viewedArraySliceCount == -1 )
        m_viewedArraySliceCount = GetArrayCount() - m_viewedArraySlice; //((GetType() == vaTextureType::Texture3D)?( GetDepth() ): ( GetArrayCount() )) - m_viewedArraySlice;

    assert( m_viewedMipSlice >= 0 && m_viewedMipSlice < m_mipLevels );
    assert( (m_viewedMipSlice+m_viewedMipSliceCount) > 0 && (m_viewedMipSlice+m_viewedMipSliceCount) <= m_mipLevels );
    assert( m_viewedArraySlice >= 0 && m_viewedArraySlice < m_arrayCount );
    assert( ( m_viewedArraySlice + m_viewedArraySliceCount ) > 0 && ( m_viewedArraySlice + m_viewedArraySliceCount ) <= m_arrayCount );

    // this is to support views (vaTexture::CreateView) into specific array items or mips - it's initialized for regular textures as well to maintain full interoperability
    int viewedSliceSizeX = m_sizeX;
    int viewedSliceSizeY = m_sizeY;
    int viewedSliceSizeZ = m_sizeZ;
    for( int i = 0; i < m_viewedMipSlice; i++ )
    {
        // mipmap generation seems to compute mip dimensions by round down (dim >> 1) which means potential loss of data, so that's why we assert
        // (these asserts were temporarily disabled - need to be put back in at some point when the rest of the codebase is fixed to comply)
        // assert( (m_viewedSliceSizeX % 2) == 0 || m_viewedSliceSizeX == 1 ); 
        // assert( (m_viewedSliceSizeY % 2) == 0 || m_viewedSliceSizeY == 1 ); 
        // if( m_type == vaTextureType::Texture3D )
        //     assert( (m_viewedSliceSizeZ % 2) == 0 || m_viewedSliceSizeZ == 1 );
        viewedSliceSizeX = (viewedSliceSizeX) / 2;
        viewedSliceSizeY = (viewedSliceSizeY) / 2;
        viewedSliceSizeZ = (viewedSliceSizeZ) / 2;
    }
    viewedSliceSizeX = vaMath::Max( viewedSliceSizeX, 1 );
    viewedSliceSizeY = vaMath::Max( viewedSliceSizeY, 1 );
    viewedSliceSizeZ = vaMath::Max( viewedSliceSizeZ, 1 );

    // this could be a subview
    if( m_viewedOriginal != nullptr )
    {
        // is it a subview or do we cover all subresources?
        if( m_viewedMipSliceCount != m_mipLevels || m_viewedArraySliceCount != m_arrayCount )
        {
            // just some subresources - add them to the list
            for( int mipSlice = m_viewedMipSlice; mipSlice < m_viewedMipSlice + m_viewedMipSliceCount; mipSlice++ )
                for( int arraySlice = m_viewedArraySlice; arraySlice < m_viewedArraySlice + m_viewedArraySliceCount; arraySlice++ )
                {
                    m_viewSubresourceList.push_back( D3D12CalcSubresource( mipSlice, arraySlice, 0, m_mipLevels, m_arrayCount ) );
                }
        }

        m_sizeX      = viewedSliceSizeX;
        m_sizeY      = viewedSliceSizeY;
        m_sizeZ      = viewedSliceSizeZ;
        m_mipLevels  = m_viewedMipSliceCount;
        m_arrayCount = m_viewedArraySliceCount;
    }

    if( isCubemap )
        m_flags |= vaTextureFlags::Cubemap;

    // make sure bind flags were set up correctly
    if( !notAllBindViewsNeeded )
    {
        if( (desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE       ) != 0 )
        { assert( ( m_bindSupportFlags & (vaResourceBindSupportFlags::ConstantBuffer|vaResourceBindSupportFlags::ShaderResource|vaResourceBindSupportFlags::UnorderedAccess) ) == 0 ); }
        else
            if( m_mappableTextureInfo == nullptr )
            { assert( ( m_bindSupportFlags & (vaResourceBindSupportFlags::ConstantBuffer|vaResourceBindSupportFlags::ShaderResource|vaResourceBindSupportFlags::UnorderedAccess|vaResourceBindSupportFlags::RenderTarget) ) != 0 ); } 
        if( (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET       ) != 0 )
        {     assert( ( m_bindSupportFlags & vaResourceBindSupportFlags::RenderTarget ) != 0 ); }
        if( (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL       ) != 0 )
        {     assert( ( m_bindSupportFlags & vaResourceBindSupportFlags::DepthStencil ) != 0 ); }
        if( (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS       ) != 0 )
        {     assert( ( m_bindSupportFlags & vaResourceBindSupportFlags::UnorderedAccess ) != 0 ); }
    }
}

void vaTextureDX12::SetResource( ID3D12Resource * resource, D3D12_RESOURCE_STATES initialState )
{
    Destroy();
    m_resource = resource;
    if( m_viewedOriginal == nullptr )
        m_rsth.RSTHAttach(resource, initialState);
    else
        { int dbg = 0; dbg++; }
}

void vaTextureDX12::ProcessResource( bool notAllBindViewsNeeded, bool dontResetFlags )
{
    bool isCubemap = ((m_flags & vaTextureFlags::Cubemap) != 0) && ((m_flags & vaTextureFlags::CubemapButArraySRV) == 0);
    InternalUpdateFromRenderingCounterpart( notAllBindViewsNeeded, dontResetFlags, isCubemap );

    if( ( GetBindSupportFlags( ) & (vaResourceBindSupportFlags::VertexBuffer | vaResourceBindSupportFlags::IndexBuffer | vaResourceBindSupportFlags::ConstantBuffer) ) != 0 )
    {
        assert( false ); // not implemented
    }

    if( ( ( GetBindSupportFlags( ) & vaResourceBindSupportFlags::ShaderResource ) != 0 ) && (GetSRVFormat() != vaResourceFormat::Unknown) )
    {
        // not the cleanest way to do this - should probably get updated and also assert on _TYPELESS
        if( GetSRVFormat() == vaResourceFormat::Automatic )
        {
            m_srvFormat = m_resourceFormat;
        }
        
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
        vaDirectXTools12::FillShaderResourceViewDesc( srvDesc, m_resource.Get(), DXGIFormatFromVA(GetSRVFormat( )), m_viewedMipSlice, m_viewedMipSliceCount, m_viewedArraySlice, m_viewedArraySliceCount, isCubemap );
        m_srv.Create( m_resource.Get(), srvDesc );
    }

    if( ( ( GetBindSupportFlags( ) & vaResourceBindSupportFlags::RenderTarget ) != 0 ) && (GetRTVFormat() != vaResourceFormat::Unknown) )
    {
        // not the cleanest way to do this - should probably get updated and also assert on _TYPELESS
        if( GetRTVFormat( ) == vaResourceFormat::Automatic )
        {
            m_rtvFormat = m_resourceFormat;
        }

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc;
        vaDirectXTools12::FillRenderTargetViewDesc( rtvDesc, m_resource.Get(), DXGIFormatFromVA(GetRTVFormat( )), m_viewedMipSlice, m_viewedArraySlice, m_viewedArraySliceCount );
        m_rtv.Create( m_resource.Get(), rtvDesc );
    }

    if( ( ( GetBindSupportFlags( ) & vaResourceBindSupportFlags::DepthStencil ) != 0 ) && (GetDSVFormat() != vaResourceFormat::Unknown) )
    {
        // not the cleanest way to do this - should probably get updated and also assert on _TYPELESS
        if( GetDSVFormat( ) == vaResourceFormat::Automatic )
        {
            m_dsvFormat = m_resourceFormat;
        }
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
        vaDirectXTools12::FillDepthStencilViewDesc( dsvDesc, m_resource.Get(), DXGIFormatFromVA(GetDSVFormat( )), m_viewedMipSlice, m_viewedArraySlice, m_viewedArraySliceCount );
        m_dsv.Create( m_resource.Get(), dsvDesc );
    }

    if( ( ( GetBindSupportFlags( ) & vaResourceBindSupportFlags::UnorderedAccess ) != 0 ) && (GetUAVFormat() != vaResourceFormat::Unknown) )
    {
        // not the cleanest way to do this - should probably get updated and also assert on _TYPELESS
        if( GetUAVFormat( ) == vaResourceFormat::Automatic )
        {
            m_uavFormat = m_resourceFormat;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;
        vaDirectXTools12::FillUnorderedAccessViewDesc( uavDesc, m_resource.Get(), DXGIFormatFromVA(GetUAVFormat( )), m_viewedMipSlice, m_viewedArraySlice, m_viewedArraySliceCount );
        m_uav.Create( m_resource.Get(), nullptr, uavDesc );
    }
}

void vaTextureDX12::ClearRTV( vaRenderDeviceContext & context, const vaVector4 & clearValue )
{
    assert( GetRenderDevice( ).IsFrameStarted( ) );
    assert( m_rtv.IsCreated() ); if( !m_rtv.IsCreated() ) return;   // texture most likely missing vaResourceBindSupportFlags::RenderTarget flag!
    TransitionResource( AsDX12(context), D3D12_RESOURCE_STATE_RENDER_TARGET );
    AsDX12( context ).GetCommandList()->ClearRenderTargetView( m_rtv.GetCPUHandle(), &clearValue.x, 0, nullptr );

    // manually transitioning states below means we might mess up the render target states cache
    AsDX12( context ).ResetCachedOutputs( );
}

void vaTextureDX12::ClearUAV( vaRenderDeviceContext & context, const vaVector4ui & clearValue )
{
    assert( GetRenderDevice( ).IsFrameStarted( ) );
    // see https://www.gamedev.net/forums/topic/672063-d3d12-clearunorderedaccessviewfloat-fails/ for the reason behind the mess below
    assert( m_uav.IsCreated() ); if( !m_uav.IsCreated() ) return;   // texture most likely missing vaResourceBindSupportFlags::UnorderedAccess flag!
    TransitionResource( AsDX12(context), D3D12_RESOURCE_STATE_UNORDERED_ACCESS );

#if 0
    vaRenderDeviceDX12::TransientGPUDescriptorHeap * allocator = AsDX12( GetRenderDevice() ).GetTransientDescriptorHeap( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    assert( allocator != nullptr );
    int descIndex;
    allocator->Allocate( 1, descIndex );
    AsDX12( GetRenderDevice() ).GetPlatformDevice()->CopyDescriptorsSimple( 1, allocator->ComputeCPUHandle( descIndex ), m_uav.GetCPUHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    AsDX12( context ).GetCommandList()->ClearUnorderedAccessViewUint( allocator->ComputeGPUHandle( descIndex ), m_uav.GetCPUHandle(), m_resource.Get(), &clearValue.x, 0, nullptr );
#else
    AsDX12( context ).GetCommandList()->ClearUnorderedAccessViewUint( m_uav.GetCPUReadableGPUHandle(), m_uav.GetCPUReadableCPUHandle(), m_resource.Get(), &clearValue.x, 0, nullptr );
#endif

    // manually transitioning states below means we might mess up the render target states cache
    AsDX12( context ).ResetCachedOutputs( );
}

void vaTextureDX12::ClearUAV( vaRenderDeviceContext & context, const vaVector4 & clearValue )
{
    assert( GetRenderDevice( ).IsFrameStarted( ) );
    // see https://www.gamedev.net/forums/topic/672063-d3d12-clearunorderedaccessviewfloat-fails/ for the reason behind the mess below
    assert( m_uav.IsCreated() ); if( !m_uav.IsCreated() ) return;
    TransitionResource( AsDX12(context), D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
#if 0
    vaRenderDeviceDX12::TransientGPUDescriptorHeap * allocator = AsDX12( GetRenderDevice() ).GetTransientDescriptorHeap( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    assert( allocator != nullptr );
    int descIndex;
    allocator->Allocate( 1, descIndex );
    AsDX12( GetRenderDevice() ).GetPlatformDevice()->CopyDescriptorsSimple( 1, allocator->ComputeCPUHandle( descIndex ), m_uav.GetCPUHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    AsDX12( context ).GetCommandList()->ClearUnorderedAccessViewFloat( allocator->ComputeGPUHandle( descIndex ), m_uav.GetCPUHandle(), m_resource.Get(), &clearValue.x, 0, nullptr );
#else
    AsDX12( context ).GetCommandList( )->ClearUnorderedAccessViewFloat( m_uav.GetCPUReadableGPUHandle(), m_uav.GetCPUReadableCPUHandle(), m_resource.Get( ), &clearValue.x, 0, nullptr );
#endif

    // manually transitioning states below means we might mess up the render target states cache
    AsDX12( context ).ResetCachedOutputs( );
}

void vaTextureDX12::ClearDSV( vaRenderDeviceContext & context, bool clearDepth, float depthValue, bool clearStencil, uint8 stencilValue )
{
    assert( GetRenderDevice( ).IsFrameStarted( ) );
    assert( m_dsv.IsCreated() ); if( !m_dsv.IsCreated() ) return;
    D3D12_CLEAR_FLAGS clearFlags = (D3D12_CLEAR_FLAGS)0;
    if( clearDepth )    clearFlags |= D3D12_CLEAR_FLAG_DEPTH;
    if( clearStencil )  clearFlags |= D3D12_CLEAR_FLAG_STENCIL;
    TransitionResource( AsDX12(context), D3D12_RESOURCE_STATE_DEPTH_WRITE );
    AsDX12( context ).GetCommandList()->ClearDepthStencilView( m_dsv.GetCPUHandle(), clearFlags, depthValue, stencilValue, 0, nullptr );

    // manually transitioning states below means we might mess up the render target states cache
    AsDX12( context ).ResetCachedOutputs( );
}

void vaTextureDX12::Copy( vaRenderDeviceContextDX12 & renderContext, vaTextureDX12 & dstTexture, vaTextureDX12 & srcTexture )
{
    // just a sanity check
    assert( &srcTexture.GetRenderDevice() == &renderContext.GetRenderDevice() );
    assert( &dstTexture.GetRenderDevice() == &renderContext.GetRenderDevice() );
    assert( renderContext.GetRenderDevice( ).IsRenderThread() );
    assert( renderContext.GetRenderDevice( ).IsFrameStarted( ) );
    
    // these cases are not implemented
    // assert( !srcTexture.IsView() ); // this is actually fine
    // assert( !dstTexture.IsView() ); // this should be fine too?
    assert( srcTexture.GetOverrideView() == nullptr );
    assert( dstTexture.GetOverrideView() == nullptr );
    
    ID3D12Resource * dstResourceDX12 = dstTexture.GetResource();
    ID3D12Resource * srcResourceDX12 = srcTexture.GetResource();

    // manually transitioning states below means we might mess up the render target states cache
    renderContext.ResetCachedOutputs();

    if( srcTexture.m_mappableTextureInfo == nullptr && dstTexture.m_mappableTextureInfo == nullptr )    // Regular case, GPU <-> GPU copies
    {   
        if( srcTexture.GetSampleCount( ) > 1 && dstTexture.GetSampleCount( ) == 1 )
        {
            dstTexture.TransitionResource( renderContext, D3D12_RESOURCE_STATE_RESOLVE_DEST );
            srcTexture.TransitionResource( renderContext, D3D12_RESOURCE_STATE_RESOLVE_SOURCE );
            renderContext.GetCommandList()->ResolveSubresource( dstResourceDX12, 0, srcResourceDX12, 0, DXGIFormatFromVA(dstTexture.GetSRVFormat()) );
        }
        else
        {
            dstTexture.TransitionResource( renderContext, D3D12_RESOURCE_STATE_COPY_DEST );
            srcTexture.TransitionResource( renderContext, D3D12_RESOURCE_STATE_COPY_SOURCE );
            renderContext.GetCommandList( )->CopyResource( dstResourceDX12, srcResourceDX12 );
        }
    }
    else
    {
        // not supported for both textures being mappable
        assert( srcTexture.m_mappableTextureInfo == nullptr || dstTexture.m_mappableTextureInfo == nullptr );
        if( srcTexture.m_mappableTextureInfo != nullptr )   
        {
            // CPU -> GPU upload case

            MappableTextureInfo & mappableInfo = *srcTexture.m_mappableTextureInfo;
            assert( srcTexture.GetAccessFlags() == vaResourceAccessFlags::CPUWrite );
            assert( dstTexture.GetAccessFlags() == vaResourceAccessFlags::Default );
            assert( !srcTexture.IsMapped() );
            
            int subresourceCount = mappableInfo.NumSubresources;
            assert( dstTexture.GetArrayCount() * dstTexture.GetMipLevels() == subresourceCount );
            dstTexture.TransitionResource( renderContext, D3D12_RESOURCE_STATE_COPY_DEST );
            for( int i = 0; i < subresourceCount; i++ )
            {
                CD3DX12_TEXTURE_COPY_LOCATION Src( srcTexture.GetResource(), mappableInfo.Layouts[i] );
                CD3DX12_TEXTURE_COPY_LOCATION Dst( dstTexture.GetResource(), i );
                renderContext.GetCommandList()->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
                // renderContext.GetCommandList()->CopyResource( dstResourceDX12, srcResourceDX12 );
            }

            // have to flush - need to start executing the copy so the fence below is behind the copy!
            // (not sure if there's a better way to do this)
            renderContext.Flush();

            // and immediately add a fence so we can wait for it on map
            mappableInfo.SignalNextFence( AsDX12( renderContext.GetRenderDevice() ) );
        }
        else if( dstTexture.m_mappableTextureInfo != nullptr )   
        {
            // GPU -> CPU download case

            MappableTextureInfo & mappableInfo = *dstTexture.m_mappableTextureInfo;
            assert( srcTexture.GetAccessFlags() == vaResourceAccessFlags::Default );
            assert( (dstTexture.GetAccessFlags() & vaResourceAccessFlags::CPURead) != 0 );
            assert( !dstTexture.IsMapped() );

            int subresourceCount = mappableInfo.NumSubresources;
            assert( srcTexture.GetArrayCount() * srcTexture.GetMipLevels() == subresourceCount );
            srcTexture.TransitionResource( renderContext, D3D12_RESOURCE_STATE_COPY_SOURCE );
            for( int i = 0; i < subresourceCount; i++ )
            {
                CD3DX12_TEXTURE_COPY_LOCATION Dst( dstTexture.GetResource(), mappableInfo.Layouts[i] );
                CD3DX12_TEXTURE_COPY_LOCATION Src( srcTexture.GetResource(), i );
                renderContext.GetCommandList()->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
                // renderContext.GetCommandList()->CopyResource( dstResourceDX12, srcResourceDX12 );
            }

            if( (dstTexture.GetAccessFlags() & vaResourceAccessFlags::CPUReadManuallySynced) == 0 )
            {
                // have to flush - need to start executing the copy so the fence below is behind the copy!
                // (not sure if there's a better way to do this)
                renderContext.Flush();

                // and immediately add a fence so we can wait for it on map
                mappableInfo.SignalNextFence( AsDX12( renderContext.GetRenderDevice() ) );
            }
        }
    }
}

void vaTextureDX12::CopyFrom( vaRenderDeviceContext & context, const shared_ptr<vaTexture> & srcTexture )
{
    Copy( AsFullDX12(context), *this, AsDX12(*srcTexture) );
}

void vaTextureDX12::CopyTo( vaRenderDeviceContext & context, const shared_ptr<vaTexture> & dstTexture )
{
    Copy( AsFullDX12(context), AsDX12(*dstTexture), *this );
}

bool vaTextureDX12::SaveAPACK( vaStream & outStream )
{
    assert( m_viewedOriginal == nullptr );  // don't save a vaTexture that is a view, that's wrong
    if( m_viewedOriginal != nullptr )
        return false;

    VERIFY_TRUE_RETURN_ON_FALSE( outStream.WriteValue<int32>( c_fileVersion ) );

    VERIFY_TRUE_RETURN_ON_FALSE( outStream.WriteValue<vaTextureContentsType     >( m_contentsType ) );

    int64 posOfSize = outStream.GetPosition( );
    VERIFY_TRUE_RETURN_ON_FALSE( outStream.WriteValue<int64>( 0 ) );

    {
        if( GetRenderDevice( ).IsFrameStarted( ) )
            AsFullDX12( GetRenderDevice().GetMainContext() )->Flush( );  // this should be done to make sure resource states are matching
        assert( m_overrideView == nullptr );
        assert( m_viewedOriginal == nullptr );
        assert( (m_flags & vaTextureFlags::CubemapButArraySRV) == 0 ); // not sure what this would mean exactly - is it a cubemap or not? :) the flag will be lost anyway - it can only be vaTextureFlags::Cubemap
        HRESULT hr = vaDirectXTools12::SaveDDSTexture( outStream, AsDX12( GetRenderDevice( ) ).GetCommandQueue().Get( ), m_resource.Get(), (m_flags & (vaTextureFlags::Cubemap | vaTextureFlags::CubemapButArraySRV)) != 0, m_rsth.RSTHGetCurrentState( ), m_rsth.RSTHGetCurrentState( ) );
        if( GetRenderDevice( ).IsFrameStarted( ) )
            AsFullDX12( GetRenderDevice( ).GetMainContext( ) )->Flush( );  // this should be done to make sure resource states are matching
        if( !SUCCEEDED( hr ) )
        {
            VA_LOG_ERROR( L"vaTextureDX12::SaveAPACK failed!" );
        }
    }

    int64 calculatedSize = outStream.GetPosition( ) - posOfSize;
    outStream.Seek( posOfSize );
    VERIFY_TRUE_RETURN_ON_FALSE( outStream.WriteValue<int64>( calculatedSize - 8 ) );
    outStream.Seek( posOfSize + calculatedSize );

    return true;
}

bool vaTextureDX12::LoadAPACK( vaStream & inStream )
{
    Destroy( );
    InitializePreLoadDefaults( );

    int32 fileVersion = 0;
    VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<int32>( fileVersion ) );
    
    // support old format
    if( fileVersion == 2 )
    {
        vaTextureFlags dummyFlags;                  VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<vaTextureFlags          >( dummyFlags ) );
        vaResourceAccessFlags dummyAccessFlags;     VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<vaResourceAccessFlags   >( dummyAccessFlags ) );
        vaTextureType dummyType;                    VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<vaTextureType           >( dummyType ) );
        vaResourceBindSupportFlags dummyBindFlags;  VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<vaResourceBindSupportFlags >( dummyBindFlags ) );
        
        VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<vaTextureContentsType   >( m_contentsType ) );

        vaResourceFormat dummyResFormat;            VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<vaResourceFormat        >( dummyResFormat ) );
        vaResourceFormat dummySRVFormat;            VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<vaResourceFormat        >( dummySRVFormat ) );
        vaResourceFormat dummyRTVFormat;            VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<vaResourceFormat        >( dummyRTVFormat ) );
        vaResourceFormat dummyDSVFormat;            VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<vaResourceFormat        >( dummyDSVFormat ) );
        vaResourceFormat dummyUAVFormat;            VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<vaResourceFormat        >( dummyUAVFormat ) );
        assert( dummyResFormat == dummySRVFormat );
        
        int dummySizeX;                             VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<int                     >( dummySizeX       ) );
        int dummySizeY;                             VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<int                     >( dummySizeY       ) );
        int dummySizeZ;                             VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<int                     >( dummySizeZ       ) );
        int dummySampleCount;                       VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<int                     >( dummySampleCount ) );
        int dummyMipLevels;                         VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<int                     >( dummyMipLevels   ) );
    }
    else if( fileVersion == c_fileVersion )
    {
        VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<vaTextureContentsType     >( m_contentsType ) );
    }
    else
    {
        VA_LOG( L"vaRenderMaterial::Load(): unsupported file version" );
        return false;
    }

    int64 textureDataSize;
    VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<int64                     >( textureDataSize ) );

    // direct reading from the stream not implemented yet
    byte * buffer = new byte[ textureDataSize ];
    if( !inStream.Read( buffer, textureDataSize ) )
    {
        assert( false );
        delete[] buffer;
        return false;
    }

    bool ok = Import( buffer, textureDataSize, vaTextureLoadFlags::Default, m_bindSupportFlags, m_contentsType );

    delete[] buffer;

    if( !ok || m_resource == nullptr )
    {
        VA_WARN( L"vaTextureDX12::Load - error processing file!" );
        assert( false );

        return false;
    }

    return true;
}

bool vaTextureDX12::SerializeUnpacked( vaXMLSerializer & serializer, const string & assetFolder )
{
    if( serializer.IsReading( ) )
        InitializePreLoadDefaults( );

    int32 fileVersion = c_fileVersion;
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<int32>( "FileVersion", fileVersion ) );

    VERIFY_TRUE_RETURN_ON_FALSE( fileVersion == c_fileVersion );

    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<int32>( "contentsType", (int32&)m_contentsType ) );

    string textureFileName = assetFolder + "/Texture.dds";
    vaFileStream textureFile;

    if( serializer.IsWriting( ) )
    {
        if( !textureFile.Open( textureFileName, FileCreationMode::Create, FileAccessMode::ReadWrite ) )
        {
            VA_LOG_ERROR( "vaTextureDX12::SerializeUnpacked - Unable to open '%s'", ( textureFileName ).c_str( ) );
            return false;
        }

        // TODO: there's a same chunk in SaveAPACK - merge into one function
        {
            if( GetRenderDevice( ).IsFrameStarted( ) )
                AsFullDX12( GetRenderDevice( ).GetMainContext( ) )->Flush( );  // this should be done to make sure resource states are matching

            assert( m_overrideView == nullptr );
            assert( m_viewedOriginal == nullptr );
            assert( ( m_flags & vaTextureFlags::CubemapButArraySRV ) == 0 ); // not sure what this would mean exactly - is it a cubemap or not? :) the flag will be lost anyway - it can only be vaTextureFlags::Cubemap
            HRESULT hr = vaDirectXTools12::SaveDDSTexture( textureFile, AsDX12( GetRenderDevice( ) ).GetCommandQueue( ).Get( ), m_resource.Get( ), ( m_flags & ( vaTextureFlags::Cubemap | vaTextureFlags::CubemapButArraySRV ) ) != 0, m_rsth.RSTHGetCurrentState( ), m_rsth.RSTHGetCurrentState( ) );
            if( 
                GetRenderDevice( ).IsFrameStarted( ) )
                AsFullDX12( GetRenderDevice( ).GetMainContext( ) )->Flush( );  // this should be done to make sure resource states are matching
            
            if( !SUCCEEDED( hr ) )
            {
                VA_LOG_ERROR( L"vaTextureDX12::SaveAPACK failed!" );
            }
        }

        textureFile.Close( );
    }
    else if( serializer.IsReading( ) )
    {
        auto memStream = vaFileTools::LoadMemoryStream( textureFileName );
        if( memStream == nullptr )
        {
            VA_LOG_ERROR( "vaTextureDX12::SerializeUnpacked - Unable to open '%s'", ( textureFileName ).c_str( ) );
            return false;
        }
        bool ok = Import( memStream->GetBuffer(), memStream->GetLength(), vaTextureLoadFlags::Default, m_bindSupportFlags, m_contentsType );
        if( !ok )
        {
            VA_WARN( "vaTextureDX12::SerializeUnpacked - error processing file!" );
            assert( false );
        }
    }
    else { assert( false ); return false; }
    return true;
}


bool vaTextureDX12::InternalTryMap( vaResourceMapType mapType, bool doNotWait )
{
    if( m_mappableTextureInfo == nullptr )
    {
        // mapping not supported on this instance - check its vaResourceAccessFlags
        assert( false );
        return false;
    }
    assert( !m_isMapped );
    if( m_isMapped )
        return false;
    assert( m_currentMapType == vaResourceMapType::None );
    if( m_currentMapType != vaResourceMapType::None )
        return false;
    // map not supported on this resource - either the flags are wrong or the support has not yet been added; check InternalUpdateFromRenderingCounterpart for adding support
    assert( m_mappedData.size() > 0 );
    if( m_mappedData.size() == 0 )
        return false;

    // the request must match our capabilities
    if( mapType == vaResourceMapType::Read )
    {
        if( (m_accessFlags & vaResourceAccessFlags::CPURead) == 0 )
        { assert( false ); return false; }
    }
    else if( mapType == vaResourceMapType::Write )
    {
        // it's legal for CPU to write to D3D12_HEAP_TYPE_READBACK - useful for initializing contents
        // if( (m_accessFlags & vaResourceAccessFlags::CPUWrite) == 0 )
        // { assert( false ); return false; }
    }
    else
        { assert( false ); return false; }

    doNotWait;
    
    // Wait for any pending GPU operation on this resource
    bool allOkMoveAlong = ( mapType == vaResourceMapType::Read ) && ((GetAccessFlags() & vaResourceAccessFlags::CPUReadManuallySynced) != 0);
    if( !allOkMoveAlong && !m_mappableTextureInfo->TryWaitLastFence( doNotWait ) )
        return false;
        
    m_currentMapType = mapType;

    assert( m_mappedData.size() == m_mappableTextureInfo->NumSubresources );

    assert( m_type != vaTextureType::Texture3D ); // just not supported yet but can be made to work

    HRESULT hr;
    for( int i = 0; i < m_mappableTextureInfo->NumSubresources; i++ )
    {
        void * dataPtr;
        int64 lockDataSize = m_mappableTextureInfo->Layouts[i].Footprint.Height * m_mappableTextureInfo->Layouts[i].Footprint.RowPitch;
        // this is what we used when the resource was created based on GetCopyableFootprints directly but now we make sure size is aligned up to RowPitch so this is no longer needed
        // // compute lockable data size as (Height-1)*RowPitch plus one RowSizeInBytes as this is how much we're actually reading.
        // int64 lockDataSize = (m_mappableTextureInfo->Layouts[i].Footprint.Height-1) * m_mappableTextureInfo->Layouts[i].Footprint.RowPitch + m_mappableTextureInfo->RowSizesInBytes[i];
        D3D12_RANGE range = {0,0};
        if( mapType == vaResourceMapType::Read )
            range.End = lockDataSize;

        V( m_resource->Map( i, &range, &dataPtr ) );
        if( SUCCEEDED(hr) )
        {
            m_mappedData[i].Buffer          = (byte*)dataPtr;
            assert( m_mappedData[i].SizeX == (int)m_mappableTextureInfo->Layouts[i].Footprint.Width );
            assert( m_mappedData[i].SizeY == (int)m_mappableTextureInfo->Layouts[i].Footprint.Height );
            m_mappedData[i].RowPitch        = (int)m_mappableTextureInfo->Layouts[i].Footprint.RowPitch;
            //m_mappedData[i].DepthPitch      = m_mappedData[i].RowPitch * m_mappableTextureInfo->Layouts[i].Footprint.Depth;
            m_mappedData[i].SizeInBytes     = lockDataSize;
            
            assert( m_mappedData[i].BytesPerPixel == m_mappableTextureInfo->RowSizesInBytes[i] / m_mappableTextureInfo->Layouts[i].Footprint.Width );
        }
        else
        {
            assert( i==0 ); // this is bad, if we need to handle it look at vaTextureDX11 for how to unroll
            return false;
        }
    }
    m_isMapped = true;
    return true;
}

void vaTextureDX12::InternalUnmap( )
{
    assert( m_isMapped );
    if( !m_isMapped )
        return;
    assert( m_currentMapType != vaResourceMapType::None );
    if( m_currentMapType == vaResourceMapType::None )
        return;

    D3D12_RANGE range = {0,0};
    for( int i = 0; i < m_mappedData.size(); i++ )
    {
        auto & cpuSubresource = m_mappedData[ i ];
        assert( cpuSubresource.Buffer != nullptr );    // not fatal, checked below but still unexpected, indicating a bug
        cpuSubresource; // unreferenced in Release

        // unmap and if we're writing use null as the second parameter that indicates that the whole thing could have been written
        // (https://docs.microsoft.com/en-us/windows/desktop/api/d3d12/nf-d3d12-id3d12resource-unmap)
        m_resource->Unmap( i, (m_currentMapType == vaResourceMapType::Write != 0 )?(nullptr):(&range) );
        m_mappedData[i].Buffer      = nullptr;
        m_mappedData[i].SizeInBytes = 0;
        m_mappedData[i].RowPitch    = 0;
        m_mappedData[i].DepthPitch  = 0;
    }
    m_isMapped = false;
    m_currentMapType = vaResourceMapType::None;
}

void vaTextureDX12::ResolveSubresource( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & dstResource, uint dstSubresource, uint srcSubresource, vaResourceFormat format )
{
    if( format == vaResourceFormat::Automatic )
        format = GetResourceFormat();

    ID3D12Resource * dstResourceDX12 = AsDX12(*dstResource).GetResource();

    // transition to proper resource states
    TransitionResource( AsDX12(renderContext), D3D12_RESOURCE_STATE_RESOLVE_SOURCE );
    AsDX12(*dstResource).TransitionResource( AsDX12(renderContext), D3D12_RESOURCE_STATE_RESOLVE_DEST );

    AsDX12( renderContext ).GetCommandList()->ResolveSubresource( dstResourceDX12, dstSubresource, GetResource(), srcSubresource, DXGIFormatFromVA( format ) );

    // manually transitioning states below means we might mess up the render target states cache
    AsDX12( renderContext ).ResetCachedOutputs( );
}

// void vaTextureDX12::UpdateSubresource( vaRenderDeviceContext & renderContext, int dstSubresourceIndex, const vaBoxi & dstBox, void * srcData, int srcDataRowPitch, int srcDataDepthPitch )
// {
//     assert( false ); // not implemented - probably best to see & upgrade InternalUpdate to make this work
//     renderContext; dstSubresourceIndex; dstBox; srcData; srcDataRowPitch; srcDataDepthPitch;
// #if 0
//     ID3D11DeviceContext * dx11Context = renderContext.SafeCast<vaRenderDeviceContextDX11*>( )->GetDXContext( );
// 
//     D3D11_BOX d3d11box = { (UINT)dstBox.left, (UINT)dstBox.top, (UINT)dstBox.front, (UINT)dstBox.right, (UINT)dstBox.bottom, (UINT)dstBox.back };
//     dx11Context->UpdateSubresource( m_resource, (UINT)dstSubresourceIndex, &d3d11box, srcData, srcDataRowPitch, srcDataDepthPitch );
// #endif
// }

shared_ptr<vaTexture> vaTextureDX12::TryCompress( )
{
    // Already compressed?
    if( (m_resourceFormat >= vaResourceFormat::BC1_TYPELESS && m_resourceFormat <= vaResourceFormat::BC5_SNORM)
        || (m_resourceFormat >= vaResourceFormat::BC6H_TYPELESS && m_resourceFormat <= vaResourceFormat::BC7_UNORM_SRGB) )
        return nullptr;

    DXGI_FORMAT                 destinationFormat;
    vaTextureContentsType       destinationContentsType = m_contentsType;
    DirectX::TEX_COMPRESS_FLAGS compressFlags       = DirectX::TEX_COMPRESS_DEFAULT; 

#ifdef _OPENMP
    compressFlags |= DirectX::TEX_COMPRESS_PARALLEL;
#endif

    // normals
    if( ( m_contentsType == vaTextureContentsType::NormalsXYZ_UNORM ) || ( m_contentsType == vaTextureContentsType::NormalsXY_UNORM ) )
    {
        if( ( m_resourceFormat == vaResourceFormat::R8G8_UNORM ) || ( m_resourceFormat == vaResourceFormat::R8G8B8A8_UNORM ) || ( m_resourceFormat == vaResourceFormat::B8G8R8A8_UNORM ) || ( m_resourceFormat == vaResourceFormat::B8G8R8X8_UNORM ) )
        {
            destinationFormat = DXGI_FORMAT_BC5_UNORM;
            destinationContentsType = vaTextureContentsType::NormalsXY_UNORM;
        }
        else
        {
            // format might be ok just not tested - try out and see
            assert( false );
            return nullptr;
        }
    }
    else if( m_contentsType == vaTextureContentsType::GenericColor )
    {
        if( ( m_resourceFormat == vaResourceFormat::R8G8B8A8_UNORM_SRGB ) || ( m_resourceFormat == vaResourceFormat::B8G8R8A8_UNORM_SRGB ) )
        {
            destinationFormat = DXGI_FORMAT_BC7_UNORM_SRGB;
        }
        else
        {
            // format might be ok just not tested - try out and see
            assert( false );
            return nullptr;
        }
    } 
    else if( m_contentsType == vaTextureContentsType::GenericLinear )
    {
        if( ( m_resourceFormat == vaResourceFormat::R8G8B8A8_UNORM ) || ( m_resourceFormat == vaResourceFormat::B8G8R8A8_UNORM ) )
        {
            destinationFormat = DXGI_FORMAT_BC7_UNORM;
        }
        else
        {
            // format might be ok just not tested - try out and see
            assert( false );
            return nullptr;
        }
    }
    else if( m_contentsType == vaTextureContentsType::SingleChannelLinearMask )
    {
        if( ( m_resourceFormat == vaResourceFormat::R8G8B8A8_UNORM ) || ( m_resourceFormat == vaResourceFormat::B8G8R8A8_UNORM ) || ( m_resourceFormat == vaResourceFormat::R8_UNORM ) )
        {
            destinationFormat = DXGI_FORMAT_BC4_UNORM;
        }
        else
        {
            // format might be ok just not tested - try out and see
            assert( false );
            return nullptr;
        }
    }
    else
    {
        // not supported yet!
        assert( false );
        return nullptr;
    }


    DirectX::ScratchImage scratchImage;
    DirectX::ScratchImage compressedScratchImage;

    HRESULT hr;
    {
        hr = DirectX::CaptureTexture( AsDX12( GetRenderDevice() ).GetCommandQueue().Get(), m_resource.Get(), ( m_flags & ( vaTextureFlags::Cubemap | vaTextureFlags::CubemapButArraySRV ) ) != 0, scratchImage, m_rsth.RSTHGetCurrentState( ), m_rsth.RSTHGetCurrentState( ) );
        if( !SUCCEEDED( hr ) )
            { assert( false );  return nullptr; }
    }

    hr = DirectX::Compress( scratchImage.GetImages( ), scratchImage.GetImageCount( ), scratchImage.GetMetadata(), destinationFormat, compressFlags, DirectX::TEX_THRESHOLD_DEFAULT, compressedScratchImage );
        if( !SUCCEEDED( hr ) )
            { assert( false );  return nullptr; }
    
    DirectX::Blob blob;
    hr = DirectX::SaveToDDSMemory( compressedScratchImage.GetImages( ), compressedScratchImage.GetImageCount( ), compressedScratchImage.GetMetadata( ), DirectX::DDS_FLAGS_NONE, blob );
        if( !SUCCEEDED( hr ) )
            { assert( false );  return nullptr; }

    // drop unsupported bind flags if any - doesn't work with CreateFromImageBuffer
    vaResourceBindSupportFlags bindFlags = GetBindSupportFlags( ) & ~(vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::UnorderedAccess | vaResourceBindSupportFlags::DepthStencil );
    return vaTexture::CreateFromImageBuffer( GetRenderDevice(), blob.GetBufferPointer(), blob.GetBufferSize(), vaTextureLoadFlags::Default, bindFlags, destinationContentsType );
}

vaTextureDX12::MappableTextureInfo::MappableTextureInfo( vaRenderDeviceDX12 & device, const D3D12_RESOURCE_DESC & resDesc ) 
    : NumSubresources( resDesc.MipLevels * resDesc.DepthOrArraySize ), Layouts( NumSubresources ), NumRows( NumSubresources ), RowSizesInBytes( NumSubresources ), CopyableResDesc( resDesc )
{ 
    // get memory size and layout required for later access
    device.GetPlatformDevice()->GetCopyableFootprints( &resDesc, 0, NumSubresources, 0, &Layouts[0], &NumRows[0], &RowSizesInBytes[0], &TotalSizeInBytes );

    // ok - let's not save on total memory needed in the last row here if pitch is bigger than needed - simplifies mapping logic and there's no downsides except using more memory
    uint64 safeTotalSizeInBytes = 0;
    for( int i = 0; i < NumSubresources; i++ )
        safeTotalSizeInBytes += Layouts[0].Footprint.Height * Layouts[0].Footprint.RowPitch * Layouts[0].Footprint.Depth;
    assert( safeTotalSizeInBytes >= TotalSizeInBytes );
    TotalSizeInBytes = safeTotalSizeInBytes;

    HRESULT hr;
    V( device.GetPlatformDevice()->CreateFence( GPULastFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&GPUFence) ) );
    V( GPUFence->SetName( L"MappableTextureFence" ) );

    // Create an event handle to use for frame synchronization.
    GPUFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if( GPUFenceEvent == nullptr )
    {
        V( HRESULT_FROM_WIN32(GetLastError()) );
    }
}
vaTextureDX12::MappableTextureInfo::~MappableTextureInfo( ) 
{ 
    if( GPUFenceEvent != INVALID_HANDLE_VALUE ) CloseHandle(GPUFenceEvent); 
}

void vaTextureDX12::MappableTextureInfo::SignalNextFence( vaRenderDeviceDX12 & device )
{
    HRESULT hr;
    GPULastFenceValue++;
    V( device.GetCommandQueue()->Signal(GPUFence.Get(), GPULastFenceValue) );
}

bool vaTextureDX12::MappableTextureInfo::TryWaitLastFence( bool doNotWait )
{
    HRESULT hr;
    uint64 fenceCompletedValue = GPUFence->GetCompletedValue();
    if( fenceCompletedValue < GPULastFenceValue )
    {
        if( doNotWait )
            return false;
        V( GPUFence->SetEventOnCompletion( GPULastFenceValue, GPUFenceEvent ) );
#ifndef _DEBUG
        static int warnEveryOnceInAWhile = 0;
        if( warnEveryOnceInAWhile <= 0 )
        {
            warnEveryOnceInAWhile = 100000;
            VA_LOG( "Note, vaTextureDX12::MappableTextureInfo::TryWaitLastFence is waiting on fence, possible performance problem if happens frequently" ); // not ideal, we should rather WaitForSingleObjectEx for a shorter period and if it fails warn and then wait INFINITE
        }
        warnEveryOnceInAWhile--;
#endif
        WaitForSingleObjectEx(GPUFenceEvent, INFINITE, FALSE);
    }
    return true;
}

void vaTextureDX12::InternalUpdateSubresources( uint32 firstSubresource, /*const*/ std::vector<vaTextureSubresourceData> & subresources )
{
    HRESULT hr;
    uint32 numSubresources  = (uint32)subresources.size();
    uint64 requiredSize     = 0;
    uint64 memToAlloc = static_cast<uint64>(sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT) + sizeof(uint32) + sizeof(uint64)) * numSubresources;
    uint64 intermediateOffset = 0;
    if( memToAlloc > SIZE_MAX )
        { assert( false ); return; }
    
    // used throughout this function to provide memory for GetCopyableFootprints; moved to the lambda for later use and disposal along with pointers referencing into the memory
    std::shared_ptr<byte> tmpMem( new byte[memToAlloc], std::default_delete<byte[]>() );
    // std::unique_ptr<byte[]> tmpMem = std::make_unique<byte[]>( memToAlloc ); // <- this doesn't work with lambdas, not sure why

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts = reinterpret_cast<D3D12_PLACED_SUBRESOURCE_FOOTPRINT*>(tmpMem.get());
    uint64* pRowSizesInBytes = reinterpret_cast<uint64*>(pLayouts + numSubresources);
    uint32* pNumRows = reinterpret_cast<uint32*>(pRowSizesInBytes + numSubresources);
    
    D3D12_RESOURCE_DESC destinationDesc = GetResource()->GetDesc();
    AsDX12( GetRenderDevice() ).GetPlatformDevice( )->GetCopyableFootprints(&destinationDesc, firstSubresource, numSubresources, intermediateOffset, pLayouts, pNumRows, pRowSizesInBytes, &requiredSize);

    const UINT64 uploadBufferSize = GetRequiredIntermediateSize( GetResource(), firstSubresource, numSubresources );

    ComPtr<ID3D12Resource> intermediateResource;
    V( AsDX12( GetRenderDevice() ).GetPlatformDevice( )->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer( uploadBufferSize ),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS( &intermediateResource ) ) );
    intermediateResource->SetName( L"vaTextureDX12_upload" );

    // Minor validation
    D3D12_RESOURCE_DESC intermediateDesc = intermediateResource->GetDesc();
    if (intermediateDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || 
        intermediateDesc.Width < requiredSize + pLayouts[0].Offset || requiredSize > (SIZE_T)-1 || 
        (destinationDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER && 
            (firstSubresource != 0 || numSubresources != 1)))
        { assert( false ); return; }

    // Upload to intermediate resource
    {
        BYTE* pData;
        // int64 lockDataSize = m_mappableTextureInfo->Layouts[i].Footprint.Height * m_mappableTextureInfo->Layouts[i].Footprint.RowPitch;
        // D3D12_RANGE range = {0,lockDataSize}; 
        hr = intermediateResource->Map(0, NULL, reinterpret_cast<void**>(&pData));
        if (FAILED(hr))
            { assert( false ); return; }
        
        for (UINT i = 0; i < numSubresources; ++i)
        {
            if (pRowSizesInBytes[i] > (SIZE_T)-1)
                { assert( false ); return; }
            D3D12_MEMCPY_DEST DestData = { pData + pLayouts[i].Offset, pLayouts[i].Footprint.RowPitch, SIZE_T(pLayouts[i].Footprint.RowPitch) * SIZE_T(pNumRows[i]) };
            MemcpySubresource(&DestData, reinterpret_cast<D3D12_SUBRESOURCE_DATA*>(&subresources[i]), (SIZE_T)pRowSizesInBytes[i], pNumRows[i], pLayouts[i].Footprint.Depth);
        }
        intermediateResource->Unmap(0, NULL);
    }

    weak_ptr<vaTexture*> weakThis = m_smartThis;
    auto updateLambda = [ weakThis, dimension = destinationDesc.Dimension, intermediateResource, pLayouts, firstSubresource, numSubresources, tmpMem ]( vaRenderDeviceDX12 & device )
    {
        shared_ptr<vaTexture*> smartThis = weakThis.lock( );
        if( smartThis != nullptr )
        {
            vaTextureDX12* thisDX12 = AsDX12( *smartThis );
            thisDX12->TransitionResource( AsDX12( *device.GetMainContext( ) ), D3D12_RESOURCE_STATE_COPY_DEST );

            auto commandList = AsDX12( *device.GetMainContext( ) ).GetCommandList( ).Get( );
            if( dimension == D3D12_RESOURCE_DIMENSION_BUFFER )
            {
                commandList->CopyBufferRegion( thisDX12->GetResource(), 0, intermediateResource.Get(), pLayouts[0].Offset, pLayouts[0].Footprint.Width);
            }
            else
            {
                for (UINT i = 0; i < numSubresources; ++i)
                {
                    CD3DX12_TEXTURE_COPY_LOCATION Dst(thisDX12->GetResource(), i + firstSubresource);
                    CD3DX12_TEXTURE_COPY_LOCATION Src(intermediateResource.Get(), pLayouts[i]);
                    commandList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
                }
            }
            // manually transitioning states below means we might mess up the render target states cache
            AsDX12( *device.GetMainContext( ) ).ResetCachedOutputs( );
        }

        auto nonConstIntermediateResource = intermediateResource;
        AsDX12( device ).SafeReleaseAfterCurrentGPUFrameDone( nonConstIntermediateResource, false );
        //tmpMem.reset(); //<- just let it go out of scope and self-destruct
    };

    if( !GetRenderDevice( ).IsRenderThread() || GetRenderDevice( ).GetMainContext( ) == nullptr || !GetRenderDevice( ).IsFrameStarted() )
        AsDX12( GetRenderDevice( ) ).ExecuteAtBeginFrame( updateLambda );
    else
        updateLambda( AsDX12( GetRenderDevice( ) ) );
}


D3D12_CLEAR_VALUE * vaTextureDX12::GetNextCreateFastClearStatus( D3D12_CLEAR_VALUE & clearVal, vaResourceBindSupportFlags bindFlags )
{
    clearVal.Format = DXGIFormatFromVA( s_nextCreateFastClearFormat );
    s_nextCreateFastClearFormat = vaResourceFormat::Unknown;
    if( clearVal.Format != DXGI_FORMAT_UNKNOWN )
    {
        if( ( bindFlags & vaResourceBindSupportFlags::DepthStencil ) != 0 )
        {
            clearVal.DepthStencil.Depth = s_nextCreateFastClearDepthValue;
            clearVal.DepthStencil.Stencil = s_nextCreateFastClearStencilValue;
            return &clearVal;
        }
        else if( ( bindFlags & vaResourceBindSupportFlags::RenderTarget ) != 0 )
        {
            clearVal.Color[0] = s_nextCreateFastClearColorValue.x; clearVal.Color[1] = s_nextCreateFastClearColorValue.y; clearVal.Color[2] = s_nextCreateFastClearColorValue.z; clearVal.Color[3] = s_nextCreateFastClearColorValue.w;
            return &clearVal;
        }
    }
    return nullptr;
}

bool vaTextureDX12::InternalCreate1D( vaResourceFormat format, int width, int mipLevels, int arraySize, vaResourceBindSupportFlags bindFlags, vaResourceAccessFlags accessFlags, vaResourceFormat srvFormat, vaResourceFormat rtvFormat, vaResourceFormat dsvFormat, vaResourceFormat uavFormat, vaTextureFlags flags, vaTextureContentsType contentsType, const void * initialData )
{
    Initialize( bindFlags, accessFlags, format, srvFormat, rtvFormat, dsvFormat, uavFormat, flags, 0, -1, 0, -1, contentsType );

    // Describe and create a Texture1D.
    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.MipLevels           = (UINT16)mipLevels;
    textureDesc.Format              = DXGIFormatFromVA( format );
    textureDesc.Width               = width;
    textureDesc.Height              = 1;
    textureDesc.Flags               = ResourceFlagsDX12FromVA( m_bindSupportFlags );
    textureDesc.DepthOrArraySize    = (UINT16)arraySize;
    textureDesc.SampleDesc.Count    = 1;
    textureDesc.SampleDesc.Quality  = 0;
    textureDesc.Dimension           = D3D12_RESOURCE_DIMENSION_TEXTURE1D;

    D3D12_HEAP_TYPE heapType        = HeapTypeDX12FromAccessFlags( accessFlags );
    D3D12_HEAP_FLAGS heapFlags      = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;
    if( (bindFlags & vaResourceBindSupportFlags::Shared) != 0 )
        heapFlags |= D3D12_HEAP_FLAG_SHARED;

    HRESULT hr = E_FAIL;
    ID3D12Resource * resource = nullptr;
    D3D12_CLEAR_VALUE fastClearVal; 
    if( heapType == D3D12_HEAP_TYPE_DEFAULT )
    {
        V( AsDX12( GetRenderDevice() ).GetPlatformDevice()->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(heapType),
            heapFlags,
            &textureDesc,
            D3D12_RESOURCE_STATE_COMMON,
            GetNextCreateFastClearStatus( fastClearVal, bindFlags ),
            IID_PPV_ARGS(&resource) ) );
        resource->SetName( L"vaTextureDX12_1D" );
    }
    else
    {
        // all of these things are not supported for a read or write mappable texture
        if( bindFlags != vaResourceBindSupportFlags::None )                                         { assert( false ); return false; }
        if( flags != vaTextureFlags::None )                                                         { assert( false ); return false; }
        if( srvFormat != vaResourceFormat::Automatic && srvFormat != vaResourceFormat::Unknown )    { assert( false ); return false; }
        if( dsvFormat != vaResourceFormat::Automatic && dsvFormat != vaResourceFormat::Unknown )    { assert( false ); return false; }
        if( rtvFormat != vaResourceFormat::Automatic && rtvFormat != vaResourceFormat::Unknown )    { assert( false ); return false; }
        if( uavFormat != vaResourceFormat::Automatic && uavFormat != vaResourceFormat::Unknown )    { assert( false ); return false; }

        // allocate storage that describes the texture resource footprints
        m_mappableTextureInfo = std::make_shared<MappableTextureInfo>( AsDX12( GetRenderDevice() ), textureDesc );
        MappableTextureInfo & mappableInfo = *m_mappableTextureInfo;

        assert( heapType == D3D12_HEAP_TYPE_UPLOAD || heapType == D3D12_HEAP_TYPE_READBACK );

        // Create the GPU upload/download buffer.
        V( AsDX12( GetRenderDevice() ).GetPlatformDevice()->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(heapType),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(mappableInfo.TotalSizeInBytes),
            (heapType == D3D12_HEAP_TYPE_UPLOAD)?(D3D12_RESOURCE_STATE_GENERIC_READ):(D3D12_RESOURCE_STATE_COPY_DEST),
            GetNextCreateFastClearStatus( fastClearVal, bindFlags ),
            IID_PPV_ARGS(&resource)));
            resource->SetName( L"vaTextureDX12_1D" );
    }

    if( SUCCEEDED(hr) && resource != NULL )
    {
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
        if( (accessFlags & vaResourceAccessFlags::CPURead) != 0 )
            initialState = D3D12_RESOURCE_STATE_COPY_DEST;
        else if( (accessFlags & vaResourceAccessFlags::CPUWrite) != 0 )
            initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
        SetResource( resource, initialState );
        resource->Release();        // warning - after this the only thing keeping this alive is m_resource
        ProcessResource( false, true );

        // Ok, so, setting initial data is going to be a bit of a mess as we have to handle cases where it's created before the device can
        // start handling copies (or outside of frame begin/end) in which case we'll pool the updates for later execution.
        if( initialData != nullptr )
        {
            int bpp = vaResourceFormatHelpers::GetPixelSizeInBytes( m_resourceFormat );
            assert( bpp != 0 );
            if( heapType == D3D12_HEAP_TYPE_DEFAULT )
            {
                vaTextureSubresourceData textureData = { initialData, m_sizeX * bpp, m_sizeX * bpp };
                InternalUpdateSubresources( 0, std::vector<vaTextureSubresourceData>{textureData} );
            }
            else
            {
                if( InternalTryMap( vaResourceMapType::Write, false ) )
                {
                    assert( m_mappedData.size() == 1 ); // multiple subresources not supported
                    assert( m_mappedData[0].SizeInBytes >= m_sizeX * bpp );
                    memcpy( &m_mappedData[0].Buffer[0], &((byte*)initialData)[0], m_sizeX * bpp );
                    InternalUnmap();
                }
                else { assert( false ); }
            }
        }

        assert( m_accessFlags == accessFlags );

        return true;
    }
    else
    {
        assert( false );
        return false;
    }
}

bool vaTextureDX12::InternalCreate2D( vaResourceFormat format, int width, int height, int mipLevels, int arraySize, int sampleCount, vaResourceBindSupportFlags bindFlags, vaResourceAccessFlags accessFlags, vaResourceFormat srvFormat, vaResourceFormat rtvFormat, vaResourceFormat dsvFormat, vaResourceFormat uavFormat, vaTextureFlags flags, vaTextureContentsType contentsType, const void * initialData, int initialDataRowPitch )
{
    Initialize( bindFlags, accessFlags, format, srvFormat, rtvFormat, dsvFormat, uavFormat, flags, 0, -1, 0, -1, contentsType );

    // Describe and create a Texture2D.
    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.MipLevels           = (UINT16)mipLevels;
    textureDesc.Format              = DXGIFormatFromVA( format );
    textureDesc.Width               = width;
    textureDesc.Height              = height;
    textureDesc.Flags               = ResourceFlagsDX12FromVA( m_bindSupportFlags );
    textureDesc.DepthOrArraySize    = (UINT16)arraySize;
    textureDesc.SampleDesc.Count    = sampleCount;
    textureDesc.SampleDesc.Quality  = 0;
    textureDesc.Dimension           = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    D3D12_HEAP_TYPE heapType        = HeapTypeDX12FromAccessFlags( accessFlags );
    D3D12_HEAP_FLAGS heapFlags      = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;

    // umm, getting "D3D12 ERROR: ID3D12Device::CreateCommittedResource: D3D12_HEAP_FLAGS has recognized flags set. The value is 0x400, and the following flags are unrecognized: 0x400. [ STATE_CREATION ERROR #639: CREATERESOURCEANDHEAP_UNRECOGNIZEDHEAPMISCFLAGS]" on certain hardware
    //if( (bindFlags & vaResourceBindSupportFlags::UnorderedAccess) != 0 )
    //    heapFlags |= D3D12_HEAP_FLAG_ALLOW_SHADER_ATOMICS;
    if( (bindFlags & vaResourceBindSupportFlags::Shared) != 0 )
        heapFlags |= D3D12_HEAP_FLAG_SHARED;

    HRESULT hr = E_FAIL;
    ID3D12Resource * resource = nullptr;
    D3D12_CLEAR_VALUE fastClearVal;
    if( heapType == D3D12_HEAP_TYPE_DEFAULT )
    {
        V( AsDX12( GetRenderDevice() ).GetPlatformDevice()->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(heapType),
            heapFlags,
            &textureDesc,
            D3D12_RESOURCE_STATE_COMMON,
            GetNextCreateFastClearStatus( fastClearVal, bindFlags ),
            IID_PPV_ARGS(&resource) ) );
        resource->SetName( L"vaTextureDX12_2D" );
    }
    else
    {
        // all of these things are not supported for a read or write mappable texture
        if( sampleCount != 1 )                                                                      { assert( false ); return false; }
        if( bindFlags != vaResourceBindSupportFlags::None )                                         { assert( false ); return false; }
        if( flags != vaTextureFlags::None )                                                         { assert( false ); return false; }
        if( srvFormat != vaResourceFormat::Automatic && srvFormat != vaResourceFormat::Unknown )    { assert( false ); return false; }
        if( dsvFormat != vaResourceFormat::Automatic && dsvFormat != vaResourceFormat::Unknown )    { assert( false ); return false; }
        if( rtvFormat != vaResourceFormat::Automatic && rtvFormat != vaResourceFormat::Unknown )    { assert( false ); return false; }
        if( uavFormat != vaResourceFormat::Automatic && uavFormat != vaResourceFormat::Unknown )    { assert( false ); return false; }

        // allocate storage that describes the texture resource footprints
        m_mappableTextureInfo = std::make_shared<MappableTextureInfo>( AsDX12( GetRenderDevice() ), textureDesc );
        MappableTextureInfo & mappableInfo = *m_mappableTextureInfo;

        assert( heapType == D3D12_HEAP_TYPE_UPLOAD || heapType == D3D12_HEAP_TYPE_READBACK );

        // Create the GPU upload/download buffer.
        V( AsDX12( GetRenderDevice() ).GetPlatformDevice()->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(heapType),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(mappableInfo.TotalSizeInBytes),
            (heapType == D3D12_HEAP_TYPE_UPLOAD)?(D3D12_RESOURCE_STATE_GENERIC_READ):(D3D12_RESOURCE_STATE_COPY_DEST),
            GetNextCreateFastClearStatus( fastClearVal, bindFlags ),
            IID_PPV_ARGS(&resource)));
        resource->SetName( L"vaTextureDX12_2D" );
    }

    if( SUCCEEDED(hr) && resource != NULL )
    {
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
        if( (accessFlags & vaResourceAccessFlags::CPURead) != 0 )
            initialState = D3D12_RESOURCE_STATE_COPY_DEST;
        else if( (accessFlags & vaResourceAccessFlags::CPUWrite) != 0 )
            initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
        SetResource( resource, initialState );
        resource->Release();        // warning - after this the only thing keeping this alive is m_resource
        ProcessResource( false, true );

        // Ok, so, setting initial data is going to be a bit of a mess as we have to handle cases where it's created before the device can
        // start handling copies (or outside of frame begin/end) in which case we'll pool the updates for later execution.
        if( initialData != nullptr )
        {
            if( heapType == D3D12_HEAP_TYPE_DEFAULT )
            {
                vaTextureSubresourceData textureData = { initialData, initialDataRowPitch, initialDataRowPitch * m_sizeY };
                InternalUpdateSubresources( 0, std::vector<vaTextureSubresourceData>{textureData} );
            }
            else
            {
                if( InternalTryMap( vaResourceMapType::Write, false ) )
                {
                    assert( m_mappedData.size() == 1 ); // multiple subresources not supported
                    for( int y = 0; y < m_mappedData[0].SizeY; y++ )
                        memcpy( &m_mappedData[0].Buffer[y * m_mappedData[0].RowPitch], &((byte*)initialData)[y * initialDataRowPitch], m_mappedData[0].RowPitch );
                    InternalUnmap();
                }
                else { assert( false ); }
            }
        }

        assert( m_accessFlags == accessFlags );

        return true;
    }
    else
    {
        assert( false );
        return false;
    }
}

bool vaTextureDX12::InternalCreate3D( vaResourceFormat format, int width, int height, int depth, int mipLevels, vaResourceBindSupportFlags bindFlags, vaResourceAccessFlags accessFlags, vaResourceFormat srvFormat, vaResourceFormat rtvFormat, vaResourceFormat dsvFormat, vaResourceFormat uavFormat, vaTextureFlags flags, vaTextureContentsType contentsType, const void * initialData, int initialDataRowPitch, int initialDataSlicePitch )
{
    Initialize( bindFlags, accessFlags, format, srvFormat, rtvFormat, dsvFormat, uavFormat, flags, 0, -1, 0, -1, contentsType );

    // Describe and create a Texture2D.
    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.MipLevels           = (UINT16)mipLevels;
    textureDesc.Format              = DXGIFormatFromVA( format );
    textureDesc.Width               = width;
    textureDesc.Height              = height;
    textureDesc.DepthOrArraySize    = (UINT16)depth;
    textureDesc.Flags               = ResourceFlagsDX12FromVA( m_bindSupportFlags );
    textureDesc.SampleDesc.Count    = 1;
    textureDesc.SampleDesc.Quality  = 0;
    textureDesc.Dimension           = D3D12_RESOURCE_DIMENSION_TEXTURE3D;

    D3D12_HEAP_TYPE heapType        = HeapTypeDX12FromAccessFlags( accessFlags );
    D3D12_HEAP_FLAGS heapFlags      = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;

    if( (bindFlags & vaResourceBindSupportFlags::Shared) != 0 )
        heapFlags |= D3D12_HEAP_FLAG_SHARED;

    HRESULT hr = E_FAIL;
    ID3D12Resource * resource = nullptr;
    D3D12_CLEAR_VALUE fastClearVal;
    if( heapType == D3D12_HEAP_TYPE_DEFAULT )
    {
        V( AsDX12( GetRenderDevice() ).GetPlatformDevice()->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(heapType),
            heapFlags,
            &textureDesc,
            D3D12_RESOURCE_STATE_COMMON,
            GetNextCreateFastClearStatus( fastClearVal, bindFlags ),
            IID_PPV_ARGS(&resource) ) );
        resource->SetName( L"vaTextureDX12_3D" );
    }
    else
    {
        // all of these things are not supported for a read or write mappable texture
        if( bindFlags != vaResourceBindSupportFlags::None )                                         { assert( false ); return false; }
        if( flags != vaTextureFlags::None )                                                         { assert( false ); return false; }
        if( srvFormat != vaResourceFormat::Automatic && srvFormat != vaResourceFormat::Unknown )    { assert( false ); return false; }
        if( dsvFormat != vaResourceFormat::Automatic && dsvFormat != vaResourceFormat::Unknown )    { assert( false ); return false; }
        if( rtvFormat != vaResourceFormat::Automatic && rtvFormat != vaResourceFormat::Unknown )    { assert( false ); return false; }
        if( uavFormat != vaResourceFormat::Automatic && uavFormat != vaResourceFormat::Unknown )    { assert( false ); return false; }

        // allocate storage that describes the texture resource footprints
        m_mappableTextureInfo = std::make_shared<MappableTextureInfo>( AsDX12( GetRenderDevice() ), textureDesc );
        MappableTextureInfo & mappableInfo = *m_mappableTextureInfo;

        assert( heapType == D3D12_HEAP_TYPE_UPLOAD || heapType == D3D12_HEAP_TYPE_READBACK );

        assert( false ); // codepath never tested, please step through and make sure everything's ok.

        // Create the GPU upload/download buffer.
        V( AsDX12( GetRenderDevice() ).GetPlatformDevice()->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(heapType),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(mappableInfo.TotalSizeInBytes),
            (heapType == D3D12_HEAP_TYPE_UPLOAD)?(D3D12_RESOURCE_STATE_GENERIC_READ):(D3D12_RESOURCE_STATE_COPY_DEST),
            GetNextCreateFastClearStatus( fastClearVal, bindFlags ),
            IID_PPV_ARGS(&resource)));
        resource->SetName( L"vaTextureDX12_3D" );
    }

    if( SUCCEEDED(hr) && resource != NULL )
    {
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
        if( (accessFlags & vaResourceAccessFlags::CPURead) != 0 )
            initialState = D3D12_RESOURCE_STATE_COPY_DEST;
        else if( (accessFlags & vaResourceAccessFlags::CPUWrite) != 0 )
            initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
        SetResource( resource, initialState );
        resource->Release();        // warning - after this the only thing keeping this alive is m_resource
        ProcessResource( false, true );

        // Ok, so, setting initial data is going to be a bit of a mess as we have to handle cases where it's created before the device can
        // start handling copies (or outside of frame begin/end) in which case we'll pool the updates for later execution.
        if( initialData != nullptr )
        {
            if( heapType == D3D12_HEAP_TYPE_DEFAULT )
            {
                vaTextureSubresourceData textureData = { initialData, initialDataRowPitch, initialDataRowPitch * m_sizeY };
                InternalUpdateSubresources( 0, std::vector<vaTextureSubresourceData>{textureData} );
            }
            else
            {
                if( InternalTryMap( vaResourceMapType::Write, false ) )
                {
                    assert( m_mappedData.size() == 1 ); // multiple subresources not supported
                    for( int z = 0; z < m_mappedData[0].SizeZ; z++ )
                        for( int y = 0; y < m_mappedData[0].SizeY; y++ )
                            memcpy( &m_mappedData[0].Buffer[y * m_mappedData[0].RowPitch + z * m_mappedData[0].DepthPitch], &((byte*)initialData)[y * initialDataRowPitch + z * initialDataSlicePitch], m_mappedData[0].RowPitch );
                    InternalUnmap();
                }
                else { assert( false ); }
            }
        }

        assert( m_accessFlags == accessFlags );

        return true;
    }
    else
    {
        assert( false );
        return false;
    }
}

shared_ptr<vaTexture> vaTextureDX12::CreateViewInternal( const shared_ptr<vaTexture> & thisTexture, vaResourceBindSupportFlags bindFlags, vaResourceFormat srvFormat, vaResourceFormat rtvFormat, vaResourceFormat dsvFormat, vaResourceFormat uavFormat, vaTextureFlags flags, int viewedMipSliceMin, int viewedMipSliceCount, int viewedArraySliceMin, int viewedArraySliceCount )
{
    assert( thisTexture.get() == static_cast<vaTexture*>(this) );
    vaTextureDX12 * origDX12Texture = this->SafeCast<vaTextureDX12*>( );

    // -1 means all above min
    if( viewedMipSliceCount == -1 )
        viewedMipSliceCount = this->GetMipLevels() - viewedMipSliceMin;
    if( viewedArraySliceCount == -1 )
        viewedArraySliceCount = this->GetArrayCount() - viewedArraySliceMin;
    assert( viewedMipSliceCount > 0 );
    assert( viewedArraySliceCount > 0 );

    assert( viewedMipSliceMin >= 0 && viewedMipSliceMin < this->GetMipLevels() );
    assert( (viewedMipSliceMin+viewedMipSliceCount) > 0 && (viewedMipSliceMin+viewedMipSliceCount) <= this->GetMipLevels() );
    assert( viewedArraySliceMin >= 0 && viewedArraySliceMin < this->GetArrayCount( ) );
    assert( ( viewedArraySliceMin + viewedArraySliceCount ) > 0 && ( viewedArraySliceMin + viewedArraySliceCount ) <= this->GetArrayCount( ) );

    ID3D12Resource * resource = origDX12Texture->GetResource( );

    if( resource == NULL )
    {
        assert( false ); 
        return NULL;
    }

    // Can't request additional binding flags that were not supported in the original texture
    vaResourceBindSupportFlags origFlags = origDX12Texture->GetBindSupportFlags();
    assert( ((~origFlags) & bindFlags) == 0 );
    origFlags; // unreferenced in Release

    shared_ptr<vaTexture> newTexture = GetRenderDevice().CreateModule<vaTexture, vaTextureConstructorParams>( vaCore::GUIDCreate( ) );
    AsDX12( *newTexture ).Initialize( bindFlags, this->GetAccessFlags(), this->GetResourceFormat(), srvFormat, rtvFormat, dsvFormat, uavFormat, this->GetFlags(), viewedMipSliceMin, viewedMipSliceCount, viewedArraySliceMin, viewedArraySliceCount, this->GetContentsType() );
    
    // track the original & keep it alive (not needed in DX since DX resources have reference counting and will stay alive, but it might be useful for other API implementations and/or debugging purposes)
    AsDX12( *newTexture ).SetViewedOriginal( thisTexture );

    vaTextureDX12 & newDX12Texture = AsDX12(*newTexture);
    newDX12Texture.SetResource( resource, D3D12_RESOURCE_STATE_COMMON );
    newDX12Texture.m_flags = flags;            // override flags (currently only used for cubemaps)
    newDX12Texture.ProcessResource( true, true );

    // since we've used nonAllBindViewsNeeded==true in ProcessResource above, we have to manually check if the binds requested for this specific view were correctly created
    if( ( bindFlags & vaResourceBindSupportFlags::ShaderResource ) != 0 )
        { assert( srvFormat != vaResourceFormat::Unknown ); assert( newDX12Texture.m_srv.IsCreated() ); }
    else
        { assert( srvFormat == vaResourceFormat::Unknown || srvFormat == vaResourceFormat::Automatic ); assert( !newDX12Texture.m_srv.IsCreated() ); }
    if( ( bindFlags & vaResourceBindSupportFlags::RenderTarget ) != 0 )
        { assert( rtvFormat != vaResourceFormat::Unknown ); assert( newDX12Texture.m_rtv.IsCreated() ); }
    else
        { assert( rtvFormat == vaResourceFormat::Unknown || rtvFormat == vaResourceFormat::Automatic ); assert( !newDX12Texture.m_rtv.IsCreated() ); }
    if( ( bindFlags & vaResourceBindSupportFlags::DepthStencil ) != 0 )
        { assert( dsvFormat != vaResourceFormat::Unknown ); assert( newDX12Texture.m_dsv.IsCreated() ); }
    else
        { assert( dsvFormat == vaResourceFormat::Unknown || dsvFormat == vaResourceFormat::Automatic ); assert( !newDX12Texture.m_dsv.IsCreated() ); }
    if( ( bindFlags & vaResourceBindSupportFlags::UnorderedAccess ) != 0 )
        { assert( uavFormat != vaResourceFormat::Unknown ); assert( newDX12Texture.m_uav.IsCreated() ); }
    else
        { assert( uavFormat == vaResourceFormat::Unknown || uavFormat == vaResourceFormat::Automatic ); assert( !newDX12Texture.m_uav.IsCreated() ); }

    return newTexture;
}

bool vaTextureDX12::SaveToDDSFile( vaRenderDeviceContext & renderContext, const wstring & path )
{
    assert( GetRenderDevice( ).IsFrameStarted( ) ); // probably just add 'if( GetRenderDevice( ).IsFrameStarted( ) )' before flush?
    AsFullDX12(renderContext).Flush();  // this should be done to make sure resource states are matching
    
    assert( m_overrideView == nullptr );
    assert( m_viewedOriginal == nullptr );
    HRESULT hr = DirectX::SaveDDSTextureToFile( AsDX12(renderContext.GetRenderDevice()).GetCommandQueue().Get(), GetResource( ), path.c_str(), m_rsth.RSTHGetCurrentState(), m_rsth.RSTHGetCurrentState() );
    
    assert( GetRenderDevice( ).IsFrameStarted( ) ); // probably just add 'if( GetRenderDevice( ).IsFrameStarted( ) )' before flush?
    AsFullDX12(renderContext).Flush();  // this should be done to make sure resource states are matching

    if( !SUCCEEDED( hr ) )
    {
        VA_LOG_ERROR( L"vaPostProcessDX12::SaveTextureToDDSFile ('%s') failed with hr %x !", path.c_str(), hr );
        return false;
    }
    else
    {
        return true;
    }
}

bool vaTextureDX12::SaveToPNGFile( vaRenderDeviceContext & renderContext, const wstring & path )
{
    if( IsView() )
        return GetViewedOriginal()->SaveToPNGFile( renderContext, path );
    assert( GetRenderDevice( ).IsFrameStarted( ) ); // probably just add 'if( GetRenderDevice( ).IsFrameStarted( ) )' before flush?
    AsFullDX12(renderContext).Flush();  // this should be done to make sure resource states are matching
    assert( m_overrideView == nullptr );  
    assert( m_viewedOriginal == nullptr );  // if( m_viewedOriginal ) m_viewedOriginal->SaveToPNGFile ... ?
    HRESULT hr = DirectX::SaveWICTextureToFile( AsDX12(renderContext.GetRenderDevice()).GetCommandQueue().Get(), GetResource( ), GUID_ContainerFormatPng, path.c_str(), m_rsth.RSTHGetCurrentState(), m_rsth.RSTHGetCurrentState() );
    assert( GetRenderDevice( ).IsFrameStarted( ) ); // probably just add 'if( GetRenderDevice( ).IsFrameStarted( ) )' before flush?
    AsFullDX12(renderContext).Flush();  // this should be done to make sure resource states are matching
    if( !SUCCEEDED( hr ) )
    {
        VA_LOG_ERROR( L"vaPostProcessDX12::SaveTextureToPNGFile ('%s') failed with hr %x !", path.c_str(), hr );
        return false;
    }
    else
    {
        return true;
    }
}

void vaTextureDX12::UpdateSubresources( vaRenderDeviceContext & renderContext, uint32 firstSubresource, /*const*/ std::vector<vaTextureSubresourceData> & subresources )
{
    assert( GetRenderDevice( ).IsRenderThread( ) );
    if( GetRenderDevice( ).GetMainContext( ) != &renderContext )
    {
        assert( false ); // must be main context
        return;
    }

    InternalUpdateSubresources( firstSubresource, subresources );
}

bool vaTextureDX12::TryMap( vaRenderDeviceContext & renderContext, vaResourceMapType mapType, bool doNotWait )
{
    assert( GetRenderDevice( ).IsRenderThread( ) );
    assert( GetRenderDevice( ).IsFrameStarted( ) );
    if( GetRenderDevice( ).GetMainContext( ) != &renderContext )
    {
        assert( false ); // must be main context
        return false;
    }
    assert( m_renderDevice.IsFrameStarted() );

    return InternalTryMap( mapType, doNotWait );
}

void vaTextureDX12::Unmap( vaRenderDeviceContext & renderContext )
{
    assert( GetRenderDevice( ).IsRenderThread( ) );
    assert( GetRenderDevice( ).IsFrameStarted( ) );
    if( GetRenderDevice( ).GetMainContext( ) != &renderContext )
    {
        assert( false ); // must be main context
        return;
    }

    InternalUnmap( );
}

uint32 vaTextureDX12::GetSRVBindlessIndex( vaRenderDeviceContext * renderContextPtr ) 
{ 
    assert( renderContextPtr != nullptr );  // this could be made optional but I don't think there's any point in doing so
    TransitionResource( AsDX12(*renderContextPtr), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
    return m_srv.GetBindlessIndex( ); 
}

void vaTextureDX12::TransitionResource( vaRenderDeviceContextBaseDX12& context, D3D12_RESOURCE_STATES target )
{
    if( m_overrideView != nullptr )
    {
        AsDX12( *m_overrideView ).TransitionResource( context, target );
    }
    else
        if( m_viewedOriginal != nullptr )
        {
            if( m_viewSubresourceList.size( ) == 0 )
                AsDX12( *m_viewedOriginal ).TransitionResource( context, target );
            else
            {
                for( auto subRes : m_viewSubresourceList )
                {
                    if( AsDX12( *m_viewedOriginal ).m_rsth.IsRSTHTransitionRequired( context, target, subRes ) )
                    {
                        if( !context.IsWorker( ) )
                            AsDX12( *m_viewedOriginal ).m_rsth.RSTHTransition( context, target, subRes );
                        else
                            context.GetMasterDX12( )->QueueResourceStateTransition( vaFramePtr<vaShaderResourceDX12>( this ), context.GetInstanceIndex( ), target, subRes );
                    }
                }
            }
        }
        else
        {
            if( m_rsth.IsRSTHTransitionRequired( context, target ) )
            {
                if( !context.IsWorker( ) )
                    m_rsth.RSTHTransition( context, target );
                else
                    context.GetMasterDX12( )->QueueResourceStateTransition( vaFramePtr<vaShaderResourceDX12>( this ), context.GetInstanceIndex( ), target );
            }
        }
}

// since this is the only CUDA user so far, do it like this
#ifndef VA_OPTIX_DENOISER_ENABLED
//bool vaTextureDX12::GetCUDAShared( void*& outPointer, size_t& outSize )
//{
//
//}
#endif