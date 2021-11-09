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

#include "Rendering/vaRendering.h"

#include "Core/vaUI.h"

#include "IntegratedExternals/vaOIDNIntegration.h"  // VA_OIDN_INTEGRATION_ENABLED
#include "IntegratedExternals/vaOptixIntegration.h"

namespace Vanilla
{
#ifdef VA_OIDN_INTEGRATION_ENABLED

    // struct because it's only an extension of vaPathTracer - nobody else will use it
    struct vaDenoiserOIDN
    {
        OIDNDevice                  Device              = nullptr;
        OIDNFilter                  Filter              = nullptr;

        OIDNBuffer                  Beauty              = nullptr;
        OIDNBuffer                  Output              = nullptr;
        OIDNBuffer                  AuxAlbedo           = nullptr;
        OIDNBuffer                  AuxNormals          = nullptr;

        shared_ptr<vaTexture>       BeautyGPU;
        shared_ptr<vaTexture>       BeautyCPU;

        shared_ptr<vaTexture>       DenoisedGPU;
        shared_ptr<vaTexture>       DenoisedCPU;

        shared_ptr<vaTexture>       AuxAlbedoCPU;
        shared_ptr<vaTexture>       AuxNormalsCPU;

        //shared_ptr<vaComputeShader> CSDebugDisplayDenoiser;

        int                         Width               = 0;
        int                         Height              = 0;
        int                         BytesPerPixel       = 0;
        size_t                      BufferSize          = 0;

//    public:
        vaDenoiserOIDN( );
        ~vaDenoiserOIDN( );

        void UpdateTextures( vaRenderDevice & device, int width, int height );
        static void CopyContents( vaRenderDeviceContext & renderContext, OIDNBuffer destination, const shared_ptr<vaTexture> & source, size_t bufferSize );
        static void CopyContents( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & destination, OIDNBuffer source, size_t bufferSize );
        void VanillaToDenoiser( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & beautySrc, const shared_ptr<vaTexture> & auxAlbedoSrc, const shared_ptr<vaTexture> & auxNormalsSrc );
        void Denoise( );
        void DenoiserToVanilla( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & output );
    };

#endif // #ifdef VA_OIDN_INTEGRATION_ENABLED

#ifdef VA_OPTIX_DENOISER_ENABLED
    // A utility class for a GPU/device buffer for use with CUDA.  This is essentially stolen from Falcor who stole it from Ingo Wald's SIGGRAPH 2019 tutorial code for OptiX 7 :D
    class CudaBuffer
    {
    public:
        CudaBuffer() {}

        CUdeviceptr getDevicePtr() { return (CUdeviceptr)mpDevicePtr; }
        size_t      getSize() { return mSizeBytes; }

        void _allocate(size_t size);
        void _resize(size_t size);
        void _free();

        template<typename T>
        void allocAndUpload(const std::vector<T>& vt);

        template<typename T>
        bool download(T* t, size_t count);

        template<typename T>
        bool upload(const T* t, size_t count);

    private:
        size_t  mSizeBytes  = 0;
        void*   mpDevicePtr = nullptr;
    };


    // Represent shared Vanilla DX <-> CUDA texture
    struct SharedBuffer
    {
        shared_ptr<vaRenderBuffer>  buffer;
        CUdeviceptr                 devicePtr = (CUdeviceptr)0;   // CUDA pointer to buffer
        SharedBuffer()              {}
        ~SharedBuffer()             { assert( devicePtr == (CUdeviceptr)0 ); }
    };

    // struct because it's only an extension of vaPathTracer - nobody else will use it
    // all based on https://github.com/NVIDIAGameWorks/Falcor and other open-source examples from the internets
    struct vaDenoiserOptiX
    {
        bool                        Initialized         = false;
        int                         CUDADeviceID        = -1;
        uint32                      CUDADeviceNodeMask  = 0;
        CUstream                    CUDAStream;
        CUcontext                   CUDAContext;

        OptixDeviceContext          OPTIXContext        = nullptr;
        OptixDenoiser               Denoiser            = nullptr;
        OptixDenoiserOptions        Options             = { 0, 0 }; // guide albedo and guide normals
        OptixDenoiserModelKind      ModelKind           = OptixDenoiserModelKind::OPTIX_DENOISER_MODEL_KIND_HDR;     // OPTIX_DENOISER_MODEL_KIND_TEMPORAL
        OptixDenoiserParams         Params              = { 0u, static_cast<CUdeviceptr>(0), 0.0f, static_cast<CUdeviceptr>(0) };
        OptixDenoiserSizes          Sizes               = { 0, 0, 0, 0 };

        int                         Width               = 0;
        int                         Height              = 0;
        
        bool                        IsFirstFrame        = true;

        // these are necessary for denoiser operation
        CudaBuffer                  ScratchBuffer;
        CudaBuffer                  StateBuffer;
        
        // these are tiny ones, required to precompute some stuff
        CudaBuffer                  IntensityBuffer;
        //CudaBuffer                  HDRAverageBuffer;   // 
        //, intensityBuffer, hdrAverageBuffer;

        // Albedo, normals and motion vectors (a.k.a. flow)
        OptixDenoiserGuideLayer     GuideLayer = {};

        // Input color, output color and previous frame output when using temporal
        OptixDenoiserLayer          Layer = {};

        // A wrapper around our guide layer interop with DirectX
        SharedBuffer                Albedo;
        SharedBuffer                Normal;
        SharedBuffer                MotionVec;
        SharedBuffer                DenoiserInput;
        SharedBuffer                DenoiserOutput;
        SharedBuffer                DenoiserPreviousOutput;

        shared_ptr<vaComputeShader> CSVanillaToDenoiser;
        shared_ptr<vaComputeShader> CSDenoiserToVanilla;

        vaDenoiserOptiX( vaRenderDevice & device );
        ~vaDenoiserOptiX( );

        void    Prepare( vaRenderDevice & device, int width, int height, bool useTemporal );

        void    VanillaToDenoiser( vaRenderDeviceContext & renderContext, vaDrawAttributes & drawAttributes, const shared_ptr<vaTexture> & beautySrc, const shared_ptr<vaTexture> & auxAlbedoSrc, const shared_ptr<vaTexture> & auxNormalsSrc, const shared_ptr<vaTexture> & auxMotionVectorsSrc );
        void    Denoise( vaRenderDeviceContext & renderContext );
        void    DenoiserToVanilla( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & output );

        void    AllocateStagingBuffer( vaRenderDevice & device, SharedBuffer & sharedBuffer, OptixImage2D & image, OptixPixelFormat format );
        void    FreeStagingBuffer( SharedBuffer & sharedBuffer, OptixImage2D & image );

    };

#endif // #ifdef VA_OPTIX_DENOISER_ENABLED

} // namespace Vanilla

