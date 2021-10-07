///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaPostProcess.h"

#include "Rendering/vaRenderingIncludes.h"
#include "Rendering/Shaders/vaSharedTypes.h"

using namespace Vanilla;

vaPostProcess::vaPostProcess( const vaRenderingModuleParams & params ) : 
    m_pixelShaderSingleSampleMS{ (params), (params), (params), (params), (params), (params), (params), (params) },
    vaRenderingModule( vaRenderingModuleParams( params ) ), m_colorProcessHSBC( params ), m_colorProcessLumaForEdges( params ), m_downsample4x4to1x1( params ),
    m_constantBuffer( vaConstantBuffer::Create<PostProcessConstants>( params.RenderDevice, "PostProcessConstants" ) ), 
    m_pixelShaderCompare( params ),
    m_pixelShaderCompareInSRGB( params ),
    m_vertexShaderStretchRect( params ),
//    m_pixelShaderStretchRectLinear( params ),
//    m_pixelShaderStretchRectPoint( params ),
    m_simpleBlurSharpen( params ),
    m_pixelShaderDepthToViewspaceLinear( params ),
    m_pixelShaderDepthToViewspaceLinearDS2x2Min( params ),
    m_pixelShaderDepthToViewspaceLinearDS4x4Min( params ),
    m_pixelShaderDepthToViewspaceLinearDS2x2LinAvg( params ),
    m_pixelShaderDepthToViewspaceLinearDS2x2Max( params ),
    m_pixelShaderDepthToViewspaceLinearDS4x4Max( params ),
    m_pixelShaderDepthToViewspaceLinearDS4x4LinAvg( params ),
    m_pixelShaderSmartOffscreenUpsampleComposite( params ),
    m_pixelShaderMIPFilterNormalsXY_UNORM( params ),
    m_pixelShaderMergeTextures( params ),
    m_CSCopySliceTo3DTexture( params )
{
    m_comparisonResultsGPU = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R32_UINT, POSTPROCESS_COMPARISONRESULTS_SIZE*3, 1, 1, 1, 1, vaResourceBindSupportFlags::UnorderedAccess | vaResourceBindSupportFlags::RenderTarget /* | vaResourceBindSupportFlags::ShaderResource*/, vaResourceAccessFlags::Default );
    m_comparisonResultsCPU = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::R32_UINT, POSTPROCESS_COMPARISONRESULTS_SIZE*3, 1, 1, 1, 1, vaResourceBindSupportFlags::None, vaResourceAccessFlags::CPURead );

    m_pixelShaderCompare->CompileFromFile( "vaPostProcess.hlsl", "PSCompareTextures", m_staticShaderMacros, false );

    std::vector<std::pair<std::string, std::string>> srgbMacros = m_staticShaderMacros; srgbMacros.push_back( make_pair( string( "POSTPROCESS_COMPARE_IN_SRGB_SPACE" ), string( "1" ) ) );
    m_pixelShaderCompareInSRGB->CompileFromFile( "vaPostProcess.hlsl", "PSCompareTextures", srgbMacros, false );

    std::vector<vaVertexInputElementDesc> inputElements;
    inputElements.push_back( { "SV_Position", 0, vaResourceFormat::R32G32B32A32_FLOAT, 0, 0, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );
    inputElements.push_back( { "TEXCOORD", 0, vaResourceFormat::R32G32_FLOAT, 0, 16, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );

    m_vertexShaderStretchRect->CompileVSAndILFromFile( "vaPostProcess.hlsl", "VSStretchRect", inputElements, m_staticShaderMacros, false );
//    m_pixelShaderStretchRectLinear->CompileFromFile( "vaPostProcess.hlsl", "PSStretchRectLinear", m_staticShaderMacros, false );
//    m_pixelShaderStretchRectPoint->CompileFromFile( "vaPostProcess.hlsl", "PSStretchRectPoint", m_staticShaderMacros, false );

    m_pixelShaderDepthToViewspaceLinear->CompileFromFile( "vaPostProcess.hlsl", "PSDepthToViewspaceLinear", m_staticShaderMacros, false );
    m_pixelShaderDepthToViewspaceLinearDS2x2Min->CompileFromFile( "vaPostProcess.hlsl", "PSDepthToViewspaceLinearDS2x2", m_staticShaderMacros, false );
    m_pixelShaderDepthToViewspaceLinearDS4x4Min->CompileFromFile( "vaPostProcess.hlsl", "PSDepthToViewspaceLinearDS4x4", m_staticShaderMacros, false );
    std::vector<std::pair<std::string, std::string>> maxMacros = m_staticShaderMacros; maxMacros.push_back( make_pair( string( "VA_DEPTHDOWNSAMPLE_USE_MAX" ), string( "" ) ) );
    m_pixelShaderDepthToViewspaceLinearDS2x2Max->CompileFromFile( "vaPostProcess.hlsl", "PSDepthToViewspaceLinearDS2x2", maxMacros, false );
    m_pixelShaderDepthToViewspaceLinearDS4x4Max->CompileFromFile( "vaPostProcess.hlsl", "PSDepthToViewspaceLinearDS4x4", maxMacros, false );
    std::vector<std::pair<std::string, std::string>> linAvgMacros = m_staticShaderMacros; linAvgMacros.push_back( make_pair( string( "VA_DEPTHDOWNSAMPLE_USE_LINEAR_AVERAGE" ), string( "" ) ) );
    m_pixelShaderDepthToViewspaceLinearDS2x2LinAvg->CompileFromFile( "vaPostProcess.hlsl", "PSDepthToViewspaceLinearDS2x2", linAvgMacros, false );
    m_pixelShaderDepthToViewspaceLinearDS4x4LinAvg->CompileFromFile( "vaPostProcess.hlsl", "PSDepthToViewspaceLinearDS4x4", linAvgMacros, false );

    m_pixelShaderSmartOffscreenUpsampleComposite->CompileFromFile( "vaPostProcess.hlsl", "PSSmartOffscreenUpsampleComposite", m_staticShaderMacros, false );
    
    // this still lets the 3 compile in parallel
    m_vertexShaderStretchRect->WaitFinishIfBackgroundCreateActive( );
//    m_pixelShaderStretchRectLinear->WaitFinishIfBackgroundCreateActive( );
//    m_pixelShaderStretchRectPoint->WaitFinishIfBackgroundCreateActive( );
}

vaPostProcess::~vaPostProcess( )
{
}

/*
vaDrawResultFlags vaPostProcess::StretchRect( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & srcTexture, const vaVector4 & srcRect, const vaVector4 & dstRect, bool linearFilter, vaBlendMode blendMode, const vaVector4 & colorMul, const vaVector4 & colorAdd )
{
    VA_TRACE_CPUGPU_SCOPE( PP_StretchRect, renderContext );

    // not yet supported / tested
    assert( dstRect.x == 0 );
    assert( dstRect.y == 0 );

    vaVector2 dstPixSize = vaVector2( 1.0f / (dstRect.z-dstRect.x), 1.0f / (dstRect.w-dstRect.y) );

    // Setup
    UpdateShaders( );

    vaVector2 srcPixSize = vaVector2( 1.0f / (float)srcTexture->GetSizeX(), 1.0f / (float)srcTexture->GetSizeY() );

    PostProcessConstants consts;
    consts.Param1.x = dstPixSize.x * dstRect.x * 2.0f - 1.0f;
    consts.Param1.y = 1.0f - dstPixSize.y * dstRect.y * 2.0f;
    consts.Param1.z = dstPixSize.x * dstRect.z * 2.0f - 1.0f;
    consts.Param1.w = 1.0f - dstPixSize.y * dstRect.w * 2.0f;

    consts.Param2.x = srcPixSize.x * srcRect.x;
    consts.Param2.y = srcPixSize.y * srcRect.y;
    consts.Param2.z = srcPixSize.x * srcRect.z;
    consts.Param2.w = srcPixSize.y * srcRect.w;

    consts.Param3   = colorMul;
    consts.Param4   = colorAdd;

    //consts.Param2.x = (float)viewport.Width
    //consts.Param2 = dstRect;

    m_constantBuffer.Update( renderContext, consts );

    vaGraphicsItem renderItem;
    renderContext.FillFullscreenPassGraphicsItem( renderItem );

    renderItem.ConstantBuffers[ POSTPROCESS_CONSTANTSBUFFERSLOT ]  = m_constantBuffer;
    renderItem.ShaderResourceViews[ POSTPROCESS_TEXTURE_SLOT0 ]     = srcTexture;

    renderItem.VertexShader         = m_vertexShaderStretchRect;
    //renderItem.VertexShader->WaitFinishIfBackgroundCreateActive();
    renderItem.PixelShader          = (linearFilter)?(m_pixelShaderStretchRectLinear):(m_pixelShaderStretchRectPoint);
    //renderItem.PixelShader->WaitFinishIfBackgroundCreateActive();
    renderItem.BlendMode            = blendMode;

    return renderContext.ExecuteSingleItem( renderItem, nullptr );
}

vaDrawResultFlags vaPostProcess::StretchRect( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & dstTexture, const shared_ptr<vaTexture> & srcTexture, const vaVector4 & srcRect, const vaVector4 & dstRect, bool linearFilter, vaBlendMode blendMode, const vaVector4 & colorMul, const vaVector4 & colorAdd )
{
    auto outputs = renderContext.GetOutputs();
    renderContext.SetRenderTarget( dstTexture, nullptr, true );
    auto ret = StretchRect( renderContext, srcTexture, srcRect, dstRect, linearFilter, blendMode, colorMul, colorAdd );
    renderContext.SetOutputs( outputs );
    return ret;
}
*/

vaDrawResultFlags vaPostProcess::DrawSingleSampleFromMSTexture( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const shared_ptr<vaTexture> & srcTexture, int sampleIndex )
{
    if( sampleIndex < 0 || sampleIndex >= _countof( m_pixelShaderSingleSampleMS) )
    {
        assert( false );
        return vaDrawResultFlags::UnspecifiedError;
    }

    if( m_pixelShaderSingleSampleMS[0]->IsEmpty() )
    {
        for( int i = 0; i < _countof( m_pixelShaderSingleSampleMS ); i++ )
        {
            std::vector< pair< string, string > > macros;
            macros.push_back( std::pair<std::string, std::string>( "VA_DRAWSINGLESAMPLEFROMMSTEXTURE_SAMPLE", vaStringTools::Format("%d", i ) ) );
        
            m_pixelShaderSingleSampleMS[i]->CompileFromFile( "vaPostProcess.hlsl", "SingleSampleFromMSTexturePS", macros, false );
        }
        for( int i = 0; i < _countof( m_pixelShaderSingleSampleMS ); i++ )
            m_pixelShaderSingleSampleMS[i]->WaitFinishIfBackgroundCreateActive( );
    }


    vaGraphicsItem renderItem;
    m_renderDevice.FillFullscreenPassGraphicsItem( renderItem );
    renderItem.ShaderResourceViews[ 0 ] = srcTexture;
    renderItem.PixelShader              = m_pixelShaderSingleSampleMS[sampleIndex];
    return renderContext.ExecuteSingleItem( renderItem, renderOutputs, nullptr );
}

//void vaPostProcess::ColorProcess( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & srcTexture, float someSetting )
//{
//    someSetting;
//    if( !m_colorProcessPS->IsCreated( ) || srcTexture.GetSampleCount( ) != m_colorProcessPSSampleCount )
//    {
//        m_colorProcessPSSampleCount = srcTexture.GetSampleCount();
//        m_colorProcessPS->CompileFromFile( "vaPostProcess.hlsl", "ColorProcessPS", { ( pair< string, string >( "VA_POSTPROCESS_COLORPROCESS", "" ) ), ( pair< string, string >( "VA_POSTPROCESS_COLORPROCESS_MS_COUNT", vaStringTools::Format("%d", m_colorProcessPSSampleCount) ) ) } );
//    }
//    srcTexture.SetToAPISlotSRV( renderContext, 0 );
//    renderContext.FullscreenPassDraw( *m_colorProcessPS );
//    srcTexture.UnsetFromAPISlotSRV( renderContext, 0 );
//}

vaDrawResultFlags vaPostProcess::ColorProcessHSBC( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const shared_ptr<vaTexture> & srcTexture, float hue, float saturation, float brightness, float contrast )
{
    VA_TRACE_CPUGPU_SCOPE( ColorProcessHueSatBrightContr, renderContext );

    if( m_colorProcessHSBC->IsEmpty( ) )
    {
        m_colorProcessHSBC->CompileFromFile( "vaPostProcess.hlsl", "ColorProcessHSBCPS", { ( pair< string, string >( "VA_POSTPROCESS_COLOR_HSBC", "" ) ) }, true );
    }

    PostProcessConstants consts; memset( &consts, 0, sizeof(consts) );
    consts.Param1.x = vaMath::Clamp( hue,           -1.0f, 1.0f );
    consts.Param1.y = vaMath::Clamp( saturation,    -1.0f, 1.0f ) + 1.0f;
    consts.Param1.z = vaMath::Clamp( brightness,    -1.0f, 1.0f ) + 1.0f;
    consts.Param1.w = vaMath::Clamp( contrast,      -1.0f, 1.0f );
    // hue goes from [-PI,+PI], saturation goes from [-1, 1], brightness goes from [-1, 1], contrast goes from [-1, 1]
    
    m_constantBuffer->Upload( renderContext, consts);

    vaGraphicsItem renderItem;
    m_renderDevice.FillFullscreenPassGraphicsItem( renderItem );
    renderItem.ConstantBuffers[POSTPROCESS_CONSTANTSBUFFERSLOT]    = m_constantBuffer;
    renderItem.ShaderResourceViews[ 0 ]                             = srcTexture;
    renderItem.PixelShader              = m_colorProcessHSBC;
    return renderContext.ExecuteSingleItem( renderItem, renderOutputs, nullptr );
}

// from https://developer.nvidia.com/sites/all/modules/custom/gpugems/books/GPUGems/gpugems_ch24.html
const float BicubicWeight( float x )
{
    const float A = -0.75f;

    x = vaMath::Clamp( x, 0.0f, 2.0f );

    if( x <= 1.0f )
        return (A + 2.0f) * x*x*x - (A + 3.0f) * x*x + 1.0f;
    else
        return A*x*x*x - 5.0f*A*x*x + 8.0f*A*x - 4.0f*A;
}

// all of this is very ad-hoc
vaDrawResultFlags vaPostProcess::SimpleBlurSharpen( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & dstTexture, const shared_ptr<vaTexture> & srcTexture, float sharpen = 0.0f )
{
    VA_TRACE_CPUGPU_SCOPE( SimpleBlurSharpen, renderContext );
    if( m_simpleBlurSharpen->IsEmpty( ) )
    {
        m_simpleBlurSharpen->CompileFromFile( "vaPostProcess.hlsl", "SimpleBlurSharpen", { ( pair< string, string >( "VA_POSTPROCESS_SIMPLE_BLUR_SHARPEN", "" ) ) }, true );
    }

    assert( (srcTexture->GetSizeX() == dstTexture->GetSizeX() ) && (srcTexture->GetSizeY() == dstTexture->GetSizeY()) );

    PostProcessConstants consts; memset( &consts, 0, sizeof(consts) );

    sharpen = vaMath::Clamp( sharpen, -1.0f, 1.0f );

    float blurK     = BicubicWeight( 0.65f );
    float blurDK    = BicubicWeight( 0.65f*vaMath::Sqrt(2.0f) );
    float sharpK    = BicubicWeight( 1.23f );
    float sharpDK   = BicubicWeight( 1.23f*vaMath::Sqrt(2.0f) );

    consts.Param1.x = (sharpen < 0)?(vaMath::Lerp(0.0f, blurK, -sharpen)):(vaMath::Lerp(0.0f, sharpK, sharpen));
    consts.Param1.y = (sharpen < 0)?(vaMath::Lerp(0.0f, blurDK, -sharpen)):(vaMath::Lerp(0.0f, sharpDK, sharpen));
    consts.Param1.z = 0.0f;
    consts.Param1.w = 0.0f;

    m_constantBuffer->Upload( renderContext, consts);

    vaGraphicsItem renderItem;
    m_renderDevice.FillFullscreenPassGraphicsItem( renderItem );
    renderItem.ConstantBuffers[POSTPROCESS_CONSTANTSBUFFERSLOT]    = m_constantBuffer;
    renderItem.ShaderResourceViews[ 0 ]                             = srcTexture;
    renderItem.PixelShader              = m_simpleBlurSharpen;
    vaDrawResultFlags renderResults = renderContext.ExecuteSingleItem( renderItem, vaRenderOutputs::FromRTDepth(dstTexture), nullptr );

    return renderResults;
}

vaDrawResultFlags vaPostProcess::ComputeLumaForEdges( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & dstTexture, const shared_ptr<vaTexture> & srcTexture )
{
    VA_TRACE_CPUGPU_SCOPE( ComputeLumaForEdges, renderContext );

    if( m_colorProcessLumaForEdges->IsEmpty( ) )
    {
        m_colorProcessLumaForEdges->CompileFromFile( "vaPostProcess.hlsl", "ColorProcessLumaForEdges", { ( pair< string, string >( "VA_POSTPROCESS_LUMA_FOR_EDGES", "" ) ) }, true );
    }

    vaGraphicsItem renderItem;
    m_renderDevice.FillFullscreenPassGraphicsItem( renderItem );
    renderItem.ShaderResourceViews[ 0 ] = srcTexture;
    renderItem.PixelShader              = m_colorProcessLumaForEdges;
    return renderContext.ExecuteSingleItem( renderItem, vaRenderOutputs::FromRTDepth(dstTexture), nullptr );
}

vaDrawResultFlags vaPostProcess::Downsample4x4to1x1( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & dstTexture, const shared_ptr<vaTexture> & srcTexture, float sharpen )
{
    VA_TRACE_CPUGPU_SCOPE( Downsample4x4to1x1, renderContext );

    sharpen = vaMath::Clamp( sharpen, 0.0f, 1.0f );

    assert( (srcTexture->GetSizeX() % 4 == 0) && (srcTexture->GetSizeY() % 4 == 0) );
    assert( (srcTexture->GetSizeX() / 4 == dstTexture->GetSizeX() ) && (srcTexture->GetSizeY() / 4 == dstTexture->GetSizeY()) );

    PostProcessConstants consts; memset( &consts, 0, sizeof(consts) );
    consts.Param1.x = 1.0f / (float)srcTexture->GetSizeX();
    consts.Param1.y = 1.0f / (float)srcTexture->GetSizeY();
    consts.Param1.z = 1.0f - sharpen * 0.5f;
    consts.Param1.w = 1.0f - sharpen * 0.5f;

    if( m_downsample4x4to1x1->IsEmpty( ) )
    {
        m_downsample4x4to1x1->CompileFromFile( "vaPostProcess.hlsl", "Downsample4x4to1x1", { ( pair< string, string >( "VA_POSTPROCESS_DOWNSAMPLE", "" ) ) }, true );
    }

    m_constantBuffer->Upload( renderContext, consts );

    vaGraphicsItem renderItem;
    m_renderDevice.FillFullscreenPassGraphicsItem( renderItem );
    renderItem.ConstantBuffers[POSTPROCESS_CONSTANTSBUFFERSLOT]    = m_constantBuffer;
    renderItem.ShaderResourceViews[ 0 ]                             = srcTexture;
    renderItem.PixelShader              = m_downsample4x4to1x1;
    vaDrawResultFlags renderResults = renderContext.ExecuteSingleItem( renderItem, vaRenderOutputs::FromRTDepth(dstTexture), nullptr );

    return renderResults;
}

void vaPostProcess::UpdateShaders( )
{
    //if( newShaderMacros != m_staticShaderMacros )
    //{
    //    m_staticShaderMacros = newShaderMacros;
    //    m_shadersDirty = true;
    //}

    if( m_shadersDirty )
    {
        m_shadersDirty = false;
    }
}

vaVector4 vaPostProcess::CompareImages( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & textureA, const shared_ptr<vaTexture> & textureB, bool compareInSRGB )
{
    VA_TRACE_CPUGPU_SCOPE( PP_CompareImages, renderContext );

    assert( textureA->GetSizeX() == textureB->GetSizeX() );
    assert( textureA->GetSizeY() == textureB->GetSizeY() );

    // Setup
    UpdateShaders( );

   
    int inputSizeX = textureA->GetSizeX();
    int inputSizeY = textureA->GetSizeY();

    // set output
    vaRenderOutputs renderOutputs;
    renderOutputs.UnorderedAccessViews[0] = m_comparisonResultsGPU;
    renderOutputs.Viewport = vaViewport( inputSizeX, inputSizeY );

    // clear results UAV
    // m_comparisonResultsGPU->ClearUAV( renderContext, vaVector4ui(0, 0, 0, 0) );
    m_comparisonResultsGPU->ClearRTV( renderContext, vaVector4(0, 0, 0, 0) );

    // Call GPU comparison shader
    vaGraphicsItem renderItem;
    m_renderDevice.FillFullscreenPassGraphicsItem( renderItem );
    renderItem.ShaderResourceViews[ POSTPROCESS_TEXTURE_SLOT0 ] = textureA;
    renderItem.ShaderResourceViews[ POSTPROCESS_TEXTURE_SLOT1 ] = textureB;
    renderItem.PixelShader = (compareInSRGB)?(m_pixelShaderCompareInSRGB):(m_pixelShaderCompare);
    renderItem.PixelShader->WaitFinishIfBackgroundCreateActive();
    vaDrawResultFlags renderResults = renderContext.ExecuteSingleItem( renderItem, renderOutputs, nullptr );
    if( renderResults != vaDrawResultFlags::None )
    {
        VA_ERROR( "vaPostProcess::CompareImages - error while rendering" );
    }

    // GPU -> CPU copy ( SYNC POINT HERE!! but it doesn't matter because this is only supposed to be used for unit tests and etc.)
    m_comparisonResultsCPU->CopyFrom( renderContext, m_comparisonResultsGPU );

    // we must work on the main context due to mapping limitations
    assert( &renderContext == GetRenderDevice().GetMainContext() );

    uint32 data[POSTPROCESS_COMPARISONRESULTS_SIZE*3];
    if( m_comparisonResultsCPU->TryMap( renderContext, vaResourceMapType::Read, false ) )
    {
        auto & mappedData = m_comparisonResultsCPU->GetMappedData();
        memcpy( data, mappedData[0].Buffer, sizeof( data ) );
        m_comparisonResultsCPU->Unmap( renderContext );
    }
    else
    {
        VA_LOG_ERROR( "vaPostProcess::CompareImages failed to map result data!" );
        assert( false );
    }

    // calculate results
    vaVector4 ret( 0.0f, 0.0f, 0.0f, 0.0f );

    int totalPixelCount = inputSizeX * inputSizeY;
    uint64 resultsSumR = 0;
    uint64 resultsSumG = 0;
    uint64 resultsSumB = 0;

    //double resultsSumD = 0.0;
    //double resultsSumCount = 0.0;

    for( size_t i = 0; i < _countof( data ); i+=3 )
    {
//        VA_LOG( " %d %x", i, data[i] );

        resultsSumR += data[i+0];
        resultsSumG += data[i+1];
        resultsSumB += data[i+2];
    }
    double resultsSumAvg = ((double)resultsSumR + (double)resultsSumG + (double)resultsSumB) / 3.0; // or use Luma-based weights? like (0.2989, 0.5866, 0.1145)? or apply them before sqr in the shader? no idea

    double MSEVal = ( ((double)resultsSumAvg / POSTPROCESS_COMPARISONRESULTS_FIXPOINT_MAX ) / (double)totalPixelCount);

    ret.x = (float)(MSEVal);                        // Mean Squared Error
    ret.y = (float)vaMath::PSNR( MSEVal, 1.0 );     // PSNR - we assume 1 is the max value
    ret.z = 0.0f; // unused
    ret.w = 0.0f; // unused

    return ret;
}

vaDrawResultFlags vaPostProcess::DepthToViewspaceLinear( vaRenderDeviceContext & renderContext, const vaDrawAttributes & drawAttributes, const shared_ptr<vaTexture> & dstTexture, const shared_ptr<vaTexture> & srcTexture )
{
    VA_TRACE_CPUGPU_SCOPE( DepthToViewspaceLinear, renderContext );

    m_pixelShaderDepthToViewspaceLinear->WaitFinishIfBackgroundCreateActive( );

    vaGraphicsItem renderItem;
    m_renderDevice.FillFullscreenPassGraphicsItem( renderItem );
    renderItem.ShaderResourceViews[ 0 ] = srcTexture;
    renderItem.PixelShader              = m_pixelShaderDepthToViewspaceLinear;
    auto ret = renderContext.ExecuteSingleItem( renderItem, vaRenderOutputs::FromRTDepth(dstTexture), &drawAttributes );

    return ret;
}

vaDrawResultFlags vaPostProcess::DepthToViewspaceLinearDownsample2x2( vaRenderDeviceContext & renderContext, const vaDrawAttributes & drawAttributes, const shared_ptr<vaTexture> & dstTexture, const shared_ptr<vaTexture> & srcTexture, vaDepthFilterMode depthFilterMode )
{
    VA_TRACE_CPUGPU_SCOPE( DepthToViewspaceLinearDS2x2, renderContext );

    shared_ptr<vaPixelShader> shader;
    if( depthFilterMode == vaDepthFilterMode::LinearAvg )
        shader = m_pixelShaderDepthToViewspaceLinearDS2x2LinAvg;
    else
    {
        bool useMin = depthFilterMode == vaDepthFilterMode::Closest;
        if( drawAttributes.Camera.GetUseReversedZ() )
            useMin = !useMin;
        shader = (useMin)?(m_pixelShaderDepthToViewspaceLinearDS2x2Min):(m_pixelShaderDepthToViewspaceLinearDS2x2Max);
    }

    shader->WaitFinishIfBackgroundCreateActive( );

    vaGraphicsItem renderItem;
    m_renderDevice.FillFullscreenPassGraphicsItem( renderItem );
    renderItem.ShaderResourceViews[ 0 ] = srcTexture;
    renderItem.PixelShader              = shader;
    auto ret = renderContext.ExecuteSingleItem( renderItem, vaRenderOutputs::FromRTDepth(dstTexture), &drawAttributes );


    return ret;
}

vaDrawResultFlags vaPostProcess::DepthToViewspaceLinearDownsample4x4( vaRenderDeviceContext & renderContext, const vaDrawAttributes & drawAttributes, const shared_ptr<vaTexture> & dstTexture, const shared_ptr<vaTexture> & srcTexture, vaDepthFilterMode depthFilterMode )
{
    VA_TRACE_CPUGPU_SCOPE( DepthToViewspaceLinearDS4x4, renderContext );

    shared_ptr<vaPixelShader> shader;
    if( depthFilterMode == vaDepthFilterMode::LinearAvg )
        shader = m_pixelShaderDepthToViewspaceLinearDS4x4LinAvg;
    else
    {
        bool useMin = depthFilterMode == vaDepthFilterMode::Closest;
        if( drawAttributes.Camera.GetUseReversedZ() )
            useMin = !useMin;
        shader = (useMin)?(m_pixelShaderDepthToViewspaceLinearDS4x4Min):(m_pixelShaderDepthToViewspaceLinearDS4x4Max);
    }

    shader->WaitFinishIfBackgroundCreateActive( );

    vaGraphicsItem renderItem;
    m_renderDevice.FillFullscreenPassGraphicsItem( renderItem );
    renderItem.ShaderResourceViews[ 0 ] = srcTexture;
    renderItem.PixelShader              = shader;
    auto ret = renderContext.ExecuteSingleItem( renderItem, vaRenderOutputs::FromRTDepth(dstTexture), &drawAttributes );

    return ret;
}

vaDrawResultFlags vaPostProcess::SmartOffscreenUpsampleComposite( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaDrawAttributes & drawAttributes, const shared_ptr<vaTexture> & srcOffscreenColor, const shared_ptr<vaTexture> & srcOffscreenLinearDepth, const shared_ptr<vaTexture> & srcReferenceDepth )
{
    VA_TRACE_CPUGPU_SCOPE( PP_SmartOffscreenUpsampleComposite, renderContext );
   
    if( renderOutputs.RenderTargets[0] == nullptr )
        { assert( false ); return vaDrawResultFlags::UnspecifiedError; }

    if( renderOutputs.RenderTargets[0]->GetSize( ) != srcReferenceDepth->GetSize( ) )
        { assert( false ); return vaDrawResultFlags::UnspecifiedError; }

    if( srcOffscreenColor->GetSize( ) != srcOffscreenLinearDepth->GetSize( ) )
        { assert( false ); return vaDrawResultFlags::UnspecifiedError; }

    vaVector4 srcRect( 0.0f, 0.0f, (float)srcOffscreenColor->GetSizeX(), (float)srcOffscreenColor->GetSizeY() );
    vaVector4 dstRect( 0.0f, 0.0f, (float)srcReferenceDepth->GetSizeX(), (float)srcReferenceDepth->GetSizeY() );

    // not yet supported / tested
    assert( dstRect.x == 0 );
    assert( dstRect.y == 0 );

    vaVector2 dstPixSize = vaVector2( 1.0f / (dstRect.z-dstRect.x), 1.0f / (dstRect.w-dstRect.y) );
    vaVector2 srcPixSize = vaVector2( 1.0f / (float)srcOffscreenColor->GetSizeX(), 1.0f / (float)srcOffscreenColor->GetSizeY() );

    m_pixelShaderSmartOffscreenUpsampleComposite->WaitFinishIfBackgroundCreateActive( );

    // Setup
    UpdateShaders( );

    PostProcessConstants consts;
    consts.Param1.x = dstPixSize.x * dstRect.x * 2.0f - 1.0f;
    consts.Param1.y = 1.0f - dstPixSize.y * dstRect.y * 2.0f;
    consts.Param1.z = dstPixSize.x * dstRect.z * 2.0f - 1.0f;
    consts.Param1.w = 1.0f - dstPixSize.y * dstRect.w * 2.0f;

    consts.Param2.x = srcPixSize.x * srcRect.x;
    consts.Param2.y = srcPixSize.y * srcRect.y;
    consts.Param2.z = srcPixSize.x * srcRect.z;
    consts.Param2.w = srcPixSize.y * srcRect.w;

    consts.Param3   = { (float)srcOffscreenColor->GetSizeX(), (float)srcOffscreenColor->GetSizeY(), 0, 0 };
    consts.Param4   = {0,0,0,0};

    //consts.Param2.x = (float)viewport.Width
    //consts.Param2 = dstRect;

    m_constantBuffer->Upload( renderContext, consts );

    vaGraphicsItem renderItem;
    m_renderDevice.FillFullscreenPassGraphicsItem( renderItem );

    renderItem.ConstantBuffers[ POSTPROCESS_CONSTANTSBUFFERSLOT ]  = m_constantBuffer;
    renderItem.ShaderResourceViews[ POSTPROCESS_TEXTURE_SLOT0 ]    = srcOffscreenColor;
    renderItem.ShaderResourceViews[ POSTPROCESS_TEXTURE_SLOT1 ]    = srcOffscreenLinearDepth;
    renderItem.ShaderResourceViews[ POSTPROCESS_TEXTURE_SLOT2 ]    = srcReferenceDepth;

    renderItem.VertexShader         = m_vertexShaderStretchRect;
    //renderItem.VertexShader->WaitFinishIfBackgroundCreateActive();
    renderItem.PixelShader          = m_pixelShaderSmartOffscreenUpsampleComposite;
    //renderItem.PixelShader->WaitFinishIfBackgroundCreateActive();
    renderItem.BlendMode            = vaBlendMode::PremultAlphaBlend;

    return renderContext.ExecuteSingleItem( renderItem, renderOutputs, &drawAttributes );
}

vaDrawResultFlags vaPostProcess::FilterMIPLevel( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & dstTexture, const shared_ptr<vaTexture> & srcTexture )
{
    assert( dstTexture->GetSizeX() == srcTexture->GetSizeX()/2 || dstTexture->GetSizeX() == 1 );
    assert( dstTexture->GetSizeY() == srcTexture->GetSizeY()/2 || dstTexture->GetSizeY() == 1 );

    assert( dstTexture->GetContentsType() == srcTexture->GetContentsType() );
    if( dstTexture->GetContentsType() == vaTextureContentsType::GenericColor 
        || dstTexture->GetContentsType() == vaTextureContentsType::GenericLinear
        || dstTexture->GetContentsType() == vaTextureContentsType::SingleChannelLinearMask )
        return renderContext.StretchRect( dstTexture, srcTexture );
    
    shared_ptr<vaPixelShader> pixelShader = nullptr;
    
    if( dstTexture->GetContentsType( ) == vaTextureContentsType::NormalsXY_UNORM )
    {
        if( m_pixelShaderMIPFilterNormalsXY_UNORM->IsEmpty( ) )
            m_pixelShaderMIPFilterNormalsXY_UNORM->CompileFromFile( "vaPostProcess.hlsl", "MIPFilterNormalsXY_UNORM", { ( pair< string, string >( "VA_POSTPROCESS_MIP_FILTERS", "" ) ) }, true );
        pixelShader = m_pixelShaderMIPFilterNormalsXY_UNORM;
    }

    if( pixelShader == nullptr )
    {
        assert( false ); // not implemented yet I guess
        return vaDrawResultFlags::UnspecifiedError;
    }

    VA_TRACE_CPUGPU_SCOPE( FilterMIPLevel, renderContext );
    vaGraphicsItem renderItem;
    m_renderDevice.FillFullscreenPassGraphicsItem( renderItem );
    renderItem.ShaderResourceViews[0] = srcTexture;
    renderItem.PixelShader = pixelShader;
    auto ret = renderContext.ExecuteSingleItem( renderItem, vaRenderOutputs::FromRTDepth(dstTexture), nullptr );
    return ret;
}

vaDrawResultFlags vaPostProcess::GenerateCubeMIPs( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & inoutCubemap )
{
    int mipCount = inoutCubemap->GetMipLevels();
    
    for( int face = 0; face < 6; face++ )
    {
        // since we don't have vaTextureLoadFlags::AutogenerateMIPsIfMissing anymore, manually generate mips here using just a box filter
        for( int mipLevel = 1; mipLevel < mipCount; mipLevel++ )
        {
            auto facemipViewSrc = vaTexture::CreateView( inoutCubemap, inoutCubemap->GetBindSupportFlags( ), vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::None,
                mipLevel - 1, 1, face, 1 );
            auto facemipViewDst = vaTexture::CreateView( inoutCubemap, inoutCubemap->GetBindSupportFlags( ), vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::None,
                mipLevel, 1, face, 1 );
            auto result = FilterMIPLevel( renderContext, facemipViewDst, facemipViewSrc );
            if( result != vaDrawResultFlags::None )
            {
                assert( false ); return result;
            }
        }
    }
    return vaDrawResultFlags::None;
}

vaDrawResultFlags vaPostProcess::GenerateMIPs( vaRenderDeviceContext& renderContext, const shared_ptr<vaTexture>& inoutTexture )
{
    int mipCount = inoutTexture->GetMipLevels( );

    // this function requires MIP layers to be created
    if( mipCount == 1 )
        { assert( false ); return vaDrawResultFlags::UnspecifiedError; }    

    // this function doesn't support cubemap textures
    if( (inoutTexture->GetFlags() & vaTextureFlags::Cubemap) != 0 || (inoutTexture->GetFlags() & vaTextureFlags::CubemapButArraySRV) != 0 )
        { assert( false ); return vaDrawResultFlags::UnspecifiedError; }    

    // this function only supports 2D textures for now
    if( inoutTexture->GetType() != vaTextureType::Texture2D )
        { assert( false ); return vaDrawResultFlags::UnspecifiedError; }    

    // this function only supports non-array textures for now
    if( inoutTexture->GetArrayCount() != 1 )
        { assert( false ); return vaDrawResultFlags::UnspecifiedError; }    

    // this function only supports non-MS textures
    if( inoutTexture->GetSampleCount() != 1 )
        { assert( false ); return vaDrawResultFlags::UnspecifiedError; }    

    // since we don't have vaTextureLoadFlags::AutogenerateMIPsIfMissing anymore, manually generate mips here using just a box filter
    for( int mipLevel = 1; mipLevel < mipCount; mipLevel++ )
    {
        auto mipViewSrc = vaTexture::CreateView( inoutTexture, vaTextureFlags::None, mipLevel - 1, 1 );
        auto mipViewDst = vaTexture::CreateView( inoutTexture, vaTextureFlags::None, mipLevel, 1 );
        auto result = FilterMIPLevel( renderContext, mipViewDst, mipViewSrc );
        if( result != vaDrawResultFlags::None )
        {
            assert( false ); return result;
        }
    }
    return vaDrawResultFlags::None;
}

vaDrawResultFlags vaPostProcess::MergeTextures( vaRenderDeviceContext& renderContext, const shared_ptr<vaTexture>& dstTexture, const shared_ptr<vaTexture>& srcTextureA, const shared_ptr<vaTexture>& srcTextureB, const shared_ptr<vaTexture>& srcTextureC, const string& mergeCode, bool uintValues )
{
    VA_TRACE_CPUGPU_SCOPE( MergeTextures, renderContext );
    if( mergeCode != m_pixelShaderMergeTexturesConversionCode || m_pixelShaderMergeTextures->IsEmpty() )
    {
        static int recompiles = 0;
        recompiles++;
        assert( recompiles < 100 ); // recompiling frequently? you probably need an std::unordered_map< string, shader > to store these :)
       
        m_pixelShaderMergeTexturesConversionCode = mergeCode;
        m_pixelShaderMergeTextures->CompileFromFile( "vaPostProcess.hlsl", "PSMergeTextures", { { "VA_POSTPROCESS_MERGETEXTURES", "" }, { "VA_POSTPROCESS_MERGETEXTURES_CODE", mergeCode }, { "VA_POSTPROCESS_MERGETEXTURES_UINT_VALUES", uintValues?"1":"0" } }, true );
    }

    if( srcTextureA == nullptr )
    {
        assert( false );
        return vaDrawResultFlags::UnspecifiedError;
    }

    //PostProcessConstants consts; memset( &consts, 0, sizeof( consts ) );
    //consts.Param1.x = vaMath::Clamp( hue, -1.0f, 1.0f );
    //m_constantBuffer.Update( renderContext, consts );

    vaGraphicsItem renderItem;
    m_renderDevice.FillFullscreenPassGraphicsItem( renderItem );
    renderItem.ConstantBuffers[POSTPROCESS_CONSTANTSBUFFERSLOT] = m_constantBuffer;
    renderItem.ShaderResourceViews[0] = srcTextureA;
    renderItem.ShaderResourceViews[1] = (srcTextureB!=nullptr)?(srcTextureB):(srcTextureA);
    renderItem.ShaderResourceViews[2] = (srcTextureC!=nullptr)?(srcTextureC):(srcTextureA);

    renderItem.PixelShader = m_pixelShaderMergeTextures;
    auto retVal = renderContext.ExecuteSingleItem( renderItem, vaRenderOutputs::FromRTDepth(dstTexture), nullptr );

    return retVal;
}

vaDrawResultFlags vaPostProcess::CopySliceToTexture3D( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & dstTexture, uint32 dstSlice, const shared_ptr<vaTexture> & srcTexture )
{
    if( dstTexture->GetType( ) != vaTextureType::Texture3D || srcTexture->GetType( ) != vaTextureType::Texture2D )
    {
        assert( false ); // texture type mismatch
        return vaDrawResultFlags::UnspecifiedError;
    }
    if( dstTexture->GetSizeX() != srcTexture->GetSizeX() || dstTexture->GetSizeY() != srcTexture->GetSizeY() || (uint32)dstTexture->GetSizeZ() <= dstSlice )
    {
        assert( false ); // texture dimension mismatch
        return vaDrawResultFlags::UnspecifiedError;
    }

    if( m_CSCopySliceTo3DTexture->IsEmpty() )
        m_CSCopySliceTo3DTexture->CompileFromFile( "vaPostProcess.hlsl", "CSCopySliceTo3DTexture", { { "VA_POSTPROCESS_3DTEXTURESTUFF", "" } }, true );

    PostProcessConstants consts; memset( &consts, 0, sizeof( consts ) );
    consts.Param1.x = (float)dstTexture->GetSizeX();
    consts.Param1.y = (float)dstTexture->GetSizeZ();
    consts.Param1.z = (float)dstSlice;
    m_constantBuffer->Upload( renderContext, consts );

    vaComputeItem computeItem;
    computeItem.ConstantBuffers[POSTPROCESS_CONSTANTSBUFFERSLOT] = m_constantBuffer;
    computeItem.ShaderResourceViews[0] = srcTexture;
    computeItem.ComputeShader = m_CSCopySliceTo3DTexture;
    computeItem.SetDispatch( (dstTexture->GetSizeX() + 7)/8, (dstTexture->GetSizeY() + 7)/8, 1 );
    auto retVal = renderContext.ExecuteSingleItem( computeItem, vaRenderOutputs::FromUAVs(dstTexture), nullptr );

    return retVal;
}
