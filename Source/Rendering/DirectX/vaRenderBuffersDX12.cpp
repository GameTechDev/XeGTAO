///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaRenderBuffersDX12.h"

#include "Rendering/DirectX/vaRenderDeviceContextDX12.h"

#pragma warning ( disable : 4238 )  //  warning C4238: nonstandard extension used: class rvalue used as lvalue

using namespace Vanilla;

void vaUploadBufferDX12::Construct( uint64 sizeInBytes, const wstring & resourceName )
{
    m_size          = sizeInBytes;
    
    HRESULT hr;
    V( m_device.GetPlatformDevice( )->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer( m_size ),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS( &m_resource ) ) );
    m_resource->SetName( resourceName.c_str() );

    assert( sizeInBytes <= UINT_MAX ); // looks like we can't use bigger than 4GB buffers? D3D12_CONSTANT_BUFFER_VIEW_DESC::SizeInBytes is UINT?
    m_CBV = { m_resource->GetGPUVirtualAddress( ), (UINT)m_size };
    m_desc = m_resource->GetDesc();

    CD3DX12_RANGE readRange( 0, 0 );        // We do not intend to read from this resource on the CPU.
    if( FAILED( hr = m_resource->Map( 0, &readRange, reinterpret_cast<void**>( &m_mappedData ) ) ) )
    {
        assert( false );
    }
}

vaUploadBufferDX12::vaUploadBufferDX12( vaRenderDeviceDX12 & device, uint64 sizeInBytes, const wstring & resourceName )
    : m_device( device )
{
    Construct( sizeInBytes, resourceName );
}

vaUploadBufferDX12::vaUploadBufferDX12( vaRenderDeviceDX12 & device, const void * initialContents, uint64 sizeInBytes, const wstring & resourceName )
    : m_device( device )
{
    Construct( sizeInBytes, resourceName );
    assert( m_mappedData != nullptr );
    if( initialContents != nullptr )
        memcpy( m_mappedData, initialContents, sizeInBytes );
}

vaUploadBufferDX12::~vaUploadBufferDX12( )
{
    if( m_mappedData != nullptr )
    {
        m_resource->Unmap( 0, nullptr );
        m_mappedData = nullptr;
    }
    m_device.SafeReleaseAfterCurrentGPUFrameDone( m_resource, true );
}


vaConstantBufferDX12::DetachableUploadBuffer::DetachableUploadBuffer( vaRenderDeviceDX12 & device, ComPtr<ID3D12Resource> resource, uint64 totalSizeInBytes ) 
    : Device( device ), Resource( resource )
{  
    //RSTHAttach( m_resource.Get(), D3D12_RESOURCE_STATE_GENERIC_READ );
    assert( resource != nullptr );
    assert( totalSizeInBytes <= UINT_MAX ); // looks like we can't use bigger than 4GB buffers? D3D12_CONSTANT_BUFFER_VIEW_DESC::SizeInBytes is UINT?
    CBV = { Resource->GetGPUVirtualAddress(), (UINT)totalSizeInBytes };    
    HRESULT hr;
    CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
    if( SUCCEEDED( hr = Resource->Map( 0, &readRange, reinterpret_cast<void**>(&MappedData) ) ) )
    {
    }
    else
    {
        assert( false );
        MappedData = nullptr;
    }
}
vaConstantBufferDX12::DetachableUploadBuffer::~DetachableUploadBuffer( )
{
    if( MappedData != nullptr )
    {
        Resource->Unmap(0, nullptr);
        MappedData = nullptr;
    }
    //if( DestroyImmediate )
    //    Resource.Reset();
    //else
    Device.SafeReleaseAfterCurrentGPUFrameDone( Resource, false );
}

vaConstantBufferDX12::vaConstantBufferDX12( const vaRenderingModuleParams & params ) : vaConstantBuffer( params ), m_deviceDX12( AsDX12(params.RenderDevice) )
{
}

vaConstantBufferDX12::~vaConstantBufferDX12( )
{
    assert( vaThreading::IsMainThread( ) );
    DestroyInternal();
}

void vaConstantBufferDX12::AllocateNextUploadBuffer( )
{
    assert( m_uploadConstantBuffer == nullptr );

    int safeToAllocatePool = (int)(m_deviceDX12.GetCurrentFrameIndex() % countof(m_unusedBuffersPools));

    while( m_unusedBuffersPools[safeToAllocatePool].size( ) > 0 )
    {
        auto back = m_unusedBuffersPools[safeToAllocatePool].back();
        m_unusedBuffersPools[safeToAllocatePool].pop_back();
        if( back->CBV.SizeInBytes == m_actualTotalSizeInBytes )
        {
            m_uploadConstantBuffer = back;
            break;
        }
        else
        {
            assert( false ); // why different size? it won't crash here and will cleanup the buffer but it doesn't make sense logic-wise (or the logic changed?)
            delete back;
        }
    }

    if( m_uploadConstantBuffer == nullptr )
    {
        ComPtr<ID3D12Resource> resource;
        HRESULT hr;
        V( m_deviceDX12.GetPlatformDevice()->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer( m_actualTotalSizeInBytes ),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&resource)) );
        resource->SetName( m_resourceName.c_str() ); // add upload suffix?

        m_uploadConstantBuffer = new DetachableUploadBuffer( m_deviceDX12, resource, m_actualTotalSizeInBytes );
    }
}

void vaConstantBufferDX12::SafeReleaseUploadBuffer( DetachableUploadBuffer * uploadBuffer, vaRenderDeviceContextBaseDX12 * renderContextPtr )
{
    renderContextPtr;
    int safeToReleasePool = (int)( (m_deviceDX12.GetCurrentFrameIndex( ) + countof( m_unusedBuffersPools ) - 1 ) % countof( m_unusedBuffersPools ) );
    m_unusedBuffersPools[safeToReleasePool].push_back( uploadBuffer );
}

bool vaConstantBufferDX12::Create( int bufferSize, const string & name, const void * initialData, bool dynamic, int deviceContextIndex )
{
    m_deviceContextIndex = deviceContextIndex;

    DestroyInternal(/*false*/);

    m_createdThis = std::make_shared<vaConstantBufferDX12*>( this );
    m_resourceName = vaStringTools::SimpleWiden(name);

    assert( m_uploadConstantBuffer == nullptr );

    assert( bufferSize > 0 );
    if( bufferSize <= 0 )
        return false;

    m_dynamic       = dynamic;

    m_dataSize      = bufferSize;
    const uint32 alignUpToBytes = 256;
    m_actualSizeInBytes = ((m_dataSize-1) / alignUpToBytes + 1) * alignUpToBytes;
    m_actualTotalSizeInBytes = m_actualSizeInBytes * ((m_dynamic)?(c_dynamicChunkCount):(1) );
    m_currentChunk  = 0;

    assert( m_actualTotalSizeInBytes <= UINT_MAX ); // looks like we can't use bigger than 4GB buffers? D3D12_CONSTANT_BUFFER_VIEW_DESC::SizeInBytes is UINT?

    AllocateNextUploadBuffer( );
    assert( m_uploadConstantBuffer != nullptr && m_uploadConstantBuffer->MappedData != nullptr );
    if( m_uploadConstantBuffer != nullptr && m_uploadConstantBuffer->MappedData != nullptr )
    {
        if( initialData != nullptr )
            memcpy( m_uploadConstantBuffer->MappedData, initialData, bufferSize );
        else
            memset( m_uploadConstantBuffer->MappedData, 0, bufferSize );
    }

    return true;
}

void vaConstantBufferDX12::Upload( vaRenderDeviceContext & renderContext, const void * data, uint32 dataSize )
{
    assert( renderContext.GetInstanceIndex() == m_deviceContextIndex );
    
    assert( m_dataSize == dataSize );

    m_currentChunk++;
    if( !m_dynamic || m_currentChunk >= c_dynamicChunkCount )
    {
        assert( m_uploadConstantBuffer != nullptr );
        if( m_uploadConstantBuffer != nullptr ) 
        {
            SafeReleaseUploadBuffer( m_uploadConstantBuffer, AsDX12(&renderContext) );
            m_uploadConstantBuffer = nullptr;
        }
        m_currentChunk = 0;
    }

    if( m_uploadConstantBuffer == nullptr )
        AllocateNextUploadBuffer();

    assert( m_uploadConstantBuffer->MappedData != nullptr );
    if( m_uploadConstantBuffer->MappedData != nullptr )
        memcpy( m_uploadConstantBuffer->MappedData + ComputeDynamicOffset(), data, dataSize );
}

void vaConstantBufferDX12::DestroyInternal( /*bool lockMutex*/ )
{
    //std::unique_lock<mutex> mutexLock(m_mutex, std::defer_lock ); if( lockMutex ) mutexLock.lock();

    m_createdThis = nullptr;

    if( m_uploadConstantBuffer != nullptr ) 
    {
        SafeReleaseUploadBuffer( m_uploadConstantBuffer );
        m_uploadConstantBuffer = nullptr;
    }

    m_dataSize          = 0;
    m_actualSizeInBytes = 0;
    m_actualTotalSizeInBytes = 0;
    m_dynamic           = false;
    for( int i = 0; i < countof( m_unusedBuffersPools ); i++ )
    {
        for( DetachableUploadBuffer* buffs : m_unusedBuffersPools[i] )
        {
            // buffs->DestroyImmediate = true;
            delete buffs;
        }
        m_unusedBuffersPools[i].clear( );
    }
}

void vaConstantBufferDX12::Destroy( )
{
    DestroyInternal( /*true */);
}

vaVertIndBufferDX12::DetachableBuffer::DetachableBuffer( vaRenderDeviceDX12 & device, ComPtr<ID3D12Resource> resource, uint32 sizeInBytes, uint32 actualSizeInBytes ) : Device( device ), Resource( resource ), SizeInBytes( sizeInBytes )
{  
    //RSTHAttach( m_resource.Get(), D3D12_RESOURCE_STATE_GENERIC_READ );
    actualSizeInBytes;
    assert( resource != nullptr );
    assert( actualSizeInBytes >= sizeInBytes );
}
vaVertIndBufferDX12::DetachableBuffer::~DetachableBuffer( )
{
    //RSTHDetach( m_resource.Get() );
    Device.SafeReleaseAfterCurrentGPUFrameDone( Resource, false );
}

vaVertIndBufferDX12::vaVertIndBufferDX12( vaRenderDeviceDX12 & device )              
    : m_device( device ) 
{ 
}
vaVertIndBufferDX12::~vaVertIndBufferDX12( )
{
    assert( vaThreading::IsMainThread( ) );
    if( m_buffer != nullptr ) 
    {
        delete m_buffer;
        m_buffer = nullptr;
    }
    for( DetachableBuffer * buffs : m_unusedBuffersPool )
        delete buffs;
    m_unusedBuffersPool.clear();
}

void vaVertIndBufferDX12::Create( int elementCount, int elementSize, const wstring & resourceName, const void * initialData )
{
    assert( !IsMapped() );

    DestroyInternal( false );

    m_createdThis = std::make_shared<vaVertIndBufferDX12*>( this );
    m_resourceName = resourceName;

    assert( elementSize > 0 );
    if( elementCount <= 0 || elementSize <= 0 )
        return;

    m_elementCount  = elementCount;
    m_elementSize   = elementSize;
    m_dataSize      = (uint32)elementCount * elementSize;

    uint32 actualBufferSize = m_dataSize;// vaMath::Max( (uint32)256, m_dataSize );

    while( m_unusedBuffersPool.size( ) > 0 )
    {
        auto back = m_unusedBuffersPool.back();
        m_unusedBuffersPool.pop_back();
        if( back->SizeInBytes == m_dataSize )
        {
            m_buffer = back;
            break;
        }
        else
        {
            assert( false ); // why different size? maybe ok but check it out anyway
            delete back;
        }
    }

    if( m_buffer == nullptr )
    {
        ComPtr<ID3D12Resource> resource;
        HRESULT hr;
        V( m_device.GetPlatformDevice()->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(actualBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,      // TODO - resource state tracking
            nullptr,
            IID_PPV_ARGS(&resource)) );
        resource->SetName( m_resourceName.c_str() ); // add upload suffix?

        m_buffer = new DetachableBuffer( m_device, resource, m_dataSize, actualBufferSize );
    }

    if( initialData != nullptr )
    {
        HRESULT hr;
        UINT8* pDataBegin;
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        V( m_buffer->Resource->Map(0, &readRange, reinterpret_cast<void**>(&pDataBegin)) );
        memcpy(pDataBegin, initialData, m_dataSize);
        m_buffer->Resource->Unmap(0, nullptr);
    }
}

void vaVertIndBufferDX12::Destroy( )
{
    DestroyInternal( true );
}

void vaVertIndBufferDX12::DestroyInternal( bool lockMutex )
{
    lockMutex;
    // std::unique_lock<mutex> mutexLock(m_mutex, std::defer_lock ); if( lockMutex ) mutexLock.lock();

    m_createdThis = nullptr;

    assert( m_mappedData == nullptr ); //assert( !IsMapped() );
    if( m_buffer == nullptr ) 
        return;
    
    weak_ptr<vaVertIndBufferDX12*> thisVIBuffer = m_smartThis;

    m_device.ExecuteAfterCurrentGPUFrameDone(
    [thisVIBuffer, dataSize = m_dataSize, buffer = m_buffer]( vaRenderDeviceDX12 & device )
        {
            device;
            shared_ptr<vaVertIndBufferDX12*> theVIBuffer = thisVIBuffer.lock();
            if( theVIBuffer != nullptr )
            {
                (*theVIBuffer)->m_unusedBuffersPool.push_back( buffer );
            }
            else
            {
                delete buffer;
            }
        } );
    m_mappedData    = nullptr;
    m_elementCount  = 0;
    m_elementSize   = 0;
    m_dataSize      = 0;
    m_buffer        = nullptr;
    m_dataSize      = 0;
}

void vaVertIndBufferDX12::Upload( const void * data, uint32 dataSize )
{
    assert( IsCreated() );
    assert( !IsMapped() );
    if( dataSize != m_dataSize )
    {
        assert( false );
        return;
    }

    // not the most optimal path but it works
    Map( vaResourceMapType::WriteDiscard );
    memcpy( GetMappedData(), data, dataSize );
    Unmap( );
}

bool vaVertIndBufferDX12::Map( vaResourceMapType mapType )
{
    assert( m_device.IsRenderThread() ); // for now...

    if( !IsCreated() || IsMapped() )
        { assert( false ); return false; }

    if( mapType != vaResourceMapType::WriteDiscard && mapType != vaResourceMapType::WriteNoOverwrite )
    {
        assert( false ); // only map types supported on this buffer
        return false;
    }

    HRESULT hr;

    if( mapType == vaResourceMapType::WriteDiscard )
    {
        int elementCount = m_elementCount;
        int elementSize = m_elementSize;
        Create( elementCount, elementSize, m_resourceName, nullptr ); // will destroy and re-create
    }

    // Copy the triangle data to the vertex buffer.
    CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
    if( SUCCEEDED( hr = m_buffer->Resource->Map( 0, &readRange, reinterpret_cast<void**>( &m_mappedData ) ) ) )
    {
        return true;
    }
    else
    {
        m_mappedData = nullptr;
        return false;
    }
}

void vaVertIndBufferDX12::Unmap( )
{
    assert( m_device.IsRenderThread() ); // for now...

    assert( IsMapped() );
    if( !IsMapped() )
        return;
    
    m_mappedData = nullptr;
    m_buffer->Resource->Unmap(0, nullptr);
}

//vaIndexBufferDX12::vaIndexBufferDX12( const vaRenderingModuleParams & params ) : vaIndexBuffer( params ), m_buffer( AsDX12(params.RenderDevice) ) { }

vaDynamicVertexBufferDX12::vaDynamicVertexBufferDX12( const vaRenderingModuleParams & params ) : vaDynamicVertexBuffer( params ), m_buffer( AsDX12(params.RenderDevice) ) { }


vaRenderBufferDX12::vaRenderBufferDX12( const vaRenderingModuleParams & params ) : vaRenderBuffer( params ),
    m_srv( AsDX12(params.RenderDevice) ), m_uav( AsDX12(params.RenderDevice) ), m_uavSimple( AsDX12(params.RenderDevice) )
{
}

vaRenderBufferDX12::~vaRenderBufferDX12( )
{
    Destroy();
    assert( !IsMapped( ) );
}

bool vaRenderBufferDX12::Create( uint64 elementCount, uint32 structByteSize, vaRenderBufferFlags flags, const string & name )
{
    return CreateInternal( elementCount, structByteSize, vaResourceFormat::Unknown, flags, name );
}
bool vaRenderBufferDX12::Create( uint64 elementCount, vaResourceFormat resourceFormat, vaRenderBufferFlags flags, const string & name )
{
    return CreateInternal( elementCount, vaResourceFormatHelpers::GetPixelSizeInBytes(resourceFormat), resourceFormat, flags, name );
}

bool vaRenderBufferDX12::CreateInternal( uint64 elementCount, uint32 structByteSize, vaResourceFormat resourceFormat, vaRenderBufferFlags flags, const string & name )
{
    Destroy( );

    if( ((flags & vaRenderBufferFlags::Readback) != 0 ) && ((flags & vaRenderBufferFlags::Upload) != 0) )
        { assert( false ); return false; }  // can't have upload and readback at the same time
    if( ((flags & vaRenderBufferFlags::VertexIndexBuffer) != 0 ) && ((flags & vaRenderBufferFlags::RaytracingAccelerationStructure) != 0) )
        { assert( false ); return false; }  // raytracing acc struct doesn't mix with others
    if( (((flags & vaRenderBufferFlags::Readback) != 0 ) || ((flags & vaRenderBufferFlags::Upload) != 0)) &&
        (((flags & vaRenderBufferFlags::RaytracingAccelerationStructure) != 0 ) || ((flags & vaRenderBufferFlags::VertexIndexBuffer) != 0)))
        { assert( false ); return false; }  // can't have upload or readback buffers which are raytracing or vert/ind buffers (although upload + vert/ind should be ok?)

    m_flags                         = flags;
    m_dataSize                      = (uint64)elementCount * structByteSize;
    m_elementByteSize                = structByteSize;
    m_elementCount                  = elementCount;
    m_resourceFormat                = resourceFormat;
    m_resourceName                  =  vaStringTools::SimpleWiden(name);

    D3D12_RESOURCE_DESC BufferDesc;
    BufferDesc.Dimension            = D3D12_RESOURCE_DIMENSION_BUFFER;
    BufferDesc.Alignment            = 0;
    BufferDesc.Width                = m_dataSize;
    BufferDesc.Height               = 1;
    BufferDesc.DepthOrArraySize     = 1;
    BufferDesc.MipLevels            = 1;
    BufferDesc.Format               = DXGI_FORMAT_UNKNOWN;
    BufferDesc.SampleDesc.Count     = 1;
    BufferDesc.SampleDesc.Quality   = 0;
    BufferDesc.Layout               = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    BufferDesc.Flags                = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if( IsReadback() || IsUpload() )
        BufferDesc.Flags            = D3D12_RESOURCE_FLAG_NONE;

    vaResourceAccessFlags resourceAccessFlags = vaResourceAccessFlags::Default;
    D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_COMMON;
    if( IsReadback( ) )
    {
        resourceAccessFlags = vaResourceAccessFlags::CPURead | vaResourceAccessFlags::CPUReadManuallySynced;
        initialResourceState = D3D12_RESOURCE_STATE_COPY_DEST;
    }
    if( IsUpload( ) )
    {
        resourceAccessFlags = vaResourceAccessFlags::CPUWrite;
        initialResourceState = D3D12_RESOURCE_STATE_GENERIC_READ;
    }
    if( ( flags & vaRenderBufferFlags::VertexIndexBuffer ) != 0 )
    {
        assert( !IsUpload() && !IsReadback() );
        initialResourceState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER;
    }

    D3D12_HEAP_TYPE heapType = HeapTypeDX12FromAccessFlags( resourceAccessFlags );
    D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;
    if( (flags & vaRenderBufferFlags::Shared) != 0 )
        heapFlags |= D3D12_HEAP_FLAG_SHARED;
    CD3DX12_HEAP_PROPERTIES heapProps(heapType);

    if( ( m_flags & vaRenderBufferFlags::RaytracingAccelerationStructure ) != 0 )
    {
        assert( !IsReadback() );
        initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
    }

    HRESULT hr;
    if( BufferDesc.Width > 0 )
    {
        V( AsDX12(m_renderDevice).GetPlatformDevice()->CreateCommittedResource( &heapProps, heapFlags, &BufferDesc, initialResourceState, nullptr, IID_PPV_ARGS(& m_resource ) ) );
        m_resource->SetName( m_resourceName.c_str() );

        m_desc = m_resource->GetDesc( );
        m_rsth.RSTHAttach( m_resource.Get(), initialResourceState );
    }
    else
        m_desc = BufferDesc;

    if( !IsReadback() && !IsUpload() )
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = { };

        uavDesc.ViewDimension                   = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement             = 0;

        if( m_resourceFormat == vaResourceFormat::Unknown || (flags & vaRenderBufferFlags::ForceByteAddressBufferViews) != 0 )
        {
            // This indicates ByteAddressBuffer
            if( structByteSize == 1 || (flags & vaRenderBufferFlags::ForceByteAddressBufferViews) != 0 )
            {
                uavDesc.Format                      = DXGI_FORMAT_R32_TYPELESS;
                uavDesc.Buffer.NumElements          = (UINT)m_dataSize / 4;
                uavDesc.Buffer.StructureByteStride  = 0;
                uavDesc.Buffer.Flags                = D3D12_BUFFER_UAV_FLAG_RAW;
            }
            else // StructuredBuffer 
            {
                uavDesc.Format                      = DXGI_FORMAT_UNKNOWN;
                uavDesc.Buffer.NumElements          = (UINT)m_elementCount;
                uavDesc.Buffer.StructureByteStride  = m_elementByteSize;
                uavDesc.Buffer.Flags                = D3D12_BUFFER_UAV_FLAG_NONE;
            }
        }
        else
        {
            uavDesc.Format                      = DXGIFormatFromVA( m_resourceFormat );
            uavDesc.Buffer.NumElements          = (UINT)m_elementCount;
            uavDesc.Buffer.StructureByteStride  = 0;
            uavDesc.Buffer.Flags                = D3D12_BUFFER_UAV_FLAG_NONE;
        }
        m_uav.Create( m_resource.Get( ), nullptr, uavDesc );

        // used for clears and etc.
        uavDesc.Format                      = DXGI_FORMAT_R32_UINT;
        uavDesc.Buffer.NumElements          = (UINT)m_dataSize / 4;
        uavDesc.Buffer.StructureByteStride  = 0;
        uavDesc.Buffer.Flags                = D3D12_BUFFER_UAV_FLAG_NONE;
        m_uavSimple.Create( m_resource.Get( ), nullptr, uavDesc );

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension               = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping     = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.FirstElement         = 0;

        if( m_resourceFormat == vaResourceFormat::Unknown || (flags & vaRenderBufferFlags::ForceByteAddressBufferViews) != 0 )
        {
            // This indicates ByteAddressBuffer
            if( structByteSize == 1 || (flags & vaRenderBufferFlags::ForceByteAddressBufferViews) != 0 )
            {
                srvDesc.Format                      = DXGI_FORMAT_R32_TYPELESS;
                srvDesc.Buffer.NumElements          = (UINT)m_dataSize / 4;
                srvDesc.Buffer.StructureByteStride  = 0;
                srvDesc.Buffer.Flags                = D3D12_BUFFER_SRV_FLAG_RAW;
            }
            else // StructuredBuffer 
            {
                srvDesc.Format                      = DXGI_FORMAT_UNKNOWN;
                srvDesc.Buffer.NumElements          = (UINT)m_elementCount;
                srvDesc.Buffer.StructureByteStride  = m_elementByteSize;
                srvDesc.Buffer.Flags                = D3D12_BUFFER_SRV_FLAG_NONE;
            }
        }
        else
        {
            srvDesc.Format                      = DXGIFormatFromVA( m_resourceFormat );
            srvDesc.Buffer.NumElements          = (UINT)m_elementCount;
            srvDesc.Buffer.StructureByteStride  = 0;
            srvDesc.Buffer.Flags                = D3D12_BUFFER_SRV_FLAG_NONE;
        }

        m_srv.Create( m_resource.Get( ), srvDesc );
    }
    else
    {
        //assert( m_resourceFormat == vaResourceFormat::Unknown );
    }

    if( m_resource != nullptr )
    {
        // map by default for readback/upload
        if( IsReadback() )
        {
            CD3DX12_RANGE readRange( 0, m_dataSize );   // we intend to read it all
            if( SUCCEEDED( hr = m_resource->Map( 0, &readRange, reinterpret_cast<void**>( &m_mappedData ) ) ) )
                return true;
            else
                { assert( false ); m_mappedData = nullptr; return false; }
        }
        else if( IsUpload() )
        {
            assert( IsUpload( ) ); // only read supported
            CD3DX12_RANGE readRange( 0, 0 );            // we do not intend to read
            if( SUCCEEDED( hr = m_resource->Map( 0, &readRange, reinterpret_cast<void**>( &m_mappedData ) ) ) )
                return true;
            else
                { assert( false ); m_mappedData = nullptr; return false; }
        }
    }

    return true;
}

void vaRenderBufferDX12::Destroy( )
{
    if( m_resource != nullptr )
    {
        if( IsMapped( ) )
        {
            m_mappedData = nullptr;
            m_resource->Unmap( 0, nullptr );
        }

        m_rsth.RSTHDetach( m_resource.Get() );
        assert( !IsMapped( ) );
        AsDX12(m_renderDevice).SafeReleaseAfterCurrentGPUFrameDone( m_resource, true );
        m_srv.SafeRelease( );
        m_uav.SafeRelease( );
        m_uavSimple.SafeRelease( );
    }
    m_desc              = {};
    m_mappedData        = nullptr;
    m_dataSize          = 0;
    m_elementByteSize    = 0;
    m_elementCount      = 0;
    m_flags             = vaRenderBufferFlags::None;
    m_resourceFormat    = vaResourceFormat::Unknown;
}

void vaRenderBufferDX12::Upload( vaRenderDeviceContext & renderContext, const void * data, uint64 dstByteOffset, uint64 dataSize )
{
    assert( !IsReadback() );
    assert( !IsUpload() );
    assert( dataSize <= (m_dataSize-dstByteOffset) );
    assert( dataSize > 0 );

    vaUploadBufferDX12 uploadBuffer( AsDX12(GetRenderDevice()), data, dataSize, m_resourceName );

    TransitionResource( AsDX12( renderContext ), D3D12_RESOURCE_STATE_COPY_DEST );
    AsDX12( renderContext ).GetCommandList( ).Get( )->CopyBufferRegion( m_resource.Get( ), dstByteOffset, uploadBuffer.GetResource(), 0, dataSize );

    // Special case for vertex/index buffers: keep them in vertex/index/SRV readable states to avoid any need for subsequent transitions
    if( ( m_flags & vaRenderBufferFlags::VertexIndexBuffer ) != 0 )
        TransitionResource( AsDX12( renderContext ), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER );
}

void vaRenderBufferDX12::DeferredUpload( const void * data, uint64 dstByteOffset, uint64 dataSize )
{
    assert( !IsReadback( ) );    // can't upload to readback buffer
    assert( !IsUpload( ) );      // there is no need for deferred upload to the upload buffer - it's mapped and can just be written to in-place
    if( dataSize == 0 )
        return;

    byte * copyBuffer = new byte[dataSize];
    memcpy( copyBuffer, data, dataSize );
    AsDX12(GetRenderDevice( )).ExecuteAtBeginFrame(
        [ this, copyBuffer, aliveToken{ std::weak_ptr<void>(m_aliveToken) }, dstByteOffset, dataSize ]( vaRenderDeviceDX12 & device )
    {
        auto alive = aliveToken.lock();
        if( alive != nullptr )
            this->Upload( *device.GetMainContext( ), copyBuffer, dstByteOffset, dataSize );
        delete[] copyBuffer;
    } );
}

void vaRenderBufferDX12::TransitionResource( vaRenderDeviceContextBaseDX12& context, D3D12_RESOURCE_STATES target )
{
    assert( !IsReadback() && !IsUpload() );
    if( ( m_flags & vaRenderBufferFlags::RaytracingAccelerationStructure ) != 0 && target != D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE )
    {
        // there should be no resource changes for raytracing acceleration structure - it's always D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE
        // is this intentional? if so, feel free to remove the assert
        // assert( false );
        return; 
    }
    if( ( m_flags & vaRenderBufferFlags::VertexIndexBuffer ) != 0 )
    {
        // always keep vert/ind buffer states
        if( ( target & ( D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER ) ) != 0 )
            target |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER;
    }

    if( m_rsth.IsRSTHTransitionRequired( context, target ) )
    {
        if( !context.IsWorker( ) )
            m_rsth.RSTHTransition( context, target );
        else
            context.GetMasterDX12( )->QueueResourceStateTransition( vaFramePtr<vaShaderResourceDX12>( this ), context.GetInstanceIndex( ), target );
    }
}

void vaRenderBufferDX12::AdoptResourceState( vaRenderDeviceContextBaseDX12 & context, D3D12_RESOURCE_STATES target )
{
    m_rsth.RSTHAdoptResourceState( context, target ); 
}

void vaRenderBufferDX12::ClearUAV( vaRenderDeviceContext & renderContext, const vaVector4ui & clearValue )
{
    assert( !IsReadback() );
    assert( !IsUpload() );

    assert( GetRenderDevice( ).IsFrameStarted( ) );
    // see https://www.gamedev.net/forums/topic/672063-d3d12-clearunorderedaccessviewfloat-fails/ for the reason behind the mess below
    assert( m_uavSimple.IsCreated( ) ); if( !m_uavSimple.IsCreated( ) ) return;
    TransitionResource( AsDX12( renderContext ), D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
    AsDX12( renderContext ).GetCommandList( )->ClearUnorderedAccessViewUint( m_uavSimple.GetCPUReadableGPUHandle( ), m_uavSimple.GetCPUReadableCPUHandle( ), m_resource.Get( ), &clearValue.x, 0, nullptr );
    // manually transitioning states below means we might mess up the render target states cache
    AsDX12( renderContext ).ResetCachedOutputs( );
}

void vaRenderBufferDX12::CopyFrom( vaRenderDeviceContext & renderContext, vaRenderBuffer & source, uint64 dataSizeInBytes )
{
    //VA_TRACE_CPUGPU_SCOPE( RenderBufferCopyFrom, renderContext );

    if( dataSizeInBytes == -1 )
    {
        assert( GetDataSize() == source.GetDataSize() );
        dataSizeInBytes = GetDataSize();
    }

    vaRenderBufferDX12 * srcDX12 = source.SafeCast<vaRenderBufferDX12*>();
    
    // src can't be readback! (doesn't make any sense)
    assert( !srcDX12->IsReadback() );
    assert( !IsUpload( ) );

    if( !IsReadback() )
        m_rsth.RSTHTransition( AsDX12( renderContext ), D3D12_RESOURCE_STATE_COPY_DEST );
    assert( m_dataSize >= source.GetDataSize() );

    if( !srcDX12->IsUpload() )
        srcDX12->TransitionResource( AsDX12( renderContext ), D3D12_RESOURCE_STATE_COPY_SOURCE );

    assert( dataSizeInBytes != 0 );
    assert( dataSizeInBytes <= GetDataSize() && dataSizeInBytes <= srcDX12->GetDataSize() );

    AsDX12( renderContext ).GetCommandList( ).Get( )->CopyBufferRegion( m_resource.Get( ), 0, srcDX12->m_resource.Get( ), 0, dataSizeInBytes );

    //AsDX12( renderContext ).GetCommandList( ).Get( )->CopyResource( m_resource.Get( ), srcDX12->m_resource.Get( ) ); dataSizeInBytes;
}

// since this is the only CUDA user so far, do it like this
#ifndef VA_OPTIX_DENOISER_ENABLED
bool vaRenderBufferDX12::GetCUDAShared( void * & outPointer, size_t & outSize )
{
    assert( false );
    outPointer = nullptr;
    outSize = 0;
    return false;
}
#endif

void RegisterBuffersDX12( )
{
    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX12, vaConstantBuffer, vaConstantBufferDX12 );
    //VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX12, vaIndexBuffer,    vaIndexBufferDX12 );
    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX12, vaDynamicVertexBuffer,   vaDynamicVertexBufferDX12 );
    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX12, vaRenderBuffer,   vaRenderBufferDX12 );
}