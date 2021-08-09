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

#include "Core/vaCore.h"
#include "vaStream.h"
#include "vaMemoryStream.h"
#include "Core/vaStringTools.h"

#include <map>

namespace Vanilla
{

#ifdef DeleteFile
   #undef DeleteFile
#endif
#ifdef MoveFile
   #undef MoveFile
#endif
#ifdef FindFiles
   #undef FindFiles
#endif

   class vaMemoryBuffer;
 
   class vaFileTools
   {
   public:
      struct EmbeddedFileData
      {
         string                              Name;
         std::shared_ptr<vaMemoryStream>     MemStream;
         int64                               TimeStamp;

                                             EmbeddedFileData() : Name(""), MemStream(NULL), TimeStamp(0)  { }
                                             EmbeddedFileData( const string & name, const std::shared_ptr<vaMemoryStream> & memStream, const int64 & timeStamp ) 
                                                : Name(name), MemStream(memStream), TimeStamp(timeStamp)  { }

         bool                                HasContents( )                      { return MemStream != NULL; }
      };

   private:
      static std::map<string, EmbeddedFileData> s_EmbeddedFiles;

   public:
      // As the name says
      static bool                               FileExists( const wstring & path );
      static bool                               FileExists( const string& path )                        { return FileExists( vaStringTools::SimpleWiden( path ) ); }

      static bool								DeleteFile( const wstring & path );
      static bool								DeleteFile( const string & path )                       { return DeleteFile( vaStringTools::SimpleWiden( path ) ); }

      static bool                               DeleteDirectory( const wstring & path );
      static bool                               DeleteDirectory( const string & path )                  { return DeleteDirectory( vaStringTools::SimpleWiden(path) ); }

      static bool								MoveFile( const wstring & oldPath, const wstring & newPath );
      static bool								MoveFile( const string & oldPath, const string & newPath )  { return MoveFile( vaStringTools::SimpleWiden( oldPath ), vaStringTools::SimpleWiden( newPath ) ); }

      static bool                               DirectoryExists( const wchar_t * path );
      static bool                               DirectoryExists( const wstring & path )                 { return DirectoryExists( path.c_str() ); }

      static bool                               DirectoryExists( const string & path )                  { return DirectoryExists( vaStringTools::SimpleWiden(path) ); }

      // Converts to lowercase, removes all duplicated "\\" or "//" and converts all '/' to '\'
      // (used to enable simple string-based path comparison, etc)
      // Note: it ignores first double "\\\\" because it could be a network path
      static wstring                            CleanupPath( const wstring & inputPath, bool convertToLowercase, bool useBackslash = true );
      static string                             CleanupPath( const string & inputPath, bool convertToLowercase, bool useBackslash = true )  
                                                                                                        { return vaStringTools::SimpleNarrow( CleanupPath( vaStringTools::SimpleWiden(inputPath), convertToLowercase, useBackslash ) ); }

      // tries to find the file using GetWorkingDirectory as root, then GetExecutableDirectory and then finally using system default; if file is found, returns full path; if not, returns empty string
      static wstring                            FindLocalFile( const wstring & fileName );

      // SplitPath is in vaFileTools::SplitPath

      static bool                               EnsureDirectoryExists( const wchar_t * path );
      static bool                               EnsureDirectoryExists( const wstring & path )           { return EnsureDirectoryExists(path.c_str()); }
      static bool                               EnsureDirectoryExists( const string & path )            { return EnsureDirectoryExists( vaStringTools::SimpleWiden( path ) );
   }
//      // Just loads the whole file into a buffer 
//      // (this should be deleted)
//      static std::shared_ptr<vaMemoryBuffer> LoadFileToMemoryBuffer( const wchar_t * fileName );

      // Just loads the whole file into a buffer; TODO: rename and unify naming with ReadBuffer to ReadBinary/WriteBinary
      static std::shared_ptr<vaMemoryStream>    LoadMemoryStream( const wstring & fileName );
      static std::shared_ptr<vaMemoryStream>    LoadMemoryStream( const string & fileName );

      static string                             ReadText( const wstring & fileName );
      static string                             ReadText( const string & fileName )                     { return ReadText( vaStringTools::SimpleWiden( fileName ) ); }
   
      static wstring                            GetAbsolutePath( const wstring & path );
      static string                             GetAbsolutePath( const string & path )                  { return vaStringTools::SimpleNarrow( GetAbsolutePath( vaStringTools::SimpleWiden( path ))); }

      // Wildcards allowed!
      static std::vector<wstring>               FindFiles( const wstring & startDirectory, const wstring & searchName, bool recursive );
      static std::vector<wstring>               FindDirectories( const wstring & startDirectory );

      static std::vector<string>                FindFiles( const string & startDirectory, const string & searchName, bool recursive );
      static std::vector<string>                FindDirectories( const string & startDirectory );

      // The pointers must remain valid until vaFileTools::Deinitialize is called
      static void                               EmbeddedFilesRegister( const string & pathName, byte * data, int64 dataSize, int64 timeStamp );
      static EmbeddedFileData                   EmbeddedFilesFind( const string & pathName );

      static wstring                            OpenFileDialog( const wstring & initialFileName = L"", const wstring & initialDir = L"", const wchar_t * filter = L"All Files\0*.*\0\0", int filterIndex = 0, const wstring & dialogTitle = L"Open" );
      static wstring                            SaveFileDialog( const wstring & fileName, const wstring & initialDir = L"", const wchar_t * filter = L"All Files\0*.*\0\0", int filterIndex = 0, const wstring & dialogTitle = L"Save as" );
      static wstring                            SelectFolderDialog( const wstring & initialDir = L"" );
      static void                               OpenSystemExplorerFolder( const wstring & folderPath );

      static string                             OpenFileDialog( const string & initialFileName = "", const string & initialDir = "", const char * filter = "All Files\0*.*\0\0", int filterIndex = 0, const string & dialogTitle = "Open" );
      static string                             SaveFileDialog( const string & fileName, const string & initialDir = "", const char * filter = "All Files\0*.*\0\0", int filterIndex = 0, const string & dialogTitle = "Save as" );
      static string                             SelectFolderDialog( const string & initialDir = "" );
      static void                               OpenSystemExplorerFolder( const string & folderPath );

      static bool                               WriteBuffer( const wstring & filePath, void * buffer, size_t size );
      static bool                               ReadBuffer( const wstring & filePath, void * buffer, size_t size );

      static bool                               WriteBuffer( const string & filePath, void* buffer, size_t size );
      static bool                               ReadBuffer( const string & filePath, void* buffer, size_t size );

      // Todo: standardize to UTF-8
      static bool                               WriteText( const string & filePath, const string & text );

      static void                               SplitPath( const string & inFullPath, string * outDirectory, string * outFileName, string * outFileExt );
      static void                               SplitPath( const wstring & inFullPath, wstring * outDirectory, wstring * outFileName, wstring * outFileExt );

      static bool                               PathHasDirectory( const string & inFullPath );

      static wstring                            FixExtension( const wstring & path, const wstring & ext );
      static string                             FixExtension( const string & path, const string & ext );

      static string                             SplitPathExt( const string & inFullPath );
      static wstring                            SplitPathExt( const wstring & inFullPath );

   private:
      friend class vaCore;
      static void                               Initialize( );
      static void                               Deinitialize( );
   };


}

