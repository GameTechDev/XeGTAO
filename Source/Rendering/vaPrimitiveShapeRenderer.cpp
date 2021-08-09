///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vaPrimitiveShapeRenderer.h"

#include "Rendering/vaStandardShapes.h"

#include "Rendering/vaRenderDeviceContext.h"


using namespace Vanilla;

vaPrimitiveShapeRenderer::vaPrimitiveShapeRenderer( const vaRenderingModuleParams & params )
    : vaRenderingModule( params ), m_vertexBufferGPU( params, c_totalVertexCount ), m_shapeInfoBufferGPU( params, c_totalShapeBufferSize ),
    m_constantsBuffer( params ), m_vertexShader( params ), m_pixelShader( params )
{                                
    m_buffersDirty = false;
    m_verticesToDraw = 0;

    std::vector<vaVertexInputElementDesc> inputElements;
    inputElements.push_back( { "SV_Position", 0,    vaResourceFormat::R32G32_UINT,    0, 0, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );

    m_vertexShader->CreateShaderAndILFromFile( L"vaPrimitiveShapeRenderer.hlsl", "vs_5_0", "VSMain", inputElements, vaShaderMacroContaner(), true );
    m_pixelShader->CreateShaderFromFile( L"vaPrimitiveShapeRenderer.hlsl", "ps_5_0", "PSMain", vaShaderMacroContaner(), true );
}

//static void PushUint32( std::vector< uint32 > & outBuffer, uint32 val )
//{
//    outBuffer.push_back( val );
//}

static void PushFloat( std::vector< uint32 > & outBuffer, float val )
{
    outBuffer.push_back( *(uint32*)&val );
}

//static void PushVec2( std::vector< uint32 > & outBuffer, const vaVector2 & vec )
//{
//    outBuffer.push_back( *(uint32*)&vec.x );
//    outBuffer.push_back( *(uint32*)&vec.y );
//}

static void PushVec3( std::vector< uint32 > & outBuffer, const vaVector3 & vec )
{
    outBuffer.push_back( *(uint32*)&vec.x );
    outBuffer.push_back( *(uint32*)&vec.y );
    outBuffer.push_back( *(uint32*)&vec.z );
}

static void PushVec4( std::vector< uint32 > & outBuffer, const vaVector4 & vec )
{
    outBuffer.push_back( *(uint32*)&vec.x );
    outBuffer.push_back( *(uint32*)&vec.y );
    outBuffer.push_back( *(uint32*)&vec.z );
    outBuffer.push_back( *(uint32*)&vec.w );
}

void vaPrimitiveShapeRenderer::UpdateConstants( vaRenderDeviceContext & renderContext, const DrawSettings & drawSettings )
{
    {
        PrimitiveShapeRendererShaderConstants consts;

        consts.ColorMul = drawSettings.ColorMultiplier;

        m_constantsBuffer.Update( renderContext, consts );
    }

}

void vaPrimitiveShapeRenderer::AddTriangle( const vaVector3 & a, const vaVector3 & b, const vaVector3 & c, const vaVector4 & color )
{
    //////////////////////////////////////////////////////////////////////////
    // always the same header - should be pulled into a separate function
    //
    // get offset before writing
    uint32 shapeInfoBufferOffset = (uint32)m_shapeInfoBuffer.size();
    //
    // shape info ID always first
    uint32 shapeInfo        = 1;
    m_shapeInfoBuffer.push_back( shapeInfo );
    //
    // shape color always second
    PushVec4( m_shapeInfoBuffer, color );
    //////////////////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////////////////////
    // Now type-specific stuff
    //
    // 3 positions (AddTriangle custom)
    PushVec3( m_shapeInfoBuffer, a );
    PushVec3( m_shapeInfoBuffer, b );
    PushVec3( m_shapeInfoBuffer, c );
    //
    // vertex encoding for triangles: shape info buffer offset (always first, gets read as .x in vertex shader) and triangle position data offset second (gets read as .y in vertex shader)
    m_vertexBuffer.push_back( shapeInfoBufferOffset | (uint64(0 * 3) << 32) );
    m_vertexBuffer.push_back( shapeInfoBufferOffset | (uint64(1 * 3) << 32) );
    m_vertexBuffer.push_back( shapeInfoBufferOffset | (uint64(2 * 3) << 32) );
    //////////////////////////////////////////////////////////////////////////

    m_buffersDirty = true;
}

static void EncodeCylinderVertex( std::vector< uint64 > & outBuffer, uint32 shapeInfoBufferOffset, bool topFlag, bool awayFromCenterAxis, int angle )
{
    uint32 vertexInfo = (topFlag?(1<<31):0) | (awayFromCenterAxis?(1<<30):0) | (angle & ((1<<30)-1) );
    outBuffer.push_back( shapeInfoBufferOffset | (uint64(vertexInfo) << 32) );
}

void vaPrimitiveShapeRenderer::AddCylinder( float height, float radiusBottom, float radiusTop, int tessellation, bool openTopBottom, const vaVector4 & color, const vaMatrix4x4 & transform )
{
    // has to have at least 3 sides!
    if( tessellation < 3 )
    {
        assert( false );
        return;
    }
    // more than 32767 not supported
    if( tessellation > 32767 )
    {
        assert( false );
        tessellation = 32767;
    }

    //////////////////////////////////////////////////////////////////////////
    // always the same header - should be pulled into a separate function
    //
    // get offset before writing
    uint32 shapeInfoBufferOffset = (uint32)m_shapeInfoBuffer.size();
    //
    // shape info ID always first
    uint32 shapeInfo        = 2;
    m_shapeInfoBuffer.push_back( shapeInfo );
    //
    // shape color always second
    PushVec4( m_shapeInfoBuffer, color );
    //////////////////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////////////////////
    // All non-triangle types have the matrix here
    //
    PushVec4( m_shapeInfoBuffer, transform.Row(0) );
    PushVec4( m_shapeInfoBuffer, transform.Row(1) );
    PushVec4( m_shapeInfoBuffer, transform.Row(2) );
    PushVec4( m_shapeInfoBuffer, transform.Row(3) );
    //////////////////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////////////////////
    // Now type-specific stuff
    //
    // tessellation count
    PushFloat( m_shapeInfoBuffer, height );
    PushFloat( m_shapeInfoBuffer, radiusBottom );
    PushFloat( m_shapeInfoBuffer, radiusTop );
    PushFloat( m_shapeInfoBuffer, (float)tessellation );
    //
    // vertex encoding for triangles: shape info buffer offset (always first, gets read as .x in vertex shader) and triangle position data offset second (gets read as .y in vertex shader)
    for( int i = 0; i < tessellation; i++ )
    {
        if( !openTopBottom )
        {
            // top
            EncodeCylinderVertex( m_vertexBuffer, shapeInfoBufferOffset, true, false, i   );
            EncodeCylinderVertex( m_vertexBuffer, shapeInfoBufferOffset, true, true,  i );
            EncodeCylinderVertex( m_vertexBuffer, shapeInfoBufferOffset, true, true,  i+1 );
            // bottom
            EncodeCylinderVertex( m_vertexBuffer, shapeInfoBufferOffset, false, false, i );
            EncodeCylinderVertex( m_vertexBuffer, shapeInfoBufferOffset, false, true,  i+1 );
            EncodeCylinderVertex( m_vertexBuffer, shapeInfoBufferOffset, false, true,  i );
        }
        // sides
        EncodeCylinderVertex( m_vertexBuffer, shapeInfoBufferOffset, true,  true,  i );
        EncodeCylinderVertex( m_vertexBuffer, shapeInfoBufferOffset, false, true,  i );
        EncodeCylinderVertex( m_vertexBuffer, shapeInfoBufferOffset, false, true,  i+1 );
        EncodeCylinderVertex( m_vertexBuffer, shapeInfoBufferOffset, false, true,  i+1 );
        EncodeCylinderVertex( m_vertexBuffer, shapeInfoBufferOffset, true,  true,  i+1 );
        EncodeCylinderVertex( m_vertexBuffer, shapeInfoBufferOffset, true,  true,  i );
    }
    m_vertexBuffer.push_back( shapeInfoBufferOffset | (uint64(0 * 3) << 32) );
    m_vertexBuffer.push_back( shapeInfoBufferOffset | (uint64(1 * 3) << 32) );
    m_vertexBuffer.push_back( shapeInfoBufferOffset | (uint64(2 * 3) << 32) );
    //////////////////////////////////////////////////////////////////////////

    m_buffersDirty = true;
}

void vaPrimitiveShapeRenderer::Draw( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaDrawAttributes & drawAttributes, DrawSettings & drawSettings, bool clearCollected )
{
    if( m_buffersDirty )
    {
        m_vertexBufferGPU.Update( renderContext, m_vertexBuffer.data(), (uint32)m_vertexBuffer.size() );
        m_shapeInfoBufferGPU.Update( renderContext, m_shapeInfoBuffer.data(), (uint32)m_shapeInfoBuffer.size() );
        m_verticesToDraw = (int)m_vertexBuffer.size();
        m_buffersDirty = false;
    }

    if( m_verticesToDraw == 0 )
        return;

    UpdateConstants( renderContext, drawSettings );
    
    vaGraphicsItem renderItem;

    renderItem.ConstantBuffers[PRIMITIVESHAPERENDERER_CONSTANTSBUFFERSLOT] = m_constantsBuffer;
    renderItem.ShaderResourceViews[PRIMITIVESHAPERENDERER_SHAPEINFO_SRV]    = m_shapeInfoBufferGPU.GetBuffer();
    renderItem.VertexBuffer     = m_vertexBufferGPU;
    renderItem.VertexShader     = m_vertexShader;
    renderItem.PixelShader      = m_pixelShader;
    renderItem.DepthEnable      = drawSettings.UseDepth;
    renderItem.DepthFunc        = ( drawAttributes.Camera.GetUseReversedZ() )?( vaComparisonFunc::GreaterEqual ):( vaComparisonFunc::LessEqual );
    renderItem.DepthWriteEnable = drawSettings.WriteDepth;
    renderItem.BlendMode        = (drawSettings.AlphaBlend)?(vaBlendMode::AlphaBlend):(vaBlendMode::Opaque);
    renderItem.FillMode         = (drawSettings.Wireframe)?(vaFillMode::Wireframe):(vaFillMode::Solid);
    renderItem.CullMode         = drawSettings.CullMode;
    renderItem.Topology         = vaPrimitiveTopology::TriangleList;
    renderItem.SetDrawSimple( m_verticesToDraw, 0 );

    renderContext.ExecuteSingleItem( renderItem, renderOutputs, &drawAttributes );

    if( clearCollected )
    {
        m_vertexBuffer.clear();
        m_shapeInfoBuffer.clear();
        m_buffersDirty = true;
    }
}
