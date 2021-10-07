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

#include "Rendering/DirectX/vaDirectXIncludes.h"
#include "Rendering/DirectX/vaDirectXTools.h"

#include "Rendering/vaRenderingIncludes.h"


namespace Vanilla
{

    // Helper used mostly for one-off uploading of GPU data.
    // Once destroyed, the resource >will< be kept alive at least until GPU finishes the frame it's currently processing.
    // The contents might get reused after, and the actual resource size might be bigger than sizeInBytes.
    class vaUploadBufferDX12
    {
    private:
        vaRenderDeviceDX12 &                m_device;
        ComPtr<ID3D12Resource>              m_resource;
        //D3D12_RESOURCE_STATES               m_resourceState;
        //vaConstantBufferViewDX12            CBV;
        D3D12_CONSTANT_BUFFER_VIEW_DESC     m_CBV                   = {0, 0};
        D3D12_RESOURCE_DESC                 m_desc;
        byte *                              m_mappedData            = nullptr;
        
        // this is the 'sizeInBytes' as constructed
        uint64                              m_size                  = 0;

    private:
        void Construct( uint64 sizeInBytes, const wstring & resourceName );

    public:
        vaUploadBufferDX12( vaRenderDeviceDX12 & device, uint64 sizeInBytes, const wstring & resourceName );
        vaUploadBufferDX12( vaRenderDeviceDX12 & device, const void * initialContents, uint64 sizeInBytes, const wstring & resourceName );
        ~vaUploadBufferDX12( );

        ID3D12Resource *                    GetResource( ) const    { return m_resource.Get(); }
        D3D12_GPU_VIRTUAL_ADDRESS           GetGPUVirtualAddress( ) const { return m_resource->GetGPUVirtualAddress(); }
        const D3D12_RESOURCE_DESC &         GetDesc( ) const        { return m_desc; }

        byte *                              MappedData( ) const     { return m_mappedData; }
        uint64                              Size( )                 { return m_size; } 

    };

    // Constant buffers placed on upload heap - could be upgraded to allow for default heap but no pressing need atm
    class vaConstantBufferDX12 : public vaConstantBuffer//, public vaShaderResourceDX12//, public vaResourceStateTransitionHelperDX12
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );

        // this is for discarded buffers
        struct DetachableUploadBuffer
        {
            vaRenderDeviceDX12 &                Device;
            ComPtr<ID3D12Resource>              Resource;
            D3D12_RESOURCE_STATES               ResourceState;
            //vaConstantBufferViewDX12            CBV;
            D3D12_CONSTANT_BUFFER_VIEW_DESC     CBV                 = {0, 0};
            byte *                              MappedData;
            //bool                                DestroyImmediate = false;

            DetachableUploadBuffer( vaRenderDeviceDX12 & device, ComPtr<ID3D12Resource> resource, uint64 totalSizeInBytes );
            ~DetachableUploadBuffer( );
        };

    private:
        vaRenderDeviceDX12 &                m_deviceDX12;

        DetachableUploadBuffer *            m_uploadConstantBuffer = nullptr;

        std::vector< DetachableUploadBuffer * >  m_unusedBuffersPools[vaRenderDevice::c_BackbufferCount+1];

        // // so we can have weak_ptr-s for tracking the lifetime of this object - useful for lambdas and stuff
        // shared_ptr<vaConstantBufferDX12*>   const m_smartThis                       = std::make_shared<vaConstantBufferDX12*>(this);

        // this gets set and re-set on every Create - used to track deferred initial Update fired from a Create; if there's another Create call before its Update gets called, the Update will get orphaned by looking at m_createdThis
        shared_ptr<vaConstantBufferDX12*>   m_createdThis                           = nullptr;

        wstring                             m_resourceName;

        // mutex                               m_mutex;        // fairly global mutex - proper multithreading not yet supported though

        uint64                              m_actualSizeInBytes     = 0;            // when aligned
        uint64                              m_actualTotalSizeInBytes= 0;            // same as m_actualSizeInBytes with !m_dynamic, otherwise m_actualSizeInBytes * c_dynamicChunkCount

        // When not dynamic, each update creates a new D3D12 resource and maps & writes to it, safely disposing with the old one;
        // when dynamic, a bigger buffer (c_dynamicChunkCount times the data size) will be created and each Update will just write
        // to it and increment m_currentChunk until exhausted.
        // Non-dynamic is actually fine for 1-10 dynamic updates per frame and uses less memory.
        bool                                m_dynamic               = false;
        int                                 m_currentChunk          = 0;

    protected:
        friend class vaConstantBuffer;
        explicit                            vaConstantBufferDX12( const vaRenderingModuleParams & params );
        virtual                             ~vaConstantBufferDX12( );

        virtual void                        Upload( vaRenderDeviceContext & renderContext, const void * data, uint32 dataSize ) override;
        virtual bool                        Create( int bufferSize, const string & name, const void * initialData, bool dynamic, int deviceContextIndex ) override;
        virtual void                        Destroy( ) override;

    public:

        virtual vaResourceBindSupportFlags  GetBindSupportFlags( ) const override                           { return vaResourceBindSupportFlags::ConstantBuffer; }
        virtual uint32                      GetSRVBindlessIndex( vaRenderDeviceContext * renderContextPtr ) override { assert( renderContextPtr == nullptr ); renderContextPtr; assert( false ); return 0; }    // no bindless for constant buffers

        D3D12_GPU_VIRTUAL_ADDRESS           GetGPUBufferLocation( ) const                                   { return (m_uploadConstantBuffer != nullptr)?(m_uploadConstantBuffer->CBV.BufferLocation + ComputeDynamicOffset()):(0); }

    private:            
        uint64                              ComputeDynamicOffset( ) const                                   { assert( m_actualSizeInBytes * m_currentChunk < m_actualTotalSizeInBytes);  return m_actualSizeInBytes * m_currentChunk; }

        void                                DestroyInternal( /*bool lockMutex*/ );
        void                                AllocateNextUploadBuffer( );
        void                                SafeReleaseUploadBuffer( DetachableUploadBuffer * uploadBuffer, vaRenderDeviceContextBaseDX12 * renderContextPtr = nullptr );
    };

    // keeping this in for future use - it would be a good as a dynamic upload vertex shader resource
    class vaVertIndBufferDX12 // : protected vaResourceStateTransitionHelperDX12//, public vaShaderResourceDX12
    {
        // this is for discarded buffers - only applicable to dynamic upload buffers!
        struct DetachableBuffer
        {
            vaRenderDeviceDX12 &                Device;
            ComPtr<ID3D12Resource>              Resource;
            const uint32                        SizeInBytes;
            DetachableBuffer( vaRenderDeviceDX12 & device, ComPtr<ID3D12Resource> resource, uint32 sizeInBytes, uint32 actualSizeInBytes );
            ~DetachableBuffer( );
        };

    private:
        vaRenderDeviceDX12 &                m_device;
        DetachableBuffer *                  m_buffer = nullptr;
        std::vector< DetachableBuffer * >   m_unusedBuffersPool;

        void *                              m_mappedData    = nullptr;

        int                                 m_elementCount  = 0;
        int                                 m_elementSize   = 0;
        uint32                              m_dataSize      = 0;

        // so we can have weak_ptr-s for tracking the lifetime of this object - useful for lambdas and stuff
        shared_ptr<vaVertIndBufferDX12*>    const m_smartThis                       = std::make_shared<vaVertIndBufferDX12*>(this);
        
        // this gets set and re-set on every Create - used to track deferred initial Update fired from a Create; if there's another Create call before its Update gets called, the Update will get orphaned by looking at m_createdThis
        shared_ptr<vaVertIndBufferDX12*>    m_createdThis                           = nullptr;

        wstring                             m_resourceName;

        // mutable mutex                       m_mutex;        // fairly global mutex - proper multithreading not yet supported though

    public:
                                            vaVertIndBufferDX12( vaRenderDeviceDX12 & device );
        virtual                             ~vaVertIndBufferDX12( );

        void                                Create( int elementCount, int elementSize, const wstring & resourceName, const void * initialData );
        void                                Destroy( );
        bool                                IsCreated( ) const              { return m_buffer != nullptr; }

        // This is not an efficient way of updating the vertex buffer as it will create a disposable upload resource but it's fine to do at creation time, etc
        // (it could be optimized but why - dynamicUpload true/false should cover most/all cases?)
        void                                Upload( const void * data, uint32 dataSize );

        bool                                Map( vaResourceMapType mapType );
        void                                Unmap( );
        bool                                IsMapped( ) const               { return m_mappedData != nullptr; }
        void *                              GetMappedData( )                { assert(m_mappedData != nullptr); return m_mappedData; }

    public:
        ID3D12Resource *                    GetResource( ) const            { return (m_buffer != nullptr)?(m_buffer->Resource.Get()):(nullptr); }

    private:            
        void                                DestroyInternal( bool lockMutex );
    };

    class vaDynamicVertexBufferDX12 : public vaDynamicVertexBuffer, public vaShaderResourceDX12
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );

    private:
        vaVertIndBufferDX12                 m_buffer;

    protected:
        friend class vaConstantBuffer;
        explicit                            vaDynamicVertexBufferDX12( const vaRenderingModuleParams & params );
        virtual                             ~vaDynamicVertexBufferDX12( )  { }

        virtual bool                        Create( int vertexCount, int vertexSize, const string & name, const void * initialData ) override   { m_buffer.Create( vertexCount, vertexSize, vaStringTools::SimpleWiden(name), initialData ); if( IsCreated() ) { m_vertexSize = vertexSize; m_vertexCount = vertexCount; m_dataSize = vertexSize*vertexCount; }; return IsCreated(); }
        virtual void                        Destroy( ) override                                                                                 { m_vertexSize = 0; m_vertexCount = 0; m_dataSize = 0; return m_buffer.Destroy(); }
        virtual bool                        IsCreated( ) const override                                                                         { return m_buffer.IsCreated(); }

        virtual void                        Upload( const void * data, uint32 dataSize ) override                                               { return m_buffer.Upload( data, dataSize ); }

        virtual bool                        Map( vaResourceMapType mapType )                                                                    { if( m_buffer.Map( mapType ) ) { m_mappedData = m_buffer.GetMappedData(); return true; } else return false; }
        virtual void                        Unmap( )                                                                                            { m_mappedData = nullptr; m_buffer.Unmap( ); }

    public:
        virtual vaResourceBindSupportFlags  GetBindSupportFlags( ) const override                           { return vaResourceBindSupportFlags::VertexBuffer; }
        //ID3D12Resource *                    GetResource( ) const                                            { return m_buffer.GetResource(); }
        //D3D12_VERTEX_BUFFER_VIEW            GetResourceView( ) const                                        { return { (GetResource() != nullptr)?(GetResource()->GetGPUVirtualAddress()):(0), m_dataSize, m_vertexSize }; }
        virtual uint32                      GetSRVBindlessIndex( vaRenderDeviceContext * renderContextPtr ) override { assert( renderContextPtr == nullptr ); renderContextPtr; assert( false ); return 0; }    // no bindless for dynamic buffers

        virtual void                        TransitionResource( vaRenderDeviceContextBaseDX12 & context, D3D12_RESOURCE_STATES target )         { context; target; }
        virtual D3D12_GPU_VIRTUAL_ADDRESS   GetGPUVirtualAddress( ) const override                          { return (m_buffer.GetResource() != nullptr)?(m_buffer.GetResource()->GetGPUVirtualAddress()):(0); }
        virtual UINT64                      GetSizeInBytes( ) const       override                          { return m_dataSize; }
        virtual UINT                        GetStrideInBytes( ) const     override                          { return m_vertexSize; }
    };

    class vaRenderBufferDX12 : public vaRenderBuffer, public virtual vaShaderResourceDX12
    {
        ComPtr<ID3D12Resource>              m_resource;
        D3D12_RESOURCE_DESC                 m_desc          = {};

        vaResourceStateTransitionHelperDX12 m_rsth;

        vaShaderResourceViewDX12            m_srv;
        vaUnorderedAccessViewDX12           m_uav;
        vaUnorderedAccessViewDX12           m_uavSimple;       // always hold a simple non-raw non-structured buffer for clears

        wstring                             m_resourceName;

    public:
        vaRenderBufferDX12( const vaRenderingModuleParams & params );
        ~vaRenderBufferDX12( );

        virtual bool                        Create( uint64 elementCount, uint32 structByteSize, vaRenderBufferFlags flags, const string & name ) override;
        virtual bool                        Create( uint64 elementCount, vaResourceFormat resourceFormat, vaRenderBufferFlags flags, const string & name ) override;
        virtual void                        Destroy( ) override;
        virtual void                        Upload( vaRenderDeviceContext& renderContext, const void * data, uint64 dstByteOffset, uint64 dataSize ) override;
        virtual void                        DeferredUpload( const void * data, uint64 dstByteOffset, uint64 dataSize ) override;

        //bool                                Map( vaResourceMapType mapType );
        //void                                Unmap( );

        ID3D12Resource *                    GetResource( ) const                                                                            { return m_resource.Get(); }
        const D3D12_RESOURCE_DESC &         GetDesc( ) const                                                                                { return m_desc; }

        virtual const vaConstantBufferViewDX12 *    GetCBV( ) const override                                                                { return nullptr; };
        virtual const vaUnorderedAccessViewDX12 *   GetUAV( ) const override                                                                { return (!IsReadback())?(&m_uav):(nullptr); };
        virtual const vaShaderResourceViewDX12 *    GetSRV( ) const override                                                                { return (!IsReadback())?(&m_srv):(nullptr); };

        virtual void                        TransitionResource( vaRenderDeviceContextBaseDX12 & context, D3D12_RESOURCE_STATES target ) override;
        virtual void                        AdoptResourceState( vaRenderDeviceContextBaseDX12 & context, D3D12_RESOURCE_STATES target ) override;

        virtual void                        ClearUAV( vaRenderDeviceContext & renderContext, const vaVector4ui & clearValue )   override;

        virtual void                        CopyFrom( vaRenderDeviceContext & renderContext, vaRenderBuffer & source, uint64 dataSizeInBytes = -1 ) override;

        virtual vaResourceBindSupportFlags  GetBindSupportFlags( ) const override                                                           { return (!IsReadback())?(vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess):(vaResourceBindSupportFlags::None); };
        virtual uint32                      GetSRVBindlessIndex( vaRenderDeviceContext * renderContextPtr ) override                        { assert( renderContextPtr == nullptr ); renderContextPtr; return m_srv.GetBindlessIndex(); }

        virtual D3D12_GPU_VIRTUAL_ADDRESS   GetGPUVirtualAddress( ) const override                          { return (m_resource != nullptr)?(m_resource->GetGPUVirtualAddress()):(0); }
        virtual UINT64                      GetSizeInBytes( ) const       override                          { return m_dataSize; }
        virtual DXGI_FORMAT                 GetFormat( ) const            override                          { return DXGIFormatFromVA( m_resourceFormat ); }
        virtual UINT                        GetStrideInBytes( ) const     override                          { return m_elementByteSize; }


    protected:
        bool                                CreateInternal( uint64 elementCount, uint32 structByteSize, vaResourceFormat resourceFormat, vaRenderBufferFlags flags, const string & name );
    };

    inline vaConstantBufferDX12 &   AsDX12( vaConstantBuffer & buffer )   { return *buffer.SafeCast<vaConstantBufferDX12*>(); }
    inline vaConstantBufferDX12 *   AsDX12( vaConstantBuffer * buffer )   { return buffer->SafeCast<vaConstantBufferDX12*>(); }

    //inline vaIndexBufferDX12 &      AsDX12( vaIndexBuffer & buffer )   { return *buffer.SafeCast<vaIndexBufferDX12*>(); }
    //inline vaIndexBufferDX12 *      AsDX12( vaIndexBuffer * buffer )   { return buffer->SafeCast<vaIndexBufferDX12*>(); }

    inline vaDynamicVertexBufferDX12& AsDX12( vaDynamicVertexBuffer& buffer ) { return *buffer.SafeCast<vaDynamicVertexBufferDX12*>( ); }
    inline vaDynamicVertexBufferDX12* AsDX12( vaDynamicVertexBuffer* buffer ) { return buffer->SafeCast<vaDynamicVertexBufferDX12*>( ); }

    inline vaRenderBufferDX12 &        AsDX12( vaRenderBuffer& buffer ) { return *buffer.SafeCast<vaRenderBufferDX12*>( ); }
    inline vaRenderBufferDX12 *        AsDX12( vaRenderBuffer* buffer ) { return buffer->SafeCast<vaRenderBufferDX12*>( ); }
}