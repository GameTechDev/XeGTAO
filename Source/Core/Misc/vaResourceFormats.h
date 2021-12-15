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

namespace Vanilla
{

    // shamelessly mirrors DXGI_FORMAT
    enum class vaResourceFormat : int32
    {
        Unknown	                    = 0,
        R32G32B32A32_TYPELESS       = 1,
        R32G32B32A32_FLOAT          = 2,
        R32G32B32A32_UINT           = 3,
        R32G32B32A32_SINT           = 4,
        R32G32B32_TYPELESS          = 5,
        R32G32B32_FLOAT             = 6,
        R32G32B32_UINT              = 7,
        R32G32B32_SINT              = 8,
        R16G16B16A16_TYPELESS       = 9,
        R16G16B16A16_FLOAT          = 10,
        R16G16B16A16_UNORM          = 11,
        R16G16B16A16_UINT           = 12,
        R16G16B16A16_SNORM          = 13,
        R16G16B16A16_SINT           = 14,
        R32G32_TYPELESS             = 15,
        R32G32_FLOAT                = 16,
        R32G32_UINT                 = 17,
        R32G32_SINT                 = 18,
        R32G8X24_TYPELESS           = 19,
        D32_FLOAT_S8X24_UINT        = 20,
        R32_FLOAT_X8X24_TYPELESS    = 21,
        X32_TYPELESS_G8X24_UINT     = 22,
        R10G10B10A2_TYPELESS        = 23,
        R10G10B10A2_UNORM           = 24,
        R10G10B10A2_UINT            = 25,
        R11G11B10_FLOAT             = 26,
        R8G8B8A8_TYPELESS           = 27,
        R8G8B8A8_UNORM              = 28,
        R8G8B8A8_UNORM_SRGB         = 29,
        R8G8B8A8_UINT               = 30,
        R8G8B8A8_SNORM              = 31,
        R8G8B8A8_SINT               = 32,
        R16G16_TYPELESS             = 33,
        R16G16_FLOAT                = 34,
        R16G16_UNORM                = 35,
        R16G16_UINT                 = 36,
        R16G16_SNORM                = 37,
        R16G16_SINT                 = 38,
        R32_TYPELESS                = 39,
        D32_FLOAT                   = 40,
        R32_FLOAT                   = 41,
        R32_UINT                    = 42,
        R32_SINT                    = 43,
        R24G8_TYPELESS              = 44,
        D24_UNORM_S8_UINT           = 45,
        R24_UNORM_X8_TYPELESS       = 46,
        X24_TYPELESS_G8_UINT        = 47,
        R8G8_TYPELESS               = 48,
        R8G8_UNORM                  = 49,
        R8G8_UINT                   = 50,
        R8G8_SNORM                  = 51,
        R8G8_SINT                   = 52,
        R16_TYPELESS                = 53,
        R16_FLOAT                   = 54,
        D16_UNORM                   = 55,
        R16_UNORM                   = 56,
        R16_UINT                    = 57,
        R16_SNORM                   = 58,
        R16_SINT                    = 59,
        R8_TYPELESS                 = 60,
        R8_UNORM                    = 61,
        R8_UINT                     = 62,
        R8_SNORM                    = 63,
        R8_SINT                     = 64,
        A8_UNORM                    = 65,
        R1_UNORM                    = 66,
        R9G9B9E5_SHAREDEXP          = 67,
        R8G8_B8G8_UNORM             = 68,
        G8R8_G8B8_UNORM             = 69,
        BC1_TYPELESS                = 70,
        BC1_UNORM                   = 71,
        BC1_UNORM_SRGB              = 72,
        BC2_TYPELESS                = 73,
        BC2_UNORM                   = 74,
        BC2_UNORM_SRGB              = 75,
        BC3_TYPELESS                = 76,
        BC3_UNORM                   = 77,
        BC3_UNORM_SRGB              = 78,
        BC4_TYPELESS                = 79,
        BC4_UNORM                   = 80,
        BC4_SNORM                   = 81,
        BC5_TYPELESS                = 82,
        BC5_UNORM                   = 83,
        BC5_SNORM                   = 84,
        B5G6R5_UNORM                = 85,
        B5G5R5A1_UNORM              = 86,
        B8G8R8A8_UNORM              = 87,
        B8G8R8X8_UNORM              = 88,
        R10G10B10_XR_BIAS_A2_UNORM  = 89,
        B8G8R8A8_TYPELESS           = 90,
        B8G8R8A8_UNORM_SRGB         = 91,
        B8G8R8X8_TYPELESS           = 92,
        B8G8R8X8_UNORM_SRGB         = 93,
        BC6H_TYPELESS               = 94,
        BC6H_UF16                   = 95,
        BC6H_SF16                   = 96,
        BC7_TYPELESS                = 97,
        BC7_UNORM                   = 98,
        BC7_UNORM_SRGB              = 99,
        AYUV                        = 100,
        Y410                        = 101,
        Y416                        = 102,
        NV12                        = 103,
        P010                        = 104,
        P016                        = 105,
        F420_OPAQUE                 = 106,
        YUY2                        = 107,
        Y210                        = 108,
        Y216                        = 109,
        NV11                        = 110,
        AI44                        = 111,
        IA44                        = 112,
        P8                          = 113,
        A8P8                        = 114,
        B4G4R4A4_UNORM              = 115,
        MaxVal,


        // used for automatic view format selection
        Automatic                   = 0x7FFFFFFF
    };

    enum class vaResourceBindSupportFlags : uint32
    {
        None                        = 0,
        VertexBuffer                = (1 << 0),
        IndexBuffer                 = (1 << 1),
        ConstantBuffer              = (1 << 2),
        ShaderResource              = (1 << 3),
        RenderTarget                = (1 << 4),
        DepthStencil                = (1 << 5),
        UnorderedAccess             = (1 << 6),
        Shared                      = (1 << 7),     // for sharing with non-D3D12 APIs
        RaytracingAccelerationStructure = (1 << 8),
    };
    BITFLAG_ENUM_CLASS_HELPER( vaResourceBindSupportFlags );

    enum class vaResourceMapType : uint32
    {
        None                        = 0,
        Read                        = 1,    // see D3D11_MAP_READ
        Write	                    = 2,    // see D3D11_MAP_WRITE
        ReadWrite                   = 3,    // this is not actually supported and will probably be removed
        WriteDiscard                = 4,    // (not supported for textures) see D3D11_MAP_WRITE_DISCARD
        WriteNoOverwrite            = 5     // (not supported for textures) see D3D11_MAP_WRITE_NO_OVERWRITE
    };


    class vaResourceFormatHelpers
    {
    private:
        vaResourceFormatHelpers()    { }
        ~vaResourceFormatHelpers()   { }
    
    public:
        static string               EnumToString( vaResourceFormat val );
        static int                  GetPixelSizeInBytes( vaResourceFormat val );
        static int                  GetChannelCount( vaResourceFormat val );
        static bool                 HasAlphaChannel( vaResourceFormat val );
        static bool                 IsTypeless( vaResourceFormat val );
        static bool                 IsSRGB( vaResourceFormat val );
        static bool                 IsFloat( vaResourceFormat val );
        static vaResourceFormat     StripSRGB( vaResourceFormat val );
    };

    // generic bindable resource base class
    class vaShaderResource : virtual public vaFramePtrTag
    { 
        void *                      m_cachedPlatformPtr = nullptr;
    public:
        virtual ~vaShaderResource( ) {}; 

        virtual vaResourceBindSupportFlags  GetBindSupportFlags( ) const = 0;

        // If a renderContextPtr is provided then the resource state is properly transitioned; if nullptr the caller is responsible for states
        virtual uint32              GetSRVBindlessIndex( class vaRenderDeviceContext * renderContextPtr )                             = 0;

    public:
       // to avoid dynamic_cast for performance reasons (as this specific call happens very, very frequently) 
       template< typename OutType > OutType SafeCast( ) { return vaCachedDynamicCast<OutType>( this, m_cachedPlatformPtr ); }
    };


}