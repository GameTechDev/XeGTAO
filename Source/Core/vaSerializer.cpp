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

#include "vaSerializer.h"

#include "Core/System/vaFileTools.h"

using namespace Vanilla;

vaSerializer::vaSerializer( vaSerializer && src ) :
    m_json( std::move(src.m_json) ), m_isReading( src.m_isReading ), m_isWriting( src.m_isWriting ), m_type( std::move( src.m_type ) )
{
    src.m_isReading = false;
    src.m_isWriting = false;
    assert( src.m_type == "" );
    assert( src.m_json.is_null() );
}

vaSerializer::vaSerializer( nlohmann::json && src, bool forReading ) 
    : m_json( std::move( src ) ) 
{ 
    if( forReading )
    {
        m_isReading = true; // !m_json.is_null(); <- actually null is fine
        if( m_json.is_null() )
            return;
        if( m_json.contains( "!type" ) )
            m_json["!type"].get_to<std::string>( m_type );
    }
    else
    {
        m_isWriting = true;
    }
}

vaSerializer::vaSerializer( const string & type ) : m_isReading( false ), m_isWriting( true ), m_type( type )
{
    if( type != "" )
        m_json["!type"]      = type;
}

vaSerializer::~vaSerializer( )
{
    if( IsWriting( ) )
    {
        if( m_type != "" )
        {
            // if these asserts fired, it means you've probably overwritten/changed the "!type" key, perhaps by writing to the json as if it were a 'value' type
            assert( m_json.contains("!type") );
            assert( m_json["!type"] == m_type );
        }
    }
}

vaSerializer vaSerializer::OpenReadFile( const string & filePath, const string & assertType )
{
    json j = json::parse( vaFileTools::ReadText( filePath ), nullptr, false );
    if( j == "" )
    {
        //VA_WARN( "vaSerializer::OpenReadFile( %s ): unable to read file or json was unable to parse inputs", filePath.c_str() );
        return vaSerializer( );
    }
    
    vaSerializer retVal( std::move(j), true );
    assert( assertType == "" || assertType == retVal.Type() ); assertType;
    return retVal;
}

vaSerializer vaSerializer::OpenReadString( const string & jsonData, const string & assertType )
{
    json j = json::parse( jsonData, nullptr, false );
    if( j == "" )
    {
        //VA_WARN( "vaSerializer::OpenReadString( ): json unable to parse inputs." );
        return vaSerializer( );
    }

    vaSerializer retVal( std::move(j), true );
    assert( assertType == "" || assertType == retVal.Type() ); assertType;
    return retVal;
}

string vaSerializer::Dump( ) const
{
    return m_json.dump(4, ' ');
}

bool vaSerializer::Write( vaStream & stream ) const
{
    string dump = Dump();
    return stream.Write( dump.data(), dump.size() );
}

bool vaSerializer::Write( const string & filePath ) const
{
    assert( m_isWriting );
    vaFileStream outFile;
    if( !outFile.Open( filePath, FileCreationMode::Create ) )
    {
        VA_LOG_ERROR( "vaXMLSerializer::WriterSaveToFile(%s) - unable to create file for saving", filePath.c_str( ) );
        return false;
    }
    return Write( outFile );

}

////////////////////////////////////////////////////////////////////////////////////////////////
// vaSerializer adapters!
////////////////////////////////////////////////////////////////////////////////////////////////

namespace Vanilla
{

    std::string      S_ValueToString( const float & value )
    {
        return vaStringTools::Format( "%f", value );
    }
    bool             S_StringToValue( const std::string & str, float & value )
    {
        return sscanf_s( str.c_str( ), "%f", &value ) == 1;
    }

    std::string      S_ValueToString( const double & value )
    {
        return vaStringTools::Format( "%f", value );
    }
    bool             S_StringToValue( const std::string & str, double & value )
    {
        return sscanf_s( str.c_str( ), "%Lf", &value ) == 1;
    }

    std::string      S_ValueToString( const vaVector3 & value )
    {
        return vaStringTools::Format( "%f,%f,%f", value.x, value.y, value.z );
    }
    bool             S_StringToValue( const std::string & str, vaVector3 & value )
    {
        return sscanf_s( str.c_str( ), "%f,%f,%f", &value.x, &value.y, &value.z ) == 3;
    }

    std::string      S_ValueToString( const vaVector4 & value )
    {
        return vaStringTools::Format( "%f,%f,%f,%f", value.x, value.y, value.z, value.w );
    }
    bool             S_StringToValue( const std::string & str, vaVector4 & value )
    {
        return sscanf_s( str.c_str( ), "%f,%f,%f,%f", &value.x, &value.y, &value.z, &value.w ) == 3;
    }

    std::string      S_ValueToString( const vaMatrix3x3 & value )
    {
        return vaStringTools::Format( "%f,%f,%f,%f,%f,%f,%f,%f,%f",
            value._11, value._12, value._13,
            value._21, value._22, value._23,
            value._31, value._32, value._33 );
    }
    bool             S_StringToValue( const std::string & str, vaMatrix3x3 & value )
    {
        return sscanf_s( str.c_str( ), "%f,%f,%f,%f,%f,%f,%f,%f,%f",
            &value._11, &value._12, &value._13,
            &value._21, &value._22, &value._23,
            &value._31, &value._32, &value._33 ) == 9;
    }

    std::string      S_ValueToString( const vaMatrix4x4 & value )
    {
        return vaStringTools::Format( "%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f", 
            value._11, value._12, value._13, value._14,
            value._21, value._22, value._23, value._24,
            value._31, value._32, value._33, value._34,
            value._41, value._42, value._43, value._44 );
    }
    bool             S_StringToValue( const std::string & str, vaMatrix4x4 & value )
    {
        return sscanf_s( str.c_str( ), "%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f",
            &value._11, &value._12, &value._13, &value._14,
            &value._21, &value._22, &value._23, &value._24,
            &value._31, &value._32, &value._33, &value._34,
            &value._41, &value._42, &value._43, &value._44 ) == 16;
    }

}