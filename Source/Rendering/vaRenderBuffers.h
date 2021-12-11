///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// These classes are a platform-independent CPU/GPU constant/vertex/index buffer layer. They are, however, only
// first iteration and interface is clunky and incomplete - expect major changes in the future.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Core/vaCoreIncludes.h"

#include "vaRendering.h"
#include "vaRenderDevice.h"
#include "Rendering/vaTexture.h"

namespace Vanilla
{
    class vaRenderDeviceContext;

    // These are all buffers for CPU<->GPU interop. 
    // Due to different requirements there are 3 different kinds that came out of one initial single version due to
    // conflicting optimizations:
    //
    //  * vaConstantBuffer:         Used for constants buffers only (and only supports direct CBVs, in DX12 terms!)
    //     - Fixed size
    //     - Use Upload to transfer new contents; there's no support for partial updates, it's all or nothing.
    //     - When 'dynamicUpload' creation option is used, the backing buffer will be c_dynamicChunkCount times
    //       larger than the dataSize which takes more space but allows very efficient updates - for ex., this
    //       is used for per-draw instance constant buffers and can be done 1,000,000 times per frame.
    //     - Use 'dynamicUpload == false' if the constant buffer is updated 1-10 times per frame.
    //     - There is no support for readback
    //
    //  * vaDynamicVertexBuffer:    Used for dynamic vertex (and could be index and maybe constant) buffers only
    //      - Specialized for dynamic upload; can't be used as a shader resource, intended use case is the 
    //      'write-no-overwrite'+"write-discard' approach (very similar to as detailed in 
    //      https://docs.microsoft.com/en-us/windows/win32/direct3d11/how-to--use-dynamic-resources)
    //      - NOTE: this could be used as an Index buffer with minor changes on implementation side.
    //
    //  * vaRenderBuffer:           A CPU<->GPU buffer, used for StructuredBuffer UAVs or SRVs, upload and readback, etc.
    //     - Closer to textures, without all the overhead but also not limited by texture dimension size limits!
    //     - Supports 'upload' flag which can be written into efficiently from the CPU (but no UAV/SRV support)
    //     - Supports 'readback' flag which can be read from the CPU (but no UAV/SRV support)
    //     - Supports updates to non-upload and non-readback but not efficient for more than 1-10 per frame in general
    //     - Supports StructuredBuffer and RawAddressBuffer and typed UAV/SRVs
    //     - Supports shader viewing through UAVs and SRVs (when !readback and !upload)
    //     - Supports copy from !readback to readback and copy from upload to !upload
    //

    class vaConstantBuffer : public vaRenderingModule, public virtual vaShaderResource
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );

    protected:
        uint32                              m_dataSize      = 0;

        // this basically means it can only be updated or used from the specific device context - this is how we handle multithreading for constant buffers
        int                                 m_deviceContextIndex = 0;

        static constexpr int                c_dynamicChunkCount = 512;          // 

    
    protected:
        vaConstantBuffer( const vaRenderingModuleParams & params ) : vaRenderingModule( params ) { }
        vaConstantBuffer( vaRenderDevice & device );
    public:
        virtual ~vaConstantBuffer( )        { }

        uint32                              GetDataSize( ) const                                                    { return m_dataSize; }

    public:
        virtual void                        Upload( vaRenderDeviceContext & renderContext, const void * data, uint32 dataSize )                             = 0;
        template<typename StructType>
        void                                Upload( vaRenderDeviceContext & renderContext, const StructType & data ){ assert( sizeof(StructType) <= m_dataSize ); Upload( renderContext, &data, sizeof(StructType) ); }

        virtual bool                        Create( int bufferSize, const string & name, const void * initialData, bool dynamicUpload, int deviceContextIndex ) = 0;
        virtual void                        Destroy( )                                                                                                      = 0;

        virtual vaResourceBindSupportFlags  GetBindSupportFlags( ) const override                                   { return vaResourceBindSupportFlags::ConstantBuffer; }

    public:

        // deviceContextIndex -1 means "device.GetMainContext().GetInstanceIndex()"
        static shared_ptr<vaConstantBuffer> Create( vaRenderDevice & device, int bufferSize, const string & name, const void * initialData = nullptr, bool dynamicUpload = true, int deviceContextIndex = - 1 );
        
        // deviceContextIndex -1 means "device.GetMainContext().GetInstanceIndex()"
        template<typename StructType>
        static shared_ptr<vaConstantBuffer> Create( vaRenderDevice & device, const string & name, StructType * initialData = nullptr, bool dynamicUpload = true, int deviceContextIndex = -1 )
                                                { return Create( device, sizeof( StructType ), name, initialData, dynamicUpload, deviceContextIndex ); }
    };
    // used to have "template< typename StructType, bool dynamicUploadT = false > class vaTypedConstantBufferWrapper" but that's gone to speed up compile times (since it required including shader<->cpp shared types in headers)

    /*
    class vaIndexBuffer : public vaRenderingModule, public virtual vaShaderResource
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );

    protected:
        uint32                              m_dataSize      = 0;
        uint32                              m_indexCount    = 0;
    
    protected:
        vaIndexBuffer( const vaRenderingModuleParams & params ) : vaRenderingModule( params ) { }
        vaIndexBuffer( vaRenderDevice & device ) : vaRenderingModule( vaRenderingModuleParams(device) )    { }
    public:
        virtual ~vaIndexBuffer( )           { }


    public:
        virtual void                        Upload( vaRenderDeviceContext & renderContext, const void * data, uint32 dataSize )                             = 0;

        uint32                              GetDataSize( ) const                                                    { return m_dataSize; }
        uint32                              GetIndexCount( ) const                                                  { return m_indexCount; }
    
        virtual void                        Create( int indexCount, const void * initialData = nullptr )                                                    = 0;
        virtual void                        Destroy( )                                                                                                      = 0;
        virtual bool                        IsCreated( ) const                                                                                              = 0;

        virtual vaResourceBindSupportFlags  GetBindSupportFlags( ) const override                                   { return vaResourceBindSupportFlags::IndexBuffer; }
    };*/

    // Specialized for dynamic upload; can't be used as a shader resource
    // NOTE: this could be used as an Index buffer with minor changes on implementation side.
    class vaDynamicVertexBuffer : public vaRenderingModule, public virtual vaShaderResource
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );

    protected:
        void *                              m_mappedData    = nullptr;

        uint32                              m_dataSize      = 0;        // total buffer size in bytes - 4gb is enough for a vertex buffer, right, RIGHT?
        uint32                              m_vertexSize    = 0;        // single element size a.k.a. 'stride in bytes'
        uint32                              m_vertexCount   = 0;        // m_dataSize / m_vertexSize
    
    protected:
        vaDynamicVertexBuffer( const vaRenderingModuleParams & params ) : vaRenderingModule( params ) { }
        vaDynamicVertexBuffer( vaRenderDevice & device ) : vaRenderingModule( vaRenderingModuleParams(device) )    { }
    
    public:
        virtual ~vaDynamicVertexBuffer( )          { }


    public:
        virtual void                        Upload( const void * data, uint32 dataSize )                            = 0;

        bool                                IsMapped( ) const                                                       { return m_mappedData != nullptr; }
        void *                              GetMappedData( )                                                        { assert(IsMapped()); return m_mappedData; }
        virtual bool                        Map( vaResourceMapType mapType)                                         = 0;
        virtual void                        Unmap( )                                                                = 0;


        uint32                              GetVertexCount( ) const                                                 { return m_vertexCount; }
        uint                                GetByteStride( ) const                                                  { return m_vertexSize; }

        virtual bool                        Create( int vertexCount, int vertexSize, const string & name, const void * initialData ) = 0;
        virtual void                        Destroy( )                                                                                                      = 0;
        virtual bool                        IsCreated( ) const                                                                                              = 0;

        virtual vaResourceBindSupportFlags  GetBindSupportFlags( ) const override                                   { return vaResourceBindSupportFlags::VertexBuffer; }

        // Typed helpers
        template<typename VertexType>
        bool                                Create( int vertexCount, const string & name, const void * initialData = nullptr ) { return Create( vertexCount, sizeof( StructType ), name, initialData ); }
        template<typename VertexType>
        VertexType *                        GetMappedData( )                                                        { assert(IsMapped()); assert( sizeof(VertexType) == m_vertexSize ); return static_cast<VertexType*>(m_mappedData); }
        template<typename VertexType>
        void                                Upload( const std::vector<VertexType> vertices )                        { assert( sizeof(VertexType) == m_vertexSize ); Upload( vertices.data(), vertices.size() * m_vertexSize ); }

        static shared_ptr<vaDynamicVertexBuffer> Create( vaRenderDevice & device, int vertexCount, int vertexSize, const string & name, const void * initialData = nullptr );
        template<typename VertexType>
        static shared_ptr<vaDynamicVertexBuffer> Create( vaRenderDevice & device, int vertexCount, const string & name, const void * initialData = nullptr ) { return Create( device, vertexCount, (int)sizeof( VertexType ), name, initialData ); }
    };

    enum class vaRenderBufferFlags : int32
    {
        None                            = 0,
        Readback                        = (1 << 0),
        Upload                          = (1 << 1),     // write-only, with limited SRV/UAV support
        RaytracingAccelerationStructure = (1 << 2),
        ConstantBuffer                  = (1 << 3),
        VertexIndexBuffer               = (1 << 4),
        ForceByteAddressBufferViews     = (1 << 5),
        Shared                          = (1 << 6),     // same as vaResourceBindSupportFlags::Shared
    };
    BITFLAG_ENUM_CLASS_HELPER( vaRenderBufferFlags );

    // Generic GPU buffer; can be structured or raw and can be used as an SRV or UAV.
    // This is work in progress.
    class vaRenderBuffer : public vaRenderingModule, public virtual vaShaderResource, virtual public vaFramePtrTag, public std::enable_shared_from_this<vaRenderBuffer>
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );

    protected:
        byte *                              m_mappedData                = nullptr;
        uint64                              m_dataSize                  = 0;
        uint32                              m_elementByteSize           = 0;
        uint64                              m_elementCount              = 0;
        vaRenderBufferFlags                 m_flags                     = vaRenderBufferFlags::None;
        vaResourceFormat                    m_resourceFormat            = vaResourceFormat::Unknown;

        shared_ptr<void> const              m_aliveToken                = std::make_shared<int>( 42 );    // this is only used to track object lifetime for callbacks and etc.
    
    protected:
        vaRenderBuffer( const vaRenderingModuleParams & params ) : vaRenderingModule( params ) { }
        vaRenderBuffer( vaRenderDevice & device ) : vaRenderingModule( vaRenderingModuleParams(device) )    { }
    public:
        virtual ~vaRenderBuffer( )      { }

    public:
        // if structByteSize == 1 then it's a ByteAddressBuffer
        virtual bool                        Create( uint64 elementCount, uint32 structByteSize, vaRenderBufferFlags flags, const string & name )             = 0;
        virtual bool                        Create( uint64 elementCount, vaResourceFormat format, vaRenderBufferFlags flags, const string & name )           = 0;
        template<typename StructType>
        bool                                Create( uint64 elementCount, vaRenderBufferFlags flags, const string & name )                            { return Create( elementCount, sizeof(StructType), flags, name ); }
        
        bool                                IsCreated( )                                                            { return m_dataSize > 0; };

        virtual void                        Destroy( )                                                                                                      = 0;

        uint64                              GetDataSize( ) const                                                    { return m_dataSize;            }
        uint32                              GetElementByteSize( ) const                                             { return m_elementByteSize;      }
        uint64                              GetElementCount( ) const                                                { return m_elementCount;        }
        vaResourceFormat                    GetResourceFormat( ) const                                              { return m_resourceFormat;      }    

        // Upload-s will create a new UPLOAD heap resource, copy data to it, schedule GPU copy from it and keep the temporary resource alive until GPU has finished the copy
        virtual void                        Upload( vaRenderDeviceContext & renderContext, const void * data, uint64 dstByteOffset, uint64 dataSize )   = 0;
        template< typename ElementType >
        void                                Upload( vaRenderDeviceContext & renderContext, const std::vector<ElementType> & srcVector );
        template< typename ElementType >
        void                                UploadSingle( vaRenderDeviceContext & renderContext, const ElementType & value, int index );


        // This will store the data and execute at the beginning of the frame; there's no ordering guarantees!
        virtual void                         DeferredUpload( const void * data, uint64 dstByteOffset, uint64 dataSize )                   = 0;

        // Mapping is allowed only for 'readback' type buffer, and is read-only. An easier way to just copy everything is to use 'Readback' function which internally uses Map/Unmap
        bool                                IsMapped( ) const                                                       { return m_mappedData != nullptr; }
        void *                              GetMappedData( )                                                        { assert(IsMapped()); return m_mappedData; }
        template< typename ElementType >
        ElementType *                       GetMappedData( )                                                        { assert(IsMapped()); assert( sizeof(ElementType) == m_elementByteSize ); return static_cast<ElementType*>((void*)m_mappedData); }
        //virtual bool                        Map( vaResourceMapType mapType )                                                                                = 0;
        //virtual void                        Unmap( )                                                                                                        = 0;

        bool                                IsReadback( ) const                                                     { return (m_flags & vaRenderBufferFlags::Readback) != 0; }
        bool                                IsUpload( ) const                                                       { return (m_flags & vaRenderBufferFlags::Upload) != 0; }
        
        // This does map/unmap internally
        void                                Readback( void * dstData, uint64 dstDataSizeInBytes );
        template< typename ElementType >
        void                                Readback( ElementType & dstData ) 
                                                                                                                    { Readback( (void*)&dstData, sizeof(ElementType) ); }
        template< typename ElementType >
        void                                Readback( ElementType * dstData, uint32 itemCount ) 
                                                                                                                    { Readback( (void*)dstData, itemCount * sizeof(ElementType) ); }

        virtual void                        CopyFrom( vaRenderDeviceContext & renderContext, vaRenderBuffer & source, uint64 dstOffsetInBytes, uint64 srcOffsetInBytes, uint64 dataSizeInBytes = -1 ) = 0;

        virtual vaResourceBindSupportFlags  GetBindSupportFlags( ) const override                                   { return vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess; }

        //vaDrawResultFlags                   ClearUAV( vaRenderDeviceContext & renderContext, const vaVector4ui & clearValue );
        //vaDrawResultFlags                   ClearUAV( vaRenderDeviceContext & renderContext, uint32 clearValue );

        virtual bool                        GetCUDAShared( void * & outPointer, size_t & outSize )                  { return false; outPointer; outSize; }


    public:
        // static helpers
        static shared_ptr<vaRenderBuffer>   Create( vaRenderDevice & device, uint64 elementCount, uint32 structByteSize, vaRenderBufferFlags flags, const string & name, void * initialData = nullptr );
        static shared_ptr<vaRenderBuffer>   Create( vaRenderDevice & device, uint64 elementCount, vaResourceFormat format, vaRenderBufferFlags flags, const string & name, void * initialData = nullptr );
        template<typename StructType>
        static shared_ptr<vaRenderBuffer>   Create( vaRenderDevice & device, uint64 elementCount, vaRenderBufferFlags flags, const string & name, StructType * initialData = nullptr )     { return Create( device, elementCount, sizeof( StructType ), flags, name, initialData ); }
    };

    template< typename ElementType >
    void vaRenderBuffer::Upload( vaRenderDeviceContext & renderContext, const std::vector<ElementType> & srcVector )
    {
        assert( sizeof(ElementType) == m_elementByteSize );
        Upload( renderContext, srcVector.data(), 0, m_elementByteSize * srcVector.size() );
    }

    template< typename ElementType >
    void vaRenderBuffer::UploadSingle( vaRenderDeviceContext & renderContext, const ElementType & value, int index )
    {
        assert( sizeof(ElementType) == m_elementByteSize );
        assert( index * m_elementByteSize < m_dataSize );
        Upload( renderContext, &value, index * m_elementByteSize, m_elementByteSize );
    }
}