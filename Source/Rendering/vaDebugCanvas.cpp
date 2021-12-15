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

#include "vaDebugCanvas.h"

#include "Rendering/vaRenderDeviceContext.h"

#include "IntegratedExternals/vaImguiIntegration.h"

using namespace Vanilla;

vaDebugCanvas2D::vaDebugCanvas2D( const vaRenderingModuleParams & params )
    :
      m_pixelShader( params ),
      m_vertexShader( params )
{
    std::vector<vaVertexInputElementDesc> inputElements;
    inputElements.push_back( { "SV_Position",   0,  vaResourceFormat::R32G32B32A32_FLOAT,    0, vaVertexInputElementDesc::AppendAlignedElement, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );
    inputElements.push_back( { "COLOR",         0,  vaResourceFormat::B8G8R8A8_UNORM,        0, vaVertexInputElementDesc::AppendAlignedElement, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );
    inputElements.push_back( { "TEXCOORD",      0,  vaResourceFormat::R32G32B32A32_FLOAT,    0, vaVertexInputElementDesc::AppendAlignedElement, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );
    inputElements.push_back( { "TEXCOORD",      1,  vaResourceFormat::R32G32_FLOAT,          0, vaVertexInputElementDesc::AppendAlignedElement, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );

    m_vertexShader->CompileVSAndILFromFile( "vaCanvas.hlsl", "VS_Canvas2D", inputElements, vaShaderMacroContaner(), false );
    m_pixelShader->CompileFromFile( "vaCanvas.hlsl", "PS_Canvas2D", vaShaderMacroContaner(), false );

    m_vertexBuffer = vaDynamicVertexBuffer::Create<CanvasVertex2D>( params.RenderDevice, m_vertexBufferSize, "Canvas2DBuffer", nullptr ),

    m_vertexBufferCurrentlyUsed = 0;
    //m_vertexBufferSize = 0;

    m_vertexBufferCurrentlyUsed = 0;
}

vaDebugCanvas2D::~vaDebugCanvas2D( )
{
}

// I guess vaStringTools::Format wasn't around when I wrote this?
#define vaDirectXCanvas2D_FORMAT_STR() \
   va_list args; \
   va_start(args, text); \
   int nBuf; \
   char szBuffer[16768]; \
   nBuf = _vsnprintf_s(szBuffer, _countof(szBuffer), _countof(szBuffer)-1, text, args); \
   assert(nBuf < sizeof(szBuffer)); \
   va_end(args); \
//
void vaDebugCanvas2D::DrawText( float x, float y, unsigned int penColor, unsigned int shadowColor, const char * text, ... )
{
    vaDirectXCanvas2D_FORMAT_STR( );
    m_drawTextLines.push_back( DrawTextItem( x, y, penColor, shadowColor, szBuffer ) );
}
//
void vaDebugCanvas2D::DrawText3D( const vaCameraBase & camera, const vaVector3 & position3D, const vaVector2 & screenOffset, unsigned int penColor, unsigned int shadowColor, const char * text, ... )
{
    vaDirectXCanvas2D_FORMAT_STR( );
    const vaVector4 worldPos = vaVector4(position3D,1);

    vaMatrix4x4 viewProj = camera.GetViewMatrix( ) * camera.GetProjMatrix( );
    vaVector4 pos = vaVector4::Transform(worldPos, viewProj); pos /= pos.w; pos.x = pos.x * 0.5f + 0.5f; pos.y = -pos.y * 0.5f + 0.5f;
    if( pos.z > 0 ) // don't draw if behind near clipping plane
    {
        pos.x *= camera.GetViewportWidth( ); pos.y *= camera.GetViewportHeight( );
        DrawText( pos.x + screenOffset.x, pos.y + screenOffset.y, penColor, shadowColor, szBuffer );
    }
}
//
void vaDebugCanvas2D::DrawLine( float x0, float y0, float x1, float y1, unsigned int penColor )
{
    m_drawLines.push_back( DrawLineItem( x0, y0, x1, y1, penColor ) );
}
//
void vaDebugCanvas2D::DrawRectangle( float x0, float y0, float width, float height, unsigned int penColor )
{
    DrawLine( x0 - 0.5f, y0, x0 + width, y0, penColor );
    DrawLine( x0 + width, y0, x0 + width, y0 + height, penColor );
    DrawLine( x0 + width, y0 + height, x0, y0 + height, penColor );
    DrawLine( x0, y0 + height, x0, y0, penColor );
}
//
void vaDebugCanvas2D::FillRectangle( float x0, float y0, float width, float height, unsigned int brushColor )
{
    m_drawRectangles.push_back( DrawRectangleItem( x0, y0, width, height, brushColor ) );
}
//
void vaDebugCanvas2D::DrawCircle( float x, float y, float radius, unsigned int penColor, float tess )
{
    tess = vaMath::Clamp( tess, 0.0f, 1.0f );

    float circumference = 2 * VA_PIf * radius;
    int steps = (int)( circumference / 4.0f * tess );
    steps = vaMath::Clamp( steps, 5, 32768 );

    float cxp = x + cos( 0 * 2 * VA_PIf ) * radius;
    float cyp = y + sin( 0 * 2 * VA_PIf ) * radius;

    for( int i = 1; i <= steps; i++ )
    {
        float p = i / (float)steps;
        float cx = x + cos( p * 2 * VA_PIf ) * radius;
        float cy = y + sin( p * 2 * VA_PIf ) * radius;

        DrawLine( cxp, cyp, cx, cy, penColor );

        cxp = cx;
        cyp = cy;
    }
}

void vaDebugCanvas2D::CleanQueued( )
{
    m_drawRectangles.clear( );
    m_drawLines.clear( );
    m_drawTextLines.clear( );
}

void vaDebugCanvas2D::Render( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, bool bJustClearData )
{
    //ID3D11DeviceContext * context = renderContext.SafeCast<vaRenderDeviceContextDX11*>( )->GetDXContext();
    int canvasWidth = renderOutputs.Viewport.Width;
    int canvasHeight = renderOutputs.Viewport.Height;

    // Fill shapes first
    if( !bJustClearData )
    {
        uint32 rectsDrawn = 0;
        while( rectsDrawn < m_drawRectangles.size( ) )
        {
            if( ( m_vertexBufferCurrentlyUsed + 6 ) >= m_vertexBufferSize )
            {
                m_vertexBufferCurrentlyUsed = 0;
            }

            vaResourceMapType mapType = ( m_vertexBufferCurrentlyUsed == 0 ) ? ( vaResourceMapType::WriteDiscard ) : ( vaResourceMapType::WriteNoOverwrite );
            if( m_vertexBuffer->Map( mapType ) )
            {
                CanvasVertex2D * vertices = m_vertexBuffer->GetMappedData<CanvasVertex2D>( );
                int drawFromVertex = m_vertexBufferCurrentlyUsed;

                while( ( rectsDrawn < m_drawRectangles.size( ) ) && ( ( m_vertexBufferCurrentlyUsed + 6 ) < m_vertexBufferSize ) )
                {
                    const int index = m_vertexBufferCurrentlyUsed;

                    const DrawRectangleItem & rect = m_drawRectangles[rectsDrawn];

                    vertices[index + 0] = CanvasVertex2D( canvasWidth, canvasHeight, vaVector2( rect.x, rect.y ), rect.color );
                    vertices[index + 1] = CanvasVertex2D( canvasWidth, canvasHeight, vaVector2( rect.x + rect.width, rect.y ), rect.color );
                    vertices[index + 2] = CanvasVertex2D( canvasWidth, canvasHeight, vaVector2( rect.x, rect.y + rect.height ), rect.color );
                    vertices[index + 3] = CanvasVertex2D( canvasWidth, canvasHeight, vaVector2( rect.x, rect.y + rect.height ), rect.color );
                    vertices[index + 4] = CanvasVertex2D( canvasWidth, canvasHeight, vaVector2( rect.x + rect.width, rect.y ), rect.color );
                    vertices[index + 5] = CanvasVertex2D( canvasWidth, canvasHeight, vaVector2( rect.x + rect.width, rect.y + rect.height ), rect.color );

                    m_vertexBufferCurrentlyUsed += 6;
                    rectsDrawn++;
                }
                int drawVertexCount = m_vertexBufferCurrentlyUsed - drawFromVertex;
                m_vertexBuffer->Unmap( );

                vaGraphicsItem renderItem;

                renderItem.CullMode     = vaFaceCull::None;
                renderItem.BlendMode    = vaBlendMode::AlphaBlend;
                renderItem.VertexShader = m_vertexShader;
                renderItem.VertexBuffer = m_vertexBuffer;
                renderItem.Topology     = vaPrimitiveTopology::TriangleList;
                renderItem.PixelShader  = m_pixelShader;
                renderItem.SetDrawSimple( drawVertexCount, drawFromVertex );

                renderContext.ExecuteSingleItem( renderItem, renderOutputs, nullptr );
            }
            else
            {
                assert( false );
            }
        }
    }

    // Lines
    if( !bJustClearData )
    {
        uint32 linesDrawn = 0;
        while( linesDrawn < m_drawLines.size( ) )
        {
            if( ( m_vertexBufferCurrentlyUsed + 2 ) >= m_vertexBufferSize )
            {
                m_vertexBufferCurrentlyUsed = 0;
            }

            vaResourceMapType mapType = ( m_vertexBufferCurrentlyUsed == 0 ) ? ( vaResourceMapType::WriteDiscard ) : ( vaResourceMapType::WriteNoOverwrite );
            if( m_vertexBuffer->Map( mapType ) )
            {
                CanvasVertex2D * vertices = m_vertexBuffer->GetMappedData<CanvasVertex2D>( );
                int drawFromVertex = m_vertexBufferCurrentlyUsed;

                while( ( linesDrawn < m_drawLines.size( ) ) && ( ( m_vertexBufferCurrentlyUsed + 2 ) < m_vertexBufferSize ) )
                {
                    const int index = m_vertexBufferCurrentlyUsed;

                    vertices[index + 0] = CanvasVertex2D( canvasWidth, canvasHeight, vaVector2( m_drawLines[linesDrawn].x0, m_drawLines[linesDrawn].y0 ), m_drawLines[linesDrawn].penColor );
                    vertices[index + 1] = CanvasVertex2D( canvasWidth, canvasHeight, vaVector2( m_drawLines[linesDrawn].x1, m_drawLines[linesDrawn].y1 ), m_drawLines[linesDrawn].penColor );

                    m_vertexBufferCurrentlyUsed += 2;
                    linesDrawn++;
                }
                int drawVertexCount = m_vertexBufferCurrentlyUsed - drawFromVertex;
                m_vertexBuffer->Unmap( );

                vaGraphicsItem renderItem;

                renderItem.CullMode     = vaFaceCull::None;
                renderItem.BlendMode    = vaBlendMode::AlphaBlend;
                renderItem.VertexShader = m_vertexShader;
                renderItem.VertexBuffer = m_vertexBuffer;
                renderItem.Topology     = vaPrimitiveTopology::LineList;
                renderItem.PixelShader  = m_pixelShader;
                renderItem.SetDrawSimple( drawVertexCount, drawFromVertex );

                renderContext.ExecuteSingleItem( renderItem, renderOutputs, nullptr );
            }
            else
            {
                assert( false );
            }
        }
    }

    // Text
    if( !bJustClearData && m_drawTextLines.size( ) > 0 )
    {

        // Farming this off to ImGui :)
        ImGuiWindowFlags window_flags = 0;
        window_flags |= ImGuiWindowFlags_NoTitleBar             ;
        window_flags |= ImGuiWindowFlags_NoResize               ;
        window_flags |= ImGuiWindowFlags_NoMove                 ;
        window_flags |= ImGuiWindowFlags_NoScrollbar            ;
        window_flags |= ImGuiWindowFlags_NoScrollWithMouse      ;
        window_flags |= ImGuiWindowFlags_NoCollapse             ;
        window_flags |= ImGuiWindowFlags_NoBackground           ;
        window_flags |= ImGuiWindowFlags_NoSavedSettings        ;
        window_flags |= ImGuiWindowFlags_NoMouseInputs          ;
        window_flags |= ImGuiWindowFlags_NoFocusOnAppearing     ;
        window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus  ;
        window_flags |= ImGuiWindowFlags_NoNavInputs            ;
        window_flags |= ImGuiWindowFlags_NoNavFocus             ;
        window_flags |= ImGuiWindowFlags_NoDocking              ;
        bool open = true;
        ImGui::SetNextWindowBgAlpha(0);
        ImGui::SetNextWindowPos( {0,0}, ImGuiCond_Always );
        ImGui::SetNextWindowSize( {(float)canvasWidth,(float)canvasHeight}, ImGuiCond_Always );
        if( ImGui::Begin( "HiddenTextDrawWindow", &open, window_flags ) )
        {
            for( size_t i = 0; i < m_drawTextLines.size( ); i++ )
            {
                DrawTextItem & item = m_drawTextLines[i];

                if( item.shadowColor != 0 )
                {
                    ImU32 color = vaVector4::FromBGRA( item.shadowColor ).ToRGBA( );
                    ImGui::GetWindowDrawList( )->AddText( ImGui::GetFont( ), ImGui::GetFontSize( ) * 1.0f, ImVec2( item.x+1, item.y+1 ), color, item.text.c_str( ), nullptr, 0.0f, nullptr );
                }

                ImU32 color = vaVector4::FromBGRA(item.penColor).ToRGBA();
                ImGui::GetWindowDrawList()->AddText( ImGui::GetFont(), ImGui::GetFontSize()*1.0f, ImVec2(item.x, item.y), color, item.text.c_str(), nullptr, 0.0f, nullptr );
            }
        }
        ImGui::End();

        // m_font.Begin( );
        // m_font.SetInsertionPos( 5, 5 );
        // m_font.SetForegroundColor( 0xFFFFFFFF );
        // 
        // for( size_t i = 0; i < m_drawTextLines.size( ); i++ )
        // {
        //     if( ( m_drawTextLines[i].shadowColor & 0xFF000000 ) == 0 ) continue;
        // 
        //     m_font.SetInsertionPos( m_drawTextLines[i].x + 1, m_drawTextLines[i].y + 1 );
        //     m_font.SetForegroundColor( m_drawTextLines[i].shadowColor );
        //     m_font.DrawTextLine( m_drawTextLines[i].text.c_str( ) );
        // }
        // 
        // for( size_t i = 0; i < m_drawTextLines.size( ); i++ )
        // {
        //     m_font.SetInsertionPos( m_drawTextLines[i].x, m_drawTextLines[i].y );
        //     m_font.SetForegroundColor( m_drawTextLines[i].penColor );
        //     m_font.DrawTextLine( m_drawTextLines[i].text.c_str( ) );
        // }
        // 
        // m_font.End( );
    }

    CleanQueued( );
}

/*

      //struct Circle2D
      //{
      //   float fX, fY, fRadiusFrom, fRadiusTo;
      //   u_int uColour;

      //   Circle2D( )		{}
      //   Circle2D( float fX, float fY, float fRadiusFrom, float fRadiusTo, u_int uColour ) : fX(fX), fY(fY), fRadiusFrom(fRadiusFrom), fRadiusTo(fRadiusTo), uColour(uColour) {}
      //};

      //struct PolyLinePoint2D
      //{
      //   float fX, fY;
      //   float fThickness;
      //   u_int uColour;

      //   PolyLinePoint2D( )		{}
      //   PolyLinePoint2D( float fX, float fY, float fThickness, u_int uColour ) : fX(fX), fY(fY), fThickness(fThickness), uColour(uColour) { }
      //};

      //static const u_int														g_nItemBufferSize = 4096;

      //static Collection_Vector<Direct3D_2DRenderer::Circle2D>		g_xCircles;
      //static Collection_Vector<Direct3D_2DRenderer::Line2D>		g_xLines;

      //static Direct3D_2DRenderer::SlightlyLessSimpleVertex								g_xDrawVertices[g_nItemBufferSize*6];

*/

/*


void Direct3D_2DRenderer::Flush( )
{
   IDirect3DSurface9* pSurf = NULL;
   Direct3D::D3DDevice->GetRenderTarget( 0, &pSurf );

   D3DSURFACE_DESC xDesc;
   pSurf->GetDesc( &xDesc );

   SAFE_RELEASE( pSurf );

   Direct3D::D3DDevice->SetRenderState( D3DRS_SRGBWRITEENABLE, FALSE );

   Render::CurrentStates.RequestZBufferEnabled(false);
   Render::CurrentStates.RequestZBufferWriteEnabled(false);
   Render::CurrentStates.RequestCullMode( _CULLMODE_NONE );
   Render::CurrentStates.RequestWireFrameMode( false );
   Render::CurrentStates.RequestTranslucencyMode( _TRANSLUCENCY_NORMAL );
   Render::CurrentStates.RequestZBufferEnabled(false);

   g_pxEffect->SetParameterByName( "g_xScreenSize", D3DXVECTOR4( (float)xDesc.Width, (float)xDesc.Height, 1.0f / (float)xDesc.Width, 1.0f / (float)xDesc.Height ) );

   Direct3D::D3DDevice->SetVertexDeclaration( g_pxVertDecl );

   if( g_xLines.GetSize() > 0 )
   {
      u_int uVertexCount = g_xLines.GetSize() * 2;
      for( u_int i = 0; i < g_xLines.GetSize(); i++ )
      {
         Line2D& xLine = g_xLines[i];
         SlightlyLessSimpleVertex* pVerts = &g_xDrawVertices[i*2];
         pVerts[0] = SlightlyLessSimpleVertex( Vector_2( xLine.fXFrom, xLine.fYFrom ), Vector_2( 0.0f, 0.0f ), Vector_2( 0.0f, 0.0f ), xLine.uColour );
         pVerts[1] = SlightlyLessSimpleVertex( Vector_2( xLine.fXTo, xLine.fYTo ), Vector_2( 0.0f, 0.0f ), Vector_2( 0.0f, 0.0f ), xLine.uColour );
      }

      g_pxEffect->Begin( false, 2 );
      g_pxEffect->RenderAllPrimitivePassesUp( D3DPT_LINELIST, uVertexCount/2, g_xDrawVertices, sizeof( SlightlyLessSimpleVertex ) );
      g_pxEffect->End( );
   }

   if( g_xCircles.GetSize() > 0 )
   {
      u_int uVertexCount = g_xCircles.GetSize() * 6;
      for( u_int i = 0; i < g_xCircles.GetSize(); i++ )
      {
         Circle2D& xCircle = g_xCircles[i];
         SlightlyLessSimpleVertex* pVerts = &g_xDrawVertices[i*6];
         pVerts[0] = SlightlyLessSimpleVertex( Vector_2( xCircle.fX - xCircle.fRadiusTo - 1.0f, xCircle.fY - xCircle.fRadiusTo - 1.0f ), Vector_2( xCircle.fX, xCircle.fY ), Vector_2( xCircle.fRadiusFrom, xCircle.fRadiusTo ), xCircle.uColour );
         pVerts[1] = SlightlyLessSimpleVertex( Vector_2( xCircle.fX + xCircle.fRadiusTo + 1.0f, xCircle.fY - xCircle.fRadiusTo - 1.0f ), Vector_2( xCircle.fX, xCircle.fY ), Vector_2( xCircle.fRadiusFrom, xCircle.fRadiusTo ), xCircle.uColour );
         pVerts[2] = SlightlyLessSimpleVertex( Vector_2( xCircle.fX - xCircle.fRadiusTo - 1.0f, xCircle.fY + xCircle.fRadiusTo + 1.0f ), Vector_2( xCircle.fX, xCircle.fY ), Vector_2( xCircle.fRadiusFrom, xCircle.fRadiusTo ), xCircle.uColour );
         pVerts[3] = SlightlyLessSimpleVertex( Vector_2( xCircle.fX - xCircle.fRadiusTo - 1.0f, xCircle.fY + xCircle.fRadiusTo + 1.0f ), Vector_2( xCircle.fX, xCircle.fY ), Vector_2( xCircle.fRadiusFrom, xCircle.fRadiusTo ), xCircle.uColour );
         pVerts[4] = SlightlyLessSimpleVertex( Vector_2( xCircle.fX + xCircle.fRadiusTo + 1.0f, xCircle.fY - xCircle.fRadiusTo - 1.0f ), Vector_2( xCircle.fX, xCircle.fY ), Vector_2( xCircle.fRadiusFrom, xCircle.fRadiusTo ), xCircle.uColour );
         pVerts[5] = SlightlyLessSimpleVertex( Vector_2( xCircle.fX + xCircle.fRadiusTo + 1.0f, xCircle.fY + xCircle.fRadiusTo + 1.0f ), Vector_2( xCircle.fX, xCircle.fY ), Vector_2( xCircle.fRadiusFrom, xCircle.fRadiusTo ), xCircle.uColour );
      }

      g_pxEffect->Begin( false, 0 );
      g_pxEffect->RenderAllPrimitivePassesUp( D3DPT_TRIANGLELIST, uVertexCount/3, g_xDrawVertices, sizeof( SlightlyLessSimpleVertex ) );
      g_pxEffect->End( );
   }

   g_xLines.Clear();
   g_xCircles.Clear();

   const bool bSRGB = Direct3D::UseGammaCorrection();
   Direct3D::D3DDevice->SetRenderState( D3DRS_SRGBWRITEENABLE, bSRGB );
}

void Direct3D_2DRenderer::DrawPolyline( PolyLinePoint2D* axPoints, int iPointCount )
{
   const int iMaxPolylinePointCount = g_nItemBufferSize-2;
   Assert( iPointCount < iMaxPolylinePointCount, "Direct3D_2DRenderer::DrawPolyline does not support as many points (will be clamped)" );
   iPointCount = Maths::Min( iPointCount, iMaxPolylinePointCount );

   IDirect3DSurface9* pSurf = NULL;
   Direct3D::D3DDevice->GetRenderTarget( 0, &pSurf );

   D3DSURFACE_DESC xDesc;
   pSurf->GetDesc( &xDesc );

   SAFE_RELEASE( pSurf );

   Direct3D::D3DDevice->SetRenderState( D3DRS_SRGBWRITEENABLE, FALSE );

   Render::CurrentStates.RequestZBufferEnabled(false);
   Render::CurrentStates.RequestZBufferWriteEnabled(false);
   Render::CurrentStates.RequestCullMode( _CULLMODE_NONE );
   Render::CurrentStates.RequestWireFrameMode( false );
   Render::CurrentStates.RequestTranslucencyMode( _TRANSLUCENCY_NORMAL );
   Render::CurrentStates.RequestZBufferEnabled(false);

   g_pxEffect->SetParameterByName( "g_xScreenSize", D3DXVECTOR4( (float)xDesc.Width, (float)xDesc.Height, 1.0f / (float)xDesc.Width, 1.0f / (float)xDesc.Height ) );

   Direct3D::D3DDevice->SetVertexDeclaration( g_pxVertDecl );

   u_int uVertexCount = (iPointCount-1) * 6;

   Vector_2 xDirPrev;
   Vector_2 xDirCurr;
   for( int i = -1; i < (iPointCount-1); i++ )
   {
      Vector_2 xDirNext;
      const PolyLinePoint2D& xPtNext		= axPoints[i+1];


      if( i < (iPointCount-2) )
      {
         const PolyLinePoint2D& xPtNextNext		= axPoints[i+2];
         xDirNext = Vector_2( xPtNextNext.fX, xPtNextNext.fY ) - Vector_2( xPtNext.fX, xPtNext.fY );
         xDirNext.Normalise();
      }

      if( i >= 0 )
      {
         const PolyLinePoint2D& xPtCurrent	= axPoints[i];

         float fThicknessIn	= xPtCurrent.fThickness;
         float fThicknessOut = xPtNext.fThickness;

         float fDotIn = xDirPrev * xDirCurr;
         float fDotOut = xDirCurr * xDirNext;

         float fInAngle	= Maths::ArcCosine( Maths::ClampToRange( fDotIn,	-0.9999f, 1.0f ) );
         float fOutAngle = Maths::ArcCosine( Maths::ClampToRange( fDotOut,	-0.9999f, 1.0f ) );
         float fInDist	= Maths::Tangent( fInAngle*0.5f );
         float fOutDist	= Maths::Tangent( fOutAngle*0.5f );

         Vector_2 xDirCurrLeft = Vector_2( +xDirCurr.y, -xDirCurr.x );

         Vector_2 xFrom( xPtCurrent.fX, xPtCurrent.fY );
         Vector_2 xTo( xPtNext.fX, xPtNext.fY );

         float fThicknessInMod = fThicknessIn * 0.5f + 1.0f;
         float fThicknessOutMod = fThicknessOut * 0.5f + 1.0f;

         fInDist		*= Maths::Sign( xDirCurrLeft * xDirPrev );
         fOutDist	*= Maths::Sign( xDirCurrLeft * xDirNext );


         Vector_2 xCFromLeft	= xFrom 		- xDirCurr * (fThicknessInMod * fInDist);
         Vector_2 xCFromRight	= xFrom 		+ xDirCurr * (fThicknessInMod * fInDist);
         Vector_2 xCToLeft		= xTo			- xDirCurr * (fThicknessOutMod * fOutDist);
         Vector_2 xCToRight	= xTo			+ xDirCurr * (fThicknessOutMod * fOutDist);
         Vector_2 xFromLeft	= xCFromLeft	+ xDirCurrLeft * fThicknessInMod;
         Vector_2 xFromRight	= xCFromRight	- xDirCurrLeft * fThicknessInMod;
         Vector_2 xToLeft		= xCToLeft		+ xDirCurrLeft * fThicknessOutMod;
         Vector_2 xToRight		= xCToRight		- xDirCurrLeft * fThicknessOutMod;

         SlightlyLessSimpleVertex* pVerts = &g_xDrawVertices[i*6];
         pVerts[0] = SlightlyLessSimpleVertex( xFromLeft,		xCFromLeft,		Vector_2( fThicknessIn * 0.5f, 0.0f ),	xPtCurrent.uColour );
         pVerts[1] = SlightlyLessSimpleVertex( xToLeft,			xCToLeft,		Vector_2( fThicknessOut * 0.5f, 0.0f ),	xPtNext.uColour );
         pVerts[2] = SlightlyLessSimpleVertex( xFromRight,		xCFromRight,	Vector_2( fThicknessIn * 0.5f, 0.0f ),	xPtCurrent.uColour );
         pVerts[3] = SlightlyLessSimpleVertex( xFromRight,		xCFromRight,	Vector_2( fThicknessIn * 0.5f, 0.0f ),	xPtCurrent.uColour );
         pVerts[4] = SlightlyLessSimpleVertex( xToLeft,			xCToLeft,		Vector_2( fThicknessOut * 0.5f, 0.0f ), 	xPtNext.uColour );
         pVerts[5] = SlightlyLessSimpleVertex( xToRight,			xCToRight,		Vector_2( fThicknessOut * 0.5f, 0.0f ), 	xPtNext.uColour );
      }
      else
      {
         xDirCurr = xDirNext;
      }

      xDirPrev = xDirCurr;
      xDirCurr = xDirNext;
   }

   g_pxEffect->Begin( false, 3 );
   g_pxEffect->RenderAllPrimitivePassesUp( D3DPT_TRIANGLELIST, uVertexCount/3, g_xDrawVertices, sizeof( SlightlyLessSimpleVertex ) );
   g_pxEffect->End( );

   const bool bSRGB = Direct3D::UseGammaCorrection();
   Direct3D::D3DDevice->SetRenderState( D3DRS_SRGBWRITEENABLE, bSRGB );
}

#endif

*/


/*
float4	g_xScreenSize;

void VShader( inout float4 xColour : COLOR, inout float4 xPos : Position, inout float4 xUV : TEXCOORD0, out float2 xOrigScreenPos : TEXCOORD1 )
{
   xOrigScreenPos = xPos.xy;

   xPos.xy *= g_xScreenSize.zw;
   xPos.xy *= float2( 2.0, -2.0 );
   xPos.xy += float2( -1.0, 1.0 );
}

void PShader_Circle( inout float4 xColour : COLOR, in float4 xUV : TEXCOORD0, in float2 xOrigScreenPos : TEXCOORD1 )
{
   float2 xDelta = xOrigScreenPos.xy - xUV.xy;
   float fDistSq = dot( xDelta, xDelta );
   float fRadius1 = xUV.z;
   float fRadius2 = xUV.w;

   if( !((fDistSq >= fRadius1*fRadius1) && (fDistSq < fRadius2*fRadius2)) )
      discard;
}

void PShader_Rectangle( inout float4 xColour : COLOR, in float4 xUV : TEXCOORD0, in float2 xOrigScreenPos : TEXCOORD1 )
{
}

void PShader_Line( inout float4 xColour : COLOR, in float4 xUV : TEXCOORD0, in float2 xOrigScreenPos : TEXCOORD1 )
{

}

void PShader_LineAA( inout float4 xColour : COLOR, in float4 xUV : TEXCOORD0, in float2 xOrigScreenPos : TEXCOORD1 )
{
   float2 xDist = xOrigScreenPos - xUV.xy;

   xColour.a *= saturate( xUV.z - length( xDist ) + 0.5 );
}

technique Circle
{
    pass p0
    {
      VertexShader	= compile vs_3_0 VShader();
      PixelShader		= compile ps_3_0 PShader_Circle();
    }
}

technique Rectangle
{
    pass p0
    {
      VertexShader	= compile vs_3_0 VShader();
      PixelShader		= compile ps_3_0 PShader_Rectangle();
    }
}

technique Line
{
    pass p0
    {
      VertexShader	= compile vs_3_0 VShader();
      PixelShader		= compile ps_3_0 PShader_Line();
    }
}

technique LineAA
{
    pass p0
    {
      VertexShader	= compile vs_3_0 VShader();
      PixelShader		= compile ps_3_0 PShader_LineAA();
    }
}

*/


#include "Rendering/vaStandardShapes.h"

using namespace Vanilla;

vaDebugCanvas3D::vaDebugCanvas3D( const vaRenderingModuleParams & params ) :
    m_vertexShader( params ),
    m_pixelShader( params )
{
    std::vector<vaVertexInputElementDesc> inputElements;
    inputElements.push_back( { "SV_Position",   0, vaResourceFormat::R32G32B32A32_FLOAT,    0, vaVertexInputElementDesc::AppendAlignedElement, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );
    inputElements.push_back( { "NORMAL",        0, vaResourceFormat::R32G32B32_FLOAT,       0, vaVertexInputElementDesc::AppendAlignedElement, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );
    inputElements.push_back( { "COLOR",         0, vaResourceFormat::B8G8R8A8_UNORM,        0, vaVertexInputElementDesc::AppendAlignedElement, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );

    m_vertexShader->CompileVSAndILFromFile( "vaCanvas.hlsl", "VS_Canvas3D", inputElements, vaShaderMacroContaner{}, false );
    m_pixelShader->CompileFromFile( "vaCanvas.hlsl", "PS_Canvas3D", vaShaderMacroContaner{}, false );

    m_triVertexBuffer   = vaDynamicVertexBuffer::Create<CanvasVertex3D>( params.RenderDevice, m_triVertexBufferSizeInVerts, "Canvas3DTriangleBuffer", nullptr ),
    m_lineVertexBuffer  = vaDynamicVertexBuffer::Create<CanvasVertex3D>( params.RenderDevice, m_lineVertexBufferSizeInVerts, "Canvas3DLineBuffer", nullptr ),

    m_triVertexBufferCurrentlyUsed = 0;
    m_triVertexBufferStart = 0;

    m_lineVertexBufferCurrentlyUsed = 0;
    m_lineVertexBufferStart = 0;

    vaStandardShapes::CreateSphere( m_sphereVertices, m_sphereIndices, 2, true );

    m_triVertexBufferCurrentlyUsed = 0;
    m_triVertexBufferStart = 0;
    m_lineVertexBufferCurrentlyUsed = 0;
    m_lineVertexBufferStart = 0;
}

vaDebugCanvas3D::~vaDebugCanvas3D( )
{
    //m_triVertexBuffer.Destroy( );
    m_triVertexBufferCurrentlyUsed = 0;
    //m_triVertexBufferSizeInVerts = 0;
    m_triVertexBufferStart = 0;

    //m_lineVertexBuffer.Destroy( );
    m_lineVertexBufferCurrentlyUsed = 0;
    //m_lineVertexBufferSizeInVerts = 0;
    m_lineVertexBufferStart = 0;
}


void vaDebugCanvas3D::DrawCylinder( const vaVector3 & centerFrom, const vaVector3 & centerTo, float radiusFrom, float radiusTo, unsigned int penColor, unsigned int brushColor, const vaMatrix4x4 * transform )
{
    int tessellation = 9;

    radiusFrom  = vaMath::Abs( radiusFrom );
    radiusTo    = vaMath::Abs( radiusTo );

    vaVector3 direction = centerTo - centerFrom;
    float length = direction.Length();
    direction /= length;

    const float angle_delta = VA_PIf * 2.0f / tessellation;

    float s     = 1.0f; // cos(angle == 0);
    float t     = 0.0f; // sin(angle == 0);
    float angle = 0.0f;

    vaVector3 basisX, basisY;
    vaVector3::ComputeOrthonormalBasis( direction, basisX, basisY );

    vaVector3 prevVF, prevVT;

    for( int i = 0; i <= tessellation; i++ )
    {
        s = ::cos( angle );
        t = ::sin( angle );
        angle += angle_delta;

        vaVector3 vf = centerFrom + basisX * radiusFrom * s + basisY * radiusFrom * t;
        vaVector3 vt = centerTo + basisX * radiusTo * s + basisY * radiusTo * t;

        if( i > 0 )
        {
            // from (bottom/top)
            DrawTriangle( centerFrom,   vf, prevVF, penColor, brushColor, transform );
            // to (bottom/top)
            DrawTriangle( centerTo,     vt, prevVT, penColor, brushColor, transform );
            // sides
            DrawTriangle( vf,   vt, prevVF,     penColor, brushColor, transform );
            DrawTriangle( prevVF, vt, prevVT,   penColor, brushColor, transform );
        }
        prevVF = vf;
        prevVT = vt;
    }
}

void vaDebugCanvas3D::DrawArrow( const vaVector3 & centerFrom, const vaVector3 & centerTo, float radius, unsigned int penColor, unsigned int lineBrushColor, unsigned int arrowBrushColor, const vaMatrix4x4 * transform )
{
    vaVector3 direction = centerTo - centerFrom;
    float length = direction.Length( );
    direction /= length;

    float arrowLength   = radius * 6;
    float arrowWidth    = radius * 3;

    // line 
    DrawCylinder( centerFrom, centerTo - direction * arrowLength, radius, radius, penColor, lineBrushColor, transform );

    // arrowhead
    DrawCylinder( centerTo - direction * arrowLength, centerTo, arrowWidth, 0.0f, penColor, arrowBrushColor, transform );
}

void vaDebugCanvas3D::DrawSphereCone( const vaVector3 & center, const vaVector3 & direction, float radius, float angle, unsigned int penColor, unsigned int brushColor )
{
    const int tessellation = 9;

    angle = vaMath::Clamp( angle, 0.0f, VA_PIf );
    if( angle == 0.0f )
        return;

    const float angleStepPolar      = VA_PIf / tessellation;
    const float angleStepAzimuth    = VA_PIf * 2.0f / tessellation;

    // direction not unit length?
    // assert( direction.IsUnit() );

    vaVector3 _prevRow[tessellation + 1];
    vaVector3 _currRow[tessellation + 1];
    vaVector3 * prevRow = _prevRow;
    vaVector3 * currRow = _currRow;
    for( int i = 0; i <= tessellation; i++ )
        prevRow[i] = vaVector3( center+direction*radius );

    vaVector3 basisX, basisY;
    vaVector3::ComputeOrthonormalBasis( direction, basisX, basisY );

    float currentAngle = 0.0f;  // 'polar' angle (from Z axis)
    while( currentAngle < angle )
    {
        currentAngle = std::min( currentAngle + angleStepPolar, angle );

        vaVector3 vertPrev;
        for( int i = 0; i <= tessellation; i++ )
        {
            vaVector3 polar;
            polar.x = radius * std::sin( currentAngle ) * std::cos( i * angleStepAzimuth );
            polar.y = radius * std::sin( currentAngle ) * std::sin( i * angleStepAzimuth );
            polar.z = radius * std::cos( currentAngle );
            currRow[i] = center + basisX * polar.x + basisY * polar.y + direction * polar.z;
        }

        for( int i = 1; i <= tessellation; i++ )
        {
            DrawTriangle( currRow[i], prevRow[i], currRow[i-1],     penColor, brushColor );
            DrawTriangle( currRow[i-1], prevRow[i], prevRow[i-1],   penColor, brushColor );
        }
        std::swap( prevRow, currRow );
    }

    if( currentAngle == VA_PIf )
        return;
    
    for( int i = 0; i <= tessellation; i++ )
        currRow[i] = vaVector3( center );
    
    for( int i = 1; i <= tessellation; i++ )
    {
        DrawTriangle( currRow[i], prevRow[i], currRow[i - 1], penColor, brushColor );
        DrawTriangle( currRow[i - 1], prevRow[i], prevRow[i - 1], penColor, brushColor );
    }
}

void vaDebugCanvas3D::DrawLightViz( const vaVector3 & center, const vaVector3 & direction, float radius, float range, float coneInnerAngle, float coneOuterAngle, const vaVector3 & color )
{
    assert( coneInnerAngle <= coneOuterAngle );
    assert( coneOuterAngle <= VA_PIf );

    bool isSpotlight = !( ( (coneInnerAngle == 0) && (coneOuterAngle == 0) ) || ( coneInnerAngle >= (VA_PI-VA_EPSf) ) );

    uint32 colorSolid = vaVector4::ToBGRA( vaVector4( vaVector3::LinearToSRGB( color ), 0.9f ) );
    uint32 colorTrans = vaVector4::ToBGRA( vaVector4( vaVector3::LinearToSRGB( color ), 0.05f ) );

    if( isSpotlight )
    {
        DrawSphereCone( center, direction, radius,  coneOuterAngle, 0,          colorSolid );
        DrawSphereCone( center, direction, range,   coneInnerAngle, 0x80FF0000, colorTrans );
        DrawSphereCone( center, direction, range,   coneOuterAngle, 0x8000FF00, colorTrans );

#if 0
        vaVector3 lightUp = vaVector3::Cross( vaVector3( 0.0f, 0.0f, 1.0f ), direction );
        if( lightUp.Length( ) < 1e-3f )
            lightUp = vaVector3::Cross( vaVector3( 0.0f, 1.0f, 0.0f ), direction );
        lightUp = lightUp.Normalized( );

        vaVector3 coneInner = vaVector3::TransformNormal( direction, vaMatrix3x3::RotationAxis( lightUp, SpotInnerAngle ) );
        vaVector3 coneOuter = vaVector3::TransformNormal( direction, vaMatrix3x3::RotationAxis( lightUp, SpotOuterAngle ) );

        float coneRadius = 0.1f + Size * 2.0f; //light.EffectiveRadius();

        const int lineCount = 50;
        for( int j = 0; j < lineCount; j++ )
        {
            float angle = j / (float)( lineCount - 1 ) * VA_PIf * 2.0f;
            vaVector3 coneInnerR = vaVector3::TransformNormal( coneInner, vaMatrix3x3::RotationAxis( direction, angle ) );
            vaVector3 coneOuterR = vaVector3::TransformNormal( coneOuter, vaMatrix3x3::RotationAxis( direction, angle ) );
            canvas3D.DrawLine( position, position + coneRadius * coneInnerR, 0xFFFFFF00 );
            canvas3D.DrawLine( position, position + coneRadius * coneOuterR, 0xFFFF0000 );
        }
#endif
    }
    else
    {
        DrawSphere( center, radius, 0, colorSolid );
        DrawSphere( center, range, colorTrans, 0 );
    }

}

void vaDebugCanvas3D::CleanQueued( )
{
    m_drawItems.clear( );
    m_drawItemsTransforms.clear( );
    m_drawLines.clear( );
    m_drawLinesTransformed.clear( );
    m_drawTrianglesTransformed.clear( );
}

void vaDebugCanvas3D::RenderLine( vaRenderDeviceContext& renderContext, const vaRenderOutputs& renderOutputs, const vaCameraBase& camera, const CanvasVertex3D& a, const CanvasVertex3D& b )
{
    if( ( m_lineVertexBufferCurrentlyUsed + 2 ) >= m_lineVertexBufferSizeInVerts )
    {
        FlushLines( renderContext, renderOutputs, camera );
        m_lineVertexBufferCurrentlyUsed = 0;
        m_lineVertexBufferStart = 0;
    }

    vaResourceMapType mapType = ( m_lineVertexBufferCurrentlyUsed == 0 ) ? ( vaResourceMapType::WriteDiscard ) : ( vaResourceMapType::WriteNoOverwrite );
    if( m_lineVertexBuffer->Map( mapType ) )
    {
        CanvasVertex3D * vertices = m_lineVertexBuffer->GetMappedData<CanvasVertex3D>( );
        vertices[m_lineVertexBufferCurrentlyUsed++] = a;
        vertices[m_lineVertexBufferCurrentlyUsed++] = b;
        m_lineVertexBuffer->Unmap( );
    }
}

void vaDebugCanvas3D::RenderLineBatch( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaCameraBase & camera, DrawLineTransformed * itemFrom, size_t count )
{
    if( ( m_lineVertexBufferCurrentlyUsed + count * 2 ) >= m_lineVertexBufferSizeInVerts )
    {
        FlushLines( renderContext, renderOutputs, camera );
        m_lineVertexBufferCurrentlyUsed = 0;
        m_lineVertexBufferStart = 0;
    }

    vaResourceMapType mapType = ( m_lineVertexBufferCurrentlyUsed == 0 ) ? ( vaResourceMapType::WriteDiscard ) : ( vaResourceMapType::WriteNoOverwrite );
    if( m_lineVertexBuffer->Map( mapType ) )
    {
        CanvasVertex3D * vertices = m_lineVertexBuffer->GetMappedData<CanvasVertex3D>( );
        for( size_t i = 0; i < count; i++ )
        {
            DrawLineTransformed & line = itemFrom[i];

            vertices[m_lineVertexBufferCurrentlyUsed++] = line.v0;
            vertices[m_lineVertexBufferCurrentlyUsed++] = line.v1;
        }
        m_lineVertexBuffer->Unmap( );
    }
}

void vaDebugCanvas3D::FlushLines( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaCameraBase & camera )
{
    int verticesToRender = m_lineVertexBufferCurrentlyUsed - m_lineVertexBufferStart;

    if( verticesToRender > 0 )
    {
        vaGraphicsItem renderItem;

        renderItem.DepthEnable      = true;
        renderItem.DepthWriteEnable = false;
        renderItem.DepthFunc        = ( camera.GetUseReversedZ() )?( vaComparisonFunc::GreaterEqual ):( vaComparisonFunc::LessEqual );
        renderItem.CullMode         = vaFaceCull::None;
        renderItem.BlendMode        = vaBlendMode::AlphaBlend;
        renderItem.VertexShader     = m_vertexShader;
        renderItem.VertexBuffer     = m_lineVertexBuffer;
        renderItem.Topology         = vaPrimitiveTopology::LineList;
        renderItem.PixelShader      = m_pixelShader;
        renderItem.SetDrawSimple( verticesToRender, m_lineVertexBufferStart );

        renderContext.ExecuteSingleItem( renderItem, renderOutputs, nullptr );
    }
    m_lineVertexBufferStart = m_lineVertexBufferCurrentlyUsed;
}

void vaDebugCanvas3D::RenderTriangle( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaCameraBase & camera, const CanvasVertex3D & a, const CanvasVertex3D & b, const CanvasVertex3D & c )
{
    if( ( m_triVertexBufferCurrentlyUsed + 3 ) >= m_triVertexBufferSizeInVerts )
    {
        FlushTriangles( renderContext, renderOutputs, camera );
        m_triVertexBufferCurrentlyUsed = 0;
        m_triVertexBufferStart = 0;
    }

    vaResourceMapType mapType = ( m_triVertexBufferCurrentlyUsed == 0 ) ? ( vaResourceMapType::WriteDiscard ) : ( vaResourceMapType::WriteNoOverwrite );
    if( m_triVertexBuffer->Map( mapType ) )
    {
        CanvasVertex3D * vertices = m_triVertexBuffer->GetMappedData<CanvasVertex3D>( );
        vertices[m_triVertexBufferCurrentlyUsed++] = a;
        vertices[m_triVertexBufferCurrentlyUsed++] = b;
        vertices[m_triVertexBufferCurrentlyUsed++] = c;
        m_triVertexBuffer->Unmap( );
    }
}

void vaDebugCanvas3D::RenderTrianglesBatch( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaCameraBase & camera, DrawTriangleTransformed * itemFrom, size_t count )
{
    if( ( m_triVertexBufferCurrentlyUsed + count * 3 ) >= m_triVertexBufferSizeInVerts )
    {
        FlushTriangles( renderContext, renderOutputs, camera );
        m_triVertexBufferCurrentlyUsed = 0;
        m_triVertexBufferStart = 0;
    }

    vaResourceMapType mapType = ( m_triVertexBufferCurrentlyUsed == 0 ) ? ( vaResourceMapType::WriteDiscard ) : ( vaResourceMapType::WriteNoOverwrite );
    if( m_triVertexBuffer->Map( mapType ) )
    {
        CanvasVertex3D * vertices = m_triVertexBuffer->GetMappedData<CanvasVertex3D>( );
        for( size_t i = 0; i < count; i++ )
        {
            DrawTriangleTransformed & triangle = itemFrom[i];

            vertices[m_triVertexBufferCurrentlyUsed++] = triangle.v0;
            vertices[m_triVertexBufferCurrentlyUsed++] = triangle.v1;
            vertices[m_triVertexBufferCurrentlyUsed++] = triangle.v2;
        }
        m_triVertexBuffer->Unmap( );
    }
}

void vaDebugCanvas3D::FlushTriangles( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaCameraBase & camera )
{
    int verticesToRender = m_triVertexBufferCurrentlyUsed - m_triVertexBufferStart;

    if( verticesToRender > 0 )
    {
        vaGraphicsItem renderItem;

        renderItem.DepthEnable      = true;
        renderItem.DepthWriteEnable = false;
        renderItem.DepthFunc        = ( camera.GetUseReversedZ() )?( vaComparisonFunc::GreaterEqual ):( vaComparisonFunc::LessEqual );
        renderItem.BlendMode        = vaBlendMode::AlphaBlend;
        renderItem.VertexShader     = m_vertexShader;
        renderItem.VertexBuffer     = m_triVertexBuffer;
        renderItem.Topology         = vaPrimitiveTopology::TriangleList;
        renderItem.PixelShader      = m_pixelShader;
        renderItem.SetDrawSimple( verticesToRender, m_triVertexBufferStart );

        // renderItem.CullMode         = vaFaceCull::Front;
        // renderContext.ExecuteSingleItem( renderItem, renderOutputs, nullptr );
        //renderItem.CullMode         = vaFaceCull::Back;
        renderItem.CullMode         = vaFaceCull::None;
        renderContext.ExecuteSingleItem( renderItem, renderOutputs, nullptr );
    }
    m_triVertexBufferStart = m_triVertexBufferCurrentlyUsed;
}

void vaDebugCanvas3D::Render( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaCameraBase & camera, bool bJustClearData )
{
    m_lastCamera = camera;
    vaMatrix4x4 viewProj = camera.GetViewMatrix( ) * camera.ComputeZOffsettedProjMatrix( );

    if( !bJustClearData )
    {
        vaMatrix4x4 tempMat;

        // first do triangles
        for( size_t i = 0; i < m_drawItems.size( ); i++ )
        {
            DrawItem & item = m_drawItems[i];

            // use viewProj by default
            const vaMatrix4x4 * trans = &viewProj;
            const vaMatrix4x4 * worldTrans = &vaMatrix4x4::Identity;

            // or if the object has its own transform matrix, 'add' it to the viewProj
            if( item.transformIndex != -1 )
            {
                //assert( false ); // this is broken; lines are different from triangles; add InternalDrawLine that accepts already transformed lines...
                vaMatrix4x4 &local = m_drawItemsTransforms[item.transformIndex];
                worldTrans = &local;
                tempMat = local * viewProj;
                trans = &tempMat;
            }

            if( item.type == Triangle )
            {
                CanvasVertex3D a0( item.v0, item.brushColor, trans );
                CanvasVertex3D a1( item.v1, item.brushColor, trans );
                CanvasVertex3D a2( item.v2, item.brushColor, trans );

                if( ( item.brushColor & 0xFF000000 ) != 0 )
                {
                    InternalDrawTriangle( a0, a1, a2, vaVector3::TransformNormal( vaVector3::TriangleNormal( item.v0, item.v1, item.v2, false ), *worldTrans ) );
                }

                if( ( item.penColor & 0xFF000000 ) != 0 )
                {
                    a0.color = item.penColor;
                    a1.color = item.penColor;
                    a2.color = item.penColor;

                    InternalDrawLine( a0, a1 );
                    InternalDrawLine( a1, a2 );
                    InternalDrawLine( a2, a0 );
                }
            }

            if( item.type == Box )
            {

                const vaVector3 & boxMin = item.v0;
                const vaVector3 & boxMax = item.v1;

                vaVector3 va0( boxMin.x, boxMin.y, boxMin.z );
                vaVector3 va1( boxMax.x, boxMin.y, boxMin.z );
                vaVector3 va2( boxMax.x, boxMax.y, boxMin.z );
                vaVector3 va3( boxMin.x, boxMax.y, boxMin.z );
                vaVector3 vb0( boxMin.x, boxMin.y, boxMax.z );
                vaVector3 vb1( boxMax.x, boxMin.y, boxMax.z );
                vaVector3 vb2( boxMax.x, boxMax.y, boxMax.z );
                vaVector3 vb3( boxMin.x, boxMax.y, boxMax.z );

                CanvasVertex3D a0( va0, item.brushColor, trans );
                CanvasVertex3D a1( va1, item.brushColor, trans );
                CanvasVertex3D a2( va2, item.brushColor, trans );
                CanvasVertex3D a3( va3, item.brushColor, trans );
                CanvasVertex3D b0( vb0, item.brushColor, trans );
                CanvasVertex3D b1( vb1, item.brushColor, trans );
                CanvasVertex3D b2( vb2, item.brushColor, trans );
                CanvasVertex3D b3( vb3, item.brushColor, trans );

                vaVector3 normXP = vaVector3::TransformNormal( vaVector3( 1, 0, 0 ), *worldTrans );
                vaVector3 normYP = vaVector3::TransformNormal( vaVector3( 0, 1, 0 ), *worldTrans );
                vaVector3 normZP = vaVector3::TransformNormal( vaVector3( 0, 0, 1 ), *worldTrans );

                if( ( item.brushColor & 0xFF000000 ) != 0 )
                {
                    InternalDrawTriangle( a0, a2, a1, -normZP );
                    InternalDrawTriangle( a2, a0, a3, -normZP );

                    InternalDrawTriangle( b0, b1, b2, +normZP );
                    InternalDrawTriangle( b2, b3, b0, +normZP );

                    InternalDrawTriangle( a0, a1, b1, -normYP );
                    InternalDrawTriangle( b1, b0, a0, -normYP );

                    InternalDrawTriangle( a1, a2, b2, +normXP );
                    InternalDrawTriangle( b1, a1, b2, +normXP );

                    InternalDrawTriangle( a2, a3, b3, +normYP );
                    InternalDrawTriangle( b3, b2, a2, +normYP );

                    InternalDrawTriangle( a3, a0, b0, -normXP );
                    InternalDrawTriangle( b0, b3, a3, -normXP );
                }

                if( ( item.penColor & 0xFF000000 ) != 0 )
                {
                    a0.color = item.penColor;
                    a1.color = item.penColor;
                    a2.color = item.penColor;
                    a3.color = item.penColor;
                    b0.color = item.penColor;
                    b1.color = item.penColor;
                    b2.color = item.penColor;
                    b3.color = item.penColor;

                    InternalDrawLine( a0, a1 );
                    InternalDrawLine( a1, a2 );
                    InternalDrawLine( a2, a3 );
                    InternalDrawLine( a3, a0 );
                    InternalDrawLine( a0, b0 );
                    InternalDrawLine( a1, b1 );
                    InternalDrawLine( a2, b2 );
                    InternalDrawLine( a3, b3 );
                    InternalDrawLine( b0, b1 );
                    InternalDrawLine( b1, b2 );
                    InternalDrawLine( b2, b3 );
                    InternalDrawLine( b3, b0 );
                }
            }

            if( item.type == Sphere )
            {
                if( ( item.brushColor & 0xFF000000 ) != 0 )
                {
                    for( size_t j = 0; j < m_sphereIndices.size( ); j += 3 )
                    {
                        vaVector3 sCenter = item.v0;
                        float sRadius = item.v1.x;

                        vaVector3 v0 = m_sphereVertices[m_sphereIndices[j + 0]] * sRadius + sCenter;
                        vaVector3 v1 = m_sphereVertices[m_sphereIndices[j + 1]] * sRadius + sCenter;
                        vaVector3 v2 = m_sphereVertices[m_sphereIndices[j + 2]] * sRadius + sCenter;

                        CanvasVertex3D a0( v0, item.brushColor, trans );
                        CanvasVertex3D a1( v1, item.brushColor, trans );
                        CanvasVertex3D a2( v2, item.brushColor, trans );

                        InternalDrawTriangle( a0, a1, a2, vaVector3::TransformNormal( vaVector3::TriangleNormal( v0, v1, v2, false ), *worldTrans ) );
                    }
                }

                if( ( item.penColor & 0xFF000000 ) != 0 )
                {
                    for( size_t j = 0; j < m_sphereIndices.size( ); j += 3 )
                    {
                        vaVector3 sCenter = item.v0;
                        float sRadius = item.v1.x;

                        CanvasVertex3D a0( m_sphereVertices[m_sphereIndices[j + 0]] * sRadius + sCenter, item.penColor, trans );
                        CanvasVertex3D a1( m_sphereVertices[m_sphereIndices[j + 1]] * sRadius + sCenter, item.penColor, trans );
                        CanvasVertex3D a2( m_sphereVertices[m_sphereIndices[j + 2]] * sRadius + sCenter, item.penColor, trans );

                        InternalDrawLine( a0, a1 );
                        InternalDrawLine( a1, a2 );
                        InternalDrawLine( a2, a0 );
                    }
                }
            }

            //if( item.type == SphereCone )
            //{
            //    auto hackToCone = []( const vaVector3 & v, const vaVector3 & dir, float coneAngle )
            //    {
            //        dir; coneAngle;

            //        //float ca = std::cosf( coneAngle );
            //        //float dp = vaVector3::Dot( v, dir );
            //        //if( dp < ca )
            //        //    return (v + (ca-dp) * dir);

            //        float vangle = vaVector3::AngleBetweenVectors( v, vaVector3( 0, 0, 1 ) );
            //        if( vangle > coneAngle )
            //        {

            //            return (v + dir * sin( (vangle-coneAngle) ));
            //        }
            //        else
            //            return v;
            //    };

            //    if( ( item.brushColor & 0xFF000000 ) != 0 )
            //    {
            //        for( size_t j = 0; j < m_sphereIndices.size( ); j += 3 )
            //        {
            //            vaVector3 center    = item.v0;
            //            float radius        = item.v1.x;
            //            float coneAngle     = item.v1.y;
            //            vaVector3 dir       = item.v2;

            //            vaVector3 v0 = hackToCone( m_sphereVertices[m_sphereIndices[j + 0]], dir, coneAngle );
            //            vaVector3 v1 = hackToCone( m_sphereVertices[m_sphereIndices[j + 1]], dir, coneAngle );
            //            vaVector3 v2 = hackToCone( m_sphereVertices[m_sphereIndices[j + 2]], dir, coneAngle );

            //            v0 = v0 * radius + center;
            //            v1 = v1 * radius + center;
            //            v2 = v2 * radius + center;

            //            CanvasVertex3D a0( v0, item.brushColor, trans );
            //            CanvasVertex3D a1( v1, item.brushColor, trans );
            //            CanvasVertex3D a2( v2, item.brushColor, trans );

            //            InternalDrawTriangle( a0, a1, a2, vaVector3::TransformNormal( vaVector3::TriangleNormal( v0, v1, v2, false ), *worldTrans ) );
            //        }
            //    }
            //}
        }
        size_t batchSize = 512;
        for( size_t i = 0; i < m_drawTrianglesTransformed.size( ); i += batchSize )
            RenderTrianglesBatch( renderContext, renderOutputs, camera, m_drawTrianglesTransformed.data( ) + i, vaMath::Min( batchSize, m_drawTrianglesTransformed.size( ) - i ) );

        FlushTriangles( renderContext, renderOutputs, camera );

        // then add the lines (non-transformed to transformed)
        for( size_t i = 0; i < m_drawLines.size( ); i++ )
            m_drawLinesTransformed.push_back( DrawLineTransformed( CanvasVertex3D(m_drawLines[i].v0, m_drawLines[i].penColor0, &viewProj), CanvasVertex3D(m_drawLines[i].v1, m_drawLines[i].penColor1, &viewProj) ) );

        for( size_t i = 0; i < m_drawLinesTransformed.size( ); i += batchSize )
            RenderLineBatch( renderContext, renderOutputs, camera, m_drawLinesTransformed.data( ) + i, vaMath::Min( batchSize, m_drawLinesTransformed.size( ) - i ) );

        FlushLines( renderContext, renderOutputs, camera );

    }
    CleanQueued( );
}

void vaDebugCanvas3D::DrawText3D( vaDebugCanvas2D & canvas2D, const vaVector3 & position3D, const vaVector2 & screenOffset, unsigned int penColor, unsigned int shadowColor, const char * text, ... )
{
    vaDirectXCanvas2D_FORMAT_STR( );
    const vaVector4 worldPos = vaVector4(position3D,1);

    vaMatrix4x4 viewProj = m_lastCamera.GetViewMatrix( ) * m_lastCamera.GetProjMatrix( );
    vaVector4 pos = vaVector4::Transform(worldPos, viewProj); pos /= pos.w; pos.x = pos.x * 0.5f + 0.5f; pos.y = -pos.y * 0.5f + 0.5f;
    if( pos.z > 0 ) // don't draw if behind near clipping plane
    {
        pos.x *= m_lastCamera.GetViewportWidth( ); pos.y *= m_lastCamera.GetViewportHeight( );
        canvas2D.DrawText( pos.x + screenOffset.x, pos.y + screenOffset.y, penColor, shadowColor, szBuffer );
    }

}
