///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// !!! SCHEDULED FOR DELETION !!!

#pragma once

#include "Core/vaCoreIncludes.h"

#include "Rendering/vaRenderingIncludes.h"

namespace Vanilla
{
    /*
    class vaSimpleVolumeShadowMapPlugin
    {
    public:
        virtual ~vaSimpleVolumeShadowMapPlugin( ) { }

        virtual const std::vector< std::pair< std::string, std::string > > &
            GetShaderMacros( ) = 0;

        virtual void                        StartGenerating( vaDrawAttributes & context, vaSimpleShadowMap * ssm )   = 0;
        virtual void                        StopGenerating( vaDrawAttributes & context, vaSimpleShadowMap * ssm )    = 0;
        virtual void                        StartUsing( vaDrawAttributes & context, vaSimpleShadowMap * ssm )        = 0;
        virtual void                        StopUsing( vaDrawAttributes & context, vaSimpleShadowMap * ssm )         = 0;
        
        virtual void                        SetDebugTexelLocation( int x, int y )                                   = 0;
        virtual int                         GetResolution( )                                                        = 0;
        virtual bool                        GetSupported( )                                                         = 0;    // supported on current hardware?
    };

    class vaSimpleShadowMapAPIInternalCallbacks
    {
    public:
        virtual void                    InternalResolutionOrTexelWorldSizeChanged( )                                = 0;
        virtual void                    InternalStartGenerating( const vaDrawAttributes & context )                  = 0;
        virtual void                    InternalStopGenerating( const vaDrawAttributes & context )                   = 0;
        virtual void                    InternalStartUsing( const vaDrawAttributes & context )                       = 0;
        virtual void                    InternalStopUsing( const vaDrawAttributes & context )                        = 0;
    };

    // a very simple directional shadow map
    class vaSimpleShadowMap : public Vanilla::vaRenderingModule, protected vaSimpleShadowMapAPIInternalCallbacks
    {
    private:

        int                             m_resolution;
        vaOrientedBoundingBox           m_volume;

        std::shared_ptr<vaTexture>      m_shadowMap;

        vaMatrix4x4                     m_view;
        vaMatrix4x4                     m_proj;
        vaMatrix4x4                     m_viewProj;
        vaVector2                       m_texelSize;

        vaSimpleVolumeShadowMapPlugin * m_volumeShadowMapPlugin;

    protected:
        vaSimpleShadowMap( );

    public:
        ~vaSimpleShadowMap( );

    public:
        void                                Initialize( int resolution );

        void                                SetVolumeShadowMapPlugin( vaSimpleVolumeShadowMapPlugin * vsmp )                            { m_volumeShadowMapPlugin = vsmp; }
        vaSimpleVolumeShadowMapPlugin *     GetVolumeShadowMapPlugin( ) const                                                           { return m_volumeShadowMapPlugin; }

        void                                UpdateArea( const vaOrientedBoundingBox & volume );

        const std::shared_ptr<vaTexture> &  GetShadowMapTexture( ) const                        { return m_shadowMap; }
        const vaMatrix4x4 &                 GetViewMatrix( ) const                              { return m_view;     }
        const vaMatrix4x4 &                 GetProjMatrix( ) const                              { return m_proj;     }
        const vaMatrix4x4 &                 GetViewProjMatrix( ) const                          { return m_viewProj; }

        int                                 GetResolution( ) const                              { return m_resolution; }
        const vaOrientedBoundingBox &       GetVolume( ) const                                  { return m_volume; }
        const vaVector2 &                   GetTexelSize( ) const                               { return m_texelSize; }

        void                                StartGenerating( vaDrawAttributes & context )        { InternalStartGenerating( context ); assert( context.SimpleShadowMap == NULL ); context.SimpleShadowMap = this; }
        void                                StopGenerating( vaDrawAttributes & context )         { InternalStopGenerating(  context ); assert( context.SimpleShadowMap == this ); context.SimpleShadowMap = NULL; }
        void                                StartUsing( vaDrawAttributes & context )             { InternalStartUsing(      context ); assert( context.SimpleShadowMap == NULL ); context.SimpleShadowMap = this; }
        void                                StopUsing( vaDrawAttributes & context )              { InternalStopUsing(       context ); assert( context.SimpleShadowMap == this ); context.SimpleShadowMap = NULL; }

    public:

    private:
        void                                SetResolution( int resolution );

    };
    */
}
