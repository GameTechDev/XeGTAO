///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaASSAOLite.h"

#include "Core/vaInput.h"

#include "IntegratedExternals/vaImguiIntegration.h"

#include "Rendering/vaRenderDeviceContext.h"

#include "Rendering/vaRenderDevice.h"
#include "Rendering/vaRenderGlobals.h"


using namespace Vanilla;

vaASSAOLite::vaASSAOLite( const vaRenderingModuleParams & params ) : 
    vaRenderingModule( params ),
    vaUIPanel( "ASSAOLite", 10, !VA_MINIMAL_UI_BOOL, vaUIPanel::DockLocation::DockedLeftBottom ),
    m_CSPrepareDepthsAndNormals( params ),
    m_CSGenerate                                            { (params), (params), (params) },//(params), (params) },
    m_CSSmartBlur(params),
    m_CSApply(params),
    m_constantBuffer( vaConstantBuffer::Create<ASSAO::ASSAOConstants>( params.RenderDevice, "ASSAOLiteConstants" ) )
{ 
    m_size = m_halfSize = vaVector2i( 0, 0 ); 
	m_enableMLSSAO					= false;
    m_debugShowNormals              = false; 
    m_debugShowEdges                = false;
}

void vaASSAOLite::UIPanelTick( vaApplicationBase & )
{
#ifdef VA_IMGUI_INTEGRATION_ENABLED

    // Keyboard input (but let the ImgUI controls have input priority)

    ImGui::PushItemWidth( 120.0f );

    ASSAO::ASSAOImGuiSettings( m_settings );

    ImGui::Separator( );
    ImGui::Text( m_debugInfo.c_str( ) );
    ImGui::Separator( );
    
    ImGui::PopItemWidth( );

#endif
}

// TODO: remove this, it's no longer required for the few number of textures we have now
bool vaASSAOLite::ReCreateIfNeeded( shared_ptr<vaTexture> & inoutTex, vaVector2i size, vaResourceFormat format, float & inoutTotalSizeSum, int mipLevels, int arraySize )
{
    int approxSize = size.x * size.y * vaResourceFormatHelpers::GetPixelSizeInBytes( format );
    if( mipLevels != 1 ) approxSize = approxSize * 2; // is this an overestimate?
    inoutTotalSizeSum += approxSize;

    vaResourceBindSupportFlags bindFlags = vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess;

    if( ( size.x == 0 ) || ( size.y == 0 ) || ( format == vaResourceFormat::Unknown ) )
    {
        inoutTex = nullptr;
    }
    else
    {
        vaResourceFormat resourceFormat = format;
        vaResourceFormat srvFormat = format;
        vaResourceFormat rtvFormat = vaResourceFormat::Unknown;
        vaResourceFormat dsvFormat = vaResourceFormat::Unknown;
        vaResourceFormat uavFormat = format;

        // handle special cases
        if( format == vaResourceFormat::D32_FLOAT )
        {
            bindFlags = ( bindFlags & ~( vaResourceBindSupportFlags::RenderTarget ) ) | vaResourceBindSupportFlags::DepthStencil;
            resourceFormat = vaResourceFormat::R32_TYPELESS;
            srvFormat = vaResourceFormat::R32_FLOAT;
            dsvFormat = vaResourceFormat::D32_FLOAT;
        }
        if( format == vaResourceFormat::R8G8B8A8_UNORM_SRGB )
        {
            resourceFormat = vaResourceFormat::R8G8B8A8_TYPELESS;
            srvFormat = vaResourceFormat::R8G8B8A8_UNORM_SRGB;
        }

        if( (inoutTex != nullptr) && (inoutTex->GetSizeX() == size.x) && (inoutTex->GetSizeY()==size.y) &&
            (inoutTex->GetResourceFormat()==resourceFormat) && (inoutTex->GetSRVFormat()==srvFormat) && (inoutTex->GetRTVFormat()==rtvFormat) && (inoutTex->GetDSVFormat()==dsvFormat) && (inoutTex->GetUAVFormat()==uavFormat) )
            return false;

        inoutTex = vaTexture::Create2D( GetRenderDevice(), resourceFormat, size.x, size.y, mipLevels, arraySize, 1, bindFlags, vaResourceAccessFlags::Default, srvFormat, rtvFormat, dsvFormat, uavFormat );
    }

    return true;
}

void vaASSAOLite::UpdateWorkingTextures( int width, int height, bool generateNormals )
{
    std::vector< pair< string, string > > newShaderMacros;

	if (m_enableMLSSAO)
        { assert( false ); } // TODO: re-add ML part // newShaderMacros.push_back(std::pair<std::string, std::string>("ASSAO_ENABLE_MLSSAO", ""));

    if( m_debugShowNormals )
        newShaderMacros.push_back( std::pair<std::string, std::string>( "ASSAO_DEBUG_SHOWNORMALS", "" ) );
    if( m_debugShowEdges )
        newShaderMacros.push_back( std::pair<std::string, std::string>( "ASSAO_DEBUG_SHOWEDGES", "" ) );
    if( generateNormals )
        newShaderMacros.push_back( std::pair<std::string, std::string>( "ASSAO_GENERATE_NORMALS", "" ) );

    if( m_specialShaderMacro != pair<string, string>(std::make_pair( "", "" )) )
        newShaderMacros.push_back( m_specialShaderMacro );

    if( newShaderMacros != m_staticShaderMacros )
    {
        m_staticShaderMacros = newShaderMacros;
        m_shadersDirty = true;
    }

    if( m_shadersDirty )
    {
        m_shadersDirty = false;

        string shaderFileToUse = "vaASSAOLite.hlsl";
        
        // to allow parallel background compilation but still ensure they're all compiled after this function
        std::vector<shared_ptr<vaShader>> allShaders;
        allShaders.push_back( m_CSPrepareDepthsAndNormals.get() );
        for( auto sh : m_CSGenerate ) allShaders.push_back( sh.get() );
        allShaders.push_back( m_CSSmartBlur.get() );
        allShaders.push_back( m_CSApply.get() );

        m_CSPrepareDepthsAndNormals->CompileFromFile( shaderFileToUse, "CSPrepareDepthsAndNormals", m_staticShaderMacros, false );

        m_CSGenerate[0]->CompileFromFile( shaderFileToUse, "CSGenerateQ0", m_staticShaderMacros, false );
        m_CSGenerate[1]->CompileFromFile( shaderFileToUse, "CSGenerateQ1", m_staticShaderMacros, false );
        m_CSGenerate[2]->CompileFromFile( shaderFileToUse, "CSGenerateQ2", m_staticShaderMacros, false );

        m_CSSmartBlur->CompileFromFile( shaderFileToUse, "CSSmartBlur", m_staticShaderMacros, false );
        m_CSApply->CompileFromFile( shaderFileToUse, "CSApply", m_staticShaderMacros, false );

        // wait until shaders are compiled! this allows for parallel compilation
        for( auto sh : allShaders ) sh->WaitFinishIfBackgroundCreateActive();
    }

    bool needsUpdate = false;

    if( !generateNormals )
    {
        needsUpdate |= m_normals != nullptr;
        m_normals = nullptr;
    }
    else
        needsUpdate = m_normals == nullptr;

    needsUpdate |= (m_size.x != width) || (m_size.y != height);
    needsUpdate |= (m_debugShowNormals || m_debugShowEdges) == (m_debugImage == nullptr);

    m_size.x        = width;
    m_size.y        = height;
    m_halfSize.x    = (width+1)/2;
    m_halfSize.y    = (height+1)/2;

    if( !needsUpdate )
        return;
    
    vaResourceFormat WorkingFormat = vaResourceFormat::R8G8_UNORM;
    vaResourceFormat NormalsFormat = vaResourceFormat::R8G8B8A8_UNORM;

    float totalSizeInMB = 0.0f;

    if( ReCreateIfNeeded( m_workingDepthsAll, m_halfSize, m_depthViewspaceFormat, totalSizeInMB, ASSAO_DEPTH_MIP_LEVELS, 4 ) )
    {
        for( int mip = 0; mip < ASSAO_DEPTH_MIP_LEVELS; mip++ )
            m_workingDepthsMipViews[mip] = vaTexture::CreateView( m_workingDepthsAll, m_workingDepthsAll->GetBindSupportFlags( ), vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::None, mip, 1 );
    }

    ReCreateIfNeeded( m_pingPongWorkingA, m_halfSize, WorkingFormat, totalSizeInMB, 1, 4 );
    ReCreateIfNeeded( m_pingPongWorkingB, m_halfSize, WorkingFormat, totalSizeInMB, 1, 4 );
    
    if( generateNormals )
        ReCreateIfNeeded( m_normals, m_size, NormalsFormat, totalSizeInMB, 1, 1 );

    // this is only needed for visual debugging
    if( m_debugShowNormals || m_debugShowEdges )
        ReCreateIfNeeded( m_debugImage, m_size, vaResourceFormat::R11G11B10_FLOAT, totalSizeInMB, 1, 1 );
    else
        m_debugImage = nullptr;

    totalSizeInMB /= 1024 * 1024;

    m_debugInfo = vaStringTools::Format( "Approx. %.2fMB video memory used.", totalSizeInMB );
}

void vaASSAOLite::UpdateConstants( vaRenderDeviceContext & renderContext, const vaMatrix4x4 & viewMatrix, const vaMatrix4x4 & projMatrix )
{
    ASSAO::ASSAOConstants consts;

    ASSAO::ASSAOUpdateConstants( consts, m_size.x, m_size.y, m_settings, &viewMatrix._11, &projMatrix._11, true );
      
    m_constantBuffer->Upload<ASSAO::ASSAOConstants>( renderContext, consts );
}

vaDrawResultFlags vaASSAOLite::Compute( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & outputAO, const vaMatrix4x4 & viewMatrix, const vaMatrix4x4 & projMatrix, const shared_ptr<vaTexture> & inputDepth, const shared_ptr<vaTexture> & inputNormals )
{
    assert( outputAO->GetSize( ) == inputDepth->GetSize( ) );
    assert( m_settings.QualityLevel >= 0 && m_settings.QualityLevel <= 2 );
    assert( inputDepth->GetSampleCount( ) == 1 ); // MSAA no longer supported!

    UpdateWorkingTextures( inputDepth->GetSizeX( ), inputDepth->GetSizeY( ), inputNormals == nullptr );

    VA_TRACE_CPUGPU_SCOPE( ASSAO, renderContext );

    const shared_ptr<vaTexture> & workingNormals = (inputNormals==nullptr)?(m_normals):(inputNormals);

    assert( ((workingNormals->GetSizeX() == m_size.x) || (workingNormals->GetSizeX() == m_size.x-1)) && ( (workingNormals->GetSizeY() == m_size.y) || (workingNormals->GetSizeY() == m_size.y-1)) );
    assert( !m_shadersDirty ); if( m_shadersDirty ) return vaDrawResultFlags::UnspecifiedError;

    UpdateConstants( renderContext, viewMatrix, projMatrix );

    vaComputeItem computeItem;
    vaRenderOutputs computeOutputs;

    // these are used by all passes
    computeItem.ConstantBuffers[ASSAO_CONSTANTBUFFER_SLOT] = m_constantBuffer;

    // since we're transitioning input/output resources by switching between SRV<->UAVs, we don't need any additional barriers
    computeItem.GlobalUAVBarrierBefore = false; computeItem.GlobalUAVBarrierAfter = false;

    // Prepare (convert to viewspace, deinterleave) depths and generate normals (if needed)
    {
        VA_TRACE_CPUGPU_SCOPE( PrepareDepthsAndNormals, renderContext );

        bool generateNormals = (inputNormals==nullptr);
            
        computeOutputs.UnorderedAccessViews[ASSAO_UAV_DEPTHS_SLOT]       = m_workingDepthsAll;
        computeOutputs.UnorderedAccessViews[ASSAO_UAV_DEPTHS_MIP1_SLOT]  = m_workingDepthsMipViews[1];
        computeOutputs.UnorderedAccessViews[ASSAO_UAV_DEPTHS_MIP2_SLOT]  = m_workingDepthsMipViews[2];
        computeOutputs.UnorderedAccessViews[ASSAO_UAV_DEPTHS_MIP3_SLOT]  = m_workingDepthsMipViews[3];
        computeOutputs.UnorderedAccessViews[ASSAO_UAV_NORMALMAP_SLOT]    = ( generateNormals ) ? ( m_normals ) : ( nullptr );
        computeItem.ShaderResourceViews[ASSAO_SRV_SOURCE_NDC_DEPTH_SLOT] = inputDepth;
        computeItem.ComputeShader = m_CSPrepareDepthsAndNormals;
        computeItem.SetDispatch( ( m_halfSize.x + ASSAO_NUMTHREADS_X - 1 ) / ASSAO_NUMTHREADS_X, ( m_halfSize.y + ASSAO_NUMTHREADS_Y - 1 ) / ASSAO_NUMTHREADS_Y, 1 );
        renderContext.ExecuteSingleItem( computeItem, computeOutputs, nullptr );
    }

    // We can read these now
    computeItem.ShaderResourceViews[ASSAO_SRV_WORKING_DEPTH_SLOT]    = m_workingDepthsAll;
    computeItem.ShaderResourceViews[ASSAO_SRV_SOURCE_NORMALMAP_SLOT] = workingNormals;
    // But we can't write to them anymore (can't have the same texture selected as UAV and SRV at the same time)
    computeOutputs.UnorderedAccessViews[ASSAO_UAV_DEPTHS_SLOT]       = nullptr;
    computeOutputs.UnorderedAccessViews[ASSAO_UAV_DEPTHS_MIP1_SLOT]  = nullptr;
    computeOutputs.UnorderedAccessViews[ASSAO_UAV_DEPTHS_MIP2_SLOT]  = nullptr;
    computeOutputs.UnorderedAccessViews[ASSAO_UAV_DEPTHS_MIP3_SLOT]  = nullptr;
    computeOutputs.UnorderedAccessViews[ASSAO_UAV_NORMALMAP_SLOT]    = nullptr;

    // only for debugging!
    if( m_debugShowNormals || m_debugShowEdges )
    {
        computeOutputs.UnorderedAccessViews[ASSAO_UAV_DEBUG_IMAGE_SLOT] = m_debugImage;
    }

    // Generate SSAO
    bool readFromA = true;
    {
        VA_TRACE_CPUGPU_SCOPE( GenerateAndBlur, renderContext );

        {
            VA_TRACE_CPUGPU_SCOPE( Generate, renderContext );

            computeItem.ComputeShader = m_CSGenerate[vaMath::Max( 0, m_settings.QualityLevel )];
            computeItem.SetDispatch( ( m_halfSize.x + ASSAO_NUMTHREADS_X - 1 ) / ASSAO_NUMTHREADS_X, ( m_halfSize.y + ASSAO_NUMTHREADS_Y - 1 ) / ASSAO_NUMTHREADS_Y, 4 / ASSAO_NUMTHREADS_LAYERED_Z );
            computeOutputs.UnorderedAccessViews[ASSAO_UAV_OCCLUSION_EDGE_SLOT] = m_pingPongWorkingA;
            renderContext.ExecuteSingleItem( computeItem, computeOutputs, nullptr );
        }

        int blurPasses = vaMath::Min( m_settings.BlurPassCount, ASSAO_MAX_BLUR_PASS_COUNT );
        // Blur
        if( blurPasses > 0 )
        {
            VA_TRACE_CPUGPU_SCOPE( Blur, renderContext );

            for( int i = 0; i < blurPasses; i++ )
            {
                shared_ptr<vaTexture>* pFromTex = ( readFromA ) ? ( &m_pingPongWorkingA ) : ( &m_pingPongWorkingB );
                shared_ptr<vaTexture>* pToTex   = ( readFromA ) ? ( &m_pingPongWorkingB ) : ( &m_pingPongWorkingA );
                readFromA = !readFromA;

                computeItem.ComputeShader = m_CSSmartBlur;
                computeOutputs.UnorderedAccessViews[ASSAO_UAV_OCCLUSION_EDGE_SLOT]       = *pToTex;
                computeItem.ShaderResourceViews[ASSAO_SRV_WORKING_OCCLUSION_EDGE_SLOT]   = *pFromTex;
                computeItem.SetDispatch( ( m_halfSize.x + ASSAO_NUMTHREADS_X - 1 ) / ASSAO_NUMTHREADS_X, ( m_halfSize.y + ASSAO_NUMTHREADS_Y - 1 ) / ASSAO_NUMTHREADS_Y, 4 / ASSAO_NUMTHREADS_LAYERED_Z );
                renderContext.ExecuteSingleItem( computeItem, computeOutputs, nullptr );
            }
        }
        computeOutputs.UnorderedAccessViews[ASSAO_UAV_OCCLUSION_EDGE_SLOT] = nullptr;    // can't have the same texture selected as UAV and SRV at the same time
    }

    // Apply (take 4 deinterleaved AO textures in the texture array, and merge & output!)
    {
        VA_TRACE_CPUGPU_SCOPE( Apply, renderContext );

        computeItem.ComputeShader = m_CSApply;            // 'apply' shader
        computeOutputs.UnorderedAccessViews[ASSAO_UAV_FINAL_OCCLUSION_SLOT] = outputAO;
        computeItem.ShaderResourceViews[ASSAO_SRV_WORKING_OCCLUSION_EDGE_SLOT] = (readFromA)?(m_pingPongWorkingA):(m_pingPongWorkingB);
        computeItem.SetDispatch( (m_size.x + ASSAO_NUMTHREADS_X-1) / ASSAO_NUMTHREADS_X, (m_size.y + ASSAO_NUMTHREADS_Y-1) / ASSAO_NUMTHREADS_Y, 1 );
        renderContext.ExecuteSingleItem( computeItem, computeOutputs, nullptr );
    }

    return vaDrawResultFlags::None;
}


