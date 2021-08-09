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

#include "Rendering/vaRenderingIncludes.h"

#ifndef __INTELLISENSE__
#include "Rendering/Shaders/vaSky.hlsl"
#endif

namespace Vanilla
{
    class vaSky : public Vanilla::vaRenderingModule
    {
    public:
        struct Settings
        {
            float                       SunAzimuth;
            float                       SunElevation;

            vaVector4                   SkyColorLow;
            vaVector4                   SkyColorHigh;

            vaVector4                   SunColorPrimary;
            vaVector4                   SunColorSecondary;

            float                       SkyColorLowPow;
            float                       SkyColorLowMul;

            float                       SunColorPrimaryPow;
            float                       SunColorPrimaryMul;
            float                       SunColorSecondaryPow;
            float                       SunColorSecondaryMul;

            vaVector3                   FogColor;
            float                       FogDistanceMin;
            float                       FogDensity;
        };

    protected:

        // these are calculated from azimuth & elevation, but smoothly interpolated to avoid sudden changes
        vaVector3                       m_sunDirTargetL0;    // directly calculated from azimuth & elevation
        vaVector3                       m_sunDirTargetL1;    // lerped to m_sunDirTargetL0
        vaVector3                       m_sunDir;            // final, lerped to m_sunDirTargetL1

        Settings                        m_settings;

        vaAutoRMI<vaVertexShader>       m_vertexShader;
        vaAutoRMI<vaPixelShader>        m_pixelShader;
        //shared_ptr<vaDynamicVertexBuffer>  m_screenTriangleVertexBuffer;
        //shared_ptr<vaDynamicVertexBuffer>  m_screenTriangleVertexBufferReversedZ;

        vaTypedConstantBufferWrapper< SimpleSkyConstants >
                                        m_constantsBuffer;

    protected:
        VA_RENDERING_MODULE_MAKE_FRIENDS();
        vaSky( const vaRenderingModuleParams & params );
    public:
        ~vaSky( );

    public:

    public:
        Settings &                  GetSettings( )          { return m_settings; }
        const Settings &            GetSettings( ) const    { return m_settings; }
        const vaVector3 &           GetSunDir( ) const      { return m_sunDir; }

        void                        Tick( float deltaTime, vaSceneLighting * lightingToUpdate );

    public:
        virtual void                Draw( vaDrawAttributes & drawAttributes );
    };

}
