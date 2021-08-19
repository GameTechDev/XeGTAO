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

#include "vaCore.h"

#include <unordered_map>
#include <algorithm>

namespace Vanilla
{
    class vaMemoryStream;

    // Used to avoid frequent memory allocations for strings when string repeat very often: you call MapName and you 
    // get a const char * (MappedName) to a permanently stored string. They are, at the moment, permanently stored - 
    // but this could be avoided with some kind of reference/age tracking. This would however at least double the 
    // storage requirements, or increase indirection so maybe not needed for now.
    // If storage is important, the way I'd do it is, instead of storing m_string as a const char *, just store
    // the ptr to the 'master' structure in the dictionary which would have a refcount, string itself, etc.
    // But that means addref/release on a ptr elsewhere on every copy/destruction.
    class vaMappedString
    {
        const char*         m_string   = nullptr;

    private:
        friend class vaStringDictionary;
        vaMappedString( const char* str ) : m_string( str ) { }
    public:
        vaMappedString( ) { }

        //vaMappedString & operator = ( const vaMappedString & str ) { m_string }

    public:
        operator const char* ( ) const { return m_string; }
    };

    class vaStringDictionary
    {
        std::unordered_map<string_view, shared_ptr<string>>
            m_dictionary;

    private:
        template<typename StringType>
        vaMappedString MapInternal( StringType str )
        {
            auto it = m_dictionary.find( str );
            if( it == m_dictionary.end( ) )
            {
                pair<string_view, shared_ptr<string>> entry;
                entry.second = std::make_shared<string>( str );
                entry.first = (string_view)( *entry.second );
                it = m_dictionary.insert( entry ).first;
            }
            return it->first.data( );
        }
    public:
        vaMappedString Map( const string& str )     { return MapInternal( str ); }
        vaMappedString Map( const char* str )       { return MapInternal( str ); }

        // !! WARNING !! any instance of vaMappedString is now dangling, so use with great caution (or don't use)
        void                                        Reset( ) { std::unordered_map<string_view, shared_ptr<string>> empty; m_dictionary.swap( empty ); } // https://en.wikibooks.org/wiki/More_C%2B%2B_Idioms/Clear-and-minimize
    };

    class vaStringTools
    {
    public:

        // TODO: switch all Format methods to use variadic template version of https://github.com/c42f/tinyformat (no need to keep the C++98 version) once wstring version becomes available
        static wstring              Format( const wchar_t * fmtString, ... );
        static string               Format( const char * fmtString, ... );

//  this doesn't work - see variadic template version above
//        static wstring              Format( const wstring & fmtString, ... );
//        static string               Format( const string & fmtString, ... );

        static wstring              Format( const wchar_t * fmtString, va_list args );
        static string               Format( const char * fmtString, va_list args );

        static wstring              FormatArray( const int intArr[], int count );
        static wstring              FormatArray( const float intArr[], int count );

        static wstring              SimpleWiden( const string & s );
        static string               SimpleNarrow( const wstring & s );

        static wstring              ToLower( const wstring & str );
        static string               ToLower( const string & str );

        static wstring              ToUpper( const wstring & str );
        static string               ToUpper( const string & str );

        static bool                 IsLower( const string & str );
        static bool                 IsUpper( const string & str );
        static bool                 IsAlpha( const string & str );

        static int                  CompareNoCase( const wstring & left, const wstring & right );
        static int                  CompareNoCase( const string & left, const string & right );

        // switch / value pairs
        static std::vector< std::pair<wstring, wstring> >
                                    SplitCmdLineParams( const wstring & cmdLine );

        static wstring              Trim( const wstring & inputStr, const wchar_t * trimCharStr );
        static string               Trim( const string & inputStr, const char * trimCharStr );
        //static wstring            TrimLeft( const wchar_t * trimCharsStr );
        //static wstring            TrimRight( const wchar_t * trimCharsStr );

        static std::vector<wstring> Tokenize( const wchar_t * inputStr, const wchar_t * separatorStr, const wchar_t * trimCharsStr = NULL );
        static std::vector<string>  Tokenize( const char * inputStr, const char * separatorStr, const char * trimCharsStr = NULL );

        static float                StringToFloat( const char * inputStr );
        static float                StringToFloat( const wchar_t * inputStr );

        static wstring              FromGUID( const GUID & id );

        static void                 ReplaceAll( string & inoutStr, const string & searchStr, const string & replaceStr );

        static bool                 WriteTextFile( const wstring & filePath, const string & textData );
        static bool                 WriteTextFile( const string & filePath, const string & textData )       { return WriteTextFile( vaStringTools::SimpleWiden(filePath), textData ); }

        static string               ReplaceSpacesWithUnderscores( string text )             { std::replace( text.begin( ), text.end( ), ' ', '_' ); return text; }

        // filter format: (inc,-exc)
        static bool                 Filter( const string & filter, const string & text );

        static string               Base64Encode( const void * data, const size_t dataSize );
        static shared_ptr<vaMemoryStream>       
                                    Base64Decode( const string & base64 );

        static string               URLEncode( const string & text );
    };
}