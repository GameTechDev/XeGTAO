///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaRenderBuffers.h"
#include "vaRenderDeviceContext.h"

using namespace Vanilla;

vaConstantBuffer::vaConstantBuffer( vaRenderDevice & device ) : vaRenderingModule( vaRenderingModuleParams(device) )
{ 
}

// static helpers

shared_ptr<vaConstantBuffer> vaConstantBuffer::Create( vaRenderDevice & device, int bufferSize, const string & name, const void * initialData, bool dynamicUpload, int deviceContextIndex )
{
    if( deviceContextIndex == -1 )
        deviceContextIndex = device.GetMainContext()->GetInstanceIndex();
    shared_ptr<vaConstantBuffer> ret = device.CreateModule<vaConstantBuffer>( );
    bool ok = ret->Create( bufferSize, name, initialData, dynamicUpload, deviceContextIndex );
    assert( ok );
    if( !ok )
        ret = nullptr;
    return ret;
}

shared_ptr<vaDynamicVertexBuffer> vaDynamicVertexBuffer::Create( vaRenderDevice & device, int vertexCount, int vertexSize, const string & name, const void * initialData )
{
    shared_ptr<vaDynamicVertexBuffer>  ret = device.CreateModule<vaDynamicVertexBuffer>( );
    bool ok = ret->Create( vertexCount, vertexSize, name, initialData );
    assert( ok );
    if( !ok )
        ret = nullptr;
    return ret;
}

shared_ptr<vaRenderBuffer> vaRenderBuffer::Create( vaRenderDevice & device, uint64 elementCount, uint32 structByteSize, vaRenderBufferFlags flags, const string & name, void * initialData )
{
    shared_ptr<vaRenderBuffer>  ret = device.CreateModule<vaRenderBuffer>( );
    bool ok = ret->Create( elementCount, structByteSize, flags, name );
    assert( ok );
    if( !ok )
        return false;
    if( initialData != nullptr )
        ret->DeferredUpload( initialData, 0, ret->GetDataSize( ) );
    return ret;
}
shared_ptr<vaRenderBuffer> vaRenderBuffer::Create( vaRenderDevice & device, uint64 elementCount, vaResourceFormat format, vaRenderBufferFlags flags, const string & name, void * initialData )
{
    shared_ptr<vaRenderBuffer>  ret = device.CreateModule<vaRenderBuffer>( );
    bool ok = ret->Create( elementCount, format, flags, name );
    assert( ok );
    if( !ok )
        return false;
    if( initialData != nullptr )
        ret->DeferredUpload( initialData, 0, ret->GetDataSize() );
    return ret;
}

void vaRenderBuffer::Readback( void * dstData, uint64 dstDataSize )
{
    assert( IsReadback() );
    assert( dstDataSize >= m_dataSize );
    //assert( !IsMapped() );
    //bool mapped = Map( vaResourceMapType::Read );
    //assert( mapped ); 
    //if( !mapped ) 
    //    return;
    assert( IsMapped() );
    memcpy( dstData, m_mappedData, dstDataSize );
    //Unmap( );
}

