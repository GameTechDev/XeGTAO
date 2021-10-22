///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaDirectXTools.h"
//#include "DirectX\vaDirectXCanvas.h"
#include "IntegratedExternals/DirectXTex/DDSTextureLoader/DDSTextureLoader.h"
#include "IntegratedExternals/DirectXTex/WICTextureLoader/WICTextureLoader.h"
#include "IntegratedExternals/DirectXTex/DDSTextureLoader/DDSTextureLoader12.h"
#include "IntegratedExternals/DirectXTex/WICTextureLoader/WICTextureLoader12.h"
#include "IntegratedExternals/DirectXTex/DirectXTex/DirectXTex.h"
//#include "DirectXTex\DirectXTex.h"

#include "Rendering/Shaders/vaSharedTypes.h"

#include "Rendering/DirectX/vaRenderDeviceDX12.h"
#include "Rendering/DirectX/vaRenderDeviceContextDX12.h"

#include "Rendering/DirectX/vaShaderDX12.h"

#include "Rendering/DirectX/vaRenderMaterialDX12.h"

#include "Rendering/Shaders/vaRaytracingShared.h"

#include "Core/System/vaFileTools.h"

#include <stdarg.h>

#include <dxgiformat.h>
#include <assert.h>
#include <memory>
#include <algorithm>

#include "Core/Misc/vaXXHash.h"

//#include <iostream>
#include <iomanip>
#include <sstream>

#pragma warning (default : 4995)
#pragma warning ( disable : 4238 )  //  warning C4238: nonstandard extension used: class rvalue used as lvalue

using namespace std;
using namespace Vanilla;

template<typename T>
struct AnyStructComparer
{
    bool operator()( const T & Left, const T & Right ) const
    {
        // comparison logic goes here
        return memcmp( &Left, &Right, sizeof( Right ) ) < 0;
    }
};

////////////////////////////////////////////////////////////////////////////////

bool vaDirectXTools12::SaveDDSTexture( Vanilla::vaStream& outStream, _In_ ID3D12CommandQueue* pCommandQueue, _In_ ID3D12Resource* pSource, _In_ bool isCubeMap, _In_ D3D12_RESOURCE_STATES beforeState, _In_ D3D12_RESOURCE_STATES afterState )
{
    DirectX::ScratchImage scratchImage;

    HRESULT hr = DirectX::CaptureTexture( pCommandQueue, pSource, isCubeMap, scratchImage, beforeState, afterState );
    if( FAILED( hr ) )
    {
        assert( false );
        return false;
    }

    DirectX::Blob blob;

    hr = DirectX::SaveToDDSMemory( scratchImage.GetImages( ), scratchImage.GetImageCount( ), scratchImage.GetMetadata( ), DirectX::DDS_FLAGS_NONE, blob );
    if( FAILED( hr ) )
    {
        assert( false );
        return false;
    }

    return outStream.Write( blob.GetBufferPointer( ), blob.GetBufferSize( ) );
}

bool vaDirectXTools12::FillShaderResourceViewDesc( D3D12_SHADER_RESOURCE_VIEW_DESC & outDesc, ID3D12Resource * resource, DXGI_FORMAT format, int mipSliceMin, int mipSliceCount, int arraySliceMin, int arraySliceCount, bool isCubemap )
{
    assert( mipSliceMin >= 0 );
    assert( arraySliceMin >= 0 );
    assert( arraySliceCount >= -1 );    // -1 means all

    D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();

    outDesc.Format                  = (format == DXGI_FORMAT_UNKNOWN)?(resourceDesc.Format):(format);
    outDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    if( resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D )
    {
        if( !isCubemap )
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
        }
        else // is a cubemap
        {
            if( mipSliceCount == -1 )
                mipSliceCount = resourceDesc.MipLevels - mipSliceMin;
            if( arraySliceCount == -1 )
                arraySliceCount = resourceDesc.DepthOrArraySize - arraySliceMin;

            assert( mipSliceMin >= 0 && (UINT)mipSliceMin < resourceDesc.MipLevels );
            assert( mipSliceMin + mipSliceCount > 0 && (UINT)mipSliceMin + mipSliceCount <= resourceDesc.MipLevels );
            assert( arraySliceMin >= 0 && (UINT)arraySliceMin < resourceDesc.DepthOrArraySize );
            assert( arraySliceMin + arraySliceCount > 0 && (UINT)arraySliceMin + arraySliceCount <= resourceDesc.DepthOrArraySize );

            outDesc.ViewDimension = (resourceDesc.DepthOrArraySize==6)?(D3D12_SRV_DIMENSION_TEXTURECUBE):(D3D12_SRV_DIMENSION_TEXTURECUBEARRAY);
            assert( resourceDesc.DepthOrArraySize % 6 == 0 );

            if( outDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBE )
            {
                outDesc.TextureCube.MostDetailedMip     = mipSliceMin;
                outDesc.TextureCube.MipLevels           = mipSliceCount;
                outDesc.TextureCube.ResourceMinLODClamp = 0.0f;
                assert( arraySliceMin == 0 );
                assert( arraySliceCount == 6 );
            }
            else if( outDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBEARRAY )
            {
                outDesc.TextureCubeArray.MostDetailedMip    = mipSliceMin;
                outDesc.TextureCubeArray.MipLevels          = mipSliceCount;
                outDesc.TextureCubeArray.First2DArrayFace   = arraySliceMin/6;
                outDesc.TextureCubeArray.NumCubes           = arraySliceCount/6;
                outDesc.TextureCubeArray.ResourceMinLODClamp= 0.0f;
            }
            else { assert(false); }
        }
        return true;
    }
    else if( resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D )
    {
        assert( !isCubemap );   // can't be 3D cubemap

        if( mipSliceCount == -1 )
            mipSliceCount = resourceDesc.MipLevels - mipSliceMin;
        assert( mipSliceMin >= 0 && (UINT)mipSliceMin < resourceDesc.MipLevels );
        assert( mipSliceMin + mipSliceCount > 0 && (UINT)mipSliceMin + mipSliceCount <= resourceDesc.MipLevels );

        // no array slices for 3D textures
        assert( arraySliceMin == 0 );
        assert( arraySliceCount == resourceDesc.DepthOrArraySize );

        outDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;

        outDesc.Texture3D.MostDetailedMip       = mipSliceMin;
        outDesc.Texture3D.MipLevels             = mipSliceCount;
        outDesc.Texture3D.ResourceMinLODClamp   = 0.0f;
        return true;
    }
    else if( resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D )
    {
        assert( !isCubemap );   // can't be 1D cubemap

        if( mipSliceCount == -1 )
            mipSliceCount = resourceDesc.MipLevels - mipSliceMin;
        if( arraySliceCount == -1 )
            arraySliceCount = resourceDesc.DepthOrArraySize - arraySliceMin;

        assert( mipSliceMin >= 0 && (UINT)mipSliceMin < resourceDesc.MipLevels );
        assert( mipSliceMin + mipSliceCount > 0 && (UINT)mipSliceMin + mipSliceCount <= resourceDesc.MipLevels );
        assert( arraySliceMin >= 0 && (UINT)arraySliceMin < resourceDesc.DepthOrArraySize );
        assert( arraySliceMin + arraySliceCount > 0 && (UINT)arraySliceMin + arraySliceCount <= resourceDesc.DepthOrArraySize );

        outDesc.ViewDimension = ( resourceDesc.DepthOrArraySize == 1 ) ? ( D3D12_SRV_DIMENSION_TEXTURE1D ) : ( D3D12_SRV_DIMENSION_TEXTURE1DARRAY );

        if( outDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE1D )
        {
            outDesc.Texture1D.MostDetailedMip       = mipSliceMin;
            outDesc.Texture1D.MipLevels             = mipSliceCount;
            outDesc.Texture1D.ResourceMinLODClamp   = 0.0f;
        }
        else if( outDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE1DARRAY )
        {
            outDesc.Texture1DArray.MostDetailedMip      = mipSliceMin;
            outDesc.Texture1DArray.MipLevels            = mipSliceCount;
            outDesc.Texture1DArray.FirstArraySlice      = arraySliceMin;
            outDesc.Texture1DArray.ArraySize            = arraySliceCount;
            outDesc.Texture1DArray.ResourceMinLODClamp  = 0.0f;
        } else { assert( false ); }
        return true;
    }
    else if( resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER )
    {
        assert( false ); // not intended for buffers
        return false;
        //outDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        //outDesc.Buffer.
    }
    else
    {
        assert( false ); // resource not recognized; additional code might be needed above
        return false;
    }
}

bool vaDirectXTools12::FillDepthStencilViewDesc( D3D12_DEPTH_STENCIL_VIEW_DESC & outDesc, ID3D12Resource * resource, DXGI_FORMAT format, int mipSliceMin, int arraySliceMin, int arraySliceCount )
{
    assert( mipSliceMin >= 0 );
    assert( arraySliceMin >= 0 );
    assert( arraySliceCount >= -1 );    // -1 means all

    D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();
    outDesc.Format  = (format == DXGI_FORMAT_UNKNOWN)?(resourceDesc.Format):(format);
    outDesc.Flags   = D3D12_DSV_FLAG_NONE;  // D3D12_DSV_FLAG_READ_ONLY_DEPTH / D3D12_DSV_FLAG_READ_ONLY_STENCIL

    if( resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D )
    {
        if( arraySliceCount == -1 )
            arraySliceCount = resourceDesc.DepthOrArraySize - arraySliceMin;

        assert( mipSliceMin >= 0 && (UINT)mipSliceMin < resourceDesc.MipLevels );
        assert( arraySliceMin >= 0 && (UINT)arraySliceMin < resourceDesc.DepthOrArraySize );
        assert( arraySliceMin + arraySliceCount > 0 && (UINT)arraySliceMin + arraySliceCount <= resourceDesc.DepthOrArraySize );

        if( resourceDesc.SampleDesc.Count > 1 )
            outDesc.ViewDimension = ( resourceDesc.DepthOrArraySize == 1 ) ? ( D3D12_DSV_DIMENSION_TEXTURE2DMS ) : ( D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY );
        else
            outDesc.ViewDimension = ( resourceDesc.DepthOrArraySize == 1 ) ? ( D3D12_DSV_DIMENSION_TEXTURE2D ) : ( D3D12_DSV_DIMENSION_TEXTURE2DARRAY );

        if( outDesc.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2D )
        {
            outDesc.Texture2D.MipSlice             = mipSliceMin;
            assert( arraySliceMin == 0 );
        }
        else if( outDesc.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2DARRAY )
        {
            outDesc.Texture2DArray.MipSlice        = mipSliceMin;
            outDesc.Texture2DArray.FirstArraySlice = arraySliceMin;
            outDesc.Texture2DArray.ArraySize       = arraySliceCount;
        }
        else if( outDesc.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2DMS )
        {
            outDesc.Texture2DMS.UnusedField_NothingToDefine = 42;
            assert( mipSliceMin == 0 );
            assert( arraySliceMin == 0 );
        }
        else if( outDesc.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY )
        {
            outDesc.Texture2DMSArray.FirstArraySlice = arraySliceMin;
            outDesc.Texture2DMSArray.ArraySize       = arraySliceCount;
            assert( mipSliceMin == 0 );
        }
        else { assert( false ); return false; }

        return true;
    }
    
    assert( false );    // not implemented / supported
    return false;
}

bool vaDirectXTools12::FillRenderTargetViewDesc( D3D12_RENDER_TARGET_VIEW_DESC & outDesc, ID3D12Resource * resource, DXGI_FORMAT format, int mipSliceMin, int arraySliceMin, int arraySliceCount )
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

        if( resourceDesc.SampleDesc.Count > 1 )
            outDesc.ViewDimension = ( resourceDesc.DepthOrArraySize == 1 ) ? ( D3D12_RTV_DIMENSION_TEXTURE2DMS ) : ( D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY );
        else
            outDesc.ViewDimension = ( resourceDesc.DepthOrArraySize == 1 ) ? ( D3D12_RTV_DIMENSION_TEXTURE2D ) : ( D3D12_RTV_DIMENSION_TEXTURE2DARRAY );

        if( outDesc.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2D )
        {
            outDesc.Texture2D.MipSlice      = mipSliceMin;
            outDesc.Texture2D.PlaneSlice    = 0;
            assert( arraySliceMin == 0 );
        }
        else if( outDesc.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2DARRAY )
        {
            outDesc.Texture2DArray.MipSlice         = mipSliceMin;
            outDesc.Texture2DArray.FirstArraySlice  = arraySliceMin;
            outDesc.Texture2DArray.ArraySize        = arraySliceCount;
            outDesc.Texture2DArray.PlaneSlice       = 0;
        }
        else if( outDesc.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2DMS )
        {
            outDesc.Texture2DMS.UnusedField_NothingToDefine = 42;
            assert( mipSliceMin == 0 );
            assert( arraySliceMin == 0 );
        }
        else if( outDesc.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY )
        {
            outDesc.Texture2DMSArray.FirstArraySlice = arraySliceMin;
            outDesc.Texture2DMSArray.ArraySize       = arraySliceCount;
            assert( mipSliceMin == 0 );
        }
        else { assert( false ); return false; }

        return true;
    }
    
    assert( false );    // not implemented / supported
    return false;
}

bool vaDirectXTools12::FillUnorderedAccessViewDesc( D3D12_UNORDERED_ACCESS_VIEW_DESC & outDesc, ID3D12Resource * resource, DXGI_FORMAT format, int mipSliceMin, int arraySliceMin, int arraySliceCount )
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
    else if( resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D )
    {
        if( arraySliceCount == -1 )
            arraySliceCount = resourceDesc.DepthOrArraySize - arraySliceMin;

        assert( mipSliceMin >= 0 && (UINT)mipSliceMin < resourceDesc.MipLevels );
        assert( arraySliceMin >= 0 && (UINT)arraySliceMin < resourceDesc.DepthOrArraySize );
        assert( arraySliceMin + arraySliceCount > 0 && (UINT)arraySliceMin + arraySliceCount <= resourceDesc.DepthOrArraySize );

        outDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
            
        outDesc.Texture3D.MipSlice     = mipSliceMin;
        outDesc.Texture3D.FirstWSlice  = arraySliceMin;
        outDesc.Texture3D.WSize        = arraySliceCount;
        return true;
    }
    else if( resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D )
    {
        if( arraySliceCount == -1 )
            arraySliceCount = resourceDesc.DepthOrArraySize - arraySliceMin;

        assert( mipSliceMin >= 0 && (UINT)mipSliceMin < resourceDesc.MipLevels );
        assert( arraySliceMin >= 0 && (UINT)arraySliceMin < resourceDesc.DepthOrArraySize );
        assert( arraySliceMin + arraySliceCount > 0 && (UINT)arraySliceMin + arraySliceCount <= resourceDesc.DepthOrArraySize );

        outDesc.ViewDimension = ( resourceDesc.DepthOrArraySize == 1 ) ? ( D3D12_UAV_DIMENSION_TEXTURE1D ) : ( D3D12_UAV_DIMENSION_TEXTURE1DARRAY );

        if( outDesc.ViewDimension == D3D12_UAV_DIMENSION_TEXTURE1D )
        {
            outDesc.Texture1D.MipSlice = mipSliceMin;
        }
        else if( outDesc.ViewDimension == D3D12_UAV_DIMENSION_TEXTURE1DARRAY )
        {
            outDesc.Texture1DArray.MipSlice        = mipSliceMin;
            outDesc.Texture1DArray.FirstArraySlice = arraySliceMin;
            outDesc.Texture1DArray.ArraySize       = arraySliceCount;
        } 
        else { assert( false ); return false; }
        return true;
    }

    assert( false ); // resource not recognized; additional code might be needed above
    return false;
}

////////////////////////////////////////////////////////////////////////////////

void vaResourceStateTransitionHelperDX12::RSTHTransitionSubResUnroll( vaRenderDeviceContextBaseDX12 & context )
{
    // unroll all subres transitions because they are evil
    for( auto subRes : m_rsthSubResStates )
    {
        if( subRes.second != m_rsthCurrent )
            context.GetCommandList( )->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition( m_rsthResource.Get(), subRes.second, m_rsthCurrent, subRes.first ) );
    }
    m_rsthSubResStates.clear();
}

bool vaResourceStateTransitionHelperDX12::IsRSTHTransitionRequired( vaRenderDeviceContextBaseDX12 & context, D3D12_RESOURCE_STATES target, uint32 subResIndex )
{
    context;
    if( subResIndex == -1 )
        return (m_rsthSubResStates.size() > 0) || (m_rsthCurrent != target);
    else
    {
        for( auto subRes : m_rsthSubResStates )
            if( subRes.first == subResIndex && subRes.second == target )
                return false;
        return true;
    }
}

void vaResourceStateTransitionHelperDX12::RSTHTransition( vaRenderDeviceContextBaseDX12 & context, D3D12_RESOURCE_STATES target, uint32 subResIndex )
{
    assert( context.GetRenderDevice().IsRenderThread() );
    assert( AsDX12( context.GetRenderDevice() ).GetMainContext() == &context ); // we must be the main context for now
    assert( !context.IsWorker() );
    assert( m_rsthResource != nullptr );
    if( subResIndex != -1 )
    {
        RSTHTransitionSubRes( context, target, subResIndex );
        return;
    }

    if( m_rsthSubResStates.size() > 0 )
        RSTHTransitionSubResUnroll( context );

    if( m_rsthCurrent == target )
        return;

    auto trans = CD3DX12_RESOURCE_BARRIER::Transition( m_rsthResource.Get(), m_rsthCurrent, target );
    context.GetCommandList( )->ResourceBarrier(1, &trans );
    
    m_rsthCurrent = target;
}

void vaResourceStateTransitionHelperDX12::RSTHTransitionSubRes( vaRenderDeviceContextBaseDX12 & context, D3D12_RESOURCE_STATES target, uint32 subResIndex )
{
    // if already in, just transition that one
    for( auto it = m_rsthSubResStates.begin(); it < m_rsthSubResStates.end(); it++ )
    {
        auto & subRes = *it;
        if( subRes.first == subResIndex )
        {
            if( target != subRes.second )
                context.GetCommandList( )->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition( m_rsthResource.Get(), subRes.second, target, subResIndex ) );
            subRes.second = target;
            if( target == m_rsthCurrent )
                m_rsthSubResStates.erase( it );
            return;
        }
    }
    if( target == m_rsthCurrent )
        return;
    m_rsthSubResStates.push_back( make_pair( subResIndex, target ) ) ;
    context.GetCommandList( )->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition( m_rsthResource.Get(), m_rsthCurrent, target, subResIndex ) );
}

void vaResourceStateTransitionHelperDX12::RSTHAdoptResourceState( vaRenderDeviceContextBaseDX12 & context, D3D12_RESOURCE_STATES target, uint32 subResIndex )
{
    assert( context.GetRenderDevice().IsRenderThread() );
    assert( AsDX12( context.GetRenderDevice() ).GetMainContext() == &context ); // we must be the main context for now
    assert( !context.IsWorker() );
    assert( m_rsthResource != nullptr );
    context;
    if( subResIndex != -1 )
    {
        assert( false ); // not implemented/tested for subresources
        return;
    }
    if( m_rsthSubResStates.size() > 0 )
    {
        assert( false ); // not implemented/tested for subresources
        m_rsthSubResStates.clear();
    }

    if( m_rsthCurrent == target )
        return;

    m_rsthCurrent = target;
}

vaResourceViewDX12::~vaResourceViewDX12( ) 
{ 
    SafeRelease( );
}

void vaResourceViewDX12::Allocate( bool allocateCPUReadableToo )
{ 
    assert( !IsCreated() ); 
    m_device.AllocatePersistentResourceView( m_type, m_heapIndex, m_CPUHandle, m_GPUHandle );
    if( allocateCPUReadableToo )
    {
        assert( m_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        m_device.AllocatePersistentResourceView( D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, m_CPUReadableHeapIndex, m_CPUReadableCPUHandle, m_CPUReadableGPUHandle );
    }
    assert( IsCreated() ); 
}

void vaResourceViewDX12::SafeRelease( )
{
    if( !IsCreated() )
        return;
    auto type = m_type;
    int heapIndex = m_heapIndex;

    if( m_GPUHandle.ptr == D3D12_GPU_DESCRIPTOR_HANDLE{0}.ptr )
    {
        // these are now CPU-side only so we can remove them immediately 
        m_device.ReleasePersistentResourceView( type, heapIndex );
        if( m_CPUReadableHeapIndex != -1 )
            m_device.ReleasePersistentResourceView( D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, m_CPUReadableHeapIndex );
    }
    else
    {
        // let the resource be removed until we can guarantee GPU finished using it
        m_device.ExecuteAfterCurrentGPUFrameDone( 
            [type = m_type, heapIndex = m_heapIndex, CPUReadableHeapIndex = m_CPUReadableHeapIndex]( vaRenderDeviceDX12 & device )
                { 
                    device.ReleasePersistentResourceView( type, heapIndex );
                    if( CPUReadableHeapIndex != -1 )
                        device.ReleasePersistentResourceView( D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, CPUReadableHeapIndex );
                } );
    }
    m_heapIndex = -1;       // mark as destroyed
    m_CPUHandle = { 0 };    // avoid any confusion later
    m_GPUHandle = { 0 };    // avoid any confusion later
    m_CPUReadableHeapIndex  = -1;
    m_CPUReadableCPUHandle  = { 0 };
    m_CPUReadableGPUHandle  = { 0 };
}

void vaConstantBufferViewDX12::Create( const D3D12_CONSTANT_BUFFER_VIEW_DESC & desc )
{
    Allocate( false );
    if( m_heapIndex >= 0 )
    {
        m_desc = desc;
        m_device.GetPlatformDevice()->CreateConstantBufferView( &desc, m_CPUHandle );
    }
    else { assert( false ); }
}

void vaConstantBufferViewDX12::CreateNull( )
{
    D3D12_CONSTANT_BUFFER_VIEW_DESC desc = { 0, 0 };
    Allocate( false );
    if( m_heapIndex >= 0 )
    {
        m_desc = desc;
        m_device.GetPlatformDevice()->CreateConstantBufferView( &desc, m_CPUHandle );
    }
    else { assert( false ); }
    assert( m_CPUReadableHeapIndex == -1 );
}

void vaShaderResourceViewDX12::Create( ID3D12Resource * resource, const D3D12_SHADER_RESOURCE_VIEW_DESC & desc )
{
    Allocate( true );
    if( m_heapIndex >= 0 )
    {
        m_desc = desc;
        m_device.GetPlatformDevice()->CreateShaderResourceView( resource, &desc, m_CPUHandle );
    }
    else { assert( false ); }
    if( m_CPUReadableHeapIndex >= 0 )
    {
        m_desc = desc;
        m_device.GetPlatformDevice( )->CreateShaderResourceView( resource, &desc, m_CPUReadableCPUHandle );
    }
    else { assert( false ); }
}

void vaShaderResourceViewDX12::CreateNull( )
{
    D3D12_SHADER_RESOURCE_VIEW_DESC desc = { DXGI_FORMAT_R32_FLOAT, D3D12_SRV_DIMENSION_TEXTURE1D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING, {0, 0, 0} };
    Allocate( true );
    if( m_heapIndex >= 0 )
    {
        m_desc = desc;
        m_device.GetPlatformDevice()->CreateShaderResourceView( nullptr, &desc, m_CPUHandle );
    }
    else { assert( false ); }
    if( m_CPUReadableHeapIndex >= 0 )
    {
        m_desc = desc;
        m_device.GetPlatformDevice( )->CreateShaderResourceView( nullptr, &desc, m_CPUReadableCPUHandle );
    }
    else { assert( false ); }
}

void vaUnorderedAccessViewDX12::Create( ID3D12Resource *resource, ID3D12Resource * counterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC & desc )
{
    Allocate( true );
    if( m_heapIndex >= 0 )
    {
        m_desc = desc;
        m_device.GetPlatformDevice()->CreateUnorderedAccessView( resource, counterResource, &desc, m_CPUHandle );
    }
    else { assert( false ); }
    if( m_CPUReadableHeapIndex >= 0 )
    {
        m_desc = desc;
        m_device.GetPlatformDevice( )->CreateUnorderedAccessView( resource, counterResource, &desc, m_CPUReadableCPUHandle );
    }
    else { assert( false ); }
}

void vaUnorderedAccessViewDX12::CreateNull( D3D12_UAV_DIMENSION dimension )
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc = { DXGI_FORMAT_R32_FLOAT, dimension, { 0 } };
    Allocate( true );
    if( m_heapIndex >= 0 )
    {
        m_desc = desc;
        m_device.GetPlatformDevice()->CreateUnorderedAccessView( nullptr, nullptr, &desc, m_CPUHandle );
    }
    else { assert( false ); }
    if( m_CPUReadableHeapIndex >= 0 )
    {
        m_desc = desc;
        m_device.GetPlatformDevice( )->CreateUnorderedAccessView( nullptr, nullptr, &desc, m_CPUReadableCPUHandle );
    }
    else { assert( false ); }
}

void vaRenderTargetViewDX12::Create( ID3D12Resource *resource, const D3D12_RENDER_TARGET_VIEW_DESC & desc )
{
    Allocate( false );
    if( m_heapIndex >= 0 )
    {
        m_desc = desc;
        m_device.GetPlatformDevice()->CreateRenderTargetView( resource, &desc, m_CPUHandle );
    }
    else { assert( false ); }
    assert( m_CPUReadableHeapIndex == -1 );
}

void vaRenderTargetViewDX12::CreateNull( )
{
    D3D12_RENDER_TARGET_VIEW_DESC desc = { DXGI_FORMAT_R32_FLOAT, D3D12_RTV_DIMENSION_TEXTURE1D, { 0 } };
    Allocate( false );
    if( m_heapIndex >= 0 )
    {
        m_desc = desc;
        m_device.GetPlatformDevice()->CreateRenderTargetView( nullptr, &desc, m_CPUHandle );
    }
    else { assert( false ); }
    assert( m_CPUReadableHeapIndex == -1 );
}

void vaDepthStencilViewDX12::Create( ID3D12Resource *resource, const D3D12_DEPTH_STENCIL_VIEW_DESC & desc )
{
    Allocate( false );
    if( m_heapIndex >= 0 )
    {
        m_desc = desc;
        m_device.GetPlatformDevice()->CreateDepthStencilView( resource, &desc, m_CPUHandle );
    }
    else { assert( false ); }
    assert( m_CPUReadableHeapIndex == -1 );
}

void vaDepthStencilViewDX12::CreateNull( )
{
    D3D12_DEPTH_STENCIL_VIEW_DESC desc = { DXGI_FORMAT_D32_FLOAT, D3D12_DSV_DIMENSION_TEXTURE1D, D3D12_DSV_FLAG_NONE, { 0 } };
    Allocate( false );
    if( m_heapIndex >= 0 )
    {
        m_desc = desc;
        m_device.GetPlatformDevice()->CreateDepthStencilView( nullptr, &desc, m_CPUHandle );
    }
    else { assert( false ); }
    assert( m_CPUReadableHeapIndex == -1 );
}

void vaSamplerViewDX12::Create( const D3D12_SAMPLER_DESC & desc )
{
    assert( false ); // never tested
    Allocate( false );
    if( m_heapIndex >= 0 )
    {
        m_desc = desc;
        m_device.GetPlatformDevice()->CreateSampler( &desc, m_CPUHandle );
    }
    else { assert( false ); }
    assert( m_CPUReadableHeapIndex == -1 );
}

void vaSamplerViewDX12::CreateNull( )
{
    D3D12_SAMPLER_DESC desc = { D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, 0.0f, 0, D3D12_COMPARISON_FUNC_NEVER, {0.0f, 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f };
    Allocate( false );
    if( m_heapIndex >= 0 )
    {
        m_desc = desc;
        m_device.GetPlatformDevice()->CreateSampler( &desc, m_CPUHandle );
    }
    else { assert( false ); }
    assert( m_CPUReadableHeapIndex == -1 );
}

void vaDirectXTools12::FillSamplerStatePointClamp( D3D12_STATIC_SAMPLER_DESC & outDesc )
{
    outDesc = CD3DX12_STATIC_SAMPLER_DESC( );
    outDesc.Filter          = D3D12_FILTER_MIN_MAG_MIP_POINT;
    outDesc.AddressU        = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    outDesc.AddressV        = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    outDesc.AddressW        = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    outDesc.MipLODBias      = 0;
    outDesc.MaxAnisotropy   = 16;
    outDesc.ComparisonFunc  = D3D12_COMPARISON_FUNC_NEVER;
    outDesc.BorderColor     = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    outDesc.MinLOD          = 0.0f;
    outDesc.MaxLOD          = D3D12_FLOAT32_MAX;
    outDesc.ShaderRegister  = SHADERGLOBAL_POINTCLAMP_SAMPLERSLOT;
    outDesc.RegisterSpace   = 0;
    outDesc.ShaderVisibility= D3D12_SHADER_VISIBILITY_ALL;
}

void vaDirectXTools12::FillSamplerStatePointWrap( D3D12_STATIC_SAMPLER_DESC & outDesc )
{
    outDesc = CD3DX12_STATIC_SAMPLER_DESC( );
    outDesc.Filter          = D3D12_FILTER_MIN_MAG_MIP_POINT;
    outDesc.AddressU        = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    outDesc.AddressV        = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    outDesc.AddressW        = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    outDesc.MipLODBias      = 0;
    outDesc.MaxAnisotropy   = 16;
    outDesc.ComparisonFunc  = D3D12_COMPARISON_FUNC_NEVER;
    outDesc.BorderColor     = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    outDesc.MinLOD          = 0.0f;
    outDesc.MaxLOD          = D3D12_FLOAT32_MAX;
    outDesc.ShaderRegister  = SHADERGLOBAL_POINTWRAP_SAMPLERSLOT;
    outDesc.RegisterSpace   = 0;
    outDesc.ShaderVisibility= D3D12_SHADER_VISIBILITY_ALL;
}

void vaDirectXTools12::FillSamplerStateLinearClamp( D3D12_STATIC_SAMPLER_DESC & outDesc )
{
    outDesc = CD3DX12_STATIC_SAMPLER_DESC( );
    outDesc.Filter          = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    outDesc.AddressU        = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    outDesc.AddressV        = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    outDesc.AddressW        = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    outDesc.MipLODBias      = 0;
    outDesc.MaxAnisotropy   = 16;
    outDesc.ComparisonFunc  = D3D12_COMPARISON_FUNC_NEVER;
    outDesc.BorderColor     = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    outDesc.MinLOD          = 0.0f;
    outDesc.MaxLOD          = D3D12_FLOAT32_MAX;
    outDesc.ShaderRegister  = SHADERGLOBAL_LINEARCLAMP_SAMPLERSLOT;
    outDesc.RegisterSpace   = 0;
    outDesc.ShaderVisibility= D3D12_SHADER_VISIBILITY_ALL;
}

void vaDirectXTools12::FillSamplerStateLinearWrap( D3D12_STATIC_SAMPLER_DESC & outDesc )
{
    outDesc = CD3DX12_STATIC_SAMPLER_DESC( );
    outDesc.Filter          = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    outDesc.AddressU        = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    outDesc.AddressV        = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    outDesc.AddressW        = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    outDesc.MipLODBias      = 0;
    outDesc.MaxAnisotropy   = 16;
    outDesc.ComparisonFunc  = D3D12_COMPARISON_FUNC_NEVER;
    outDesc.BorderColor     = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    outDesc.MinLOD          = 0.0f;
    outDesc.MaxLOD          = D3D12_FLOAT32_MAX;
    outDesc.ShaderRegister  = SHADERGLOBAL_LINEARWRAP_SAMPLERSLOT;
    outDesc.RegisterSpace   = 0;
    outDesc.ShaderVisibility= D3D12_SHADER_VISIBILITY_ALL;
}

void vaDirectXTools12::FillSamplerStateAnisotropicClamp( D3D12_STATIC_SAMPLER_DESC & outDesc )
{
    outDesc = CD3DX12_STATIC_SAMPLER_DESC();
    outDesc.Filter          = D3D12_FILTER_ANISOTROPIC;
    outDesc.AddressU        = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    outDesc.AddressV        = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    outDesc.AddressW        = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    outDesc.MipLODBias      = 0;
    outDesc.MaxAnisotropy   = 16;
    outDesc.ComparisonFunc  = D3D12_COMPARISON_FUNC_NEVER;
    outDesc.BorderColor     = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    outDesc.MinLOD          = 0.0f;
    outDesc.MaxLOD          = D3D12_FLOAT32_MAX;
    outDesc.ShaderRegister  = SHADERGLOBAL_ANISOTROPICCLAMP_SAMPLERSLOT;
    outDesc.RegisterSpace   = 0;
    outDesc.ShaderVisibility= D3D12_SHADER_VISIBILITY_ALL;
}

void vaDirectXTools12::FillSamplerStateAnisotropicWrap( D3D12_STATIC_SAMPLER_DESC & outDesc )
{
    outDesc = CD3DX12_STATIC_SAMPLER_DESC( );
    outDesc.Filter          = D3D12_FILTER_ANISOTROPIC;
    outDesc.AddressU        = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    outDesc.AddressV        = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    outDesc.AddressW        = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    outDesc.MipLODBias      = 0;
    outDesc.MaxAnisotropy   = 16;
    outDesc.ComparisonFunc  = D3D12_COMPARISON_FUNC_NEVER;
    outDesc.BorderColor     = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    outDesc.MinLOD          = 0.0f;
    outDesc.MaxLOD          = D3D12_FLOAT32_MAX;
    outDesc.ShaderRegister  = SHADERGLOBAL_ANISOTROPICWRAP_SAMPLERSLOT;
    outDesc.RegisterSpace   = 0;
    outDesc.ShaderVisibility= D3D12_SHADER_VISIBILITY_ALL;
}

void vaDirectXTools12::FillSamplerStateShadowCmp( D3D12_STATIC_SAMPLER_DESC & outDesc )
{
    outDesc = CD3DX12_STATIC_SAMPLER_DESC( );
    outDesc.Filter          = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR; // D3D12_FILTER_COMPARISON_ANISOTROPIC; 
    outDesc.AddressU        = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    outDesc.AddressV        = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    outDesc.AddressW        = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    outDesc.MipLODBias      = 0;
    outDesc.MaxAnisotropy   = 16;
    outDesc.ComparisonFunc  = D3D12_COMPARISON_FUNC_GREATER_EQUAL; // D3D12_COMPARISON_FUNC_LESS_EQUAL;
    outDesc.BorderColor     = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    outDesc.MinLOD          = 0.0f;
    outDesc.MaxLOD          = D3D12_FLOAT32_MAX;
    outDesc.ShaderRegister  = SHADERGLOBAL_SHADOWCMP_SAMPLERSLOT;
    outDesc.RegisterSpace   = 0;
    outDesc.ShaderVisibility= D3D12_SHADER_VISIBILITY_ALL;
}

void vaDirectXTools12::FillBlendState( D3D12_BLEND_DESC & outDesc, vaBlendMode blendMode )
{
    outDesc.AlphaToCoverageEnable   = false;
    outDesc.IndependentBlendEnable  = false;
    const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc =
    {
        FALSE, FALSE,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_LOGIC_OP_NOOP,
        D3D12_COLOR_WRITE_ENABLE_ALL,
    };
    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        outDesc.RenderTarget[ i ] = defaultRenderTargetBlendDesc;

    switch( blendMode )
    {
    case vaBlendMode::Opaque:
        // already in default
        break;
    case vaBlendMode::Additive:
        outDesc.RenderTarget[0].BlendEnable     = true;
        outDesc.RenderTarget[0].BlendOp         = D3D12_BLEND_OP_ADD;
        outDesc.RenderTarget[0].BlendOpAlpha    = D3D12_BLEND_OP_ADD;
        outDesc.RenderTarget[0].SrcBlend        = D3D12_BLEND_ONE;
        outDesc.RenderTarget[0].SrcBlendAlpha   = D3D12_BLEND_ONE;
        outDesc.RenderTarget[0].DestBlend       = D3D12_BLEND_ONE;
        outDesc.RenderTarget[0].DestBlendAlpha  = D3D12_BLEND_ONE;
        break;
    case vaBlendMode::AlphaBlend:
        outDesc.RenderTarget[0].BlendEnable     = true;
        outDesc.RenderTarget[0].BlendOp         = D3D12_BLEND_OP_ADD;
        outDesc.RenderTarget[0].BlendOpAlpha    = D3D12_BLEND_OP_ADD;
        outDesc.RenderTarget[0].SrcBlend        = D3D12_BLEND_SRC_ALPHA;
        outDesc.RenderTarget[0].SrcBlendAlpha   = D3D12_BLEND_ZERO;
        outDesc.RenderTarget[0].DestBlend       = D3D12_BLEND_INV_SRC_ALPHA;
        outDesc.RenderTarget[0].DestBlendAlpha  = D3D12_BLEND_ONE;
        break;
    case vaBlendMode::PremultAlphaBlend:
        outDesc.RenderTarget[0].BlendEnable     = true;
        outDesc.RenderTarget[0].BlendOp         = D3D12_BLEND_OP_ADD;
        outDesc.RenderTarget[0].BlendOpAlpha    = D3D12_BLEND_OP_ADD;
        outDesc.RenderTarget[0].SrcBlend        = D3D12_BLEND_ONE;
        outDesc.RenderTarget[0].SrcBlendAlpha   = D3D12_BLEND_ZERO;
        outDesc.RenderTarget[0].DestBlend       = D3D12_BLEND_INV_SRC_ALPHA;
        outDesc.RenderTarget[0].DestBlendAlpha  = D3D12_BLEND_ONE;
        break;
    case vaBlendMode::Mult:
        outDesc.RenderTarget[0].BlendEnable     = true;
        outDesc.RenderTarget[0].BlendOp         = D3D12_BLEND_OP_ADD;
        outDesc.RenderTarget[0].BlendOpAlpha    = D3D12_BLEND_OP_ADD;
        outDesc.RenderTarget[0].SrcBlend        = D3D12_BLEND_ZERO;
        outDesc.RenderTarget[0].SrcBlendAlpha   = D3D12_BLEND_ZERO;
        outDesc.RenderTarget[0].DestBlend       = D3D12_BLEND_SRC_COLOR;
        outDesc.RenderTarget[0].DestBlendAlpha  = D3D12_BLEND_SRC_ALPHA;
        break;
    case vaBlendMode::OffscreenAccumulate:
        outDesc.RenderTarget[0].BlendEnable     = true;
        outDesc.RenderTarget[0].BlendOp         = D3D12_BLEND_OP_ADD;
        outDesc.RenderTarget[0].BlendOpAlpha    = D3D12_BLEND_OP_ADD;
        outDesc.RenderTarget[0].SrcBlend        = D3D12_BLEND_SRC_ALPHA;
        outDesc.RenderTarget[0].DestBlend       = D3D12_BLEND_INV_SRC_ALPHA;
        outDesc.RenderTarget[0].SrcBlendAlpha   = D3D12_BLEND_ONE;
        outDesc.RenderTarget[0].DestBlendAlpha  = D3D12_BLEND_INV_SRC_ALPHA;
        break;
    default: assert( false );
    }
}

bool vaDirectXTools12::LoadTexture( ID3D12Device * device, void * dataBuffer, uint64 dataBufferSize, vaTextureLoadFlags loadFlags, vaResourceBindSupportFlags bindFlags, ID3D12Resource *& outResource, std::vector<D3D12_SUBRESOURCE_DATA> & outSubresources, std::unique_ptr<Vanilla::byte[]> & outDecodedData, bool & outIsCubemap )
{
    D3D12_RESOURCE_FLAGS resourceFlags = ResourceFlagsDX12FromVA( bindFlags );

    if( (loadFlags & vaTextureLoadFlags::PresumeDataIsSRGB) != 0 )
        { assert( (loadFlags & vaTextureLoadFlags::PresumeDataIsLinear) == 0 ); }   // both at the same time don't make sense
    if( (loadFlags & vaTextureLoadFlags::PresumeDataIsLinear) != 0 )
        { assert( (loadFlags & vaTextureLoadFlags::PresumeDataIsSRGB) == 0 );  }    // both at the same time don't make sense
    // assert( (loadFlags & vaTextureLoadFlags::AutogenerateMIPsIfMissing) == 0 );     // not supported anymore

    assert( outSubresources.size() == 0 );
    outSubresources.clear();
    HRESULT hr;

    // at least 16 bytes in size needed (don't think there's any format that would work with less)
    if( dataBufferSize < 16 )
        return false;

    outIsCubemap = false;

    const uint32_t DDS_MAGIC = 0x20534444;      // "DDS "
    const char HDRSignature[] = "#?RADIANCE";   // for the .hdr - https://en.wikipedia.org/wiki/RGBE_image_format format
    const char HDRSignatureAlt[] = "#?RGBE";

    if( (*(reinterpret_cast<uint32_t*>( dataBuffer ))==DDS_MAGIC) )
    {
        assert( outDecodedData == nullptr || outDecodedData.get() == dataBuffer );   // loading inplace from provided data
        uint32 dxLoadFlags = 0;
        dxLoadFlags |= ( (loadFlags & vaTextureLoadFlags::PresumeDataIsSRGB) != 0 ) ? ( DirectX::DDS_LOADER_FORCE_SRGB ) : ( 0 );
        hr = DirectX::LoadDDSTextureFromMemoryEx( device, (const uint8_t *)dataBuffer, (size_t)dataBufferSize, 0, resourceFlags, dxLoadFlags, &outResource, outSubresources, nullptr, &outIsCubemap );
    }
    else if( (memcmp(dataBuffer, HDRSignature, sizeof(HDRSignature) - 1) == 0) || (memcmp(dataBuffer, HDRSignatureAlt, sizeof(HDRSignatureAlt) - 1) == 0) )
    {
        auto image = std::make_unique<DirectX::ScratchImage>();
        DirectX::TexMetadata metadata;
        hr = DirectX::LoadFromHDRMemory( dataBuffer, dataBufferSize, &metadata, *image );
        if( FAILED(hr) )
            { assert( false ); return hr; }
        const DirectX::Image* imgLoaded = image->GetImage( 0, 0, 0 );
        if( imgLoaded == nullptr )
            { assert( false ); return false; }

        assert( outDecodedData == nullptr );
        outDecodedData = std::make_unique<Vanilla::byte[]>( imgLoaded->slicePitch );
        DirectX::Image imgLoadedExternalStorage = *imgLoaded;
        imgLoadedExternalStorage.pixels = outDecodedData.get( );
        memcpy( imgLoadedExternalStorage.pixels, imgLoaded->pixels, imgLoaded->slicePitch );

        hr = DirectX::CreateTextureEx( device, metadata, resourceFlags, false, &outResource );
        if( FAILED(hr) )
            { assert( false ); return false; }

        hr = DirectX::PrepareUpload( device, &imgLoadedExternalStorage, 1, metadata, outSubresources );
    }
    else
    {
        outIsCubemap = false;
        assert( outDecodedData == nullptr );            // will create data
        outSubresources.resize(1);

        uint32 wicLoadFlags = 0;
        wicLoadFlags |= ( ( loadFlags & vaTextureLoadFlags::PresumeDataIsSRGB ) != 0 ) ? ( DirectX::WIC_LOADER_FORCE_SRGB ) : ( 0 );
        wicLoadFlags |= ( ( loadFlags & vaTextureLoadFlags::PresumeDataIsLinear ) != 0 ) ? ( DirectX::WIC_LOADER_IGNORE_SRGB ) : ( 0 );

        hr = DirectX::LoadWICTextureFromMemoryEx( device, (const uint8_t *)dataBuffer, (size_t)dataBufferSize, 0, resourceFlags, wicLoadFlags, &outResource, outDecodedData, outSubresources[0] );
    }

    if( SUCCEEDED( hr ) )
    {
        D3D12_RESOURCE_DESC desc = outResource->GetDesc();
        if( (loadFlags & vaTextureLoadFlags::PresumeDataIsSRGB) != 0 )
        {
            // wanted sRGB but didn't get it? there's something wrong
            assert( DirectX::IsSRGB( desc.Format ) );
        }
        if( (loadFlags & vaTextureLoadFlags::PresumeDataIsLinear) != 0 )
        {
            // there is no support for this at the moment in DirectX tools so asserting if the result is not as requested; fix in the future
            assert( !DirectX::IsSRGB( desc.Format ) );
        }
        return true;
    }
    outResource = nullptr;
    outDecodedData = nullptr;
    outSubresources.clear();
    return false;
}

bool vaDirectXTools12::LoadTexture( ID3D12Device * device, const wchar_t * filePath, bool isDDS, vaTextureLoadFlags loadFlags, vaResourceBindSupportFlags bindFlags, ID3D12Resource *& outResource, std::vector<D3D12_SUBRESOURCE_DATA> & outSubresources, std::unique_ptr<Vanilla::byte[]> & outDecodedData, bool & outIsCubemap )
{
    auto buffer = vaFileTools::LoadMemoryStream( filePath );

    if( buffer == nullptr )
        return nullptr;

    if( isDDS )
    {
        outDecodedData = std::make_unique<Vanilla::byte[]>( buffer->GetLength() );
        memcpy( outDecodedData.get(), buffer->GetBuffer(), buffer->GetLength() );
        return LoadTexture( device, outDecodedData.get(), buffer->GetLength( ), loadFlags, bindFlags, outResource, outSubresources, outDecodedData, outIsCubemap );
    }
    else
    {
        return LoadTexture( device, buffer->GetBuffer( ), buffer->GetLength( ), loadFlags, bindFlags, outResource, outSubresources, outDecodedData, outIsCubemap );
    }
}

#if 0
ID3D12Resource * vaDirectXTools12::LoadTextureDDS( ID3D12Device * device, void * dataBuffer, int64 dataSize, vaTextureLoadFlags loadFlags, vaResourceBindSupportFlags bindFlags, uint64 * outCRC )
{
    outCRC; // unreferenced, not implemented
    assert( outCRC == nullptr ); // not implemented

    ID3D12Resource * texture = nullptr;

    // std::vector<D3D12_SUBRESOURCE_DATA> subresources; <- ok need to output this and isCubemap!
    hr = DirectX::LoadDDSTextureFromMemoryEx( device, (const uint8_t *)dataBuffer, (size_t)dataSize, 0, resourceFlags, dxLoadFlags, &texture, subresources, NULL );

    if( SUCCEEDED( hr ) )
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc;
        textureSRV->GetDesc( &desc );
        if( (loadFlags & vaTextureLoadFlags::PresumeDataIsSRGB) != 0 )
        {
            // wanted sRGB but didn't get it? there's something wrong
            assert( DirectX::IsSRGB( desc.Format ) );
        }
        if( (loadFlags & vaTextureLoadFlags::PresumeDataIsLinear) != 0 )
        {
            // there is no support for this at the moment in DirectX tools so asserting if the result is not as requested; fix in the future
            assert( !DirectX::IsSRGB( desc.Format ) );
        }
        if( (loadFlags & vaTextureLoadFlags::AutogenerateMIPsIfMissing) != 0 )
        {
            // check for mips here maybe?
            // assert( desc. );
        }

        SAFE_RELEASE( textureSRV );

        return texture;
    }
    else
    {
        SAFE_RELEASE( texture );
        SAFE_RELEASE( textureSRV );
        assert( false ); // check hr
        throw "Error creating the texture from the stream";
    }
}

ID3D12Resource * vaDirectXTools12::LoadTextureDDS( ID3D12Device * device, const wchar_t * path, vaTextureLoadFlags loadFlags, vaResourceBindSupportFlags bindFlags, uint64 * outCRC )
{
    outCRC; // unreferenced, not implemented
    assert( outCRC == nullptr ); // not implemented

    auto buffer = vaFileTools::LoadMemoryStream( path );

    if( buffer == nullptr )
        return nullptr;

    return LoadTextureDDS( device, buffer->GetBuffer( ), buffer->GetLength( ), loadFlags, bindFlags, outCRC );
}

ID3D12Resource * vaDirectXTools12::LoadTextureWIC( ID3D12Device * device, void * dataBuffer, int64 dataSize, vaTextureLoadFlags loadFlags, vaResourceBindSupportFlags bindFlags, uint64 * outCRC )
{
    outCRC; // unreferenced, not implemented
    assert( outCRC == nullptr ); // not implemented

    ID3D12DeviceContext * immediateContext = nullptr;
    device->GetImmediateContext( &immediateContext );
    immediateContext->Release(); // yeah, ugly - we know device will guarantee immediate context persistence but still...

    ID3D12Resource * texture = NULL;

    UINT dxBindFlags = BindFlagsDX12FromVA( bindFlags );

    // not actually used, needed internally for mipmap generation
    ID3D12ShaderResourceView * textureSRV = NULL;

    DirectX::WIC_LOADER_FLAGS wicLoaderFlags = DirectX::WIC_LOADER_DEFAULT;
    if( (loadFlags & vaTextureLoadFlags::PresumeDataIsSRGB) != 0 )
    {
        assert( (loadFlags & vaTextureLoadFlags::PresumeDataIsLinear) == 0 );   // both at the same time don't make sense
        wicLoaderFlags = (DirectX::WIC_LOADER_FLAGS)(wicLoaderFlags | DirectX::WIC_LOADER_FORCE_SRGB);
    }
    if( (loadFlags & vaTextureLoadFlags::PresumeDataIsLinear) != 0 )
    {
        assert( (loadFlags & vaTextureLoadFlags::PresumeDataIsSRGB) == 0 );   // both at the same time don't make sense
        wicLoaderFlags = (DirectX::WIC_LOADER_FLAGS)(wicLoaderFlags | DirectX::WIC_LOADER_IGNORE_SRGB);
    }
    bool dontAutogenerateMIPs = (loadFlags & vaTextureLoadFlags::AutogenerateMIPsIfMissing) == 0;

    HRESULT hr;

    if( dontAutogenerateMIPs )
    {
        hr = DirectX::CreateWICTextureFromMemoryEx( device, (const uint8_t *)dataBuffer, (size_t)dataSize, 0, D3D11_USAGE_DEFAULT, dxBindFlags, 0, 0, wicLoaderFlags, &texture, &textureSRV );
    }
    else
    {
        hr = DirectX::CreateWICTextureFromMemoryEx( device, immediateContext, (const uint8_t *)dataBuffer, (size_t)dataSize, 0, D3D11_USAGE_DEFAULT, dxBindFlags, 0, 0, wicLoaderFlags, &texture, &textureSRV );
    }

    if( SUCCEEDED( hr ) )
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc;
        textureSRV->GetDesc( &desc );
        if( (loadFlags & vaTextureLoadFlags::PresumeDataIsSRGB) != 0 )
        {
            // wanted sRGB but didn't get it? there's something wrong
            assert( DirectX::IsSRGB( desc.Format ) );
        }
        if( (loadFlags & vaTextureLoadFlags::PresumeDataIsLinear) != 0 )
        {
            // there is no support for this at the moment in DirectX tools so asserting if the result is not as requested; fix in the future
            assert( !DirectX::IsSRGB( desc.Format ) );
        }

        SAFE_RELEASE( textureSRV );
        return texture;
    }
    else
    {
        SAFE_RELEASE( texture );
        SAFE_RELEASE( textureSRV );
        VA_LOG_ERROR_STACKINFO( L"Error loading texture" );
        return nullptr;
    }
}

ID3D12Resource * vaDirectXTools12::LoadTextureWIC( ID3D12Device * device, const wchar_t * path, vaTextureLoadFlags loadFlags, vaResourceBindSupportFlags bindFlags, uint64 * outCRC )
{
    outCRC; // unreferenced, not implemented
    assert( outCRC == nullptr ); // not implemented

    auto buffer = vaFileTools::LoadMemoryStream( path );

    if( buffer == nullptr )
        return nullptr;

    return LoadTextureWIC( device, buffer->GetBuffer( ), buffer->GetLength( ), loadFlags, bindFlags, outCRC );
}

#endif

/*bool vaGraphicsPSODescDX12::operator == ( const vaGraphicsPSODescDX12 & other ) const
{
    if(  
        VSBlob             != other.VSBlob             ||
        VSInputLayout      != other.VSInputLayout      ||
        VSUniqueContentsID != other.VSUniqueContentsID ||
        PSBlob             != other.PSBlob             ||
        PSUniqueContentsID != other.PSUniqueContentsID ||
        DSBlob             != other.DSBlob             ||
        DSUniqueContentsID != other.DSUniqueContentsID ||
        HSBlob             != other.HSBlob             ||
        HSUniqueContentsID != other.HSUniqueContentsID ||
        GSBlob             != other.GSBlob             ||
        GSUniqueContentsID != other.GSUniqueContentsID )
        return false;

    if(
        BlendMode               != other.BlendMode              ||
        FillMode                != other.FillMode               ||
        CullMode                != other.CullMode               ||
        FrontCounterClockwise   != other.FrontCounterClockwise  ||
        MultisampleEnable       != other.MultisampleEnable      ||
        DepthEnable             != other.DepthEnable            ||
        DepthWriteEnable        != other.DepthWriteEnable       ||
        DepthFunc               != other.DepthFunc              ||
        Topology                != other.Topology               )
        return false;

    if( 
        NumRenderTargets        != other.NumRenderTargets       ||
        DSVFormat               != other.DSVFormat              ||
        SampleDescCount         != other.SampleDescCount        )
        return false;

    bool retVal = true;
    for( int i = 0; i < countof(RTVFormats); i++ )
        retVal &= RTVFormats[i] == other.RTVFormats[i];
    return retVal;
}*/

void vaGraphicsPSODescDX12::PartialReset( )
{
    VSBlob              = nullptr;
    VSInputLayout       = nullptr;
    VSUniqueContentsID  = -1;
    PSBlob              = nullptr;
    PSUniqueContentsID  = -1;
    DSBlob              = nullptr;
    DSUniqueContentsID  = -1;
    HSBlob              = nullptr;
    HSUniqueContentsID  = -1;
    GSBlob              = nullptr;
    GSUniqueContentsID  = -1;
}

void vaGraphicsPSODescDX12::CleanPointers( )
{
    VSBlob          = nullptr;        
    VSInputLayout   = nullptr;
    PSBlob          = nullptr;
    DSBlob          = nullptr;
    HSBlob          = nullptr;
    GSBlob          = nullptr;
}

void vaGraphicsPSODescDX12::InvalidateCache( )
{
    VSUniqueContentsID = -1;
}

void vaGraphicsPSODescDX12::FillGraphicsPipelineStateDesc( D3D12_GRAPHICS_PIPELINE_STATE_DESC & outDesc, ID3D12RootSignature * pRootSignature ) const
{
    assert( VSBlob != nullptr );
    outDesc.pRootSignature    = pRootSignature;
    outDesc.VS                = (VSBlob != nullptr)?( CD3DX12_SHADER_BYTECODE(VSBlob.Get()) ):(D3D12_SHADER_BYTECODE({0, 0}));
    outDesc.PS                = (PSBlob != nullptr)?( CD3DX12_SHADER_BYTECODE(PSBlob.Get()) ):(D3D12_SHADER_BYTECODE({0, 0}));
    outDesc.DS                = (DSBlob != nullptr)?( CD3DX12_SHADER_BYTECODE(DSBlob.Get()) ):(D3D12_SHADER_BYTECODE({0, 0}));
    outDesc.HS                = (HSBlob != nullptr)?( CD3DX12_SHADER_BYTECODE(HSBlob.Get()) ):(D3D12_SHADER_BYTECODE({0, 0}));
    outDesc.GS                = (GSBlob != nullptr)?( CD3DX12_SHADER_BYTECODE(GSBlob.Get()) ):(D3D12_SHADER_BYTECODE({0, 0}));
    outDesc.StreamOutput      = D3D12_STREAM_OUTPUT_DESC({nullptr, 0, nullptr, 0, 0});
    vaDirectXTools12::FillBlendState( outDesc.BlendState, BlendMode );
    outDesc.SampleMask        = UINT_MAX;
    
    // rasterizer state
    {
        D3D12_RASTERIZER_DESC rastDesc;
        rastDesc.CullMode               = (CullMode == vaFaceCull::None)?(D3D12_CULL_MODE_NONE):( (CullMode == vaFaceCull::Front)?(D3D12_CULL_MODE_FRONT):(D3D12_CULL_MODE_BACK) );
        rastDesc.FillMode               = (FillMode == vaFillMode::Solid)?(D3D12_FILL_MODE_SOLID):(D3D12_FILL_MODE_WIREFRAME);
        rastDesc.FrontCounterClockwise  = FrontCounterClockwise;
        rastDesc.DepthBias              = D3D12_DEFAULT_DEPTH_BIAS;
        rastDesc.DepthBiasClamp         = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        rastDesc.SlopeScaledDepthBias   = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        rastDesc.DepthClipEnable        = true;
        rastDesc.MultisampleEnable      = MultisampleEnable;
        //rastDesc.ScissorEnable          = true;
        rastDesc.AntialiasedLineEnable  = false;
        rastDesc.ForcedSampleCount      = 0;
        rastDesc.ConservativeRaster     = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        outDesc.RasterizerState       = rastDesc;
    }

    // depth stencil state
    {
        D3D12_DEPTH_STENCIL_DESC dsDesc;
        dsDesc.DepthEnable              = DepthEnable;
        dsDesc.DepthWriteMask           = (DepthWriteEnable)?(D3D12_DEPTH_WRITE_MASK_ALL):(D3D12_DEPTH_WRITE_MASK_ZERO);
        dsDesc.DepthFunc                = (D3D12_COMPARISON_FUNC)DepthFunc;
        dsDesc.StencilEnable            = false;
        dsDesc.StencilReadMask          = 0;
        dsDesc.StencilWriteMask         = 0;
        dsDesc.FrontFace                = {D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
        dsDesc.BackFace                 = {D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
        outDesc.DepthStencilState = dsDesc;
    }
    
    // input layout
    {
        if( VSInputLayout != nullptr )
        {
            outDesc.InputLayout.NumElements          = (UINT)VSInputLayout->Layout().size();
            outDesc.InputLayout.pInputElementDescs   = &(VSInputLayout->Layout()[0]);
        }
        else
            outDesc = { 0, nullptr };
    }

    outDesc.IBStripCutValue   = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;

    // topology
    {
        outDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
        // TODO: "If the HS and DS members are specified, the PrimitiveTopologyType member for topology type must be set to D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH."
        assert( HSBlob == nullptr );
        assert( DSBlob == nullptr );
        switch( Topology )
        {   case vaPrimitiveTopology::PointList:        outDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;        break;
            case vaPrimitiveTopology::LineList:         outDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;         break;
            case vaPrimitiveTopology::TriangleList:     outDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;     break;
            case vaPrimitiveTopology::TriangleStrip:    outDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;     break;
            default: assert( false ); break;    // for hull shader
        }
    }

    outDesc.NumRenderTargets  = NumRenderTargets;
    for( int i = 0; i < _countof(outDesc.RTVFormats); i++ )
        outDesc.RTVFormats[i] = DXGIFormatFromVA(RTVFormats[i]);
    outDesc.DSVFormat         = DXGIFormatFromVA(DSVFormat);
    outDesc.SampleDesc        = { SampleDescCount, 0 };
    outDesc.NodeMask          = 0;
    outDesc.CachedPSO         = { nullptr, 0 };
    outDesc.Flags             = D3D12_PIPELINE_STATE_FLAG_NONE; // for warp devices automatically use D3D12_PIPELINE_STATE_FLAG_TOOL_DEBUG?

}

//void vaGraphicsPSODescDX12::FillKey( vaMemoryStream & outStream ) const
//{
//    size_t dbgSizeOfThis = sizeof(*this); dbgSizeOfThis;
//    assert( dbgSizeOfThis == 168 ); // size of the structure changed, did you change the key creation too?
//    
//    assert( outStream.GetPosition() == 0 );
//
//    // add space for the 64bit hash 
//    outStream.WriteValue<uint64>( 0ui64 );
//
//    outStream.WriteValue<int64>( VSUniqueContentsID );
//    outStream.WriteValue<int64>( PSUniqueContentsID );
//    outStream.WriteValue<int64>( DSUniqueContentsID );
//    outStream.WriteValue<int64>( HSUniqueContentsID );
//    outStream.WriteValue<int64>( GSUniqueContentsID );
//
//    outStream.WriteValue<int32>( static_cast<int32>(this->BlendMode) );
//    outStream.WriteValue<int32>( static_cast<int32>(this->FillMode) );
//    outStream.WriteValue<int32>( static_cast<int32>(this->CullMode) );
//    outStream.WriteValue<bool>( this->FrontCounterClockwise );
//    outStream.WriteValue<bool>( this->MultisampleEnable );
//    // outStream.WriteValue<bool>( this->ScissorEnable );
//    outStream.WriteValue<bool>( this->DepthEnable );
//    outStream.WriteValue<bool>( this->DepthWriteEnable );
//    outStream.WriteValue<int32>( static_cast<int32>(this->DepthFunc) );
//    outStream.WriteValue<int32>( static_cast<int32>(this->Topology) );
//    outStream.WriteValue<int32>( this->NumRenderTargets );
//    
//    for( int i = 0; i < countof(this->RTVFormats); i++ )
//        outStream.WriteValue<int32>( static_cast<int32>(this->RTVFormats[i]) );
//    outStream.WriteValue<int32>( static_cast<int32>(this->DSVFormat) );
//    outStream.WriteValue<uint32>( this->SampleDescCount );
//
//    *reinterpret_cast<uint64*>(outStream.GetBuffer()) = vaXXHash64::Compute( outStream.GetBuffer() + sizeof(uint64), outStream.GetPosition() - sizeof(uint64), 0 );
//}

uint32 vaGraphicsPSODescDX12::FillKeyFast( uint8 * __restrict buffer ) const
{
    size_t dbgSizeOfThis = sizeof( *this ); dbgSizeOfThis;
    assert( dbgSizeOfThis == 208 ); // size of the structure changed, did you change the key creation too?

    struct Data
    {
        uint64      HashKey;
        int64       VSUniqueContentsID;
        int64       PSUniqueContentsID;
        int64       DSUniqueContentsID;
        int64       HSUniqueContentsID;
        int64       GSUniqueContentsID;

        int32       RTVFormats[countof( vaGraphicsPSODescDX12::RTVFormats )];
        int32       DSVFormat;
        uint32      SampleDescCount;

        int8        BlendMode;
        int8        FillMode;
        int8        CullMode;
        int8        DepthFunc;
        int8        Topology;
        int8        NumRenderTargets;
        int8        FrontCounterClockwise;
        int8        MultisampleEnable;

        int8        DepthEnable;
        int8        DepthWriteEnable;

        // Last member is padded with the number of bytes required so that the total size of the structure should be a multiple of the largest alignment of any structure member
        int16       Padding0;
        uint32      Padding1;
    };

    Data & __restrict data = *reinterpret_cast<Data*>(buffer);

    data.VSUniqueContentsID     = VSUniqueContentsID;
    data.PSUniqueContentsID     = PSUniqueContentsID;
    data.DSUniqueContentsID     = DSUniqueContentsID;
    data.HSUniqueContentsID     = HSUniqueContentsID;
    data.GSUniqueContentsID     = GSUniqueContentsID;

    data.BlendMode              = (int8)BlendMode;
    data.FillMode               = (int8)FillMode;
    data.CullMode               = (int8)CullMode;
    data.FrontCounterClockwise  = (FrontCounterClockwise)?(1):(0);
    data.MultisampleEnable      = (MultisampleEnable    )?(1):(0);
    data.DepthEnable            = (DepthEnable          )?(1):(0);
    data.DepthWriteEnable       = (DepthWriteEnable     )?(1):(0);
    data.DepthFunc              = (int8)DepthFunc;
    data.Topology               = (int8)Topology;
    data.NumRenderTargets       = (int8)NumRenderTargets;
    data.Padding0               = 0;
    data.Padding1               = 0;

    for( int i = 0; i < countof( this->RTVFormats ); i++ )
        data.RTVFormats[i ] = static_cast<int32>( this->RTVFormats[i] );
    data.DSVFormat              = static_cast<int32>( this->DSVFormat );;
    data.SampleDescCount        = this->SampleDescCount;

    int sizeofData = sizeof( Data ); 
    assert( sizeofData == 104 );
    assert( sizeofData < vaGraphicsPSODX12::c_keyStorageSize );
    data.HashKey = vaXXHash64::Compute( buffer + sizeof( uint64 ), sizeofData - sizeof( uint64 ), 0 );
    return sizeofData;
}

void vaComputePSODescDX12::FillComputePipelineStateDesc( D3D12_COMPUTE_PIPELINE_STATE_DESC & outDesc, ID3D12RootSignature * pRootSignature ) const
{
    assert( CSBlob != nullptr && CSUniqueContentsID != -1 );
    outDesc.pRootSignature    = pRootSignature;
    outDesc.CS                = (CSBlob != nullptr)?( CD3DX12_SHADER_BYTECODE(CSBlob.Get()) ):(D3D12_SHADER_BYTECODE({0, 0}));
    outDesc.NodeMask          = 0;
    outDesc.CachedPSO         = { nullptr, 0 };
    outDesc.Flags             = D3D12_PIPELINE_STATE_FLAG_NONE; // for warp devices automatically use D3D12_PIPELINE_STATE_FLAG_TOOL_DEBUG?
}

//void vaComputePSODescDX12::FillKey( vaMemoryStream & outStream ) const
//{
//    size_t dbgSizeOfThis = sizeof(*this); dbgSizeOfThis;
//    assert( dbgSizeOfThis == 16 ); // size of the structure changed, did you change the key creation too?
//    
//    assert( outStream.GetPosition() == 0 );
//
//    outStream.WriteValue<int64>( CSUniqueContentsID );
//}

uint32 vaComputePSODescDX12::FillKeyFast( uint8 * buffer ) const
{
    size_t dbgSizeOfThis = sizeof( *this ); dbgSizeOfThis;
    assert( dbgSizeOfThis == 24 ); // size of the structure changed, did you change the key creation too?

    struct Data
    {
        uint64      HashKey;
        int64       CSUniqueContentsID;
    };

    Data& data = *reinterpret_cast<Data*>( buffer );

    data.CSUniqueContentsID = CSUniqueContentsID;

    int sizeofData = sizeof( Data ); sizeofData;
    assert( sizeofData == 16 );
    assert( sizeofData < vaComputePSODX12::c_keyStorageSize );

    data.HashKey = vaXXHash64::Compute( buffer + sizeof( uint64 ), sizeofData - sizeof( uint64 ), 0 );
    
    return sizeofData;
}

void vaGraphicsPSODX12::CreatePSO( vaRenderDeviceDX12 & device, ID3D12RootSignature * rootSignature )
{ 
#ifdef _DEBUG
    // this should never happen - only one thread can ever call CreatePSO
    if( m_pso != nullptr )
    {
        assert( false );
        m_desc.CleanPointers();
        return;
    }
#endif

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc;
    m_desc.FillGraphicsPipelineStateDesc( desc, rootSignature );

    // VA_TRACE_CPU_SCOPE( D3D12_CreateGraphicsPipelineState );

    ID3D12PipelineState * pso = nullptr;

    auto res = device.GetPlatformDevice()->CreateGraphicsPipelineState( &desc, IID_PPV_ARGS(&pso) );
    assert( SUCCEEDED(res) ); res;
    m_desc.CleanPointers( );

    pso = m_pso.exchange( pso, std::memory_order_relaxed );
    assert( pso == nullptr );   // this should never happen
    SAFE_RELEASE( pso );
}

void vaComputePSODX12::CreatePSO( vaRenderDeviceDX12 & device, ID3D12RootSignature * rootSignature )
{ 
    if( m_pso != nullptr )
    {
        assert( false );
        m_desc.CleanPointers( );
        return;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC desc;
    m_desc.FillComputePipelineStateDesc( desc, rootSignature );

    ID3D12PipelineState* pso = nullptr;

    auto res = device.GetPlatformDevice()->CreateComputePipelineState( &desc, IID_PPV_ARGS(&pso) );
    assert( SUCCEEDED(res) ); res;
    m_desc.CleanPointers( );

    pso = m_pso.exchange( pso, std::memory_order_relaxed );
    assert( pso == nullptr );   // this should never happen
    SAFE_RELEASE( pso );
}

// Local raytracing stuff here
namespace
{
    // Shader record = {{Shader ID}, {RootArguments}}
    class ShaderRecord
    {
    public:
        ShaderRecord( void* pShaderIdentifier, UINT shaderIdentifierSize ) :
            shaderIdentifier( pShaderIdentifier, shaderIdentifierSize )
        {
        }

        ShaderRecord( void* pShaderIdentifier, UINT shaderIdentifierSize, void* pLocalRootArguments, UINT localRootArgumentsSize ) :
            shaderIdentifier( pShaderIdentifier, shaderIdentifierSize ),
            localRootArguments( pLocalRootArguments, localRootArgumentsSize )
        {
        }

        void CopyTo( void * dest ) const
        {
            uint8_t* byteDest = static_cast<uint8_t*>( dest );
            // it's fine for the record to be null in the setup (because you have to PusBack something), just don't actually call it from the shaders
            if( shaderIdentifier.ptr == nullptr )
                memset( byteDest, 0, shaderIdentifier.size );
            else
                memcpy( byteDest, shaderIdentifier.ptr, shaderIdentifier.size );
            if( localRootArguments.ptr )
            {
                memcpy( byteDest + shaderIdentifier.size, localRootArguments.ptr, localRootArguments.size );
            }
        }

        struct PointerWithSize {
            void* ptr;
            UINT size;

            PointerWithSize( ) : ptr( nullptr ), size( 0 ) {}
            PointerWithSize( void* _ptr, UINT _size ) : ptr( _ptr ), size( _size ) {};
        };
        PointerWithSize shaderIdentifier;
        PointerWithSize localRootArguments;
    };
    // Shader table = {{ ShaderRecord 1}, {ShaderRecord 2}, ...}
    class ShaderTable
    {
        uint8_t*                    m_mappedShaderRecords;
        UINT                        m_shaderRecordSize;

        shared_ptr<vaRenderBuffer>  m_bufferGPU;

        // Debug support
        std::string                 m_name;
        std::vector<ShaderRecord>   m_shaderRecords;

        ShaderTable( )              { }
    public:
        ShaderTable( vaRenderDevice & device, UINT numShaderRecords, UINT shaderRecordSize, const string & resourceName = nullptr )
            : m_name( resourceName )
        {
            m_shaderRecordSize = vaMath::Align( shaderRecordSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT );
            m_shaderRecords.reserve( numShaderRecords );
            UINT bufferSize = numShaderRecords * m_shaderRecordSize;
            
            m_bufferGPU = vaRenderBuffer::Create( device, (uint64)bufferSize, 1, vaRenderBufferFlags::Upload, resourceName );
            //m_bufferGPU->Map( vaResourceMapType::Write );

            m_mappedShaderRecords = (uint8_t*)m_bufferGPU->GetMappedData();
        }

        void PushBack( const ShaderRecord & shaderRecord )
        {
            if( m_shaderRecords.size( ) >= m_shaderRecords.capacity( ) )
                { assert( false ); abort(); }
            m_shaderRecords.push_back( shaderRecord );
            shaderRecord.CopyTo( m_mappedShaderRecords );
            m_mappedShaderRecords += m_shaderRecordSize;

            if( m_shaderRecords.size( ) == m_shaderRecords.capacity( ) )
            {
                //m_bufferGPU->Unmap();
                m_mappedShaderRecords = nullptr;
            }
        }

        UINT GetShaderRecordSize( ) { return m_shaderRecordSize; }

        // Pretty-print the shader records.
        void DebugPrint( std::unordered_map<void*, std::string> shaderIdToStringMap )
        {
            std::stringstream str;
            str << "|--------------------------------------------------------------------\n";
            str << "|Shader table - " << m_name.c_str( ) << ": "
                << m_shaderRecordSize << " | "
                << m_shaderRecords.size( ) * m_shaderRecordSize << " bytes\n";

            for( UINT i = 0; i < m_shaderRecords.size( ); i++ )
            {
                str << "| [" << i << "]: ";
                str << shaderIdToStringMap[m_shaderRecords[i].shaderIdentifier.ptr] << ", ";
                str << m_shaderRecords[i].shaderIdentifier.size << " + " << m_shaderRecords[i].localRootArguments.size << " bytes \n";
            }
            str << "|--------------------------------------------------------------------\n";
            str << "\n";
            OutputDebugStringA( str.str( ).c_str( ) );
        }

        const shared_ptr<vaRenderBuffer> & GetBuffer( )                { return m_bufferGPU; }
    };

    // Pretty-print a state object tree.
    void PrintStateObjectDesc(const D3D12_STATE_OBJECT_DESC* desc)
    {
        std::wstringstream str;
        str << L"\n";
        str << L"--------------------------------------------------------------------\n";
        str << L"| D3D12 State Object 0x" << static_cast<const void*>(desc) << L": ";
        if (desc->Type == D3D12_STATE_OBJECT_TYPE_COLLECTION) str << L"Collection\n";
        if (desc->Type == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE) str << L"Raytracing Pipeline\n";

        auto ExportTree = [](UINT depth, UINT numExports, const D3D12_EXPORT_DESC* exports)
        {
            std::wostringstream woss;
            for (UINT i = 0; i < numExports; i++)
            {
                woss << L"|";
                if (depth > 0)
                {
                    for (UINT j = 0; j < 2 * depth - 1; j++) woss << L" ";
                }
                woss << L" [" << i << L"]: ";
                if (exports[i].ExportToRename) woss << exports[i].ExportToRename << L" --> ";
                woss << exports[i].Name << L"\n";
            }
            return woss.str();
        };

        for (UINT i = 0; i < desc->NumSubobjects; i++)
        {
            str << L"| [" << i << L"]: ";
            switch (desc->pSubobjects[i].Type)
            {
            case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
                str << L"Global Root Signature 0x" << desc->pSubobjects[i].pDesc << L"\n";
                break;
            case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
                str << L"Local Root Signature 0x" << desc->pSubobjects[i].pDesc << L"\n";
                break;
            case D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK:
                str << L"Node Mask: 0x" << std::hex << std::setfill(L'0') << std::setw(8) << *static_cast<const UINT*>(desc->pSubobjects[i].pDesc) << std::setw(0) << std::dec << L"\n";
                break;
            case D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY:
            {
                str << L"DXIL Library 0x";
                auto lib = static_cast<const D3D12_DXIL_LIBRARY_DESC*>(desc->pSubobjects[i].pDesc);
                str << lib->DXILLibrary.pShaderBytecode << L", " << lib->DXILLibrary.BytecodeLength << L" bytes\n";
                str << ExportTree(1, lib->NumExports, lib->pExports);
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION:
            {
                str << L"Existing Library 0x";
                auto collection = static_cast<const D3D12_EXISTING_COLLECTION_DESC*>(desc->pSubobjects[i].pDesc);
                str << collection->pExistingCollection << L"\n";
                str << ExportTree(1, collection->NumExports, collection->pExports);
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
            {
                str << L"Subobject to Exports Association (Subobject [";
                auto association = static_cast<const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(desc->pSubobjects[i].pDesc);
                UINT index = static_cast<UINT>(association->pSubobjectToAssociate - desc->pSubobjects);
                str << index << L"])\n";
                for (UINT j = 0; j < association->NumExports; j++)
                {
                    str << L"|  [" << j << L"]: " << association->pExports[j] << L"\n";
                }
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
            {
                str << L"DXIL Subobjects to Exports Association (";
                auto association = static_cast<const D3D12_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(desc->pSubobjects[i].pDesc);
                str << association->SubobjectToAssociate << L")\n";
                for (UINT j = 0; j < association->NumExports; j++)
                {
                    str << L"|  [" << j << L"]: " << association->pExports[j] << L"\n";
                }
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG:
            {
                str << L"Raytracing Shader Config\n";
                auto config = static_cast<const D3D12_RAYTRACING_SHADER_CONFIG*>(desc->pSubobjects[i].pDesc);
                str << L"|  [0]: Max Payload Size: " << config->MaxPayloadSizeInBytes << L" bytes\n";
                str << L"|  [1]: Max Attribute Size: " << config->MaxAttributeSizeInBytes << L" bytes\n";
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:
            {
                str << L"Raytracing Pipeline Config\n";
                auto config = static_cast<const D3D12_RAYTRACING_PIPELINE_CONFIG*>(desc->pSubobjects[i].pDesc);
                str << L"|  [0]: Max Recursion Depth: " << config->MaxTraceRecursionDepth << L"\n";
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP:
            {
                str << L"Hit Group (";
                auto hitGroup = static_cast<const D3D12_HIT_GROUP_DESC*>(desc->pSubobjects[i].pDesc);
                str << (hitGroup->HitGroupExport ? hitGroup->HitGroupExport : L"[none]") << L")\n";
                str << L"|  [0]: Any Hit Import: " << (hitGroup->AnyHitShaderImport ? hitGroup->AnyHitShaderImport : L"[none]") << L"\n";
                str << L"|  [1]: Closest Hit Import: " << (hitGroup->ClosestHitShaderImport ? hitGroup->ClosestHitShaderImport : L"[none]") << L"\n";
                str << L"|  [2]: Intersection Import: " << (hitGroup->IntersectionShaderImport ? hitGroup->IntersectionShaderImport : L"[none]") << L"\n";
                break;
            }
            }
            str << L"|--------------------------------------------------------------------\n";
        }
        str << L"\n";
        OutputDebugStringW(str.str().c_str());
    }
}

void vaRaytracePSODescDX12::CleanPointers( )
{
    ItemSLBlob = nullptr;
}

uint32 vaRaytracePSODescDX12::FillKeyFast( uint8 * __restrict buffer ) const
{
    size_t dbgSizeOfThis = sizeof( *this ); dbgSizeOfThis;
    assert( dbgSizeOfThis == 400 ); // size of the structure changed, did you change the key creation too?

    struct Data
    {
        uint64      HashKey;
        int64       ItemSLUniqueContentsID;
        int64       MaterialsSLUniqueContentsID;
        wchar_t     ItemSLEntryRayGen[c_maxNameBufferSize];
        wchar_t     ItemSLEntryAnyHit[c_maxNameBufferSize];
        wchar_t     ItemSLEntryClosestHit[c_maxNameBufferSize];
        wchar_t     ItemSLEntryMiss[c_maxNameBufferSize];
        wchar_t     ItemSLEntryMissSecondary[c_maxNameBufferSize];
        wchar_t     ItemMaterialAnyHit[c_maxNameBufferSize];
        wchar_t     ItemMaterialClosestHit[c_maxNameBufferSize];
        wchar_t     ItemMaterialCallable[c_maxNameBufferSize];
        wchar_t     ItemMaterialMissCallable[c_maxNameBufferSize];

        uint32      MaxRecursionDepth;
        uint32      MaxPayloadSize;

        // Last member is padded with the number of bytes required so that the total size of the structure should be a multiple of the largest alignment of any structure member
        // uint32      Padding0;
        // uint32      Padding1;
    };

    Data & __restrict data = *reinterpret_cast<Data*>(buffer);

    data.ItemSLUniqueContentsID             = ItemSLUniqueContentsID;
    data.MaterialsSLUniqueContentsID        = MaterialsSLUniqueContentsID;

    memset( data.ItemSLEntryRayGen,         0,  sizeof(data.ItemSLEntryRayGen) );
    memset( data.ItemSLEntryAnyHit,         0,  sizeof(data.ItemSLEntryAnyHit) );
    memset( data.ItemSLEntryClosestHit,     0,  sizeof(data.ItemSLEntryClosestHit) );
    memset( data.ItemSLEntryMiss,           0,  sizeof(data.ItemSLEntryMiss) );
    memset( data.ItemSLEntryMissSecondary,  0,  sizeof(data.ItemSLEntryMissSecondary) );
    memset( data.ItemMaterialAnyHit,        0,  sizeof(data.ItemMaterialAnyHit    ) );
    memset( data.ItemMaterialClosestHit,    0,  sizeof(data.ItemMaterialClosestHit) );
    memset( data.ItemMaterialCallable,      0,  sizeof(data.ItemMaterialCallable  ) );
    memset( data.ItemMaterialMissCallable,  0,  sizeof(data.ItemMaterialMissCallable  ) );
    ItemSLEntryRayGen       .copy( data.ItemSLEntryRayGen       , std::string::npos );
    ItemSLEntryAnyHit       .copy( data.ItemSLEntryAnyHit       , std::string::npos );
    ItemSLEntryClosestHit   .copy( data.ItemSLEntryClosestHit   , std::string::npos );
    ItemSLEntryMiss         .copy( data.ItemSLEntryMiss         , std::string::npos );
    ItemSLEntryMissSecondary.copy( data.ItemSLEntryMissSecondary, std::string::npos );
    ItemMaterialAnyHit      .copy( data.ItemMaterialAnyHit      , std::string::npos );
    ItemMaterialClosestHit  .copy( data.ItemMaterialClosestHit  , std::string::npos );
    ItemMaterialCallable    .copy( data.ItemMaterialCallable    , std::string::npos );
    ItemMaterialMissCallable.copy( data.ItemMaterialMissCallable, std::string::npos );

    data.MaxRecursionDepth                  = MaxRecursionDepth;
    data.MaxPayloadSize                     = MaxPayloadSize;

    int sizeofData = sizeof( Data ); 
    assert( sizeofData == 896 );
    assert( sizeofData < vaRaytracePSODX12::c_keyStorageSize );
    data.HashKey = vaXXHash64::Compute( buffer + sizeof( uint64 ), sizeofData - sizeof( uint64 ), 0 );
    return sizeofData;
}

bool vaRaytracePSODescDX12::FillPipelineStateDesc( CD3DX12_STATE_OBJECT_DESC & outDesc, ID3D12RootSignature * pRootSignature, const vaRenderMaterialManagerDX12 & materialManager12 ) const
{
    // expecting to be inited with CD3DX12_STATE_OBJECT_DESC raytracingPipeline{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };
    assert( ItemSLBlob != nullptr );

    assert( ItemSLEntryRayGen       .size() < vaRaytracePSODescDX12::c_maxNameBufferSize );
    assert( ItemSLEntryAnyHit       .size() < vaRaytracePSODescDX12::c_maxNameBufferSize );
    assert( ItemSLEntryClosestHit   .size() < vaRaytracePSODescDX12::c_maxNameBufferSize );
    assert( ItemSLEntryMiss         .size() < vaRaytracePSODescDX12::c_maxNameBufferSize );
    assert( ItemSLEntryMissSecondary.size() < vaRaytracePSODescDX12::c_maxNameBufferSize );
    assert( ItemMaterialAnyHit      .size() < vaRaytracePSODescDX12::c_maxNameBufferSize );
    assert( ItemMaterialClosestHit  .size() < vaRaytracePSODescDX12::c_maxNameBufferSize );
    assert( ItemMaterialCallable    .size() < vaRaytracePSODescDX12::c_maxNameBufferSize );
    assert( ItemMaterialMissCallable.size() < vaRaytracePSODescDX12::c_maxNameBufferSize );

    const std::vector<vaRenderMaterialManagerDX12::CallableShaders> & materialCallablesTable = materialManager12.GetCallablesTable( );              // this is per-material
    const std::unordered_map<vaFramePtr<vaShaderDataDX12>, uint32> & uniqueCallableLibraries = materialManager12.GetUniqueCallableLibraries( );     // this is per-material-shader - some materials share the same set of shaders

    // At the moment disallow incomplete raytracing PSO-s - all shaders must compile for any to work
    for( auto matLibIt : uniqueCallableLibraries )
    {
        if( matLibIt.first == nullptr )
        {
            // null entry in shader table should be valid as per the specs
            return false;
            continue;
        }
        const vaRenderMaterialManagerDX12::CallableShaders & materialCallables = materialCallablesTable[matLibIt.second];
        if( materialCallables.LibraryBlob == nullptr )
        {
            // to find out which material has broken shaders use this: materialCallables.MaterialID
            VA_LOG( " ** Unable to build raytracing PSO - compile errors or waiting on all shaders to complete compiling (in which case, please wait a bit longer :) ) ** " );
            return false;
        }
    }

    // Create the subobjects that combine into a RTPSO

    // The "item" shader library contains the raygen and (optionally) AnyHit, ClosestHit and Miss shaders
    {
        auto itemLib = outDesc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>( );
        D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE( ItemSLBlob->GetBufferPointer(), ItemSLBlob->GetBufferSize() ); //(void*)g_pRaytracing, ARRAYSIZE( g_pRaytracing ) );
        itemLib->SetDXILLibrary( &libdxil );

        // Define which shader exports to surface from the library.
        {
            assert( ItemSLEntryRayGen != L"" );
            itemLib->DefineExport( ItemSLEntryRayGen.c_str() );
            if( ItemSLEntryAnyHit != L"" )
                itemLib->DefineExport( ItemSLEntryAnyHit.c_str() );
            if( ItemSLEntryClosestHit != L"" )
                itemLib->DefineExport( ItemSLEntryClosestHit.c_str() );
            if( ItemSLEntryMiss != L"" )
                itemLib->DefineExport( ItemSLEntryMiss.c_str() );
            if( ItemSLEntryMissSecondary != L"" )
                itemLib->DefineExport( ItemSLEntryMissSecondary.c_str() );
        }
    }

    assert( MaterialsSLUniqueContentsID == materialManager12.GetCallablesTableID( ) );

    // Expose all material callables - anyhit/closesthit for hitgroups or standalone callables
    for( auto matLibIt : uniqueCallableLibraries )
    {
        if( matLibIt.first == nullptr )
        {
            // null entry in shader table should be valid as per the specs
            return false;
            continue;
        }
        const vaRenderMaterialManagerDX12::CallableShaders & materialCallables = materialCallablesTable[matLibIt.second];
        assert( materialCallables.LibraryBlob == matLibIt.first );
        auto libSubObj = outDesc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>( );
        D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE( materialCallables.LibraryBlob->GetBufferPointer(), materialCallables.LibraryBlob->GetBufferSize() );
        libSubObj->SetDXILLibrary( &libdxil );

        // "surface" per-material-library shaders (and rename to unique per material-shader ID)
        if( ItemMaterialAnyHit != L"" )
            libSubObj->DefineExport( (ItemMaterialAnyHit+materialCallables.UniqueIDString).c_str(), ItemMaterialAnyHit.c_str(), D3D12_EXPORT_FLAG_NONE );
        if( ItemMaterialClosestHit != L"" )
            libSubObj->DefineExport( (ItemMaterialClosestHit+materialCallables.UniqueIDString).c_str(), ItemMaterialClosestHit.c_str(), D3D12_EXPORT_FLAG_NONE );
        if( ItemMaterialCallable != L"" )
            libSubObj->DefineExport( (ItemMaterialCallable+materialCallables.UniqueIDString).c_str(), ItemMaterialCallable.c_str(), D3D12_EXPORT_FLAG_NONE );
        if( ItemMaterialMissCallable != L"" )
            libSubObj->DefineExport( (ItemMaterialMissCallable+materialCallables.UniqueIDString).c_str(), ItemMaterialMissCallable.c_str(), D3D12_EXPORT_FLAG_NONE );

        // and now define the hit group! also name it so it's per material-shader unique
        {
            auto hitGroup = outDesc.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>( );

            // ClosestHit
            if( ItemSLEntryClosestHit != L"" )
                hitGroup->SetClosestHitShaderImport( ItemSLEntryClosestHit.c_str() );
            else if( ItemMaterialClosestHit != L"" )
                hitGroup->SetClosestHitShaderImport( (ItemMaterialClosestHit+materialCallables.UniqueIDString).c_str() );
            else
                {   assert( false ); } // no default closest hit exposed by materials yet but that could be done easily

            // AnyHit
            if( ItemSLEntryAnyHit != L"" )
                hitGroup->SetAnyHitShaderImport( ItemSLEntryAnyHit.c_str() );
            else if( ItemMaterialAnyHit != L"" )
                hitGroup->SetAnyHitShaderImport( (ItemMaterialAnyHit+materialCallables.UniqueIDString).c_str() );
            else
                {   assert( false ); } // no default closest hit exposed by materials yet but that could be done easily

            hitGroup->SetHitGroupExport( (L"HitGroup_"+materialCallables.UniqueIDString).c_str() );
            hitGroup->SetHitGroupType( D3D12_HIT_GROUP_TYPE_TRIANGLES );
        }
    }

    // Some additional shader config
    // Maximum sizes in bytes for the ray payload and attribute structure is hacked in here but this could/should be a parameter in vaRaytraceItem
    auto shaderConfig = outDesc.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>( );
    UINT payloadSize    = MaxPayloadSize; assert( MaxPayloadSize > 0 );
    UINT attributeSize  = 2 * sizeof( float ); // float2 barycentrics 
    shaderConfig->Config( payloadSize, attributeSize );

    // Local root signature and shader association - a root signature that enables a shader to have unique arguments that come from shader tables. We don't use them at the moment!!
    // CreateLocalRootSignatureSubobjects( m_raytracingLocalRootSignature, &outDesc );

    // Global root signature
    // This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
    auto globalRootSignature = outDesc.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>( );
    globalRootSignature->SetRootSignature( pRootSignature ); // <- this is Vanilla's global root signature

    // Pipeline config
    // Defines the maximum TraceRay() recursion depth.
    auto pipelineConfig = outDesc.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>( );
    // PERFOMANCE TIP: Set max recursion depth as low as needed as drivers may apply optimization strategies for low recursion depths. 
    pipelineConfig->Config( MaxRecursionDepth );

// #if _DEBUG
//     PrintStateObjectDesc( outDesc );
// #endif
    VA_LOG( "===================================================================================================" );
    VA_LOG( "Raytracing PSO rebuilt, number of materials: %d, number of unique hitgroups & callables: %d", (int)materialCallablesTable.size(), (int)uniqueCallableLibraries.size() );
    VA_LOG( "===================================================================================================" );

    return true;
}

void vaRaytracePSODX12::CreatePSO( vaRenderDeviceDX12 & device, ID3D12RootSignature * rootSignature )
{
    VA_GENERIC_RAII_SCOPE( ;, m_desc.CleanPointers( ); );    // clean pointers when leaving the function (success/fail)

    if( m_pso != nullptr )
    {
        assert( false );
        return;
    }

    const auto & materialManager12 = AsDX12(device.GetMaterialManager());
    std::unique_lock meshLock( materialManager12.Mutex( ) );

    CD3DX12_STATE_OBJECT_DESC desc{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };
    if( !m_desc.FillPipelineStateDesc( desc, rootSignature, materialManager12 ) )
        return;

    Inner * inner = new Inner();
    
    auto deviceDX12 = device.GetPlatformDevice();

    // Create the state object.
    HRESULT hr = deviceDX12->CreateStateObject( desc, IID_PPV_ARGS( &inner->PSO ) );
    // fail gracefully here? :)
    if( FAILED( hr ) )
    {
        assert( false );
        delete inner;
        return;
    }
    inner->PSO->SetName( L"vaRaytracePSODX12_PSO" );

    ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
    hr = inner->PSO.As( &stateObjectProperties ); assert( SUCCEEDED( hr ) ); hr;

    inner->Incomplete = false;

    // build shader tables
    {
        assert( m_desc.MaterialsSLUniqueContentsID == materialManager12.GetCallablesTableID( ) );
        UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

        const std::vector<vaRenderMaterialManagerDX12::CallableShaders> & materialCallablesTable = materialManager12.GetCallablesTable( );

        {
            void * rayGenShaderIdentifier           = stateObjectProperties->GetShaderIdentifier( m_desc.ItemSLEntryRayGen.c_str() );
            void * missShaderIdentifier             = stateObjectProperties->GetShaderIdentifier( m_desc.ItemSLEntryMiss.c_str() );
            void * missShaderSecondaryIdentifier    = (m_desc.ItemSLEntryMissSecondary!=L"")?(stateObjectProperties->GetShaderIdentifier( m_desc.ItemSLEntryMissSecondary.c_str() )):(nullptr);

            assert( rayGenShaderIdentifier   != nullptr );
            assert( missShaderIdentifier     != nullptr );
            
            // Ray gen shader table
            {
                struct ShaderRayGenConstants
                {
                    vaVector4           Something;
                };
                struct RootArguments 
                {
                    ShaderRayGenConstants cb;
                } rootArguments;
                rootArguments.cb = {};

                UINT numShaderRecords = 1;
                UINT shaderRecordSize = shaderIdentifierSize + sizeof( rootArguments );
                ShaderTable rayGenShaderTable( device, numShaderRecords, shaderRecordSize, "RayGenShaderTable" );
                rayGenShaderTable.PushBack( ShaderRecord( rayGenShaderIdentifier, shaderIdentifierSize, &rootArguments, sizeof( rootArguments ) ) );
                inner->RayGenShaderTable = rayGenShaderTable.GetBuffer( );
            }

            // Miss shader table
            {
                UINT numShaderRecords = (m_desc.ItemMaterialMissCallable != L"")?(2+(UINT)materialCallablesTable.size() * vaRenderMaterialManagerDX12::CallableShaders::CallablesPerMaterial):(2); //(missShaderSecondaryIdentifier==nullptr)?(1):(2);
                UINT shaderRecordSize = shaderIdentifierSize;
                ShaderTable missShaderTable( device, numShaderRecords, shaderRecordSize, "MissShaderTable" );
                missShaderTable.PushBack( ShaderRecord( missShaderIdentifier, shaderIdentifierSize ) );
                if( missShaderSecondaryIdentifier != nullptr )
                    missShaderTable.PushBack( ShaderRecord( missShaderSecondaryIdentifier, shaderIdentifierSize ) );
                else
                    missShaderTable.PushBack( ShaderRecord( nullptr, shaderIdentifierSize ) );  // need to insert empty one for correct VA_RAYTRACING_SHADER_MISS_CALLABLES_SHADE_OFFSET
                static_assert( VA_RAYTRACING_SHADER_MISS_CALLABLES_SHADE_OFFSET == 2 );

                // optional Miss-Callables
                if( m_desc.ItemMaterialMissCallable != L"" )
                {
                    for( uint32 index = 0; index < materialCallablesTable.size(); index++ ) 
                    {
                        const vaRenderMaterialManagerDX12::CallableShaders & materialCallables = materialCallablesTable[index];
                        if( materialCallables.LibraryBlob == nullptr )
                        {
                            // this is actually fine - it should never get referenced - only unique ones do
                            //inner->Incomplete = true;
                            for( int i = 0; i < vaRenderMaterialManagerDX12::CallableShaders::CallablesPerMaterial; i++ )
                                missShaderTable.PushBack( ShaderRecord( nullptr, shaderIdentifierSize ) );
                        }
                        else
                        {
                            void * shaderIdentifier     = stateObjectProperties->GetShaderIdentifier( (m_desc.ItemMaterialMissCallable+materialCallables.UniqueIDString).c_str() );
                            assert( shaderIdentifier     != nullptr );
                            missShaderTable.PushBack( ShaderRecord( shaderIdentifier, shaderIdentifierSize ) );
                        }
                    }
                }

                inner->MissShaderTable = missShaderTable.GetBuffer( );
                inner->MissShaderTableStride = shaderRecordSize;
            }
        }

        // Hit groups shader table
        {
            UINT numShaderRecords = (UINT)materialCallablesTable.size();
            UINT shaderRecordSize = shaderIdentifierSize;
            ShaderTable hitGroupShaderTable( device, numShaderRecords, shaderRecordSize, "HitGroupShaderTable" );

            for( uint32 index = 0; index < materialCallablesTable.size(); index++ ) 
            {
                const vaRenderMaterialManagerDX12::CallableShaders & materialCallables = materialCallablesTable[index];
                if( materialCallables.LibraryBlob == nullptr )
                {
                    // this is actually fine - it should never get referenced - only unique ones do
                    // inner->Incomplete = true;
                    hitGroupShaderTable.PushBack( ShaderRecord( nullptr, shaderIdentifierSize ) );
                }
                else
                {
                    void * hitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier( (L"HitGroup_"+materialCallables.UniqueIDString).c_str() );
                    assert( hitGroupShaderIdentifier != nullptr );
                    hitGroupShaderTable.PushBack( ShaderRecord( hitGroupShaderIdentifier, shaderIdentifierSize ) );
                }
            }

            inner->HitGroupShaderTable = hitGroupShaderTable.GetBuffer( );
            inner->HitGroupShaderTableStride = shaderRecordSize;
        }

        // Callables shader table (if any)
        if( m_desc.ItemMaterialCallable != L"" )
        {
            UINT numShaderRecords = (UINT)materialCallablesTable.size() * vaRenderMaterialManagerDX12::CallableShaders::CallablesPerMaterial;
            UINT shaderRecordSize = shaderIdentifierSize;
            ShaderTable callableShaderTable( device, numShaderRecords, shaderRecordSize, "CallablesShaderTable" );
            for( uint32 index = 0; index < materialCallablesTable.size(); index++ ) 
            {
                const vaRenderMaterialManagerDX12::CallableShaders & materialCallables = materialCallablesTable[index];
                if( materialCallables.LibraryBlob == nullptr )
                {
                    inner->Incomplete = true;
                    for( int i = 0; i < vaRenderMaterialManagerDX12::CallableShaders::CallablesPerMaterial; i++ )
                        callableShaderTable.PushBack( ShaderRecord( nullptr, shaderIdentifierSize ) );
                }
                else
                {
                    void * shaderIdentifier     = stateObjectProperties->GetShaderIdentifier( (m_desc.ItemMaterialCallable+materialCallables.UniqueIDString).c_str() );
                    assert( shaderIdentifier     != nullptr );
                    //callableShaderTable.PushBack( ShaderRecord( hitTestIdentifier, shaderIdentifierSize ) );
                    callableShaderTable.PushBack( ShaderRecord( shaderIdentifier, shaderIdentifierSize ) );
                }
            }
            inner->CallableShaderTable = callableShaderTable.GetBuffer( );
            inner->CallableShaderTableStride = shaderRecordSize;
        }
        else
        {
            inner->CallableShaderTable          = nullptr;
            inner->CallableShaderTableStride    = 0;
        }
    }

    // 'incomplete' means some shader identifiers are set to null (shaders still compiling, etc.) - this should be perfectly legal from
    // the API side as far the docs say but example 'ShaderRecord' code didn't support it and there's a random crash that could be
    // associated with it, so let's not allow it for now.
    if( inner->Incomplete )
    {
        VA_LOG( "ray tracing PSO incomplete - shader had an error or did not finish compiling" );

        delete inner;
        inner = nullptr;
        hr = E_FAIL;
    }
    
    inner = m_pso.exchange( inner, std::memory_order_relaxed );
    assert( inner == nullptr );   // this should never happen
    if( inner != nullptr )
        delete inner;
}
