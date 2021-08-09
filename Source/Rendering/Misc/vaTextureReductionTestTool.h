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
#include "Core/vaSingleton.h"

#include "Rendering/vaRenderingIncludes.h"

#include "Rendering/Effects/vaPostProcessTonemap.h"

#include "vaImageCompareTool.h"

#include "Core/System/vaMemoryStream.h"

//#define VA_ENABLE_TEXTURE_REDUCTION_TOOL

#ifdef VA_ENABLE_TEXTURE_REDUCTION_TOOL

namespace Vanilla
{
    struct vaAssetTexture;

    class vaTextureReductionTestTool : public vaSingletonBase<vaTextureReductionTestTool>
    {
    public:
        typedef pair< shared_ptr<vaTexture>, string > TestItemType;

        //struct Camera

    protected:
        vector<TestItemType>                m_textures;
        vector<shared_ptr<vaAssetTexture>>  m_textureAssets;
        vector<int>                         m_texturesMaxFoundReduction;
        vector<int>                         m_texturesSorted;

        // vector<vector<float>>               m_resultsPSNR;
        // vector<vector<float>>               m_resultsMSR;
        // vector<float>                       m_avgPSNR;
        // vector<float>                       m_avgMSR;

        bool                                m_runningTests                  = false;
        int                                 m_currentTexture                = -1;
        int                                 m_currentCamera                 = -1;
        int                                 m_currentSearchReductionCount   = -1;
        //bool                                m_restoreTonemappingAfterTests  = false;

        bool                                m_enabled                       = true;
        bool                                m_popupJustOpened               = true;

        shared_ptr<vaTexture>               m_referenceTexture;

        shared_ptr<vaTexture>               m_currentlyOverriddenTexture;
        shared_ptr<vaTexture>               m_currentOverrideView;

        static const int                    c_cameraSlotCount               = 40;
        shared_ptr<vaMemoryStream>          m_cameraSlots[c_cameraSlotCount];
        shared_ptr<vaMemoryStream>          m_userCameraBackup;
        float                               m_targetPSNRThreshold           = 68.0f;
        int                                 m_maxLevelsToDrop               = 4;
        //int                                 m_minResNotToGoBelow            = 128; todo: do this at some point ...
        int                                 m_cameraSlotSelectedIndex       = -1;

        int                                 m_downscaleTextureButtonClicks  = 2;

        //vaPostProcessTonemap::TMSettings
        //                                    m_userTonemapSettings;

        bool                                m_overrideAll                   = false;

        static bool                         s_supportedByApp;

    public:
        // this could be made to work without asset links - except then no automatic reduction could be done, just the report
        vaTextureReductionTestTool( const vector<TestItemType> & textures, vector<shared_ptr<vaAssetTexture>> textureAssets );
        ~vaTextureReductionTestTool( );

    public:
        // if had control of camera, revert to before captured
        void                        ResetCamera( const shared_ptr<vaRenderCamera> & camera );

        void                        TickCPU( const shared_ptr<vaRenderCamera> & camera );
        void                        TickGPU( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & colorBuffer );

        bool                        IsEnabled( ) const              { return m_enabled; }

        // ideally you want to automatically stop any scene movement during this
        bool                        IsRunningTests( ) const         { return m_runningTests; }

        void                        TickUI( vaRenderDevice & device, const shared_ptr<vaRenderCamera> & camera, bool hasMouse );

        void                        OverrideAllWithCurrentStates( );

        void                        DownscaleAll( vaRenderDeviceContext & renderContext );

    private:
        void                        SaveAsReference( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & colorBuffer );

        void                        ResetData( );
        void                        ResetTextureOverrides( );
        //void                        ComputeAveragesAndSort( );

    public:
        static void                 SetSupportedByApp( )            { s_supportedByApp = true; }
        static bool                 GetSupportedByApp( )            { return s_supportedByApp; }
    };

}

#endif