///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//

#include "vaDirectXTools.h"

#include <assert.h>

#include "sys/stat.h"

#include <D3DCompiler.h>

#include <sstream>

#include <inttypes.h>

#include "dxc/dxcapi.use.h"
#include "dxc/addref.h"

#include "vaShaderDX12.h"

#include "Rendering/DirectX/vaRenderDeviceDX12.h"

#include "Core/System/vaFileTools.h"

//////////////////////////////////////////////////////////////////////////
// from "DirectXShaderCompiler\include\dxc\Support\microcom.h"
/// <summary>
/// Provides a QueryInterface implementation for a class that supports
/// any number of interfaces in addition to IUnknown.
/// </summary>
/// <remarks>
/// This implementation will also report the instance as not supporting
/// marshaling. This will help catch marshaling problems early or avoid
/// them altogether.
/// </remarks>
template<typename TObject>
HRESULT DoBasicQueryInterface_recurse( TObject* self, REFIID iid, void** ppvObject ) { self; iid; ppvObject;
    return E_NOINTERFACE;
}
template<typename TObject, typename TInterface, typename... Ts>
HRESULT DoBasicQueryInterface_recurse( TObject* self, REFIID iid, void** ppvObject ) {
    if( ppvObject == nullptr ) return E_POINTER;
    if( IsEqualIID( iid, __uuidof( TInterface ) ) ) {
        *(TInterface**)ppvObject = self;
        self->AddRef( );
        return S_OK;
    }
    return DoBasicQueryInterface_recurse<TObject, Ts...>( self, iid, ppvObject );
}
template<typename... Ts, typename TObject>
HRESULT DoBasicQueryInterface( TObject* self, REFIID iid, void** ppvObject ) {
    if( ppvObject == nullptr ) return E_POINTER;

    // Support INoMarshal to void GIT shenanigans.
    if( IsEqualIID( iid, __uuidof( IUnknown ) ) ||
        IsEqualIID( iid, __uuidof( INoMarshal ) ) ) {
        *ppvObject = reinterpret_cast<IUnknown*>( self );
        reinterpret_cast<IUnknown*>( self )->AddRef( );
        return S_OK;
    }

    return DoBasicQueryInterface_recurse<TObject, Ts...>( self, iid, ppvObject );
}
//////////////////////////////////////////////////////////////////////////

using namespace Vanilla;

#ifdef NDEBUG
#define           STILL_LOAD_FROM_CACHE_IF_ORIGINAL_FILE_MISSING
#endif

using namespace std;

namespace Vanilla
{
    static dxc::DxcDllSupport       s_dxcSupport;

    static IDxcCompiler *           s_dxcCompiler       = nullptr;
    static IDxcLibrary *            s_dxcLibrary        = nullptr;

    //static wstring                  s_customDXCPath     = L"";
    //static wstring                  s_customDXCTempStorageDir = L"";

    //const D3D_SHADER_MACRO c_builtInMacros[] = { { "VA_COMPILED_AS_SHADER_CODE", "1" }, { "VA_DIRECTX", "12" }, { 0, 0 } };

    const DxcDefine c_builtInMacrosDXC[] = { { L"VA_COMPILED_AS_SHADER_CODE", L"1" }, { L"VA_DIRECTX", L"12" }, { L"VA_DXC", L"1" } };

    class vaShaderIncludeHelper12 : public IDxcIncludeHandler// ID3DInclude
    {
        std::vector<vaShaderCacheEntry12::FileDependencyInfo> &     m_dependenciesCollector;

        std::vector< std::pair<string, string> >                    m_foundNamePairs;

        wstring                                                     m_relativePath;

        string                                                      m_macrosAsIncludeFile;

        vaShaderIncludeHelper12( const vaShaderIncludeHelper12 & c ) = delete; //: m_dependenciesCollector( c.m_dependenciesCollector ), m_relativePath( c.m_relativePath ), m_macros( c.m_macros )
        //{
        //    assert( false );
        //}    // to prevent warnings (and also this object doesn't support copying/assignment)
        void operator = ( const vaShaderIncludeHelper12 & ) = delete;
        //{
        //    assert( false );
        //}    // to prevent warnings (and also this object doesn't support copying/assignment)

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // support of COM even though it's not needed in practice
    public:
        DXC_MICROCOM_REF_FIELD(m_dwRef);
        DXC_MICROCOM_ADDREF_RELEASE_IMPL( m_dwRef )
        HRESULT m_defaultErrorCode = E_FAIL;
        HRESULT STDMETHODCALLTYPE QueryInterface( REFIID iid, void** ppvObject ) override {
            return DoBasicQueryInterface<IDxcIncludeHandler>( this, iid, ppvObject );
        }
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    public:
        vaShaderIncludeHelper12( std::vector<vaShaderCacheEntry12::FileDependencyInfo> & dependenciesCollector, const wstring & relativePath, const string & macrosAsIncludeFile ) 
            : m_dwRef(1), m_dependenciesCollector( dependenciesCollector ), m_relativePath( relativePath ), m_macrosAsIncludeFile( macrosAsIncludeFile )
        {
        }
        ~vaShaderIncludeHelper12( )
        {
            assert( m_dwRef == 1 );
        }

        //STDMETHOD( Open )( D3D10_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID *ppData, UINT *pBytes )
        virtual HRESULT STDMETHODCALLTYPE LoadSource( _In_ LPCWSTR pFileName, _COM_Outptr_result_maybenull_ IDxcBlob** ppIncludeSource ) override
        {
            wstring inFileName(pFileName);
            if( inFileName.length() > 2 && inFileName[0] == L'.' && inFileName[1] == '/' )
                inFileName = inFileName.substr( 2 );

            {

                // special case to handle macros - no need to add dependencies here, macros are CRC-ed separately
                if( inFileName.find( L"MagicMacrosMagicFile.h" ) != wstring::npos || vaStringTools::ToLower(inFileName).find( L"magicmacrosmagicfile.h" ) != wstring::npos )
                {
                    ComPtr<IDxcBlobEncoding> blobSource;
                    HRESULT hr = s_dxcLibrary->CreateBlobWithEncodingOnHeapCopy( m_macrosAsIncludeFile.c_str( ), (UINT32)m_macrosAsIncludeFile.size( ), CP_UTF8, blobSource.GetAddressOf() );
                    blobSource->AddRef();
                    *ppIncludeSource = blobSource.Get();
                    assert( SUCCEEDED( hr ) );

                    return hr;
                }
            }

            vaShaderCacheEntry12::FileDependencyInfo fileDependencyInfo;
            std::shared_ptr<vaMemoryStream>        memBuffer;

            wstring fileNameR = m_relativePath + wstring( inFileName );
            wstring fileNameA = wstring( inFileName );

            // First try the file system...
            wstring fullFileName = vaDirectX12ShaderManager::GetInstance( ).FindShaderFile( fileNameR.c_str( ) );
            if( fullFileName == L"" )
                fullFileName = vaDirectX12ShaderManager::GetInstance( ).FindShaderFile( fileNameA.c_str( ) );
            if( fullFileName != L"" )
            {
                fileDependencyInfo = vaShaderCacheEntry12::FileDependencyInfo( fullFileName.c_str( ) );
                memBuffer = vaFileTools::LoadMemoryStream( fullFileName.c_str( ) );
            }
            else
            {
                // ...then try embedded storage
                wstring foundName = fileNameR;
                vaFileTools::EmbeddedFileData embeddedData = vaFileTools::EmbeddedFilesFind( vaStringTools::SimpleNarrow( wstring( L"shaders:\\" ) + foundName ) );
                if( !embeddedData.HasContents( ) )
                {
                    foundName = fileNameA;
                    embeddedData = vaFileTools::EmbeddedFilesFind( vaStringTools::SimpleNarrow( wstring( L"shaders:\\" ) + foundName ) );
                }

                if( !embeddedData.HasContents( ) )
                {
                    VA_WARN( L"Error trying to find shader file '%s' / '%s'!", fileNameR.c_str( ), fileNameA.c_str( ) );
                    return E_FAIL;
                }
                fileDependencyInfo = vaShaderCacheEntry12::FileDependencyInfo( foundName, embeddedData.TimeStamp );
                memBuffer = embeddedData.MemStream;
            }

            m_dependenciesCollector.push_back( fileDependencyInfo );
            m_foundNamePairs.push_back( std::make_pair( vaStringTools::SimpleNarrow(inFileName).c_str(), vaStringTools::SimpleNarrow(fullFileName) ) );

            ComPtr<IDxcBlobEncoding> blobSource;
            HRESULT hr = s_dxcLibrary->CreateBlobWithEncodingOnHeapCopy( memBuffer->GetBuffer(), (UINT32)memBuffer->GetLength(), CP_UTF8, blobSource.GetAddressOf( ) );
            blobSource->AddRef( );
            *ppIncludeSource = blobSource.Get( );
            assert( SUCCEEDED( hr ) );
            return hr;
        }
        const std::vector< std::pair<string, string> > & GetFoundNameParis( )       { return m_foundNamePairs; }
    };

    // this is used to make the error reporting report to correct file (or embedded storage)
    static string CorrectErrorIfNotFullPath12( const string & errorText, vaShaderIncludeHelper12 & includeHelper )
    {
        string ret;

        string codeStr( errorText );

        stringstream ss( codeStr );
        string line;
        //int lineNum = 1;
        while( std::getline( ss, line ) )
        {
            size_t driveSeparator   = line.find( ":\\" );

            size_t fileSeparator    = line.find( ':', driveSeparator+1 );
            size_t lineSeparator    = line.find( ':', fileSeparator+1 );
            size_t columnSeparator  = line.find( ':', lineSeparator+1 );
            if( fileSeparator != string::npos && lineSeparator != string::npos && columnSeparator != string::npos )
            {
                line[fileSeparator] = '(';
                line[lineSeparator] = ',';
                line.insert(line.begin()+columnSeparator, ')');
                columnSeparator++;

                string filePlusLinePart = line.substr( 0, columnSeparator );
                string errorPart = line.substr( columnSeparator );

                {
                    string filePart = filePlusLinePart.substr( 0, fileSeparator );
                    string lineInfoPart = filePlusLinePart.substr( fileSeparator );

                    wstring fileName = vaStringTools::SimpleWiden( filePart );

                    for( auto & fnp : includeHelper.GetFoundNameParis() )
                    {
                        if( vaStringTools::CompareNoCase( filePart, fnp.first ) == 0 )
                        {
                            filePart = fnp.second;
                            break;
                        }
                    }

                    line = filePart + lineInfoPart + errorPart;
                }
            }
            ret += line + "\n";
        }
        return ret;
    }

    vaShaderDX12::vaShaderDX12( const vaRenderingModuleParams & ) // params ) 
        // warning C4589: Constructor of abstract class 'Vanilla::vaShaderDX12' ignores initializer for virtual base class 'Vanilla::vaShader'
        //        : vaShader( params )
    {
//#ifdef VA_HOLD_SHADER_DISASM
//        m_disasmAutoDumpToFile = false;
//#endif
    }
    //
    vaShaderDX12::~vaShaderDX12( )
    {
        std::unique_lock allShaderDataLock( m_allShaderDataMutex );
        assert( m_destroyed );
    }
    //
    void vaShaderDX12::SafeDestruct( )
    {
        assert( vaDirectX12ShaderManager::GetInstancePtr( ) != nullptr ); // this should have been created by now - if not, see why

        {
            std::unique_lock btlock( m_backgroundCreationTaskMutex );
            vaBackgroundTaskManager::GetInstance( ).WaitUntilFinished( m_backgroundCreationTask );
        }

        std::unique_lock allShaderDataLock( m_allShaderDataMutex );
        DestroyShaderBase( );
        m_destroyed = true;
    }
    //
    void vaShaderDX12::Clear( bool lockWorkerMutex )
    {
        if( lockWorkerMutex )
        {
            std::unique_lock btlock( m_backgroundCreationTaskMutex );
            vaBackgroundTaskManager::GetInstance().WaitUntilFinished( m_backgroundCreationTask );
        }
        else
            vaBackgroundTaskManager::GetInstance().WaitUntilFinished( m_backgroundCreationTask );

        std::unique_lock allShaderDataLock( m_allShaderDataMutex ); 
        m_state             = vaShader::State::Empty;
        m_uniqueContentsID  = -1;
        DestroyShader( );
        assert( m_shaderData == nullptr );
        m_entryPoint        = "";
        m_shaderFilePath    = L"";
        m_shaderCode        = "";
        m_shaderModel       = "";
#ifdef VA_HOLD_SHADER_DISASM
        m_disasm            = "";
#endif
    }

    void vaShaderDX12::DestroyShaderBase( )
    {
        // m_allShaderDataMutex.assert_locked_by_caller();

        m_lastLoadedFromCache   = false;
        m_shaderData            = nullptr;
        if( m_state != State::Empty )
        {
            m_state             = State::Uncooked;
            m_uniqueContentsID  = -1;
            m_lastError         = "";
        }
    }
    //
    void vaShaderDX12::CreateCacheKey( vaShaderCacheKey12 & outKey )
    {
        // m_allShaderDataMutex.assert_locked_by_caller();

        // supposed to be empty!
        assert( outKey.StringPart.size( ) == 0 );
        outKey.StringPart.clear( );

        outKey.StringPart += vaStringTools::Format( "%d", (int)m_macros.size( ) ) + " ";
        for( uint32 i = 0; i < m_macros.size( ); i++ )
        {
            outKey.StringPart += m_macros[i].first + " ";
            outKey.StringPart += m_macros[i].second + " ";
        }
        outKey.StringPart += m_shaderModel + " ";
        outKey.StringPart += m_entryPoint + " ";
        outKey.StringPart += vaStringTools::ToLower( vaStringTools::SimpleNarrow( m_shaderFilePath ) ) + " ";
    }
    //
    void vaVertexShaderDX12::CreateCacheKey( vaShaderCacheKey12 & outKey )
    {
        // m_allShaderDataMutex.assert_locked_by_caller();

        vaShaderDX12::CreateCacheKey( outKey );
        
        outKey.StringPart += m_inputLayout.GetHashString();
    }
    //
    
    // from https://stackoverflow.com/questions/1802471/suppress-console-when-calling-system-in-c
    static int windows_system(const char *cmd)
    {
      PROCESS_INFORMATION p_info;
      STARTUPINFO s_info;
      //LPSTR cmdline, programpath;

      memset(&s_info, 0, sizeof(s_info));
      memset(&p_info, 0, sizeof(p_info));
      s_info.cb = sizeof(s_info);

      // cmdline     = _tcsdup(TEXT(cmd));
      // programpath = _tcsdup(TEXT(cmd));
      Vanilla::wstring cmdLine = vaStringTools::SimpleWiden( string(cmd) );
      WCHAR cmdLineNullTerminated[32768];
      if( cmdLine.length() > (countof(cmdLineNullTerminated)-1) )
          cmdLine = cmdLine.substr( 0, countof(cmdLineNullTerminated)-1 );
      for( size_t i = 0; i < cmdLine.length(); i++ )
          cmdLineNullTerminated[i] = cmdLine[i];
      cmdLineNullTerminated[cmdLine.length()] = 0;

      if( CreateProcess( NULL, cmdLineNullTerminated, NULL, NULL, 0, 0, NULL, NULL, &s_info, &p_info) )
      {
        WaitForSingleObject(p_info.hProcess, INFINITE);
        CloseHandle(p_info.hProcess);
        CloseHandle(p_info.hThread);
        return 0;
      }
      return -1;
    }

    static HRESULT CompileShaderFromBuffer( LPCVOID pSrcData, SIZE_T SrcDataSize, LPCSTR pFileName, LPCSTR szEntryPoint, LPCSTR szShaderModel, shared_ptr<vaShaderDataDX12> & outBlob, string& outErrorInfo, vaShaderIncludeHelper12& includeHelper )
    {
        HRESULT hr = S_OK;

        DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
//#if defined( DEBUG ) || defined( _DEBUG )
        // Set the D3D10_SHADER_DEBUG flag to embed debug information in the shaders.
        // Setting this flag improves the shader debugging experience, but still allows 
        // the shaders to be optimized and to run exactly the way they will run in 
        // the release configuration of this program.
        dwShaderFlags |= D3DCOMPILE_DEBUG;
//#endif
//#if defined( DEBUG ) || defined( _DEBUG )
//        dwShaderFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL0;
//#else
        dwShaderFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
//#endif
        if( vaDirectX12ShaderManager::GetInstance( ).Settings( ).WarningsAreErrors )
            dwShaderFlags |= D3DCOMPILE_WARNINGS_ARE_ERRORS;

        ComPtr<IDxcBlobEncoding> blobSource;
        hr = s_dxcLibrary->CreateBlobWithEncodingOnHeapCopy( pSrcData, (UINT32)SrcDataSize, CP_UTF8, blobSource.GetAddressOf() );
        assert( SUCCEEDED( hr ) );

        std::vector<LPCWSTR> arguments;

        // /Gec, /Ges Not implemented:
        //if(dwShaderFlags & D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY) arguments.push_back(L"/Gec");
        if(dwShaderFlags & D3DCOMPILE_ENABLE_STRICTNESS) arguments.push_back(L"/Ges");
        if( dwShaderFlags & D3DCOMPILE_IEEE_STRICTNESS ) arguments.push_back( L"/Gis" );
        if( dwShaderFlags & D3DCOMPILE_OPTIMIZATION_LEVEL2 )
        {
            switch( dwShaderFlags & D3DCOMPILE_OPTIMIZATION_LEVEL2 )
            {
            case D3DCOMPILE_OPTIMIZATION_LEVEL0: arguments.push_back( L"/O0" ); break;
            case D3DCOMPILE_OPTIMIZATION_LEVEL2: arguments.push_back( L"/O2" ); break;
            case D3DCOMPILE_OPTIMIZATION_LEVEL3: arguments.push_back( L"/O3" ); break;
            }
        }
        if( dwShaderFlags & D3DCOMPILE_WARNINGS_ARE_ERRORS )
            arguments.push_back( L"/WX" );
        // Currently, /Od turns off too many optimization passes, causing incorrect DXIL to be generated.
        // Re-enable once /Od is implemented properly:
        //if(dwShaderFlags & D3DCOMPILE_SKIP_OPTIMIZATION) arguments.push_back(L"/Od");
        if( dwShaderFlags & D3DCOMPILE_DEBUG ) 
        {
            arguments.push_back( L"/Zi" );
            arguments.push_back( L"-Qembed_debug" ); // this is for the "warning: no output provided for debug - embedding PDB in shader container.  Use -Qembed_debug to silence this warning."
        }

        if( dwShaderFlags & D3DCOMPILE_PACK_MATRIX_ROW_MAJOR )      { assert( false ); }    //arguments.push_back( L"/Zpr" );
        if( dwShaderFlags & D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR )   { assert( false ); }    //arguments.push_back( L"/Zpc" );
        
        // Vanilla defaults to column-major in shaders because that is the DXC default with no arguments, and it seems to be 
        // more common* in general. (*AFAIK)
        // See vaShaderCore.h!
        arguments.push_back( L"/Zpc" );

        if( dwShaderFlags & D3DCOMPILE_AVOID_FLOW_CONTROL ) arguments.push_back( L"/Gfa" );
        if( dwShaderFlags & D3DCOMPILE_PREFER_FLOW_CONTROL ) arguments.push_back( L"/Gfp" );
        // We don't implement this:
        //if(dwShaderFlags & D3DCOMPILE_PARTIAL_PRECISION) arguments.push_back(L"/Gpp");
        if( dwShaderFlags & D3DCOMPILE_RESOURCES_MAY_ALIAS ) arguments.push_back( L"/res_may_alias" );
        // arguments.push_back( L"-HV" );
        // arguments.push_back( L"2016" );

        // this is a bit tricky - on TitanV for example using float16_t performs worse than min16float
        // arguments.push_back( L"-enable-16bit-types" );

        wstring longFileName = vaStringTools::SimpleWiden( pFileName );
        wstring longEntryPoint = vaStringTools::SimpleWiden( szEntryPoint );
        wstring longShaderModel = vaStringTools::SimpleWiden( szShaderModel );

        // we've got to up the shader model - old ones no longer supported
        if( longShaderModel[3] < L'6' )
        {
            assert( false );
            longShaderModel[3] = L'6';
        }

        ComPtr<IDxcOperationResult> operationResult;

        hr = s_dxcCompiler->Compile( blobSource.Get(), longFileName.c_str(), longEntryPoint.c_str(), longShaderModel.c_str(), arguments.data(), (uint32)arguments.size(), c_builtInMacrosDXC, countof(c_builtInMacrosDXC), &includeHelper, operationResult.GetAddressOf() );

        if( operationResult )
            operationResult->GetStatus( &hr );
        if( SUCCEEDED( hr ) )
        {
            IDxcBlob * dxBlob = nullptr;
            hr = operationResult->GetResult( &dxBlob );
            if( FAILED(hr) || dxBlob->GetBufferSize() == 0 )
            {
                outErrorInfo = "Unknown shader compilation error, no blob or empty blob returned"; assert( false );
                return hr;
            }

            outBlob = std::make_shared<vaShaderDataDX12>( dxBlob->GetBufferSize() );
            memcpy( outBlob->GetBufferPointer(), dxBlob->GetBufferPointer(), outBlob->GetBufferSize() );
            dxBlob->Release();
            return S_OK;

            // code for saving asm below - not sure what exactly it was for :)
            //{
            //    IDxcBlob * processed = nullptr;
            //    hr = operationResult->GetResult( &processed );
            //    assert( SUCCEEDED( hr ) );
            //    
            //    wstring tempFilePath    = vaFileTools::GetAbsolutePath( s_customDXCTempStorageDir + vaCore::GUIDToString(vaCore::GUIDCreate()) + L".hlsl");
            //    wstring outFilePath     = vaFileTools::GetAbsolutePath( s_customDXCTempStorageDir + vaCore::GUIDToString(vaCore::GUIDCreate()) + L".obj" );
            //    vaFileTools::WriteBuffer( tempFilePath, processed->GetBufferPointer(), processed->GetBufferSize() );
            //    SAFE_RELEASE( processed );
            //
            //    wstring fullCommand = s_customDXCPath + L" ";
            //    for( auto argument : arguments )
            //        fullCommand += argument + wstring(L" ");
            //
            //    fullCommand += L"-T " + longShaderModel + L" ";
            //    fullCommand += L"-E " + longEntryPoint + L" ";
            //
            //    fullCommand += L"-Fo " + outFilePath + L" ";
            //
            //    fullCommand += tempFilePath;
            //
            //    // std::thread( [fullCommand] () { system( vaStringTools::SimpleNarrow( fullCommand ).c_str() ); } ).join();
            //    std::thread( [fullCommand] () { windows_system( vaStringTools::SimpleNarrow( fullCommand ).c_str() ); } ).join();
            //
            //    UINT codePage = 0;
            //    ComPtr<IDxcBlobEncoding> blobOut;
            //    hr = s_dxcLibrary->CreateBlobFromFile( outFilePath.c_str(), &codePage, blobOut.GetAddressOf() );
            //    vaFileTools::DeleteFile( tempFilePath );
            //    vaFileTools::DeleteFile( outFilePath );
            //    if( FAILED(hr) )
            //    {
            //        outErrorInfo = "Error trying to compile shader using DXC"; 
            //    }
            //    if( codePage != CP_UTF8 && codePage != CP_ACP )
            //    {
            //        outErrorInfo = "Unknown shader compilation error - unsupported error message encoding";
            //        return E_FAIL;
            //    }
            //    if( SUCCEEDED(hr) )
            //    {
            //        blobOut->AddRef();
            //        *ppBlobOut = blobOut.Get();
            //    }
            //    return hr;
            //}
        }
        else 
        {
            ComPtr<IDxcBlobEncoding> blobErrors;
            if( FAILED( operationResult->GetErrorBuffer( blobErrors.GetAddressOf() ) ) )
                { outErrorInfo = "Unknown shader compilation error"; assert( false ); }
            
            BOOL known = false; UINT32 codePage;
            if( FAILED( blobErrors->GetEncoding( &known, &codePage ) ) )
                { outErrorInfo = "Unknown shader compilation error"; assert( false ); }
            else
            {
                if( !known || codePage != CP_UTF8 )
                {
                    outErrorInfo = "Unknown shader compilation error - unsupported error message encoding";
                }
                else
                {
                    outErrorInfo = string( (char*)blobErrors->GetBufferPointer( ), blobErrors->GetBufferSize()-1 );
                    outErrorInfo = CorrectErrorIfNotFullPath12( outErrorInfo, includeHelper );
                    outErrorInfo = vaStringTools::Format( "Error compiling shader '%s', '%s', '%s': \n", pFileName, szEntryPoint, szShaderModel ) + outErrorInfo;
                    vaCore::DebugOutput( outErrorInfo );
                    VA_LOG_WARNING( "%s", outErrorInfo.c_str() );
                }
            }

        }

        return S_OK;
        
    }    
    //
    static HRESULT CompileShaderFromFile( const wchar_t* szFileName, const string & macrosAsIncludeFile, LPCSTR szEntryPoint,
        LPCSTR szShaderModel, shared_ptr<vaShaderDataDX12> & outBlob, std::vector<vaShaderCacheEntry12::FileDependencyInfo> & outDependencies, string & outErrorInfo )
    {
        outDependencies.clear( );

        // find the file
        wstring fullFileName = vaDirectX12ShaderManager::GetInstance( ).FindShaderFile( szFileName );

        string ansiName;
        std::shared_ptr<vaShaderIncludeHelper12> includeHelper;

        if( fullFileName != L"" )
        {
            outDependencies.push_back( vaShaderCacheEntry12::FileDependencyInfo( szFileName ) );
            
            std::shared_ptr<vaMemoryStream> memBuffer = vaFileTools::LoadMemoryStream( fullFileName.c_str( ) );
            ansiName = vaStringTools::SimpleNarrow( fullFileName );

            wstring relativePath;
            vaFileTools::SplitPath( szFileName, &relativePath, nullptr, nullptr );

            includeHelper = std::make_shared<vaShaderIncludeHelper12>( outDependencies, relativePath, macrosAsIncludeFile );
            return CompileShaderFromBuffer( memBuffer->GetBuffer( ), memBuffer->GetLength( ), ansiName.c_str(), szEntryPoint, szShaderModel, outBlob, outErrorInfo, *includeHelper );
        }
        else
        {
            // ...then try embedded storage
            vaFileTools::EmbeddedFileData embeddedData = vaFileTools::EmbeddedFilesFind( vaStringTools::SimpleNarrow( wstring( L"shaders:\\" ) + wstring( szFileName ) ) );

            if( !embeddedData.HasContents( ) )
            {
                VA_WARN( vaStringTools::Format( "Error while compiling '%s' shader, SM: '%s', EntryPoint: '%s' :", vaStringTools::SimpleNarrow( szFileName ).c_str( ), szShaderModel, szEntryPoint ).c_str( ) );
                VA_WARN( L">>Error trying to find shader file '%s'!<<", szFileName );
                return E_FAIL;
            }

            outDependencies.push_back( vaShaderCacheEntry12::FileDependencyInfo( szFileName, embeddedData.TimeStamp ) );

            wstring relativePath;
            vaFileTools::SplitPath( szFileName, &relativePath, nullptr, nullptr );

            includeHelper = std::make_shared<vaShaderIncludeHelper12>( outDependencies, relativePath, macrosAsIncludeFile );

            ansiName = vaStringTools::SimpleNarrow( szFileName );

            return CompileShaderFromBuffer( embeddedData.MemStream->GetBuffer(), embeddedData.MemStream->GetLength(), ansiName.c_str(), szEntryPoint, szShaderModel, outBlob, outErrorInfo, *includeHelper );
        }

        // assert( false );
        // return E_FAIL;
    }
    //
    shared_ptr<vaShaderDataDX12> vaShaderDX12::CreateShaderBase( bool & loadedFromCache )
    {
        // m_allShaderDataMutex.assert_locked_by_caller();

        shared_ptr<vaShaderDataDX12> shaderBlob = nullptr;
        loadedFromCache = false;

        auto & device = GetRenderDevice().SafeCast<vaRenderDeviceDX12*>( )->GetPlatformDevice();
        if( device == nullptr )
            return nullptr;
        if( ( m_shaderFilePath.size( ) == 0 ) && ( m_shaderCode.size( ) == 0 ) )
        {
            vaLog::GetInstance( ).Add( LOG_COLORS_SHADERS, L" Shader has no file or code provided - cannot compile" );
            return nullptr; // still no path or code set provided
        }

        string macrosAsIncludeFile;
        GetMacrosAsIncludeFile( macrosAsIncludeFile );

        if( m_shaderFilePath.size( ) != 0 )
        {
            vaShaderCacheKey12 cacheKey;
            CreateCacheKey( cacheKey );

#ifdef VA_SHADER_CACHE_PERSISTENT_STORAGE_ENABLE
            bool foundButModified;
            shaderBlob = vaDirectX12ShaderManager::GetInstance( ).FindInCache( cacheKey, foundButModified );

            if( shaderBlob == nullptr )
            {
                wstring entryw = vaStringTools::SimpleWiden( m_entryPoint );
                wstring shadermodelw = vaStringTools::SimpleWiden( m_shaderModel );

                if( foundButModified )
                    vaLog::GetInstance( ).Add( LOG_COLORS_SHADERS, L" > file '%s' for '%s', entry '%s', found in cache but modified; recompiling...", m_shaderFilePath.c_str( ), shadermodelw.c_str( ), entryw.c_str( ) );
                else
                    vaLog::GetInstance( ).Add( LOG_COLORS_SHADERS, L" > file '%s' for '%s', entry '%s', not found in cache; recompiling...", m_shaderFilePath.c_str( ), shadermodelw.c_str( ), entryw.c_str( ) );
            }
#endif

            loadedFromCache = shaderBlob != nullptr;

            if( shaderBlob == nullptr )
            {
                std::vector<vaShaderCacheEntry12::FileDependencyInfo> dependencies;

                CompileShaderFromFile( m_shaderFilePath.c_str( ), macrosAsIncludeFile, m_entryPoint.c_str( ), m_shaderModel.c_str( ), shaderBlob, dependencies, m_lastError );

                if( shaderBlob != nullptr )
                    vaDirectX12ShaderManager::GetInstance( ).AddToCache( cacheKey, shaderBlob, dependencies );
            }
        }
        else if( m_shaderCode.size( ) != 0 )
        {
            std::vector<vaShaderCacheEntry12::FileDependencyInfo> dependencies;
            vaShaderIncludeHelper12 includeHelper( dependencies, L"", macrosAsIncludeFile );

            CompileShaderFromBuffer( m_shaderCode.c_str( ), m_shaderCode.size( ), "EmbeddedInCodebase", m_entryPoint.c_str( ), m_shaderModel.c_str( ), shaderBlob, m_lastError, includeHelper );
            
            // this one doesn't get cached. Could it? Should it? I'm not sure!
            // if( shaderBlob != nullptr )
            //     vaDirectX12ShaderManager::GetInstance( ).AddToCache( cacheKey, shaderBlob, dependencies );
        }
        else
        {
            assert( false );
        }


#ifdef VA_HOLD_SHADER_DISASM
        if( shaderBlob != nullptr )
        {

            IDxcBlobEncoding * disasmBlob = nullptr;
            if( FAILED( s_dxcCompiler->Disassemble( (IDxcBlob*)(shaderBlob.get()), &disasmBlob ) ) )
            {
                m_disasm = "s_dxcCompiler->Disassemble failed";
            }
            else
            {
                BOOL known = false; UINT32 codePage;
                if( FAILED( disasmBlob->GetEncoding( &known, &codePage ) ) )
                { m_disasm = "s_dxcCompiler->Disassemble succeeded but disasmBlob->GetEncoding failed"; assert( false ); }
                else
                {
                    if( !known || codePage != CP_UTF8 )
                    { m_disasm = "s_dxcCompiler->Disassemble succeeded but unknown/unsupported text encoding"; assert( false ); }
                    else
                        m_disasm = string( (char*)disasmBlob->GetBufferPointer( ), disasmBlob->GetBufferSize()-1 );
                }

                //if( m_disasmAutoDumpToFile )
                //{
                //    wstring fileName = vaCore::GetWorkingDirectory( );
                //
                //    vaShaderCacheKey12 cacheKey;
                //    CreateCacheKey( cacheKey );
                //    // vaCRC64 crc;
                //    // crc.AddString( cacheKey.StringPart );
                //
                //    fileName += L"shaderdump_" + vaStringTools::SimpleWiden( m_entryPoint ) + L"_" + vaStringTools::SimpleWiden( m_shaderModel ) /*+ L"_" + vaStringTools::SimpleWiden( vaStringTools::Format( "0x%" PRIx64, crc.GetCurrent() ) )*/ + L".txt";
                //    vaStringTools::WriteTextFile( fileName, m_disasm );
                //}

                SAFE_RELEASE( disasmBlob );
            }
        }
#endif

        return shaderBlob;
    }
    //
    void vaShaderDX12::CreateShader( )
    {
        assert( vaDirectX12ShaderManager::GetInstancePtr() != nullptr ); // this should have been created by now - if not, see why
        // m_allShaderDataMutex.assert_locked_by_caller();

        assert( m_shaderData == nullptr );

        auto blob = CreateShaderBase( m_lastLoadedFromCache );
        m_shaderData = blob;
        if( m_shaderData != nullptr )
        {
            m_state             = State::Cooked;
            m_uniqueContentsID  = ++s_lastUniqueShaderContentsID;
            m_lastError         = "";
        }
        else
        {
            assert( m_state == State::Uncooked );
        }
    }
    //
    vaVertexShaderDX12::~vaVertexShaderDX12( )
    {
        {
            std::unique_lock btlock( m_backgroundCreationTaskMutex );
            vaBackgroundTaskManager::GetInstance().WaitUntilFinished( m_backgroundCreationTask );
        }
        SafeDestruct();
    }
    //
    // void vaVertexShaderDX12::SetInputLayout( D3D12_INPUT_ELEMENT_DESC * elements, uint32 elementCount )
    // {
    //     std::unique_lock allShaderDataLock( m_allShaderDataMutex ); 
    // 
    //     m_inputLayout = LayoutVAFromDX( elements, elementCount );
    //     LayoutDXFromVA( m_inputLayoutDX12, m_inputLayout );
    // }
    //
    void vaVertexShaderDX12::CompileVSAndILFromFile( const string & _filePath, const string & entryPoint, const std::vector<vaVertexInputElementDesc> & inputLayoutElements, const vaShaderMacroContaner & macros, bool forceImmediateCompile )
    {
        wstring filePath = vaStringTools::SimpleWiden( _filePath );
        const string shaderModel = string( GetSMPrefix( ) ) + "_" + GetSMVersion( );

        assert( filePath != L"" && entryPoint != "" && shaderModel != "" );
        assert( vaDirectX12ShaderManager::GetInstancePtr() != nullptr ); // this should have been created by now - if not, see why
        {
            std::unique_lock btlock( m_backgroundCreationTaskMutex );
            vaBackgroundTaskManager::GetInstance().WaitUntilFinished( m_backgroundCreationTask );
        }

        {
            std::unique_lock allShaderDataLock( m_allShaderDataMutex ); 
            m_inputLayout = vaVertexInputLayoutDesc( inputLayoutElements );
            m_inputLayoutDX12 = std::make_shared<vaInputLayoutDataDX12>( m_inputLayout );
        }

        vaShaderDX12::CompileFromFile( _filePath, entryPoint, macros, forceImmediateCompile );
    }
    //
    void vaVertexShaderDX12::CompileVSAndILFromBuffer( const string & shaderCode, const string & entryPoint, const std::vector<vaVertexInputElementDesc> & inputLayoutElements, const vaShaderMacroContaner & macros, bool forceImmediateCompile )
    {
        const string shaderModel = string( GetSMPrefix( ) ) + "_" + GetSMVersion( );
        assert( shaderCode != "" && entryPoint != "" && shaderModel != "" );
        assert( vaDirectX12ShaderManager::GetInstancePtr() != nullptr ); // this should have been created by now - if not, see why
        {
            std::unique_lock btlock( m_backgroundCreationTaskMutex );
            vaBackgroundTaskManager::GetInstance().WaitUntilFinished( m_backgroundCreationTask );
        }

        {
            std::unique_lock allShaderDataLock( m_allShaderDataMutex ); 
            m_inputLayout = vaVertexInputLayoutDesc( inputLayoutElements );
            m_inputLayoutDX12 = std::make_shared<vaInputLayoutDataDX12>( m_inputLayout );
        }
        vaShaderDX12::CompileFromBuffer( shaderCode, entryPoint, macros, forceImmediateCompile );
    }
    //
    void vaVertexShaderDX12::CreateShader( )
    {
        assert( vaDirectX12ShaderManager::GetInstancePtr() != nullptr ); // this should have been created by now - if not, see why
        // m_allShaderDataMutex.assert_locked_by_caller();

        assert( m_shaderData == nullptr );

        auto blob = CreateShaderBase( m_lastLoadedFromCache );
        m_shaderData = blob;
        if( m_shaderData != nullptr )
        {
            m_state             = State::Cooked;
            m_uniqueContentsID  = ++s_lastUniqueShaderContentsID;
            m_lastError         = "";
        }
        else
        {
            assert( m_state == State::Uncooked );
        }
    }
    //
    void vaVertexShaderDX12::DestroyShader( )
    {
        assert( vaDirectX12ShaderManager::GetInstancePtr() != nullptr ); // this should have been created by now - if not, see why
        // m_allShaderDataMutex.assert_locked_by_caller();
        vaShaderDX12::DestroyShader( );
    }
    //
    //vaVertexInputLayoutDesc vaVertexShaderDX12::LayoutVAFromDX( D3D12_INPUT_ELEMENT_DESC * elements, uint32 elementCount )
    //{
    //    std::vector<vaVertexInputElementDesc> descArray;

    //    for( int i = 0; i < (int)elementCount; i++ )
    //    {
    //        const D3D12_INPUT_ELEMENT_DESC & src = elements[i];
    //        vaVertexInputElementDesc desc;
    //        desc.SemanticName           = src.SemanticName;
    //        desc.SemanticIndex          = src.SemanticIndex;
    //        desc.Format                 = VAFormatFromDXGI(src.Format);
    //        desc.InputSlot              = src.InputSlot;
    //        desc.AlignedByteOffset      = src.AlignedByteOffset;
    //        desc.InputSlotClass         = (vaVertexInputElementDesc::InputClassification)src.InputSlotClass;
    //        desc.InstanceDataStepRate   = src.InstanceDataStepRate;

    //        descArray.push_back( desc );
    //    }
    //    return vaVertexInputLayoutDesc(descArray);
    //}
    //
    vaInputLayoutDataDX12::vaInputLayoutDataDX12( const vaVertexInputLayoutDesc & inLayout ) : m_inputLayout( inLayout )
    {
        const std::vector<vaVertexInputElementDesc> & srcArray = m_inputLayout.ElementArray( );

        m_inputLayoutDX12.resize( srcArray.size() );

        for( int i = 0; i < srcArray.size(); i++ )
        {
            const vaVertexInputElementDesc & src = srcArray[i];
            D3D12_INPUT_ELEMENT_DESC desc;
            
            // I hate doing this
            desc.SemanticName           = src.SemanticName.c_str();
            desc.SemanticIndex          = src.SemanticIndex;
            desc.Format                 = (DXGI_FORMAT)src.Format;
            desc.InputSlot              = src.InputSlot;
            desc.AlignedByteOffset      = src.AlignedByteOffset;
            desc.InputSlotClass         = ( D3D12_INPUT_CLASSIFICATION )src.InputSlotClass;
            desc.InstanceDataStepRate   = src.InstanceDataStepRate;

            m_inputLayoutDX12[i] = desc;
        }
    }
    //
    vaDirectX12ShaderManager::vaDirectX12ShaderManager( vaRenderDevice & device ) : vaShaderManager( device )
    {
        assert( GetRenderDevice( ).IsRenderThread( ) );

        wstring compilerPath = vaCore::GetExecutableDirectory() + L"CustomDXC\\";
        if( !vaFileTools::FileExists( compilerPath + L"dxcompiler.dll" ) )
            compilerPath = vaCore::GetExecutableDirectory();

        s_dxcSupport.Initialize( compilerPath );

        HRESULT hr = E_FAIL;

        if( s_dxcSupport.IsEnabled( ) )
            hr = s_dxcSupport.CreateInstance( CLSID_DxcCompiler, &s_dxcCompiler );
        if( !SUCCEEDED( hr ) )
        {
            VA_ERROR( "Unable to create DirectX12 shader compiler - are 'dxcompiler.dll' and 'dxil.dll' files in place?" );
            assert( SUCCEEDED( hr ) );
        }
        if( SUCCEEDED( hr ) )
            hr = s_dxcSupport.CreateInstance( CLSID_DxcLibrary, &s_dxcLibrary );
        if( !SUCCEEDED( hr ) )
        {
            VA_ERROR( "Unable to create DirectX12 shader compiler - are 'dxcompiler.dll' and 'dxil.dll' files in place?" );
        }

#ifdef VA_SHADER_CACHE_PERSISTENT_STORAGE_ENABLE
        // start loading the cache before initializing anything else as it will continue in background - faster startup!
        {
            // lock here just in case we put something after the loading task 
            std::unique_lock<mutex> cacheMutexLock( m_cacheMutex );

            // this should maybe be set externally, but good enough for now
            m_cacheFilePath = vaCore::GetExecutableDirectory( ) + L".cache\\";

            // Do we need per-adapter caches? probably not but who cares - different adapter >usually< means different machine so recaching anyway
#if defined( DEBUG ) || defined( _DEBUG )
            m_cacheFilePath += L"shaders_dx12_debug_" + vaStringTools::SimpleWiden( vaStringTools::ReplaceSpacesWithUnderscores( GetRenderDevice( ).GetAdapterNameID( ) ) );
#else
            m_cacheFilePath += L"shaders_dx12_release_" + vaStringTools::SimpleWiden( vaStringTools::ReplaceSpacesWithUnderscores( GetRenderDevice( ).GetAdapterNameID( ) ) );
#endif
        }

        LoadCacheInternal( );
#endif // VA_SHADER_CACHE_PERSISTENT_STORAGE_ENABLE

        // if( !vaFileTools::FileExists( s_customDXCPath ) )
        //     s_customDXCPath = L"";
        // else
        // {
        //     s_customDXCTempStorageDir = vaCore::GetExecutableDirectory( ) + L".cache\\";
        //     vaFileTools::EnsureDirectoryExists( s_customDXCTempStorageDir );
        // }
    }
    //
    //
    vaDirectX12ShaderManager::~vaDirectX12ShaderManager( )
    {
        assert( GetRenderDevice().IsRenderThread() );

#ifdef VA_SHADER_CACHE_PERSISTENT_STORAGE_ENABLE
        {
            std::unique_lock<mutex> cacheMutexLock( m_cacheMutex );    // this is also needed to make sure loading finished!
            SaveCacheInternal( );
        }
#endif
        ClearCache( );

        // Ensure no shaders remain
        {
            std::unique_lock<mutex> shaderListLock( vaShader::GetAllShaderListMutex( ) );
            for( auto it : vaShader::GetAllShaderList( ) )
            {
                VA_LOG_ERROR( "Shader '%s' not unloaded", it->GetEntryPoint( ).c_str() );
            }
            assert( vaShader::GetAllShaderList().size( ) == 0 );
        }

        SAFE_RELEASE( s_dxcCompiler );

        // if there are blobs being cleaned up after, they will crash - hard to track down to this location
        s_dxcSupport.Cleanup();

        // s_customDXCPath.clear(); s_customDXCPath.shrink_to_fit();
        // s_customDXCTempStorageDir.clear(); s_customDXCTempStorageDir.shrink_to_fit();
    }
    //
    void vaDirectX12ShaderManager::RegisterShaderSearchPath( const std::wstring & path, bool pushBack )
    {
        wstring cleanedSearchPath = vaFileTools::CleanupPath( path + L"\\", false );
        if( pushBack )
            m_searchPaths.push_back( cleanedSearchPath );
        else
            m_searchPaths.push_front( cleanedSearchPath );
    }
    //
    wstring vaDirectX12ShaderManager::FindShaderFile( const wstring & fileName )
    {
        assert( m_searchPaths.size() > 0 ); // forgot to call RegisterShaderSearchPath?
        for( unsigned int i = 0; i < m_searchPaths.size( ); i++ )
        {
            std::wstring filePath = m_searchPaths[i] + L"\\" + fileName;
            if( vaFileTools::FileExists( filePath.c_str( ) ) )
            {
                return vaFileTools::GetAbsolutePath( filePath ); // vaFileTools::CleanupPath( filePath, false );
            }
            if( vaFileTools::FileExists( ( vaCore::GetWorkingDirectory( ) + filePath ).c_str( ) ) )
            {
                return vaFileTools::GetAbsolutePath( vaCore::GetWorkingDirectory( ) + filePath ); //vaFileTools::CleanupPath( vaCore::GetWorkingDirectory( ) + filePath, false );
            }
        }

        if( vaFileTools::FileExists( fileName ) )
            return vaFileTools::GetAbsolutePath( fileName ); // vaFileTools::CleanupPath( fileName, false );

        if( vaFileTools::FileExists( ( vaCore::GetWorkingDirectory( ) + fileName ).c_str( ) ) )
            return vaFileTools::GetAbsolutePath( vaCore::GetWorkingDirectory( ) + fileName ); // vaFileTools::CleanupPath( vaCore::GetWorkingDirectory( ) + fileName, false );

        return L"";
    }
    //
    void vaDirectX12ShaderManager::LoadCacheInternal( )
    {
        auto loadingLambda = [this]( vaBackgroundTaskManager::TaskContext & context ) 
        {
            vaTimerLogScope log( "Loading DirectX12 shader cache" );

            // lock here just in case we put something after the loading task 
            std::unique_lock<mutex> cacheMutexLock( m_cacheMutex );

            {
                std::unique_lock cacheLoadStartedMutexLock( m_cacheLoadStartedMutex );
                m_cacheLoadStarted = true;
            }
            m_cacheLoadStartedCV.notify_all();

            wstring fullFileName = m_cacheFilePath;

            if( fullFileName == L"" )
                return false;

            if( !vaFileTools::FileExists( fullFileName ) )
                return false;

            ClearCacheInternal( );

            vaFileStream inFile;
            if( inFile.Open( fullFileName.c_str( ), FileCreationMode::Open, FileAccessMode::Read ) )
            {
                int version = -1;
                if( inFile.ReadValue<int32>( version ) && version == 1 )
                {
                    assert( version == 1 );

                    int32 entryCount = 0;
                    if( !inFile.ReadValue<int32>( entryCount ) || ( entryCount < 0 ) )
                    {
                        VA_WARN( "Error while reading shader cache file, resetting and starting from scratch!" );
                        ClearCacheInternal( );
                        return false;
                    }

                    for( int i = 0; i < entryCount; i++ )
                    {
                        context.Progress = float(i)/float(entryCount-1);

                        vaShaderCacheKey12 key;
                        if( !key.Load( inFile ) )
                        {
                            VA_WARN( "Error while reading shader cache file, resetting and starting from scratch!" );
                            ClearCacheInternal( );
                            return false;
                        }
                        vaShaderCacheEntry12 * entry = new vaShaderCacheEntry12( );
                        if( !entry->Load( inFile ) )
                        {
                            delete entry;
                            VA_WARN( "Error while reading shader cache file, resetting and starting from scratch!" );
                            ClearCacheInternal( );
                            return false;
                        }

                        m_cache.insert( std::pair<vaShaderCacheKey12, vaShaderCacheEntry12 *>( key, entry ) );
                    }

                    int32 terminator;
                    if( !inFile.ReadValue<int32>( terminator ) || terminator != 0xFEEEFEEE )
                    {
                        VA_WARN( "Error while reading shader cache file, resetting and starting from scratch!" );
                        ClearCacheInternal( );
                        return false;
                    }
                }
                else
                {
                    VA_WARN( "Shader cache version upgraded, cannot use old cache, resetting and starting from scratch!" );
                    ClearCacheInternal();
                    return false;
                }
            }
            return true;
        };

        bool multithreaded = true;
        if( multithreaded )
        {
            vaBackgroundTaskManager::GetInstance().Spawn( "Loading Shader Cache", vaBackgroundTaskManager::SpawnFlags::ShowInUI, loadingLambda );
            
            // make sure we've started and taken the thread
            {
                std::unique_lock<std::mutex> lk(m_cacheLoadStartedMutex);
                m_cacheLoadStartedCV.wait(lk, [this] { return (bool)m_cacheLoadStarted; } );
            }
        }
        else
        {
            loadingLambda( vaBackgroundTaskManager::TaskContext() );
        }
    }
    //
    void vaDirectX12ShaderManager::SaveCacheInternal( ) const
    {
        vaTimerLogScope log( "Saving DirectX12 shader cache" );

        // must already be locked by this point!
        // std::unique_lock<mutex> cacheMutexLock( m_cacheMutex );
        m_cacheMutex.assert_locked_by_caller();

        wstring fullFileName = m_cacheFilePath;

        wstring cacheDir;
        vaFileTools::SplitPath( fullFileName, &cacheDir, nullptr, nullptr );
        vaFileTools::EnsureDirectoryExists( cacheDir.c_str( ) );

        vaFileStream outFile;
        outFile.Open( fullFileName.c_str( ), FileCreationMode::Create );

        outFile.WriteValue<int32>( 1 );                 // version;

        outFile.WriteValue<int32>( (int32)m_cache.size( ) );    // number of entries

        for( std::map<vaShaderCacheKey12, vaShaderCacheEntry12 *>::const_iterator it = m_cache.cbegin( ); it != m_cache.cend( ); ++it )
        {
            // Save key
            ( *it ).first.Save( outFile );

            // Save data
            ( *it ).second->Save( outFile );
        }

        outFile.WriteValue<int32>( 0xFEEEFEEE );  // EOF;
    }
    //
    void vaDirectX12ShaderManager::ClearCacheInternal( )
    {
        m_cacheMutex.assert_locked_by_caller();
        for( std::map<vaShaderCacheKey12, vaShaderCacheEntry12 *>::iterator it = m_cache.begin( ); it != m_cache.end( ); ++it )
        {
            delete ( *it ).second;;
        }
        m_cache.clear( );
    }
    //
    void vaDirectX12ShaderManager::ClearCache( )
    {
        std::unique_lock<mutex> cacheMutexLock( m_cacheMutex );
        ClearCacheInternal();
    }
    //
    shared_ptr<vaShaderDataDX12> vaDirectX12ShaderManager::FindInCache( vaShaderCacheKey12 & key, bool & foundButModified )
    {
        std::unique_lock<mutex> cacheMutexLock( m_cacheMutex );

        foundButModified = false;

        std::map<vaShaderCacheKey12, vaShaderCacheEntry12 *>::iterator it = m_cache.find( key );

        if( it != m_cache.end( ) )
        {
            if( ( *it ).second->IsModified( ) )
            {
                foundButModified = true;

                // Have to recompile...
                delete ( *it ).second;
                m_cache.erase( it );

                return nullptr;
            }

            return ( *it ).second->GetCompiledShader( );
        }
        return nullptr;
    }
    //
    void vaDirectX12ShaderManager::AddToCache( vaShaderCacheKey12 & key, const shared_ptr<vaShaderDataDX12> & shaderBlob, std::vector<vaShaderCacheEntry12::FileDependencyInfo> & dependencies )
    {
        std::unique_lock<mutex> cacheMutexLock( m_cacheMutex );

        std::map<vaShaderCacheKey12, vaShaderCacheEntry12 *>::iterator it = m_cache.find( key );

        if( it != m_cache.end( ) )
        {
            // Already in? can happen with parallel compilation I guess?
            // well let's just not do anything then
            return;
            // delete ( *it ).second;
            // m_cache.erase( it );
        }

        m_cache.insert( std::pair<vaShaderCacheKey12, vaShaderCacheEntry12 *>( key, new vaShaderCacheEntry12( shaderBlob, dependencies ) ) );
    }
    //
    vaShaderCacheEntry12::FileDependencyInfo::FileDependencyInfo( const wstring & filePath )
    {
        wstring fullFileName = vaDirectX12ShaderManager::GetInstance( ).FindShaderFile( filePath );

        if( fullFileName == L"" )
        {
            VA_WARN( L"Error trying to find shader file '%s'!", filePath.c_str() );

            assert( false );
            this->FilePath = L"";
            this->ModifiedTimeDate = 0;
        }
        else
        {
            this->FilePath = filePath;

            // struct _stat64 fileInfo;
            // _wstat64( fullFileName.c_str( ), &fileInfo ); // check error code?
            //this->ModifiedTimeDate = fileInfo.st_mtime;

            // maybe add some CRC64 here too? that would require reading contents of every file and every dependency which would be costly! 
            WIN32_FILE_ATTRIBUTE_DATA attrInfo;
            ::GetFileAttributesEx( fullFileName.c_str( ), GetFileExInfoStandard, &attrInfo );
            this->ModifiedTimeDate = ( ( (int64)attrInfo.ftLastWriteTime.dwHighDateTime ) << 32 ) | ( (int64)attrInfo.ftLastWriteTime.dwLowDateTime );
        }
    }
    //
    vaShaderCacheEntry12::FileDependencyInfo::FileDependencyInfo( const wstring & filePath, int64 modifiedTimeDate )
    {
        this->FilePath = filePath;
        this->ModifiedTimeDate = modifiedTimeDate;
    }
    //
    bool vaShaderCacheEntry12::FileDependencyInfo::IsModified( )
    {
        wstring fullFileName = vaDirectX12ShaderManager::GetInstance( ).FindShaderFile( this->FilePath.c_str( ) );

        //vaLog::GetInstance( ).Add( LOG_COLORS_SHADERS, (L"vaShaderCacheEntry12::FileDependencyInfo::IsModified, file name %s", fullFileName.c_str() ) );

        if( fullFileName == L"" )  // Can't find the file?
        {
            vaFileTools::EmbeddedFileData embeddedData = vaFileTools::EmbeddedFilesFind( vaStringTools::SimpleNarrow( wstring( L"shaders:\\" ) + this->FilePath ) );

            if( !embeddedData.HasContents( ) )
            {
                //vaLog::GetInstance( ).Add( LOG_COLORS_SHADERS, L"        > embedded data has NO contents" );
#ifdef STILL_LOAD_FROM_CACHE_IF_ORIGINAL_FILE_MISSING
                return false;
#else
                VA_WARN( L"Error trying to find shader file '%s'!", this->FilePath.c_str( ) );
                return true;
#endif
            }
            else
            {
                //vaLog::GetInstance( ).Add( LOG_COLORS_SHADERS,  L"        > embedded data has contents" );
                return this->ModifiedTimeDate != embeddedData.TimeStamp;
            }
        }

        // struct _stat64 fileInfo;
        // _wstat64( fullFileName.c_str( ), &fileInfo ); // check error code?
        // return this->ModifiedTimeDate != fileInfo.st_mtime;

        // maybe add some CRC64 here too? that would require reading contents of every file and every dependency which would be costly! 
        WIN32_FILE_ATTRIBUTE_DATA attrInfo;
        ::GetFileAttributesEx( fullFileName.c_str( ), GetFileExInfoStandard, &attrInfo );
        bool ret = this->ModifiedTimeDate != ( ( ( (int64)attrInfo.ftLastWriteTime.dwHighDateTime ) << 32 ) | ( (int64)attrInfo.ftLastWriteTime.dwLowDateTime ) );

        // if( ret )
        // {
        //     vaLog::GetInstance( ).Add( LOG_COLORS_SHADERS,  (const wchar_t*)((ret)?(L"   > shader file '%s' modified "):(L"   > shader file '%s' NOT modified ")), fullFileName.c_str() );
        // }

        return ret;
    }
    //
    vaShaderCacheEntry12::vaShaderCacheEntry12( const shared_ptr<vaShaderDataDX12> & compiledShader, std::vector<vaShaderCacheEntry12::FileDependencyInfo> & dependencies )
    {
        m_compiledShader = compiledShader;
        m_dependencies = dependencies;
    }
    //
    vaShaderCacheEntry12::~vaShaderCacheEntry12( )
    {
    }
    //
    bool vaShaderCacheEntry12::IsModified( )
    {
        for( std::vector<vaShaderCacheEntry12::FileDependencyInfo>::iterator it = m_dependencies.begin( ); it != m_dependencies.end( ); ++it )
        {
            if( ( *it ).IsModified( ) )
                return true;
        }
        return false;
    }
    //
    void vaShaderCacheKey12::Save( vaStream & outStream ) const
    {
        outStream.WriteString( this->StringPart.c_str( ) );
    }
    //
    void vaShaderCacheEntry12::FileDependencyInfo::Save( vaStream & outStream ) const
    {
        outStream.WriteString( this->FilePath.c_str( ) );
        outStream.WriteValue<int64>( this->ModifiedTimeDate );
    }
    //
    void vaShaderCacheEntry12::Save( vaStream & outStream ) const
    {
        outStream.WriteValue<int32>( ( int32 )this->m_dependencies.size( ) );       // number of dependencies

        for( std::vector<vaShaderCacheEntry12::FileDependencyInfo>::const_iterator it = m_dependencies.cbegin( ); it != m_dependencies.cend( ); ++it )
        {
            ( *it ).Save( outStream );
        }

        int bufferSize = (int)m_compiledShader->GetBufferSize( );

        outStream.WriteValue<int32>( bufferSize );
        outStream.Write( m_compiledShader->GetBufferPointer( ), bufferSize );
    }
    //
    bool vaShaderCacheKey12::Load( vaStream & inStream )
    {
        return inStream.ReadString( this->StringPart );
    }
    //
    bool vaShaderCacheEntry12::FileDependencyInfo::Load( vaStream & inStream )
    {
        return inStream.ReadString( this->FilePath ) && inStream.ReadValue<int64>( this->ModifiedTimeDate );
    }
    //
    bool vaShaderCacheEntry12::Load( vaStream & inStream )
    {
        int dependencyCount;
        if( !inStream.ReadValue<int32>( dependencyCount ) || dependencyCount < 0 )
            return false;

        for( int i = 0; i < dependencyCount; i++ )
        {
            auto dependencyInfo = vaShaderCacheEntry12::FileDependencyInfo( );
            if( !dependencyInfo.Load(inStream) )
                return false;
            m_dependencies.push_back( std::move(dependencyInfo) );
        }

        int bufferSize;
        if( !inStream.ReadValue<int32>( bufferSize ) || bufferSize < 0 )
            return false;

        assert( m_compiledShader == nullptr );
        m_compiledShader = std::make_shared<vaShaderDataDX12>( bufferSize );
        
        if( !inStream.Read( m_compiledShader->GetBufferPointer(), bufferSize ) )
        {
            m_compiledShader = nullptr;
            return false;
        }
        return true;
    }
}

void RegisterShaderDX12( )
{
    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX12, vaPixelShader,    vaPixelShaderDX12       );
    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX12, vaComputeShader,  vaComputeShaderDX12     );
    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX12, vaHullShader,     vaHullShaderDX12        );
    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX12, vaDomainShader,   vaDomainShaderDX12      );
    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX12, vaGeometryShader, vaGeometryShaderDX12    );
    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX12, vaVertexShader,   vaVertexShaderDX12      );
    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX12, vaShaderLibrary,  vaShaderLibraryDX12     );
}
