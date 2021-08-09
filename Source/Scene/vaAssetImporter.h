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

#include "Rendering/vaAssetPack.h"

#include "Scene/vaScene.h"

namespace Vanilla
{
    class vaAssetImporter : public vaUIPanel
    {
    public:

        struct ImporterSettings
        {
            bool                        TextureOnlyLoadDDS                  = false;
            bool                        TextureTryLoadDDS                   = true;
            bool                        TextureGenerateMIPs                 = true;

            bool                        AIForceGenerateNormals              = false;
            bool                        AIGenerateNormalsIfNeeded           = true;
            bool                        AIGenerateSmoothNormalsIfGenerating = true;
            float                       AIGenerateSmoothNormalsSmoothingAngle = 88.0f;      // in degrees, see AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE for more info
            //bool                        RegenerateTangents;  

            bool                        AISplitLargeMeshes                  = false;        // aiProcess_SplitLargeMeshes
            bool                        AIFindInstances                     = true;         // aiProcess_FindInstances
            bool                        AIOptimizeMeshes                    = false;        // aiProcess_OptimizeMeshes
            bool                        AIOptimizeGraph                     = false;        // aiProcess_OptimizeGraph
            bool                        AIFLipUVs                           = false;        // aiProcess_FlipUVs

            bool                        EnableLogInfo                       = true;
            bool                        EnableLogWarning                    = true;
            bool                        EnableLogError                      = true;

            string                      AssetNamePrefix                     = "";

            string                      DefaultDistantIBL                   = "noon_grass_2k.hdr";
            //bool                        AddDefaultLightDirectional          = false;
            //vaVector3                   DefaultLightDirectionalDir          = vaVector3( -0.5f, -0.5f, -1.0f ).Normalized();
            //vaVector3                   DefaultLightDirectionalColor        = vaVector3( 1, 1, 1 );
            //float                       DefaultLightDirectionalIntensity    = 1.0f;

            vaVector3                   BaseRotateYawPitchRoll              = { 0.0f, 0.0f, 90.0f }; // Yaw around the +Z (up) axis, a pitch around the +Y (right) axis, and a roll around the +X (forward) axis.
            vaVector3                   BaseTransformScaling                = { 1, 1, 1 };
            vaVector3                   BaseTransformOffset                 = { 0, 0, 0 };
        };

        struct ImporterContext
        {
        private:
            vaRenderDevice &                Device;

        public:
            string const                    FileName;

            // pack to save assets into and to search dependencies to link to
            shared_ptr<vaAssetPack>         AssetPack;
            shared_ptr<vaScene> const       Scene;
            ImporterSettings                Settings;

            string const                    NamePrefix;             // added to loaded asset resource names (can be used to create hierarchy - "importfilename\"
            vaMatrix4x4 const               BaseTransform;          // for conversion between coord systems, etc

        private:
            std::atomic_bool                aborted                 = false;
            std::shared_mutex               progressMutex;
            std::atomic<float>              progressPercentage;
            string                          progressLog;

        public:

            ImporterContext( vaRenderDevice & device, string fileName, shared_ptr<vaAssetPack> assetPack, shared_ptr<vaScene> scene, ImporterSettings & settings, const vaMatrix4x4 & baseTransform = vaMatrix4x4::Identity ) 
                : Device( device ), FileName( fileName ), AssetPack( assetPack ), Scene( scene ), Settings( settings ), BaseTransform( baseTransform ), NamePrefix("")
            {
            }

            ~ImporterContext( );

            ImporterContext( const ImporterContext & ) = delete;

            void                        Abort( )                            { aborted = true; }
            void                        AddLog( string logLine )            { std::scoped_lock lock(progressMutex); progressLog += logLine; }
            void                        SetProgress( float percentage )     { progressPercentage = percentage; }
            bool                        IsAborted( )                        { return aborted; }
            string                      GetLog( )                           { std::scoped_lock lock( progressMutex ); return progressLog; }
            float                       GetProgress( )                      { return progressPercentage; }

            bool                        AsyncInvokeAtBeginFrame( const std::function<bool( vaRenderDevice & renderDevice, ImporterContext& )> & asyncCallback )
            {
                if( Device.IsRenderThread( ) )
                {
                    if( IsAborted( ) )
                        return false;
                    return asyncCallback( this->Device, *this );
                }
                else
                {
                    if( ! Device.AsyncInvokeAtBeginFrame( [ importerContext=this, asyncCallback ]( vaRenderDevice& renderDevice, float deltaTime ) -> bool
                    {
                        if( importerContext->IsAborted( ) || deltaTime == std::numeric_limits<float>::lowest( ) )
                            return false;

                        return asyncCallback( renderDevice, *importerContext );
                    } ).get( ) )
                    {
                        Abort( ); return false;
                    }
                    else
                        return true;
                }
            }
        };

        bool                                                m_readyToImport = true;

        string                                              m_inputFile;
        ImporterSettings                                    m_settings;
        // vector<shared_ptr<vaAsset>>                         m_importedAssetsList;

        shared_ptr<ImporterContext>                         m_importerContext;
        shared_ptr<vaBackgroundTaskManager::Task>           m_importerTask;

        vaRenderDevice &                                    m_device;

    public:
        vaAssetImporter( vaRenderDevice & device );
        virtual ~vaAssetImporter( );

    public:
        shared_ptr<vaAssetPack>                             GetAssetPack( ) const                                   { return (m_importerContext==nullptr)?(nullptr):(m_importerContext->AssetPack); }
        shared_ptr<vaScene>                                 GetScene( ) const                                       { return (m_importerContext==nullptr)?(nullptr):(m_importerContext->Scene); }

        void                                                Clear( );

        void                                                Draw3DUI( vaDebugCanvas3D & canvas3D ) const;

    protected:
        virtual string                                      UIPanelGetDisplayName( ) const override                 { return "Asset Importer"; }
        virtual void                                        UIPanelTick( vaApplicationBase & application ) override;
        virtual void                                        UIPanelTickAlways( vaApplicationBase & application ) override;

    public:
        static bool                                         LoadFileContents( const string & path, ImporterContext & parameters );

    };


}