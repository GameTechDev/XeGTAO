///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaFileTools.h"
#include "vaFileStream.h"
#include "Core/vaMemory.h"
#include "Core/System/vaMemoryStream.h"
#include "Core/vaLog.h"

#include "Core/vaStringTools.h"

#include <stdio.h>

#include "EmbeddedMedia.inc"


using namespace Vanilla;

#pragma warning ( disable: 4996 )

std::map<string, vaFileTools::EmbeddedFileData> vaFileTools::s_EmbeddedFiles;

bool vaFileTools::FileExists( const wstring & path )
{
    FILE * fp = _wfopen( path.c_str( ), L"rb" );
    if( fp != NULL )
    {
        fclose( fp );
        return true;
    }
    return false;
}

bool vaFileTools::DeleteFile( const wstring & path )
{
    int ret = ::_wremove( path.c_str( ) );
    return ret == 0;
}

bool vaFileTools::MoveFile( const wstring & oldPath, const wstring & newPath )
{
    int ret = ::_wrename( oldPath.c_str( ), newPath.c_str( ) );
    return ret == 0;
}

std::shared_ptr<vaMemoryStream> vaFileTools::LoadMemoryStream( const string & fileName )
{
    return LoadMemoryStream( vaStringTools::SimpleWiden( fileName ) );
}

std::shared_ptr<vaMemoryStream> vaFileTools::LoadMemoryStream( const wstring & fileName )
{
    vaFileStream file;
    if( !file.Open( fileName, FileCreationMode::Open ) )
        return std::shared_ptr<vaMemoryStream>( nullptr );

    int64 length = file.GetLength( );

    if( length == 0 )
        return std::shared_ptr<vaMemoryStream>( nullptr );

    std::shared_ptr<vaMemoryStream> ret( new vaMemoryStream( length ) );

    if( !file.Read( ret->GetBuffer( ), ret->GetLength( ) ) )
        return std::shared_ptr<vaMemoryStream>( nullptr );

    return ret;
}

string vaFileTools::ReadText( const wstring& fileName )
{
    auto stream = LoadMemoryStream( fileName );
    if( stream == nullptr )
        return "";
    string retVal = "";
    if( stream->ReadTXT( retVal ) )
        return retVal;
    return "";
}

wstring vaFileTools::CleanupPath( const wstring & inputPath, bool convertToLowercase, bool useBackslash )
{
    wstring ret = inputPath;
    if( convertToLowercase )
        ret = vaStringTools::ToLower( ret );

    wstring::size_type foundPos;
    while( ( foundPos = ret.find( L"/" ) ) != wstring::npos )
        ret.replace( foundPos, 1, L"\\" );
    while( ( foundPos = ret.find( L"\\\\" ) ) != wstring::npos )
        ret.replace( foundPos, 2, L"\\" );

    // restore network path
    if( ( ret.length( ) > 0 ) && ( ret[0] == '\\' ) )
        ret = L"\\" + ret;

    // remove relative '..\' ("A\B\..\C" -> "A\C")
    std::vector<wstring> parts; foundPos = 0; wstring::size_type prevSep = 0;
    while( ( foundPos = ret.find( L"\\", foundPos ) ) != wstring::npos )
    {
        foundPos++;
        wstring thisPart = ret.substr( prevSep, foundPos-prevSep );
        if( parts.size() > 0  && thisPart == L"..\\" )
            parts.pop_back();
        else
            parts.push_back( thisPart );
        prevSep = foundPos;
    }
    parts.push_back( ret.substr( prevSep ) );

    ret = L"";
    for( const auto & part : parts )
        ret += part;

    if( !useBackslash )
        while( ( foundPos = ret.find( L"\\" ) ) != wstring::npos )
            ret.replace( foundPos, 1, L"/" );



    return ret;
}

wstring vaFileTools::GetAbsolutePath( const wstring & path )
{
#define BUFSIZE 32770
    DWORD  retval = 0;
    TCHAR  buffer[BUFSIZE] = TEXT( "" );
    TCHAR** lppPart = { NULL };

    wstring lpp = L"\\\\?\\";
    wstring longPath = lpp + path;

    // Retrieve the full path name for a file. 
    // The file does not need to exist.
    retval = GetFullPathName( longPath.c_str(), BUFSIZE, buffer, lppPart );

    if( retval == 0 )
    {
        VA_ASSERT_ALWAYS( L"Failed getting absolute path to '%s'", path.c_str( ) );
        return L"";
    }
    else
    {
        wstring absolutePath = buffer;
        auto lpti = absolutePath.find( lpp );
        if( lpti != wstring::npos )
            absolutePath = absolutePath.substr( lpti + lpp.length() );
        return absolutePath;
    }
}

static void FindFilesRecursive( const wstring & startDirectory, const wstring & searchName, bool recursive, std::vector<wstring> & outResult )
{
    WIN32_FIND_DATA FindFileData;
    HANDLE hFind = INVALID_HANDLE_VALUE;

    wstring combined = startDirectory + searchName;

    hFind = FindFirstFileExW( combined.c_str( ), FindExInfoBasic, &FindFileData, FindExSearchNameMatch, NULL, 0 );
    bool bFound = hFind != INVALID_HANDLE_VALUE;
    while( bFound )
    {
        if( //( ( FindFileData.dwFileAttributes & (FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NORMAL) ) != 0 ) &&
             ( ( FindFileData.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_VIRTUAL) ) == 0 ) )
            outResult.push_back( startDirectory + FindFileData.cFileName );

        bFound = FindNextFileW( hFind, &FindFileData ) != 0;
    }

    if( recursive )
    {
        combined = startDirectory + L"*";

        FindClose( hFind );
        hFind = FindFirstFileExW( combined.c_str( ), FindExInfoBasic, &FindFileData, FindExSearchLimitToDirectories, NULL, 0 );
        bFound = hFind != INVALID_HANDLE_VALUE;
        while( bFound )
        {
            if( ( FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0 )
            {
                wstring subDir = FindFileData.cFileName;
                if( subDir != L"." && subDir != L".." )
                {
                    FindFilesRecursive( startDirectory + subDir + L"\\", searchName, recursive, outResult );
                }
            }

            bFound = FindNextFileW( hFind, &FindFileData ) != 0;
        }
    }
    if( INVALID_HANDLE_VALUE != hFind )
        FindClose( hFind );
}

std::vector<wstring> vaFileTools::FindFiles( const wstring & startDirectory, const wstring & searchName, bool recursive )
{
    std::vector<wstring> result;

    FindFilesRecursive( startDirectory, searchName, recursive, result );

    return result;
}

std::vector<wstring> vaFileTools::FindDirectories( const wstring & startDirectory )
{
    std::vector<wstring> result;

    WIN32_FIND_DATA FindFileData;
    HANDLE hFind = INVALID_HANDLE_VALUE;

    wstring combined = startDirectory + L"*";

    FindClose( hFind );
    hFind = FindFirstFileExW( combined.c_str( ), FindExInfoBasic, &FindFileData, FindExSearchLimitToDirectories, NULL, 0 );
    bool bFound = hFind != INVALID_HANDLE_VALUE;
    while( bFound )
    {
        if( ( FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0 )
        {
            wstring subDir = FindFileData.cFileName;
            if( subDir != L"." && subDir != L".." )
            {
                result.push_back( startDirectory + FindFileData.cFileName );
            }
        }
        bFound = FindNextFileW( hFind, &FindFileData ) != 0;
    }

    if( INVALID_HANDLE_VALUE != hFind )
        FindClose( hFind );
    return result;
}

void                      vaFileTools::EmbeddedFilesRegister( const string & _pathName, byte * data, int64 dataSize, int64 timeStamp )
{
    // case insensitive
    string pathName = vaStringTools::ToLower( _pathName );

    std::map<string, EmbeddedFileData>::iterator it = s_EmbeddedFiles.find( pathName );

    if( it != s_EmbeddedFiles.end( ) )
    {
        VA_WARN( "Embedded file %s already registered!", pathName.c_str( ) );
        return;
    }

    s_EmbeddedFiles.insert( std::pair<string, EmbeddedFileData>( pathName,
        EmbeddedFileData( pathName, std::shared_ptr<vaMemoryStream>( new vaMemoryStream( data, dataSize ) ), timeStamp ) ) );
}

vaFileTools::EmbeddedFileData vaFileTools::EmbeddedFilesFind( const string & _pathName )
{
    // case insensitive
    string pathName = CleanupPath( _pathName, true );

    std::map<string, EmbeddedFileData>::iterator it = s_EmbeddedFiles.find( pathName );

    if( it != s_EmbeddedFiles.end( ) )
        return it->second;

    return EmbeddedFileData( );
}

void vaFileTools::Initialize( )
{
    vaTimerLogScope timeThis( "Loading embedded data" );
    for( int i = 0; i < BINARY_EMBEDDER_ITEM_COUNT; i++ )
    {
        wchar_t* name = BINARY_EMBEDDER_NAMES[i];
        unsigned char* data = BINARY_EMBEDDER_DATAS[i];
        int64 dataSize = BINARY_EMBEDDER_SIZES[i];
        int64 timeStamp = BINARY_EMBEDDER_TIMES[i];

        vaFileTools::EmbeddedFilesRegister( vaStringTools::SimpleNarrow( name ), data, dataSize, timeStamp );
    }
}

void vaFileTools::Deinitialize( )
{
    for( std::map<string, EmbeddedFileData>::iterator it = s_EmbeddedFiles.begin( ); it != s_EmbeddedFiles.end( ); ++it )
    {
        VA_ASSERT( it->second.MemStream.unique( ), "Embedded file %s reference count not 0, stream not closed but storage no longer guaranteed!", it->first.c_str( ) );
    }

    s_EmbeddedFiles.clear( );
}

bool vaFileTools::ReadBuffer( const wstring & filePath, void * buffer, size_t size )
{
    vaFileStream file;
    if( !file.Open( filePath, FileCreationMode::Open ) )
        return false;

    return file.Read( buffer, size );
}

bool vaFileTools::WriteBuffer( const wstring & filePath, void * buffer, size_t size )
{
    vaFileStream file;
    if( !file.Open( filePath, FileCreationMode::Create ) )
        return false;

    return file.Write( buffer, size );
}

bool vaFileTools::ReadBuffer( const string & filePath, void * buffer, size_t size )
{
    vaFileStream file;
    if( !file.Open( filePath, FileCreationMode::Open ) )
        return false;

    return file.Read( buffer, size );
}

bool vaFileTools::WriteBuffer( const string & filePath, void * buffer, size_t size )
{
    vaFileStream file;
    if( !file.Open( filePath, FileCreationMode::Create ) )
        return false;

    return file.Write( buffer, size );
}

void vaFileTools::SplitPath( const string & inFullPath, string * outDirectory, string * outFileName, string * outFileExt )
{
    char buffDrive[32];
    char buffDir[4096];
    char buffName[4096];
    char buffExt[4096];

    _splitpath_s( inFullPath.c_str( ), buffDrive, _countof( buffDrive ),
        buffDir, _countof( buffDir ), buffName, _countof( buffName ), buffExt, _countof( buffExt ) );

    if( outDirectory != NULL ) *outDirectory = string( buffDrive ) + string( buffDir );
    if( outFileName != NULL )  *outFileName = buffName;
    if( outFileExt != NULL )   *outFileExt = buffExt;
}

void vaFileTools::SplitPath( const wstring & inFullPath, wstring * outDirectory, wstring * outFileName, wstring * outFileExt )
{
    wchar_t buffDrive[32];
    wchar_t buffDir[4096];
    wchar_t buffName[4096];
    wchar_t buffExt[4096];

    //assert( !((outDirectory != NULL) && ( (outDirectory != outFileName) || (outDirectory != outFileExt) )) );
    //assert( !((outFileName != NULL) && (outFileName != outFileExt)) );

    _wsplitpath_s( inFullPath.c_str( ), buffDrive, _countof( buffDrive ),
        buffDir, _countof( buffDir ), buffName, _countof( buffName ), buffExt, _countof( buffExt ) );

    if( outDirectory != NULL ) *outDirectory = wstring( buffDrive ) + wstring( buffDir );
    if( outFileName != NULL )  *outFileName = buffName;
    if( outFileExt != NULL )   *outFileExt = buffExt;
}

string  vaFileTools::SplitPathExt( const string & inFullPath )
{
    string ret;
    SplitPath( inFullPath, nullptr, nullptr, &ret );
    return ret;
}

wstring vaFileTools::SplitPathExt( const wstring & inFullPath )
{
    wstring ret;
    SplitPath( inFullPath, nullptr, nullptr, &ret );
    return ret;
}

bool vaFileTools::PathHasDirectory( const string & inFullPath )
{
    string dir;
    SplitPath( inFullPath, &dir, nullptr, nullptr );
    return dir != "";
}

wstring vaFileTools::FindLocalFile( const wstring & fileName )
{
    if( vaFileTools::FileExists( ( vaCore::GetWorkingDirectory( ) + fileName ).c_str( ) ) )
        return vaFileTools::CleanupPath( vaCore::GetWorkingDirectory( ) + fileName, false );
    if( vaFileTools::FileExists( ( vaCore::GetExecutableDirectory( ) + fileName ).c_str( ) ) )
        return vaFileTools::CleanupPath( vaCore::GetExecutableDirectory( ) + fileName, false );
    if( vaFileTools::FileExists( fileName.c_str( ) ) )
        return vaFileTools::CleanupPath( fileName, false );

    return L"";
}

wstring vaFileTools::FixExtension( const wstring& path, const wstring & _ext )
{
    wstring ext = _ext;
    if( ext.length() == 0 ) return path;
    if( ext[0] != L'.' )
    {
        assert( false );
        return L""; 
    }
    wstring currentExt;
    SplitPath( path, nullptr, nullptr, &currentExt );
    if( vaStringTools::CompareNoCase( currentExt, ext ) != 0 )
        return path + ext;
    else
        return path;
}

string vaFileTools::FixExtension( const string & path, const string & _ext )
{
    string ext = _ext;
    if( ext.length( ) == 0 ) return path;
    if( ext[0] != L'.' )
    {
        assert( false );
        return "";
    }
    string currentExt;
    SplitPath( path, nullptr, nullptr, &currentExt );
    if( vaStringTools::CompareNoCase( currentExt, ext ) != 0 )
        return path + ext;
    else
        return path;
}

string vaFileTools::SelectFolderDialog( const string & initialDir )
{
    return vaStringTools::SimpleNarrow( SelectFolderDialog( vaStringTools::SimpleWiden( initialDir ) ) );
}

void vaFileTools::OpenSystemExplorerFolder( const string & folderPath )
{
    OpenSystemExplorerFolder( vaStringTools::SimpleWiden( folderPath ) );
}

std::vector<string> vaFileTools::FindFiles( const string & startDirectory, const string & searchName, bool recursive )
{
    std::vector<string> ret;
    auto list = FindFiles( vaStringTools::SimpleWiden(startDirectory), vaStringTools::SimpleWiden(searchName), recursive );
    for( auto & item : list )
        ret.push_back( vaStringTools::SimpleNarrow( item ) );
    return ret;
}

std::vector<string> vaFileTools::FindDirectories( const string & startDirectory )
{
    std::vector<string> ret;
    auto list = FindDirectories( vaStringTools::SimpleWiden( startDirectory ) );
    for( auto& item : list )
        ret.push_back( vaStringTools::SimpleNarrow( item ) );
    return ret;
}

bool vaFileTools::WriteText( const string & filePath, const string & text )
{
    // Todo: standardize to UTF-8
    return WriteBuffer( filePath, (void*)text.data(), text.size() );
}
