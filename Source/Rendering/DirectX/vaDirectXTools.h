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

#include "Core/System/vaMemoryStream.h"

#include "Rendering/DirectX/vaDirectXIncludes.h"

#include "Rendering/vaTexture.h"
#include "Rendering/DirectX/vaShaderDX12.h"

#include "Core/Misc/vaResourceFormats.h"

#include <wrl/client.h>

namespace Vanilla
{
    class vaStream;
    class vaSimpleProfiler;

    //class vaDirectXNotifyTarget;

    class vaDirectXTools12 final
    {
    public:

        static bool FillShaderResourceViewDesc( D3D12_SHADER_RESOURCE_VIEW_DESC & outDesc, ID3D12Resource * resource, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, int mipSliceMin = 0, int mipSliceCount = -1, int arraySliceMin = 0, int arraySliceCount = -1, bool isCubemap = false );
        static bool FillDepthStencilViewDesc( D3D12_DEPTH_STENCIL_VIEW_DESC & outDesc, ID3D12Resource * resource, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, int mipSliceMin = 0, int arraySliceMin = 0, int arraySliceCount = -1 );
        static bool FillRenderTargetViewDesc( D3D12_RENDER_TARGET_VIEW_DESC & outDesc, ID3D12Resource * resource, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, int mipSliceMin = 0, int arraySliceMin = 0, int arraySliceCount = -1 );
        static bool FillUnorderedAccessViewDesc( D3D12_UNORDERED_ACCESS_VIEW_DESC & outDesc, ID3D12Resource * resource, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, int mipSliceMin = 0, int arraySliceMin = 0, int arraySliceCount = -1 );

        static void FillSamplerStatePointClamp( D3D12_STATIC_SAMPLER_DESC & outDesc );
        static void FillSamplerStatePointWrap( D3D12_STATIC_SAMPLER_DESC & outDesc );
        static void FillSamplerStateLinearClamp( D3D12_STATIC_SAMPLER_DESC & outDesc );
        static void FillSamplerStateLinearWrap( D3D12_STATIC_SAMPLER_DESC & outDesc );
        static void FillSamplerStateAnisotropicClamp( D3D12_STATIC_SAMPLER_DESC & outDesc );
        static void FillSamplerStateAnisotropicWrap( D3D12_STATIC_SAMPLER_DESC & outDesc );
        static void FillSamplerStateShadowCmp( D3D12_STATIC_SAMPLER_DESC & outDesc );

        // VERY simple - expand when needed :)
        static void FillBlendState( D3D12_BLEND_DESC & outDesc, vaBlendMode blendMode );

        // isDDS==true path will not allocate memory in outData and outSubresources will point into dataBuffer so make sure to keep it alive
        static bool LoadTexture( ID3D12Device * device, void * dataBuffer, uint64 dataBufferSize, vaTextureLoadFlags loadFlags, vaResourceBindSupportFlags bindFlags, ID3D12Resource *& outResource, std::vector<D3D12_SUBRESOURCE_DATA> & outSubresources, std::unique_ptr<byte[]> & outData, bool & outIsCubemap ); // ,  uint64 * outCRC
        
        // both isDDS paths will allocate memory in outData and outSubresources will point into it so make sure to keep it alive
        static bool LoadTexture( ID3D12Device * device, const wchar_t * filePath, bool isDDS, vaTextureLoadFlags loadFlags, vaResourceBindSupportFlags bindFlags, ID3D12Resource *& outResource, std::vector<D3D12_SUBRESOURCE_DATA> & outSubresources, std::unique_ptr<byte[]> & outData, bool & outIsCubemap ); 

        static bool SaveDDSTexture( Vanilla::vaStream& outStream, _In_ ID3D12CommandQueue* pCommandQueue, _In_ ID3D12Resource* pSource, _In_ bool isCubeMap, _In_ D3D12_RESOURCE_STATES beforeState, _In_ D3D12_RESOURCE_STATES afterState );
    };

    // yep, quite horrible
    using Microsoft::WRL::ComPtr;

#if 0
    //! A template COM smart pointer. I don't remember why I'm not using ComPtr which would probably be a better idea. TODO I guess?
    template<typename I>
    class vaCOMSmartPtr
    {
    public:
        //! Constructs an empty smart pointer.
        vaCOMSmartPtr( ) : m_p( 0 ) {}

        //! Assumes ownership of the given instance, if non-null.
        /*! \param	p	A pointer to an existing COM instance, or 0 to create an
                    empty COM smart pointer.
                    \note The smart pointer will assume ownership of the given instance.
                    It will \b not AddRef the contents, but it will Release the object
                    as it goes out of scope.
                    */
        vaCOMSmartPtr( I* p ) : m_p( p ) {}

        //! Releases the contained instance.
        ~vaCOMSmartPtr( )
        {
            SafeRelease( );
        }

        //! Copy-construction.
        vaCOMSmartPtr( vaCOMSmartPtr<I> const& ptr ) : m_p( ptr.m_p )
        {
            if( m_p )
                m_p->AddRef( );
        }

        //! Assignment.
        vaCOMSmartPtr<I>& operator=( vaCOMSmartPtr<I> const& ptr )
        {
            vaCOMSmartPtr<I> copy( ptr );
            Swap( copy );
            return *this;
        }

        //! Releases a contained instance, if present.
        /*! \note You should never need to call this function unless you wish to
           take control a Release an instance before the smart pointer goes out
           of scope.
           */
        void SafeRelease( )
        {
            if( m_p )
                m_p->Release( );
            m_p = 0;
        }

        //! First release existing ptr if any and then explicitly get the address of the pointer.
        I** ReleaseAndGetAddressOf( )
        {
            SafeRelease( );
            return &m_p;
        }

        //! Explicitly gets the address of the pointer.
        /*! \note This function should not be called on a smart pointer with
           non-zero contents. This is to avoid memory leaks by blatting over the
           contents without calling Release. Hence the complaint to std::cerr.
           */
        I** AddressOf( )
        {
            if( m_p != nullptr )
            {
                assert( false );
                // std::cerr << __FUNCTION__
                //     << ": non-zero contents - possible memory leak" << std::endl;
            }
            return &m_p;
        }

        //! Gets the address of the pointer using the address of operator.
        /*! \note This function should not be called on a smart pointer with
           non-zero contents. This is to avoid memory leaks by blatting over the
           contents without calling Release. Hence the complaint to std::cerr.
           */
        I** operator&( )
        {
            if( m_p != nullptr )
            {
                assert( false );
                // std::cerr << __FUNCTION__
                //     << ": non-zero contents - possible memory leak" << std::endl;
            }
            return &m_p;
        }

        //! Gets the encapsulated pointer.
        I* Get( ) const { return m_p; }

        //! Gets the encapsulated pointer.
        I* operator->( ) const { return m_p; }

        //! Swaps the encapsulated pointer with that of the argument.
        void Swap( vaCOMSmartPtr<I>& ptr )
        {
            I* p = m_p;
            m_p = ptr.m_p;
            ptr.m_p = p;
        }

        //! Gets the encapsulated pointer resets the smart pointer with without releasing the pointer.
        I* Detach( )
        {
            I * ret = m_p;
            m_p = nullptr;
            return ret;
        }

        // query for U interface
        HRESULT CopyFrom( IUnknown * inPtr )
        {
            I* outPtr = nullptr;
            HRESULT hr = inPtr->QueryInterface( __uuidof( I ), reinterpret_cast<void**>( &outPtr ) );
            if( SUCCEEDED( hr ) )
            {
                SafeRelease( );
                m_p = outPtr;
            }
            return hr;
        }

        template<typename U>
        HRESULT CopyFrom( const vaCOMSmartPtr<U> & inPtr )
        {
            return CopyFrom( inPtr.Get( ) );
        }

        //! Returns true if non-empty.
        operator bool( ) const { return m_p != 0; }

    private:
        //! The encapsulated instance.
        I* m_p;
    };
#endif

    inline DXGI_FORMAT DXGIFormatFromVA( vaResourceFormat format )       
    { 
        return (DXGI_FORMAT)format;
    }

    inline vaResourceFormat VAFormatFromDXGI( DXGI_FORMAT format )
    {
        return (vaResourceFormat)format;
    }

    inline vaResourceBindSupportFlags BindFlagsVAFromDX12( D3D12_RESOURCE_FLAGS resFlags )
    {
        vaResourceBindSupportFlags ret = vaResourceBindSupportFlags::ShaderResource;
        if( ( resFlags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE ) != 0 )
            ret &= ~vaResourceBindSupportFlags::ShaderResource;
        if( ( resFlags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET ) != 0 )
            ret |= vaResourceBindSupportFlags::RenderTarget;
        if( ( resFlags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL ) != 0 )
            ret |= vaResourceBindSupportFlags::DepthStencil;
        if( ( resFlags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS ) != 0 )
            ret |= vaResourceBindSupportFlags::UnorderedAccess;
        return ret;
    }

    inline D3D12_RESOURCE_FLAGS ResourceFlagsDX12FromVA( vaResourceBindSupportFlags bindFlags )
    {
        D3D12_RESOURCE_FLAGS ret = D3D12_RESOURCE_FLAG_NONE;
        if( ( bindFlags & vaResourceBindSupportFlags::VertexBuffer ) != 0 )
            ret |= D3D12_RESOURCE_FLAG_NONE;
        if( ( bindFlags & vaResourceBindSupportFlags::IndexBuffer ) != 0 )
            ret |= D3D12_RESOURCE_FLAG_NONE;
        if( ( bindFlags & vaResourceBindSupportFlags::ConstantBuffer ) != 0 )
            ret |= D3D12_RESOURCE_FLAG_NONE;
        if( ( bindFlags & vaResourceBindSupportFlags::ShaderResource ) != 0 )
            ret |= D3D12_RESOURCE_FLAG_NONE;
        if( ( bindFlags & vaResourceBindSupportFlags::RenderTarget ) != 0 )
            ret |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if( ( bindFlags & vaResourceBindSupportFlags::DepthStencil ) != 0 )
        {
            ret |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            if( ( bindFlags & (vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::ConstantBuffer | vaResourceBindSupportFlags::UnorderedAccess | vaResourceBindSupportFlags::RenderTarget) ) == 0 ) // not sure about vertex/index but why would anyone want same buffer bound as depth and vert/ind buff? 
                ret |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
        }
        if( ( bindFlags & vaResourceBindSupportFlags::UnorderedAccess ) != 0 )
            ret |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return ret;
    }

    inline D3D12_HEAP_TYPE HeapTypeDX12FromAccessFlags( vaResourceAccessFlags accessFlags )
    {
        if( accessFlags == vaResourceAccessFlags::Default )         return D3D12_HEAP_TYPE_DEFAULT; 
        if( ( accessFlags & vaResourceAccessFlags::CPUWrite) != 0 ) return D3D12_HEAP_TYPE_UPLOAD;  
        if( ( accessFlags & vaResourceAccessFlags::CPURead ) != 0 ) return D3D12_HEAP_TYPE_READBACK;
        assert( false ); return (D3D12_HEAP_TYPE)0;
    }

     class vaRenderDeviceDX12;
    class vaRenderDeviceContextDX12;
    class vaRenderDeviceContextBaseDX12;

    // Resource state transition manager - initial implementation is naive but extendable while the interface & usage should remain the same
    //  * Intended to be inherited from - if you need to use it as a standalone variable that's fine, either change const/dest & methods to public or inherit and expose.
    //  * Must manually attach and detach
    class vaResourceStateTransitionHelperDX12
    {
    private:
        ComPtr<ID3D12Resource>              m_rsthResource;
        D3D12_RESOURCE_STATES               m_rsthCurrent = D3D12_RESOURCE_STATE_COMMON;

        // this currently only contains those different from m_rsthCurrent
        // alternative would be to track all subresources - this would use storage even when no subresources are transitioned independently but could be cleaner?
        std::vector< pair< uint32, D3D12_RESOURCE_STATES > >
                                            m_rsthSubResStates;

    public:
        vaResourceStateTransitionHelperDX12( ) { }
        ~vaResourceStateTransitionHelperDX12( ) { assert( m_rsthResource == nullptr ); }    // since this works as an 'attachment' then make sure it's been detached correctly

        void                                            RSTHAttach( ID3D12Resource * resource, D3D12_RESOURCE_STATES current )              { assert( m_rsthResource == nullptr ); m_rsthResource = resource; m_rsthCurrent = current; }
        void                                            RSTHDetach( ID3D12Resource * resource )                                             { resource; assert( m_rsthResource != nullptr && m_rsthResource.Get( ) == resource ); m_rsthResource.Reset( ); m_rsthCurrent = D3D12_RESOURCE_STATE_COMMON; m_rsthSubResStates.clear(); }
        const ComPtr<ID3D12Resource> &                  RSTHGetResource( ) const                                                            { return m_rsthResource; }
        D3D12_RESOURCE_STATES                           RSTHGetCurrentState( ) const                                                        { assert( m_rsthSubResStates.size() == 0 ); assert( m_rsthResource != nullptr ); return m_rsthCurrent; } // if this asserts, call RSTHTransitionSubResUnroll before

        bool                                            IsRSTHTransitionRequired( vaRenderDeviceContextBaseDX12 & context, D3D12_RESOURCE_STATES target, uint32 subResIndex = -1 );
        void                                            RSTHTransition( vaRenderDeviceContextBaseDX12 & context, D3D12_RESOURCE_STATES target, uint32 subResIndex = -1 );
        void                                            RSTHTransitionSubResUnroll( vaRenderDeviceContextBaseDX12 & context );
        void                                            RSTHAdoptResourceState( vaRenderDeviceContextBaseDX12 & context, D3D12_RESOURCE_STATES target, uint32 subResIndex = -1 );
        
    private:
        void                                            RSTHTransitionSubRes( vaRenderDeviceContextBaseDX12 & context, D3D12_RESOURCE_STATES target, uint32 subResIndex  );
    };

    // Resource view helpers - unlike DX11 views (ID3D11ShaderResourceView) these do not hold a reference to the actual resource
    // but they can get safely re-initialized with a different resource at runtime by doing Destroy->Create. Old descriptor is
    // kept alive until the frame is done.
    class vaResourceViewDX12
    {
    protected:
        vaRenderDeviceDX12 &                m_device;
        const D3D12_DESCRIPTOR_HEAP_TYPE    m_type = (D3D12_DESCRIPTOR_HEAP_TYPE)-1;
        int                                 m_heapIndex = -1;
        D3D12_CPU_DESCRIPTOR_HANDLE         m_CPUHandle = { 0 };
        D3D12_GPU_DESCRIPTOR_HANDLE         m_GPUHandle = { 0 };

        // this is only for ClearUAVs and similar which require that "descriptor must not be in a shader-visible descriptor heap." (see https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-clearunorderedaccessviewuint)
        int                                 m_CPUReadableHeapIndex = -1;
        D3D12_CPU_DESCRIPTOR_HANDLE         m_CPUReadableCPUHandle = { 0 };
        D3D12_GPU_DESCRIPTOR_HANDLE         m_CPUReadableGPUHandle = { 0 };

    protected:
        vaResourceViewDX12( vaRenderDeviceDX12 & device, D3D12_DESCRIPTOR_HEAP_TYPE type ) : m_device( device ), m_type( type ) { }
        void                                Allocate( bool allocateCPUReadableToo );

    public:
        ~vaResourceViewDX12( );

        bool                                IsCreated( ) const                  { return m_heapIndex != -1; }
        void                                SafeRelease( );

        uint32                              GetBindlessIndex( ) const           { assert( m_heapIndex >= 0 ); return (uint32)m_heapIndex; }

        const D3D12_CPU_DESCRIPTOR_HANDLE & GetCPUHandle( ) const               { return m_CPUHandle; }
        const D3D12_GPU_DESCRIPTOR_HANDLE & GetGPUHandle( ) const               { return m_GPUHandle; }
        const D3D12_CPU_DESCRIPTOR_HANDLE & GetCPUReadableCPUHandle( ) const    { return m_CPUReadableCPUHandle; }
        const D3D12_GPU_DESCRIPTOR_HANDLE & GetCPUReadableGPUHandle( ) const    { return m_CPUReadableGPUHandle; }
    };

    // I think this needs to go out as it's no longer used; leaving in for some future cleanup
    class vaConstantBufferViewDX12 : public vaResourceViewDX12
    {
        // this is left in for convenience & debugging purposes
        D3D12_CONSTANT_BUFFER_VIEW_DESC     m_desc = { };

    public:
        vaConstantBufferViewDX12( vaRenderDeviceDX12 & device ) : vaResourceViewDX12( device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ) { }

    public:
        const D3D12_CONSTANT_BUFFER_VIEW_DESC & GetDesc( ) const                            { return m_desc; }
        void                                Create( const D3D12_CONSTANT_BUFFER_VIEW_DESC & desc );
        void                                CreateNull( );
    };

    class vaShaderResourceViewDX12 : public vaResourceViewDX12
    {
        // this is left in for convenience & debugging purposes
        D3D12_SHADER_RESOURCE_VIEW_DESC     m_desc = { };

    public:
        vaShaderResourceViewDX12( vaRenderDeviceDX12 & device ) : vaResourceViewDX12( device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ) { }

    public:
        const D3D12_SHADER_RESOURCE_VIEW_DESC & GetDesc( ) const                            { return m_desc; }
        void                                Create( ID3D12Resource * resource, const D3D12_SHADER_RESOURCE_VIEW_DESC & desc );
        void                                CreateNull( );
    };

    class vaUnorderedAccessViewDX12 : public vaResourceViewDX12
    {
        // this is left in for convenience & debugging purposes
        D3D12_UNORDERED_ACCESS_VIEW_DESC    m_desc = { };

    public:
        vaUnorderedAccessViewDX12( vaRenderDeviceDX12 & device ) : vaResourceViewDX12( device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ) { }

    public:
        const D3D12_UNORDERED_ACCESS_VIEW_DESC & GetDesc( ) const                            { return m_desc; }
        void                                Create( ID3D12Resource *resource, ID3D12Resource * counterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC & desc );
        void                                CreateNull( D3D12_UAV_DIMENSION dimension );
    };

    class vaRenderTargetViewDX12 : public vaResourceViewDX12
    {
        // this is left in for convenience & debugging purposes
        D3D12_RENDER_TARGET_VIEW_DESC       m_desc = { };

    public:
        vaRenderTargetViewDX12( vaRenderDeviceDX12 & device ) : vaResourceViewDX12( device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV ) { }

        vaRenderTargetViewDX12( ) = delete;
        vaRenderTargetViewDX12( const vaRenderTargetViewDX12& ) = delete;
        vaRenderTargetViewDX12& operator =( const vaRenderTargetViewDX12& ) = delete;

    public:
        const D3D12_RENDER_TARGET_VIEW_DESC & GetDesc( ) const                            { return m_desc; }
        void                                Create( ID3D12Resource *resource, const D3D12_RENDER_TARGET_VIEW_DESC & desc );
        void                                CreateNull( );
    };

    class vaDepthStencilViewDX12 : public vaResourceViewDX12
    {
        // this is left in for convenience & debugging purposes
        D3D12_DEPTH_STENCIL_VIEW_DESC       m_desc = { };

    public:
        vaDepthStencilViewDX12( vaRenderDeviceDX12 & device ) : vaResourceViewDX12( device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV ) { }

    public:
        const D3D12_DEPTH_STENCIL_VIEW_DESC & GetDesc( ) const                            { return m_desc; }
        void                                Create( ID3D12Resource *resource, const D3D12_DEPTH_STENCIL_VIEW_DESC & desc );
        void                                CreateNull( );
    };

    class vaSamplerViewDX12 : public vaResourceViewDX12
    {
        // this is left in for convenience & debugging purposes
        D3D12_SAMPLER_DESC                  m_desc = { };

    public:
        vaSamplerViewDX12( vaRenderDeviceDX12 & device ) : vaResourceViewDX12( device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ) { }

    public:
        const D3D12_SAMPLER_DESC &          GetDesc( ) const                            { return m_desc; }
        void                                Create( const D3D12_SAMPLER_DESC & desc );
        void                                CreateNull( );
    };

    // generic bindable resource base class
    class vaShaderResourceDX12 : public virtual vaShaderResource
    {
    public:
        virtual ~vaShaderResourceDX12( ) {};

        //virtual const vaConstantBufferViewDX12 *    GetCBV( )   const                                                                               { assert( false ); return nullptr; } <- no longer used, just use GetGPUVirtualAddress
        virtual const vaUnorderedAccessViewDX12 *   GetUAV( )   const                                                                               { assert( false ); return nullptr; }
        virtual const vaShaderResourceViewDX12 *    GetSRV( )   const                                                                               { assert( false ); return nullptr; }

        virtual void                                TransitionResource( vaRenderDeviceContextBaseDX12 & context, D3D12_RESOURCE_STATES target )     { context; target; assert( false ); }
        virtual void                                AdoptResourceState( vaRenderDeviceContextBaseDX12 & context, D3D12_RESOURCE_STATES target )     { context; target; assert( false ); }   // if something external does a transition we can update our internal tracking

        // used by constant buffers
        virtual D3D12_GPU_VIRTUAL_ADDRESS           GetGPUVirtualAddress( ) const                                                                   { assert( false ); return {0}; }

        virtual UINT64                              GetSizeInBytes( ) const                                                                         { assert( false ); return 0; }
        virtual DXGI_FORMAT                         GetFormat( ) const                                                                              { assert( false ); return DXGI_FORMAT_UNKNOWN; }
        virtual UINT                                GetStrideInBytes( ) const                                                                       { assert( false ); return 0; }

        virtual ID3D12Resource *                    GetResource( ) const                                                                            { assert( false ); return nullptr; }
    };

    //struct MemoryBufferComparer
    //{
    //    bool operator()( const vaMemoryBuffer& left, const vaMemoryBuffer& right ) const
    //    {
    //        assert( left.GetSize( ) == right.GetSize( ) );
    //        // comparison logic goes here
    //        return memcmp( left.GetData( ), right.GetData( ), std::min( left.GetSize( ), right.GetSize( ) ) ) < 0;
    //    }
    //};

    struct vaPSOKeyDataHasher
    {
        std::size_t operator () ( const vaMemoryBuffer & key ) const
        {
            // see vaGraphicsPSODescDX12::FillKey and vaComputePSODescDX12::FillKey - hash is in the first 64 bits of the buffer :)

            return *reinterpret_cast<uint64*>(key.GetData());
        }
    };

    template< int _KeyStorageSize >
    class vaBasePSODX12
    {
    public:
        static constexpr int                c_keyStorageSize = _KeyStorageSize;
        uint8                               m_keyStorage[c_keyStorageSize];

    private:

        lc_atomic_counter<int64, 17>        m_lastUsedFrame = -1;

        std::mutex                          m_mutex;                    // user will get unique lock of this before adding object to container, and unlock after the PSO was created (lengthy op)

    public:
        ~vaBasePSODX12( )                   { }

        // this is used to allow cache to be cleared after "a while" (see vaRenderDeviceDX12 for details)
        int64                               GetLastUsedFrame( ) const noexcept          { return m_lastUsedFrame.highest(); }
        void                                SetLastUsedFrame( int64 lastUsedFrame )     { m_lastUsedFrame.store( lastUsedFrame ); }

        //auto  &                             Mutex( ) noexcept                           { return m_mutex; }

        uint8 *                             KeyStorage( ) noexcept                      { return m_keyStorage; }
    };

    // Used to request cached D3D12_GRAPHICS_PIPELINE_STATE_DESC from the vaRenderDeviceContextDX12 (baseline implementation is in vaRenderDeviceDX12)
    struct vaGraphicsPSODescDX12
    {
        vaFramePtr<vaShaderDataDX12>        VSBlob                  = nullptr;
        int64                               VSUniqueContentsID      = -1;
        vaFramePtr<vaInputLayoutDataDX12>   VSInputLayout           = nullptr;
        //
        vaFramePtr<vaShaderDataDX12>        PSBlob                  = nullptr;
        int64                               PSUniqueContentsID      = -1;
        //
        vaFramePtr<vaShaderDataDX12>        DSBlob                  = nullptr;
        int64                               DSUniqueContentsID      = -1;
        //
        vaFramePtr<vaShaderDataDX12>        HSBlob                  = nullptr;
        int64                               HSUniqueContentsID      = -1;
        //
        vaFramePtr<vaShaderDataDX12>        GSBlob                  = nullptr;;
        int64                               GSUniqueContentsID      = -1;

        // stream output not yet implemented and probably will never be
        // D3D12_STREAM_OUTPUT_DESC StreamOutput;

        // blend states oversimplified with just vaBlendMode - to be upgraded when needed (full info is in D3D12_BLEND_DESC BlendState)
        vaBlendMode                         BlendMode               = vaBlendMode::Opaque;
            
        // blend sample mask not yet implemented
        // UINT SampleMask;

        // simplified rasterizer desc (expand when needed)
        // D3D12_RASTERIZER_DESC
        vaFillMode                          FillMode                = vaFillMode::Solid;
        vaFaceCull                          CullMode                = vaFaceCull::Back;
        bool                                FrontCounterClockwise   = false;
        bool                                MultisampleEnable       = true;  // only enabled if currently set render target
        //bool                                ScissorEnable           = true;

        // simplified depth-stencil (expand when needed)
        bool                                DepthEnable             = false;
        bool                                DepthWriteEnable        = false;
        vaComparisonFunc                    DepthFunc               = vaComparisonFunc::Always;

        // InputLayout picked up and versioned from vertex shader
        // D3D12_INPUT_LAYOUT_DESC InputLayout;

        // not implemented
        // D3D12_INDEX_BUFFER_STRIP_CUT_VALUE IBStripCutValue;

        // topology
        vaPrimitiveTopology                 Topology                = vaPrimitiveTopology::TriangleList;

        // 
        uint32                              NumRenderTargets        = 0;
        vaResourceFormat                    RTVFormats[8]           = { vaResourceFormat::Unknown, vaResourceFormat::Unknown, vaResourceFormat::Unknown, vaResourceFormat::Unknown, vaResourceFormat::Unknown, vaResourceFormat::Unknown, vaResourceFormat::Unknown, vaResourceFormat::Unknown };
        vaResourceFormat                    DSVFormat               = vaResourceFormat::Unknown;
        uint32                              SampleDescCount         = 0;

                                            vaGraphicsPSODescDX12( )    { PartialReset(); }
                                            ~vaGraphicsPSODescDX12( )   { }

        bool                                operator == ( const vaGraphicsPSODescDX12 & other ) const = delete;

        // this is done when reusing vaGraphicsPSODescDX12 between draw calls - must reset some caches
        void                                PartialReset( );
        void                                InvalidateCache( );

        // after an actual PSO was created from this, we can clean the input pointers
        void                                CleanPointers( );

        void                                FillGraphicsPipelineStateDesc( D3D12_GRAPHICS_PIPELINE_STATE_DESC & outDesc, ID3D12RootSignature * pRootSignature ) const;

        //void                                FillKey( vaMemoryStream & outStream ) const;
        uint32                              FillKeyFast( uint8 * buffer ) const;
    };

    // Used for caching
    class vaGraphicsPSODX12 : public vaBasePSODX12<128>
    {
        friend class vaRenderDeviceDX12;
        vaGraphicsPSODescDX12               m_desc;

        alignas( VA_ALIGN_PAD )  char       m_padding0[VA_ALIGN_PAD];
        alignas( VA_ALIGN_PAD )  std::atomic<ID3D12PipelineState*>   
                                            m_pso       = nullptr;
        alignas( VA_ALIGN_PAD )  char       m_padding1[VA_ALIGN_PAD];

    public:
        vaGraphicsPSODX12( const vaGraphicsPSODescDX12 & desc ) : m_desc(desc) { }
        ~vaGraphicsPSODX12()  { auto prev = m_pso.exchange(nullptr); if( prev != nullptr ) prev->Release(); }

        vaGraphicsPSODX12( const vaGraphicsPSODX12 & ) = delete;
        vaGraphicsPSODX12& operator =( const vaGraphicsPSODX12& ) = delete;

        const vaGraphicsPSODescDX12 &       GetDesc() const                             { return m_desc; }
        ID3D12PipelineState *               GetPSO() const                              { return m_pso.load(std::memory_order_relaxed); }

        void                                CreatePSO( vaRenderDeviceDX12 & device, ID3D12RootSignature * rootSignature );
    };

    // Used to request cached D3D12_COMPUTE_PIPELINE_STATE_DESC from the vaRenderDeviceContextDX12 (baseline implementation is in vaRenderDeviceDX12)
    struct vaComputePSODescDX12
    {
        // root signature inherited from device context 
        // ID3D12RootSignature *pRootSignature;

        // 'extracted' shader data here - will keep the blobs and layouts alive as long as they're needed even if the shader gets deleted or recompiled 
        // (probably not needed as this shouldn't happen during rendering, but just to be on the safe side)
        // (shaders will have an unique identifier - this can persist between app restarts since they're cached already)
        vaFramePtr<vaShaderDataDX12>        CSBlob              = nullptr;
        int64                               CSUniqueContentsID  = -1;

        // not implemented
        // UINT NodeMask;

        // should probably be enabled for WARP once I get WARP working
        // D3D12_PIPELINE_STATE_FLAGS Flags;

        // after an actual PSO was created from this, we can clean the input pointers
        void                                CleanPointers( )                                { CSBlob = nullptr; }

        void                                FillComputePipelineStateDesc( D3D12_COMPUTE_PIPELINE_STATE_DESC & outDesc, ID3D12RootSignature * pRootSignature ) const;

        //void                                FillKey( vaMemoryStream & outStream ) const;
        uint32                              FillKeyFast( uint8 * buffer ) const;
    };

    // Used for caching
    class vaComputePSODX12 : public vaBasePSODX12<64>
    {
        friend class vaRenderDeviceDX12;
        vaComputePSODescDX12                m_desc;

        alignas( VA_ALIGN_PAD )  char       m_padding0[VA_ALIGN_PAD];
        alignas( VA_ALIGN_PAD )  std::atomic<ID3D12PipelineState*>   
                                            m_pso       = nullptr;
        alignas( VA_ALIGN_PAD )  char       m_padding1[VA_ALIGN_PAD];

    public:
        vaComputePSODX12( const vaComputePSODescDX12 & desc ) : m_desc( desc ) {  }
        ~vaComputePSODX12()  { auto prev = m_pso.exchange(nullptr); if( prev != nullptr ) prev->Release(); }

        vaComputePSODX12( const vaComputePSODX12 & ) = delete;
        vaComputePSODX12& operator =( const vaComputePSODX12& ) = delete;

        const vaComputePSODescDX12 &        GetDesc() const                             { return m_desc; }
        ID3D12PipelineState *               GetPSO() const                              { return m_pso.load(std::memory_order_relaxed); }

        void                                CreatePSO( vaRenderDeviceDX12 & device, ID3D12RootSignature * rootSignature );
    };

    // Used to request cached D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE object (and related data) from the vaRenderDeviceContextDX12 (baseline implementation is in vaRenderDeviceDX12)
    struct vaRaytracePSODescDX12
    {
        static constexpr int                c_maxNameBufferSize         = 48;

        // per-vaRaytraceItem shader library (vaRaytraceItem::ShaderLibrary)
        vaFramePtr<vaShaderDataDX12>        ItemSLBlob                  = nullptr;
        int64                               ItemSLUniqueContentsID      = -1;
        wstring                             ItemSLEntryRayGen           = L"";       // max length 63 chars <- TODO: convert these to char[64] to avoid dynamic allocations
        wstring                             ItemSLEntryMiss             = L"";       // max length 63 chars <- TODO: convert these to char[64] to avoid dynamic allocations
        wstring                             ItemSLEntryMissSecondary    = L"";       // max length 63 chars <- TODO: convert these to char[64] to avoid dynamic allocations
        wstring                             ItemSLEntryAnyHit           = L"";       // max length 63 chars <- TODO: convert these to char[64] to avoid dynamic allocations
        wstring                             ItemSLEntryClosestHit       = L"";       // max length 63 chars <- TODO: convert these to char[64] to avoid dynamic allocations

        wstring                             ItemMaterialAnyHit          = L"";
        wstring                             ItemMaterialClosestHit      = L"";
        wstring                             ItemMaterialCallable        = L"";
        wstring                             ItemMaterialMissCallable    = L"";
        //
        // per-vaSceneRaytracing, per-material shader library (libraries) identifier - they contain all material-related raytracing stuff
        int64                               MaterialsSLUniqueContentsID = -1;
        //
        uint32                              MaxRecursionDepth           = 0;
        uint32                              MaxPayloadSize              = 0;
        //

        vaRaytracePSODescDX12( )            { }
        ~vaRaytracePSODescDX12( )           { }

        bool                                operator == ( const vaRaytracePSODescDX12 & other ) const = delete;

        // after an actual PSO was created from this, we can clean the input pointers
        void                                CleanPointers( );

        bool                                FillPipelineStateDesc( CD3DX12_STATE_OBJECT_DESC & outDesc, ID3D12RootSignature * pRootSignature, const class vaRenderMaterialManagerDX12 & materialManager12 ) const;

        //void                                FillKey( vaMemoryStream & outStream ) const;
        uint32                              FillKeyFast( uint8 * buffer ) const;
    };

    // Used for caching - in case of raytracing PSO it also contains related shader tables
    class vaRaytracePSODX12 : public vaBasePSODX12<1024>
    {
    public:
        struct Inner
        {
            // DX state object
            ComPtr<ID3D12StateObject>       PSO;

            // Shader tables (GPU buffers)
            shared_ptr<vaRenderBuffer>      MissShaderTable;
            UINT64                          MissShaderTableStride; 
            shared_ptr<vaRenderBuffer>      HitGroupShaderTable;
            UINT64                          HitGroupShaderTableStride; 
            shared_ptr<vaRenderBuffer>      RayGenShaderTable;
            shared_ptr<vaRenderBuffer>      CallableShaderTable;
            UINT64                          CallableShaderTableStride; 

            // this indicates that some shaders couldn't compile due to an error, or are still compiling
            bool                            Incomplete;
        };

    private:
        friend class vaRenderDeviceDX12;
        vaRaytracePSODescDX12               m_desc;

        // cache optimizations irrelevant for raytrace calls - they're never executed per-instance; leaving in, in a strange case it turns out to be needed
        // alignas( VA_ALIGN_PAD )  char       m_padding0[VA_ALIGN_PAD];
        // thread-safety also likely unnecessary but leaving it in for any crazy future contingencies since it's already there
        std::atomic<Inner*>                 m_pso       = nullptr;

    public:
        vaRaytracePSODX12( const vaRaytracePSODescDX12 & desc ) : m_desc(desc) { }
        ~vaRaytracePSODX12()  { auto prev = m_pso.exchange(nullptr); if( prev != nullptr ) delete prev; }

        vaRaytracePSODX12( const vaGraphicsPSODX12 & ) = delete;
        vaRaytracePSODX12& operator =( const vaGraphicsPSODX12& ) = delete;

        const vaRaytracePSODescDX12 &       GetDesc() const                             { return m_desc; }
        Inner *                             GetPSO() const                              { return m_pso.load(std::memory_order_relaxed); }

        void                                CreatePSO( vaRenderDeviceDX12 & device, ID3D12RootSignature * rootSignature );
    };
        
    inline vaShaderResourceDX12 &  AsDX12( vaShaderResource & resource )   { return *resource.SafeCast<vaShaderResourceDX12*>(); }
    inline vaShaderResourceDX12 *  AsDX12( vaShaderResource * resource )   { return resource->SafeCast<vaShaderResourceDX12*>(); }
}
