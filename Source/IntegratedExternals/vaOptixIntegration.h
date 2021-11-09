// To enable:
//  * download CUDA SDK from https://developer.nvidia.com/cuda-downloads (tested with 11.5)
//  * download OptiX SDK from https://developer.nvidia.com/designworks/optix/download (tested with 7.3.0)
//  * copy OptiX SDK stuff from for ex. "C:/ProgramData/NVIDIA Corporation/OptiX SDK 7.3.0/include" to "../vanilla/Source/IntegratedExternals/optix/include"

#pragma once

#ifdef VA_OPTIX_DENOISER_ENABLED

#include "Core/vaCore.h"

#include <cuda.h>                   // relies on CUDA_PATH being set!
#include <cuda_runtime.h>           // relies on CUDA_PATH being set!

#pragma comment(lib, "cuda")        // relies on CUDA_PATH being set!
#pragma comment(lib, "cudart")      // relies on CUDA_PATH being set!


#define OPTIX_DONT_INCLUDE_CUDA
#include "optix/include/optix.h"
#include "optix/include/optix_stubs.h"


#endif // VA_OIDN_INTEGRATION_ENABLED