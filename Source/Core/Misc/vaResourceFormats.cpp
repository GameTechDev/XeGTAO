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

#include "vaResourceFormats.h"

using namespace Vanilla;


string vaResourceFormatHelpers::EnumToString( vaResourceFormat val )
{
    assert( (int)vaResourceFormat::MaxVal == 116 ); // need to update the table maybe?

    const char * table[] = {
        "Unknown",
        "R32G32B32A32_TYPELESS",
        "R32G32B32A32_FLOAT",
        "R32G32B32A32_UINT",
        "R32G32B32A32_SINT",
        "R32G32B32_TYPELESS",
        "R32G32B32_FLOAT",
        "R32G32B32_UINT",
        "R32G32B32_SINT",
        "R16G16B16A16_TYPELESS",
        "R16G16B16A16_FLOAT",
        "R16G16B16A16_UNORM",
        "R16G16B16A16_UINT",
        "R16G16B16A16_SNORM",
        "R16G16B16A16_SINT",
        "R32G32_TYPELESS",
        "R32G32_FLOAT",
        "R32G32_UINT",
        "R32G32_SINT",
        "R32G8X24_TYPELESS",
        "D32_FLOAT_S8X24_UINT",
        "R32_FLOAT_X8X24_TYPELESS",
        "X32_TYPELESS_G8X24_UINT",
        "R10G10B10A2_TYPELESS",
        "R10G10B10A2_UNORM",
        "R10G10B10A2_UINT",
        "R11G11B10_FLOAT",
        "R8G8B8A8_TYPELESS",
        "R8G8B8A8_UNORM",
        "R8G8B8A8_UNORM_SRGB",
        "R8G8B8A8_UINT",
        "R8G8B8A8_SNORM",
        "R8G8B8A8_SINT",
        "R16G16_TYPELESS",
        "R16G16_FLOAT",
        "R16G16_UNORM",
        "R16G16_UINT",
        "R16G16_SNORM",
        "R16G16_SINT",
        "R32_TYPELESS",
        "D32_FLOAT",
        "R32_FLOAT",
        "R32_UINT",
        "R32_SINT",
        "R24G8_TYPELESS",
        "D24_UNORM_S8_UINT",
        "R24_UNORM_X8_TYPELESS",
        "X24_TYPELESS_G8_UINT",
        "R8G8_TYPELESS",
        "R8G8_UNORM",
        "R8G8_UINT",
        "R8G8_SNORM",
        "R8G8_SINT",
        "R16_TYPELESS",
        "R16_FLOAT",
        "D16_UNORM",
        "R16_UNORM",
        "R16_UINT",
        "R16_SNORM",
        "R16_SINT",
        "R8_TYPELESS",
        "R8_UNORM",
        "R8_UINT",
        "R8_SNORM",
        "R8_SINT",
        "A8_UNORM",
        "R1_UNORM",
        "R9G9B9E5_SHAREDEXP",
        "R8G8_B8G8_UNORM",
        "G8R8_G8B8_UNORM",
        "BC1_TYPELESS",
        "BC1_UNORM",
        "BC1_UNORM_SRGB",
        "BC2_TYPELESS",
        "BC2_UNORM",
        "BC2_UNORM_SRGB",
        "BC3_TYPELESS",
        "BC3_UNORM",
        "BC3_UNORM_SRGB",
        "BC4_TYPELESS",
        "BC4_UNORM",
        "BC4_SNORM",
        "BC5_TYPELESS",
        "BC5_UNORM",
        "BC5_SNORM",
        "B5G6R5_UNORM",
        "B5G5R5A1_UNORM",
        "B8G8R8A8_UNORM",
        "B8G8R8X8_UNORM",
        "R10G10B10_XR_BIAS_A2_UNORM",
        "B8G8R8A8_TYPELESS",
        "B8G8R8A8_UNORM_SRGB",
        "B8G8R8X8_TYPELESS",
        "B8G8R8X8_UNORM_SRGB",
        "BC6H_TYPELESS",
        "BC6H_UF16",
        "BC6H_SF16",
        "BC7_TYPELESS",
        "BC7_UNORM",
        "BC7_UNORM_SRGB",
        "AYUV",
        "Y410",
        "Y416",
        "NV12",
        "P010",
        "P016",
        "F420_OPAQUE",
        "YUY2",
        "Y210",
        "Y216",
        "NV11",
        "AI44",
        "IA44",
        "P8",
        "A8P8",
        "B4G4R4A4_UNORM",
    };

    int index = (int)val;
    if( ( index < 0 ) || ( index > _countof( table ) - 1 ) )
    {
        assert( false );
        return "OutOfRangeError";
    }

    return table[index];
}

int vaResourceFormatHelpers::GetPixelSizeInBytes( vaResourceFormat val )
{
    switch( val )
    {
    case Vanilla::vaResourceFormat::Unknown:                    return 0;
    case Vanilla::vaResourceFormat::R32G32B32A32_TYPELESS:      return 4*4;
    case Vanilla::vaResourceFormat::R32G32B32A32_FLOAT:         return 4*4;
    case Vanilla::vaResourceFormat::R32G32B32A32_UINT:          return 4*4;
    case Vanilla::vaResourceFormat::R32G32B32A32_SINT:          return 4*4;
    case Vanilla::vaResourceFormat::R32G32B32_TYPELESS:         return 3*4;
    case Vanilla::vaResourceFormat::R32G32B32_FLOAT:            return 3*4;
    case Vanilla::vaResourceFormat::R32G32B32_UINT:             return 3*4;
    case Vanilla::vaResourceFormat::R32G32B32_SINT:             return 3*4;
    case Vanilla::vaResourceFormat::R16G16B16A16_TYPELESS:      return 4*2;
    case Vanilla::vaResourceFormat::R16G16B16A16_FLOAT:         return 4*2;
    case Vanilla::vaResourceFormat::R16G16B16A16_UNORM:         return 4*2;
    case Vanilla::vaResourceFormat::R16G16B16A16_UINT:          return 4*2;
    case Vanilla::vaResourceFormat::R16G16B16A16_SNORM:         return 4*2;
    case Vanilla::vaResourceFormat::R16G16B16A16_SINT:          return 4*2;
    case Vanilla::vaResourceFormat::R32G32_TYPELESS:            return 2*4;
    case Vanilla::vaResourceFormat::R32G32_FLOAT:               return 2*4;
    case Vanilla::vaResourceFormat::R32G32_UINT:                return 2*4;
    case Vanilla::vaResourceFormat::R32G32_SINT:                return 2*4;
    case Vanilla::vaResourceFormat::R32G8X24_TYPELESS:          return 4+1;
    case Vanilla::vaResourceFormat::D32_FLOAT_S8X24_UINT:       return 4+1;
    case Vanilla::vaResourceFormat::R32_FLOAT_X8X24_TYPELESS:   return 4+1;
    case Vanilla::vaResourceFormat::X32_TYPELESS_G8X24_UINT:    return 4+1;
    case Vanilla::vaResourceFormat::R10G10B10A2_TYPELESS:       return 4;
    case Vanilla::vaResourceFormat::R10G10B10A2_UNORM:          return 4;
    case Vanilla::vaResourceFormat::R10G10B10A2_UINT:           return 4;
    case Vanilla::vaResourceFormat::R11G11B10_FLOAT:            return 4;
    case Vanilla::vaResourceFormat::R8G8B8A8_TYPELESS:          return 4;
    case Vanilla::vaResourceFormat::R8G8B8A8_UNORM:             return 4;
    case Vanilla::vaResourceFormat::R8G8B8A8_UNORM_SRGB:        return 4;
    case Vanilla::vaResourceFormat::R8G8B8A8_UINT:              return 4;
    case Vanilla::vaResourceFormat::R8G8B8A8_SNORM:             return 4;
    case Vanilla::vaResourceFormat::R8G8B8A8_SINT:              return 4;
    case Vanilla::vaResourceFormat::R16G16_TYPELESS:            return 4;
    case Vanilla::vaResourceFormat::R16G16_FLOAT:               return 4;
    case Vanilla::vaResourceFormat::R16G16_UNORM:               return 4;
    case Vanilla::vaResourceFormat::R16G16_UINT:                return 4;
    case Vanilla::vaResourceFormat::R16G16_SNORM:               return 4;
    case Vanilla::vaResourceFormat::R16G16_SINT:                return 4;
    case Vanilla::vaResourceFormat::R32_TYPELESS:               return 4;
    case Vanilla::vaResourceFormat::D32_FLOAT:                  return 4;
    case Vanilla::vaResourceFormat::R32_FLOAT:                  return 4;
    case Vanilla::vaResourceFormat::R32_UINT:                   return 4;
    case Vanilla::vaResourceFormat::R32_SINT:                   return 4;
    case Vanilla::vaResourceFormat::R24G8_TYPELESS:             return 4;
    case Vanilla::vaResourceFormat::D24_UNORM_S8_UINT:          return 4;
    case Vanilla::vaResourceFormat::R24_UNORM_X8_TYPELESS:      return 4;
    case Vanilla::vaResourceFormat::X24_TYPELESS_G8_UINT:       return 4;
    case Vanilla::vaResourceFormat::R8G8_TYPELESS:              return 2;
    case Vanilla::vaResourceFormat::R8G8_UNORM:                 return 2;
    case Vanilla::vaResourceFormat::R8G8_UINT:                  return 2;
    case Vanilla::vaResourceFormat::R8G8_SNORM:                 return 2;
    case Vanilla::vaResourceFormat::R8G8_SINT:                  return 2;
    case Vanilla::vaResourceFormat::R16_TYPELESS:               return 2;
    case Vanilla::vaResourceFormat::R16_FLOAT:                  return 2;
    case Vanilla::vaResourceFormat::D16_UNORM:                  return 2;
    case Vanilla::vaResourceFormat::R16_UNORM:                  return 2;
    case Vanilla::vaResourceFormat::R16_UINT:                   return 2;
    case Vanilla::vaResourceFormat::R16_SNORM:                  return 2;
    case Vanilla::vaResourceFormat::R16_SINT:                   return 2;
    case Vanilla::vaResourceFormat::R8_TYPELESS:                return 1;
    case Vanilla::vaResourceFormat::R8_UNORM:                   return 1;
    case Vanilla::vaResourceFormat::R8_UINT:                    return 1;
    case Vanilla::vaResourceFormat::R8_SNORM:                   return 1;
    case Vanilla::vaResourceFormat::R8_SINT:                    return 1;
    case Vanilla::vaResourceFormat::A8_UNORM:                   return 1;
    case Vanilla::vaResourceFormat::R1_UNORM:                   return 1;
    case Vanilla::vaResourceFormat::R9G9B9E5_SHAREDEXP:         return 4;
    case Vanilla::vaResourceFormat::R8G8_B8G8_UNORM:            return 4;
    case Vanilla::vaResourceFormat::G8R8_G8B8_UNORM:            return 4;
    case Vanilla::vaResourceFormat::BC1_TYPELESS:               assert( false ); return 0; // not supported for compressed formats
    case Vanilla::vaResourceFormat::BC1_UNORM:                  assert( false ); return 0; // not supported for compressed formats
    case Vanilla::vaResourceFormat::BC1_UNORM_SRGB:             assert( false ); return 0; // not supported for compressed formats
    case Vanilla::vaResourceFormat::BC2_TYPELESS:               assert( false ); return 0; // not supported for compressed formats
    case Vanilla::vaResourceFormat::BC2_UNORM:                  assert( false ); return 0; // not supported for compressed formats
    case Vanilla::vaResourceFormat::BC2_UNORM_SRGB:             assert( false ); return 0; // not supported for compressed formats
    case Vanilla::vaResourceFormat::BC3_TYPELESS:               assert( false ); return 0; // not supported for compressed formats
    case Vanilla::vaResourceFormat::BC3_UNORM:                  assert( false ); return 0; // not supported for compressed formats
    case Vanilla::vaResourceFormat::BC3_UNORM_SRGB:             assert( false ); return 0; // not supported for compressed formats
    case Vanilla::vaResourceFormat::BC4_TYPELESS:               assert( false ); return 0; // not supported for compressed formats
    case Vanilla::vaResourceFormat::BC4_UNORM:                  assert( false ); return 0; // not supported for compressed formats
    case Vanilla::vaResourceFormat::BC4_SNORM:                  assert( false ); return 0; // not supported for compressed formats
    case Vanilla::vaResourceFormat::BC5_TYPELESS:               assert( false ); return 0; // not supported for compressed formats
    case Vanilla::vaResourceFormat::BC5_UNORM:                  assert( false ); return 0; // not supported for compressed formats
    case Vanilla::vaResourceFormat::BC5_SNORM:                  assert( false ); return 0; // not supported for compressed formats
    case Vanilla::vaResourceFormat::B5G6R5_UNORM:               return 2;
    case Vanilla::vaResourceFormat::B5G5R5A1_UNORM:             return 2;
    case Vanilla::vaResourceFormat::B8G8R8A8_UNORM:             return 4;
    case Vanilla::vaResourceFormat::B8G8R8X8_UNORM:             return 4;
    case Vanilla::vaResourceFormat::R10G10B10_XR_BIAS_A2_UNORM: return 4;
    case Vanilla::vaResourceFormat::B8G8R8A8_TYPELESS:          return 4;
    case Vanilla::vaResourceFormat::B8G8R8A8_UNORM_SRGB:        return 4;
    case Vanilla::vaResourceFormat::B8G8R8X8_TYPELESS:          return 4;
    case Vanilla::vaResourceFormat::B8G8R8X8_UNORM_SRGB:        return 4;
    case Vanilla::vaResourceFormat::BC6H_TYPELESS:              assert( false ); return 0; // not supported for compressed formats
    case Vanilla::vaResourceFormat::BC6H_UF16:                  assert( false ); return 0; // not supported for compressed formats
    case Vanilla::vaResourceFormat::BC6H_SF16:                  assert( false ); return 0; // not supported for compressed formats
    case Vanilla::vaResourceFormat::BC7_TYPELESS:               assert( false ); return 0; // not supported for compressed formats
    case Vanilla::vaResourceFormat::BC7_UNORM:                  assert( false ); return 0; // not supported for compressed formats
    case Vanilla::vaResourceFormat::BC7_UNORM_SRGB:             assert( false ); return 0; // not supported for compressed formats
    case Vanilla::vaResourceFormat::AYUV:                       assert( false ); return 0; // not yet implemented
    case Vanilla::vaResourceFormat::Y410:                       assert( false ); return 0; // not yet implemented
    case Vanilla::vaResourceFormat::Y416:                       assert( false ); return 0; // not yet implemented
    case Vanilla::vaResourceFormat::NV12:                       assert( false ); return 0; // not yet implemented
    case Vanilla::vaResourceFormat::P010:                       assert( false ); return 0; // not yet implemented
    case Vanilla::vaResourceFormat::P016:                       assert( false ); return 0; // not yet implemented
    case Vanilla::vaResourceFormat::F420_OPAQUE:                assert( false ); return 0; // not yet implemented
    case Vanilla::vaResourceFormat::YUY2:                       assert( false ); return 0; // not yet implemented
    case Vanilla::vaResourceFormat::Y210:                       assert( false ); return 0; // not yet implemented
    case Vanilla::vaResourceFormat::Y216:                       assert( false ); return 0; // not yet implemented
    case Vanilla::vaResourceFormat::NV11:                       assert( false ); return 0; // not yet implemented
    case Vanilla::vaResourceFormat::AI44:                       assert( false ); return 0; // not yet implemented
    case Vanilla::vaResourceFormat::IA44:                       assert( false ); return 0; // not yet implemented
    case Vanilla::vaResourceFormat::P8:                         return 1;
    case Vanilla::vaResourceFormat::A8P8:                       return 2;
    case Vanilla::vaResourceFormat::B4G4R4A4_UNORM:             return 2;
    default: break;
    }
    assert( false );
    return 0;
}

bool vaResourceFormatHelpers::IsTypeless( vaResourceFormat val )
{
    switch( val )
    {
        case Vanilla::vaResourceFormat::R32G32B32A32_TYPELESS   : return true;
        case Vanilla::vaResourceFormat::R32G32B32_TYPELESS      : return true;
        case Vanilla::vaResourceFormat::R16G16B16A16_TYPELESS   : return true;
        case Vanilla::vaResourceFormat::R32G32_TYPELESS         : return true;
        case Vanilla::vaResourceFormat::R32G8X24_TYPELESS       : return true;
        case Vanilla::vaResourceFormat::R32_FLOAT_X8X24_TYPELESS: return true;
        case Vanilla::vaResourceFormat::X32_TYPELESS_G8X24_UINT : return true;
        case Vanilla::vaResourceFormat::R10G10B10A2_TYPELESS    : return true;
        case Vanilla::vaResourceFormat::R8G8B8A8_TYPELESS       : return true;
        case Vanilla::vaResourceFormat::R16G16_TYPELESS         : return true;
        case Vanilla::vaResourceFormat::R32_TYPELESS            : return true;
        case Vanilla::vaResourceFormat::R24G8_TYPELESS          : return true;
        case Vanilla::vaResourceFormat::R24_UNORM_X8_TYPELESS   : return true;
        case Vanilla::vaResourceFormat::X24_TYPELESS_G8_UINT    : return true;
        case Vanilla::vaResourceFormat::R8G8_TYPELESS           : return true;
        case Vanilla::vaResourceFormat::R16_TYPELESS            : return true;
        case Vanilla::vaResourceFormat::R8_TYPELESS             : return true;
        case Vanilla::vaResourceFormat::BC1_TYPELESS            : return true;
        case Vanilla::vaResourceFormat::BC2_TYPELESS            : return true;
        case Vanilla::vaResourceFormat::BC3_TYPELESS            : return true;
        case Vanilla::vaResourceFormat::BC4_TYPELESS            : return true;
        case Vanilla::vaResourceFormat::BC5_TYPELESS            : return true;
        case Vanilla::vaResourceFormat::B8G8R8A8_TYPELESS       : return true;
        case Vanilla::vaResourceFormat::B8G8R8X8_TYPELESS       : return true;
        case Vanilla::vaResourceFormat::BC6H_TYPELESS           : return true;
        case Vanilla::vaResourceFormat::BC6H_UF16               : return true;
        case Vanilla::vaResourceFormat::BC6H_SF16               : return true;
        case Vanilla::vaResourceFormat::BC7_TYPELESS            : return true;
        default: return false; break;
    }
}

bool vaResourceFormatHelpers::IsSRGB( vaResourceFormat val )
{
    switch( val )
    {
    case Vanilla::vaResourceFormat::R8G8B8A8_UNORM_SRGB:        return true;
    case Vanilla::vaResourceFormat::BC1_UNORM_SRGB:             return true;
    case Vanilla::vaResourceFormat::BC2_UNORM_SRGB:             return true;
    case Vanilla::vaResourceFormat::BC3_UNORM_SRGB:             return true;
    case Vanilla::vaResourceFormat::B8G8R8A8_UNORM_SRGB:        return true;
    case Vanilla::vaResourceFormat::B8G8R8X8_UNORM_SRGB:        return true;
    case Vanilla::vaResourceFormat::BC7_UNORM_SRGB:             return true;
    default: return false; break;
    }
}

vaResourceFormat vaResourceFormatHelpers::StripSRGB( vaResourceFormat val )
{
    switch( val )
    {
    case Vanilla::vaResourceFormat::R8G8B8A8_UNORM_SRGB:        return Vanilla::vaResourceFormat::R8G8B8A8_UNORM;
    case Vanilla::vaResourceFormat::BC1_UNORM_SRGB:             return Vanilla::vaResourceFormat::BC1_UNORM;
    case Vanilla::vaResourceFormat::BC2_UNORM_SRGB:             return Vanilla::vaResourceFormat::BC2_UNORM;
    case Vanilla::vaResourceFormat::BC3_UNORM_SRGB:             return Vanilla::vaResourceFormat::BC3_UNORM;
    case Vanilla::vaResourceFormat::B8G8R8A8_UNORM_SRGB:        return Vanilla::vaResourceFormat::B8G8R8A8_UNORM;
    case Vanilla::vaResourceFormat::B8G8R8X8_UNORM_SRGB:        return Vanilla::vaResourceFormat::B8G8R8X8_UNORM;
    case Vanilla::vaResourceFormat::BC7_UNORM_SRGB:             return Vanilla::vaResourceFormat::BC7_UNORM;
    default: return val;
    }
}

bool vaResourceFormatHelpers::IsFloat( vaResourceFormat val )
{
    switch( val )
    {
    case Vanilla::vaResourceFormat::R32G32B32A32_FLOAT      : return true;
    case Vanilla::vaResourceFormat::R32G32B32_FLOAT         : return true;
    case Vanilla::vaResourceFormat::R16G16B16A16_FLOAT      : return true;
    case Vanilla::vaResourceFormat::R32G32_FLOAT            : return true;
    case Vanilla::vaResourceFormat::D32_FLOAT_S8X24_UINT    : return true;
    case Vanilla::vaResourceFormat::R32_FLOAT_X8X24_TYPELESS: return true;
    case Vanilla::vaResourceFormat::R11G11B10_FLOAT         : return true;
    case Vanilla::vaResourceFormat::R16G16_FLOAT            : return true;
    case Vanilla::vaResourceFormat::D32_FLOAT               : return true;
    case Vanilla::vaResourceFormat::R32_FLOAT               : return true;
    case Vanilla::vaResourceFormat::R16_FLOAT               : return true;
    default: return false; break;
    }
}

int vaResourceFormatHelpers::GetChannelCount( vaResourceFormat val )
{
    switch( val )
    {
    case Vanilla::vaResourceFormat::Unknown:                    return 0;
    case Vanilla::vaResourceFormat::R32G32B32A32_TYPELESS:      return 4;
    case Vanilla::vaResourceFormat::R32G32B32A32_FLOAT:         return 4;
    case Vanilla::vaResourceFormat::R32G32B32A32_UINT:          return 4;
    case Vanilla::vaResourceFormat::R32G32B32A32_SINT:          return 4;
    case Vanilla::vaResourceFormat::R32G32B32_TYPELESS:         return 3;
    case Vanilla::vaResourceFormat::R32G32B32_FLOAT:            return 3;
    case Vanilla::vaResourceFormat::R32G32B32_UINT:             return 3;
    case Vanilla::vaResourceFormat::R32G32B32_SINT:             return 3;
    case Vanilla::vaResourceFormat::R16G16B16A16_TYPELESS:      return 4;
    case Vanilla::vaResourceFormat::R16G16B16A16_FLOAT:         return 4;
    case Vanilla::vaResourceFormat::R16G16B16A16_UNORM:         return 4;
    case Vanilla::vaResourceFormat::R16G16B16A16_UINT:          return 4;
    case Vanilla::vaResourceFormat::R16G16B16A16_SNORM:         return 4;
    case Vanilla::vaResourceFormat::R16G16B16A16_SINT:          return 4;
    case Vanilla::vaResourceFormat::R32G32_TYPELESS:            return 2;
    case Vanilla::vaResourceFormat::R32G32_FLOAT:               return 2;
    case Vanilla::vaResourceFormat::R32G32_UINT:                return 2;
    case Vanilla::vaResourceFormat::R32G32_SINT:                return 2;
    case Vanilla::vaResourceFormat::R32G8X24_TYPELESS:          return 2;
    case Vanilla::vaResourceFormat::D32_FLOAT_S8X24_UINT:       return 2;
    case Vanilla::vaResourceFormat::R32_FLOAT_X8X24_TYPELESS:   return 1;
    case Vanilla::vaResourceFormat::X32_TYPELESS_G8X24_UINT:    return 1;
    case Vanilla::vaResourceFormat::R10G10B10A2_TYPELESS:       return 4;
    case Vanilla::vaResourceFormat::R10G10B10A2_UNORM:          return 4;
    case Vanilla::vaResourceFormat::R10G10B10A2_UINT:           return 4;
    case Vanilla::vaResourceFormat::R11G11B10_FLOAT:            return 3;
    case Vanilla::vaResourceFormat::R8G8B8A8_TYPELESS:          return 4;
    case Vanilla::vaResourceFormat::R8G8B8A8_UNORM:             return 4;
    case Vanilla::vaResourceFormat::R8G8B8A8_UNORM_SRGB:        return 4;
    case Vanilla::vaResourceFormat::R8G8B8A8_UINT:              return 4;
    case Vanilla::vaResourceFormat::R8G8B8A8_SNORM:             return 4;
    case Vanilla::vaResourceFormat::R8G8B8A8_SINT:              return 4;
    case Vanilla::vaResourceFormat::R16G16_TYPELESS:            return 2;
    case Vanilla::vaResourceFormat::R16G16_FLOAT:               return 2;
    case Vanilla::vaResourceFormat::R16G16_UNORM:               return 2;
    case Vanilla::vaResourceFormat::R16G16_UINT:                return 2;
    case Vanilla::vaResourceFormat::R16G16_SNORM:               return 2;
    case Vanilla::vaResourceFormat::R16G16_SINT:                return 2;
    case Vanilla::vaResourceFormat::R32_TYPELESS:               return 1;
    case Vanilla::vaResourceFormat::D32_FLOAT:                  return 1;
    case Vanilla::vaResourceFormat::R32_FLOAT:                  return 1;
    case Vanilla::vaResourceFormat::R32_UINT:                   return 1;
    case Vanilla::vaResourceFormat::R32_SINT:                   return 1;
    case Vanilla::vaResourceFormat::R24G8_TYPELESS:             return 2;
    case Vanilla::vaResourceFormat::D24_UNORM_S8_UINT:          return 2;
    case Vanilla::vaResourceFormat::R24_UNORM_X8_TYPELESS:      return 2;
    case Vanilla::vaResourceFormat::X24_TYPELESS_G8_UINT:       return 2;
    case Vanilla::vaResourceFormat::R8G8_TYPELESS:              return 2;
    case Vanilla::vaResourceFormat::R8G8_UNORM:                 return 2;
    case Vanilla::vaResourceFormat::R8G8_UINT:                  return 2;
    case Vanilla::vaResourceFormat::R8G8_SNORM:                 return 2;
    case Vanilla::vaResourceFormat::R8G8_SINT:                  return 2;
    case Vanilla::vaResourceFormat::R16_TYPELESS:               return 1;
    case Vanilla::vaResourceFormat::R16_FLOAT:                  return 1;
    case Vanilla::vaResourceFormat::D16_UNORM:                  return 1;
    case Vanilla::vaResourceFormat::R16_UNORM:                  return 1;
    case Vanilla::vaResourceFormat::R16_UINT:                   return 1;
    case Vanilla::vaResourceFormat::R16_SNORM:                  return 1;
    case Vanilla::vaResourceFormat::R16_SINT:                   return 1;
    case Vanilla::vaResourceFormat::R8_TYPELESS:                return 1;
    case Vanilla::vaResourceFormat::R8_UNORM:                   return 1;
    case Vanilla::vaResourceFormat::R8_UINT:                    return 1;
    case Vanilla::vaResourceFormat::R8_SNORM:                   return 1;
    case Vanilla::vaResourceFormat::R8_SINT:                    return 1;
    case Vanilla::vaResourceFormat::A8_UNORM:                   return 1;
    case Vanilla::vaResourceFormat::R1_UNORM:                   return 1;
    case Vanilla::vaResourceFormat::R9G9B9E5_SHAREDEXP:         assert( false ); return 3; // not sure
    case Vanilla::vaResourceFormat::R8G8_B8G8_UNORM:            assert( false ); return 3; // not sure
    case Vanilla::vaResourceFormat::G8R8_G8B8_UNORM:            assert( false ); return 3; // not sure
    case Vanilla::vaResourceFormat::BC1_TYPELESS:               return 4;
    case Vanilla::vaResourceFormat::BC1_UNORM:                  return 4;
    case Vanilla::vaResourceFormat::BC1_UNORM_SRGB:             return 4;
    case Vanilla::vaResourceFormat::BC2_TYPELESS:               return 4;
    case Vanilla::vaResourceFormat::BC2_UNORM:                  return 4;
    case Vanilla::vaResourceFormat::BC2_UNORM_SRGB:             return 4;
    case Vanilla::vaResourceFormat::BC3_TYPELESS:               return 4;
    case Vanilla::vaResourceFormat::BC3_UNORM:                  return 4;
    case Vanilla::vaResourceFormat::BC3_UNORM_SRGB:             return 4;
    case Vanilla::vaResourceFormat::BC4_TYPELESS:               return 1;
    case Vanilla::vaResourceFormat::BC4_UNORM:                  return 1;
    case Vanilla::vaResourceFormat::BC4_SNORM:                  return 1;
    case Vanilla::vaResourceFormat::BC5_TYPELESS:               return 2;
    case Vanilla::vaResourceFormat::BC5_UNORM:                  return 2;
    case Vanilla::vaResourceFormat::BC5_SNORM:                  return 2;
    case Vanilla::vaResourceFormat::B5G6R5_UNORM:               return 3;
    case Vanilla::vaResourceFormat::B5G5R5A1_UNORM:             return 4;
    case Vanilla::vaResourceFormat::B8G8R8A8_UNORM:             return 4;
    case Vanilla::vaResourceFormat::B8G8R8X8_UNORM:             return 4;
    case Vanilla::vaResourceFormat::R10G10B10_XR_BIAS_A2_UNORM: return 4;
    case Vanilla::vaResourceFormat::B8G8R8A8_TYPELESS:          return 4;
    case Vanilla::vaResourceFormat::B8G8R8A8_UNORM_SRGB:        return 4;
    case Vanilla::vaResourceFormat::B8G8R8X8_TYPELESS:          return 4;
    case Vanilla::vaResourceFormat::B8G8R8X8_UNORM_SRGB:        return 4;
    case Vanilla::vaResourceFormat::BC6H_TYPELESS:              return 3;
    case Vanilla::vaResourceFormat::BC6H_UF16:                  return 3;
    case Vanilla::vaResourceFormat::BC6H_SF16:                  return 3;
    case Vanilla::vaResourceFormat::BC7_TYPELESS:               return 4;
    case Vanilla::vaResourceFormat::BC7_UNORM:                  return 4;
    case Vanilla::vaResourceFormat::BC7_UNORM_SRGB:             return 4;
    case Vanilla::vaResourceFormat::AYUV:                       assert( false ); return 3; // not sure
    case Vanilla::vaResourceFormat::Y410:                       assert( false ); return 3; // not sure
    case Vanilla::vaResourceFormat::Y416:                       assert( false ); return 3; // not sure
    case Vanilla::vaResourceFormat::NV12:                       assert( false ); return 3; // not sure
    case Vanilla::vaResourceFormat::P010:                       assert( false ); return 3; // not sure
    case Vanilla::vaResourceFormat::P016:                       assert( false ); return 3; // not sure
    case Vanilla::vaResourceFormat::F420_OPAQUE:                assert( false ); return 3; // not sure
    case Vanilla::vaResourceFormat::YUY2:                       assert( false ); return 3; // not sure
    case Vanilla::vaResourceFormat::Y210:                       assert( false ); return 3; // not sure
    case Vanilla::vaResourceFormat::Y216:                       assert( false ); return 3; // not sure
    case Vanilla::vaResourceFormat::NV11:                       assert( false ); return 3; // not sure
    case Vanilla::vaResourceFormat::AI44:                       assert( false ); return 3; // not sure
    case Vanilla::vaResourceFormat::IA44:                       assert( false ); return 3; // not sure
    case Vanilla::vaResourceFormat::P8:                         return 1;
    case Vanilla::vaResourceFormat::A8P8:                       return 2;
    case Vanilla::vaResourceFormat::B4G4R4A4_UNORM:             return 4;
    default: break;
    }
    assert( false );
    return 0;
}
