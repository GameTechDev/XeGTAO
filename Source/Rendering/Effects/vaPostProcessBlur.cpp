///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaPostProcessBlur.h"

#include "Rendering/vaRenderDeviceContext.h"

#include "Core/Misc/stack_container.h"

using namespace Vanilla;

template< class _Alloc = allocator<_Ty> >
static void GenerateSeparableGaussKernel( float sigma, int kernelSize, std::vector<float, _Alloc> & outKernel )
{
    if( (kernelSize % 2) != 1 )
    {
        assert( false ); // kernel size must be odd number
        outKernel.resize(0);
        return;
    }

    int halfKernelSize = kernelSize/2;

    outKernel.resize( kernelSize );

    const double cPI= 3.14159265358979323846;
    double mean     = halfKernelSize;
    double sum      = 0.0;
    for (int x = 0; x < kernelSize; ++x) 
    {
        outKernel[x] = (float)sqrt( exp( -0.5 * (pow((x-mean)/sigma, 2.0) + pow((mean)/sigma,2.0)) )
            / (2 * cPI * sigma * sigma) );
        sum += outKernel[x];
    }
    for (int x = 0; x < kernelSize; ++x) 
        outKernel[x] /= (float)sum;
}

vaPostProcessBlur::vaPostProcessBlur( const vaRenderingModuleParams & params )
 : vaRenderingModule( params ), 
    m_textureSize( vaVector2i( 0, 0 ) ),
    m_constantsBuffer( params.RenderDevice ),
    m_CSGaussHorizontal( params ),
    m_CSGaussVertical( params )
{
    m_texturesUpdatedCounter = 0;
    m_currentGaussKernelRadius = 0;
    m_currentGaussKernelSigma = 0.0f;

    m_constantsBufferNeedsUpdate = true;

    m_CSGaussHorizontal->CreateShaderFromFile( "vaPostProcessBlur.hlsl", "CSGaussHorizontal", m_staticShaderMacros, false );
    m_CSGaussVertical->CreateShaderFromFile( "vaPostProcessBlur.hlsl", "CSGaussVertical", m_staticShaderMacros, false );
}

vaPostProcessBlur::~vaPostProcessBlur( )
{
}

void vaPostProcessBlur::UpdateShaders( vaRenderDeviceContext & renderContext )
{
    renderContext; // unreferenced

    //if( newShaderMacros != m_staticShaderMacros )
    //{
    //    m_staticShaderMacros = newShaderMacros;
    //    m_shadersDirty = true;
    //}

    if( m_shadersDirty )
    {
        m_shadersDirty = false;

        // m_PSBlurA.CreateShaderFromFile( L"vaPostProcessBlur.hlsl", "ps_5_0", "PSBlurA", m_staticShaderMacros );
        // m_PSBlurB.CreateShaderFromFile( L"vaPostProcessBlur.hlsl", "ps_5_0", "PSBlurB", m_staticShaderMacros );
    }
}

void vaPostProcessBlur::UpdateGPUConstants( vaRenderDeviceContext & renderContext, float factor0 )
{
    if( !m_constantsBufferNeedsUpdate )
        return;
    m_constantsBufferNeedsUpdate = false;

    // Constants
    {
        PostProcessBlurConstants consts;
        consts.PixelSize    = vaVector2( 1.0f / (float)m_textureSize.x, 1.0f / (float)m_textureSize.y );
        consts.Factor0      = factor0;
        consts.Dummy0       = 0;
        consts.Dummy1       = 0;
        consts.Dummy2       = 0;
        consts.Dummy3       = 0;

        consts.GaussIterationCount = (int)m_currentGaussOffsets.size( );
        assert( consts.GaussIterationCount == (int)m_currentGaussWeights.size( ) );

        for( int i = 0; i < _countof(consts.GaussOffsetsWeights); i++ )
        {
            if( i < consts.GaussIterationCount )
                consts.GaussOffsetsWeights[i] = vaVector4( m_currentGaussOffsets[i], m_currentGaussWeights[i], 0.0f, 0.0f );
            else
                consts.GaussOffsetsWeights[i] = vaVector4( 0.0f, 0.0f, 0.0f, 0.0f );
        }

        m_constantsBuffer.Upload( renderContext, consts );
    }
}

void vaPostProcessBlur::UpdateFastKernelWeightsAndOffsets( )
{
    std::vector<float> & inputKernel = m_currentGaussKernel;
    int kernelSize = (int)inputKernel.size();
    if( kernelSize == 0 )
    {
        return;
    }

    assert( (kernelSize % 2) == 1 );
//    assert( (((kernelSize/2)+1) % 2) == 0 );

    vaStackVector< float, 4096 > oneSideInputsStackVector;
    auto & oneSideInputs = oneSideInputsStackVector.container();

    for( int i = (kernelSize/2); i >= 0; i-- )
    {
        if( i == (kernelSize/2) )
            oneSideInputs.push_back( (float)inputKernel[i] * 0.5f );
        else
            oneSideInputs.push_back( (float)inputKernel[i] );
    }
    if( (oneSideInputs.size() % 2) == 1 )
        oneSideInputs.push_back( 0.0f );

    int numSamples = (int)oneSideInputs.size()/2;

    std::vector<float> & weights = m_currentGaussWeights;
    weights.clear();

    float weightSum = 0.0f;

    for( int i = 0; i < numSamples; i++ )
    {
        float sum = oneSideInputs[i*2+0] + oneSideInputs[i*2+1];
        weights.push_back(sum);

        weightSum += sum;
    }

    std::vector<float> & offsets = m_currentGaussOffsets;
    offsets.clear();

    for( int i = 0; i < numSamples; i++ )
    {
        offsets.push_back( i*2.0f + oneSideInputs[i*2+1] / weights[i] );
    }

    assert( m_currentGaussOffsets.size() == m_currentGaussWeights.size( ) );


    // std::string indent = "    ";
    // 
    // std::string shaderCode = (forPreprocessorDefine)?(""):("");
    // std::string eol = (forPreprocessorDefine)?("\\\n"):("\n");
    // if( !forPreprocessorDefine) shaderCode += indent + "//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////;" + eol;
    // if( !forPreprocessorDefine) shaderCode += indent + stringFormatA( "// Kernel width %d x %d", kernelSize, kernelSize ) + eol;
    // if( !forPreprocessorDefine) shaderCode += indent + "//" + eol;
    // shaderCode += indent + stringFormatA( "const int stepCount = %d;", numSamples ) + eol;
    // 
    // if( !workaroundForNoCLikeArrayInitialization )
    // {
    //     if( !forPreprocessorDefine) shaderCode += indent + "//" + eol;
    //     shaderCode += indent + "const float gWeights[stepCount] ={" + eol;
    //     for( int i = 0; i < numSamples; i++ )
    //         shaderCode += indent + stringFormatA( "   %.5f", weights[i] ) + ((i!=(numSamples-1))?(","):("")) + eol;
    //     shaderCode += indent + "};"+eol;
    //     shaderCode += indent + "const float gOffsets[stepCount] ={"+eol;
    //     for( int i = 0; i < numSamples; i++ )
    //         shaderCode += indent + stringFormatA( "   %.5f", offsets[i] ) + ((i!=(numSamples-1))?(","):("")) + eol;
    //     shaderCode += indent + "};" + eol;
    // }
    // else
    // {
    //     if( !forPreprocessorDefine) shaderCode += indent + "//" + eol;
    //     shaderCode += indent + "float gWeights[stepCount];" + eol;
    //     for( int i = 0; i < numSamples; i++ )
    //         shaderCode += indent + stringFormatA( " gWeights[%d] = %.5f;", i, weights[i] ) + eol;
    //     shaderCode += indent + eol;
    //     shaderCode += indent + "float gOffsets[stepCount];"+eol;
    //     for( int i = 0; i < numSamples; i++ )
    //         shaderCode += indent + stringFormatA( " gOffsets[%d] = %.5f;", i, offsets[i] ) + eol;
    //     shaderCode += indent + eol;
    // }
    // 
    // if( !forPreprocessorDefine) shaderCode += indent + "//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////;" + eol;
    // 
    // return shaderCode;
}

bool vaPostProcessBlur::UpdateKernel( float gaussSigma, int gaussRadius )
{
    gaussSigma = vaMath::Clamp( gaussSigma, 0.1f, 256.0f );
    if( gaussRadius == -1 )
    {
        // The '* 5.0f' is a very ad-hoc heuristic for computing the default kernel (actual kernel is radius * 2 + 1) size so the
        // precision remains good for HDR range of roughly [0, 1000] for sensible sigmas (i.e. up to 100-ish).
        // To do it properly (either compute kernel size so that required % of the curve is within the discrete kernel area, or so 
        // that the edge weight is below min required precision threshold), refer to:
        // http://dev.theomader.com/gaussian-kernel-calculator/ and/or http://reference.wolfram.com/language/ref/GaussianMatrix.html
        gaussRadius = (int)vaMath::Ceil( gaussSigma * 5.0f );
    }
    if( gaussRadius <= 0 )
    {
        assert( false );
        return false;
    }
    if( gaussRadius > 2048 )
    {
        assert( false );    // too large, not supported
        return false;
    }

    // no need to update
    if( ( gaussRadius == m_currentGaussKernelRadius ) && (vaMath::Abs( gaussSigma - m_currentGaussKernelSigma ) < 1e-5f) )
        return true;

    m_constantsBufferNeedsUpdate = true;

    m_currentGaussKernelRadius  = gaussRadius; 
    m_currentGaussKernelSigma   = gaussSigma;

    // just ensure sensible values
    assert( (gaussRadius > gaussSigma) && (gaussRadius < gaussSigma*12.0f) );

    int kernelSize = (int)gaussRadius * 2 + 1;

    {
        vaStackVector< float, 4096 > tmpKernelArray;
        GenerateSeparableGaussKernel( m_currentGaussKernelSigma, kernelSize, tmpKernelArray.container() );
        m_currentGaussKernel.resize( tmpKernelArray.container().size() );
        for( size_t i = 0; i < tmpKernelArray.container().size(); i++ )
            m_currentGaussKernel[i] = (float)tmpKernelArray[i];
    }

    UpdateFastKernelWeightsAndOffsets( );

    return true;
}

void vaPostProcessBlur::UpdateTextures( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & srcTexture )
{
    renderContext; // unreferenced

    vaResourceFormat srcFormat = srcTexture->GetSRVFormat();

    if( ( srcTexture->GetSizeX( ) == m_textureSize.x ) && ( srcTexture->GetSizeY( ) == m_textureSize.y ) && ( srcFormat == m_textureFormat ) )
    {
        m_texturesUpdatedCounter = 0;
        return;
    }

    m_textureSize.x    = srcTexture->GetSizeX( );
    m_textureSize.y    = srcTexture->GetSizeY( );
    m_textureFormat     = srcFormat;

    m_texturesUpdatedCounter++;
    // textures being updated multiple times per frame? that's bad: use separate vaPostProcessBlur instead
    assert( m_texturesUpdatedCounter < 3 ); // this assert isn't very correct - actual frame index should be used too

    m_fullresPingTexture = vaTexture::Create2D( GetRenderDevice(), m_textureFormat, m_textureSize.x, m_textureSize.y, 1, 1, 1, vaResourceBindSupportFlags::UnorderedAccess | vaResourceBindSupportFlags::ShaderResource, vaResourceAccessFlags::Default );
    m_fullresPongTexture = vaTexture::Create2D( GetRenderDevice(), m_textureFormat, m_textureSize.x, m_textureSize.y, 1, 1, 1, vaResourceBindSupportFlags::UnorderedAccess | vaResourceBindSupportFlags::ShaderResource, vaResourceAccessFlags::Default );
    m_lastScratchTexture = nullptr;
}

vaDrawResultFlags vaPostProcessBlur::Blur( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & dstTexture, const shared_ptr<vaTexture> & srcTexture, float gaussSigma, int gaussRadius )
{
    if( !UpdateKernel( gaussSigma, gaussRadius ) )
        return vaDrawResultFlags::UnspecifiedError;

    UpdateTextures( renderContext, srcTexture );

    m_lastScratchTexture = nullptr;
    return BlurInternal( renderContext, dstTexture, srcTexture );
}

// output goes into m_lastScratchTexture which remains valid until next call to Blur or BlurToScratch or device reset
vaDrawResultFlags vaPostProcessBlur::BlurToScratch( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & srcTexture, float gaussSigma, int gaussRadius )
{
    if( !UpdateKernel( gaussSigma, gaussRadius ) )
        return vaDrawResultFlags::UnspecifiedError;

    UpdateTextures( renderContext, srcTexture );

    m_lastScratchTexture = m_fullresPingTexture;
    return BlurInternal( renderContext, m_fullresPingTexture, srcTexture );
}

vaDrawResultFlags vaPostProcessBlur::BlurInternal( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & dstTexture, const shared_ptr<vaTexture> & srcTexture )
{
    vaDrawResultFlags renderResults = vaDrawResultFlags::None;

    // Setup
    UpdateShaders( renderContext );

    vaComputeItem computeItem;

    computeItem.ConstantBuffers[ POSTPROCESS_BLUR_CONSTANTSBUFFERSLOT ]     = m_constantsBuffer;

    computeItem.SetDispatch( ( dstTexture->GetWidth() + 8 - 1 ) / 8, ( dstTexture->GetHeight() + 8 - 1 ) / 8 );

    // Separable Gauss blur
//    if( (int)m_currentGaussOffsets.size( ) > 0 )
    {
        UpdateGPUConstants( renderContext, 0.0f );

        computeItem.ShaderResourceViews[ POSTPROCESS_BLUR_TEXTURE_SLOT0 ]    = srcTexture;

        m_CSGaussHorizontal->WaitFinishIfBackgroundCreateActive();
        computeItem.ComputeShader = m_CSGaussHorizontal;

        renderResults |= renderContext.ExecuteSingleItem( computeItem, vaRenderOutputs::FromUAVs(m_fullresPongTexture), nullptr );

        computeItem.ShaderResourceViews[ POSTPROCESS_BLUR_TEXTURE_SLOT0 ]    = m_fullresPongTexture;

        m_CSGaussVertical->WaitFinishIfBackgroundCreateActive();
        computeItem.ComputeShader = m_CSGaussVertical;

        renderResults |= renderContext.ExecuteSingleItem( computeItem, vaRenderOutputs::FromUAVs(dstTexture), nullptr );
    }

    return renderResults;
}