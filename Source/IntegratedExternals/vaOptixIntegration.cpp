#pragma once

#ifdef VA_OPTIX_DENOISER_ENABLED

#include "vaOptixIntegration.h"

#include "optix/include/optix_function_table_definition.h"

#include "Rendering/DirectX/vaTextureDX12.h"
#include "Rendering/DirectX/vaRenderBuffersDX12.h"
#include "Rendering/DirectX/vaRenderDeviceDX12.h"

#if 0
// since this is the only CUDA user so far, do it like this
bool Vanilla::vaTextureDX12::GetCUDAShared( void * & outPointer, size_t & outSize )
{
    if( m_resource == nullptr )
    { assert( false ); outPointer = nullptr; outSize = 0; return false; }

    if( m_sharedApiHandle == 0 )
    {
        auto & d3d12Device = AsDX12( GetRenderDevice( ) ).GetPlatformDevice( );
        HRESULT hr = d3d12Device->CreateSharedHandle( m_resource.Get(), nullptr, GENERIC_ALL, m_wname.c_str(), &m_sharedApiHandle );
        if( FAILED(hr) )
        { assert( false ); outPointer = nullptr; outSize = 0; return false; }
    }

    outSize = GetWidth() * GetHeight() * vaResourceFormatHelpers::GetPixelSizeInBytes(m_resourceFormat); //GetSizeInBytes();

    // Create the descriptor of our shared memory buffer
    cudaExternalMemoryHandleDesc externalMemoryHandleDesc;
    memset(&externalMemoryHandleDesc, 0, sizeof(externalMemoryHandleDesc));
    externalMemoryHandleDesc.type = cudaExternalMemoryHandleTypeD3D12Resource;
    externalMemoryHandleDesc.handle.win32.handle = m_sharedApiHandle;
    externalMemoryHandleDesc.size = outSize;
    externalMemoryHandleDesc.flags = cudaExternalMemoryDedicated;

    // Get a handle to that memory
    cudaExternalMemory_t externalMemory;
    if( cudaImportExternalMemory(&externalMemory, &externalMemoryHandleDesc) != cudaError::cudaSuccess )
        { assert( false ); outPointer = nullptr; outSize = 0; return false; }

    // Create a descriptor for our shared buffer pointer
    cudaExternalMemoryBufferDesc bufDesc;
    memset(&bufDesc, 0, sizeof(bufDesc));
    bufDesc.size = outSize;

    // Actually map the buffer
    void* devPtr = nullptr;
    if( cudaExternalMemoryGetMappedBuffer(&devPtr, externalMemory, &bufDesc) != cudaError::cudaSuccess )
    { assert( false ); outPointer = nullptr; outSize = 0; return false; }
    
    outPointer = devPtr;
    return true;
}
#endif

// since this is the only CUDA user so far, do it like this
bool Vanilla::vaRenderBufferDX12::GetCUDAShared( void * & outPointer, size_t & outSize )
{
    if( m_resource == nullptr )
    { assert( false ); outPointer = nullptr; outSize = 0; return false; }

    if( m_sharedApiHandle == 0 )
    {
        auto & d3d12Device = AsDX12( GetRenderDevice( ) ).GetPlatformDevice( );
        wstring uniqueName = m_resourceName + L"_" + vaStringTools::SimpleWiden( vaGUID::Create( ).ToString( ) );
        HRESULT hr = d3d12Device->CreateSharedHandle( m_resource.Get(), nullptr, GENERIC_ALL, uniqueName.c_str(), &m_sharedApiHandle );
        if( FAILED(hr) )
        { assert( false ); outPointer = nullptr; outSize = 0; return false; }
    }

    outSize = GetSizeInBytes();
                                                                                                         // Create the descriptor of our shared memory buffer
    cudaExternalMemoryHandleDesc externalMemoryHandleDesc;
    memset(&externalMemoryHandleDesc, 0, sizeof(externalMemoryHandleDesc));
    externalMemoryHandleDesc.type = cudaExternalMemoryHandleTypeD3D12Resource;
    externalMemoryHandleDesc.handle.win32.handle = m_sharedApiHandle;
    externalMemoryHandleDesc.size = outSize;
    externalMemoryHandleDesc.flags = cudaExternalMemoryDedicated;

    // Get a handle to that memory
    cudaExternalMemory_t externalMemory;
    if( cudaImportExternalMemory(&externalMemory, &externalMemoryHandleDesc) != cudaError::cudaSuccess )
    { assert( false ); outPointer = nullptr; outSize = 0; return false; }

    // Create a descriptor for our shared buffer pointer
    cudaExternalMemoryBufferDesc bufDesc;
    memset(&bufDesc, 0, sizeof(bufDesc));
    bufDesc.size = outSize;

    // Actually map the buffer
    void* devPtr = nullptr;
    if( cudaExternalMemoryGetMappedBuffer(&devPtr, externalMemory, &bufDesc) != cudaError::cudaSuccess )
    { assert( false ); outPointer = nullptr; outSize = 0; return false; }

    outPointer = devPtr;
    return true;
}

#endif // VA_OIDN_INTEGRATION_ENABLED