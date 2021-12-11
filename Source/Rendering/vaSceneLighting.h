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
#include "Core/vaUI.h"

#include "vaRendering.h"
#include "vaRenderInstanceList.h"

#include "vaIBL.h"

#include "Rendering/Shaders/Lighting/vaLightingShared.h"

#include "Scene/vaSceneAsync.h"

namespace Vanilla
{
    class vaShadowmap;
    class vaCubeShadowmap;

    // Lights description
    //  * Other than IBLs, everything is spherical-ish point-ish lights
    //    * There is a light Size parameter, introduced to avoid singularities. Distance attenuation is computed based
    //      on http://www.cemyuksel.com/research/pointlightattenuation/ (disk-like attenuation with no singularities)
    //    * There is a light Range parameter which is an additional non-pbr falloff used for performance purposes (see code).
    //

    // Lights tree description
    //
    //  * perfect binary tree (https://en.wikipedia.org/wiki/Binary_tree#Types_of_binary_trees) - size rounded up to 2^n
    //    * built bottom up after sorting based on Morton order
    //    * 


    struct LightSortMetadata
    {
        uint    MortonCode;
    };

    // Master lighting processor and storage; it's tied to vaScene w.r.t. inputs
    class vaSceneLighting : public vaRenderingModule, public vaUIPanel, public std::enable_shared_from_this<vaSceneLighting>
    {
    public:

    protected:
        string                                          m_debugInfo;

        shared_ptr<vaIBLProbe>                          m_localIBLProbe             = nullptr;
        Scene::IBLProbe                                 m_localIBLProbePendingData  = {};
        shared_ptr<vaIBLProbe>                          m_distantIBLProbe           = nullptr;
        Scene::IBLProbe                                 m_distantIBLProbePendingData= {};


        shared_ptr<vaTexture>                           m_AOTexture;

        std::vector<shared_ptr<vaShadowmap>>            m_shadowmaps;

        weak_ptr<vaShadowmap>                           m_UI_SelectedShadow;

        bool                                            m_shadowmapTexturesCreated  = false;
        static const int                                m_shadowCubeMapCount        = ShaderLightingConstants::MaxShadowCubes;    // total supported at one time
        const int                                       m_shadowCubeResolution      = 2048; // one face resolution
        //vaResourceFormat                                m_shadowCubeDepthFormat     = vaResourceFormat::D16_UNORM;  // D32_FLOAT for more precision
        //vaResourceFormat                                m_shadowCubeFormat          = vaResourceFormat::R16_UNORM;  // R16_FLOAT not supported for compare sampler!!! not sure about R32_FLOAT?
        shared_ptr<vaTexture>                           m_shadowCubeArrayTexture;
        weak_ptr<vaShadowmap>                           m_shadowCubeArrayCurrentUsers[m_shadowCubeMapCount];
        //shared_ptr<vaTexture>                           m_shadowCubeDepthTexture;

        float                                           m_shadowCubeDepthBiasScale  = 1.4f; // 1.2f;
        float                                           m_shadowCubeFilterKernelSize= 1.8f; // 1.5f;

        shared_ptr<vaConstantBuffer>                    m_constantBuffer;

        //////////////////////////////////////////////////////////////////////////
        // Stuff that gets collected from the scene - these get reset and updated from scratch every frame
        Scene::FogSphere                                m_collectedFogSphere        = {};
        vaVector3                                       m_collectedAmbientLightIntensity = {0,0,0};
        //
        // Lights with shadow maps need attached entities for continued tracking
        std::vector<entt::entity>                       m_collectedPointLightEntities;
        std::vector<ShaderLightPoint>                   m_collectedPointLights;
        std::vector<ShaderLightPoint>                   m_sortedPointLights;                // <- maybe replace m_collectedPointLights with this once sorted
        std::vector<LightSortMetadata>                  m_collectedPointLightsMetadata;     // <- maybe put m_collectedPointLightEntities here too?
        std::vector<uint32>                             m_collectedPointLightsSortIndices;
        //
        // tree
        std::vector<ShaderLightTreeNode>                m_lightTree;
        int                                             m_lightTreeDepth            = -1;
        int                                             m_lightTreeBottomLevelSize  = 0;
        int                                             m_lightTreeBottomLevelOffset= 0;
        //
        bool                                            m_debugVizLTEnable          = false;
        bool                                            m_debugVizLTTextEnable      = false;
        int                                             m_debugVizLTHighlightLevel  = -1;
        bool                                            m_debugVizLTTraversalTest   = false;
        vaVector3                                       m_debugVizLTTraversalRefPt  = {FLT_MAX,FLT_MAX,FLT_MAX};
        int                                             m_debugVizLTTraversalCount  = 128;
        uint32                                          m_debugVizLTTraversalSeed   = 0;
        //
        shared_ptr<vaRenderBuffer>                      m_pointLightBuffer;
        shared_ptr<vaRenderBuffer>                      m_lightTreeBuffer;
        bool                                            m_renderBuffersDirty        = true;    // set when these CPU side buffers change
        //////////////////////////////////////////////////////////////////////////

        vaVector3                                       m_worldBase             = { 0, 0, 0 };  // see vaDrawAttributes::GlobalSettings
        shared_ptr<vaScene>                             m_scene                 = nullptr;
        std::vector<shared_ptr<vaSceneAsync::WorkNode>> m_asyncWorkNodes;

    protected:
        vaSceneLighting( const vaRenderingModuleParams & params );
    public:
        virtual ~vaSceneLighting( );

    private:
        friend class vaSceneLightingDX11;

    protected:
        void                                            UpdateShaderConstants( vaRenderDeviceContext & renderContext, const vaDrawAttributes & drawAttributes );
        //const shared_ptr<vaConstantBuffer> &            GetConstantsBuffer( ) const                                                             { return m_constantBuffer.GetBuffer(); };
        //const shared_ptr<vaTexture> &                   GetEnvmapTexture( ) const                                                               { return m_envmapTexture; }
        //const shared_ptr<vaTexture> &                   GetShadowCubeArrayTexture( ) const                                                      { return m_shadowCubeArrayTexture; }

        // This is where the vaScene says "I'm starting my tick, what data do you need"
    public:
        void                                            SetWorldBase( const vaVector3 & worldBase )                                             { m_worldBase = worldBase; }
        void                                            UpdateFromScene( vaScene & scene, float deltaTime, int64 tickCounter );

    public:
        void                                            UpdateAndSetToGlobals( vaRenderDeviceContext & renderContext, vaShaderItemGlobals & shaderItemGlobals, const vaDrawAttributes & drawAttributes );

    public:
        void                                            SetLocalIBLData( const Scene::IBLProbe & localIBLData )                                 { m_localIBLProbePendingData    = localIBLData; }
        void                                            SetDistantIBLData( const Scene::IBLProbe & distantIBLData )                             { m_distantIBLProbePendingData  = distantIBLData; }
        
        const shared_ptr<vaIBLProbe> &                  GetDistantIBLProbe( )                                                                   { return m_distantIBLProbe; }

        void                                            SetAOMap( const shared_ptr<vaTexture> & texture )                                       { m_AOTexture = texture; }
        const shared_ptr<vaTexture> &                   GetAOMap( ) const                                                                       { return m_AOTexture; }

        // call vaShadowmap::SetUpToDate() to make 'fresh' - 'fresh' ones will not get returned by this function, and if there's no dirty ones left it will return nullptr
        shared_ptr<vaShadowmap>                         GetNextHighestPriorityShadowmapForRendering( );
        std::pair<shared_ptr<vaIBLProbe>, Scene::IBLProbe>
                                                        GetNextHighestPriorityIBLProbeForRendering( );

        // returns true if lighting is not in a 'steady state' (has GetNextHighestPriorityXXX). This is temporary until we get to fully dynamic updates.
        bool                                            HasPendingVisualDependencies( );

        void                                            SetScene( const shared_ptr<class vaScene> & scene );

        int                                             GetLastLightCount( ) const                                                              { return (int)m_collectedPointLights.size(); }

        void                                            Reset( );
        // vaVector4                                       GetShadowCubeViewspaceDepthOffsets( ) const                                             { return vaVector4( m_shadowCubeFlatOffsetAdd / (float)m_shadowCubeResolution, m_shadowCubeFlatOffsetScale / (float)m_shadowCubeResolution, m_shadowCubeSlopeOffsetAdd / (float)m_shadowCubeResolution, m_shadowCubeSlopeOffsetScale / (float)m_shadowCubeResolution ); }

    protected:
        void                                            DestroyShadowmapTextures( );
        void                                            CreateShadowmapTextures( );

        shared_ptr<vaCubeShadowmap>                     FindShadowmapForPointLight( entt::entity entity );
        shared_ptr<vaShadowmap>                         FindShadowmapForDirectionalLight( entt::entity entity );

    protected:
        // virtual void                                    UpdateResourcesIfNeeded( vaDrawAttributes & drawAttributes );
        // virtual void                                    ApplyDirectionalAmbientLighting( vaDrawAttributes & drawAttributes, vaGBuffer & GBuffer ) = 0;
        // virtual void                                    ApplyDynamicLighting( vaDrawAttributes & drawAttributes, vaGBuffer & GBuffer ) = 0;

    public:
        // could add a 'deallocate' if vaShadowmap wants to detach for any reason, but not sure that's needed - they get destroyed anyway when not needed and that removes them from this list
        bool                                            AllocateShadowStorageTextureIndex( const shared_ptr<vaShadowmap> & shadowmap, int & outTextureIndex, shared_ptr<vaTexture> & outTextureArray );
        bool                                            AllocateShadowStorage( const shared_ptr<vaCubeShadowmap> & shadowmap, int & outTextureIndex, shared_ptr<vaTexture> & outTextureArray );

        // // if using cubemap shadows this is one depth buffer used for rendering all of them
        // const shared_ptr<vaTexture> &                   GetCubemapDepthTexture( )                                              { return m_shadowCubeDepthTexture; }

    private:
        virtual void                                    UIPanelTick( vaApplicationBase & application ) override;

    protected:
        friend struct MainWorkNode;
        struct MainWorkNode : vaSceneAsync::WorkNode
        {
            vaSceneLighting &                       Lighting;
            vaScene &                               Scene;
            entt::basic_view< entt::entity, entt::exclude_t<>, const Scene::WorldBounds>
                BoundsView;

            ShaderInstanceConstants *               UploadConstants = nullptr;
            struct vaRenderInstance *               InstanceArray   = nullptr;
            std::atomic_uint32_t                    InstanceCounter = 0;
            uint32                                  MaxInstances    = 0;
            int64                                   ApplicationTickIndex = -1;
            float                                   DeltaTime       = 0.0f;
            MainWorkNode( vaSceneLighting & lighting, vaScene & scene );
            virtual void                    ExecutePrologue( float deltaTime, int64 applicationTickIndex ) override;
            virtual std::pair<uint, uint>   ExecuteNarrow( const uint32 pass, vaSceneAsync::ConcurrencyContext & ) override;
            virtual void                    ExecuteWide( const uint32 pass, const uint32 itemBegin, const uint32 itemEnd, vaSceneAsync::ConcurrencyContext & ) override;
        };
    };

    class vaShadowmap : public vaRenderingModule, public vaUIPanel, public vaUIPropertiesItem, public std::enable_shared_from_this<vaShadowmap>
    {
    protected:
        vaSceneLighting &                                    m_lightingSystem;

        const weak_ptr<vaScene>                         m_scene;
        const entt::entity                              m_entity                = entt::null;   // this is the entity from which the light was taken
        const string                                    m_entityName            = "";

        int                                             m_storageTextureIndex   = -1;  // index to vaSceneLighting's texture storage index (into cubemap array for point lights, csm array for directional, etc.)

        // if covering dynamic objects then use age to determine which shadow map to update first; for static-only shadows there's no need to update except if light parameters changed
        bool                                            m_inUse                 = false;
        bool                                            m_includeDynamicObjects = false;
        float                                           m_dataDirtyTime         = VA_FLOAT_HIGHEST;

    protected:
        vaShadowmap( ) = delete;
        vaShadowmap( vaRenderDevice & device, vaSceneLighting & lightingSystem, const weak_ptr<vaScene> & scene, entt::entity entity );

    public:
        virtual ~vaShadowmap( ) { }

    public:
        const vaSceneLighting &                         GetLighting( ) const                                { return m_lightingSystem; }
        entt::entity                                    GetEntity( ) const                                  { return m_entity; }
        int                                             GetStorageTextureIndex( ) const                     { return m_storageTextureIndex; }
        const float                                     GetDataAge( ) const                                 { return m_dataDirtyTime; }

        bool &                                          InUse( )                                            { return m_inUse; }

        static shared_ptr<vaShadowmap>                  CreateDirectional( vaRenderDevice & device, vaSceneLighting & lightingSystem, const weak_ptr<vaScene> & scene, entt::entity entity );
        static shared_ptr<vaCubeShadowmap>              CreatePoint( vaRenderDevice & device, vaSceneLighting & lightingSystem, const weak_ptr<vaScene> & scene, entt::entity entity );

        virtual void                                    Tick( float deltaTime );

        virtual void                                    Invalidate( )                                       { m_dataDirtyTime = VA_FLOAT_HIGHEST; }
        void                                            SetUpToDate( )                                      { m_dataDirtyTime = 0; }
        void                                            SetIncludeDynamicObjects( bool includeDynamic )     { m_includeDynamicObjects = includeDynamic; }

        // create draw filter
        virtual void                                    SetToRenderSelectionFilter( vaRenderInstanceList::FilterSettings & filter ) const = 0 ;
        // draw
        virtual vaDrawResultFlags                       Draw( vaRenderDeviceContext & renderContext, vaRenderInstanceList & renderSelection ) = 0;

        virtual string                                  UIPropertiesItemGetDisplayName( ) const override    { return UIPanelGetDisplayName(); }
        virtual void                                    UIPropertiesItemTick( vaApplicationBase &, bool , bool ) override                    { assert( false ); } //return UIPanelTick(); }
    };

    // for point/spot lights
    class vaCubeShadowmap : public vaShadowmap
    {
    private:
        std::shared_ptr<vaTexture>                      m_cubemapArraySRV;      // array SRV into the big cube texture pointing to the beginning and of size of 6
        //std::shared_ptr<vaTexture>                      m_cubemapArrayRTVs[6];  // RTVs each pointing at the beginning + n (n goes from 0 to 5)
        std::shared_ptr<vaTexture>                      m_cubemapSliceDSVs[6];  // temp DSVs used to render the cubemap

        vaVector3                                       m_lightCenter;
        float                                           m_lightRadius;
        float                                           m_lightRange;

    public:
        vaCubeShadowmap( ) = delete;
        vaCubeShadowmap( vaRenderDevice & device, vaSceneLighting & lightingSystem, const weak_ptr<vaScene> & scene, entt::entity entity );

    public:
        virtual ~vaCubeShadowmap( ) { }

    protected:
        virtual void                                    SetToRenderSelectionFilter( vaRenderInstanceList::FilterSettings & filter ) const;
        virtual vaDrawResultFlags                       Draw( vaRenderDeviceContext & renderContext, vaRenderInstanceList & renderSelection );
        virtual void                                    Tick( float deltaTime );

        virtual void                                    Invalidate( )                                       { vaShadowmap::Invalidate(); m_lightCenter = {0,0,0}; m_lightRadius = {0}; m_lightRange = {0}; }

    public:
        void                                            Tick( float deltaTime, const ShaderLightPoint & lightPoint );

    protected:
        virtual string                                  UIPanelGetDisplayName( ) const override             { return vaStringTools::Format( "Cubemap [%s]", m_entityName.c_str() ); }
        virtual void                                    UIPanelTick( vaApplicationBase & application ) override;
    };
}