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

#include "Rendering/vaShader.h"
#include "Rendering/vaRenderBuffers.h"

namespace Vanilla
{
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // TODO: these should simply become device-owned objects instead of singletons
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class vaDebugCanvas2D : public vaSingletonBase<vaDebugCanvas2D> // maybe better to use vaMultitonBase?
    {
    protected:
        // Types
        struct CanvasVertex2D
        {
            vaVector4   pos;
            uint32      color;
            vaVector2   uv0;
            vaVector2   uv1;
            vaVector2   screenPos;

            CanvasVertex2D( float x, float y, float z, float w, uint32 color, float sx, float sy ) : pos( x, y, z, w ), color( color ), uv0( 0.0f, 0.0f ), uv1( 0.0f, 0.0f ), screenPos( sx, sy ) { }
            CanvasVertex2D( float x, float y, float z, uint32 color, const vaMatrix4x4 * viewProj, float sx, float sy )
                : color( color ), uv0( 0.0f, 0.0f ), uv1( 0.0f, 0.0f ), screenPos( sx, sy ), pos( vaVector4::Transform( vaVector4( x, y, z, 1.0f ), *viewProj ) )   { }

            CanvasVertex2D( int screenWidth, int screenHeight, const vaVector2 & screenPos, uint32 color, const vaVector2 & uv0 = vaVector2( 0.0f, 0.0f ), const vaVector2 & uv1 = vaVector2( 0.0f, 0.0f ) )
            {
                this->pos.x = ( screenPos.x /*+ 0.5f*/ ) / screenWidth * 2.0f - 1.0f;
                this->pos.y = 1.0f - ( screenPos.y /*+ 0.5f*/ ) / screenHeight * 2.0f;
                this->pos.z = 0.5f;
                this->pos.w = 1.0f;
                this->color = color;
                this->uv0 = uv0;
                this->uv1 = uv1;
                this->screenPos = screenPos;
            }
        };
        //
        struct DrawRectangleItem
        {
            float x, y, width, height;
            uint32 color;

            DrawRectangleItem( )	{}
            DrawRectangleItem( float x, float y, float width, float height, uint32 color ) : x( x ), y( y ), width( width ), height( height ), color( color ) {}
        };
        //
        struct DrawTextItem
        {
            float          x, y;
            unsigned int   penColor;
            unsigned int   shadowColor;
            std::string     text;
            DrawTextItem( float x, float y, unsigned int penColor, unsigned int shadowColor, const char * text )
                : x( x ), y( y ), penColor( penColor ), shadowColor( shadowColor ), text( text ) {}
        };
        //
        struct DrawLineItem
        {
            float x0;
            float y0;
            float x1;
            float y1;
            unsigned int penColor;

            DrawLineItem( float x0, float y0, float x1, float y1, unsigned int penColor )
                : x0( x0 ), y0( y0 ), x1( x1 ), y1( y1 ), penColor( penColor ) { }
        };

    protected:
        // buffers for queued render calls
        std::vector<DrawTextItem>           m_drawTextLines;
        std::vector<DrawLineItem>           m_drawLines;
        std::vector<DrawRectangleItem>      m_drawRectangles;

        // GPU buffers
        shared_ptr<vaDynamicVertexBuffer>   m_vertexBuffer; // type is CanvasVertex2D 
        uint32				                m_vertexBufferCurrentlyUsed;
        static const uint32				    m_vertexBufferSize                  = 256 * 1024;

        // shaders
        vaAutoRMI<vaPixelShader>            m_pixelShader;
        vaAutoRMI<vaVertexShader>           m_vertexShader;

        mutable std::mutex                  m_mutex;

    public:
        vaDebugCanvas2D( const vaRenderingModuleParams & params );
        virtual ~vaDebugCanvas2D( );
        //
        vaDebugCanvas2D( const vaDebugCanvas2D & ) = delete;
        void operator = ( const vaDebugCanvas2D & )    = delete; 
        //
        void                 CleanQueued( );
        void                 Render( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, bool bJustClearData = false );
        //
        auto &              Mutex( )    { return m_mutex; }
        //
    public:
        // 'DrawText' name is messy because thats #defined somewhere in windows.h or related headers :/
        virtual void        DrawText( float x, float y, const char * text, ... );
        virtual void        DrawText( float x, float y, unsigned int penColor, const char * text, ... );
        virtual void        DrawText( float x, float y, unsigned int penColor, unsigned int shadowColor, const char * text, ... );
        //virtual void        GetTextWidth( const char * text );
        //virtual void        GetTextWidth( const wchar_t * text );
        virtual void        DrawLine( float x0, float y0, float x1, float y1, unsigned int penColor );
        virtual void        DrawLineArrowhead( float x0, float y0, float x1, float y1, float arrowHeadSize, unsigned int penColor );
        virtual void        DrawRectangle( float x0, float y0, float width, float height, unsigned int penColor );
        virtual void        DrawCircle( float x, float y, float radius, unsigned int penColor, float tess = 0.6f );
        virtual void        FillRectangle( float x0, float y0, float width, float height, unsigned int brushColor );
        //
        inline void         DrawLine( const vaVector2 & a, vaVector2 & b, unsigned int penColor )                                                   { DrawLine( a.x, a.y, b.x, b.y, penColor ); }
        inline void         DrawLineArrowhead( const vaVector2 & a, vaVector2 & b, float arrowHeadSize, unsigned int penColor )                     { DrawLineArrowhead( a.x, a.y, b.x, b.y, arrowHeadSize, penColor ); }
        inline void         DrawCircle( const vaVector2 & a, float radius, unsigned int penColor, float tess = 0.6f )                               { DrawCircle( a.x, a.y, radius, penColor, tess ); }
        virtual void        DrawRectangle( const vaVector2 & topLeft, const vaVector2 & bottomRight, unsigned int penColor )                        { DrawRectangle( topLeft.x, topLeft.y, bottomRight.x-topLeft.x, bottomRight.y-topLeft.y, penColor ); }
    };

    class vaDebugCanvas3D : public vaSingletonBase<vaDebugCanvas3D> // maybe better to use vaMultitonBase?
    {
     protected:
        // Types
        struct CanvasVertex3D
        {
            vaVector4   pos;
            vaVector3   normal  = {0,0,0};
            uint32      color;

            CanvasVertex3D( float x, float y, float z, float w, uint32 color /*, float sx, float sy*/ ) : pos( x, y, z, w ), color( color ) { } //, uv0( 0.0f, 0.0f ), uv1( 0.0f, 0.0f ), screenPos( sx, sy ) { }
            CanvasVertex3D( float x, float y, float z, uint32 color, const vaMatrix4x4 * viewProj )// , float sx, float sy )
                : color( color ) //, uv0( 0.0f, 0.0f ), uv1( 0.0f, 0.0f ), screenPos( sx, sy )
            {
                pos = vaVector4::Transform( vaVector4( x, y, z, 1.0f ), *viewProj );
            }

            CanvasVertex3D( const vaVector3 & vec, uint32 color, const vaMatrix4x4 * viewProj ) //, float sx, float sy )
                : color( color ) //, uv0( 0.0f, 0.0f ), uv1( 0.0f, 0.0f ), screenPos( sx, sy )
            {
                pos = vaVector4::Transform( vaVector4( vec.x, vec.y, vec.z, 1.0f ), *viewProj );
            }

            CanvasVertex3D( const vaVector4 & vec, uint32 color )
                : color( color ), pos( vec )
            {
            }

            CanvasVertex3D( int screenWidth, int screenHeight, const vaVector2 & screenPos, uint32 color ) //, const vaVector2 & uv0 = vaVector2( 0.0f, 0.0f ), const vaVector2 & uv1 = vaVector2( 0.0f, 0.0f ) )
            {
                this->pos.x = ( screenPos.x /*+ 0.5f*/ ) / screenWidth * 2.0f - 1.0f;
                this->pos.y = 1.0f - ( screenPos.y /*+ 0.5f*/ ) / screenHeight * 2.0f;
                this->pos.z = 0.5f;
                this->pos.w = 1.0f;
                this->color = color;
                //this->uv0         = uv0;
                //this->uv1         = uv1;
                //this->screenPos   = screenPos;
            }
        };
        //
        enum DrawItemType
        {
            Triangle,
            Box,
            Sphere,
            //SphereCone,
        };
        //
        struct DrawItem
        {
            vaVector3         v0;
            vaVector3         v1;
            vaVector3         v2;
            unsigned int      penColor;
            unsigned int      brushColor;
            DrawItemType      type;
            int               transformIndex;

            DrawItem( const vaVector3 &v0, const vaVector3 &v1, const vaVector3 &v2, unsigned int penColor, unsigned int brushColor, DrawItemType type, int transformIndex = -1 )
                : v0( v0 ), v1( v1 ), v2( v2 ), penColor( penColor ), brushColor( brushColor ), type( type ), transformIndex( transformIndex ) { }
        };
        struct DrawLineItem
        {
            vaVector3         v0;
            vaVector3         v1;
            unsigned int      penColor0;
            unsigned int      penColor1;

            DrawLineItem( const vaVector3 &v0, const vaVector3 &v1, unsigned int penColor0, unsigned int penColor1 ) : v0( v0 ), v1( v1 ), penColor0( penColor0 ), penColor1( penColor1 ) { }
        };
        struct DrawTriangleTransformed
        {
            CanvasVertex3D    v0;
            CanvasVertex3D    v1;
            CanvasVertex3D    v2;

            DrawTriangleTransformed( const CanvasVertex3D& _v0, const CanvasVertex3D& _v1, const CanvasVertex3D& _v2, const vaVector3 & normal ) : v0( _v0 ), v1( _v1 ), v2( _v2 ) { v0.normal = normal; v1.normal = normal; v2.normal = normal; }
        };
        struct DrawLineTransformed
        {
            CanvasVertex3D    v0;
            CanvasVertex3D    v1;

            DrawLineTransformed( const CanvasVertex3D &v0, const CanvasVertex3D &v1 ) : v0( v0 ), v1( v1 ) { }
        };      
        //
     protected:
        // 
        //const vaViewport &         m_viewport;
        //int                              m_width;
        //int                              m_height;

        // buffers for queued render calls
        std::vector<DrawItem>               m_drawItems;
        std::vector<vaMatrix4x4>            m_drawItemsTransforms;
        std::vector<DrawLineItem>           m_drawLines;
        //
        // buffers for transformed data ready for rendering
        std::vector<DrawLineTransformed>    m_drawLinesTransformed;
        std::vector<DrawTriangleTransformed> m_drawTrianglesTransformed;
        //
        // GPU buffers
        shared_ptr<vaDynamicVertexBuffer>   m_triVertexBuffer;                      // type is CanvasVertex3D
        uint32				                m_triVertexBufferStart;
        uint32				                m_triVertexBufferCurrentlyUsed;
        static const uint32				    m_triVertexBufferSizeInVerts = 1024 * 1024 * 2;
        shared_ptr<vaDynamicVertexBuffer>   m_lineVertexBuffer;
        uint32				                m_lineVertexBufferStart;
        uint32				                m_lineVertexBufferCurrentlyUsed;
        static const uint32                 m_lineVertexBufferSizeInVerts = 1024 * 1024 * 2;

        // shaders
        vaAutoRMI<vaPixelShader>            m_pixelShader;
        vaAutoRMI<vaVertexShader>           m_vertexShader;

        std::vector<vaVector3>              m_sphereVertices;
        std::vector<uint32>                 m_sphereIndices;

        vaCameraBase                        m_lastCamera; // this is purely for debugging purposes

        mutable std::mutex                  m_mutex;

    public:
        vaDebugCanvas3D( const vaRenderingModuleParams & params );
        virtual ~vaDebugCanvas3D( );
        //
        auto &                  Mutex( )    { return m_mutex; }
        //
    public:
        //
        virtual void            DrawLine( const vaVector3 & v0, const vaVector3 & v1, unsigned int penColor );
        virtual void            DrawAxis( const vaVector3 & v0, float size, const vaMatrix4x4 * transform = NULL, float alpha = 0.5f );

        virtual void            DrawBox( const vaVector3 & v0, const vaVector3 & v1, unsigned int penColor, unsigned int brushColor = 0, const vaMatrix4x4 * transform = NULL );
        virtual void            DrawTriangle( const vaVector3 & v0, const vaVector3 & v1, const vaVector3 & v2, unsigned int penColor, unsigned int brushColor = 0, const vaMatrix4x4 * transform = NULL );
        virtual void            DrawQuad( const vaVector3 & v0, const vaVector3 & v1, const vaVector3 & v2, const vaVector3 & v3, unsigned int penColor, unsigned int brushColor = 0, const vaMatrix4x4 * transform = NULL );
        virtual void            DrawSphere( const vaVector3 & center, float radius, unsigned int penColor, unsigned int brushColor = 0 );
        virtual void            DrawCylinder( const vaVector3 & centerFrom, const vaVector3 & centerTo, float radiusFrom, float radiusTo, unsigned int penColor, unsigned int brushColor = 0, const vaMatrix4x4 * transform = NULL );
        void                    DrawArrow( const vaVector3 & centerFrom, const vaVector3 & centerTo, float radius, unsigned int penColor, unsigned int lineBrushColor, unsigned int arrowBrushColor, const vaMatrix4x4 * transform = NULL );
        void                    DrawSphereCone( const vaVector3 & center, const vaVector3 & direction, float radius, float angle, unsigned int penColor, unsigned int brushColor );
        //
        void                    DrawBox( const vaBoundingBox & aabb, unsigned int penColor, unsigned int brushColor = 0, const vaMatrix4x4 * transform = NULL  );
        void                    DrawBox( const vaOrientedBoundingBox & obb, unsigned int penColor, unsigned int brushColor = 0 );
        void                    DrawSphere( const vaBoundingSphere & bs, unsigned int penColor, unsigned int brushColor = 0 );
        void                    DrawPlane( const vaPlane & plane, unsigned int brushColor, float extents = 1e5f );
        //
        void                    DrawLightViz( const vaVector3 & center, const vaVector3 & direction, float size, float range, float coneInnerAngle, float coneOuterAngle, const vaVector3 & color );
        //
        virtual void            CleanQueued( );
        virtual void            Render( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaCameraBase & camera, bool bJustClearData = false );
        //
        const vaCameraBase &    GetLastCamera( ) const { return m_lastCamera; } // this is purely for debugging purposes
        //
    private:
        void RenderTriangle( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaCameraBase & camera, const CanvasVertex3D & a, const CanvasVertex3D & b, const CanvasVertex3D & c );
        void FlushTriangles( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaCameraBase & camera );

        void RenderLine( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaCameraBase & camera, const CanvasVertex3D & a, const CanvasVertex3D & b );
        void RenderLineBatch( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaCameraBase & camera, DrawLineTransformed * itemFrom, size_t count );
        void RenderTrianglesBatch( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaCameraBase & camera, DrawTriangleTransformed * itemFrom, size_t count );
        void FlushLines( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaCameraBase & camera );

        void InternalDrawTriangle( const CanvasVertex3D & v0, const CanvasVertex3D & v1, const CanvasVertex3D & v2, const vaVector3 & normal )
        {
            m_drawTrianglesTransformed.push_back( DrawTriangleTransformed( v0, v1, v2, normal ) );
        }
        void InternalDrawLine( const CanvasVertex3D & v0, const CanvasVertex3D & v1 )
        {
            m_drawLinesTransformed.push_back( DrawLineTransformed( v0, v1 ) );
        }
    };

    //////////////////////////////////////////////////////////////////////////
    // Inline
    //////////////////////////////////////////////////////////////////////////

    inline void vaDebugCanvas2D::DrawLineArrowhead( float x0, float y0, float x1, float y1, float arrowHeadSize, unsigned int penColor )
    {
        vaVector2 lineDir = vaVector2( x1 - x0, y1 - y0 );
        float lengthSq = lineDir.LengthSq();
        if( lengthSq < VA_EPSf )
            return;

        lineDir /= vaMath::Sqrt( lengthSq );
        vaVector2 lineDirOrt = vaVector2( lineDir.y, -lineDir.x );

        vaVector2 ptFrom = vaVector2( x1, y1 );
        vaVector2 ptA   = ptFrom - lineDir * arrowHeadSize + lineDirOrt * arrowHeadSize;
        vaVector2 ptB   = ptFrom - lineDir * arrowHeadSize - lineDirOrt * arrowHeadSize;

        ptFrom += lineDir * 1.5f;

        DrawLine( ptFrom, ptA, penColor );
        DrawLine( ptFrom, ptB, penColor );
    }

    inline void vaDebugCanvas3D::DrawSphere( const vaBoundingSphere & bs, unsigned int penColor, unsigned int brushColor )
    {
        DrawSphere( bs.Center, bs.Radius, penColor, brushColor );
    }

    inline void vaDebugCanvas3D::DrawBox( const vaBoundingBox & aabb, unsigned int penColor, unsigned int brushColor, const vaMatrix4x4 * transform )
    {
        DrawBox( aabb.Min, aabb.Max(), penColor, brushColor, transform );
    }

    inline void vaDebugCanvas3D::DrawBox( const vaOrientedBoundingBox & obb, unsigned int penColor, unsigned int brushColor )
    {
        vaBoundingBox bb;
        vaMatrix4x4 transform;
        obb.ToAABBAndTransform( bb, transform );
        DrawBox( bb.Min, bb.Max( ), penColor, brushColor, &transform );
    }

    // vaDebugCanvas3D
    inline void vaDebugCanvas3D::DrawLine( const vaVector3 & v0, const vaVector3 & v1, unsigned int penColor )
    {
        m_drawLines.push_back( DrawLineItem( v0, v1, penColor, penColor ) );
    }
    inline void vaDebugCanvas3D::DrawAxis( const vaVector3 & _v0, float size, const vaMatrix4x4 * transform, float alpha )
    {
        vaVector3 v0 = _v0;
        vaVector3 vX = v0 + vaVector3( size, 0.0f, 0.0f );
        vaVector3 vY = v0 + vaVector3( 0.0f, size, 0.0f );
        vaVector3 vZ = v0 + vaVector3( 0.0f, 0.0f, size );

        if( transform != NULL )
        {
            v0 = vaVector3::TransformCoord( v0, *transform );
            vX = vaVector3::TransformCoord( vX, *transform );
            vY = vaVector3::TransformCoord( vY, *transform );
            vZ = vaVector3::TransformCoord( vZ, *transform );
        }

        // X, red
        DrawLine( v0, vX, vaVector4::ToBGRA( vaVector4( 1.0f, 0.0f, 0.0f, alpha ) ) );
        // Y, green
        DrawLine( v0, vY, vaVector4::ToBGRA( vaVector4( 0.0f, 1.0f, 0.0f, alpha ) ) );
        // Z, blue
        DrawLine( v0, vZ, vaVector4::ToBGRA( vaVector4( 0.0f, 0.0f, 1.0f, alpha ) ) );
    }
    inline void vaDebugCanvas3D::DrawBox( const vaVector3 & v0, const vaVector3 & v1, unsigned int penColor, unsigned int brushColor, const vaMatrix4x4 * transform )
    {
        int transformIndex = -1;
        if( transform != NULL )
        {
            transformIndex = (int)m_drawItemsTransforms.size( );
            m_drawItemsTransforms.push_back( *transform );
        }
        m_drawItems.push_back( DrawItem( v0, v1, vaVector3( 0.0f, 0.0f, 0.0f ), penColor, brushColor, Box, transformIndex ) );
    }
    inline void vaDebugCanvas3D::DrawTriangle( const vaVector3 & v0, const vaVector3 & v1, const vaVector3 & v2, unsigned int penColor, unsigned int brushColor, const vaMatrix4x4 * transform )
    {
        int transformIndex = -1;
        if( transform != NULL )
        {
            transformIndex = (int)m_drawItemsTransforms.size( );
            m_drawItemsTransforms.push_back( *transform );
        }

        m_drawItems.push_back( DrawItem( v0, v1, v2, penColor, brushColor, Triangle, transformIndex ) );
    }
    //
    inline void vaDebugCanvas3D::DrawQuad( const vaVector3 & v0, const vaVector3 & v1, const vaVector3 & v2, const vaVector3 & v3, unsigned int penColor, unsigned int brushColor, const vaMatrix4x4 * transform )
    {
        int transformIndex = -1;
        if( transform != NULL )
        {
            transformIndex = (int)m_drawItemsTransforms.size( );
            m_drawItemsTransforms.push_back( *transform );
        }

        m_drawItems.push_back( DrawItem( v0, v1, v2, penColor, brushColor, Triangle, transformIndex ) );
        m_drawItems.push_back( DrawItem( v2, v1, v3, penColor, brushColor, Triangle, transformIndex ) );
    }

    inline void vaDebugCanvas3D::DrawSphere( const vaVector3 & center, float radius, unsigned int penColor, unsigned int brushColor )
    {
        m_drawItems.push_back( DrawItem( center, vaVector3( radius, 0.0f, 0.0f ), vaVector3( 0.0f, 0.0f, 0.0f ), penColor, brushColor, Sphere ) );
    }

    inline void vaDebugCanvas3D::DrawPlane( const vaPlane & plane, unsigned int brushColor, float extents )
    {
        vaVector3 target( 0.0f, 0.0f, 1.0f );
        float angle = vaVector3::AngleBetweenVectors( plane.Normal(), target );
        vaMatrix3x3 orientation; 
        if( angle < VA_EPSf )
            orientation = vaMatrix3x3::Identity;
        else
        {
            vaVector3 axis = vaVector3::Cross( target, plane.Normal() ).Normalized();
            orientation = vaMatrix3x3::RotationAxis( axis, angle );
        }

        vaOrientedBoundingBox oob( vaVector3( plane.Normal() ) * (-plane.d), vaVector3( extents, extents, 0.0f ), orientation );
        DrawBox( oob, 0x00000000, brushColor );
    }
    //
}