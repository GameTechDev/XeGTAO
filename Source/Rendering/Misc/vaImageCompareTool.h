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
#include "Rendering/Effects/vaPostProcess.h"

#include "Core/vaUI.h"

namespace Vanilla
{
    // will be moved to its own file at some point, if it ever grows into something more serious
    class vaImageCompareTool : public Vanilla::vaRenderingModule, public vaUIPanel
    {
    public:
        enum class VisType
        {
            None,
            ShowReference,
            ShowDifference,
            ShowDifferenceX10,
            ShowDifferenceX100
        };

    protected:
        shared_ptr<vaTexture>           m_referenceTexture;
        shared_ptr<vaTexture>           m_helperTexture;

        bool                            m_saveReferenceScheduled            = false;
        bool                            m_compareReferenceScheduled         = false;

        wstring                         m_referenceDDSTextureStoragePath;   // first save to raw .dds - this guarantees image is identical when loaded as a reference, but since it's not good for reading with other image tools, also save as .png
        wstring                         m_referencePNGTextureStoragePath;   // using PNG is not good as a reference due to potential conversion but it's easy to read from any other tool, so save as it like that also 
        wstring                         m_screenshotCapturePath;
        int                             m_screenshotCaptureCounter          = 0;

        VisType                         m_visualizationType                 = VisType::None;

        vaAutoRMI<vaPixelShader>        m_visualizationPS;
        shared_ptr<vaConstantBuffer>    m_constants;

        bool                            m_initialized;

    public: //protected:
        vaImageCompareTool( const vaRenderingModuleParams & params );
    public:
        ~vaImageCompareTool( );

    public:
        virtual void                    RenderTick( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & colorInOut );

    public:
        virtual void                    SaveAsReference( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & colorInOut );

        // See vaPostProcess::CompareImages for description of the results
        virtual vaVector4               CompareWithReference( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & colorInOut );

    protected:

    private:
        virtual void                    UIPanelTickAlways( vaApplicationBase & application ) override;
        virtual void                    UIPanelTick( vaApplicationBase & application ) override;
    };


}
