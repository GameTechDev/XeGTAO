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

#include "Rendering/vaRendering.h"

namespace Vanilla
{
    class vaSimpleParticleSystem;
    class vaRenderMaterial;

    struct vaSimpleParticle
    {
        vaVector3               Position;
        //vaQuaternion            Orientation;

        vaVector3               Velocity;
        // vaQuaternion             AngularVelocity;

        // only screen space angle for now
        float                   Angle;
        float                   AngularVelocity;

        // float                   AnimationPos;
        // float                   AnimationVelocity;

        float                   AffectedByGravityK;
        float                   AffectedByWindK;
        //float                   DampingK;    // need damping!!

        vaVector4               Color;

        float                   LifeStart;
        float                   LifeRemaining;

        float                   Size;
        float                   SizeChange;

        friend class vaSimpleParticleSystem;
        uint32                  CreationID;
    };

    struct vaSimpleParticleEmitter
    {
        enum SpawnAreaTypeEnum
        {
            SAT_BoundingBox,
            SAT_BoundingSphere,
        };

        struct EmitterSettings
        {

            // Settings (maybe should go into a separate "Settings" structure?)
            union
            {
                struct  
                {
                    vaOrientedBoundingBox   SpawnAreaBoundingBox;
                };
                struct
                {
                    vaBoundingSphere        SpawnAreaBoundingSphere;
                };
            };
            SpawnAreaTypeEnum           SpawnAreaType;      
            float                       SpawnFrequencyPerSecond;

            float                       SpawnSize;
            float                       SpawnSizeRandomAddSub;

            float                       SpawnSizeChange;
            float                       SpawnSizeChangeRandomAddSub;

            // starting velocity is a SpawnVelocity + random * SpawnVelocityRandomAdd
            vaVector3                   SpawnVelocity;
            vaVector3                   SpawnVelocityRandomAddSub;

            // only screen space angle for now
            float                       SpawnAngle;
            float                       SpawnAngleRandomAddSub;

            float                       SpawnAngularVelocity;
            float                       SpawnAngularVelocityRandomAddSub;

            float                       SpawnAffectedByGravityK;
            float                       SpawnAffectedByWindK;

            // starting angular velocity is a random slerp between these two
            // vaQuaternion               SpawnAngularVelocityMin;  
            // vaQuaternion               SpawnAngularVelocityMax;

            //float                       AnimationPosMin;
            //float                       AnimationPosMax;
            //
            //float                       AnimationVelocityMax;
            //float                       AnimationVelocityMin;

            float                       SpawnLife;
            float                       SpawnLifeRandomAddSub;

            float                       SpawnOpacity;

            vaVector4                   SpawnColor;
            vaVector4                   SpawnColorRandomAddSub;


                                        EmitterSettings( )  { Reset(); }

            void                        Reset( )
            {
                SpawnAreaType = SAT_BoundingBox;
                SpawnAreaBoundingBox = vaOrientedBoundingBox( vaVector3( 0.0f, 0.0f, 0.0f ), vaVector3( 1.0f, 1.0f, 1.0f ), vaMatrix3x3::Identity );
                SpawnFrequencyPerSecond = 1.0f;

                SpawnSize = 1.0f;
                SpawnSizeRandomAddSub = 0.0f;

                SpawnSizeChange = 0.0f;
                SpawnSizeChangeRandomAddSub = 0.0f;

                SpawnAngle = 0.0f;
                SpawnAngleRandomAddSub = 0.0f;

                SpawnVelocity = vaVector3( 0.0f, 0.0f, 0.0f );
                SpawnVelocityRandomAddSub = vaVector3( 0.0f, 0.0f, 0.0f );

                SpawnAngularVelocity = 0.0f;
                SpawnAngularVelocityRandomAddSub = 0.0f;

                SpawnAffectedByGravityK = 0.0f;
                SpawnAffectedByWindK = 0.0f;

                // SpawnAngularVelocityMin;
                // SpawnAngularVelocityMax;

                SpawnLife = 1.0f;
                SpawnLifeRandomAddSub = 0.0f;

                SpawnColor = vaVector4( 0.5f, 0.5f, 0.5f, 0.5f );
                SpawnColorRandomAddSub = vaVector4( 0.0f, 0.0f, 0.0f, 0.0f );
            }
        };

        EmitterSettings             Settings;

        // stuff actively calculated by the vaSimpleParticleSystem - change at your peril!
        float                       TimeSinceStarted;
        float                       RemainingEmitterLife;
        int                         RemainingEmitterParticleCount;
        bool                        Active;
        bool                        Removed;        // once removed from vaSimpleParticleSystem there's no way to put it back in, but you can release it by calling 

        float                       TimeAccumulatedSinceLastSpawn;

        // TODO: change these to std::function
        // These delegates are same as function pointers; if using, make absolutely sure you don't leave them dangling
        // Warning: emitter shader is only allowed to push back new particles to allParticles, never remove or reorder them!
        // vaSimpleParticleSystem & psys, const std::shared_ptr<vaSimpleParticleEmitter> & emitter, float deltaTime
        vaEvent<void(vaSimpleParticleSystem &, vaSimpleParticleEmitter &, std::vector<vaSimpleParticle> &, float)>
                                    delegate_emitterTickShader;

        // set at creation
        std::wstring                Name;

        // set at creation
        uint32                      CreationID;

        // every time you create a particle, use this to set its ID and increment it
        uint32                      LastParticleID;

    private:
        //bool                        AutoRemove;     // automatically remove from vaSimpleParticleSystem on lifetime end; set to false if you're manually enabling/disabling/restarting the emitter!
        shared_ptr<int>             AliveToken      = std::make_shared<int>(42);

        friend vaSimpleParticleSystem;
                                    vaSimpleParticleEmitter( ) { Reset(); }

        void                        Reset( )
        {
            Settings.Reset();

            TimeSinceStarted                = 0.0f;
            RemainingEmitterLife            = std::numeric_limits<float>::max();
            RemainingEmitterParticleCount   = std::numeric_limits<int>::max();
            //AutoRemove                      = false;
            Active                          = true;
            Removed                         = false;
            Name                            = L"";
            CreationID                      = (uint32)-1;
            LastParticleID                  = (uint32)-1;

            TimeAccumulatedSinceLastSpawn   = 0.0f;

            delegate_emitterTickShader.AddWithToken( AliveToken, &vaSimpleParticleEmitter::DefaultEmitterTickShader );
        }


        public:
            static void             DefaultEmitterTickShader( vaSimpleParticleSystem & psys, vaSimpleParticleEmitter & emitter, std::vector<vaSimpleParticle> & allParticles, float deltaTime );

        
    };

    struct vaBillboardSprite
    {
        vaVector4   Position_CreationID;
        vaVector4   Transform2D;            // could be FP16 instead
        vaVector4   Color;                  // could be FP16 instead
    };

    class vaSimpleParticleSystem : public vaRenderingModule
    {
    public:

        struct Settings
        {
            // int                 MaxParticles            = 2 * 1024  * 1024;
            float               FadeAlphaFrom           = 0.8f;                 // when, in lifeRemaining/lifeStart, to start fading out alpha
            float               VelocityDamping         = 0.01f;
            float               AngularVelocityDamping  = 0.0f;
            vaVector3           Gravity                 = vaVector3( 0.0f, 0.0f, -9.81f );
            vaVector3           Wind                    = vaVector3( 0.0f, 0.0f, 0.0f );
        };

    protected:
        shared_ptr<vaRenderMaterial>                    m_defaultMaterial;

        vaBoundingBox                                   m_boundingBox;

        Settings                                        m_settings;

        //std::shared_ptr<vaTexture>                      m_texture;
        shared_ptr<vaRenderMaterial>                    m_material;

        std::vector<vaSimpleParticle>                   m_particles;
//        uint32                                          m_lastParticleID;
        uint32                                          m_lastEmitterID;

        // used for sorting by the default DefaultDrawBufferUpdateShader function. Obviously not thread safe.
        std::vector< float >                            m_particleSortValueCache;
        std::vector< int >                              m_particleSortedIndices;

        std::vector< std::shared_ptr<vaSimpleParticleEmitter> >
                                                        m_emitters;

        std::vector< std::shared_ptr<vaSimpleParticleEmitter> >
                                                        m_unusedEmittersPool;

        int                                             m_lastTickEmitterCount;
        int                                             m_lastTickParticleCount;

        bool                                            m_sortedAfterTick;

        int                                             m_lastTickID;

    protected:
        int                                             m_debugParticleDrawCountLimit;

        shared_ptr<int>                                 m_aliveToken      = std::make_shared<int>(42);

    protected:
        //////////////////////////////////////////////////////////////////////////
        // rendering stuff
        vaAutoRMI<vaVertexShader>               m_vertexShader;
        //vaAutoRMI<vaPixelShader>                m_pixelShader;
        //vaAutoRMI<vaPixelShader>                m_pixelShaderShadowed;
        //vaAutoRMI<vaGeometryShader>             m_geometryShader;
        //vaAutoRMI<vaGeometryShader>             m_geometryShaderGenerateVolumeShadow;     // for volumetric shadow - not maintained anymore
        //vaAutoRMI<vaPixelShader>                m_pixelShaderGenerateVolumeShadow;        // for volumetric shadow - not maintained anymore
        bool                                    m_shadersDirty                      = true;
        //       
        shared_ptr<vaConstantBuffer>            m_constantBuffer;
        //
        shared_ptr<vaDynamicVertexBuffer>       m_dynamicBuffer;
        static const int                        m_dynamicBufferMaxElementCount      = 2 * 1024 * 1024;
        int                                     m_dynamicBufferCurrentlyUsed        = 0;
        //
        std::vector< pair< string, string > >        m_staticShaderMacros           = {std::pair<std::string, std::string>( "INVALID", "INVALID" )};
        //std::vector< pair< string, string > >        m_staticShaderMacrosGen        = {std::pair<std::string, std::string>( "INVALID", "INVALID" )};  // for volumetric shadows - not maintained anymore
        //
        int                                     m_buffersLastUpdateTickID           = 0;
        //
        int                                     m_buffersLastCountToDraw            = 0;
        int                                     m_buffersLastOffsetInVertices       = 0;
        //////////////////////////////////////////////////////////////////////////
    public:
        // TODO: change these to std::function
        // These delegates are same as function pointers; if using, make absolutely sure you don't leave them dangling
        // Warning: particle shader is NOT allowed to push back new particles to allParticles, remove them or reorder them!
        // vaSimpleParticleSystem & psys, std::vector<vaSimpleParticle> & allParticles, float deltaTime
        vaEvent<void( vaSimpleParticleSystem &, std::vector<vaSimpleParticle> &, float )>
                                                        delegate_particlesTickShader;

        // TODO: change these to std::function
        // These delegates are same as function pointers; if using, make absolutely sure you don't leave them dangling
        // vaSimpleParticleSystem & psys, std::vector<vaSimpleParticle> & allParticles, const std::vector< int > & sortedIndices, void * outDestinationBuffer, size_t inDestinationBufferSize )
        vaEvent<void( const vaSimpleParticleSystem &, const std::vector<vaSimpleParticle> &, const std::vector< int > & , void *, size_t )>
                                                        delegate_drawBufferUpdateShader;

    public:
        vaSimpleParticleSystem( const vaRenderingModuleParams & params );
        virtual ~vaSimpleParticleSystem( )                                                          { }

        //void                                        SetShaderFilePath(      const wstring & shaderFilePath      )       { m_settings.ShaderFilePath                         = shaderFilePath; }
        //void                                        SetVertexShaderEntry(   const string & vertexShaderEntry    )       { m_settings.VertexShaderEntry                      = vertexShaderEntry; }
        //void                                        SetGeometryShaderEntry( const string & geometryShaderEntry  )       { m_settings.GeometryShaderEntry                    = geometryShaderEntry; }
        //void                                        SetPixelShaderEntry(    const string & pixelShaderEntry     )       { m_settings.PixelShaderEntry                       = pixelShaderEntry; }
        //void                                        SetVolumeShadowGeneratePixelShaderEntry( const string & entry )     { m_settings.VolumeShadowGeneratePixelShaderEntry   = entry; }
        //void                                        SetTextureFilePath( const wstring & textureFilePath )                   { m_settings.TextureFilePath     = textureFilePath; }
        Settings &                                  Settings( )                                                             { return m_settings; }

        // only way of creating a new emitter; reference held internally; if you release a reference and it is still active and producing particles, it will continue until the time/particle count runs out
        std::shared_ptr<vaSimpleParticleEmitter>    CreateEmitter( const std::wstring & name = L"unnamed", float startRemainingLife = std::numeric_limits<float>::max(), int startRemainingParticleCount = std::numeric_limits<int>::max() );
        
        // if you're done with it you can just let it go out of scope (but it will continue producing particles if active!), or you can release it this way
        void                                        SafeReleaseEmitter( std::shared_ptr<vaSimpleParticleEmitter> & emitter ){ if( emitter != nullptr ) { emitter->Active = false; emitter = nullptr; } }

        void                                        Tick( float deltaTime );
        void                                        Sort( const vaVector3 & cameraPos, bool backToFront );

        int                                         GetLastTickID( ) const                                                  { return m_lastTickID; }

        void                                        DrawDebugBoxes( );

        const std::vector<vaSimpleParticle> &       GetParticles( ) const                                                   { return m_particles; }
        const std::vector< int > &                  GetSortedIndices( ) const                                               { assert( m_sortedAfterTick ); return m_particleSortedIndices; }

        const std::shared_ptr<vaRenderMaterial> &   GetMaterial( ) const                                                    { return m_material; }
        void                                        SetMaterial( std::shared_ptr<vaRenderMaterial> & mat )                  { m_material = mat; }

        // every time you create a particle, use this to set its ID and increment it
        // uint32 &                                    LastParticleID( )                                                       { return m_lastParticleID; }
        // every time you create an emitter, use this to set its ID and increment it
        uint32 &                                    LastEmitterID( )                                                        { return m_lastEmitterID; }

        vaMatrix4x4                                 GetTransform( ) const                                                   { return vaMatrix4x4::Identity; }
        
        bool                                        IsSortedAfterTick( )                                                    { return m_sortedAfterTick; }

    private:
        void                                        DefaultParticlesTickShader( vaSimpleParticleSystem & psys, std::vector<vaSimpleParticle> & allParticles, float deltaTime );
        void                                        DefaultDrawBufferUpdateShader( const vaSimpleParticleSystem & psys, const std::vector<vaSimpleParticle> & allParticles, const std::vector< int > & sortedIndices, void * outDestinationBuffer, size_t inDestinationBufferSize );
        // at the moment does nothing but release the shared_ptr; in the future will be used to pool disposed emitters, to reduce dynamic allocation; there's no other need to call ReleaseEmitterPtr, you can safely just let the reference go out of scope
        void                                        ReleaseEmitterPtr( std::shared_ptr<vaSimpleParticleEmitter> & ptr );

    public:
        virtual vaDrawResultFlags                   Draw( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaDrawAttributes & drawAttributes, const shared_ptr<vaTexture> & viewspaceDepthSource, vaBlendMode blendMode, vaShadingRate shadingRate = vaShadingRate::ShadingRate1X1 );
    };

    inline std::shared_ptr<vaSimpleParticleEmitter>    vaSimpleParticleSystem::CreateEmitter( const std::wstring & name, float startRemainingLife, int startRemainingParticleCount )
    {
        std::shared_ptr<vaSimpleParticleEmitter> ret;
        if( m_unusedEmittersPool.size() == 0 )
            ret = std::shared_ptr<vaSimpleParticleEmitter>( new vaSimpleParticleEmitter( ) );
        else
        {
            ret = m_unusedEmittersPool.back();
            m_unusedEmittersPool.pop_back();
            ret->Reset( );
        }

        //        ret->AutoRemove                     = autoRemove;
        ret->RemainingEmitterLife           = startRemainingLife;
        ret->RemainingEmitterParticleCount  = startRemainingParticleCount;
        ret->Name                           = name;

        LastEmitterID( )++;
        ret->CreationID                     = LastEmitterID( );

        m_emitters.push_back( ret );

        return ret;
    }

}