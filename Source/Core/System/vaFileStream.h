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

#include "Core\vaCore.h"

#include "vaStream.h"

#include "System\vaPlatformFileStream.h"


namespace Vanilla
{
   // Specifies how the operating system should open a file.
   struct FileCreationMode
   {
      enum Enum
      {
         // Create a new file. If the file already exists, the call will fail.
         CreateNew            = 1,

         // Create a new file. If the file already exists, it will be overwritten and truncated to 0 size. 
         Create               = 2,

         // Specifies that the operating system should open an existing file. If the file
         // doesn't exist the call will fail.
         Open                 = 3,

         // Open a file if it exists; otherwise, a new file will be created.
         OpenOrCreate         = 4,

         // Open existing file, truncate it's size to 0.
         Truncate             = 5,

         // Opens the file if it exists and seeks to the end of the file, or creates a new file. 
         Append               = 6,
      };
   };

   struct FileAccessMode
   {
      enum Enum
      {
         // Chose the access mode automatically based on creation mode
         Default              = -1,

         // Self-explanatory
         Read                 = 1,

         // Self-explanatory
         Write                = 2,

         // Self-explanatory
         ReadWrite            = 3,
      };
   };

   struct FileShareMode
   {
      enum Enum
      {
         // Chose the mode automatically based on creation mode
         Default              = -1,

         // Don't share. 
         // Any request to open the file (by this process or another process) will fail until the 
         // file is closed.
         None                 = 0,
         
         // Share only for read. Subsequent opening of the file for reading will be allowed.
         Read                 = 1,

         // Share only for write. Allows subsequent opening of the file for writing. 
         Write                = 2,

         // Share for read and write. Subsequent opening of the file for reading or writing will
         // be allowed.
         ReadWrite            = 3,

         // Share for delete. Subsequent deleting of a file will be allowed.
         Delete               = 4,
      };
   };

   class vaFileStream : public vaStream
   {
   public:

   private:
      vaPlatformFileStreamType   m_file;
      FileAccessMode::Enum       m_accessMode;

   public:
      vaFileStream( );
      vaFileStream( const vaFileStream & copy ) = delete;   // not implemented...
      virtual ~vaFileStream( void );

      virtual bool            Open( const wchar_t * filePath, FileCreationMode::Enum creationMode = FileCreationMode::Open, FileAccessMode::Enum accessMode = FileAccessMode::Default, FileShareMode::Enum shareMode = FileShareMode::Default );
      virtual bool            Open( const char * filePath, FileCreationMode::Enum creationMode = FileCreationMode::Open, FileAccessMode::Enum accessMode = FileAccessMode::Default, FileShareMode::Enum shareMode = FileShareMode::Default );

      virtual bool            Open( const wstring & filePath, FileCreationMode::Enum creationMode = FileCreationMode::Open, FileAccessMode::Enum accessMode = FileAccessMode::Default, FileShareMode::Enum shareMode = FileShareMode::Default )    { return Open( filePath.c_str(), creationMode, accessMode, shareMode ); }
      virtual bool            Open( const string & filePath, FileCreationMode::Enum creationMode = FileCreationMode::Open, FileAccessMode::Enum accessMode = FileAccessMode::Default, FileShareMode::Enum shareMode = FileShareMode::Default )     { return Open( filePath.c_str(), creationMode, accessMode, shareMode ); }

      virtual bool            CanSeek( )                          { return true; }
      virtual void            Seek( int64 position );
      virtual void            Close( );
      virtual bool            IsOpen( ) const;
      virtual int64           GetLength( );
      virtual int64           GetPosition( ) const override;

      virtual bool            Read( void * buffer, int64 count, int64 * outCountRead = NULL );
      virtual bool            Write( const void * buffer, int64 count, int64 * outCountWritten = NULL );

      virtual void            Flush( );

      virtual void            Truncate( );
   };

   // need to implement this, with proper text encoding, etc
   class vaTextFileStream : protected vaFileStream
   {
   public:
   };

}

