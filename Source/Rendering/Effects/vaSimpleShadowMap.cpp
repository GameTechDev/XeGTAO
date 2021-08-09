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

#include "Rendering/Effects/vaSimpleShadowMap.h"

using namespace Vanilla;

/*
vaSimpleShadowMap::vaSimpleShadowMap( )
{ 
    m_resolution = 0;

    m_view      = vaMatrix4x4::Identity;
    m_proj      = vaMatrix4x4::Identity;
    m_viewProj  = vaMatrix4x4::Identity;

    m_texelSize = vaVector2( 0.0f, 0.0f );
}

vaSimpleShadowMap::~vaSimpleShadowMap( )
{
}

void vaSimpleShadowMap::Initialize( int resolution )
{
    SetResolution( resolution );
}

void vaSimpleShadowMap::SetResolution( int resolution )
{
    if( m_resolution != resolution )
    {
        m_resolution = resolution;

        m_shadowMap = vaTexture::Create2D( vaResourceFormat::R16_TYPELESS, resolution, resolution, 1, 1, 1, vaResourceBindSupportFlags::DepthStencil | vaResourceBindSupportFlags::ShaderResource, vaResourceAccessFlags::Default, NULL, 0, vaResourceFormat::R16_UNORM, vaResourceFormat::Unknown, vaResourceFormat::D16_UNORM);

        InternalResolutionOrTexelWorldSizeChanged( );
    }
}

void vaSimpleShadowMap::UpdateArea( const vaOrientedBoundingBox & volume )
{
    m_volume = volume;

    m_view                  = vaMatrix4x4( volume.Axis.Transpose() );
    m_view.r3.x             = -vaVector3::Dot( volume.Axis.r0, volume.Center );
    m_view.r3.y             = -vaVector3::Dot( volume.Axis.r1, volume.Center );
    m_view.r3.z             = -vaVector3::Dot( volume.Axis.r2, volume.Center );

    m_view.r3.AsVec3()      += vaVector3( 0.0f, 0.0f, 1.0f ) * volume.Extents.z * 1.0f;

    m_proj                  = vaMatrix4x4::OrthoLH( volume.Extents.x*2.0f, volume.Extents.y*2.0f, 0.0f, volume.Extents.z * 2.0f );

    m_viewProj              = m_view * m_proj;

    vaVector2 newTexelSize;
    newTexelSize.x          = volume.Extents.x * 2.0f / (float)m_resolution;
    newTexelSize.y          = volume.Extents.y * 2.0f / (float)m_resolution;

    if( !vaVector2::NearEqual( newTexelSize, m_texelSize ) )
    {
        InternalResolutionOrTexelWorldSizeChanged( );
        m_texelSize         = newTexelSize;
    }
}
*/