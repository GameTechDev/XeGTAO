/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Vanilla Codebase, Copyright (c) Filip Strugar.
// Contents of this file are distributed under MIT license (https://en.wikipedia.org/wiki/MIT_License)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Core/vaCoreIncludes.h"

#include "Rendering/vaRenderingIncludes.h"

#ifndef __INTELLISENSE__
#include "Rendering/Shaders/vaSkybox.hlsl"
#endif

namespace Vanilla
{
    class vaSkybox : public Vanilla::vaRenderingModule
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );
    public:
        struct Settings
        {
            vaMatrix3x3             Rotation        = vaMatrix3x3::Identity;
            float                   ColorMultiplier = 1.0f;

            Settings( )            { }
        };

    protected:
        string                      m_cubemapPath;
        shared_ptr<vaTexture>       m_cubemap;

        Settings                    m_settings;

        vaTypedConstantBufferWrapper<ShaderSkyboxConstants>
                                    m_constantsBuffer;

        vaAutoRMI<vaVertexShader>   m_vertexShader;
        vaAutoRMI<vaPixelShader>    m_pixelShader;

        //shared_ptr<vaDynamicVertexBuffer>  m_screenTriangleVertexBuffer;
        //shared_ptr<vaDynamicVertexBuffer>  m_screenTriangleVertexBufferReversedZ;

    protected:
        vaSkybox( const vaRenderingModuleParams & params );
    public:
        ~vaSkybox( );

    public:
        Settings &                  Settings( )                                         { return m_settings; }

        shared_ptr<vaTexture>       GetCubemap( ) const                                 { return m_cubemap;     }
        void                        SetCubemap( const shared_ptr<vaTexture> & cubemap ) { m_cubemap = cubemap; m_cubemapPath = ""; }

        bool                        IsEnabled( ) const                                  { return m_cubemap != nullptr; }
        void                        Disable( )                                          { m_cubemap = nullptr;}

        void                        UpdateFromScene( class vaScene & scene, float deltaTime, int64 applicationTickIndex );

    public:
        virtual void                UpdateConstants( vaDrawAttributes & drawAttributes, ShaderSkyboxConstants & consts );

    public:
        virtual vaDrawResultFlags   Draw( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, vaDrawAttributes & drawAttributes );
    };

}
