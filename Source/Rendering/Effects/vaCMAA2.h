///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: Apache-2.0 OR MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Core/vaCoreIncludes.h"
#include "Core/vaUI.h"

#include "Rendering/vaRenderingIncludes.h"

#ifndef __INTELLISENSE__
#include "Rendering/Shaders/vaCMAA2.hlsl"
#endif

namespace Vanilla
{
    class vaCMAA2 : public Vanilla::vaRenderingModule, public vaUIPanel
    {
    public:

        enum Preset { PRESET_LOW, PRESET_MEDIUM, PRESET_HIGH, PRESET_ULTRA };

        struct Settings
        {
            bool                            ExtraSharpness;
            Preset                          QualityPreset;                  // 0 - LOW, 1 - MEDIUM, 2 - HIGH (default), 3 - HIGHEST

            Settings( )
            {
                ExtraSharpness                  = false;
                QualityPreset                   = PRESET_HIGH;
            }
        };

    protected:
        Settings                    m_settings;

        bool                        m_debugShowEdges;

    protected:
        vaCMAA2( const vaRenderingModuleParams & params );
    public:
        ~vaCMAA2( );

    public:
        virtual vaDrawResultFlags   Draw( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & inoutColor, const shared_ptr<vaTexture> & optionalInLuma = nullptr )                  = 0;

        virtual vaDrawResultFlags   DrawMS( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & inoutColor, const shared_ptr<vaTexture> & inColorMS, const shared_ptr<vaTexture> & inColorMSComplexityMask ) = 0;

        // if CMAA2 is no longer used make sure it's not reserving any memory
        virtual void                CleanupTemporaryResources( )                                                    = 0;

        Settings &                  Settings( )                                                                     { return m_settings; }

    private:
        virtual void                UIPanelTick( vaApplicationBase & ) override;
        virtual bool                UIPanelIsListed( ) const override          { return false; }
    };

}
