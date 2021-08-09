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

#include "vaTexture.h"

#include "Rendering/vaRenderBuffers.h"
#include "Rendering/vaShader.h"

#include "Rendering/Shaders/vaHelperToolsShared.h"

namespace Vanilla
{

    //////////////////////////////////////////////////////////////////////////
    // intended to provide reuse of textures but not very flexible yet
    //////////////////////////////////////////////////////////////////////////
    class vaTexturePool : public vaSingletonBase<vaTexturePool>
    {
    public:
        int                                     m_maxPooledTextureCount;
        int                                     m_maxMemoryUse;    

        struct ItemDesc
        {
            vaRenderDevice *                    Device;
            vaTextureFlags                      Flags;
            vaResourceAccessFlags               AccessFlags;
            vaTextureType                       Type;
            vaResourceBindSupportFlags          BindSupportFlags;
            vaResourceFormat                    ResourceFormat;
            vaResourceFormat                    SRVFormat;
            vaResourceFormat                    RTVFormat;
            vaResourceFormat                    DSVFormat;
            vaResourceFormat                    UAVFormat;
            int                                 SizeX;
            int                                 SizeY;
            int                                 SizeZ;
            int                                 SampleCount;
            int                                 MIPLevels;
        };

        struct ItemComparer
        {
            bool operator()( const ItemDesc & Left, const ItemDesc & Right ) const
            {
                // comparison logic goes here
                return memcmp( &Left, &Right, sizeof( Right ) ) < 0;
            }
        };

        std::multimap< ItemDesc, shared_ptr<vaTexture>, ItemComparer >
                                                m_items;

        std::mutex                              m_mutex;

    public:
        vaTexturePool( ) : m_maxPooledTextureCount( 64 ), m_maxMemoryUse( 64 * 1024 * 1024 ) { }
        virtual ~vaTexturePool( )               { }

    protected:
        void                                    FillDesc( ItemDesc & desc, const shared_ptr< vaTexture > texture );

    public:
        shared_ptr< vaTexture >                 FindOrCreate2D( vaRenderDevice & device, vaResourceFormat format, int width, int height, int mipLevels, int arraySize, int sampleCount, vaResourceBindSupportFlags bindFlags, vaResourceAccessFlags accessFlags = vaResourceAccessFlags::Default, void * initialData = NULL, int initialDataRowPitch = 0, vaResourceFormat srvFormat = vaResourceFormat::Automatic, vaResourceFormat rtvFormat = vaResourceFormat::Automatic, vaResourceFormat dsvFormat = vaResourceFormat::Automatic, vaResourceFormat uavFormat = vaResourceFormat::Automatic, vaTextureFlags flags = vaTextureFlags::None );
        
        // releases a texture into the pool so it can be returned by FindOrCreateXX
        void                                    Release( const shared_ptr< vaTexture > texture );

        void                                    ClearAll( );
    };


    //////////////////////////////////////////////////////////////////////////
    // Provides some common textures, utility functions, etc.
    //////////////////////////////////////////////////////////////////////////
    class vaTextureTools// : public vaSingletonBase<vaTextureTools>
    {
    public:
        enum class CommonTextureName
        {
            Black1x1,
            White1x1,
            Checkerboard16x16,
            Black1x1Cube,
            BlueNoise64x64x1_3spp,
            BlueNoise64x64x64_2spp,     // <- not actually available at the moment

            MaxValue
        };

        struct UITextureState
        {
            weak_ptr<vaTexture>         Texture;
            vaVector4                   ClipRectangle;  // x, y, width, height
            vaVector4                   Rectangle;      // x, y, width, height
            float                       Alpha           = 1.0f;
            int                         ArrayIndex      = 0;
            int                         MIPIndex        = 0;
            bool                        ShowAlpha       = false;
            bool                        FullscreenPopUp = false;
            bool                        InUse           = false;

            // Task scheduled from the UI - MIP creation, compression, etc.
            std::function< void( vaRenderDeviceContext &, UITextureState & ) >
                                        ScheduledTask;
        };

    protected:
        shared_ptr< vaTexture >                 m_textures[(int)CommonTextureName::MaxValue];

        std::vector<UITextureState>                  m_UIDrawItems;

        vaTypedConstantBufferWrapper< UITextureDrawShaderConstants >
                                                m_UIDrawShaderConstants;

        vaAutoRMI<vaPixelShader>                m_UIDrawTexture2DPS;
        vaAutoRMI<vaPixelShader>                m_UIDrawTexture2DArrayPS;
        vaAutoRMI<vaPixelShader>                m_UIDrawTextureCubePS;

        shared_ptr<int>                         m_aliveToken      = std::make_shared<int>(42);

    public:
        vaTextureTools( vaRenderDevice & device );
        virtual ~vaTextureTools( ) { }

    public:

        shared_ptr< vaTexture >                 GetCommonTexture( CommonTextureName textureName );
        
        void                                    UIDrawImages( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs );

        bool                                    UITickImGui( const shared_ptr<vaTexture> & texture );

    private:
        friend class vaRenderDevice;
        void                                    OnBeginFrame( vaRenderDevice & device, float deltaTime );
    };

}