///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Core/System/vaFileStream.h"

#include "Core/System/vaFileTools.h"

#include <Commdlg.h>

#include <shobjidl.h> 
#include <shlobj_core.h>

using namespace Vanilla;

#pragma warning (disable : 4996)

static bool IsDots( const wchar_t * str )
{
   if( wcscmp( str, L"." ) && wcscmp( str, L".." ) ) 
      return false;
   return true;
}

#ifndef _S_IWRITE
   #define _S_IWRITE       0x0080          /* write permission, owner */
#endif

bool vaFileTools::DeleteDirectory( const wstring & path )
{
   HANDLE hFind;
   WIN32_FIND_DATA FindFileData;

   wchar_t DirPath[MAX_PATH];
   wchar_t FileName[MAX_PATH];

   wcscpy( DirPath, path.c_str() );
   wcscat( DirPath, L"\\*" ); // searching all files
   wcscpy( FileName, path.c_str() );
   wcscat( FileName, L"\\" );

   // find the first file
   hFind = FindFirstFile( DirPath, &FindFileData );
   if( hFind == INVALID_HANDLE_VALUE )
      return false;
   wcscpy(DirPath,FileName);

   bool bSearch = true;
   while( bSearch ) 
   {
      // until we find an entry
      if( FindNextFile(hFind,&FindFileData) )
      {
         if( IsDots(FindFileData.cFileName) )
            continue;
         wcscat( FileName,FindFileData.cFileName );
         if( ( FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) ) 
         {
               // we have found a directory, recurse
               if( !DeleteDirectory(FileName) )
               {
                  FindClose(hFind);
                  return false; // directory couldn't be deleted
               }
               // remove the empty directory
               RemoveDirectory(FileName);
               wcscpy(FileName,DirPath);
         }
         else
         {
            if( FindFileData.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
               // change read-only file mode
               _wchmod( FileName, _S_IWRITE );

            if( !DeleteFile(FileName) ) 
            { 
               // delete the file
               FindClose(hFind);
               return false;
            }
            wcscpy(FileName,DirPath);
         }
      }
      else 
      {
         // no more files there
         if( GetLastError() == ERROR_NO_MORE_FILES )
            bSearch = false;
         else 
         {
            // some error occurred; close the handle and return FALSE
            FindClose( hFind );
            return false;
         }

      }

   }
   FindClose(hFind); // close the file handle

   return RemoveDirectory( path.c_str() ) != 0; // remove the empty directory
}

bool vaFileTools::DirectoryExists( const wchar_t * path )
{
   DWORD attr = ::GetFileAttributes( path );

   if( attr == INVALID_FILE_ATTRIBUTES )
      return false;

   return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

wstring vaFileTools::OpenFileDialog( const wstring & initialFileName, const wstring & initialDir, const wchar_t * filter, int filterIndex, const wstring & dialogTitle)
{
    // TODO: Switch to IFileOpenDialog path (see SelectFolderDialog for example)
    OPENFILENAME ofn ;

    ZeroMemory(&ofn, sizeof(ofn));

    wchar_t outBuffer[MAX_PATH];
    wcscpy_s( outBuffer, _countof( outBuffer ), initialFileName.c_str() );

    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = vaWindows::GetMainHWND();
    ofn.lpstrDefExt     = NULL;
    ofn.lpstrFile       = outBuffer;
    ofn.nMaxFile        = _countof( outBuffer );
    ofn.lpstrFilter     = filter;
    ofn.nFilterIndex    = filterIndex;
    ofn.lpstrInitialDir = initialDir.c_str();
    ofn.lpstrTitle      = dialogTitle.c_str();
    ofn.Flags           = OFN_FILEMUSTEXIST; // OFN_ALLOWMULTISELECT

    if( GetOpenFileName( &ofn ) )
    {
        return ofn.lpstrFile;
    }
    else
    {
        return L"";
    }
}

wstring vaFileTools::SaveFileDialog( const wstring & initialFileName, const wstring & initialDir, const wchar_t * filter, int filterIndex, const wstring & dialogTitle)
{
    // TODO: Switch to IFileOpenDialog path (see SelectFolderDialog for example)
    OPENFILENAME ofn ;

    ZeroMemory(&ofn, sizeof(ofn));

    wchar_t outBuffer[MAX_PATH];
    wcscpy_s( outBuffer, _countof( outBuffer ), initialFileName.c_str() );

    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = vaWindows::GetMainHWND();
    ofn.lpstrDefExt     = NULL;
    ofn.lpstrFile       = outBuffer;
    ofn.nMaxFile        = _countof( outBuffer );
    ofn.lpstrFilter     = filter;
    ofn.nFilterIndex    = filterIndex;
    ofn.lpstrInitialDir = initialDir.c_str();
    ofn.lpstrTitle      = dialogTitle.c_str();
    ofn.Flags           = OFN_OVERWRITEPROMPT;

    if( GetSaveFileName( &ofn ) )
    {
        return ofn.lpstrFile;
    }
    else
    {
        return L"";
    }
}

string vaFileTools::OpenFileDialog( const string & initialFileName, const string & initialDir, const char * filter, int filterIndex, const string & dialogTitle )
{
    // TODO: Switch to IFileOpenDialog path (see SelectFolderDialog for example)
    OPENFILENAMEA ofn;

    ZeroMemory( &ofn, sizeof( ofn ) );

    char outBuffer[MAX_PATH];
    strcpy_s( outBuffer, _countof( outBuffer ), initialFileName.c_str( ) );

    ofn.lStructSize = sizeof( ofn );
    ofn.hwndOwner = vaWindows::GetMainHWND( );
    ofn.lpstrDefExt = NULL;
    ofn.lpstrFile = outBuffer;
    ofn.nMaxFile = _countof( outBuffer );
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = filterIndex;
    ofn.lpstrInitialDir = initialDir.c_str( );
    ofn.lpstrTitle = dialogTitle.c_str( );
    ofn.Flags = OFN_FILEMUSTEXIST; // OFN_ALLOWMULTISELECT

    if( GetOpenFileNameA( &ofn ) )
    {
        return ofn.lpstrFile;
    }
    else
    {
        return "";
    }
}

string vaFileTools::SaveFileDialog( const string & initialFileName, const string & initialDir, const char * filter, int filterIndex, const string & dialogTitle )
{
    // TODO: Switch to IFileOpenDialog path (see SelectFolderDialog for example)
    OPENFILENAMEA ofn;

    ZeroMemory( &ofn, sizeof( ofn ) );

    char outBuffer[MAX_PATH];
    strcpy_s( outBuffer, _countof( outBuffer ), initialFileName.c_str( ) );

    ofn.lStructSize = sizeof( ofn );
    ofn.hwndOwner = vaWindows::GetMainHWND( );
    ofn.lpstrDefExt = NULL;
    ofn.lpstrFile = outBuffer;
    ofn.nMaxFile = _countof( outBuffer );
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = filterIndex;
    ofn.lpstrInitialDir = initialDir.c_str( );
    ofn.lpstrTitle = dialogTitle.c_str( );
    ofn.Flags = OFN_OVERWRITEPROMPT;

    if( GetSaveFileNameA( &ofn ) )
    {
        return ofn.lpstrFile;
    }
    else
    {
        return "";
    }
}

wstring vaFileTools::SelectFolderDialog( const wstring & initialDir )
{
    initialDir;

    IFileOpenDialog *pFileOpen;

    wstring retVal = L"";

    // Create the FileOpenDialog object.
    HRESULT hr = CoCreateInstance( CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen) );

    if (SUCCEEDED(hr))
    {
        FILEOPENDIALOGOPTIONS options;
        hr = pFileOpen->GetOptions( &options );
        assert( SUCCEEDED( hr ) );

        hr = pFileOpen->SetOptions( options | FOS_PICKFOLDERS );
        assert( SUCCEEDED( hr ) );

        IShellItem *isiInitDir = NULL;
        if( initialDir != L"" )
        {
            hr = SHCreateItemFromParsingName( initialDir.c_str(), NULL, IID_IShellItem, (void**)&isiInitDir );
            if( SUCCEEDED(hr ) )
                pFileOpen->SetDefaultFolder( isiInitDir );
            SAFE_RELEASE( isiInitDir );
        }

        // Show the Open dialog box.
        hr = pFileOpen->Show( 0 ); // not sure why, but it hangs if you provide the HWND :| vaWindows::GetMainHWND() );

        // Get the file name from the dialog box.
        if (SUCCEEDED(hr))
        {
            IShellItem *pItem;
            hr = pFileOpen->GetResult(&pItem);
            if (SUCCEEDED(hr))
            {
                PWSTR pszFilePath;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);

                // Display the file name to the user.
                if (SUCCEEDED(hr))
                {
                    retVal = pszFilePath;
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
    return retVal;
}

void vaFileTools::OpenSystemExplorerFolder( const wstring & folderPath )
{
    LPCWSTR pszPathToOpen = folderPath.c_str();
    PIDLIST_ABSOLUTE pidl;
    if (SUCCEEDED(SHParseDisplayName(pszPathToOpen, 0, &pidl, 0, 0)))
    {
        // we don't want to actually select anything in the folder, so we pass an empty
        // PIDL in the array. if you want to select one or more items in the opened
        // folder you'd need to build the PIDL array appropriately
        ITEMIDLIST idNull = { 0 };
        LPCITEMIDLIST pidlNull[1] = { &idNull };
        SHOpenFolderAndSelectItems(pidl, 1, pidlNull, 0);
        ILFree(pidl);
    }
}