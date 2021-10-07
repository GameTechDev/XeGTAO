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

#include "Core/vaUI.h"
#include "Rendering/vaRendering.h"

namespace Vanilla
{
    class vaInputMouseBase;

    class vaZoomTool : public Vanilla::vaRenderingModule, public vaUIPanel
    {
    public:
        struct Settings
        {
            bool                        Enabled     = false;
            int                         ZoomFactor  = 4;
            vaVector2i                  BoxPos      = vaVector2i( 400, 300 );
            vaVector2i                  BoxSize     = vaVector2i( 128, 96 );

            Settings( )            { }
        };

    protected:
        shared_ptr<vaTexture>           m_edgesTexture;

        Settings                        m_settings;

        shared_ptr<vaConstantBuffer>    m_constantBuffer;

        vaAutoRMI<vaComputeShader>      m_CSZoomToolFloat;
        vaAutoRMI<vaComputeShader>      m_CSZoomToolUnorm;

    public:
        vaZoomTool( const vaRenderingModuleParams & params );
        ~vaZoomTool( );

    public:
        Settings &                      Settings( )                                                             { return m_settings; }

        void                            HandleMouseInputs( vaInputMouseBase & mouseInput );

        virtual void                    Draw( vaRenderDeviceContext & renderContext, shared_ptr<vaTexture> colorInOut );    // colorInOut is not a const reference for a reason (can change if it's the current RT)

    protected:
        virtual void                    UpdateConstants( vaRenderDeviceContext & renderContext );

    private:
        virtual void                    UIPanelTickAlways( vaApplicationBase & application ) override;
        virtual void                    UIPanelTick( vaApplicationBase & application ) override;
    };
    
}
