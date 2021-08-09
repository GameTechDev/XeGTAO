///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaRenderMaterial.h"
#include "Rendering/vaRenderMesh.h"

#include "Core/System/vaFileTools.h"

#include "Rendering/vaStandardShapes.h"
#include "Rendering/vaRenderDevice.h"
#include "Rendering/vaRenderDeviceContext.h"
#include "Rendering/vaAssetPack.h"
#include "Core/vaXMLSerialization.h"

#include "IntegratedExternals/vaImguiIntegration.h"

#include "Rendering/vaTextureHelpers.h"

#include "Core/vaUI.h"

using namespace Vanilla;

//vaRenderMeshManager & renderMeshManager, const vaGUID & uid

const int c_renderMeshMaterialFileVersion = 3;
static const int c_materialItemNameMaxLength = 24;

static const float c_materialLowestSaneFloat    = -100000.0f;   // not using std::numeric_limits<float>::lowest( ) in order to reduce UI clutter
static const float c_materialMaxSaneFloat       = 100000.0f;    // not using std::numeric_limits<float>::max( ) in order to reduce UI clutter

static bool Serialize( vaXMLSerializer & serializer, const string & name, vaRenderMaterial::ValueType & value )
{
    vaSerializerScopedOpenChild nameScope( serializer, name, false );

    if( !nameScope.IsOK( ) )
        { return false; }

    int typeIndex = (serializer.IsWriting())?((int32)value.index()):(-1);
    if( !serializer.Serialize<int32>( "type", typeIndex ) )
        { assert( false ); return false; }

    if( serializer.IsReading( ) )
    {
        bool wasOk = false;
        switch( typeIndex )
        {
        case( (int32)vaRenderMaterial::ValueTypeIndex::Bool ):       { bool val;      wasOk = serializer.Serialize( name, val ); if( wasOk ) value = val; } break;
        case( (int32)vaRenderMaterial::ValueTypeIndex::Integer ):    { int32 val;     wasOk = serializer.Serialize( name, val ); if( wasOk ) value = val; } break;
        case( (int32)vaRenderMaterial::ValueTypeIndex::Scalar ):     { float val;     wasOk = serializer.Serialize( name, val ); if( wasOk ) value = val; } break;
        case( (int32)vaRenderMaterial::ValueTypeIndex::Vector3 ):    { vaVector3 val; wasOk = serializer.Serialize( name, val ); if( wasOk ) value = val; } break;
        case( (int32)vaRenderMaterial::ValueTypeIndex::Vector4 ):    { vaVector4 val; wasOk = serializer.Serialize( name, val ); if( wasOk ) value = val; } break;
        default: assert( false ); break;
        }
        return wasOk;
    }
    else if( serializer.IsWriting( ) )
    {
        switch( value.index( ) )
        {
        case( (size_t)vaRenderMaterial::ValueTypeIndex::Bool ):       return serializer.Serialize( name, std::get<bool>( value ) );         break;
        case( (size_t)vaRenderMaterial::ValueTypeIndex::Integer ):    return serializer.Serialize( name, std::get<int32>( value ) );        break;
        case( (size_t)vaRenderMaterial::ValueTypeIndex::Scalar ):     return serializer.Serialize( name, std::get<float>( value ) );        break;
        case( (size_t)vaRenderMaterial::ValueTypeIndex::Vector3 ):    return serializer.Serialize( name, std::get<vaVector3>( value ) );    break;
        case( (size_t)vaRenderMaterial::ValueTypeIndex::Vector4 ):    return serializer.Serialize( name, std::get<vaVector4>( value ) );    break;
        default: assert( false ); return false; break;
        }
    }
    else
        { assert( false ); return false; }
}

static void GetDefaultMinMax( vaRenderMaterial::ValueTypeIndex type, vaRenderMaterial::ValueType & outMin, vaRenderMaterial::ValueType & outMax )
{
    switch( type )
    {
    case( vaRenderMaterial::ValueTypeIndex::Bool ):       outMin = false; outMax = true; break;
    case( vaRenderMaterial::ValueTypeIndex::Integer ):    outMin = std::numeric_limits<int>::lowest( ); outMax = std::numeric_limits<int>::max( ); break;
    case( vaRenderMaterial::ValueTypeIndex::Scalar ):     outMin = c_materialLowestSaneFloat; outMax = c_materialMaxSaneFloat; break;
    case( vaRenderMaterial::ValueTypeIndex::Vector3 ):    outMin = vaVector3( c_materialLowestSaneFloat, c_materialLowestSaneFloat, c_materialLowestSaneFloat ); outMax = vaVector3( c_materialMaxSaneFloat, c_materialMaxSaneFloat, c_materialMaxSaneFloat ); break;
    case( vaRenderMaterial::ValueTypeIndex::Vector4 ):    outMin = vaVector4( c_materialLowestSaneFloat, c_materialLowestSaneFloat, c_materialLowestSaneFloat, c_materialLowestSaneFloat ); outMax = vaVector4( c_materialMaxSaneFloat, c_materialMaxSaneFloat, c_materialMaxSaneFloat, c_materialMaxSaneFloat ); break;
    default: assert( false ); break;
    }
}

static void UploadToConstants( const vaRenderMaterial::ValueType & value, vaVector4 & destination )
{
    switch( (vaRenderMaterial::ValueTypeIndex)value.index() )
    {
    case( vaRenderMaterial::ValueTypeIndex::Bool ):       destination = { std::get<bool>( value )?(1.0f):(0.0f), 0, 0, 0 };  break;       // not sure if we want reinterpret cast here - storing 1 in float is actually 0x3f800000
    case( vaRenderMaterial::ValueTypeIndex::Integer ):    destination = { reinterpret_cast<const float &>( std::get<int32>( value ) ), 0, 0, 0 };  break;
    case( vaRenderMaterial::ValueTypeIndex::Scalar ):     destination = { std::get<float>( value ), 0, 0, 0 };          break;
    case( vaRenderMaterial::ValueTypeIndex::Vector3 ):    destination = vaVector4( std::get<vaVector3>( value ), 0 );   break;
    case( vaRenderMaterial::ValueTypeIndex::Vector4 ):    destination = std::get<vaVector4>( value );                   break;
    default: assert( false ); break;
    };
}

static string ValueTypeIndexToHLSL( vaRenderMaterial::ValueTypeIndex type )
{
    switch( type )
    {
    case( vaRenderMaterial::ValueTypeIndex::Bool ):       return "bool";    break;
    case( vaRenderMaterial::ValueTypeIndex::Integer ):    return "int";     break;
    case( vaRenderMaterial::ValueTypeIndex::Scalar ):     return "float";   break;
    case( vaRenderMaterial::ValueTypeIndex::Vector3 ):    return "float3";  break;
    case( vaRenderMaterial::ValueTypeIndex::Vector4 ):    return "float4";  break;
    default: assert( false ); return ""; break;
    };
}

static int ValueTypeIndexGetComponentCount( vaRenderMaterial::ValueTypeIndex type )
{
    switch( type )
    {
    case( vaRenderMaterial::ValueTypeIndex::Bool ):       return 1;  break;
    case( vaRenderMaterial::ValueTypeIndex::Integer ):    return 1;  break;
    case( vaRenderMaterial::ValueTypeIndex::Scalar ):     return 1;  break;
    case( vaRenderMaterial::ValueTypeIndex::Vector3 ):    return 3;  break;
    case( vaRenderMaterial::ValueTypeIndex::Vector4 ):    return 4;  break;
    default: assert( false ); return 0; break;
    };
}

static string ValueTypeToHLSL( const vaRenderMaterial::ValueType & value, int constantsSlot )
{
#ifdef VA_MATERIAL_FAVOR_FEWER_PERMUTATONS
    string slot = vaStringTools::Format( "materialConstants.Constants[%d]", constantsSlot ); // <- this can be easily optimized
    switch( (vaRenderMaterial::ValueTypeIndex)value.index() )
    {
    case( vaRenderMaterial::ValueTypeIndex::Bool ):       { return "bool("+slot+".x)";        } break;
    case( vaRenderMaterial::ValueTypeIndex::Integer ):    { return "asint("+slot+".x)";       } break;
    case( vaRenderMaterial::ValueTypeIndex::Scalar ):     { return "float("+slot+".x)";                 } break;
    case( vaRenderMaterial::ValueTypeIndex::Vector3 ):    { return "float3("+slot+".xyz)";               } break;
    case( vaRenderMaterial::ValueTypeIndex::Vector4 ):    { return "float4("+slot+".xyzw)";              } break;
    default: assert( false ); return ""; break;
    };
#else
    constantsSlot;
    switch( (vaRenderMaterial::ValueTypeIndex)value.index() )
    {
    case( vaRenderMaterial::ValueTypeIndex::Bool ):       { bool val      = std::get<bool>(value);        return val?"bool(true)" : "bool(false)";                                              } break;
    case( vaRenderMaterial::ValueTypeIndex::Integer ):    { int32 val     = std::get<int32>(value);       return vaStringTools::Format( "int(%d)", val );                                       } break;
    case( vaRenderMaterial::ValueTypeIndex::Scalar ):     { float val     = std::get<float>(value);       return vaStringTools::Format( "float(%f)", val );                                     } break;
    case( vaRenderMaterial::ValueTypeIndex::Vector3 ):    { vaVector3 val = std::get<vaVector3>(value);   return vaStringTools::Format( "float3(%f,%f,%f)", val.x, val.y, val.z );              } break;
    case( vaRenderMaterial::ValueTypeIndex::Vector4 ):    { vaVector4 val = std::get<vaVector4>(value);   return vaStringTools::Format( "float4(%f,%f,%f,%f)", val.x, val.y, val.z, val.w );    } break;
    default: assert( false ); return ""; break;
    };
#endif
}

static string Vector3ToString( const vaVector3 & val, int floatPrecisionDecimals )
{
    string fltFormat = "%." + vaStringTools::Format( "%d", floatPrecisionDecimals ) + "f";
    return vaStringTools::Format( (fltFormat+","+fltFormat+","+fltFormat).c_str(), val.x, val.y, val.z );
}

static string Vector4ToString( const vaVector4& val, int floatPrecisionDecimals )
{
    string fltFormat = "%." + vaStringTools::Format( "%d", floatPrecisionDecimals ) + "f";
    return vaStringTools::Format( ( fltFormat + "," + fltFormat + "," + fltFormat + "," + fltFormat ).c_str( ), val.x, val.y, val.z, val.w );
}

static string ValueTypeToString( const vaRenderMaterial::ValueType & value, int floatPrecisionDecimals )
{
    string fltFormat = "%." + vaStringTools::Format( "%d", floatPrecisionDecimals ) + "f";
    switch( (vaRenderMaterial::ValueTypeIndex)value.index() )
    {
    case( vaRenderMaterial::ValueTypeIndex::Bool ):       { bool val      = std::get<bool>(value);        return val?"true" : "false";                              } break;
    case( vaRenderMaterial::ValueTypeIndex::Integer ):    { int32 val     = std::get<int32>(value);       return vaStringTools::Format( "%d", val );                } break;
    case( vaRenderMaterial::ValueTypeIndex::Scalar ):     { float val     = std::get<float>(value);       return vaStringTools::Format( fltFormat.c_str(), val );   } break;
    case( vaRenderMaterial::ValueTypeIndex::Vector3 ):    { vaVector3 val = std::get<vaVector3>(value);   return Vector3ToString( val, floatPrecisionDecimals );    } break;
    case( vaRenderMaterial::ValueTypeIndex::Vector4 ):    { vaVector4 val = std::get<vaVector4>(value);   return Vector4ToString( val, floatPrecisionDecimals );    } break;
    default: assert( false ); return ""; break;
    };
}

static string TextureSlotToHLSLVariableName( int slotIndex )
{
#ifndef VA_MATERIAL_BINDLESS
    return vaStringTools::Format( "g_RMTexture%02d", slotIndex );
#else
    //return vaStringTools::Format( "g_bindlessTex2D[g_BindlessSRVIndices[%02d]]", slotIndex );
    return vaStringTools::Format( "g_BindlessSRVIndices[%02d]", slotIndex );
#endif
}

static bool SanitizeSwizzle( char inoutSwizzle[], vaRenderMaterial::ValueTypeIndex dstType, vaRenderMaterial::ValueTypeIndex srcType = vaRenderMaterial::ValueTypeIndex::Vector4 )
{
    bool hasChanged = false;
    int dstCompCount = ValueTypeIndexGetComponentCount( dstType );
    int srcCompCount = ValueTypeIndexGetComponentCount( srcType );
    
    for( int i = 0; i < dstCompCount; i++ )
    {
        int j = (inoutSwizzle[i]=='x')?(0):( (inoutSwizzle[i]=='y')?(1):( (inoutSwizzle[i]=='z')?(2):( (inoutSwizzle[i]=='w')?(3):( 4 ) ) ) );
        if( j >= srcCompCount )
        {
            hasChanged = true;
            inoutSwizzle[i] = 'x';
        }
    }
    for( int i = dstCompCount; i < 4; i++ )
    {
        if( inoutSwizzle[i] != '\0' )
        {
            hasChanged = true;
            inoutSwizzle[i] = '\0';
        }
    }

    inoutSwizzle[4] = '\0';

    return hasChanged;
}

static string SwizzleToString( char swizzle[5] )
{
    string ret = "";
    for( int i = 0; i < 4; i++ )
    {
        if( swizzle[i] == '\0' )
            return ret;
        ret += swizzle[i];
    }
    return ret;
}

static void StringToSwizzle( char outSwizzle[], const string & inString )
{
    for( int i = 0; i < 4; i++ )
    {
        if( i >= inString.length() )
            outSwizzle[i] = '\0';
        else
            outSwizzle[i] = inString[i];
    }
    outSwizzle[4] = '\0';
}

static string SanitizeInputSlotOrNodeName( const string& name )
{
    string newValueName = name; const int maxLength = c_materialItemNameMaxLength;
    //assert( newValueName.length( ) > 0 && newValueName.length( ) <= maxLength );    // limited for UI and search/serialization reasons
    if( newValueName.length( ) == 0 )        newValueName = "unnamed";
    if( newValueName.length( ) > maxLength ) newValueName = newValueName.substr( 0, maxLength );
    for( int i = 0; i < newValueName.length( ) - 1; i++ )
    {
        if( !( ( newValueName[i] >= '0' && newValueName[i] <= '9' ) || ( newValueName[i] >= 'A' && newValueName[i] <= 'z' ) || ( newValueName[i] == '_' ) || ( newValueName[i] == 0 ) ) )
        {
            // invalid character used, will be replaced with _
            assert( false );
            newValueName[i] = '_';
        }
    }
    return newValueName;
}



#pragma warning ( suppress: 4505 ) // unreferenced local function has been removed
static bool IsValidSwizzle( const string& swizzleString )
{
    if( swizzleString.length() == 0 || swizzleString.length() > 4 )
        return false;
    for( size_t i = 0; i < swizzleString.length(); i++ )
        if( swizzleString[i] != 'x' && swizzleString[i] != 'y' && swizzleString[i] != 'z' && swizzleString[i] != 'w' )
            return false;
    return true;
}

vaRenderMaterial::vaRenderMaterial( const vaRenderingModuleParams & params ) : vaRenderingModule( params ),
    vaAssetResource( vaSaferStaticCast< const vaRenderMaterialConstructorParams &, const vaRenderingModuleParams &>( params ).UID), 
    //m_trackee( vaSaferStaticCast< const vaRenderMaterialConstructorParams &, const vaRenderingModuleParams &>( params ).RenderMaterialManager.GetRenderMaterialTracker( ), this ), 
    m_renderMaterialManager( vaSaferStaticCast< const vaRenderMaterialConstructorParams &, const vaRenderingModuleParams &>( params ).RenderMaterialManager )
    //,    m_constantsBuffer( params )
{
    m_shaderMacros.reserve( 16 );
    m_shaderMacrosDirty = true;
    m_shadersDirty = true;

    {
        std::shared_lock managerLock( m_renderMaterialManager.Mutex() );
        m_globalIndex = m_renderMaterialManager.Materials().Insert( this );
    }

    m_currentShaderConstants.Invalidate();
}

vaRenderMaterial::~vaRenderMaterial()
{
    {
        std::shared_lock managerLock( m_renderMaterialManager.Mutex() );
        m_renderMaterialManager.Materials().Remove( m_globalIndex );
    }
}

// TODO: convert all of these to enums at some point!

// obsolete, removing
// static const char * c_Legacy                = "Legacy";
// static const char * c_Unlit                 = "Unlit";

// these new ones will roughly match Filament materials (see https://google.github.io/filament/Materials.html#overview/coreconcepts)
static const char * c_FilamentStandard      = "FilamentStandard";
static const char * c_FilamentSubsurface    = "FilamentSubsurface";
static const char * c_FilamentCloth         = "FilamentCloth";
static const char * c_FilamentUnlit         = "FilamentUnlit";
static const char * c_FilamentSpecGloss     = "FilamentSpecGloss";

std::vector<string> vaRenderMaterial::GetPresetMaterials( )
{
    return { /*c_Legacy, c_Unlit,*/ c_FilamentStandard, c_FilamentSubsurface, c_FilamentCloth, c_FilamentUnlit, c_FilamentSpecGloss  };
}

bool vaRenderMaterial::SetupFromPreset( const string& presetName, bool removeNodes )
{
    m_shaderSettings.BaseMacros.clear();
    m_computedTextureSlotCount      = 0;
    m_computedConstantsSlotCount    = 0;
    m_currentShaderConstants.Invalidate();
    
    m_shaderSettings.VS_Standard        = std::make_pair( "vaRenderMesh.hlsl", "VS_Standard"     );
    m_shaderSettings.GS_Standard        = { "", "" };

    m_shaderSettings.PS_DepthOnly       = std::make_pair( "vaRenderMaterial.hlsl", "PS_DepthOnly"    );
    m_shaderSettings.PS_Forward         = std::make_pair( "vaRenderMaterial.hlsl", "PS_Forward"      );
    //m_shaderSettings.PS_Deferred        = { "", "" }; // std::make_pair( "vaRenderMaterial.hlsl", "PS_Deferred"     );
    m_shaderSettings.PS_RichPrepass    = std::make_pair( "vaRenderMaterial.hlsl", "PS_RichPrepass" );

    m_shaderSettings.CAL_LibraryFile    = "vaRenderMaterial.hlsl";

    if( removeNodes )
        RemoveAllNodes( );

    RemoveAllInputSlots( );

    m_shaderMacros.clear();
    m_shaderMacrosDirty = true;

    bool retVal = false;

    if( vaStringTools::CompareNoCase( presetName, c_FilamentStandard ) == 0 )
    {
        // see https://google.github.io/filament/Material%20Properties.pdf
        // also see material_inputs.va.fs

        SetInputSlot( "BaseColor",          vaVector4( 1.0f, 1.0f, 1.0f, 1.0f ),    true, true    );
        //SetInputSlot( "Opacity",            1.0f,                                   false   );    // handled by BaseColor alpha - could be separate though, would probably be cleaner
        SetInputSlot( "Normal",             vaVector3( 0.0f, 0.0f, 1.0f ),          false, false   );
        SetInputSlot( "EmissiveColor",      vaVector3( 1.0f, 1.0f, 1.0f ),          true, true    );
        SetInputSlot( "EmissiveIntensity",  0.0f,                                   false, false   );
        SetInputSlot( "Roughness",          1.0f,                                   false, false   );
        SetInputSlot( "Metallic",           0.0f,                                   false, false   );
        SetInputSlot( "Reflectance",        0.35f,                                  false, false   );
        SetInputSlot( "AmbientOcclusion",   1.0f,                                   true, false   );

        // not yet implemented:
        /*
        float   ClearCoat;                  // Strength of the clear coat layer on top of a base dielectric or conductor layer. The clear coat layer will commonly be set to 0.0 or 1.0. This layer has a fixed index of refraction of 1.5.
        float   ClearCoatRoughness;         // Defines the perceived smoothness (0.0) or roughness (1.0) of the clear coat layer. It is sometimes called glossiness. This may affect the roughness of the base layer
        float   Anisotropy;                 // Defines whether the material appearance is directionally dependent, that is isotropic (0.0) or anisotropic (1.0). Brushed metals are anisotropic. Values can be negative to change the orientation of the specular reflections.
        float3  AnisotropyDirection;        // The anisotropyDirection property defines the direction of the surface at a given point and thus control the shape of the specular highlights. It is specified as vector of 3 values that usually come from a texture, encoding the directions local to the surface.
#if defined(MATERIAL_HAS_CLEAR_COAT) //&& defined(MATERIAL_HAS_CLEAR_COAT_NORMAL)
        float3  ClearCoatNormal;            // Same as Normal except additionally defining a different normal for the clear coat!
#endif
        */

        m_shaderSettings.BaseMacros.push_back( { "VA_FILAMENT_STANDARD", "" } );
        m_shaderSettings.PS_RichPrepass    = std::make_pair( "vaRenderMaterial.hlsl", "PS_RichPrepass" );
        m_shaderSettings.PS_DepthOnly       = std::make_pair( "vaRenderMaterial.hlsl", "PS_DepthOnly" );
        m_shaderSettings.PS_Forward         = std::make_pair( "vaRenderMaterial.hlsl", "PS_Forward" );
        
        retVal = true;
    }
    else if( vaStringTools::CompareNoCase( presetName, c_FilamentSubsurface ) == 0 )
    {
        // see https://google.github.io/filament/Material%20Properties.pdf

        m_shaderSettings.BaseMacros.push_back( { "VA_FILAMENT_SUBSURFACE", "" } );
        m_shaderSettings.PS_RichPrepass    = std::make_pair( "vaRenderMaterial.hlsl", "PS_RichPrepass" );
        m_shaderSettings.PS_DepthOnly       = std::make_pair( "vaRenderMaterial.hlsl", "PS_DepthOnly" );
        m_shaderSettings.PS_Forward         = std::make_pair( "vaRenderMaterial.hlsl", "PS_Forward" );

        retVal = true;
    }
    else if( vaStringTools::CompareNoCase( presetName, c_FilamentCloth ) == 0 )
    {
        // see https://google.github.io/filament/Material%20Properties.pdf

        m_shaderSettings.BaseMacros.push_back( { "VA_FILAMENT_CLOTH", "" } );
        m_shaderSettings.PS_RichPrepass    = std::make_pair( "vaRenderMaterial.hlsl", "PS_RichPrepass" );
        m_shaderSettings.PS_DepthOnly       = std::make_pair( "vaRenderMaterial.hlsl", "PS_DepthOnly" );
        m_shaderSettings.PS_Forward         = std::make_pair( "vaRenderMaterial.hlsl", "PS_Forward" );

        retVal = true;
    }
    else if( vaStringTools::CompareNoCase( presetName, c_FilamentUnlit) == 0 )
    {
        // see https://google.github.io/filament/Material%20Properties.pdf

        m_shaderSettings.BaseMacros.push_back( { "VA_FILAMENT_UNLIT", "" } );
        m_shaderSettings.PS_RichPrepass    = std::make_pair( "vaRenderMaterial.hlsl", "PS_RichPrepass" );
        m_shaderSettings.PS_DepthOnly       = std::make_pair( "vaRenderMaterial.hlsl", "PS_DepthOnly" );
        m_shaderSettings.PS_Forward         = std::make_pair( "vaRenderMaterial.hlsl", "PS_Forward" );

        retVal = true;
    }
    else if( vaStringTools::CompareNoCase( presetName, c_FilamentSpecGloss ) == 0 )
    {
        // see https://google.github.io/filament/Material%20Properties.pdf

        m_shaderSettings.BaseMacros.push_back( { "VA_FILAMENT_SPECGLOSS", "" } );

        SetInputSlot( "BaseColor",          vaVector4( 1.0f, 1.0f, 1.0f, 1.0f ),    true, true    );
        //SetInputSlot( "Opacity",            1.0f,                                   false   );    // handled by BaseColor alpha - could be separate though, would probably be cleaner
        SetInputSlot( "Normal",             vaVector3( 0.0f, 0.0f, 1.0f ),          false, false   );
        SetInputSlot( "EmissiveColor",      vaVector3( 0.0f, 0.0f, 0.0f ),          true, true    );
        SetInputSlot( "EmissiveIntensity",  1.0f,                                   false, false   );
        // SetInputSlot( "Roughness",          1.0f,                                   false   );   // these don't exist in specular/glosiness model
        // SetInputSlot( "Metallic",           0.0f,                                   false   );   // these don't exist in specular/glosiness model
        // SetInputSlot( "Reflectance",        0.35f,                                   false   );   // these don't exist in specular/glosiness model
        SetInputSlot( "AmbientOcclusion",   1.0f,                                   true, false   );

        SetInputSlot( "SpecularColor",      vaVector3( 0.0f, 0.0f, 0.0f ),          true, true    );
        SetInputSlot( "Glossiness",         0.0f,                                   false, false   );

        m_shaderSettings.PS_RichPrepass    = std::make_pair( "vaRenderMaterial.hlsl", "PS_RichPrepass" );
        m_shaderSettings.PS_DepthOnly       = std::make_pair( "vaRenderMaterial.hlsl", "PS_DepthOnly" );
        m_shaderSettings.PS_Forward         = std::make_pair( "vaRenderMaterial.hlsl", "PS_Forward" );

        retVal = true;
    } 


    assert( retVal );    // preset not recognized, material no longer correct
    return retVal;
}

bool vaRenderMaterial::SetupFromOther( const vaRenderMaterial & other )
{
    SetShaderSettings( other.GetShaderSettings() );
    SetMaterialSettings( other.GetMaterialSettings() );

    m_computedTextureSlotCount      = 0;
    m_computedConstantsSlotCount    = 0;
    m_currentShaderConstants.Invalidate();

    RemoveAllInputSlots();
    RemoveAllNodes();
    m_inputSlots = other.GetInputSlots();
    const std::vector<shared_ptr<Node>> & otherNodes = other.GetNodes();
    for( int i = 0; i < otherNodes.size(); i++ )
    {
        shared_ptr<TextureNode> textureNode = std::dynamic_pointer_cast<TextureNode, Node>( otherNodes[i] );
        if( textureNode != nullptr )
        {
            auto copy = std::make_shared<TextureNode>(*textureNode);
            m_nodes.push_back( std::static_pointer_cast<Node, TextureNode>( copy ) );
        }
        else
        {
            assert( false );
        }
    }
    assert( m_inputSlots.size() <= RENDERMATERIAL_MAX_INPUT_SLOTS );
    assert( m_nodes.size() <= RENDERMATERIAL_MAX_NODES );

    return true;
}

void vaRenderMaterial::UpdateShaderMacros( )
{
    UpdateInputsDependencies( );

    if( !m_shaderMacrosDirty )
        return;

    assert( m_inputSlots.size() <= RENDERMATERIAL_MAX_INPUT_SLOTS );
    assert( m_nodes.size() <= RENDERMATERIAL_MAX_NODES );

    std::vector< pair< string, string > > prevShaderMacros = m_shaderMacros;

    m_shaderMacros.clear();

    // add global macros first (usually empty)
    m_shaderMacros = m_renderMaterialManager.GetGlobalShaderMacros( );
    
    // start from base macros and add up from there

    // this can be useful if shader wants to know whether it's included from the vaRenderMaterial code
    m_shaderMacros.insert( m_shaderMacros.begin(), std::pair<string, string>( "VA_RENDER_MATERIAL", "1" ) );

    // bool exportNormals = m_renderMaterialManager.GetGlobalDepthPrepassExportsNormals( );
    // m_shaderMacros.push_back( std::pair<string, string>( "VA_RM_DEPTH_PASS_EXPORTS_NORMALS",( ( exportNormals )   ? ( "1" ) : ( "0" ) ) ) );

    m_shaderMacros.push_back( std::pair<string, string>( "VA_RM_TRANSPARENT",               ( ( IsTransparent() ) ? ( "1" ) : ( "0" ) ) ) );
    m_shaderMacros.push_back( std::pair<string, string>( "VA_RM_DECAL",                     ( ( IsDecal()       ) ? ( "1" ) : ( "0" ) ) ) );
    m_shaderMacros.push_back( std::pair<string, string>( "VA_RM_ALPHATEST",                 ( ( IsAlphaTested() ) ? ( "1" ) : ( "0" ) ) ) );
    //m_shaderMacros.push_back( std::pair<string, string>( "VA_RM_ALPHATEST_THRESHOLD",       vaStringTools::Format( "(%.3f)", m_materialSettings.AlphaTestThreshold ) ) );
    m_shaderMacros.push_back( std::pair<string, string>( "VA_RM_ACCEPTSHADOWS",             ( ( m_materialSettings.ReceiveShadows    ) ? ( "1" ) : ( "0" ) ) ) );
    m_shaderMacros.push_back( std::pair<string, string>( "VA_RM_WIREFRAME",                 ( ( m_materialSettings.Wireframe         ) ? ( "1" ) : ( "0" ) ) ) );
    m_shaderMacros.push_back( std::pair<string, string>( "VA_RM_ADVANCED_SPECULAR_SHADER",  ( ( m_materialSettings.AdvancedSpecularShader ) ? ( "1" ) : ( "0" ) ) ) );
    m_shaderMacros.push_back( std::pair<string, string>( "VA_RM_SPECIAL_EMISSIVE_LIGHT",    ( ( m_materialSettings.SpecialEmissiveLight ) ? ( "1" ) : ( "0" ) ) ) );

    //if( m_materialSettings.LocalIBLNormalBasedBias != 0 )
    //    m_shaderMacros.push_back( std::pair<string, string>( "VA_RM_LOCALIBL_NORMALBIAS",       vaStringTools::Format( "(%.3f)", m_materialSettings.LocalIBLNormalBasedBias ) ) );
    //if( m_materialSettings.LocalIBLBasedBias != 0 )
    //    m_shaderMacros.push_back( std::pair<string, string>( "VA_RM_LOCALIBL_BIAS", vaStringTools::Format( "(%.3f)", m_materialSettings.LocalIBLBasedBias ) ) );

    // texture declarations
#ifndef VA_MATERIAL_BINDLESS
    {
        string textureDeclarations = "";

        for( int slotIndex = 0; slotIndex < m_computedTextureSlotCount; slotIndex++ )
        { 
            string thisTextureDeclaration;
            
            // type
            //thisTextureDeclaration += vaStringTools::Format( "Texture2D<%s>   ", typeName.c_str( ) );
            thisTextureDeclaration += "Texture2D   ";

            // global name
            thisTextureDeclaration += TextureSlotToHLSLVariableName( slotIndex );

            // texture register declaration
            thisTextureDeclaration += vaStringTools::Format( " : register( t%d );", slotIndex );

            textureDeclarations += thisTextureDeclaration;
        }

        m_shaderMacros.push_back( std::pair<string, string>( "VA_RM_TEXTURE_DECLARATIONS", textureDeclarations ) );
    }
#else
    // no declarations for bindless! (yet - waiting for SM6.6)
    m_shaderMacros.push_back( std::pair<string, string>( "VA_RM_TEXTURE_DECLARATIONS", "" ) );
#endif

    // inputs declarations
    {
        string inputsDeclarations;
        for( int i = 0; i < m_inputSlots.size( ); i++ )
        {
            const InputSlot & inputSlot = m_inputSlots[i];

            inputsDeclarations.append( inputSlot.GetShaderMaterialInputsType() + " " + inputSlot.GetName() + "; " );

            m_shaderMacros.push_back( { "VA_RM_HAS_INPUT_"+inputSlot.Name, "1" } );
        }
        m_shaderMacros.push_back( std::pair<string, string>( "VA_RM_INPUTS_DECLARATIONS", inputsDeclarations ) );
    }

    // nodes declarations (+ loading from variable and/or textures)
    {
        string nodesDeclarations;
        for( int i = 0; i < m_nodes.size( ); i++ )
        {
            const shared_ptr<Node> & node = m_nodes[i];

            if( !node->InUse )
                continue;

            assert( !m_renderMaterialManager.GetTexturingDisabled( ) );

            nodesDeclarations.append( node->GetShaderMaterialInputsType( ) + " " + node->Name + " = " );
            
            nodesDeclarations.append( node->GetShaderMaterialInputLoader( ) + "; " );
        }
        m_shaderMacros.push_back( std::pair<string, string>( "VA_RM_NODES_DECLARATIONS", nodesDeclarations ) );
    }

    // load inputs from nodes
    {
        string inputsLoading;
        for( int i = 0; i < m_inputSlots.size( ); i++ )
        {
            const InputSlot& inputSlot = m_inputSlots[i];

            assert( inputSlot.ComputedShaderConstantsSlot >= 0 );

            shared_ptr<const Node> node = inputSlot.CachedConnectedInput.lock();
            if( node == nullptr || !node->InUse )
            {
                if( node == nullptr )
                    assert( inputSlot.ConnectedInput == "" );

                // if we're falling back to default, we have to consider scenarios where default is a single float but input node was picking a single channel from .y or .z or .w so we need to re-sanitize
                char inputSwizzle[5] = { inputSlot.InputSwizzle[0], inputSlot.InputSwizzle[1], inputSlot.InputSwizzle[2], inputSlot.InputSwizzle[3], inputSlot.InputSwizzle[4] };   // today's winner in "I'm ashamed of my code"
                SanitizeSwizzle( inputSwizzle, inputSlot.GetType(), inputSlot.Properties.GetType() );

                inputsLoading.append( "inputs." + inputSlot.GetName( ) + " = " + ValueTypeToHLSL(inputSlot.Properties.Default, inputSlot.ComputedShaderConstantsSlot) + "." + string(inputSwizzle) + "; " );
            }
            else
            {
                assert( inputSlot.ConnectedInput != "" );
                //std::array<char *, 5> comps = { "", ".x", ".xy", ".xyz", ".xyzw" };
                if( inputSlot.Properties.IsMultiplier )
                    inputsLoading.append( "inputs." + inputSlot.GetName() + " = (" +  ValueTypeToHLSL(inputSlot.Properties.Default, inputSlot.ComputedShaderConstantsSlot) + "*" + node->Name + "." + string(inputSlot.InputSwizzle) + "); " );
                else
                    inputsLoading.append( "inputs." + inputSlot.GetName() + " = " + node->Name + "." + string(inputSlot.InputSwizzle) + "; " );
            }


        }
        m_shaderMacros.push_back( std::pair<string, string>( "VA_RM_INPUTS_LOADING", inputsLoading ) );
    }
    
    m_shaderMacros.insert( m_shaderMacros.end( ), m_shaderSettings.BaseMacros.begin( ), m_shaderSettings.BaseMacros.end( ) );

    m_shaderMacrosDirty = false;
    m_shadersDirty = prevShaderMacros != m_shaderMacros;
}

void vaRenderMaterial::RemoveAllNodes( )
{
    assert( !m_immutable );
    m_nodes.clear();
    m_inputsDirty = true;
}

void vaRenderMaterial::RemoveAllInputSlots( )
{
    assert( !m_immutable );
    m_inputSlots.clear( );
    m_inputsDirty = true;
}

bool vaRenderMaterial::SaveAPACK( vaStream & outStream )
{
    // Just using SerializeUnpacked to implement this - not a lot of binary data; can be upgraded later
    vaXMLSerializer materialSerializer;
    GetManager( ).RegisterSerializationTypeConstructors( materialSerializer );
    bool allOk;
    {
        vaSerializerScopedOpenChild rootNode( materialSerializer, "Material" );
        allOk = rootNode.IsOK( );
        if( allOk )
            allOk = SerializeUnpacked( materialSerializer, "%there-is-no-folder@" );
    }

    const char * buffer = materialSerializer.GetWritePrinter( ).CStr( );
    int64 bufferSize = materialSerializer.GetWritePrinter( ).CStrSize( );

    VERIFY_TRUE_RETURN_ON_FALSE( outStream.WriteValue<int64>( bufferSize ) );
    VERIFY_TRUE_RETURN_ON_FALSE( outStream.Write( buffer, bufferSize ) );
    return allOk;
}

bool vaRenderMaterial::LoadAPACK( vaStream & inStream )
{
    assert( !m_immutable ); 
    // Just using SerializeUnpacked to implement this - not a lot of binary data; can be upgraded later
    int64 bufferSize = 0;
    VERIFY_TRUE_RETURN_ON_FALSE( inStream.ReadValue<int64>( bufferSize ) );

    const char * buffer = new char[bufferSize];
    if( !inStream.Read( (void*)buffer, bufferSize ) )
    {
        delete[] buffer;
        VERIFY_TRUE_RETURN_ON_FALSE( false );
    }

    vaXMLSerializer materialSerializer( buffer, bufferSize );
    GetManager( ).RegisterSerializationTypeConstructors( materialSerializer );

    bool allOk;
    {
        vaSerializerScopedOpenChild rootNode( materialSerializer, "Material" );
        allOk = rootNode.IsOK( );
        if( allOk )
            allOk = SerializeUnpacked( materialSerializer, "%there-is-no-folder@" );
    }

    assert( allOk );

    delete[] buffer;
    return allOk;
}

bool vaRenderMaterial::SerializeUnpacked( vaXMLSerializer & serializer, const string & assetFolder )
{
    assetFolder; // unused

    int32 fileVersion = c_renderMeshMaterialFileVersion;
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<int32>( "FileVersion", fileVersion ) );
    VERIFY_TRUE_RETURN_ON_FALSE( (fileVersion >= 2) && (fileVersion <= c_renderMeshMaterialFileVersion) );

    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<int32>( "FaceCull", (int32&)          m_materialSettings.FaceCull ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float>( "AlphaTestThreshold",         m_materialSettings.AlphaTestThreshold ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<bool>( "ReceiveShadows",              m_materialSettings.ReceiveShadows ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<bool>( "Wireframe",                   m_materialSettings.Wireframe ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<bool>( "AdvancedSpecularShader",      m_materialSettings.AdvancedSpecularShader,  m_materialSettings.AdvancedSpecularShader ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<bool>( "SpecialEmissiveLight",        m_materialSettings.SpecialEmissiveLight,    m_materialSettings.SpecialEmissiveLight ) );
    //VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<bool>( "NoDepthPrePass",              m_materialSettings.NoDepthPrePass,          m_materialSettings.NoDepthPrePass ) );
    /*VERIFY_TRUE_RETURN_ON_FALSE*/( serializer.Serialize<float>( "LocalIBLNormalBasedBias", m_materialSettings.LocalIBLNormalBasedBias ) );
    /*VERIFY_TRUE_RETURN_ON_FALSE*/( serializer.Serialize<float>( "LocalIBLBasedBias", m_materialSettings.LocalIBLBasedBias ) );
    /*VERIFY_TRUE_RETURN_ON_FALSE*/( serializer.Serialize<int>( "VRSRateOffset", m_materialSettings.VRSRateOffset ) );
    /*VERIFY_TRUE_RETURN_ON_FALSE*/( serializer.Serialize<bool>( "VRSPreferHorizontal", m_materialSettings.VRSPreferHorizontal ) );

    // handle backward compatibility
    if( !serializer.Serialize<int32>( "LayerMode", (int32&)m_materialSettings.LayerMode ) )
    {
        bool alphaTest = false, transparent = false, decal = false;
        VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<bool>( "AlphaTest", alphaTest ) );
        VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<bool>( "Transparent", transparent ) );
        /*VERIFY_TRUE_RETURN_ON_FALSE(*/ serializer.Serialize<bool>( "Decal", decal, false ) /*)*/;
        m_materialSettings.LayerMode = vaLayerMode::Opaque;
        if( alphaTest )
            m_materialSettings.LayerMode = vaLayerMode::AlphaTest;
        if( transparent )
        {
            assert( m_materialSettings.LayerMode == vaLayerMode::Opaque );
            m_materialSettings.LayerMode = vaLayerMode::Transparent;
        }
        if( decal )
        {
            assert( m_materialSettings.LayerMode == vaLayerMode::Opaque );
            m_materialSettings.LayerMode = vaLayerMode::Decal;
        }
    }

    /*VERIFY_TRUE_RETURN_ON_FALSE(*/ serializer.Serialize<int32>( "DecalSortOrder",         m_materialSettings.DecalSortOrder ) /*)*/;
    m_materialSettings.DecalSortOrder = vaMath::Clamp( m_materialSettings.DecalSortOrder, -10000, 10000 ); 

    string oldFormatShaderFileName = "";
    if( serializer.IsReading( ) )
    {
        if( serializer.Serialize<string>( "ShaderFileName",               oldFormatShaderFileName ) )
        {
            assert( false );
        }

        // // earlier override
        // if( vaStringTools::CompareNoCase( oldFormatShaderFileName, "vaMaterialPhong.hlsl" ) == 0 )
        //     oldFormatShaderFileName = "vaMaterialBasic.hlsl";
    }

    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<string>( "ShaderFileNameVS_Standard",       m_shaderSettings.VS_Standard.first, oldFormatShaderFileName ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<string>( "ShaderFileNameGS_Standard",       m_shaderSettings.GS_Standard.first, "" ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<string>( "ShaderFileNamePS_DepthOnly",      m_shaderSettings.PS_DepthOnly.first, oldFormatShaderFileName ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<string>( "ShaderFileNamePS_Forward",        m_shaderSettings.PS_Forward.first, oldFormatShaderFileName ) );
    //VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<string>( "ShaderFileNamePS_Deferred",       m_shaderSettings.PS_Deferred.first, oldFormatShaderFileName ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<string>( "ShaderFileNamePS_RichPrepass",   m_shaderSettings.PS_RichPrepass.first, oldFormatShaderFileName ) ); // default to be removed in the future

    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<string>( "ShaderEntryVS_Standard",       m_shaderSettings.VS_Standard.second ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<string>( "ShaderEntryGS_Standard",       m_shaderSettings.GS_Standard.second, "" ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<string>( "ShaderEntryPS_DepthOnly",      m_shaderSettings.PS_DepthOnly.second ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<string>( "ShaderEntryPS_Forward",        m_shaderSettings.PS_Forward.second ) );
    //VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<string>( "ShaderEntryPS_Deferred",       m_shaderSettings.PS_Deferred.second, string("") ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<string>( "ShaderEntryPS_RichPrepass",   m_shaderSettings.PS_RichPrepass.second, string("PS_RichPrepass") ) ); // default to be removed in the future but make sure all assets have been re-saved!

    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<string>( "ShaderEntryCAL_LibraryFile",   m_shaderSettings.CAL_LibraryFile , "vaRenderMaterial.hlsl"   ) );

    if( serializer.IsReading() )
    {
        m_shaderSettings.BaseMacros.clear();

        auto updateShaderFileName = []( string & current ) {

            if( current == "vaMaterialFilament.hlsl" || current == "" )
                current = "vaRenderMaterial.hlsl";
        };

        updateShaderFileName( m_shaderSettings.PS_Forward.first );
        updateShaderFileName( m_shaderSettings.PS_RichPrepass.first );
        updateShaderFileName( m_shaderSettings.PS_DepthOnly.first );
    }

    if( !serializer.SerializeArrayGeneric<std::vector<pair<string, string>>>( "ShaderBaseMacros", m_shaderSettings.BaseMacros, 
        [ ] ( bool isReading, std::vector<pair<string, string>> & container, int & itemCount )
        { 
            if( isReading )
                container.resize( itemCount );
            else
                itemCount = (int)container.size();
        }, 
        [ ]( vaXMLSerializer & serializer, std::vector<pair<string, string>> & container, int index )
        {
            bool allOk = true;
            pair<string, string> & inoutItem = container[index];
            allOk &= serializer.Serialize<string>( "Name", inoutItem.first );           assert( allOk );
            allOk &= serializer.Serialize<string>( "Definition", inoutItem.first );     assert( allOk );
            return allOk;
        } ) ) 
    { 
        // do nothing, it's ok not to have ShadeBaseMacros for legacy reasons
    };

    assert( serializer.GetVersion() > 0 );

    if( serializer.IsReading( ) )
    {
        // always needed
        m_inputsDirty = true;
    }

    serializer.SerializeArray( "InputSlots", m_inputSlots );

    if( fileVersion == 2 )
    {
        // this is a bit ugly & manual but that's the way it is until I get the new serialization approach working
        std::vector<shared_ptr<TextureNode>> textureNodes;
        if( serializer.IsWriting( ) )
        {
            for( int i = 0; i < m_nodes.size(); i++ ) { auto snode = std::dynamic_pointer_cast<TextureNode, Node>( m_nodes[i] ); if( snode != nullptr ) textureNodes.push_back( snode ); }
        }
        serializer.SerializeArray( "TextureNodes", textureNodes );

        if( serializer.IsReading( ) )
        {
            for( int i = 0; i < textureNodes.size( ); i++ ) { m_nodes.push_back( std::static_pointer_cast<Node>( textureNodes[i] ) ); }
        }
    }
    else
    {
        VERIFY_TRUE_RETURN_ON_FALSE( serializer.TypedSerializeArray( "TextureNodes", m_nodes ) );
    }

#if 0
    for( size_t i = 0; i < m_inputSlots.size(); i++ )
    {
        if( m_inputSlots[i].Name == "InvGlossiness" )
        {
            if( std::get<float>(m_inputSlots[i].Properties.Default) == 0.0f )
            {
                // VA_LOG( " >>>>>>>>> MATERIAL %s", assetFolder.c_str() );
                //m_inputSlots[i].Properties.IsMultiplier = false;
                SetInputSlotDefaultValue( "InvGlossiness", 0.09f );
            }
        }
    }
#endif

    assert( m_inputSlots.size() <= RENDERMATERIAL_MAX_INPUT_SLOTS );
    assert( m_nodes.size() <= RENDERMATERIAL_MAX_NODES );

    return true;
}

void vaRenderMaterial::RegisterUsedAssetPacks( std::function<void( const vaAssetPack & )> registerFunction )
{
    assert( GetRenderDevice( ).IsRenderThread( ) ); 
    vaAssetResource::RegisterUsedAssetPacks( registerFunction );

    for( int i = 0; i < m_nodes.size(); i++ ) 
    { 
        auto snode = std::dynamic_pointer_cast<TextureNode, Node>( m_nodes[i] ); 
        if( snode != nullptr && !snode->GetTextureUID( ).IsNull() )
        {
            auto texture = snode->GetTextureFP( );
            if( texture != nullptr )
            {
                texture->RegisterUsedAssetPacks( registerFunction );
            }
            else
            {
                // Either ReconnectDependencies( ) was not called, or the asset is missing?
                assert( false );
            }
        }
    }
}

bool vaRenderMaterial::RemoveNode( const string & name, bool assertIfNotFound )
{
    assert( GetRenderDevice( ).IsRenderThread( ) ); 
    assert( GetRenderDevice( ).IsRenderThread( ) ); 
    assertIfNotFound;
    assert( !m_immutable );
    int index = FindNodeIndex( name );
    if( index == -1 )
    {
        assert( !assertIfNotFound );
        return false;
    }

    // replace with last and pop last (order doesn't matter)
    if( index < ( m_nodes.size( ) - 1 ) )
        m_nodes[index] = m_nodes.back( );
    m_nodes.pop_back( );

    m_inputsDirty = true;
    return true;
}

namespace
{
    static bool IsNumber( char c ) { return c >= '0' && c <= '9'; }
};

string vaRenderMaterial::FindAvailableNodeName( const string & name )
{
    string retName = SanitizeInputSlotOrNodeName( name ); 
    int index = 0;
    while( FindNode( retName ) != nullptr )
    {
        // if last 3 characters are in _00 format, remove them
        if( retName.length() > 3 && retName[retName.length()-3] == '_' && IsNumber(retName[retName.length()-2]) && IsNumber(retName[retName.length()-1]) )
            retName = retName.substr( 0, retName.length()-3 );
        if( retName.length() > (c_materialItemNameMaxLength-3) )
            retName = retName.substr(0, c_materialItemNameMaxLength-3);

        if( index > 99 )
            retName = SanitizeInputSlotOrNodeName( vaCore::GUIDToStringA(vaCore::GUIDCreate()) );
        else
            retName += vaStringTools::Format( "_%02d", index );
        index++;
    }
    return retName;
}

bool vaRenderMaterial::SetNode( const shared_ptr<vaRenderMaterial::Node> & node )
{
    assert( GetRenderDevice( ).IsRenderThread( ) ); 
    assert( !m_immutable );
    int index = FindNodeIndex( node->GetName() );
    if( index == -1 )
        m_nodes.push_back( node );
    else
        m_nodes[index] = node;

    assert( m_inputSlots.size() <= RENDERMATERIAL_MAX_INPUT_SLOTS );
    assert( m_nodes.size() <= RENDERMATERIAL_MAX_NODES );

    m_inputsDirty = true;
    return true;
}

bool vaRenderMaterial::SetTextureNode( const string & name, const vaGUID & textureUID, vaStandardSamplerType samplerType, int uvIndex )
{
    assert( GetRenderDevice( ).IsRenderThread( ) ); 
    return SetNode( std::make_shared<TextureNode>( name, textureUID, samplerType, uvIndex ) );
}

bool vaRenderMaterial::ReplaceTextureOnNode( const string& name, const vaGUID& textureUID )
{
    assert( GetRenderDevice( ).IsRenderThread( ) ); 
    assert( !m_immutable );
    auto oldTextureNode = FindNode<TextureNode>( name );
    if( oldTextureNode == nullptr )
        return false;

    return SetNode( std::make_shared<TextureNode>( name, textureUID, oldTextureNode->SamplerType, oldTextureNode->UVIndex ) );
}


bool vaRenderMaterial::RemoveInputSlot( const string & name, bool assertIfNotFound )
{
    assert( GetRenderDevice( ).IsRenderThread( ) ); 
    assertIfNotFound;
    assert( !m_immutable );
    int index = FindInputSlotIndex( name );
    if( index == -1 )
    {
        assert( !assertIfNotFound );
        return false;
    }

    // replace with last and pop last (order doesn't matter)
    if( index < ( m_inputSlots.size( ) - 1 ) )
        m_inputSlots[index] = m_inputSlots.back( );
    m_inputSlots.pop_back( );

    m_inputsDirty = true;
    return true;
}

bool vaRenderMaterial::SetInputSlot( const vaRenderMaterial::InputSlot & inputSlot )
{
    assert( GetRenderDevice( ).IsRenderThread( ) ); 
    assert( !m_immutable );
    int index = FindInputSlotIndex( inputSlot.GetName( ) );
    if( index == -1 )
        m_inputSlots.push_back( inputSlot );
    else
        m_inputSlots[index] = inputSlot;

    assert( m_inputSlots.size() <= RENDERMATERIAL_MAX_INPUT_SLOTS );
    assert( m_nodes.size() <= RENDERMATERIAL_MAX_NODES );

    m_inputsDirty = true;
    return true;
}

bool vaRenderMaterial::SetInputSlot( const string & name, const ValueType & default, bool defaultIsMultiplier, bool isColor ) 
{ 
    assert( GetRenderDevice( ).IsRenderThread( ) ); 
    return SetInputSlot( InputSlot(name, default, defaultIsMultiplier, isColor ) ); 
}

bool vaRenderMaterial::SetInputSlotDefaultValue( const string & name, const ValueType & default )
{
    assert( GetRenderDevice( ).IsRenderThread( ) ); 
    bool assertOnError = true; assertOnError;
    assert( !m_immutable );
    int index = FindInputSlotIndex( name );
    if( index == -1 )
    {
        assert( !assertOnError );
        return false;
    }
    else
    {
        if( default.index( ) != m_inputSlots[index].Properties.Default.index( ) )
        {
            assert( !assertOnError );   // type mismatch between the already defined input slot ValueType and the provided one
            return false;
        }
        m_inputSlots[index].Properties.Default = default;
        //m_inputSlots[index].Properties.IsMultiplier = defaultIsMultiplier;
        //m_inputSlots[index].Properties.IsColor = isColor;
    }

    m_inputsDirty = true;
    return true;
}

bool vaRenderMaterial::ConnectInputSlotWithNode( const string & inputSlotName, const string & nodeName, const string & inputSwizzle, bool assertIfFailed )
{
    assert( GetRenderDevice( ).IsRenderThread( ) ); 
    assertIfFailed;
    int inputSlotIndex = FindInputSlotIndex( inputSlotName );
    if( inputSlotIndex == -1 )
        { assert( !assertIfFailed ); return false; }

    int nodeIndex = FindNodeIndex( nodeName );
    if( nodeName != "" && nodeIndex == -1 )
        { assert( !assertIfFailed ); return false; }

    ValueTypeIndex srcType = (nodeIndex == -1)?( m_inputSlots[inputSlotIndex].GetType() ) : ( m_nodes[nodeIndex]->GetType() );

    m_inputSlots[inputSlotIndex].ConnectedInput = nodeName;
    m_inputSlots[inputSlotIndex].CachedConnectedInput.reset();

    StringToSwizzle( m_inputSlots[inputSlotIndex].InputSwizzle, inputSwizzle );
    SanitizeSwizzle( m_inputSlots[inputSlotIndex].InputSwizzle, m_inputSlots[inputSlotIndex].GetType(), srcType );

    m_inputsDirty = true;
    return true;
}

void vaRenderMaterial::VerifyNames( )
{
    for( int i = 0; i < m_inputSlots.size(); i++ )
    {
        // must find itself but must have no other inputs with the same name
        int foundIndex = -1;
        for( int j = 0; j < m_inputSlots.size(); j++ ) 
            if( vaStringTools::ToLower( m_inputSlots[j].Name ) == vaStringTools::ToLower( m_inputSlots[i].Name ) ) 
            {
                foundIndex = j;
                break;
            }
        assert( foundIndex == i );
    }

    for( int i = 0; i < m_nodes.size( ); i++ )
    {
        // must find itself but must have no other inputs with the same name
        assert( FindNode( m_nodes[i]->GetName() ) == m_nodes[i] );
    }
}

void vaRenderMaterial::UpdateInputsDependencies( )
{
    // there is no real reason why this would happen at runtime except during editing and streaming; so in case there's no streaming,
    // update every 5th time; this could be further optimized if needed with dirty flags
    m_inputsDirtyThoroughTextureCheckCounter = (m_inputsDirtyThoroughTextureCheckCounter+1) % 5;
    if( m_inputsDirtyThoroughTextureCheckCounter == 0 || GetRenderDevice().GetAssetPackManager().HadAnyAsyncOpExecutingLastFrame() )
    {
        for( int i = 0; i < m_nodes.size( ); i++ )
            if( m_nodes[i]->RequiresReUpdate() )
                m_inputsDirty = true;
    }

    if( m_inputsDirty )
    {
        // reset all 'in use' flags
        for( int i = 0; i < m_nodes.size( ); i++ )
            m_nodes[i]->InUse = false;

        m_computedTextureSlotCount      = 0;
        m_computedConstantsSlotCount    = 0;
        m_currentShaderConstants.Invalidate();

        for( int i = 0; i < m_inputSlots.size( ); i++ )
        {
            // assign new constants slot - this is always needed for the default value
            m_inputSlots[i].ComputedShaderConstantsSlot = m_computedConstantsSlotCount;
            if( m_computedConstantsSlotCount < RENDERMATERIAL_MAX_SHADER_CONSTANTS )
                m_computedConstantsSlotCount++;
            else
            {
                VA_WARN( "vaRenderMaterial_UpdateInputsDependencies: more used constants than available - will be overwriting previous" );
                assert( false );
            }
            // and we can set the values here as well
            UploadToConstants( m_inputSlots[i].GetDefaultValue(), m_currentShaderConstants.Constants[m_inputSlots[i].ComputedShaderConstantsSlot] );

            string connectedInputName = m_inputSlots[i].ConnectedInput;
            if( connectedInputName == "" )
            {
                m_inputSlots[i].CachedConnectedInput.reset();
                continue;
            }
            shared_ptr<const Node> connectedNode = FindNode<Node>( connectedInputName );
            if( connectedNode == nullptr )
            {
                // this shouldn't happen - probably just disconnect? 
                ConnectInputSlotWithNode( m_inputSlots[i].Name, "" );
                assert( false );
                continue;
            }
            connectedNode->InUse = true;
            m_inputSlots[i].CachedConnectedInput = connectedNode;
        }

        for( int i = 0; i < m_nodes.size( ); i++ )
        {
            if( !m_nodes[i]->InUse )
            {
                m_nodes[i]->ResetTemps( );
                continue;
            }
            shared_ptr<const TextureNode> textureNode = std::dynamic_pointer_cast<const TextureNode, const Node>( m_nodes[i] );
            if( m_renderMaterialManager.GetTexturingDisabled( ) )
                textureNode = nullptr;

            if( textureNode != nullptr && textureNode->GetTextureFP() != nullptr )
            {
                // assign new texture slot
                textureNode->ComputedShaderTextureSlot = m_computedTextureSlotCount;
                if( m_computedTextureSlotCount < RENDERMATERIAL_MAX_TEXTURES )
                    m_computedTextureSlotCount++;
                else
                {
                    VA_WARN( "vaRenderMaterial_UpdateInputsDependencies: more used texture nodes than available shader texture slots - will be overwriting previous" );
                    assert( false );
                }
            }
            else
            {
                if( textureNode != nullptr && !textureNode->GetTextureUID( ).IsNull() )
                {
                    // VA_LOG( "vaRenderMaterial::UpdateInputsDependencies: trying to draw material with some textures not yet loaded" );
                    SetDelayedDirty( 0.1 ); // let's wait a short while until this is sorted (texture loaded or whatever)
                }

                m_nodes[i]->InUse = false;
                m_nodes[i]->ResetTemps( );
            }
        }
        m_inputsDirty       = false;
        m_shaderMacrosDirty = true;
        m_shadersDirty      = true;
    }
}

vaFramePtr<vaVertexShader> vaRenderMaterial::GetVS( vaRenderMaterialShaderType shaderType )
{
    shaderType;

    if( m_shaders->VS_Standard->IsEmpty() )
        return nullptr;

    // never use VS_PosOnly for now
    return m_shaders->VS_Standard.get();
}

vaFramePtr<vaGeometryShader> vaRenderMaterial::GetGS( vaRenderMaterialShaderType shaderType )
{
    shaderType;

    if( m_shaders->GS_Standard->IsEmpty() )
        return nullptr;

    return m_shaders->GS_Standard.get();
}

vaFramePtr<vaPixelShader> vaRenderMaterial::GetPS( vaRenderMaterialShaderType shaderType )
{
    vaFramePtr<vaPixelShader> retVal;
    //if( shaderType == vaRenderMaterialShaderType::Deferred )
    //    retVal = m_shaders->PS_Deferred.get();
    //else 
    if( shaderType == vaRenderMaterialShaderType::Forward )
        retVal = m_shaders->PS_Forward.get();
    else if( shaderType == vaRenderMaterialShaderType::DepthOnly )
        retVal = m_shaders->PS_DepthOnly.get();
    else if( shaderType == vaRenderMaterialShaderType::RichPrepass )
        retVal = m_shaders->PS_RichPrepass.get();
    else
    {
        assert( false );
        return nullptr;
    }
    if( retVal->IsEmpty() )
        return nullptr;
    return retVal;
}

bool vaRenderMaterial::GetCallableShaderLibrary( vaFramePtr<vaShaderLibrary> & outLibrary, string & uniqueID )
{
    if( m_shaders == nullptr || m_shaders->CAL_Library.get() == nullptr || m_shaders->CAL_Library->IsEmpty() )
        return false;
    outLibrary          = m_shaders->CAL_Library;
    uniqueID            = m_shaders->UniqueIDString;
    return true;
}

void vaRenderMaterial::SetDelayedDirty( double delayTime )
{
    //assert( m_mutex is locked ); 
    m_delayedInputsSetDirty = vaCore::TimeFromAppStart() + delayTime;
}

bool vaRenderMaterial::Update( )
{
    // don't update if we've already updated this frame
    if( m_lastUpdateFrame == GetRenderDevice().GetCurrentFrameIndex() && !m_shaderMacrosDirty && !m_shadersDirty && !m_inputsDirty )
        return true;

    if( m_delayedInputsSetDirty < vaCore::TimeFromAppStart() )
    {
        m_delayedInputsSetDirty = std::numeric_limits<double>::max( );
        m_inputsDirty = true;
    }

    UpdateShaderMacros( );

    if( m_shaderMacrosDirty )
    {
        // still dirty? there's non-loaded textures or something similar? - need to bail out.
        assert( false );
        return false;
    }

    if( m_shadersDirty || (m_shaders == nullptr) )
    {
        m_shaders = m_renderMaterialManager.FindOrCreateShaders( IsAlphaTested(), m_shaderSettings, m_shaderMacros );
        m_shadersDirty = m_shaders == nullptr;
        assert( m_shaders != nullptr );

        if( m_shadersDirty )
            return false;
    }
    assert( !m_inputsDirty && !m_shadersDirty && !m_shaderMacrosDirty );

    m_lastUpdateFrame = GetRenderDevice().GetCurrentFrameIndex();
    return true;
}

void vaRenderMaterial::GetShaderState_VS_Standard( vaShader::State & outState, string & outErrorString )
{
    if( !Update() )     { outState = vaShader::State::Uncooked; outErrorString = "Material shader cache not (yet) created"; assert( false ); }
    m_shaders->VS_Standard->GetState( outState, outErrorString );
}

void vaRenderMaterial::GetShaderState_GS_Standard( vaShader::State & outState, string & outErrorString )
{
    if( !Update() )     { outState = vaShader::State::Uncooked; outErrorString = "Material shader cache not (yet) created"; assert( false ); }
    m_shaders->GS_Standard->GetState( outState, outErrorString );
}

void vaRenderMaterial::GetShaderState_PS_DepthOnly( vaShader::State & outState, string & outErrorString )
{
    if( !Update() )     { outState = vaShader::State::Uncooked; outErrorString = "Material shader cache not (yet) created"; assert( false ); }
    m_shaders->PS_DepthOnly->GetState( outState, outErrorString );
}

void vaRenderMaterial::GetShaderState_PS_Forward( vaShader::State & outState, string & outErrorString )
{
    if( !Update() )     { outState = vaShader::State::Uncooked; outErrorString = "Material shader cache not (yet) created"; assert( false ); }
    m_shaders->PS_Forward->GetState( outState, outErrorString );
}

// void vaRenderMaterial::GetShaderState_PS_Deferred( vaShader::State & outState, string & outErrorString )
// {
//     if( !Update() )     { outState = vaShader::State::Uncooked; outErrorString = "Material shader cache not (yet) created"; assert( false ); }
//     m_shaders->PS_Deferred->GetState( outState, outErrorString );
// }

void vaRenderMaterial::GetShaderState_PS_RichPrepass( vaShader::State & outState, string & outErrorString )
{
    if( !Update() )     { outState = vaShader::State::Uncooked; outErrorString = "Material shader cache not (yet) created"; assert( false ); }
    m_shaders->PS_RichPrepass->GetState( outState, outErrorString );
}

bool vaRenderMaterial::PreRenderUpdate( vaRenderDeviceContext & renderContext )
{
    std::unique_lock uniqueLock(m_mutex);
    assert( !renderContext.IsWorker() );    // uploading the constant buffer requires the master thread

    bool shaderConstantsUpdateRequired = false;

    if( IsDirty() )
    {
//        // release shared lock
//        sharedLock.unlock();
        bool allOk = true;
//        {
//            // grab unique
//            std::unique_lock uniqueLock(m_mutex);
//            allOk = !IsDirty( );
//            if( !allOk )
                allOk = Update( );
//        }
//        // reacquire shared lock
//        sharedLock.lock();
        if( !allOk )
        {
            SetDelayedDirty( 0.1 ); // let's wait a short while until this is sorted (texture loaded or whatever)
            return false;
        }
        shaderConstantsUpdateRequired = true;
    }

    m_currentShaderConstants.AlphaTestThreshold = m_materialSettings.AlphaTestThreshold;
    m_currentShaderConstants.VA_RM_LOCALIBL_NORMALBIAS = m_materialSettings.LocalIBLNormalBasedBias;
    m_currentShaderConstants.VA_RM_LOCALIBL_BIAS = m_materialSettings.LocalIBLBasedBias;

    // Textures might have changed, so doing it after the above pass every time
    // (frequency could be reduced if too costly but at the moment it has to be done every frame because GetSRVBindlessIndex also transitions the textures to shader readable!!)
    for( int i = 0; i < m_nodes.size( ); i++ )
    {
        // used to be this: 
        //    shared_ptr<const TextureNode> textureNode = std::dynamic_pointer_cast<const TextureNode, const Node>( m_nodes[i] );
        // but raw ptr handling below is an order of magnitude faster due to addref/release thread contention issues in heavily multithreaded environment on above
        const vaRenderMaterial::TextureNode * textureNode = dynamic_cast<const vaRenderMaterial::TextureNode *>( m_nodes[i].get( ) );
        if( textureNode != nullptr && textureNode->InUse && textureNode->ComputedShaderTextureSlot != -1 )
        {
            assert( !m_renderMaterialManager.GetTexturingDisabled( ) );
            vaFramePtr<vaTexture> texture = textureNode->GetTextureFP( );
            if( textureNode->ComputedShaderTextureSlot >= 0 && textureNode->ComputedShaderTextureSlot < countof( ShaderMaterialConstants::BindlessSRVIndices ) && texture != nullptr )
            {
                // !! IMPORTANT !! GetSRVBindlessIndex also transitions the textures to shader readable!!
                uint32 texSRVBindlessIndex = texture->GetSRVBindlessIndex( &renderContext );
                if( m_currentShaderConstants.BindlessSRVIndices[textureNode->ComputedShaderTextureSlot] != texSRVBindlessIndex )
                {
                    m_currentShaderConstants.BindlessSRVIndices[textureNode->ComputedShaderTextureSlot] = texSRVBindlessIndex;
                    shaderConstantsUpdateRequired = true;
                }
            }
            else
            {
                VA_LOG( "vaRenderMaterial::SetToRenderItem - unable to set material texture for unknown reason." );
                SetDelayedDirty( 0.1 ); // let's wait a short while until this is sorted (texture loaded or whatever)
            }
        }
    }

    // Update GPU constant buffer if required!
    if( IsDirty() )
        return false;
    else
    {
        if( shaderConstantsUpdateRequired )
            m_renderMaterialManager.GetGlobalConstantBuffer()->UploadSingle<ShaderMaterialConstants>( renderContext, m_currentShaderConstants, m_globalIndex );
    }
    return true;
}

bool vaRenderMaterial::SetToRenderData( vaRenderMaterialData & outRenderData, vaDrawResultFlags & inoutDrawResults, vaRenderMaterialShaderType shaderType, std::shared_lock<decltype(vaRenderingModule::m_mutex)> & sharedLock )
{
    assert( sharedLock.owns_lock( ) && sharedLock.mutex( ) == &m_mutex ); sharedLock;

    if( IsDirty( ) )
    {
        inoutDrawResults |= vaDrawResultFlags::AssetsStillLoading;
        return false;
    }

    bool retVal = true;

    outRenderData.CullMode          = m_materialSettings.FaceCull;
    outRenderData.IsTransparent     = IsTransparent();
    outRenderData.IsWireframe       = m_materialSettings.Wireframe;
    outRenderData.CastShadows       = m_materialSettings.CastShadows;

    outRenderData.VertexShader     = GetVS( shaderType );
    outRenderData.GeometryShader   = GetGS( shaderType );
    outRenderData.PixelShader      = GetPS( shaderType );

    retVal &= outRenderData.VertexShader != nullptr;

    return retVal;
}

bool vaRenderMaterial::TextureNode::UIDraw( vaApplicationBase & , vaRenderMaterial &  )
{
    bool inputsChanged = false;

    string label = vaStringTools::Format( "%s (TextureNode)", Name.c_str( ), Name.c_str( ) );
    if( ImGui::CollapsingHeader( label.c_str( ), ImGuiTreeNodeFlags_Framed /*| ImGuiTreeNodeFlags_DefaultOpen*/ ) )
    {
        inputsChanged |= vaAssetPackManager::UIAssetLinkWidget<vaAssetTexture>( "texture_asset", UID );

        if( ImGuiEx_Combo( "UV Index", this->UVIndex, {{"0"},{"1"}} ) )
        {
            this->UVIndex = vaMath::Clamp( this->UVIndex, 0, 1 );
            inputsChanged = true;
        }

        std::vector<string> samplers; for( int i = 0; i < (int)vaStandardSamplerType::MaxValue; i++ ) samplers.push_back( vaStandardSamplerTypeToUIName( (vaStandardSamplerType)i ) );
        if( ImGuiEx_Combo( "Sampler", (int32&)this->SamplerType, samplers ) )
        {
            //this->SamplerType = (vaStandardSamplerType)vaMath::Clamp( (int)this->SamplerType, 0, (int)vaStandardSamplerType::MaxValue );
            inputsChanged = true;
        }
    }
    else
    {
        if( ImGui::IsItemHovered( ) )
        {
            auto texture = GetTextureFP( );
            vaAsset * textureAsset = (texture != nullptr)?(texture->GetParentAsset( )):(nullptr);
            string textureName = ( textureAsset != nullptr ) ? ( textureAsset->Name( ) ) : ( "Link present, asset not found" );

            string toolTipText;
            toolTipText += vaStringTools::Format( "Asset name:      %s\n", textureName.c_str( ) );
            toolTipText += vaStringTools::Format( "UV index:        %d\n", this->UVIndex );
            toolTipText += vaStringTools::Format( "Sampler:         %s\n", vaStandardSamplerTypeToUIName(this->SamplerType).c_str() );
            ImGui::SetTooltip( toolTipText.c_str( ) );
        }
    }

    return inputsChanged;
}

bool vaRenderMaterial::InputSlot::UIDraw( vaApplicationBase &, vaRenderMaterial & ownerMaterial )
{
    assert( Name.length() <= c_materialItemNameMaxLength );
    bool inputsChanged = false;

    const float indentSize = ImGui::GetFontSize( ) / 2;

    ImGui::PushID(Name.c_str());
    
    string info = Name;
    info.insert( info.end(), std::max( 0, c_materialItemNameMaxLength - (int)(Name.length()) ) + 1, ' ' );

    if( ConnectedInput != "" )
    {
        shared_ptr<const Node> connectedInput = ownerMaterial.FindNode( ConnectedInput );
        if( connectedInput )
            info += connectedInput->GetUIShortInfo( );
    }
    else
    {
        info += this->Properties.GetUIShortInfo( );
    }

    const std::vector<shared_ptr<Node>> & availableInputs = ownerMaterial.GetNodes();

    string label = vaStringTools::Format( "%s###InputSlot", info.c_str() );
    if( ImGui::CollapsingHeader( label.c_str(), ImGuiTreeNodeFlags_Framed /*| ImGuiTreeNodeFlags_DefaultOpen*/ ) )
    {
        ImGui::Indent( indentSize );
        
        ImGui::Text( "Name:            %s", Name.c_str() ); 
        if( ImGui::IsItemHovered( ) )   ImGui::SetTooltip( "This is the shader-visible name, accessed with VA_RM_HAS_INPUT_%s / RenderMaterialInputs::%s", Name.c_str(), Name.c_str() );
        
        string typeName = ValueTypeIndexToHLSL( (ValueTypeIndex)Properties.Default.index() );
        ImGui::Text( "Type:            %s", typeName.c_str() );
        if( ImGui::IsItemHovered( ) )   ImGui::SetTooltip( "This is the type used to read the value from the shaders" );

        inputsChanged |= Properties.DrawUI( );

        {
            int currentIndex = 0;
            std::vector<string> availableInputNames;
            availableInputNames.push_back("<none>");
            for( int i = 0; i < availableInputs.size(); i++ )
            {
                availableInputNames.push_back(availableInputs[i]->GetName());
                if( availableInputNames[i+1] == ConnectedInput )
                    currentIndex = i+1;
            }
            if( ImGuiEx_Combo( "Connected Input", currentIndex, availableInputNames ) )
            {
                string newlySelectedInputName = ( currentIndex == 0 )?( "" ):( availableInputNames[currentIndex] );
                if( newlySelectedInputName != ConnectedInput )
                {
                    ownerMaterial.ConnectInputSlotWithNode( this->Name, newlySelectedInputName, this->InputSwizzle );
                    inputsChanged = true;
                }
            }
        }
        shared_ptr<const Node> connectedInput = ownerMaterial.FindNode( ConnectedInput );
        string connectedInputName = (connectedInput==nullptr)?"<none>":ConnectedInput;

        if( connectedInput!=nullptr )
        {
            char inputSwizzleEdit[countof(InputSwizzle)]; memcpy( inputSwizzleEdit, InputSwizzle, sizeof(InputSwizzle) );
            if( ImGui::InputText( "Input Swizzle", inputSwizzleEdit, IM_ARRAYSIZE( inputSwizzleEdit ), ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_EnterReturnsTrue ) )
            {
                SanitizeSwizzle( inputSwizzleEdit, GetType(), (connectedInput!=nullptr)?(connectedInput->GetType()):(GetType()) );
                if( memcmp( inputSwizzleEdit, InputSwizzle, sizeof( InputSwizzle ) ) != 0 )
                {
                    memcpy( InputSwizzle, inputSwizzleEdit, sizeof(InputSwizzle) );
                    inputsChanged = true;
                }
            }
        }

        ImGui::Unindent( indentSize );
    }
    else
    {
        if( ImGui::IsItemHovered( ) )
        {
            string toolTipText;
            toolTipText += vaStringTools::Format( "Name:            %s\n", Name.c_str( ) );
            toolTipText += vaStringTools::Format( "Type:            %s\n", ValueTypeIndexToHLSL( (ValueTypeIndex)Properties.Default.index( ) ).c_str( ) );
            toolTipText += vaStringTools::Format( "Default value:   %s\n", Properties.GetUIShortInfo( ).c_str( ) );
            string connectedInput = ( ConnectedInput == "" ) ? "<none>" : ConnectedInput;
            toolTipText += vaStringTools::Format( "Connected input: %s\n", connectedInput.c_str( ) );
            toolTipText += vaStringTools::Format( "Input swizzle:   %s\n", string(InputSwizzle).c_str() );

            ImGui::SetTooltip( toolTipText.c_str() );
        }
    }

    ImGui::PopID();
    
    return inputsChanged;
}

bool vaRenderMaterial::UIPropertiesDraw( vaApplicationBase & application )
{
    assert( GetRenderDevice().IsRenderThread() ); // upgrade to use std::unique_lock lock( m_mutex ) if you need this to change

    bool hadChanges = false;
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    const float indentSize = ImGui::GetFontSize( ) / 2;

    if( ImGui::CollapsingHeader( "Import/Export", ImGuiTreeNodeFlags_Framed /*| ImGuiTreeNodeFlags_DefaultOpen*/ ) )
    {
        ImGui::Indent( indentSize );

        if( ImGui::Button( "Export to text file" ) )
        {
            string fileName = vaFileTools::SaveFileDialog( "", vaCore::GetExecutableDirectoryNarrow( ), "Vanilla material (.vamat) \0*.vamat\0\0" );
            fileName = vaFileTools::FixExtension( fileName, ".vamat" );

            vaXMLSerializer serializer; // create serializer for writing
            GetManager().RegisterSerializationTypeConstructors( serializer ); 

            bool allOk;
            {
                vaSerializerScopedOpenChild rootNode( serializer, "Material" );
                allOk = rootNode.IsOK( );
                if( allOk )
                    allOk = SerializeUnpacked( serializer, "%there-is-no-folder@" );
            }
            assert( allOk );
            if( allOk )
                allOk = serializer.WriterSaveToFile( fileName );
            assert( allOk );
        }

        ImGui::Separator( );

        ImGui::TextColored( { 1.0f, 0.5f, 0.3f, 1.0f }, "Warning, this will completely reset the material" );
        if( ImGui::Button( "Import from text file" ) )
        {
            string fileName = vaFileTools::OpenFileDialog( "", vaCore::GetExecutableDirectoryNarrow( ), "Vanilla material (.vamat) \0*.vamat\0\0" );

            vaXMLSerializer serializer(fileName);     // create serializer for writing
            GetManager( ).RegisterSerializationTypeConstructors( serializer );

            bool allOk;
            if( (allOk = serializer.IsReading()) == true )
            {
                vaSerializerScopedOpenChild rootNode( serializer, "Material" );
                allOk = rootNode.IsOK( );
                if( allOk )
                    allOk = SerializeUnpacked( serializer, "%there-is-no-folder@" );
            }
            assert( allOk );
        }

        ImGui::Unindent( indentSize );
    }

    if( ImGui::CollapsingHeader( "Reset to preset", ImGuiTreeNodeFlags_Framed /*| ImGuiTreeNodeFlags_DefaultOpen*/ ) )
    {
        ImGui::Indent( indentSize );
        ImGui::TextColored( { 1.0f, 0.5f, 0.3f, 1.0f }, "Warning, this will completely reset the material" ); 

        auto presets = GetPresetMaterials( );
        for( string preset : presets )
        {
            if( ImGui::Button( ( "'" + preset + "'" ).c_str( ), { -1, 0 } ) )
            {
                hadChanges = true;
                if( SetupFromPreset( preset ) )
                    VA_LOG_SUCCESS( "Material set up to '%s'", preset.c_str() );
                else
                    VA_LOG_ERROR( "Material failed to set up to '%s'", preset.c_str() );;
            }
        }
        ImGui::Unindent( indentSize );
    }

    ImGui::Separator();

    bool inputsChanged = false;

    ImGui::TextColored( ImVec4( 1.0f, 0.7f, 0.7f, 1.0f ), "Input slots:" );
    {
        ImGui::Indent( indentSize );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 4, 1 ) ); // Tighten spacing
        for( int i = 0; i < m_inputSlots.size( ); i++ )
        {
            VA_GENERIC_RAII_SCOPE( ImGui::PushID(m_inputSlots[i].GetName().c_str());, ImGui::PopID(); );
            inputsChanged |= m_inputSlots[i].UIDraw( application, *this );
        }
        ImGui::PopStyleVar( );
        ImGui::Unindent( indentSize );
    }

    ImGui::Separator( );

    ImGui::TextColored( ImVec4( 1.0f, 0.7f, 0.7f, 1.0f ), "Inputs:" );
    {
        ImGui::Indent( indentSize );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 4, 1 ) ); // Tighten spacing
        for( int i = 0; i < m_nodes.size( ); i++ )
        {
            VA_GENERIC_RAII_SCOPE( ImGui::PushID(m_nodes[i]->GetName().c_str());, ImGui::PopID(); );
            inputsChanged |= m_nodes[i]->UIDraw( application, *this );
        }
        ImGui::PopStyleVar( );
        ImGui::Unindent( indentSize );
    }

    ImGui::Separator( );

    // don't dirtify for a second after each change: provides a smoother user experience
    if( inputsChanged )
        SetDelayedDirty( 0.5 );

    {
        ImGui::TextColored( ImVec4( 0.7f, 0.7f, 1.0f, 1.0f ), "Material settings:" );
        ImGui::Indent( indentSize );
        vaRenderMaterial::MaterialSettings settings = GetMaterialSettings( );
        ImGui::Combo( "Culling mode", (int*)& settings.FaceCull, "None\0Front\0Back\0\0" );
        ImGui::Combo( "Layer mode", (int*)& settings.LayerMode, "Opaque\0AlphaTest\0Decal\0Transparent\0\0" );
        if( settings.LayerMode == vaLayerMode::Decal )
            { ImGui::InputInt( " DecalSortOrder", &settings.DecalSortOrder ); settings.DecalSortOrder = vaMath::Clamp( settings.DecalSortOrder, -10000, 10000 ); }
        else if( settings.LayerMode == vaLayerMode::AlphaTest )
            ImGui::InputFloat( " AlphaTestThreshold", &settings.AlphaTestThreshold );
        settings.AlphaTestThreshold = vaMath::Clamp( settings.AlphaTestThreshold, 0.0f, 1.0f );
        ImGui::Checkbox( "ReceiveShadows", &settings.ReceiveShadows );
        ImGui::Checkbox( "CastShadows", &settings.CastShadows );
        ImGui::Checkbox( "Wireframe", &settings.Wireframe );
        ImGui::Checkbox( "AdvancedSpecularShader", &settings.AdvancedSpecularShader );
        ImGui::Checkbox( "SpecialEmissiveLight", &settings.SpecialEmissiveLight );
        //ImGui::Checkbox( "NoDepthPrePass", &settings.NoDepthPrePass );

        ImGui::InputFloat( "LocalIBLNormalBasedBias", &settings.LocalIBLNormalBasedBias );
        ImGui::InputFloat( "LocalIBLBasedBias", &settings.LocalIBLBasedBias );

        ImGui::InputInt( "VRSRateOffset", &settings.VRSRateOffset ); settings.VRSRateOffset = vaMath::Clamp( settings.VRSRateOffset, -4, 4 );

        int hvPreference = (settings.VRSPreferHorizontal)?(0):(1);
        if( ImGui::Combo( "VRSRectPreference", &hvPreference, "Horizontal\0Vertical\0\0" ) )
            settings.VRSPreferHorizontal = hvPreference == 0;

        if( GetMaterialSettings( ) != settings )
        {
            hadChanges = true;
            SetMaterialSettings( settings );
        }
        ImGui::Unindent( indentSize );
    }

    ImGui::Separator();

    {
        ImGui::TextColored( ImVec4( 0.7f, 0.7f, 1.0f, 1.0f ), "Shader settings:" );
        ImGui::Indent( indentSize );

        vaRenderMaterial::ShaderSettings shaderSettings = GetShaderSettings( );

        ImGui::Text( "Vertex Shader source file & entry point" );
        ImGui::Indent( indentSize );
        float clientWidth = ImGui::GetContentRegionAvail( ).x;
        vaShader::State shaderState; string shaderCompileError;

        ImGui::PushItemWidth( -1 );
        //ImGui::InputTextEx( "|###VS_file", &shaderSettings.VS_Standard.first, { clientWidth / 2.0f, 0.0f }, ImGuiInputTextFlags_None );
        ImGui::SetNextItemWidth( clientWidth / 2.0f );
        ImGui::InputText( "|###VS_file", &shaderSettings.VS_Standard.first, ImGuiInputTextFlags_None );
        ImGui::SameLine( );
        //ImGui::InputTextEx( "###VS_entry", &shaderSettings.VS_Standard.second, { clientWidth / 2.0f, 0.0f }, ImGuiInputTextFlags_None );
        ImGui::SetNextItemWidth( clientWidth / 2.0f );
        ImGui::InputText( "###VS_entry", &shaderSettings.VS_Standard.second, ImGuiInputTextFlags_None );
        ImGui::PopItemWidth( );
        GetShaderState_VS_Standard( shaderState, shaderCompileError );
        ImGui::Text( "Current status: %s, %s", vaShader::StateToString( shaderState ).c_str( ), ( shaderCompileError == "" ) ? ( "OK" ) : ( shaderCompileError.c_str( ) ) );

        ImGui::Unindent( indentSize );

        if( GetShaderSettings( ) != shaderSettings )
        {
            hadChanges = true;
            SetShaderSettings( shaderSettings );
        }
        ImGui::Unindent( indentSize );
    }

#endif // #ifdef VA_IMGUI_INTEGRATION_ENABLED
    return hadChanges;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// vaRenderMaterialManager
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


vaRenderMaterialManager::vaRenderMaterialManager( const vaRenderingModuleParams & params ) : 
    vaRenderingModule( params ),
    vaUIPanel( "RenderMaterialManager", 0, false, vaUIPanel::DockLocation::DockedLeftBottom )
{
    m_isDestructing = false;
    
    // m_renderMaterials.SetAddedCallback( std::bind( &vaRenderMaterialManager::RenderMaterialsTrackeeAddedCallback, this, std::placeholders::_1 ) );
    // m_renderMaterials.SetBeforeRemovedCallback( std::bind( &vaRenderMaterialManager::RenderMaterialsTrackeeBeforeRemovedCallback, this, std::placeholders::_1, std::placeholders::_2 ) );
    
    // create default material
    {
        m_defaultMaterial = CreateRenderMaterial( vaCore::GUIDFromString( L"11523d65-09ea-4342-9bad-8dab7a4dc1e0" ) );
        m_defaultMaterial->SetupFromPreset( c_FilamentStandard );
        // disable any further modifications
        m_defaultMaterial->SetImmutable( true );
    }

    // create default for displaying lights (debugging only!)
    {
        m_defaultEmissiveLightMaterial = CreateRenderMaterial( vaCore::GUIDFromString( L"11523d65-09ea-4342-9bad-8dab7a4dc1e1" ) );
        m_defaultEmissiveLightMaterial->SetupFromPreset( c_FilamentStandard );

        // how bright to show the light - no direct physical relationship
        const float intensity = 30.0f;

        // this is to make the material shine when it's within the 'Size' area of a light - otherwise it's just black
        m_defaultEmissiveLightMaterial->SetInputSlot( "EmissiveColor",      vaVector3( 1.0f, 1.0f, 1.0f ), true, true );
        m_defaultEmissiveLightMaterial->SetInputSlot( "EmissiveIntensity",  intensity, false, false );
        auto settings = m_defaultEmissiveLightMaterial->GetMaterialSettings();
        settings.SpecialEmissiveLight = true;
        m_defaultEmissiveLightMaterial->SetMaterialSettings(settings);

        m_defaultEmissiveLightMaterial->SetInputSlot( "BaseColor",          vaVector4( 0.0f, 0.0f, 0.0f, 1.0f ), true, true );
        m_defaultEmissiveLightMaterial->SetInputSlot( "Roughness",          1.0f, false, false );
        m_defaultEmissiveLightMaterial->SetInputSlot( "Metallic",           0.0f, false, false );
        m_defaultEmissiveLightMaterial->SetInputSlot( "Reflectance",        0.0f, false, false );
        m_defaultEmissiveLightMaterial->SetInputSlot( "AmbientOcclusion",   0.0f, true, false );

        // disable any further modifications
        m_defaultEmissiveLightMaterial->SetImmutable( true );
    }

    m_texturingDisabled = false;

    // from filament CMakeLists.txt:
    // set(output_path "${GENERATION_ROOT}/generated/data/dfg.inc")
    // add_custom_command(
    //         OUTPUT ${output_path}
    //         COMMAND cmgen --quiet --size=${DFG_LUT_SIZE} --ibl-dfg-multiscatter --ibl-dfg-cloth --ibl-dfg=${output_path}
    //         DEPENDS cmgen
    //         COMMENT "Generating DFG LUT ${output_path}"
    // )
    // list(APPEND DATA_BINS ${output_path})
    m_DFG_LUT = vaTexture::CreateFromImageFile( GetRenderDevice(), "dfg-multiscatter-cloth.dds" );

    m_constantBuffer = vaRenderBuffer::Create<ShaderMaterialConstants>( GetRenderDevice(), m_constantBufferMaxCount, vaRenderBufferFlags::None, "ShaderMaterialConstants" );
}

vaRenderMaterialManager::~vaRenderMaterialManager( )
{
    assert( m_cachedShaders.size() == m_cachedShadersUniqueIDs.size() );

    m_isDestructing = true;
    //m_renderMeshesMap.clear();

    m_defaultMaterial = nullptr;
    m_defaultEmissiveLightMaterial = nullptr;

    {
        std::shared_lock managerLock( Mutex() );
        for( int i = (int)m_materials.PackedArray().size()-1; i>=0; i-- )
            m_materials.At(m_materials.PackedArray()[i])->UIDObject_Untrack();

        // this must absolutely be true as they contain direct reference to this object
        assert( m_materials.Count() == 0 );
    }
}

void vaRenderMaterialManager::SetTexturingDisabled( bool texturingDisabled )
{
    if( m_texturingDisabled == texturingDisabled )
        return;

    m_texturingDisabled = texturingDisabled;

    {
        std::shared_lock managerLock( Mutex() );
        for( int i : m_materials.PackedArray() )
            m_materials.At(i)->SetInputsDirty();
    }
}

shared_ptr<vaRenderMaterial> vaRenderMaterialManager::CreateRenderMaterial( const vaGUID & uid, bool startTrackingUIDObject )
{
    auto ret = GetRenderDevice().CreateModule< vaRenderMaterial, vaRenderMaterialConstructorParams>( *this, uid );
    if( startTrackingUIDObject )
    {
        assert( vaThreading::IsMainThread( ) ); // warning, potential bug - don't automatically start tracking if adding from another thread; rather finish initialization completely and then manually call UIDObject_Track
        ret->UIDObject_Track();                 // needs to be registered to be visible/searchable by various systems such as rendering
    }
    return ret;
}

void vaRenderMaterialManager::UIPanelTick( vaApplicationBase & )
{
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    static int selected = 0;
    ImGui::BeginChild( "left pane", ImVec2( 150, 0 ), true );
    for( int i = 0; i < 7; i++ )
    {
        char label[128];
        sprintf_s( label, _countof( label ), "MyObject %d", i );
        if( ImGui::Selectable( label, selected == i ) )
            selected = i;
    }
    ImGui::EndChild( );
    ImGui::SameLine( );

    // right
    ImGui::BeginGroup( );
    ImGui::BeginChild( "item view", ImVec2( 0, -ImGui::GetFrameHeightWithSpacing( ) ) ); // Leave room for 1 line below us
    ImGui::Text( "MyObject: %d", selected );
    ImGui::Separator( );
    ImGui::TextWrapped( "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. " );
    ImGui::EndChild( );
    ImGui::BeginChild( "buttons" );
    if( ImGui::Button( "Revert" ) ) { }
    ImGui::SameLine( );
    if( ImGui::Button( "Save" ) ) { }
    ImGui::EndChild( );
    ImGui::EndGroup( );
#endif
}

shared_ptr<vaRenderMaterialCachedShaders>
vaRenderMaterialManager::FindOrCreateShaders( bool alphaTest, const vaRenderMaterial::ShaderSettings & shaderSettings, const std::vector< pair< string, string > > & _shaderMacros )
{
    vaRenderMaterialCachedShaders::Key cacheKey( alphaTest, shaderSettings, _shaderMacros );

    std::unique_lock lock( m_cachedShadersMutex );

    auto it = m_cachedShaders.find( cacheKey );
    
    // in cache but no longer used by anyone so it was destroyed
    if( it != m_cachedShaders.end() )
    {
        auto oldItem = it->second.lock();
        if( oldItem == nullptr )
        {
            m_cachedShadersUniqueIDs.erase( it->first.UniqueID );
            m_cachedShaders.erase( it );
            it = m_cachedShaders.end();
        }
    }

    // not in cache
    if( it == m_cachedShaders.end() )
    {
        shared_ptr<vaRenderMaterialCachedShaders> newShaders( new vaRenderMaterialCachedShaders( GetRenderDevice() ) );

        // This unique ID is there in only for the case of a special extra shader uint32-based define that uniquely (at runtime) describes
        // the shader. 
        uint32 uniqueID = (cacheKey.Hash & 0xFFFF);
        while( !m_cachedShadersUniqueIDs.insert( uniqueID ).second )
            uniqueID++;
        cacheKey.UniqueID = uniqueID;
        newShaders->UniqueID = uniqueID;
        newShaders->UniqueIDString = vaStringTools::Format( "%d", uniqueID );
        
        // Enable additional macros
        m_scratchShaderMacrosStorage = _shaderMacros;
        std::vector< pair< string, string > > & shaderMacros = m_scratchShaderMacrosStorage;
        m_scratchShaderMacrosStorage.push_back( std::pair<string, string>( "VA_RM_SHADER_ID",   newShaders->UniqueIDString ) );

        // vertex input layout is here!
        std::vector<vaVertexInputElementDesc> inputElements;
        inputElements.push_back( { "SV_Position", 0,    vaResourceFormat::R32G32B32_FLOAT,       0, vaVertexInputElementDesc::AppendAlignedElement, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );
        inputElements.push_back( { "COLOR", 0,          vaResourceFormat::R8G8B8A8_UNORM,        0, vaVertexInputElementDesc::AppendAlignedElement, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );
        inputElements.push_back( { "NORMAL", 0,         vaResourceFormat::R32G32B32A32_FLOAT,    0, vaVertexInputElementDesc::AppendAlignedElement, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );
        inputElements.push_back( { "TEXCOORD", 0,       vaResourceFormat::R32G32B32A32_FLOAT,    0, vaVertexInputElementDesc::AppendAlignedElement, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );
        //inputElements.push_back( { "TEXCOORD", 1,       vaResourceFormat::R32G32_FLOAT,          0, vaVertexInputElementDesc::AppendAlignedElement, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );

        if( shaderSettings.VS_Standard.first != "" && shaderSettings.VS_Standard.second != "" )
            newShaders->VS_Standard->CreateShaderAndILFromFile( shaderSettings.VS_Standard.first, shaderSettings.VS_Standard.second.c_str( ), inputElements, shaderMacros, false );
//        else
//            vaCore::Warning( "Material has no vertex shader!" );

        {
            string gsFile = shaderSettings.GS_Standard.first;
            string gsEntry = shaderSettings.GS_Standard.second;
            if( m_globalGSOverrideEnabled )
            {
                gsFile = shaderSettings.VS_Standard.first;
                gsEntry = "GS_Standard";
            }
            if( gsFile != "" && gsEntry != "" )
                newShaders->GS_Standard->CreateShaderFromFile( gsFile, gsEntry.c_str( ), shaderMacros, false );
        }

        if( alphaTest ) 
        {
            if( shaderSettings.PS_DepthOnly.first != "" && shaderSettings.PS_DepthOnly.second != "" )
                newShaders->PS_DepthOnly->CreateShaderFromFile( shaderSettings.PS_DepthOnly.first, shaderSettings.PS_DepthOnly.second.c_str( ), shaderMacros, false );
            else
                VA_ERROR( "Material has no depth only pixel shader but alpha test is used!" );
        }
        else
            newShaders->PS_DepthOnly->Clear( true );
        if( shaderSettings.PS_Forward.first != "" && shaderSettings.PS_Forward.second != "" )
            newShaders->PS_Forward->CreateShaderFromFile( shaderSettings.PS_Forward.first, shaderSettings.PS_Forward.second.c_str( ), shaderMacros, false );
//        else
//            vaCore::Warning( "Material has no pixel shader!" );
//        if( shaderSettings.PS_Deferred.first != "" && shaderSettings.PS_Deferred.second != "" )
//            newShaders->PS_Deferred->CreateShaderFromFile( shaderSettings.PS_Deferred.first, shaderSettings.PS_Deferred.second.c_str( ), shaderMacros, false );
        if( shaderSettings.PS_RichPrepass.first != "" && shaderSettings.PS_RichPrepass.second != "" )
            newShaders->PS_RichPrepass->CreateShaderFromFile( shaderSettings.PS_RichPrepass.first, shaderSettings.PS_RichPrepass.second.c_str( ), shaderMacros, false );

        // ***RAYTRACING ONLY SHADERS BELOW***
        shaderMacros.push_back( std::pair<string, string>( "VA_RAYTRACING",   "" ) );
        if( shaderSettings.CAL_LibraryFile != "" )
            newShaders->CAL_Library->CreateShaderFromFile( shaderSettings.CAL_LibraryFile, "", shaderMacros, false );
        
        // finally, add to cache
        m_cachedShaders.insert( std::make_pair( cacheKey, newShaders ) );

        return newShaders;
    }
    else
    {
        return it->second.lock();
    }
}

void vaRenderMaterialManager::ResetCaches( )
{
    {
        std::shared_lock managerLock( Mutex() );
        for( int i : m_materials.PackedArray() )
            m_materials.At(i)->SetShadersDirty();
    }
    m_cachedShaders.clear();
    m_cachedShadersUniqueIDs.clear();
}

void vaRenderMaterialManager::SetGlobalShaderMacros( const std::vector< pair< string, string > > & globalShaderMacros ) 
{
    assert( GetRenderDevice().IsRenderThread() );
 
    if( m_globalShaderMacros == globalShaderMacros )
        return;

    m_globalShaderMacros = globalShaderMacros;
    ResetCaches( );
}

void vaRenderMaterialManager::SetGlobalGSOverride( bool enabled )
{
    assert( GetRenderDevice( ).IsRenderThread( ) );

    if( m_globalGSOverrideEnabled == enabled )
        return;

    m_globalGSOverrideEnabled       = enabled;
    ResetCaches( );
}

//void vaRenderMaterialManager::SetGlobalDepthPrepassExportsNormals( bool enabled )
//{
//    assert( GetRenderDevice( ).IsRenderThread( ) );
//
//    if( m_globalDepthPrepassExportsNormals == enabled )
//        return;
//
//    m_globalDepthPrepassExportsNormals = enabled;
//    ResetCaches( ); // not actually needed at the moment
//}

void vaRenderMaterialManager::UpdateAndSetToGlobals( vaRenderDeviceContext & renderContext, vaShaderItemGlobals & shaderItemGlobals, const vaDrawAttributes * drawAttributes )
{   
    // slowly clear shader cache
    assert( m_cachedShaders.size() == m_cachedShadersUniqueIDs.size() );
    if( m_cachedShaders.size() > 0 )
    {
        auto randomIt = std::next( std::begin(m_cachedShaders), vaRandom::Singleton.NextIntRange( 0, (int)m_cachedShaders.size()) );
        // in cache but no longer used by anyone so it was destroyed
        if( randomIt != m_cachedShaders.end() )
        {
            auto oldItem = randomIt->second.lock();
            if( oldItem == nullptr )
            {
                m_cachedShadersUniqueIDs.erase( randomIt->first.UniqueID );
                m_cachedShaders.erase( randomIt );
                randomIt = m_cachedShaders.end();
            }
        }
    }

    renderContext; drawAttributes;
    assert( shaderItemGlobals.ShaderResourceViews[SHADERGLOBAL_MATERIAL_DFG_LOOKUPTABLE_TEXTURESLOT] == nullptr );
    shaderItemGlobals.ShaderResourceViews[SHADERGLOBAL_MATERIAL_DFG_LOOKUPTABLE_TEXTURESLOT] = m_DFG_LUT;
    assert( shaderItemGlobals.ShaderResourceViews[SHADERGLOBAL_MATERIAL_CONSTANTBUFFERS_TEXTURESLOT] == nullptr );
    shaderItemGlobals.ShaderResourceViews[SHADERGLOBAL_MATERIAL_CONSTANTBUFFERS_TEXTURESLOT] = m_constantBuffer;
}


vaRenderMaterial::ValueProperties::ValueProperties( const ValueType& default, bool isMultiplier, bool isColor ) : Default( default ), IsColor( isColor ), IsMultiplier( isMultiplier )
{
    GetDefaultMinMax( (ValueTypeIndex)default.index(), Min, Max );
}

int vaRenderMaterial::ValueProperties::GetComponentCount( ) const
{
    return ValueTypeIndexGetComponentCount( this->GetType() );
}

string vaRenderMaterial::ValueProperties::GetUIShortInfo( ) const
{
    string ret;
    const int decimals = 3;

    if( IsColor )
    { 
        assert( this->Default.index() == (int)ValueTypeIndex::Vector3 || this->Default.index() == (int)ValueTypeIndex::Vector4 ); 
        if( this->Default.index( ) == (int)ValueTypeIndex::Vector3 )
            ret = Vector3ToString( vaVector3::LinearToSRGB( std::get<vaVector3>( this->Default ) ), decimals );
        else if( this->Default.index( ) == (int)ValueTypeIndex::Vector4 )
            ret = Vector4ToString( vaVector4::LinearToSRGB( std::get<vaVector4>( this->Default ) ), decimals );
        else
            ret = "error";
    }
    else
    {
        ret = ValueTypeToString( this->Default, decimals );
    }

    ret += " (" + ValueTypeIndexToHLSL( (ValueTypeIndex)this->Default.index() ) + ((IsColor)?(", sRGB)"):(")"));
    return ret;
}

bool vaRenderMaterial::ValueProperties::ClampMinMax( ValueType & value )
{
    ValueType inVal = value;
    assert( value.index() == Default.index() );

    switch( (ValueTypeIndex)value.index() )
    {
        case( ValueTypeIndex::Bool    ): if( this->Min == this->Max ) value = this->Min; break;
        case( ValueTypeIndex::Integer ): value = vaMath::Clamp( std::get<int32>(value), std::get<int32>(Min), std::get<int32>(Max) ); break;
        case( ValueTypeIndex::Scalar  ): value = vaMath::Clamp( std::get<float>(value), std::get<float>(Min), std::get<float>(Max) ); break;
        case( ValueTypeIndex::Vector3 ): value = vaVector3::Clamp( std::get<vaVector3>(value), std::get<vaVector3>(Min), std::get<vaVector3>(Max) ); break;
        case( ValueTypeIndex::Vector4 ): value = vaVector4::Clamp( std::get<vaVector4>(value), std::get<vaVector4>(Min), std::get<vaVector4>(Max) ); break;
    default: assert( false ); break;
    }

    return inVal != value;
}

bool vaRenderMaterial::ValueProperties::DrawUI( )
{
    bool inputsChanged = false;
    assert( Default.index() == Min.index() && Default.index() == Max.index() );
    const float indentSize = ImGui::GetFontSize( ) / 2;

    string label = vaStringTools::Format( "Default:        %s###DefaultProps", GetUIShortInfo().c_str( ) );
    if( ImGui::CollapsingHeader( label.c_str( ), ImGuiTreeNodeFlags_Framed /*| ImGuiTreeNodeFlags_DefaultOpen*/ ) )
    {
        ImGui::Indent( indentSize );

        ValueType prevDefault = Default;
        if( IsColor )
        {
            if( Default.index( ) == (int)ValueTypeIndex::Vector3 )
            {
                vaVector3 color = vaVector3::LinearToSRGB( std::get<vaVector3>( Default ) );
                if( ImGui::ColorEdit3( "Default", &color.x, ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR ) )
                    Default = vaVector3::SRGBToLinear( color );
            }
            else if( Default.index( ) == (int)ValueTypeIndex::Vector4 )
            {
                vaVector4 color = vaVector4::LinearToSRGB( std::get<vaVector4>( Default ) );
                if( ImGui::ColorEdit4( "Default", &color.x, ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR ) )
                    Default = vaVector4::SRGBToLinear( color );
            }
            else
            {
                assert( false );
                ImGui::Text("error");
            }
            ImGui::Text(" (linear: %s)", ValueTypeToString(Default, 3).c_str() );
        }
        else
        {
            switch( (ValueTypeIndex)Default.index() )
            {
                case( ValueTypeIndex::Bool    ): { int val = std::get<bool>(Default);               ImGui::Combo( "Default", &val, "true\0false\0" );   Default = val != 0; } break;
                case( ValueTypeIndex::Integer ): { int32 val = std::get<int32>(Default);            ImGui::InputInt( "Default", &val );                 Default = val;      } break;
                case( ValueTypeIndex::Scalar  ): { float val = std::get<float>(Default);            ImGui::InputFloat( "Default", &val, 0, 0, "%.3f" ); Default = val;      } break;
                case( ValueTypeIndex::Vector3 ): { vaVector3 val = std::get<vaVector3>(Default);    ImGui::InputFloat3( "Default", &val.x, "%.3f" );    Default = val;      } break;
                case( ValueTypeIndex::Vector4 ): { vaVector4 val = std::get<vaVector4>(Default);    ImGui::InputFloat4( "Default", &val.x, "%.3f" );    Default = val;      } break;
            default: assert( false ); break;
            }
        }

        inputsChanged |= prevDefault != Default;

        inputsChanged |= ImGui::Checkbox( "Use Value as multiplier", &IsMultiplier );
        if( ImGui::IsItemHovered( ) ) ImGui::SetTooltip( "If set and input node (such as texture) is connected, \nthe Value will be used to multiply it after loading; \notherwise only the input node value is used." );

        ImGui::Text( "Min:     %s\n", ValueTypeToString( Min, 3 ).c_str( ) );
        ImGui::Text( "Max:     %s\n", ValueTypeToString( Max, 3 ).c_str( ) );

        inputsChanged |= ClampMinMax( Default );

        ImGui::Unindent( indentSize );
    }
    else
    {
        if( ImGui::IsItemHovered( ) )
        {
            string toolTipText;
            toolTipText += vaStringTools::Format( "Default: %s\n", ValueTypeToString( Default, 3 ).c_str() );
            toolTipText += vaStringTools::Format( "Min:     %s\n", ValueTypeToString( Min, 3 ).c_str( ) );
            toolTipText += vaStringTools::Format( "Max:     %s\n", ValueTypeToString( Max, 3 ).c_str( ) );
            const char * isColorTxt = IsColor?"true":"false";
            toolTipText += vaStringTools::Format( "IsColor: %s\n", isColorTxt );

            ImGui::SetTooltip( toolTipText.c_str( ) );
        }
    }

    return inputsChanged;
}

string vaRenderMaterial::Node::GetShaderMaterialInputsType( ) const
{
    return ValueTypeIndexToHLSL( Type );
}

vaRenderMaterial::Node::Node( const string& name, const ValueTypeIndex& type ) 
    : Name( name ), Type( type ) 
{ 
}

vaRenderMaterial::TextureNode::TextureNode( const string& name, const vaGUID& textureUID, vaStandardSamplerType samplerType, int uvIndex ) 
    : Node( SanitizeInputSlotOrNodeName( name ), ValueTypeIndex::Vector4 ), UID( textureUID ), SamplerType( samplerType ), UVIndex( uvIndex ) 
{ 
}

vaRenderMaterial::TextureNode::TextureNode( const string& name, const vaTexture& texture, vaStandardSamplerType samplerType, int uvIndex ) 
    : Node( SanitizeInputSlotOrNodeName( name ), ValueTypeIndex::Vector4 ), UID( texture.UIDObject_GetUID( ) ), SamplerType( samplerType ), UVIndex( uvIndex ) 
{
}

vaRenderMaterial::TextureNode::TextureNode( const TextureNode& copy ) 
    : Node( SanitizeInputSlotOrNodeName( copy.Name ), ValueTypeIndex::Vector4 ), UID( copy.UID ), SamplerType( copy.SamplerType ), UVIndex( copy.UVIndex ) 
{
}

vaRenderMaterial::InputSlot::InputSlot( const InputSlot & copy ) : Name( copy.Name ), Properties( copy.Properties ), ConnectedInput( copy.ConnectedInput )
{
    memcpy( InputSwizzle, copy.InputSwizzle, sizeof(InputSwizzle) );
}

vaRenderMaterial::InputSlot::InputSlot( const string & name, const vaRenderMaterial::ValueProperties & properties ) 
    : Name( SanitizeInputSlotOrNodeName( name ) ), Properties( properties ) 
{ 
    switch( properties.Default.index() )
    {
    case( (int32)vaRenderMaterial::ValueTypeIndex::Bool ):       StringToSwizzle( InputSwizzle, "x" );     break;
    case( (int32)vaRenderMaterial::ValueTypeIndex::Integer ):    StringToSwizzle( InputSwizzle, "x" );     break;
    case( (int32)vaRenderMaterial::ValueTypeIndex::Scalar ):     StringToSwizzle( InputSwizzle, "x" );     break;
    case( (int32)vaRenderMaterial::ValueTypeIndex::Vector3 ):    StringToSwizzle( InputSwizzle, "xyz" );   break;
    case( (int32)vaRenderMaterial::ValueTypeIndex::Vector4 ):    StringToSwizzle( InputSwizzle, "xyzw" );  break;
    default: assert( false ); StringToSwizzle( InputSwizzle, "xyzw" );  break;
    };
    SanitizeSwizzle( InputSwizzle, (ValueTypeIndex)properties.Default.index() );
}

string vaRenderMaterial::InputSlot::GetShaderMaterialInputsType( ) const
{
    return ValueTypeIndexToHLSL( ( ValueTypeIndex )Properties.Default.index() );
}

string vaRenderMaterial::TextureNode::GetShaderMaterialInputLoader( ) const
{
    // WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
    // If changing this function to rely on other vaTexture parameters, make sure to follow the same
    // existing logic as needed for RequiresReUpdate
    assert( ComputedShaderTextureSlot != -1 );
    if( ComputedShaderTextureSlot == -1 )
    {
        assert( false );
        return "";
    }

    vaFramePtr<vaTexture> texture = GetTextureFP( );

    assert( texture != nullptr );
    if( texture == nullptr )
    {
        assert( false );
        VA_LOG_ERROR( "vaRenderMaterial::TextureNode::GetShaderMaterialInputLoader - texture slot not unused but texture is null?" );
        return "float4(0,0,0,0)";
    }

    string samplerName = vaStandardSamplerTypeToShaderName( this->SamplerType );

    string textureBindlessIndex = TextureSlotToHLSLVariableName( this->ComputedShaderTextureSlot );

    // // Accessors
    // string UVAccessor;
    // switch( this->UVIndex )
    // {
    // case( 0 ): UVAccessor = "Texcoord01.xy"; break;
    // case( 1 ): UVAccessor = "Texcoord01.zw"; break;
    // case( 2 ): UVAccessor = "Texcoord23.xy"; break;
    // case( 3 ): UVAccessor = "Texcoord23.zw"; break;
    // default: assert( false ); break;
    // }

    //string accessorFunction = vaStringTools::Format( "%s.SampleBias( %s, surface.%s, g_globals.GlobalMIPOffset )", shaderTextureName.c_str( ), samplerName.c_str( ), UVAccessor.c_str( ) );
    //string accessorFunction = vaStringTools::Format( "RMSampleTexture2D( %s, %s, surface.%s )", textureBindlessIndex.c_str( ), samplerName.c_str( ), UVAccessor.c_str( ) );
    string accessorFunction = vaStringTools::Format( "RMSampleTexture2D( surface, %s, %s, %d )", textureBindlessIndex.c_str( ), samplerName.c_str( ), this->UVIndex );

    // add normalmap unpacking if needed
    switch( texture->GetContentsType() )
    {
    case Vanilla::vaTextureContentsType::NormalsXYZ_UNORM:         accessorFunction = vaStringTools::Format( "float4( NormalDecode_XYZ_UNORM(%s.xyz)   , 0)", accessorFunction.c_str( ) ); break;
    case Vanilla::vaTextureContentsType::NormalsXY_UNORM:          accessorFunction = vaStringTools::Format( "float4( NormalDecode_XY_UNORM(%s.xy)     , 0)", accessorFunction.c_str( ) ); break;
    case Vanilla::vaTextureContentsType::NormalsWY_UNORM:          accessorFunction = vaStringTools::Format( "float4( NormalDecode_WY_UNORM(%s.xyzw)   , 0)", accessorFunction.c_str( ) ); break;
    case Vanilla::vaTextureContentsType::NormalsXY_LAEA_ENCODED:   accessorFunction = vaStringTools::Format( "float4( NormalDecode_XY_LAEA(%s.xy)      , 0)", accessorFunction.c_str( ) ); break;
    }
    LastTextureContentsType = texture->GetContentsType();

    return accessorFunction;
}

bool vaRenderMaterial::ValueProperties::Serialize( vaXMLSerializer & serializer )
{
    bool allOk = true;
    if( !::Serialize( serializer, "Value", this->Default ) )
        allOk &= ::Serialize( serializer, "Default", this->Default );
    allOk &= ::Serialize( serializer, "Min", this->Min );
    allOk &= ::Serialize( serializer, "Max", this->Max );
    allOk &= serializer.Serialize<bool>( "IsColor", this->IsColor );
    /*allOk &=*/ serializer.Serialize<bool>( "ValueIsMultiplier", this->IsMultiplier, false );
    assert( allOk );
    return allOk;
}

bool vaRenderMaterial::Node::Serialize( vaXMLSerializer& serializer )
{
    bool allOk = true;
    allOk = serializer.Serialize<string>( "Name", this->Name );
    allOk = serializer.Serialize<int32>( "Type", (int32&)this->Type );
    if( serializer.IsReading( ) )
    {
        this->InUse = false;
    }
    return allOk;
}

bool vaRenderMaterial::TextureNode::Serialize( vaXMLSerializer& serializer )
{
    bool allOk = Node::Serialize( serializer );
    allOk = serializer.Serialize<vaGUID>( "UID", UID, vaGUID::Null );
    allOk = serializer.Serialize<int32>( "UVIndex", UVIndex );
    allOk = serializer.Serialize<int32>( "SamplerType", (int32&)SamplerType );
    if( serializer.IsReading( ) )
    {
        this->ComputedShaderTextureSlot = -1;
    }
    return allOk;
}

bool vaRenderMaterial::InputSlot::Serialize( vaXMLSerializer& serializer )
{
    bool allOk = true;
    allOk &= serializer.Serialize( "Name", Name );                      assert( allOk );
    allOk &= serializer.Serialize( "ConnectedInput", ConnectedInput );  assert( allOk );
    allOk &= serializer.Serialize( "Properties", Properties );          assert( allOk );

    string inputSwizzle = "";
    if( serializer.IsWriting() )
        inputSwizzle = SwizzleToString( InputSwizzle );
    allOk &= serializer.Serialize( "InputSwizzle", inputSwizzle );      assert( allOk );
    if( serializer.IsReading( ) )
    {
        StringToSwizzle( InputSwizzle, inputSwizzle );

        //let's not sanitize here since we don't know the source type in case source nodes have not been loaded yet
        //SanitizeSwizzle( InputSwizzle, (ValueTypeIndex)Properties.Default.index() );
    }

    if( serializer.IsReading() )
    {
        CachedConnectedInput.reset();

        assert( IsValidSwizzle(InputSwizzle) );
        assert( Name == SanitizeInputSlotOrNodeName(Name) );
    }

    assert( allOk );
    return allOk;

}

void vaRenderMaterial::EnumerateUsedAssets( const std::function<void( vaAsset * asset )> & callback )
{
    callback( GetParentAsset() );
    for( int i = 0; i < m_nodes.size( ); i++ )
    {
        auto snode = std::dynamic_pointer_cast<TextureNode, Node>( m_nodes[i] );
        if( snode != nullptr && !snode->GetTextureUID( ).IsNull() )
        {
            auto texture = snode->GetTextureFP( );
            if( texture != nullptr )
            {
                callback( texture->GetParentAsset() );
            }
            else
            {
                // Either ReconnectDependencies( ) was not called, or the asset is missing?
                assert( false );
            }
        }
    }
}

vaShadingRate vaRenderMaterial::ComputeShadingRate( int baseShadingRate ) const
{
    baseShadingRate += m_materialSettings.VRSRateOffset;
    baseShadingRate = vaMath::Clamp( baseShadingRate, 0, 4 );
    switch( baseShadingRate )
    {
    case( 0 ):    return vaShadingRate::ShadingRate1X1;
    case( 1 ):    return ( m_materialSettings.VRSPreferHorizontal ) ? ( vaShadingRate::ShadingRate2X1 ) : ( vaShadingRate::ShadingRate1X2 );
    case( 2 ):    return vaShadingRate::ShadingRate2X2;
    case( 3 ):    return ( m_materialSettings.VRSPreferHorizontal ) ? ( vaShadingRate::ShadingRate4X2 ) : ( vaShadingRate::ShadingRate2X4 );
    case( 4 ):    return vaShadingRate::ShadingRate4X4;
    default: assert( false );
        break;
    }
    return vaShadingRate::ShadingRate1X1;
}

void vaRenderMaterialManager::RegisterSerializationTypeConstructors( vaXMLSerializer & serializer )
{
    serializer.RegisterTypeConstructor( "TextureNode", [] { return std::make_shared<vaRenderMaterial::TextureNode>(); } );
}