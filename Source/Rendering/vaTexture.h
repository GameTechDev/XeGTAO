///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// vaTexture can be a static texture used as an asset, loaded from storage or created procedurally, or a dynamic 
// GPU-only texture for use as a render target or similar. 
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Core/vaCoreIncludes.h"
#include "Core/Misc/vaResourceFormats.h"

#include "vaRendering.h"

namespace Vanilla
{
   
    enum class vaResourceAccessFlags : uint32
    {
        Default                     = 0,                // GPU read/write is default (no CPU read/write, this allows UAVs)
        CPURead                     = ( 1 << 0 ),       // 
        CPUWrite                    = ( 1 << 1 ),       // 

        // This enables an optimization (if supported by the API) for vaResourceAccessFlags::CPURead textures that avoids syncing between GPU->CPU copies and waiting on 
        // fence in TryMap; if enabled, it assumes the user is manually double-buffering (based on vaRenderDevice::c_BackbufferCount) and ensuring the readback memory is
        // not read while the GPU is still writing to it. There is no validation in manual mode so be careful. It can only be used in combination with CPURead.
        CPUReadManuallySynced       = ( 1 << 2 )
    };

    enum class vaTextureType : int32
    {
        Unknown	                    = 0,
        Buffer	                    = 1,                // this isn't actually supported in practice for now: various buffers handled in vaRenderBuffers*
        Texture1D	                = 2,
        Texture2D	                = 4,
        Texture3D	                = 8
    };

    enum class vaTextureLoadFlags : uint32
    {
        Default                     = 0,
        PresumeDataIsSRGB           = (1 << 0),
        PresumeDataIsLinear         = (1 << 1),
        // AutogenerateMIPsIfMissing   = (1 << 2),     // not supported in dx12 - should we implement? or just convert to 'reserve mip space' like dx
    };

    enum class vaTextureFlags : uint32
    {
        None                        = 0,
        Cubemap                     = ( 1 << 0 ),

        CubemapButArraySRV          = ( 1 << 16 ),  // this is only if you wish the SRV to be of Array (and not Cube or Cube Array) type
        // ReadOnlyDSV              = ( 1 << 17 ),  // not currently implemented but will correspond to (D3D12_DSV_FLAG_READ_ONLY_DEPTH | D3D12_DSV_FLAG_READ_ONLY_STENCIL)
    };

    enum class vaTextureContentsType
    {
        GenericColor                = 0,
        GenericLinear               = 1,
        NormalsXYZ_UNORM            = 2,    // unpacked by doing normalOut = normalize( normalIn.xyz * 2.0 - 1.0 );
        NormalsXY_UNORM             = 3,    // unpacked by doing normalOut.xy = (normalIn.xy * 2.0 - 1.0); normalOut.z = sqrt( 1 - normalIn.x*normalIn.x - normalIn.y*normalIn.y );
        NormalsWY_UNORM             = 4,    // unpacked by doing normalOut.xy = (normalIn.wy * 2.0 - 1.0); normalOut.z = sqrt( 1 - normalIn.x*normalIn.x - normalIn.y*normalIn.y ); (this is for DXT5_NM - see UnpackNormalDXT5_NM)
        SingleChannelLinearMask     = 5,
        DepthBuffer                 = 6,
        LinearDepth                 = 7,
        NormalsXY_LAEA_ENCODED      = 8,    // Lambert Azimuthal Equal-Area projection, see vaShared.hlsl for encode/decode

        MaxValue
    };

    BITFLAG_ENUM_CLASS_HELPER( vaResourceAccessFlags );
    BITFLAG_ENUM_CLASS_HELPER( vaTextureLoadFlags );
    BITFLAG_ENUM_CLASS_HELPER( vaTextureFlags );


    struct vaTextureConstructorParams : vaRenderingModuleParams
    {
        const vaGUID &          UID;
        explicit vaTextureConstructorParams( vaRenderDevice & device, const vaGUID & uid = vaCore::GUIDCreate( ) ) : vaRenderingModuleParams(device), UID( uid ) { }
    };

    // for uploading texture/resource data - strangely similar to D3D12_SUBRESOURCE_DATA :P - don't change it, it gets reinterpret_cast<D3D12_SUBRESOURCE_DATA*>!
    struct vaTextureSubresourceData
    {
        const void *pData;
        int64       RowPitch;
        int64       SlicePitch;
    };

    // for accessing mapped texture data
    struct vaTextureMappedSubresource
    {
        byte *                              Buffer          = nullptr;
        int                                 BytesPerPixel   = 0;
        int64                               SizeInBytes     = 0;
        int                                 RowPitch        = 0;            // in bytes
        int                                 DepthPitch      = 0;            // in bytes
        int                                 SizeX           = 0;            // width
        int                                 SizeY           = 0;            // height
        int                                 SizeZ           = 0;            // depth

    public:
        vaTextureMappedSubresource( )             { Buffer = nullptr; Reset(); }
        ~vaTextureMappedSubresource( )            { delete[] Buffer; }
    protected:
        void Reset( )                       { if( Buffer != nullptr ) delete[] Buffer; Buffer = nullptr; BytesPerPixel = 0; SizeInBytes = 0; RowPitch = 0; DepthPitch = 0; SizeZ = 0; }

    public:
        //template<typename T>
        //T &                                 PixelAt( int x )        { assert( RowPitch == Size; ); assert( sizeof(T) == BytesPerPixel ); return *(T*)&Buffer[x * BytesPerPixel]; }

        template<typename T>
        inline T &                          PixelAt( int x, int y )         { assert( DepthPitch == 0 ); assert( sizeof(T) == BytesPerPixel ); assert( (x >= 0) && (x < SizeX) ); assert( (y >= 0) && (y < SizeY) ); return *(T*)&Buffer[x * BytesPerPixel + y * RowPitch]; }

        template<typename T>
        inline const T &                    PixelAt( int x, int y ) const   { assert( DepthPitch == Size ); assert( sizeof(T) == BytesPerPixel ); assert( (x >= 0) && (x < SizeX) ); assert( (y >= 0) && (y < SizeY) ); return *(T*)&Buffer[x * BytesPerPixel + y * RowPitch]; }
    };

    class vaTexture : public vaAssetResource, public vaRenderingModule, public virtual vaShaderResource
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );

    protected:
        vaTextureFlags                      m_flags;

        vaResourceAccessFlags               m_accessFlags;
        vaTextureType                       m_type;
        vaResourceBindSupportFlags          m_bindSupportFlags;
        vaTextureContentsType               m_contentsType;

        vaResourceFormat                    m_resourceFormat;
        vaResourceFormat                    m_srvFormat;
        vaResourceFormat                    m_rtvFormat;
        vaResourceFormat                    m_dsvFormat;
        vaResourceFormat                    m_uavFormat;

        int                                 m_sizeX;            // serves as desc.ByteWidth for Buffer
        int                                 m_sizeY;
        int                                 m_sizeZ;
        int                                 m_mipLevels;
        int                                 m_arrayCount;               // a.k.a. ArraySize
        int                                 m_sampleCount;

        int                                 m_viewedMipSlice;           // if m_viewedOriginal is nullptr, this will always be 0
        int                                 m_viewedMipSliceCount;      // if m_viewedOriginal is nullptr, this will always be 0
        int                                 m_viewedArraySlice;         // if m_viewedOriginal is nullptr, this will always be 0
        int                                 m_viewedArraySliceCount;    // if m_viewedOriginal is nullptr, this will always be 0
        shared_ptr< vaTexture >             m_viewedOriginal;           // do we want a weak_ptr? probably not

        std::vector< vaTextureMappedSubresource >      m_mappedData;
        bool                                m_isMapped;

        // Used to temporarily override this texture with another only for rendering-from purposes; effectively only overrides SRV - all other getters/setters and RTV/UAV/DSV 
        // accesses remain unchanged for now (no particular reason for restricting other views - just never tested / thought out). Useful for debugging, UI and similar.
        shared_ptr< vaTexture >             m_overrideView;

        // so we can have weak_ptr-s for tracking the lifetime of this object even when we don't have access to the this shared_ptr
		// * useful for lambdas and stuff
		// * warning, the m_smartThis gets 'refreshed' in Destroy
        shared_ptr< vaTexture* >            m_smartThis                       = std::make_shared<vaTexture*>(this);

    protected:
        static const int                    c_fileVersion           = 3;

    protected:
        //friend class vaTextureDX11;         // ugly but has to be here for every API implementation - not sure how to easily handle this otherwise
                                            vaTexture( const vaRenderingModuleParams & params  );
        void                                Initialize( vaResourceBindSupportFlags binds, vaResourceAccessFlags accessFlags, vaResourceFormat resourceFormat, vaResourceFormat srvFormat, vaResourceFormat rtvFormat, vaResourceFormat dsvFormat, vaResourceFormat uavFormat, vaTextureFlags flags, int viewedMipSliceMin = 0, int viewedMipSliceCount = -1, int viewedArraySliceMin = 0, int viewedArraySliceCount = -1, vaTextureContentsType contentsType = vaTextureContentsType::GenericColor );
        void                                SetViewedOriginal( const shared_ptr< vaTexture > & viewedOriginal ) { assert(m_viewedOriginal==nullptr); m_viewedOriginal = viewedOriginal; }   // can only be done once at initialization
        void                                InitializePreLoadDefaults( );
    
    public:
        virtual                             ~vaTexture( )                                                   { assert( !m_isMapped ); }

    public:
        // loading from file / buffer
        static shared_ptr<vaTexture>        CreateFromImageFile( vaRenderDevice & device, const wstring & storagePath, vaTextureLoadFlags loadFlags = vaTextureLoadFlags::Default, vaResourceBindSupportFlags binds = vaResourceBindSupportFlags::ShaderResource, vaTextureContentsType contentsType = vaTextureContentsType::GenericColor );
        static shared_ptr<vaTexture>        CreateFromImageFile( vaRenderDevice & device, const string & storagePath, vaTextureLoadFlags loadFlags = vaTextureLoadFlags::Default, vaResourceBindSupportFlags binds = vaResourceBindSupportFlags::ShaderResource, vaTextureContentsType contentsType = vaTextureContentsType::GenericColor )       { return CreateFromImageFile( device, vaStringTools::SimpleWiden(storagePath), loadFlags, binds, contentsType ); }
        static shared_ptr<vaTexture>        CreateFromImageBuffer( vaRenderDevice & device, void * buffer, uint64 bufferSize, vaTextureLoadFlags loadFlags = vaTextureLoadFlags::Default, vaResourceBindSupportFlags binds = vaResourceBindSupportFlags::ShaderResource, vaTextureContentsType contentsType = vaTextureContentsType::GenericColor );
        
        // 1D textures (regular, array)
        static shared_ptr<vaTexture>        Create1D( vaRenderDevice & device, vaResourceFormat format, int width, int mipLevels, int arraySize, vaResourceBindSupportFlags bindFlags, vaResourceAccessFlags accessFlags = vaResourceAccessFlags::Default, vaResourceFormat srvFormat = vaResourceFormat::Automatic, vaResourceFormat rtvFormat = vaResourceFormat::Automatic, vaResourceFormat dsvFormat = vaResourceFormat::Automatic, vaResourceFormat uavFormat = vaResourceFormat::Automatic, vaTextureFlags flags = vaTextureFlags::None, vaTextureContentsType contentsType = vaTextureContentsType::GenericColor, const void * initialData = nullptr );
        // 2D textures (regular, array, ms, msarray, cubemap, cubemap array)
        static shared_ptr<vaTexture>        Create2D( vaRenderDevice & device, vaResourceFormat format, int width, int height, int mipLevels, int arraySize, int sampleCount, vaResourceBindSupportFlags bindFlags, vaResourceAccessFlags accessFlags = vaResourceAccessFlags::Default, vaResourceFormat srvFormat = vaResourceFormat::Automatic, vaResourceFormat rtvFormat = vaResourceFormat::Automatic, vaResourceFormat dsvFormat = vaResourceFormat::Automatic, vaResourceFormat uavFormat = vaResourceFormat::Automatic, vaTextureFlags flags = vaTextureFlags::None, vaTextureContentsType contentsType = vaTextureContentsType::GenericColor, const void * initialData = nullptr, int initialDataRowPitch = 0 );
        // 3D textures
        static shared_ptr<vaTexture>        Create3D( vaRenderDevice & device, vaResourceFormat format, int width, int height, int depth, int mipLevels, vaResourceBindSupportFlags bindFlags, vaResourceAccessFlags accessFlags = vaResourceAccessFlags::Default, vaResourceFormat srvFormat = vaResourceFormat::Automatic, vaResourceFormat rtvFormat = vaResourceFormat::Automatic, vaResourceFormat dsvFormat = vaResourceFormat::Automatic, vaResourceFormat uavFormat = vaResourceFormat::Automatic, vaTextureFlags flags = vaTextureFlags::None, vaTextureContentsType contentsType = vaTextureContentsType::GenericColor, const void * initialData = nullptr, int initialDataRowPitch = 0, int initialDataSlicePitch = 0 );

        static void                         CreateMirrorIfNeeded( vaTexture & original, shared_ptr<vaTexture> & mirror );
        
        static shared_ptr<vaTexture>        CreateView( const shared_ptr<vaTexture> & texture, vaResourceBindSupportFlags bindFlags, vaResourceFormat srvFormat = vaResourceFormat::Automatic, vaResourceFormat rtvFormat = vaResourceFormat::Automatic, vaResourceFormat dsvFormat = vaResourceFormat::Automatic, vaResourceFormat uavFormat = vaResourceFormat::Automatic, vaTextureFlags flags = vaTextureFlags::None, int viewedMipSliceMin = 0, int viewedMipSliceCount = -1, int viewedArraySliceMin = 0, int viewedArraySliceCount = -1 );
        static shared_ptr<vaTexture>        CreateView( const shared_ptr<vaTexture> & texture, vaTextureFlags flags, int viewedMipSliceMin = 0, int viewedMipSliceCount = -1, int viewedArraySliceMin = 0, int viewedArraySliceCount = -1 );

    public:
        vaTextureType                       GetType( ) const                                                { return m_type; }
        virtual vaResourceBindSupportFlags  GetBindSupportFlags( ) const override                           { return m_bindSupportFlags; }
        vaTextureFlags                      GetFlags( ) const                                               { return m_flags; }
        vaResourceAccessFlags               GetAccessFlags( ) const                                         { return m_accessFlags; }

        vaTextureContentsType               GetContentsType( ) const                                        { return m_contentsType; }
        void                                SetContentsType( vaTextureContentsType contentsType )           { m_contentsType = contentsType; }

        vaResourceFormat                    GetResourceFormat( ) const                                      { return m_resourceFormat; }
        vaResourceFormat                    GetSRVFormat( ) const                                           { return m_srvFormat;      }
        vaResourceFormat                    GetDSVFormat( ) const                                           { return m_dsvFormat;      }
        vaResourceFormat                    GetRTVFormat( ) const                                           { return m_rtvFormat;      }
        vaResourceFormat                    GetUAVFormat( ) const                                           { return m_uavFormat;      }

        int                                 GetSizeX( )             const                                   { return m_sizeX;       }     // serves as desc.ByteWidth for Buffer
        int                                 GetSizeY( )             const                                   { return m_sizeY;       }
        int                                 GetSizeZ( )             const                                   { return m_sizeZ;       }
        int                                 GetWidth( )             const                                   { return m_sizeX;       }     // serves as desc.ByteWidth for Buffer
        int                                 GetHeight( )            const                                   { return m_sizeY;       }
        int                                 GetDepth( )             const                                   { return m_sizeZ;       }
        vaVector3i                          GetSize( )              const                                   { return vaVector3i( m_sizeX, m_sizeY, m_sizeZ ); }
        int                                 GetMipLevels( )         const                                   { assert( !IsView() || m_mipLevels == m_viewedMipSliceCount );    return m_mipLevels;   }
        int                                 GetArrayCount( )        const                                   { assert( (GetType() != vaTextureType::Texture3D ) && (!IsView() || m_arrayCount == m_viewedArraySliceCount) ); return m_arrayCount;  } // if( GetType() == vaTextureType::Texture1D ) return GetSizeY(); if( GetType() == vaTextureType::Texture2D ) return GetSizeZ(); return 1; }

        int                                 GetSampleCount( )       const                                   { return m_sampleCount; }

        // if view, return viewed subset, otherwise return the above (viewing the whole resource)
        int                                 GetViewedMipSlice( )    const                                   { return (IsView())?(m_viewedMipSlice       ):(0); }
        int                                 GetViewedArraySlice( )  const                                   { return (IsView())?(m_viewedArraySlice     ):(0); }

        bool                                IsCubemap( ) const                                              { return (m_flags & vaTextureFlags::Cubemap) != 0; }

        bool                                IsView( ) const                                                 { return m_viewedOriginal != nullptr; }
        const shared_ptr< vaTexture > &     GetViewedOriginal( ) const                                      { return m_viewedOriginal; }

        bool                                IsMapped( ) const                                               { return m_isMapped; }
        std::vector<vaTextureMappedSubresource>& GetMappedData( )                                                { assert(m_isMapped); return m_mappedData; }

        // Temporarily override this texture with another only for rendering-from purposes; effectively only overrides SRV - all other getters/setters and RTV/UAV/DSV accesses remain unchanged
        // (useful for debugging, UI and similar)
        void                                SetOverrideView( const shared_ptr<vaTexture> & overrideView )   { m_overrideView = overrideView; }
        const shared_ptr<vaTexture> &       GetOverrideView( )                                              { return m_overrideView; }

        // sets the debug/UI name (for ex. via ID3D12Object::SetName)
        virtual void                        SetName( const string & name )                                  = 0;

        // these can only happen on the main thread and require main render device context to be created - limitations for simplicity
        virtual void                        UpdateSubresources( vaRenderDeviceContext & renderContext, uint32 firstSubresource, /*const*/ std::vector<vaTextureSubresourceData> & subresources ) = 0;
        virtual bool                        TryMap( vaRenderDeviceContext & renderContext, vaResourceMapType mapType, bool doNotWait = false )                             = 0;
        virtual void                        Unmap( vaRenderDeviceContext & renderContext )                                                                                 = 0;

        // all these should maybe go to API context itself to match DX
        virtual void                        ClearRTV( vaRenderDeviceContext & renderContext, const vaVector4 & clearValue )                                                = 0;
        virtual void                        ClearUAV( vaRenderDeviceContext & renderContext, const vaVector4ui & clearValue )                                              = 0;
        virtual void                        ClearUAV( vaRenderDeviceContext & renderContext, const vaVector4 & clearValue )                                                = 0;
        virtual void                        ClearDSV( vaRenderDeviceContext & renderContext, bool clearDepth, float depthValue, bool clearStencil, uint8 stencilValue )    = 0;

        // these will copy or resolve if copying from multiple sample to 1 sample (hey how about a static version instead of these two?)
        virtual void                        CopyFrom( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & srcTexture )                                    = 0;
        virtual void                        CopyTo( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & dstTexture )                                      = 0;

        // virtual void                        UpdateSubresource( vaRenderDeviceContext & renderContext, int dstSubresourceIndex, const vaBoxi & dstBox, void * srcData, int srcDataRowPitch, int srcDataDepthPitch = 0 ) = 0;
        virtual void                        ResolveSubresource( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & dstResource, uint dstSubresource, uint srcSubresource, vaResourceFormat format = vaResourceFormat::Automatic ) = 0;

        // Will try to create a BC5-6-7 compressed copy of the texture to the best of its abilities or if it can't then return nullptr; very rudimentary at the moment, future upgrades required.
        virtual shared_ptr<vaTexture>       TryCompress( )                                                                                                              = 0;

        static shared_ptr<vaTexture>        TryCreateMIPs( vaRenderDeviceContext & renderContext, shared_ptr<vaTexture> & texture );

        virtual shared_ptr<vaTexture>       CreateLowerResFromMIPs( vaRenderDeviceContext & renderContext, int numberOfMIPsToDrop, bool neverGoBelow4x4 = true );

        // these are here as I've got no better place for them at the moment - maybe they should go into something like vaImageTools, or simply to vaTexture
        virtual bool                        SaveToDDSFile( vaRenderDeviceContext & renderContext, const wstring & path ) = 0;
        virtual bool                        SaveToPNGFile( vaRenderDeviceContext & renderContext, const wstring & path ) = 0;
        // virtual bool                        SaveToPNGBuffer( vaRenderDeviceContext & renderContext, const wstring & path ) = 0; // not yet implemented but should be trivial

    protected:
        virtual bool                        Import( const wstring & storageFilePath, vaTextureLoadFlags loadFlags, vaResourceBindSupportFlags binds, vaTextureContentsType contentsType = vaTextureContentsType::GenericColor ) = 0;
        virtual bool                        Import( void * buffer, uint64 bufferSize, vaTextureLoadFlags loadFlags = vaTextureLoadFlags::Default, vaResourceBindSupportFlags binds = vaResourceBindSupportFlags::ShaderResource, vaTextureContentsType contentsType = vaTextureContentsType::GenericColor ) = 0;
        virtual void                        Destroy( ) = 0;

        virtual shared_ptr<vaTexture>       CreateViewInternal( const shared_ptr<vaTexture> & thisTexture, vaResourceBindSupportFlags bindFlags, vaResourceFormat srvFormat, vaResourceFormat rtvFormat, vaResourceFormat dsvFormat, vaResourceFormat uavFormat, vaTextureFlags flags, int viewedMipSliceMin, int viewedMipSliceCount, int viewedArraySliceMin, int viewedArraySliceCount ) = 0;

        virtual bool                        InternalCreate1D( vaResourceFormat format, int width, int mipLevels, int arraySize, vaResourceBindSupportFlags bindFlags, vaResourceAccessFlags accessFlags, vaResourceFormat srvFormat, vaResourceFormat rtvFormat, vaResourceFormat dsvFormat, vaResourceFormat uavFormat, vaTextureFlags flags, vaTextureContentsType contentsType, const void * initialData ) = 0;
        virtual bool                        InternalCreate2D( vaResourceFormat format, int width, int height, int mipLevels, int arraySize, int sampleCount, vaResourceBindSupportFlags bindFlags, vaResourceAccessFlags accessFlags, vaResourceFormat srvFormat, vaResourceFormat rtvFormat, vaResourceFormat dsvFormat, vaResourceFormat uavFormat, vaTextureFlags flags, vaTextureContentsType contentsType, const void * initialData, int initialDataRowPitch ) = 0;
        virtual bool                        InternalCreate3D( vaResourceFormat format, int width, int height, int depth, int mipLevels, vaResourceBindSupportFlags bindFlags, vaResourceAccessFlags accessFlags, vaResourceFormat srvFormat, vaResourceFormat rtvFormat, vaResourceFormat dsvFormat, vaResourceFormat uavFormat, vaTextureFlags flags, vaTextureContentsType contentsType, const void * initialData, int initialDataRowPitch, int initialDataSlicePitch ) = 0;

    public:
        void                                ClearRTV( vaRenderDeviceContext & renderContext, float clearValue[] )                       { ClearRTV( renderContext, vaVector4(clearValue) ); }
        void                                ClearUAV( vaRenderDeviceContext & renderContext, uint32 clearValue[] )                      { ClearUAV( renderContext, vaVector4ui(clearValue[0], clearValue[1], clearValue[2], clearValue[3]) ); }
        void                                ClearUAV( vaRenderDeviceContext & renderContext, float clearValue[] )                       { ClearUAV( renderContext, vaVector4(clearValue) ); }
        void                                ClearUAV( vaRenderDeviceContext & renderContext, uint32 clearValue )                        { ClearUAV( renderContext, vaVector4ui(clearValue, clearValue, clearValue, clearValue) ); }
        void                                ClearUAV( vaRenderDeviceContext & renderContext, float clearValue )                         { ClearUAV( renderContext, vaVector4(clearValue,clearValue,clearValue,clearValue) ); }

        // This is for D3D12 fast clears. Does nothing on D3D11. Valid only for the next Create1D / Create2D / Create3D. Format has to match the format of the RTV/DSV. 
        // VA does no validation for this (so test verify using D3D12 validation layer)
        static void                         SetNextCreateFastClearRTV( vaResourceFormat format, vaVector4 clearColor );
        static void                         SetNextCreateFastClearDSV( vaResourceFormat format, float clearDepth, uint8 clearStencil );
        static vaResourceFormat             s_nextCreateFastClearFormat;
        static vaVector4                    s_nextCreateFastClearColorValue;
        static float                        s_nextCreateFastClearDepthValue;
        static uint8                        s_nextCreateFastClearStencilValue;

    public:
        bool                                UIPropertiesDraw( vaApplicationBase & application ) override;

        vaAssetType                         GetAssetType( ) const override                          { return vaAssetType::Texture; }
        virtual void                        SetParentAsset( vaAsset * asset ) override;

        //virtual bool                        GetCUDAShared( void * & outPointer, size_t & outSize )  { return false; outPointer; outSize; }
    };

    inline string vaTextureContentsTypeToUIName( vaTextureContentsType value )
    {
        switch( value )
        {
            case vaTextureContentsType::GenericColor           : return "GenericColor";            break;
            case vaTextureContentsType::GenericLinear          : return "GenericLinear";           break;
            case vaTextureContentsType::NormalsXYZ_UNORM       : return "NormalsXYZ_UNORM";        break;
            case vaTextureContentsType::NormalsXY_UNORM        : return "NormalsXY_UNORM";         break;
            case vaTextureContentsType::NormalsWY_UNORM        : return "NormalsWY_UNORM";         break;
            case vaTextureContentsType::SingleChannelLinearMask: return "SingleChannelLinearMask"; break;
            case vaTextureContentsType::DepthBuffer            : return "DepthBuffer";             break;
            case vaTextureContentsType::LinearDepth            : return "LinearDepth";             break;
            case vaTextureContentsType::NormalsXY_LAEA_ENCODED : return "NormalsXY_LAEA_ENCODED";  break;
        default: assert( false ); return "error"; break;
        }
    }

}
                                                                                                                                                                              