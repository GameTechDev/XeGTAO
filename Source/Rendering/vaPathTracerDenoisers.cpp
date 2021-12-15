///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaPathTracerDenoisers.h"

#ifdef VA_OIDN_INTEGRATION_ENABLED

#include "Rendering/vaRenderDeviceContext.h"
#include "Rendering/vaRenderDevice.h"

#include "Rendering/DirectX/vaRenderDeviceContextDX12.h"
#include "Rendering/DirectX/vaRenderDeviceDX12.h"

#include "Rendering/vaRenderGlobals.h"
#include "Rendering/vaShader.h"
#include "Rendering/vaRenderBuffers.h"
#include "Rendering/vaTexture.h"

#include "Rendering/Shaders/vaPathTracerShared.h"

using namespace Vanilla;


vaDenoiserOIDN::vaDenoiserOIDN( )
{
    Device = oidnNewDevice( OIDN_DEVICE_TYPE_DEFAULT );
    //oidnSetDevice1b( Device, "setAffinity", false );
    //oidnSetDevice1i( Device, "numThreads", vaThreading::GetCPULogicalCores() );
    oidnCommitDevice( Device );
    // Create a filter for denoising a beauty (color) image using optional auxiliary images too
    Filter = oidnNewFilter( Device, "RT" ); // generic ray tracing filter

}

vaDenoiserOIDN::~vaDenoiserOIDN( )
{
    if( Beauty      != nullptr ) oidnReleaseBuffer(Beauty);
    if( Output      != nullptr ) oidnReleaseBuffer(Output);
    if( AuxAlbedo   != nullptr ) oidnReleaseBuffer(AuxAlbedo);
    if( AuxNormals  != nullptr ) oidnReleaseBuffer(AuxNormals);
    oidnReleaseFilter(Filter);
    oidnReleaseDevice(Device);
}

void vaDenoiserOIDN::UpdateTextures( vaRenderDevice & device, int width, int height )
{
    if( BeautyGPU == nullptr || Width != width || Height != height )
    {
        Width  = width;
        Height = height;

        if( Beauty      != nullptr ) oidnReleaseBuffer(Beauty);
        if( Output      != nullptr ) oidnReleaseBuffer(Output);
        if( AuxAlbedo   != nullptr ) oidnReleaseBuffer(AuxAlbedo);
        if( AuxNormals  != nullptr ) oidnReleaseBuffer(AuxNormals);

        BeautyGPU = vaTexture::Create2D( device, vaResourceFormat::R32G32B32A32_FLOAT, width, height, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::UnorderedAccess );
        BeautyCPU = vaTexture::Create2D( device, vaResourceFormat::R32G32B32A32_FLOAT, width, height, 1, 1, 1, vaResourceBindSupportFlags::None, vaResourceAccessFlags::CPURead );

        DenoisedGPU = vaTexture::Create2D( device, vaResourceFormat::R32G32B32A32_FLOAT, width, height, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget | vaResourceBindSupportFlags::UnorderedAccess );
        DenoisedCPU = vaTexture::Create2D( device, vaResourceFormat::R32G32B32A32_FLOAT, width, height, 1, 1, 1, vaResourceBindSupportFlags::None, vaResourceAccessFlags::CPUWrite );

        AuxNormalsCPU = vaTexture::Create2D( device, vaResourceFormat::R32G32B32A32_FLOAT, width, height, 1, 1, 1, vaResourceBindSupportFlags::None, vaResourceAccessFlags::CPURead );
        AuxAlbedoCPU = vaTexture::Create2D( device, vaResourceFormat::R32G32B32A32_FLOAT, width, height, 1, 1, 1, vaResourceBindSupportFlags::None, vaResourceAccessFlags::CPURead );

        BytesPerPixel = 4*4;
        BufferSize    = Width * Height * BytesPerPixel;

        Beauty      = oidnNewBuffer( Device, BufferSize );
        Output      = oidnNewBuffer( Device, BufferSize );
        AuxAlbedo   = oidnNewBuffer( Device, BufferSize );
        AuxNormals  = oidnNewBuffer( Device, BufferSize );

        oidnSetFilterImage( Filter, "color",    Beauty, OIDN_FORMAT_FLOAT3, Width, Height, 0, BytesPerPixel, BytesPerPixel * Width );
        oidnSetFilterImage( Filter, "output",   Output, OIDN_FORMAT_FLOAT3, Width, Height, 0, BytesPerPixel, BytesPerPixel * Width );
        oidnSetFilterImage( Filter, "albedo",   AuxAlbedo, OIDN_FORMAT_FLOAT3, Width, Height, 0, BytesPerPixel, BytesPerPixel * Width );
        oidnSetFilterImage( Filter, "normal",   AuxNormals, OIDN_FORMAT_FLOAT3, Width, Height, 0, BytesPerPixel, BytesPerPixel * Width );
        oidnSetFilter1b( Filter, "hdr", true );         // beauty image is HDR
        oidnSetFilter1b( Filter, "cleanAux", true );    // auxiliary images are not noisy
        oidnCommitFilter( Filter );
    }
}

void vaDenoiserOIDN::CopyContents( vaRenderDeviceContext & renderContext, OIDNBuffer destination, const shared_ptr<vaTexture> & source, size_t bufferSize )
{
    // map CPU buffer Vanilla side
    if( !source->TryMap( renderContext, vaResourceMapType::Read ) )
    { assert( false ); return; }
    // map CPU buffer OIDN side
    void * dst = oidnMapBuffer( destination, OIDN_ACCESS_WRITE_DISCARD, 0, bufferSize );
    // copy CPU Vanilla -> CPU OIDN 
    memcpy( dst, source->GetMappedData()[0].Buffer, bufferSize );
    // unmap OIDN side
    oidnUnmapBuffer( destination, dst );
    // unmap Vanilla side
    source->Unmap( renderContext );
}

void vaDenoiserOIDN::CopyContents( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & destination, OIDNBuffer source, size_t bufferSize )
{
    // map Vanilla-side
    if( !destination->TryMap( renderContext, vaResourceMapType::Write ) )
    { assert( false ); return; }
    // map OIDN-side
    void * src = oidnMapBuffer( source, OIDN_ACCESS_READ, 0, bufferSize );
    // copy CPU OIDN -> CPU Vanilla
    memcpy( destination->GetMappedData()[0].Buffer, src, bufferSize );
    oidnUnmapBuffer( source, src );
    destination->Unmap( renderContext );
}

void vaDenoiserOIDN::VanillaToDenoiser( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & beautySrc, const shared_ptr<vaTexture> & auxAlbedoSrc, const shared_ptr<vaTexture> & auxNormalsSrc )
{
    VA_TRACE_CPU_SCOPE( VanillaToDenoiser );

    // copy GPU any-color-format -> GPU R32G32B32A32_FLOAT
    renderContext.CopySRVToRTV( BeautyGPU, beautySrc );
    // copy GPU -> CPU Vanilla side
    BeautyCPU->CopyFrom( renderContext, BeautyGPU );
    CopyContents( renderContext, Beauty, BeautyCPU, BufferSize );
    AuxAlbedoCPU->CopyFrom( renderContext, auxAlbedoSrc );
    CopyContents( renderContext, AuxAlbedo, AuxAlbedoCPU, BufferSize );
    AuxNormalsCPU->CopyFrom( renderContext, auxNormalsSrc );
    CopyContents( renderContext, AuxNormals, AuxNormalsCPU, BufferSize );
}

void vaDenoiserOIDN::Denoise( )
{
    VA_TRACE_CPU_SCOPE( Denoise );

    oidnExecuteFilter( Filter );

    // Check for errors
    const char* errorMessage;
    if( oidnGetDeviceError( Device, &errorMessage) != OIDN_ERROR_NONE )
        VA_WARN("Error: %s\n", errorMessage );
}

void vaDenoiserOIDN::DenoiserToVanilla( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & output )
{
    VA_TRACE_CPU_SCOPE( DenoiserToVanilla );

    CopyContents( renderContext, DenoisedCPU, Output, BufferSize );
    // copy CPU Vanilla -> GPU Vanilla
    DenoisedGPU->CopyFrom( renderContext, DenoisedCPU );
    // GPU R32G32B32A32_FLOAT - > copy GPU any-color-format
    renderContext.CopySRVToRTV( output, DenoisedGPU );
}

#endif // #ifdef VA_OIDN_INTEGRATION_ENABLED


#ifdef VA_OPTIX_DENOISER_ENABLED

void logOptixWarning(unsigned int level, const char* tag, const char* message, void*)
{
    VA_LOG_WARNING( "[OptiX][%2d][%12s]: %s", (int)level, tag, message );
}

vaDenoiserOptiX::vaDenoiserOptiX( vaRenderDevice & device )
{
    Initialized = false;

    int32 deviceCount;
    if( cudaGetDeviceCount(&deviceCount) != cudaSuccess )
    { assert( false ); return; }

    if( deviceCount <= 0 )
    { assert( false ); return; }

    if( optixInit() != OPTIX_SUCCESS )
    { assert( false ); return; }

    // Check if we have a valid OptiX function table.  If not, return now.
    if (!g_optixFunctionTable.optixDeviceContextCreate)
    { assert( false ); return; }
    
    for( int32 devID = 0; devID < deviceCount; devID++ )
    {
        cudaDeviceProp devProp;
        cudaGetDeviceProperties(&devProp, devID);

        uint32 luidLowPart; int32 luidHighPart;
        device.GetAdapterLUID( luidHighPart, luidLowPart );

        if(     (memcmp(&luidLowPart,   devProp.luid, sizeof(luidLowPart)) == 0) 
            &&  (memcmp(&luidHighPart,  devProp.luid + sizeof(luidLowPart), sizeof(luidHighPart)) == 0) ) 
        {
            if( cudaSetDevice(devID) != cudaSuccess )
                { assert( false ); return; }
            CUDADeviceID = devID;
            CUDADeviceNodeMask = devProp.luidDeviceNodeMask;
            if( cudaStreamCreate(&CUDAStream) != cudaSuccess )
                { assert( false ); return; }
            VA_LOG("CUDA device %d (%s)\n", devID, devProp.name);
            break;
        }
    }
    if( CUDADeviceID == -1 )
    { assert( false ); return; }

    cudaDeviceProp deviceProps;
    cudaGetDeviceProperties(&deviceProps, CUDADeviceID);

    if( cuCtxGetCurrent(&CUDAContext) != CUDA_SUCCESS )
    { assert( false ); return; }

    // Build our OptiX context
    if( optixDeviceContextCreate( CUDAContext, 0, &OPTIXContext ) != OptixResult::OPTIX_SUCCESS || OPTIXContext == nullptr )
    { assert( false ); return; }

    if( optixDeviceContextSetLogCallback( OPTIXContext, logOptixWarning, nullptr, 4 ) != OptixResult::OPTIX_SUCCESS )
    { assert( false ); return; }

    // Helper buffers that don't need resizing (they're tiny)
    if( IntensityBuffer.getSize() != (1 * sizeof(float)) ) IntensityBuffer._resize(1 * sizeof(float));
    //if ( HDRAverageBuffer.getSize() != (3 * sizeof(float))) mDenoiser.hdrAverageBuffer.resize(3 * sizeof(float));
}

vaDenoiserOptiX::~vaDenoiserOptiX( )
{
    ScratchBuffer._free();
    StateBuffer._free();
    IntensityBuffer._free();

    FreeStagingBuffer( Albedo, GuideLayer.albedo );
    FreeStagingBuffer( Normal, GuideLayer.normal );
    FreeStagingBuffer( MotionVec, GuideLayer.flow );
    FreeStagingBuffer( DenoiserInput, Layer.input );
    FreeStagingBuffer( DenoiserOutput, Layer.output );
    //FreeStagingBuffer( DenoiserPreviousOutput, Layer.previousOutput );

    if( Denoiser != nullptr ) optixDenoiserDestroy( Denoiser ); Denoiser = nullptr;
    optixDeviceContextDestroy( OPTIXContext );
    cudaStreamDestroy( CUDAStream );
}

bool freeSharedDevicePtr(void* ptr)
{
    if (!ptr) return false;
    return cudaSuccess == cudaFree(ptr);
}

void vaDenoiserOptiX::AllocateStagingBuffer( vaRenderDevice & device, SharedBuffer & sharedBuffer, OptixImage2D & image, OptixPixelFormat format )
{
    // Determine what sort of format this buffer should be
    uint32 elemSize     = 0;
    uint32 elemCount    = 0;
    vaResourceFormat vanillaFormat = vaResourceFormat::Unknown;
    switch (format)
    {
    case OPTIX_PIXEL_FORMAT_FLOAT4:
        elemSize        = 4 * sizeof(float);
        elemCount       = 4;
        vanillaFormat   = vaResourceFormat::R32_FLOAT;//vaResourceFormat::R32G32B32A32_FLOAT;
        break;
    case OPTIX_PIXEL_FORMAT_FLOAT3:
        elemSize        = 3 * sizeof(float);
        elemCount       = 3;
        vanillaFormat   = vaResourceFormat::R32_FLOAT;//vaResourceFormat::R32G32B32_FLOAT;
        break;
    case OPTIX_PIXEL_FORMAT_FLOAT2:
        elemSize        = 2 * sizeof(float);
        elemCount       = 2;
        vanillaFormat   = vaResourceFormat::R32_FLOAT;//vaResourceFormat::R32G32_FLOAT;
        break;
    default:
        assert( false ); // unsupported format
        return;
    }

    // If we had an existing buffer in this location, free it.
    if( sharedBuffer.devicePtr ) freeSharedDevicePtr((void*)sharedBuffer.devicePtr);

    sharedBuffer.buffer = vaRenderBuffer::Create( device, Width * Height * elemCount, vanillaFormat, vaRenderBufferFlags::Shared, "DenoiserStaging" );
    
    size_t dataSize;
    if( !sharedBuffer.buffer->GetCUDAShared( (void*&)sharedBuffer.devicePtr, dataSize ) )
    { assert( false ); return; }

    // Setup an OptiXImage2D structure so OptiX will used this new buffer for image data
    image.width                 = Width;
    image.height                = Height;
    image.rowStrideInBytes      = Width * elemSize;
    image.pixelStrideInBytes    = elemSize;
    image.format                = format;
    image.data                  = sharedBuffer.devicePtr;
}

void vaDenoiserOptiX::FreeStagingBuffer( SharedBuffer & sharedBuffer, OptixImage2D & image )
{
    // Free the CUDA memory for this buffer, then set our other references to it to NULL to avoid
    // accidentally trying to access the freed memory.
    if( sharedBuffer.devicePtr != 0 ) freeSharedDevicePtr((void*)sharedBuffer.devicePtr);
    sharedBuffer.buffer = nullptr;
    sharedBuffer.devicePtr = 0;
    image.data = static_cast<CUdeviceptr>(0);
}

void vaDenoiserOptiX::Prepare( vaRenderDevice & device, int width, int height, bool useTemporal )
{
    OptixDenoiserModelKind wantedModelKind = (useTemporal)?(OptixDenoiserModelKind::OPTIX_DENOISER_MODEL_KIND_TEMPORAL):(OptixDenoiserModelKind::OPTIX_DENOISER_MODEL_KIND_HDR);
    if( Denoiser == nullptr || ModelKind != wantedModelKind )
    {
        ModelKind = wantedModelKind;
        Options.guideAlbedo = 1;        // will have guide (aux) albedo
        Options.guideNormal = 1;        // will have guide (aux) normal

        if( Denoiser != nullptr ) optixDenoiserDestroy( Denoiser ); Denoiser = nullptr;

        if( optixDenoiserCreate( OPTIXContext, ModelKind, &Options, &Denoiser ) != OptixResult::OPTIX_SUCCESS )
        { assert( false ); return; }

        CSVanillaToDenoiser = vaComputeShader::CreateFromFile( device, "vaPathTracer.hlsl", "CSVanillaToOptiX", { { "VA_RAYTRACING", "" }, { "VA_OPTIX_DENOISER", "" } }, true );
        CSDenoiserToVanilla = vaComputeShader::CreateFromFile( device, "vaPathTracer.hlsl", "CSOptiXToVanilla", { { "VA_RAYTRACING", "" }, { "VA_OPTIX_DENOISER", "" } }, true );
        
        IsFirstFrame = true;
        Width = 0; Height = 0; // force rebuild of layers
    }

    device; width; height;
    if( Width != width || Height != height )
    {
        Width  = width;
        Height = height;

        // !!!DON'T CHANGE FORMATS HERE without changing corresponding ComputeAddr in the CSVanillaToOptiX/CSOptiXToVanilla shaders!!!
        AllocateStagingBuffer( device, Albedo, GuideLayer.albedo, OPTIX_PIXEL_FORMAT_FLOAT3 );
        AllocateStagingBuffer( device, Normal, GuideLayer.normal, OPTIX_PIXEL_FORMAT_FLOAT3 );
        AllocateStagingBuffer( device, MotionVec, GuideLayer.flow, OPTIX_PIXEL_FORMAT_FLOAT2 );
        AllocateStagingBuffer( device, DenoiserInput, Layer.input, OPTIX_PIXEL_FORMAT_FLOAT3 );
        AllocateStagingBuffer( device, DenoiserOutput, Layer.output, OPTIX_PIXEL_FORMAT_FLOAT3 );
        //AllocateStagingBuffer( device, DenoiserPreviousOutput, Layer.previousOutput, OPTIX_PIXEL_FORMAT_FLOAT4 );

        /// average log intensity of input image (default null pointer). points to a single float.
        /// with the default (null pointer) denoised results will not be optimal for very dark or
        /// bright input images.
        Params.hdrIntensity     = IntensityBuffer.getDevicePtr();
        Params.hdrAverageColor  = static_cast<CUdeviceptr>(0);      // it's either hdrIntensity or hdrAverageColor - we're using the first

        // Find out how much memory is needed for the denoiser...
        if( optixDenoiserComputeMemoryResources( Denoiser, width, height, &Sizes ) != OptixResult::OPTIX_SUCCESS )
        { assert( false ); return; }

        // ...and allocate temporary buffers 
        ScratchBuffer._resize( Sizes.withoutOverlapScratchSizeInBytes );
        StateBuffer._resize( Sizes.stateSizeInBytes );

        // ...and set up the denoiser
        optixDenoiserSetup( Denoiser, nullptr, width, height, StateBuffer.getDevicePtr(), StateBuffer.getSize(), ScratchBuffer.getDevicePtr(), ScratchBuffer.getSize() );

        IsFirstFrame = true;
    }
}

void vaDenoiserOptiX::VanillaToDenoiser( vaRenderDeviceContext & renderContext, vaDrawAttributes & drawAttributes, const shared_ptr<vaTexture> & beautySrc, const shared_ptr<vaTexture> & auxAlbedoSrc, const shared_ptr<vaTexture> & auxNormalsSrc, const shared_ptr<vaTexture> & auxMotionVectorsSrc )
{
    VA_TRACE_CPUGPU_SCOPE( VanillaToOptiXDenoiser, renderContext );

    vaRenderOutputs uavInputsOutputs;
    uavInputsOutputs.UnorderedAccessViews[0] = Albedo.buffer; // shared albedo
    uavInputsOutputs.UnorderedAccessViews[1] = Normal.buffer; // shared normals
    uavInputsOutputs.UnorderedAccessViews[2] = MotionVec.buffer; // shared motion vectors
    uavInputsOutputs.UnorderedAccessViews[3] = DenoiserInput.buffer; // shared input
    uavInputsOutputs.UnorderedAccessViews[4] = nullptr; //DenoiserPreviousOutput.buffer; // shared previous output
    uavInputsOutputs.UnorderedAccessViews[5] = DenoiserOutput.buffer; // shared output
    uavInputsOutputs.UnorderedAccessViews[6] = nullptr;

    vaComputeItem computeItem;

    computeItem.ShaderResourceViews[VA_PATH_TRACER_RADIANCE_SRV_SLOT]               = beautySrc;
    computeItem.ShaderResourceViews[VA_PATH_TRACER_DENOISE_AUX_ALBEDO_SRV_SLOT]     = auxAlbedoSrc;
    computeItem.ShaderResourceViews[VA_PATH_TRACER_DENOISE_AUX_NORMALS_SRV_SLOT]    = auxNormalsSrc;
    computeItem.ShaderResourceViews[VA_PATH_TRACER_DENOISE_AUX_MOTIONVEC_SRV_SLOT]  = auxMotionVectorsSrc;

    computeItem.ComputeShader = CSVanillaToDenoiser;
    computeItem.SetDispatch( (beautySrc->GetWidth()+7)/8, (beautySrc->GetWidth()+7)/8 );
    computeItem.GenericRootConst = beautySrc->GetWidth();
    renderContext.ExecuteSingleItem( computeItem, uavInputsOutputs, &drawAttributes );

    // For temporal, not sure how to initialize previous frames input - Falcor just passes in inputs so let's do the same.
    //if( IsFirstFrame )
    //{
    //    IsFirstFrame = false;
    //    DenoiserPreviousOutput.buffer->CopyFrom( renderContext, *DenoiserInput.buffer );
    //}
}

void vaDenoiserOptiX::Denoise( vaRenderDeviceContext & renderContext )
{
    renderContext.Flush( );                     // submit all work on render context
    renderContext.GetRenderDevice().SyncGPU( ); // sync!
    
    VA_TRACE_CPUGPU_SCOPE( OptiXDenoise, renderContext );           // will clash with flush/sync and not give correct data

    // Compute average intensity, if needed
    if( Params.hdrIntensity )
    {
        optixDenoiserComputeIntensity( Denoiser, nullptr,  &Layer.input, Params.hdrIntensity, ScratchBuffer.getDevicePtr(), ScratchBuffer.getSize() );
    }

    if( IsFirstFrame )
        Layer.previousOutput = Layer.input;

    // Run denoiser
    optixDenoiserInvoke( Denoiser,
        nullptr,                 // CUDA stream
        &Params,
        StateBuffer.getDevicePtr(), StateBuffer.getSize(),
        &GuideLayer,   // Our set of normal / albedo / motion vector guides
        &Layer,        // Array of input or AOV layers (also contains denoised per-layer outputs)
        1u,            // Number of layers in the above array
        0u,            // (Tile) Input offset X
        0u,            // (Tile) Input offset Y
        ScratchBuffer.getDevicePtr(), ScratchBuffer.getSize());

    if( IsFirstFrame )
    {
        Layer.previousOutput = Layer.output;
        IsFirstFrame = false;
    }

    // I suppose the above ^ does the sync? 
}

void vaDenoiserOptiX::DenoiserToVanilla( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & output )
{
    VA_TRACE_CPUGPU_SCOPE( OptiXDenoiserToVanilla, renderContext );

    vaRenderOutputs uavInputsOutputs;
    uavInputsOutputs.UnorderedAccessViews[0] = Albedo.buffer; // shared albedo
    uavInputsOutputs.UnorderedAccessViews[1] = Normal.buffer; // shared normals
    uavInputsOutputs.UnorderedAccessViews[2] = MotionVec.buffer; // shared motion vectors
    uavInputsOutputs.UnorderedAccessViews[3] = DenoiserInput.buffer; // shared input
    uavInputsOutputs.UnorderedAccessViews[4] = nullptr; //DenoiserPreviousOutput.buffer; // shared previous output
    uavInputsOutputs.UnorderedAccessViews[5] = DenoiserOutput.buffer; // shared output
    uavInputsOutputs.UnorderedAccessViews[6] = output; // vanilla output

    vaComputeItem computeItem;
    computeItem.ComputeShader = CSDenoiserToVanilla;
    computeItem.SetDispatch( (output->GetWidth()+7)/8, (output->GetWidth()+7)/8 );
    computeItem.GenericRootConst = output->GetWidth();
    renderContext.ExecuteSingleItem( computeItem, uavInputsOutputs, nullptr );
}


void CudaBuffer::_allocate(size_t size)
{
    if (mpDevicePtr) _free();
    mSizeBytes = size;
    if( cudaMalloc((void**)&mpDevicePtr, mSizeBytes) != cudaSuccess )
    { assert( false ); return; }
}

void CudaBuffer::_resize(size_t size)
{
    _allocate(size);
}

void CudaBuffer::_free(void)
{
    if( cudaFree(mpDevicePtr) != cudaSuccess )
    { assert( false ); return; }
    mpDevicePtr = nullptr;
    mSizeBytes = 0;
}

template<typename T>
bool CudaBuffer::download(T* t, size_t count)
{
    if (!mpDevicePtr) return false;
    if (mSizeBytes <= (count * sizeof(T))) return false;

    CUDA_CHECK(cudaMemcpy((void*)t, mpDevicePtr, count * sizeof(T), cudaMemcpyDeviceToHost));
    return true; // might be an error caught by CUDA_CHECK?  TODO: process any such error through
}

template<typename T>
bool CudaBuffer::upload(const T* t, size_t count)
{
    if (!mpDevicePtr) return false;
    if (mSizeBytes <= (count * sizeof(T))) return false;

    CUDA_CHECK(cudaMemcpy(mpDevicePtr, (void*)t, count * sizeof(T), cudaMemcpyHostToDevice));
    return true; // might be an error caught by CUDA_CHECK?  TODO: process any such error through
}

template<typename T>
void CudaBuffer::allocAndUpload(const std::vector<T>& vt)
{
    allocate(vt.size() * sizeof(T));
    upload((const T*)vt.data(), vt.size());
}

#endif // #ifdef VA_OPTIX_DENOISER_ENABLED
