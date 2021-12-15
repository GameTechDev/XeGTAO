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

#include "Core\vaCoreIncludes.h"

#include "vaApplicationWin.h"

#include "Core\vaUI.h"

#include "Core\Misc\vaMiniScript.h"

#include "Scene\vaSceneTypes.h"

namespace Vanilla
{
    class AutoBenchTool;
    class vaAssetImporter;
    class vaScene;
    class vaSceneRenderer;
    class vaSceneMainRenderView;
    class vaCameraControllerFlythrough;
    class vaCameraControllerFreeFlight;
    class vaMemoryStream;
    class vaRenderDevice;
    class vaRenderCamera;
    class vaZoomTool;
    class vaImageCompareTool;

    class VanillaSample : public vaRenderingModule, public vaUIPanel
    {
    public:

    public:

        struct VanillaSampleSettings
        {
            string                                  CurrentSceneName            = "LumberyardBistro";
            float                                   CameraYFov                  = 55.0f / 180.0f * VA_PIf;
            //AAType                                  CurrentAAOption             = AAType::None;
            //int                                     ShadingComplexity = 1;                        // 0 == low, 1 == medium, 2 == high

            void Serialize( vaXMLSerializer & serializer )
            {
#ifndef VA_GTAO_SAMPLE
                serializer.Serialize( "CurrentSceneName"            , CurrentSceneName              );
#endif
                serializer.Serialize( "CameraYFov"                  , CameraYFov                    );
                //serializer.Serialize( "CurrentAAOption"             , (int&)CurrentAAOption         );
                //serializer.Serialize( "ShadingComplexity", (int&)ShadingComplexity );

                // this here is just to remind you to update serialization when changing the struct
                size_t dbgSizeOfThis = sizeof(*this); dbgSizeOfThis;
                assert( dbgSizeOfThis == 48 );
            }

            void Validate( )
            {
                CameraYFov                      = vaMath::Clamp( CameraYFov, 15.0f / 180.0f * VA_PIf, 130.0f / 180.0f * VA_PIf );
                // ShadingComplexity               = vaMath::Clamp( ShadingComplexity, 0, 2 );
            }
        };

    protected:
        shared_ptr<vaCameraControllerFreeFlight>
                                                m_cameraFreeFlightController;
        shared_ptr<vaCameraControllerFlythrough>
                                                m_cameraFlythroughController;
        bool                                    m_cameraFlythroughPlay = false;

        vaApplicationBase &                     m_application;

        shared_ptr<vaSceneRenderer>             m_sceneRenderer;
        shared_ptr<vaSceneMainRenderView>       m_sceneMainView;

        std::vector<string>                     m_scenesInFolder;
        int                                     m_currentSceneIndex             = -1;

        shared_ptr<vaScene>                     m_currentScene;
        
        shared_ptr<vaZoomTool>                  m_zoomTool;
        shared_ptr<vaImageCompareTool>          m_imageCompareTool;

        float                                   m_lastDeltaTime;

        vaVector3                               m_mouseCursor3DWorldPosition;

        bool                                    m_requireDeterminism            = false;    // not sure this really works anymore
        bool                                    m_allLoadedPrecomputedAndStable = false;    // updated at the end of each frame; will be true if all assets are loaded, shaders compiler and static shadow maps created 

        shared_ptr<vaAssetImporter> const       m_assetImporter;

        vaMiniScript                            m_miniScript;
        shared_ptr<vaTexture>                   m_currentFrameTexture;  // used by scripting

        static constexpr int                    c_presetCameraCount             = 10;
        shared_ptr<vaMemoryStream>              m_presetCameras[c_presetCameraCount];
        bool                                    m_presetCamerasDirty            = false;
        int                                     m_presetCameraSelectedIndex     = -1;

        // backup of the original camera
        shared_ptr<vaMemoryStream>              m_cameraBackup;

    protected:
        VanillaSampleSettings                   m_settings;
        VanillaSampleSettings                   m_lastSettings;
        bool                                    m_globalShaderMacrosDirty = true;

        bool                                    m_hasTicked                     = false;

        /// temporary bistro animation stuff for testing verious temporal issues

        shared_ptr<void>                        m_interactiveBistroContext      = nullptr;
        //////////////////////////////////////////////////////////////////////////

    public:
        VanillaSample( vaRenderDevice & renderDevice, vaApplicationBase & applicationBase, bool importerMode );
        virtual ~VanillaSample( );

        const vaApplicationBase &               GetApplication( ) const             { return m_application; }
        vaApplicationBase &                     GetApplication( )                   { return m_application; }

    public:
        const shared_ptr<vaRenderCamera> &      Camera( );
        const shared_ptr<vaSceneMainRenderView> & MainRenderView( )                 { return m_sceneMainView; }
        VanillaSampleSettings &                 Settings( )                         { return m_settings; }
        //shared_ptr<vaPostProcessTonemap>  &     PostProcessTonemap( )               { return m_postProcessTonemap; }

        void                                    SetRequireDeterminism( bool enable ){ m_requireDeterminism = enable; }
        bool                                    IsAllLoadedPrecomputedAndStable( )  { return m_allLoadedPrecomputedAndStable; }

        const shared_ptr<vaCameraControllerFlythrough> & 
                                                GetFlythroughCameraController()     { return m_cameraFlythroughController; }
        bool                                    GetFlythroughCameraEnabled() const  { return m_cameraFlythroughPlay; }
        void                                    SetFlythroughCameraEnabled( bool enabled ) { m_cameraFlythroughPlay = enabled; }

        int &                                   PresetCameraIndex( )                { return m_presetCameraSelectedIndex; }
        int                                     PresetCameraCount( ) const          { return c_presetCameraCount; }
        bool                                    HasPresetCamera( int index )        { assert( index >= 0 && index < c_presetCameraCount ); return m_presetCameras[index] != nullptr; }
        const shared_ptr<vaTexture> &           CurrentFrameTexture( ) const        {  return m_currentFrameTexture; }

        //const shared_ptr<vaImageCompareTool> &  ImageCompareTools( ) const          { return m_imageCompareTool; }

    public:
        // events/callbacks:
        void                                    OnBeforeStopped( );
        void                                    OnTick( float deltaTime );
        void                                    OnSerializeSettings( vaXMLSerializer & serializer );

    protected:
        vaDrawResultFlags                       RenderTick( float deltaTime );
        void                                    LoadAssetsAndScenes( );

        bool                                    LoadCamera( int index = -1 );
        void                                    SaveCamera( int index = -1 );

        // temporary camera backup/restore (if changing with scripts or whatever)
        void                                    BackupCamera( );
        bool                                    RestoreCamera( );
        bool                                    HasCameraBackup( )                  { return m_cameraBackup != nullptr; }

        virtual void                            UIPanelTick( vaApplicationBase & application ) override;
#ifndef VA_GTAO_SAMPLE
        virtual string                          UIPanelGetDisplayName() const override                      { return (m_assetImporter!=nullptr)?("VanillaAssetImporter"):("Vanilla"); };
#else
        virtual string                          UIPanelGetDisplayName() const override                      { return (m_assetImporter!=nullptr)?("VanillaAssetImporter"):("XeGTAO Sample"); };
#endif

        void                                    ScriptedTests( vaApplicationBase & application ) ;

        void                                    ScriptedGTAOAutoTune( vaApplicationBase & application ) ;
        void                                    ScriptedCameras( vaApplicationBase & application ) ;
        void                                    ScriptedAutoBench( vaApplicationBase & application ) ;
        void                                    ScriptedDemo( vaApplicationBase & application ) ;

    private:
        void                                    InteractiveBistroTick( float deltaTime, bool sceneChanged );
        void                                    InteractiveBistroUI( vaApplicationBase & application );

    };

}