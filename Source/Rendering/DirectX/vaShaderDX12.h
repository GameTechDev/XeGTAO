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

#include "Rendering/DirectX/vaDirectXIncludes.h"
#include "Rendering/vaRenderingIncludes.h"

namespace Vanilla
{
    struct vaShaderCacheKey12;
    class vaShaderCacheEntry12;

    // This wraps the ID3DBlob/IDxcBlob into a vaFramePtr-castable smart ptr to avoid costly high-contention refcounting by the
    // refcounter in dxcompiler.dll (or the horrible horrible crashy thread-unsafe one in the old D3dcompiler.h:D3DCreateBlob).
    class vaShaderDataDX12 final: public ID3DBlob, public vaFramePtrTag//, public std::enable_shared_from_this<vaShaderDataDX12>
    {
        byte *                          m_buffer        = nullptr;
        size_t                          m_bufferSize    = 0;

    public:
        vaShaderDataDX12( size_t bufferSize )
        {
            m_buffer        = new byte[bufferSize];
            m_bufferSize    = bufferSize;
        }
        ~vaShaderDataDX12( )
        {
            delete[] m_buffer;
        }

        virtual LPVOID STDMETHODCALLTYPE    GetBufferPointer( void ) override      { return m_buffer; }
        virtual SIZE_T STDMETHODCALLTYPE    GetBufferSize( void ) override         { return m_bufferSize; }

    protected:
        // No one should be using this as a COM object - it's there just to provide ID3DBlob interface
        virtual HRESULT STDMETHODCALLTYPE   QueryInterface( /* [in] */ REFIID riid, /* [iid_is][out] */ _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject ) override    { riid; ppvObject; assert( false ); return E_UNEXPECTED; }
        virtual ULONG STDMETHODCALLTYPE     AddRef( void )                                                                                                              { assert( false ); return (ULONG)-1;           }
        virtual ULONG STDMETHODCALLTYPE     Release( void )                                                                                                             { assert( false ); return (ULONG)-1;           }
    };

    class vaInputLayoutDataDX12 final : public vaFramePtrTag//, public std::enable_shared_from_this<vaInputLayoutDataDX12>
    {
        vaVertexInputLayoutDesc         m_inputLayout;
        std::vector< D3D12_INPUT_ELEMENT_DESC >
                                        m_inputLayoutDX12;
    public:
        vaInputLayoutDataDX12( const vaVertexInputLayoutDesc & inLayout );

        const std::vector< D3D12_INPUT_ELEMENT_DESC > &
                                        Layout( ) const     { return m_inputLayoutDX12; }
    };

    class vaShaderDX12 : public virtual vaShader
    {
    protected:
        std::shared_ptr<vaShaderDataDX12>
                                        m_shaderData;

    public:
        vaShaderDX12( const vaRenderingModuleParams & params );
        virtual ~vaShaderDX12( );
        //
        virtual void                    Clear( bool lockWorkerMutex ) override;
        //
        virtual bool                    IsCreated( ) override 
        {
            std::shared_lock allShaderDataReadLock( m_allShaderDataMutex, std::try_to_lock ); 
            if( !allShaderDataReadLock.owns_lock( ) ) 
                return false; 
            return m_shaderData != nullptr; 
        }
        //
        // Let's stick to SM6.3 for now!
        static const char *             GetSMVersionStatic( )                       { return "6_3"; }
        //
        virtual const char *            GetSMVersion( ) override                    { return GetSMVersionStatic(); }
        //
        // D3D12_SHADER_BYTECODE           GetBytecode( )          { if( IsCreated() ) return CD3DX12_SHADER_BYTECODE( m_shaderBlob.Get() ); else return { nullptr, 0 }; }
        //
        vaShader::State                 GetShader( vaFramePtr<vaShaderDataDX12> & outData, int64 & outUniqueContentsID );
        //
    protected:
        virtual void                    CreateCacheKey( vaShaderCacheKey12 & outKey );
        //
    protected:
        //
        virtual void                    DestroyShader( ) override { DestroyShaderBase( ); }
        void                            DestroyShaderBase( );
        //
        shared_ptr<vaShaderDataDX12>    CreateShaderBase( bool & loadedFromCache );
        //
        virtual void                    CreateShader( ) override;
        //
        void                            SafeDestruct( );
    };

 #pragma warning ( push )
 #pragma warning ( disable : 4250 )

    class vaPixelShaderDX12 : public vaShaderDX12, public vaPixelShader
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );
    public:
        vaPixelShaderDX12( const vaRenderingModuleParams & params ) : vaShaderDX12( params ), vaShader( params ) { }
        virtual ~vaPixelShaderDX12( ) { SafeDestruct(); }
    };

    class vaComputeShaderDX12 : public vaShaderDX12, public vaComputeShader
    {
    public:
        vaComputeShaderDX12( const vaRenderingModuleParams & params ) : vaShaderDX12( params ), vaShader( params ) { }
        virtual ~vaComputeShaderDX12( ) { SafeDestruct(); }
    };

    class vaShaderLibraryDX12 : public vaShaderDX12, public vaShaderLibrary
    {
    public:
        vaShaderLibraryDX12( const vaRenderingModuleParams & params ) : vaShaderDX12( params ), vaShader( params ) { }
        virtual ~vaShaderLibraryDX12( ) { SafeDestruct(); }
    };

    class vaHullShaderDX12 : public vaShaderDX12, public vaHullShader
    {
    public:
        vaHullShaderDX12( const vaRenderingModuleParams & params ) : vaShaderDX12( params ), vaShader( params ) { }
        virtual ~vaHullShaderDX12( ) { SafeDestruct(); }
    };

    class vaDomainShaderDX12 : public vaShaderDX12, public vaDomainShader
    {
    public:
        vaDomainShaderDX12( const vaRenderingModuleParams & params ) : vaShaderDX12( params ), vaShader( params ) { }
        virtual ~vaDomainShaderDX12( ) { SafeDestruct(); }
    };

    class vaGeometryShaderDX12 : public vaShaderDX12, public vaGeometryShader
    {
    public:
        vaGeometryShaderDX12( const vaRenderingModuleParams & params ) : vaShaderDX12( params ), vaShader( params ) { }
        virtual ~vaGeometryShaderDX12( ) { SafeDestruct(); }
    };

    class vaVertexShaderDX12 : public vaShaderDX12, public vaVertexShader
    {
        shared_ptr< vaInputLayoutDataDX12 >
                                    m_inputLayoutDX12;

//    protected:
//        void                        SetInputLayout( D3D12_INPUT_ELEMENT_DESC * elements, uint32 elementCount );

    public:
        vaVertexShaderDX12( const vaRenderingModuleParams & params ) : vaShaderDX12( params ), vaShader( params ) { }
        virtual ~vaVertexShaderDX12( );

    public:
        virtual void                CompileVSAndILFromFile( const string & filePath, const string & entryPoint, const std::vector<vaVertexInputElementDesc> & inputLayoutElements, const vaShaderMacroContaner & macros, bool forceImmediateCompile ) override;
        virtual void                CompileVSAndILFromBuffer( const string & shaderCode, const string & entryPoint, const std::vector<vaVertexInputElementDesc> & inputLayoutElements, const vaShaderMacroContaner & macros, bool forceImmediateCompile ) override;

        vaShader::State             GetShader( vaFramePtr<vaShaderDataDX12> & outData, vaFramePtr<vaInputLayoutDataDX12> & outInputLayout, int64 & outUniqueContentsID );

        // // platform independent <-> dx conversions
        // static vaVertexInputLayoutDesc  LayoutVAFromDX( D3D12_INPUT_ELEMENT_DESC * elements, uint32 elementCount );

        // !!warning!! returned char pointers in descriptors outLayout point to inLayout values so make sure you keep inLayout alive until outLayout is alive
        static void                 LayoutDXFromVA( shared_ptr< vaInputLayoutDataDX12 > & outLayout, const vaVertexInputLayoutDesc & inLayout );

    public:

    protected:
        virtual void                CreateShader( ) override;
        virtual void                DestroyShader( ) override;

        virtual void                CreateCacheKey( vaShaderCacheKey12 & outKey );
    };

#pragma warning ( pop )

    struct vaShaderCacheKey12
    {
        std::string                StringPart;

        bool                       operator == ( const vaShaderCacheKey12 & cmp ) const { return this->StringPart == cmp.StringPart; }
        bool                       operator < ( const vaShaderCacheKey12 & cmp ) const { return this->StringPart < cmp.StringPart; }
        bool                       operator >( const vaShaderCacheKey12 & cmp ) const { return this->StringPart > cmp.StringPart; }

        void                       Save( vaStream & outStream ) const;
        bool                       Load( vaStream & inStream );
    };

    class vaShaderCacheEntry12
    {
    public:
        struct FileDependencyInfo
        {
            std::wstring            FilePath;
            int64                   ModifiedTimeDate;

            FileDependencyInfo( ) : FilePath( L"" ), ModifiedTimeDate( 0 ) { }
            FileDependencyInfo( const wstring & filePath );
            FileDependencyInfo( const wstring & filePath, int64 modifiedTimeDate );

            bool                    IsModified( );

            void                    Save( vaStream & outStream ) const;
            bool                    Load( vaStream & inStream );
        };

    private:

        std::shared_ptr<vaShaderDataDX12>                       m_compiledShader;
        std::vector<vaShaderCacheEntry12::FileDependencyInfo>   m_dependencies;

    private:
        explicit vaShaderCacheEntry12( const vaShaderCacheEntry12 & copy ) { copy; }
    public:
        vaShaderCacheEntry12( const std::shared_ptr<vaShaderDataDX12> & compiledShader, std::vector<vaShaderCacheEntry12::FileDependencyInfo> & dependencies );
        explicit vaShaderCacheEntry12( ) { m_compiledShader = NULL; }
        ~vaShaderCacheEntry12( );

        bool                        IsModified( );
        std::shared_ptr<vaShaderDataDX12>
                                    GetCompiledShader( ) { return m_compiledShader; }

        void                        Save( vaStream & outStream ) const;
        bool                        Load( vaStream & inStream );
    };

    // Singleton utility class for handling shaders
    class vaDirectX12ShaderManager : public vaShaderManager, public vaSingletonBase < vaDirectX12ShaderManager > // oooo I feel so dirty here but oh well
    {
    private:
        friend class vaShaderSharedManager;
        friend class vaMaterialManager;
        friend class vaShaderDX12;

    private:
        std::map<vaShaderCacheKey12, vaShaderCacheEntry12 *>    m_cache;
        mutable mutex                                       m_cacheMutex;
#ifdef VA_SHADER_CACHE_PERSISTENT_STORAGE_ENABLE
        wstring                                             m_cacheFilePath;

        // since cache loading from the disk happens only once, use this to ensure loading proc has grabbed m_cacheMutex - once it's unlocked we know it finished
        std::atomic_bool                                    m_cacheLoadStarted = false;
        std::mutex                                          m_cacheLoadStartedMutex;
        std::condition_variable                             m_cacheLoadStartedCV;
#endif

        shared_ptr<int>                                     m_objLifetimeToken;

    public:
        vaDirectX12ShaderManager( vaRenderDevice & device );
        ~vaDirectX12ShaderManager( );

    public:
        shared_ptr<vaShaderDataDX12>FindInCache( vaShaderCacheKey12 & key, bool & foundButModified );
        void                        AddToCache( vaShaderCacheKey12 & key, const shared_ptr<vaShaderDataDX12> & shaderBlob, std::vector<vaShaderCacheEntry12::FileDependencyInfo> & dependencies );
        void                        ClearCache( );

        // pushBack (searched last) or pushFront (searched first)
        virtual void                RegisterShaderSearchPath( const std::wstring & path, bool pushBack = true )     override;
        virtual wstring             FindShaderFile( const wstring & fileName )                                      override;
        virtual wstring             GetCacheStoragePath( ) const override                                           { return m_cacheFilePath; }

    private:
        void                        ClearCacheInternal( );          // will not lock m_cacheMutex - has to be done before calling!!
        void                        LoadCacheInternal( );           // will not lock m_cacheMutex - has to be done before calling!!
        void                        SaveCacheInternal( ) const;     // will not lock m_cacheMutex - has to be done before calling!!
    };

    inline vaShader::State          vaShaderDX12::GetShader( vaFramePtr<vaShaderDataDX12> & outData, int64 & outUniqueContentsID )
    { 
        {
            std::shared_lock allShaderDataReadLock( m_allShaderDataMutex, std::try_to_lock ); 
            if (!allShaderDataReadLock.owns_lock())     // don't block, don't wait
            {
                outData             = nullptr;
                outUniqueContentsID = -1;
                return vaShader::State::Uncooked;   // if there's a lock on it then it's either being compiled (most likely) or reset or similar - the end result is the same, shader is potentially valid and will probably be available later
            }
            outData             = m_shaderData;
            outUniqueContentsID = m_uniqueContentsID; 
        }
        vaShader::State ret = m_state;
        //Clear( true );
        return ret; 
    }

    inline vaShader::State          vaVertexShaderDX12::GetShader( vaFramePtr<vaShaderDataDX12> & outData, vaFramePtr<vaInputLayoutDataDX12> & outInputLayout, int64 & outUniqueContentsID )
    { 
        std::shared_lock allShaderDataReadLock( m_allShaderDataMutex, std::try_to_lock ); 
        if (!allShaderDataReadLock.owns_lock())     // don't block, don't wait
        {
            outData             = nullptr;
            outUniqueContentsID = -1;
            outInputLayout      = nullptr;
            return vaShader::State::Uncooked;   // if there's a lock on it then it's either being compiled (most likely) or reset or similar - the end result is the same, shader is potentially valid and will probably be available later
        }
        outData             = m_shaderData; 
        outUniqueContentsID = m_uniqueContentsID; 
        outInputLayout      = m_inputLayoutDX12;
        return m_state; 
    }

    inline vaVertexShaderDX12 &     AsDX12( vaVertexShader & shader )       { return *shader.SafeCast<vaVertexShaderDX12*>(); }
    inline vaVertexShaderDX12 *     AsDX12( vaVertexShader * shader )       { return shader->SafeCast<vaVertexShaderDX12*>(); }
    
    inline vaPixelShaderDX12 &      AsDX12( vaPixelShader & shader )        { return *shader.SafeCast<vaPixelShaderDX12*>(); }
    inline vaPixelShaderDX12 *      AsDX12( vaPixelShader * shader )        { return shader->SafeCast<vaPixelShaderDX12*>(); }
    
    inline vaGeometryShaderDX12 &   AsDX12( vaGeometryShader & shader )     { return *shader.SafeCast<vaGeometryShaderDX12*>(); }
    inline vaGeometryShaderDX12 *   AsDX12( vaGeometryShader * shader )     { return shader->SafeCast<vaGeometryShaderDX12*>(); }
    
    inline vaDomainShaderDX12 &     AsDX12( vaDomainShader & shader )       { return *shader.SafeCast<vaDomainShaderDX12*>(); }
    inline vaDomainShaderDX12 *     AsDX12( vaDomainShader * shader )       { return shader->SafeCast<vaDomainShaderDX12*>(); }
    
    inline vaHullShaderDX12 &       AsDX12( vaHullShader & shader )         { return *shader.SafeCast<vaHullShaderDX12*>(); }
    inline vaHullShaderDX12 *       AsDX12( vaHullShader * shader )         { return shader->SafeCast<vaHullShaderDX12*>(); }

    inline vaComputeShaderDX12 &    AsDX12( vaComputeShader & shader )      { return *shader.SafeCast<vaComputeShaderDX12*>(); }
    inline vaComputeShaderDX12 *    AsDX12( vaComputeShader * shader )      { return shader->SafeCast<vaComputeShaderDX12*>(); }
   
    inline vaShaderLibraryDX12 &    AsDX12( vaShaderLibrary & shader )      { return *shader.SafeCast<vaShaderLibraryDX12*>(); }
    inline vaShaderLibraryDX12 *    AsDX12( vaShaderLibrary * shader )      { return shader->SafeCast<vaShaderLibraryDX12*>(); }
}