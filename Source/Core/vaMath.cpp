///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaGeometry.h"
#include "vaMath.h"

using namespace Vanilla;

void vaSimple2DNoiseA::Initialize( int seed )
{
    vaRandom        random;
    random.Seed( seed );

    m_kMaxVertices = 256;
    m_kMaxVerticesMask = m_kMaxVertices - 1;
    delete[] m_r;
    m_r = new float[m_kMaxVertices * m_kMaxVertices];

    for( unsigned i = 0; i < m_kMaxVertices * m_kMaxVertices; ++i )
    {
        m_r[i] = random.NextFloat( );
    }
}

void vaSimple2DNoiseA::Destroy( )
{
    //BX_FREE( bgfx::g_allocator, m_r );
    delete[] m_r;
}

/// Evaluate the noise function at position x
float vaSimple2DNoiseA::Eval( const vaVector2& pt ) const
{
    // forgot to call Initialize?
    assert( m_r != NULL );

    if( m_r == NULL )
        return 0.0f;

    int xi = (int)floor( pt.x );
    int yi = (int)floor( pt.y );

    float tx = pt.x - xi;
    float ty = pt.y - yi;

    int rx0 = xi & m_kMaxVerticesMask;
    int rx1 = ( rx0 + 1 ) & m_kMaxVerticesMask;
    int ry0 = yi & m_kMaxVerticesMask;
    int ry1 = ( ry0 + 1 ) & m_kMaxVerticesMask;


    /// Random values at the corners of the cell
    float c00 = m_r[ry0 * m_kMaxVertices + rx0];
    float c10 = m_r[ry0 * m_kMaxVertices + rx1];
    float c01 = m_r[ry1 * m_kMaxVertices + rx0];
    float c11 = m_r[ry1 * m_kMaxVertices + rx1];

    /// Remapping of tx and ty using the Smoothstep function
    float sx = vaMath::Smoothstep( tx );
    float sy = vaMath::Smoothstep( ty );

    /// Linearly interpolate values along the x axis
    float nx0 = vaMath::Lerp( c00, c10, sx );
    float nx1 = vaMath::Lerp( c01, c11, sx );

    /// Linearly interpolate the nx0/nx1 along they y axis
    return vaMath::Lerp( nx0, nx1, sy );
}


