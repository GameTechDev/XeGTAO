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
#include "Scene/vaCameraBase.h"
#include "Core/Misc/vaResourceFormats.h"
#include "Rendering/vaRenderDeviceContext.h"

namespace Vanilla
{

    class vaRenderCamera : public vaCameraBase, public vaUIPanel, public vaRenderingModule
    {
    public:
        struct ExposureSettings
        {
            float   Exposure                        = -10.0f;           // EV100 - see https://google.github.io/filament/Filament.html#imagingpipeline/physicallybasedcamera (start underexposed for no particular reason)
            float   ExposureCompensation            = 0.0f;             // added post-autoexposure - use for user exposure adjustment

            float   ExposureMin                     = -20.0f;           // [ -20.0, +20.0   ]   - danger, the UI expect these to be consecutive in memory 
            float   ExposureMax                     = 20.0f;            // [ -20.0, +20.0   ]   - danger, the UI expect these to be consecutive in memory 
            bool    UseAutoExposure                 = true;             // 
            float   AutoExposureAdaptationSpeed     = 15.0f;            // [   0.1, FLT_MAX ]   - use std::numeric_limits<float>::infinity() for instantenious; // 
            float   AutoExposureKeyValue            = 0.5f;             // [   0.0, 2.0     ]
            bool    UseAutoAutoExposureKeyValue     = true;             // 

            float   DefaultAvgLuminanceMinWhenDataNotAvailable = 0.03f;
            float   DefaultAvgLuminanceMaxWhenDataNotAvailable = 0.5f;

            float   HDRClamp                        = 64.0f;            // limit color values in the pre-exposed space - avoids feeding extremes into tonemapper, TAA and the rest of the pipe
        };
        struct TonemapSettings
        {
            //bool    UseTonemapping                  = true;           // Just pass-through the values (with pre-exposed lighting) instead; luminance still gets computed
            float   Saturation                      = 1.0f;             // [   0.0, 5.0     ]
            bool    UseModifiedReinhard             = true;             // 
            float   ModifiedReinhardWhiteLevel      = 6.0f;             // [   0.0, FLT_MAX ]
        };
        struct BloomSettings
        {
            bool    UseBloom                        = false;
            float   BloomSize                       = 0.3f;             // the gaussian blur sigma used by the filter is BloomSize scaled by resolution
            float   BloomMultiplier                 = 0.05f;
            float   BloomMinThreshold               = 0.01f;            // ignore values below min threshold                                    (will get scaled with pre-exposure multiplier)
            float   BloomMaxClamp                   = 10.0f;             // never transfer more than this amount of color to neighboring pixels  (will get scaled with pre-exposure multiplier)
        };
        struct DepthOfFieldSettings
        {
            bool    UseDOF                          = false;
        };
        struct LevelOfDetailDSettings
        {
            float   Multiplier                      = 1.0f;
        };

        struct AllSettings
        {
            ExposureSettings                        ExposureSettings;
            TonemapSettings                         TonemapSettings;
            BloomSettings                           BloomSettings;
            DepthOfFieldSettings                    DoFSettings;
            LevelOfDetailDSettings                  LODSettings;

            bool                                    EnablePostProcess       = true;     // If false, skips tonemapping, bloom and should skip DoF - outputs pre-tonemap values (with pre-exposed lighting); luminance still gets computed
        };

        AllSettings                                 m_settings;

        static const int                            c_backbufferCount = vaRenderDevice::c_BackbufferCount+1;          // also adds c_backbufferCount-1 lag!

    private:
        int                                         m_avgLuminancePrevLastWrittenIndex;
        shared_ptr<vaTexture>                       m_avgLuminancePrevCPU[c_backbufferCount];
        bool                                        m_avgLuminancePrevCPUHasData[c_backbufferCount];
        float                                       m_avgLuminancePrevCPUPreExposure[c_backbufferCount];
        float                                       m_lastAverageLuminance = 0.04f;

        bool const                                  m_visibleInUI;

    public:
        vaRenderCamera( vaRenderDevice & renderDevice, bool visibleInUI );
        virtual ~vaRenderCamera( );
        
        // copying not supported for now (although vaCameraBase should be able to get all info from vaRenderCamera somehow?)
        vaRenderCamera( const vaRenderCamera & other )                  = delete;
        vaRenderCamera & operator = ( const vaRenderCamera & other )    = delete;

        auto &                                      Settings()                      { return m_settings; }
        auto &                                      ExposureSettings()              { return m_settings.ExposureSettings; }
        auto &                                      TonemapSettings()               { return m_settings.TonemapSettings; }
        auto &                                      BloomSettings()                 { return m_settings.BloomSettings; }
        auto &                                      DepthOfFieldSettings()          { return m_settings.DoFSettings; }

        const auto &                                Settings() const                { return m_settings; }
        const auto &                                ExposureSettings() const        { return m_settings.ExposureSettings; }
        const auto &                                TonemapSettings() const         { return m_settings.TonemapSettings; }
        const auto &                                BloomSettings() const           { return m_settings.BloomSettings; }
        const auto &                                DepthOfFieldSettings() const    { return m_settings.DoFSettings; }

        // this gets called by tonemapping to provide the last luminance data as a GPU-based texture; if called multiple times
        // between PreRenderTick calls, only the last value will be used
        void                                        UpdateLuminance( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & inputLuminance );
        
        // this has to be called before starting any rendering to setup exposure and any related params; 
        // it also expects that vaCameraBase::Tick was called before to handle matrix updates and similar
        void                                        PreRenderTick( vaRenderDeviceContext & renderContext, float deltaTime, bool alwaysUseDefaultLuminance = false );

        // if determinism is required between changing of scenes or similar
        void                                        ResetHistory( );

        virtual float                               GetEV100( bool includeExposureCompensation ) const override;
        virtual float                               GetHDRClamp( ) const override   { return m_settings.ExposureSettings.HDRClamp; }

        virtual vaLODSettings                       GetLODSettings( ) const override;


    public:
    protected:
        // vaUIPanel
        // virtual string                              UIPanelGetDisplayName( ) const;
        virtual bool                                UIPanelIsListed( ) const { return m_visibleInUI; }
        virtual void                                UIPanelTick( vaApplicationBase & application ) override;
    };
    
}
