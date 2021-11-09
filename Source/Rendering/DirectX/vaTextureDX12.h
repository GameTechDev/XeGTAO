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

#include "Core/vaCoreIncludes.h"

#include "Rendering/DirectX/vaDirectXIncludes.h"
#include "Rendering/DirectX/vaDirectXTools.h"

#include "Rendering/vaRenderingIncludes.h"

namespace Vanilla
{

    class vaTextureDX12 sealed : public vaTexture, public vaShaderResourceDX12
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );

    private:
        ComPtr<ID3D12Resource>              m_resource;
        vaResourceStateTransitionHelperDX12 m_rsth;

        wstring                             m_wname;
        HANDLE                              m_sharedApiHandle   = 0;        

        vaShaderResourceViewDX12            m_srv;
        vaRenderTargetViewDX12              m_rtv;
        vaDepthStencilViewDX12              m_dsv;
        vaUnorderedAccessViewDX12           m_uav;

        vaResourceMapType                   m_currentMapType    = vaResourceMapType::None;

        // if m_viewedOriginal is not null and we are looking into just some of the subresources then this contains list of them
        std::vector<uint32>                 m_viewSubresourceList;

    protected:
        friend class vaTexture;
        explicit                            vaTextureDX12( const vaRenderingModuleParams & params );
        virtual                             ~vaTextureDX12( )   ;
        void                                SetViewedOriginal( const shared_ptr< vaTexture > & viewedOriginal ) { vaTexture::SetViewedOriginal( viewedOriginal ); }   // can only be done once at initialization

        virtual bool                        Import( const wstring & storageFilePath, vaTextureLoadFlags loadFlags, vaResourceBindSupportFlags binds, vaTextureContentsType contentsType = vaTextureContentsType::GenericColor ) override;
        virtual bool                        Import( void * buffer, uint64 bufferSize, vaTextureLoadFlags loadFlags = vaTextureLoadFlags::Default, vaResourceBindSupportFlags binds = vaResourceBindSupportFlags::ShaderResource, vaTextureContentsType contentsType = vaTextureContentsType::GenericColor ) override;
        virtual void                        Destroy( ) override;

        virtual shared_ptr<vaTexture>       CreateViewInternal( const shared_ptr<vaTexture> & thisTexture, vaResourceBindSupportFlags bindFlags, vaResourceFormat srvFormat, vaResourceFormat rtvFormat, vaResourceFormat dsvFormat, vaResourceFormat uavFormat, vaTextureFlags flags, int viewedMipSliceMin, int viewedMipSliceCount, int viewedArraySliceMin, int viewedArraySliceCount );

        virtual bool                        InternalCreate1D( vaResourceFormat format, int width, int mipLevels, int arraySize, vaResourceBindSupportFlags bindFlags, vaResourceAccessFlags accessFlags, vaResourceFormat srvFormat, vaResourceFormat rtvFormat, vaResourceFormat dsvFormat, vaResourceFormat uavFormat, vaTextureFlags flags, vaTextureContentsType contentsType, const void * initialData ) override;
        virtual bool                        InternalCreate2D( vaResourceFormat format, int width, int height, int mipLevels, int arraySize, int sampleCount, vaResourceBindSupportFlags bindFlags, vaResourceAccessFlags accessFlags, vaResourceFormat srvFormat, vaResourceFormat rtvFormat, vaResourceFormat dsvFormat, vaResourceFormat uavFormat, vaTextureFlags flags, vaTextureContentsType contentsType, const void * initialData, int initialDataRowPitch ) override;
        virtual bool                        InternalCreate3D( vaResourceFormat format, int width, int height, int depth, int mipLevels, vaResourceBindSupportFlags bindFlags, vaResourceAccessFlags accessFlags, vaResourceFormat srvFormat, vaResourceFormat rtvFormat, vaResourceFormat dsvFormat, vaResourceFormat uavFormat, vaTextureFlags flags, vaTextureContentsType contentsType, const void * initialData, int initialDataRowPitch, int initialDataSlicePitch ) override;


    public:
        ID3D12Resource *                    GetResource( ) const        { return m_resource.Get(); }

        // vaShaderResourceDX12 impl
        virtual const vaConstantBufferViewDX12 *    GetCBV( ) const override;
        virtual const vaShaderResourceViewDX12 *    GetSRV( ) const override;
        virtual const vaUnorderedAccessViewDX12 *   GetUAV( ) const override;

        const vaRenderTargetViewDX12 *              GetRTV( ) const         ;
        const vaDepthStencilViewDX12 *              GetDSV( ) const         ;

        //D3D12_RESOURCE_STATES                       RSTHGetCurrentState( )                                                              { return GetRSTH()->RSTHGetCurrentState(); }
        virtual void                                TransitionResource( vaRenderDeviceContextBaseDX12 & context, D3D12_RESOURCE_STATES target ) override;
        virtual void                                AdoptResourceState( vaRenderDeviceContextBaseDX12 & context, D3D12_RESOURCE_STATES target ) override;   // if something external does a transition we can update our internal tracking
        //void                                        RSTHEarlyGuessTransition( vaRenderDeviceContextDX12 & context )                     { GetRSTH()->RSTHEarlyGuessTransition( context ); }


    public:
        // Given an existing D3D11 resource, make a vaTexture around it!
        static shared_ptr<vaTexture>        CreateWrap( vaRenderDevice & renderDevice, ID3D12Resource * resource, vaResourceFormat srvFormat = vaResourceFormat::Automatic, vaResourceFormat rtvFormat = vaResourceFormat::Automatic, vaResourceFormat dsvFormat = vaResourceFormat::Automatic, vaResourceFormat uavFormat = vaResourceFormat::Automatic, vaTextureContentsType contentsType = vaTextureContentsType::GenericColor );

        virtual uint32                      GetSRVBindlessIndex( vaRenderDeviceContext * renderContextPtr ) override;

    private:
        void                                SetResource( ID3D12Resource * resource, D3D12_RESOURCE_STATES initialState );
        void                                ProcessResource( bool notAllBindViewsNeeded, bool dontResetFlags );

        void                                InternalUpdateFromRenderingCounterpart( bool notAllBindViewsNeeded, bool dontResetFlags, bool isCubemap );
        void                                InternalUpdateSubresources( uint32 firstSubresource, /*const*/ std::vector<vaTextureSubresourceData> & subresources );
        bool                                InternalTryMap( vaResourceMapType mapType, bool doNotWait );
        void                                InternalUnmap( );

    protected:
        virtual void                        SetName( const string & name ) override;

        // will update texture by creating temporary upload resource, filling it with initialData and calling vaTexture::Copy on GetRenderDevice( ).GetMainContext( ) (if context == nullptr then scheduled to next frame beginning)
        // (you're free to dispose of the memory pointed to by the D3D12_SUBRESOURCE_DATA after the call)
        virtual void                        UpdateSubresources( vaRenderDeviceContext & renderContext, uint32 firstSubresource, /*const*/ std::vector<vaTextureSubresourceData> & subresources ) override;
        virtual bool                        TryMap( vaRenderDeviceContext & renderContext, vaResourceMapType mapType, bool doNotWait ) override;
        virtual void                        Unmap( vaRenderDeviceContext & renderContext ) override;

        virtual void                        ClearRTV( vaRenderDeviceContext & renderContext, const vaVector4 & clearValue );
        virtual void                        ClearUAV( vaRenderDeviceContext & renderContext, const vaVector4ui & clearValue );
        virtual void                        ClearUAV( vaRenderDeviceContext & renderContext, const vaVector4 & clearValue );
        virtual void                        ClearDSV( vaRenderDeviceContext & renderContext, bool clearDepth, float depthValue, bool clearStencil, uint8 stencilValue );
        virtual void                        CopyFrom( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & srcTexture ) override;
        virtual void                        CopyTo( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & dstTexture ) override;

        //virtual void                        UpdateSubresource( vaRenderDeviceContext & renderContext, int dstSubresourceIndex, const vaBoxi & dstBox, void * srcData, int srcDataRowPitch, int srcDataDepthPitch = 0 );
        virtual void                        ResolveSubresource( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & dstResource, uint dstSubresource, uint srcSubresource, vaResourceFormat format ) override;

        virtual bool                        LoadAPACK( vaStream & inStream ) override;
        virtual bool                        SaveAPACK( vaStream & outStream ) override;
        virtual bool                        SerializeUnpacked( vaXMLSerializer & serializer, const string & assetFolder ) override;

        virtual shared_ptr<vaTexture>       TryCompress( ) override;

        virtual bool                        SaveToDDSFile( vaRenderDeviceContext & renderContext, const wstring & path ) override;
        virtual bool                        SaveToPNGFile( vaRenderDeviceContext & renderContext, const wstring & path ) override;
        // virtual bool                        SaveToPNGBuffer( vaRenderDeviceContext & renderContext, const wstring & path ) override; // not yet implemented but should be trivial

        virtual vaResourceBindSupportFlags  GetBindSupportFlags( ) const override                           { return m_bindSupportFlags; }

        //virtual bool                        GetCUDAShared( void * & outPointer, size_t & outSize )  override;

        static void                         Copy( vaRenderDeviceContextDX12 & apiContext, vaTextureDX12 & dstTexture, vaTextureDX12 & srcTexture );

        static D3D12_CLEAR_VALUE *          GetNextCreateFastClearStatus( D3D12_CLEAR_VALUE & clearVal, vaResourceBindSupportFlags bindFlags );

    protected:
        // Since upload/readback heaps in DX12 are internally created as a D3D12_RESOURCE_DIMENSION_BUFFER, we lose bunch of metadata used
        // creation and mapping. We use this structure to provide this during creation and later.
        // This is far from optimal - could be allocated as one big single chunk of memory like 'UpdateSubresources' from d3dx12.h does but
        // at this point it's used so rarely that there's no point spending too much time on it.
        struct MappableTextureInfo
        {
            int const                                   NumSubresources;
            std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT>  Layouts;
            std::vector<uint32>                              NumRows;
            std::vector<uint64>                              RowSizesInBytes;
            uint64                                      TotalSizeInBytes;

            D3D12_RESOURCE_DESC const                   CopyableResDesc;

            ComPtr<ID3D12Fence>                         GPUFence;
            uint64                                      GPULastFenceValue = 0;
            HANDLE                                      GPUFenceEvent = INVALID_HANDLE_VALUE;

            MappableTextureInfo( vaRenderDeviceDX12 & device, const D3D12_RESOURCE_DESC & resDesc );
            ~MappableTextureInfo( );

            void                                        SignalNextFence( vaRenderDeviceDX12 & device );
            bool                                        TryWaitLastFence( bool doNotWait = false );
        };
        shared_ptr<MappableTextureInfo>                 m_mappableTextureInfo;

    };

    inline vaTextureDX12 & AsDX12( vaTexture & texture )   { return *texture.SafeCast<vaTextureDX12*>(); }
    inline vaTextureDX12 * AsDX12( vaTexture * texture )   { return texture->SafeCast<vaTextureDX12*>(); }

    inline const vaConstantBufferViewDX12 *             vaTextureDX12::GetCBV( ) const              { return nullptr; }
    inline const vaShaderResourceViewDX12 *             vaTextureDX12::GetSRV( ) const              { return (m_overrideView==nullptr)?(m_srv.IsCreated()?&m_srv:nullptr):(AsDX12(*m_overrideView).GetSRV()); }
    inline const vaUnorderedAccessViewDX12 *            vaTextureDX12::GetUAV( ) const              { assert(m_overrideView==nullptr); return m_uav.IsCreated()?(&m_uav):(nullptr); }
    
    inline const vaRenderTargetViewDX12 *               vaTextureDX12::GetRTV( ) const              { assert(m_overrideView==nullptr); return m_rtv.IsCreated()?(&m_rtv):(nullptr); }
    inline const vaDepthStencilViewDX12 *               vaTextureDX12::GetDSV( ) const              { assert(m_overrideView==nullptr); return m_dsv.IsCreated()?(&m_dsv):(nullptr); }

    inline void vaTextureDX12::AdoptResourceState( vaRenderDeviceContextBaseDX12 & context, D3D12_RESOURCE_STATES target ) 
    { 
        if( m_overrideView != nullptr )
        {
            AsDX12(*m_overrideView).AdoptResourceState( context, target );
        }
        else
        if( m_viewedOriginal != nullptr )
        {
            if( m_viewSubresourceList.size() == 0 )
                AsDX12(*m_viewedOriginal).AdoptResourceState( context, target );
            else
            {
                for( auto subRes : m_viewSubresourceList )
                    AsDX12(*m_viewedOriginal).m_rsth.RSTHAdoptResourceState( context, target, subRes );
            }
        }
        else
            m_rsth.RSTHAdoptResourceState( context, target ); 
    }


}
