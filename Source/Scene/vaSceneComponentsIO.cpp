///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Core/vaCoreIncludes.h"
#include "vaSceneComponents.h"

#include "Core/vaXMLSerialization.h"
#include "Core/vaSerializer.h"

#include "Core/System/vaMemoryStream.h"

using namespace Vanilla;

using namespace Vanilla::Scene;

bool DestroyTag::Serialize( vaSerializer & ) 
{ 
    assert( false ); return false;  // This component should never reach serialization
}

// bool WorldBoundsDirtyTag::Serialize( vaSerializer& )
// {
//     // assert( false ); return false;  // This component should never reach serialization
//     return true;
// }

// bool TransformDirtyTag::Serialize( vaSerializer & ) 
// { 
//     return true;
// }

// just a presence of Serialize will ensure the empty (tag) component will get created.
bool TransformLocalIsWorldTag::Serialize( vaSerializer & )
{ 
    return true;
}    

bool TransformLocal::Serialize( vaSerializer & serializer )
{ 
    return serializer.Serialize( "", static_cast<vaMatrix4x4&>(*this) );
}

bool TransformWorld::Serialize( vaSerializer & serializer )
{
    return serializer.Serialize( "", static_cast<vaMatrix4x4&>( *this ) );
}

// TODO: remove this in the future
bool IBLProbe::Serialize( Vanilla::vaXMLSerializer & serializer )
{
    if( serializer.IsReading( ) )
    {
        *this = IBLProbe( );
    }

    /*VERIFY_TRUE_RETURN_ON_FALSE*/( serializer.Serialize<vaVector3>( "Position", Position ) );
    /*VERIFY_TRUE_RETURN_ON_FALSE*/( serializer.Serialize<float>( "ClipNear", ClipNear ) );
    /*VERIFY_TRUE_RETURN_ON_FALSE*/( serializer.Serialize<float>( "ClipFar", ClipFar ) );
    /*VERIFY_TRUE_RETURN_ON_FALSE*/( serializer.Serialize<vaOrientedBoundingBox>( "GeometryProxy", GeometryProxy ) );
    /*VERIFY_TRUE_RETURN_ON_FALSE*/( serializer.Serialize<vaOrientedBoundingBox>( "FadeOutProxy", FadeOutProxy, GeometryProxy ) );
    /*VERIFY_TRUE_RETURN_ON_FALSE*/( serializer.Serialize<bool>( "UseGeometryProxy", UseGeometryProxy ) );
    /*VERIFY_TRUE_RETURN_ON_FALSE*/( serializer.Serialize<vaVector3>( "AmbientColor", AmbientColor ) );
    /*VERIFY_TRUE_RETURN_ON_FALSE*/( serializer.Serialize<float>( "AmbientColorIntensity", AmbientColorIntensity ) );
    /*VERIFY_TRUE_RETURN_ON_FALSE*/( serializer.Serialize<string>( "ImportFilePath", ImportFilePath ) );
    /*VERIFY_TRUE_RETURN_ON_FALSE*/( serializer.Serialize<bool>( "Enabled", Enabled ) );

    return true;
}

bool IBLProbe::Serialize( Vanilla::vaSerializer & serializer )
{
    if( serializer.IsReading( ) )
    {
        *this = IBLProbe( );
    }

    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaVector3>( "Position", Position ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float>( "ClipNear", ClipNear ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float>( "ClipFar", ClipFar ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaOrientedBoundingBox>( "GeometryProxy", GeometryProxy ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaOrientedBoundingBox>( "FadeOutProxy", FadeOutProxy, GeometryProxy ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<bool>( "UseGeometryProxy", UseGeometryProxy ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaVector3>( "AmbientColor", AmbientColor ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float>( "AmbientColorIntensity", AmbientColorIntensity ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<string>( "ImportFilePath", ImportFilePath ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<bool>( "Enabled", Enabled ) );

    return true;
}

// bool WorldBounds::Serialize( vaSerializer & serializer )
// {
//     VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaBoundingBox>( "BoundingBox", AABB ) );
//     VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaBoundingSphere>( "BoundingSphere", BS ) );
//     
//     return true;
// }

bool CustomBoundingBox::Serialize( vaSerializer& serializer )
{
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaBoundingBox>( "BoundingBox", *this ) );

    return true;
}

bool LightBase::Serialize( vaSerializer & serializer )
{
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaVector3>( "Color",      Color         ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float    >( "Intensity",  Intensity     ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float    >( "FadeFactor", FadeFactor    ) );

    return true;
}

bool LightAmbient::Serialize( vaSerializer & serializer )
{
    VERIFY_TRUE_RETURN_ON_FALSE( LightBase::Serialize( serializer ) );
    
    return true;
}

// bool LightDirectional::Serialize( vaSerializer & serializer )
// {
//     VERIFY_TRUE_RETURN_ON_FALSE( LightBase::Serialize( serializer ) );
// 
//     VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float>( "AngularRadius", AngularRadius ) );
//     VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float>( "HaloSize"     , HaloSize      ) );
//     VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float>( "HaloFalloff"  , HaloFalloff   ) );
//     VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<bool >( "CastShadows"  , CastShadows   ) );
// 
//     return true;
// }

bool LightPoint::Serialize( vaSerializer & serializer )
{
    VERIFY_TRUE_RETURN_ON_FALSE( LightBase::Serialize( serializer ) );

    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float>( "Size"          , Size           ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float>( "RTSizeModifier", RTSizeModifier, 0.75f ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float>( "Range"         , Range          ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float>( "SpotInnerAngle", SpotInnerAngle ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float>( "SpotOuterAngle", SpotOuterAngle ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<bool >( "CastShadows"   , CastShadows    ) );

    return true;
}

bool MaterialPicksLightEmissive::Serialize( vaSerializer & serializer )
{
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float>( "IntensityMultiplier" , IntensityMultiplier ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float>( "OriginalMultiplier"  , OriginalMultiplier  ) );

    return true;
}

bool RenderMesh::Serialize( vaSerializer & serializer )
{
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaGUID> ( "MeshUID"            , MeshUID             ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaGUID> ( "OverrideMaterialUID", OverrideMaterialUID ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float > ( "VisibilityRange"    , VisibilityRange     ) );

    return true;
}

bool RenderCamera::Serialize( vaSerializer & serializer )
{
    // temporary

    string dataBase64;
    if( serializer.IsWriting( ) )
        dataBase64 = (Data!=nullptr)?(vaStringTools::Base64Encode(Data->GetBuffer(), Data->GetLength()) ):("");
    serializer.Serialize<string>( "DataBase64", dataBase64 );
    if( serializer.IsReading( ) )
        Data = vaStringTools::Base64Decode( dataBase64 );
    
    return true;
}

bool FogSphere::Serialize( vaSerializer & serializer )
{
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaVector3>( "Center"          , Center          ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaVector3>( "Color"           , Color           ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float    >( "RadiusInner"     , RadiusInner     ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float    >( "RadiusOuter"     , RadiusOuter     ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float    >( "BlendCurvePow"   , BlendCurvePow   ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float    >( "BlendMultiplier" , BlendMultiplier ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<bool     >( "UseCustomCenter" , UseCustomCenter ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<bool     >( "Enabled"         , Enabled         ) );

    return true;
}

bool SkyboxTexture::Serialize( vaSerializer & serializer )
{
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<string>( "Path"           , Path            ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<vaGUID>( "UID"            , UID             ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<float >( "ColorMultiplier", ColorMultiplier ) );
    VERIFY_TRUE_RETURN_ON_FALSE( serializer.Serialize<bool  >( "Enabled"        , Enabled         ) );

    return true;
}
