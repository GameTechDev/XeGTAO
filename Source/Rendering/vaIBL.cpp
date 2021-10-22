///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaIBL.h"

#include "Rendering/vaTexture.h"

#include "Rendering/Shaders/Lighting/vaLightingShared.h"

#include "Rendering/vaRenderingIncludes.h"

#include "Rendering/Shaders/Lighting/vaIBLShared.h"

#include "Core/System/vaFileTools.h"
#include "Core/vaApplicationBase.h"

#include "Rendering/Shaders/vaSharedTypes.h"

#include "Rendering/vaTextureHelpers.h"

#include "IntegratedExternals/vaImguiIntegration.h"

#include "Rendering/Effects/vaPostProcess.h"

#include "Rendering/Effects/vaSkybox.h"

#include "Rendering/vaDebugCanvas.h"

#include <fstream>

using namespace Vanilla;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

vaIBLProbe::vaIBLProbe( vaRenderDevice & renderDevice ) : vaRenderingModule( vaRenderingModuleParams(renderDevice) ), 
    vaUIPanel( "DistantIBL", 0, false, vaUIPanel::DockLocation::DockedLeftBottom )
{
    m_reflectionsPreFilter      = std::make_shared<vaIBLCubemapPreFilter>( renderDevice );
    m_irradiancePreFilter       = std::make_shared<vaIBLCubemapPreFilter>( renderDevice );
    m_irradianceSHCalculator    = std::make_shared<vaIrradianceSHCalculator>( renderDevice );

    Reset( );
}

vaIBLProbe::~vaIBLProbe( )
{

}

void vaIBLProbe::Reset( )
{
    m_reflectionsMap        = nullptr;
    m_irradianceMap         = nullptr;
    m_skyboxTexture         = nullptr;

    //m_roughnessPreFilter    = nullptr;
    m_reflectionsPreFilter->Reset();
    m_irradiancePreFilter->Reset();
    m_irradianceSHCalculator->Reset();

    m_intensity             = 1.0f;
    std::fill(std::begin(m_irradianceCoefs), std::end(m_irradianceCoefs), vaVector3(0,0,0));

    m_capturedData          = Scene::IBLProbe( );
    m_fullImportedPath      = "";
    m_hasContents           = false;
}

void vaIBLProbe::UpdateShaderConstants( /*vaRenderDeviceContext & renderContext, */const vaDrawAttributes & drawAttributes, IBLProbeConstants & consts )
{
    //vaMatrix4x4 mat = drawContext.Camera.GetViewMatrix( ) * drawContext.Camera.GetProjMatrix( );
    
    if( m_hasContents )
    {
        assert( m_reflectionsMap != nullptr );

        // // shaders no longer support the multiplication here - saves some cycles
        // assert( m_rotation == vaMatrix3x3::Identity );

        auto proxyBaseless = m_capturedData.GeometryProxy; 
        proxyBaseless.Center -= drawAttributes.Settings.WorldBase;

        auto fadeoutProxyBaseless = m_capturedData.FadeOutProxy;
        fadeoutProxyBaseless.Center -= drawAttributes.Settings.WorldBase;

        consts.WorldToGeometryProxy = vaMatrix4x4( proxyBaseless.ToScaledTransform() ).Inversed();
        consts.WorldToFadeoutProxy  = vaMatrix4x4( fadeoutProxyBaseless.ToScaledTransform() ).Inversed();
        consts.Enabled              = 1;
        consts.PreExposedLuminance  = m_intensity * drawAttributes.Camera.GetPreExposureMultiplier( true );
        consts.MaxReflMipLevel      = (float)m_maxReflMIPLevel;
        consts.Pow2MaxReflMipLevel  = (float)(1 << (uint)consts.MaxReflMipLevel);
        consts.Position             = m_capturedData.Position - drawAttributes.Settings.WorldBase;
        consts.ReflMipLevelClamp    = consts.MaxReflMipLevel - 0.5f;
        consts.Extents              = vaVector3(0,0,0);//m_capturedData.Location.Extents;
        consts.UseProxy             = m_capturedData.UseGeometryProxy;
        //consts.
        
        for( int i = 0; i < m_irradianceCoefs.size(); i++ ) 
            consts.DiffuseSH[i] = vaVector4( m_irradianceCoefs[i], 0.0f );
    }
    else
    {
        consts.Enabled              = 0;
    }

    // m_constantBuffer.Update( drawContext.RenderDeviceContext, consts );
}

void vaIBLProbe::SetToGlobals( vaShaderItemGlobals& shaderItemGlobals, bool distantIBLSlots )
{
    if( !m_hasContents )
        return;

    // UpdateShaderConstants( drawContext );
    // assert( distantIBLSlots == !m_capturedData.UseProxy );

    if( !distantIBLSlots )
    {
        assert( shaderItemGlobals.ShaderResourceViews[LIGHTINGGLOBAL_LOCALIBL_IRRADIANCEMAP_TEXTURESLOT] == nullptr );
        shaderItemGlobals.ShaderResourceViews[LIGHTINGGLOBAL_LOCALIBL_IRRADIANCEMAP_TEXTURESLOT] = m_irradianceMap;
        assert( shaderItemGlobals.ShaderResourceViews[ LIGHTINGGLOBAL_LOCALIBL_REFROUGHMAP_TEXTURESLOT] == nullptr );
        shaderItemGlobals.ShaderResourceViews[ LIGHTINGGLOBAL_LOCALIBL_REFROUGHMAP_TEXTURESLOT] = m_reflectionsMap;
    }
    else
    {
        assert( shaderItemGlobals.ShaderResourceViews[LIGHTINGGLOBAL_DISTANTIBL_IRRADIANCEMAP_TEXTURESLOT] == nullptr );
        shaderItemGlobals.ShaderResourceViews[LIGHTINGGLOBAL_DISTANTIBL_IRRADIANCEMAP_TEXTURESLOT] = m_irradianceMap;
        assert( shaderItemGlobals.ShaderResourceViews[LIGHTINGGLOBAL_DISTANTIBL_REFROUGHMAP_TEXTURESLOT] == nullptr );
        shaderItemGlobals.ShaderResourceViews[LIGHTINGGLOBAL_DISTANTIBL_REFROUGHMAP_TEXTURESLOT] = m_reflectionsMap;
    }
}

shared_ptr<vaTexture> vaIBLProbe::ImportCubemap( vaRenderDeviceContext & renderContext, const string & path, uint32 outputBaseSize )
{
    auto srcImage = vaTexture::CreateFromImageFile( GetRenderDevice( ), path );
    if( srcImage != nullptr )
    {
        if( srcImage->IsCubemap() ) // no processing required, already a cubemap, great!
            return srcImage;

        uint32 width  = srcImage->GetSizeX();
        uint32 height = srcImage->GetSizeY();
        if( ( vaMath::IsPowOf2( width ) && ( width * 3 == height * 4 ) ) ||
            ( vaMath::IsPowOf2( height ) && ( height * 3 == width * 4 ) ) ) 
        {
            // This is cross cubemap
            VA_WARN( "vaIBLProbe::ImportCubemap failed to load '%s', cross cubemap not yet supported (%d x %d)", path.c_str( ), width, height );
            return nullptr;
            // size_t dim = g_output_size ? g_output_size : IBL_DEFAULT_SIZE;
            // if( !g_quiet ) {
            //     std::cout << "Loading cross... " << std::endl;
            // }
            // 
            // Image temp;
            // Cubemap cml = CubemapUtils::create( temp, dim );
            // CubemapUtils::crossToCubemap( js, cml, inputImage );
            // images.push_back( std::move( temp ) );
            // levels.push_back( std::move( cml ) );
        }
        else if( width == 2 * height ) 
        {
            const uint32 threadGroupSizeXY = 16;

            if( outputBaseSize == 0 )
                outputBaseSize = vaMath::PowOf2Ceil(height);

            // we assume a spherical (equirectangular) image, which we will convert to a cross image
            if( !vaMath::IsPowOf2( outputBaseSize ) || ( ( outputBaseSize % threadGroupSizeXY ) != 0 ) )
            {
                VA_WARN( "vaIBLProbe::ImportCubemap failed to load '%s', requested outputBaseSize (%d) not valid ", path.c_str( ), outputBaseSize );
                return nullptr;
            }

            {
                if( m_CSEquirectangularToCubemap == nullptr )
                {
                    m_CSEquirectangularToCubemap = GetRenderDevice().CreateModule< vaComputeShader>(); // get the platform dependent object
                    m_CSEquirectangularToCubemap->CompileFromFile( "Lighting/vaIBL.hlsl", "CSEquirectangularToCubemap", { }, true );

                    //m_CSEquirectangularToCubemap->WaitFinishIfBackgroundCreateActive( );
                }

                auto firstPass = vaTexture::Create2D( GetRenderDevice( ), vaResourceFormat::R16G16B16A16_FLOAT, outputBaseSize, outputBaseSize, 1, 6, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess, 
                                                        vaResourceAccessFlags::Default, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::Cubemap );

                int cubeDim = firstPass->GetSizeX( );
                assert( ( cubeDim % threadGroupSizeXY ) == 0 );

                vaComputeItem computeItem;
                vaRenderOutputs outputs;
                computeItem.ComputeShader                                       = m_CSEquirectangularToCubemap;
                outputs.UnorderedAccessViews[IBL_FILTER_CUBE_FACES_ARRAY_UAV_SLOT] = firstPass;
                computeItem.ShaderResourceViews[IBL_FILTER_GENERIC_TEXTURE_SLOT_0]     = srcImage;

                computeItem.SetDispatch( cubeDim / threadGroupSizeXY, cubeDim / threadGroupSizeXY, 6 );
                renderContext.ExecuteSingleItem( computeItem, outputs, nullptr );

                return firstPass;
            }

            // Image temp;
            // Cubemap cml = CubemapUtils::create( temp, dim );
            // CubemapUtils::equirectangularToCubemap( js, cml, inputImage );
            // images.push_back( std::move( temp ) );
            // levels.push_back( std::move( cml ) );
        }
        else 
        {
            VA_WARN( "vaIBLProbe::ImportCubemap failed to load '%s', aspect ratio not supported (%d x %d)", path.c_str(), width, height );
            // std::cerr << "Aspect ratio not supported: " << width << "x" << height << std::endl;
            // std::cerr << "Supported aspect ratios:" << std::endl;
            // std::cerr << "  2:1, lat/long or equirectangular" << std::endl;
            // std::cerr << "  3:4, vertical cross (height must be power of two)" << std::endl;
            // std::cerr << "  4:3, horizontal cross (width must be power of two)" << std::endl;
            // exit( 0 );
            return nullptr;
        }
    }
    else
    {
        VA_WARN( "vaIBLProbe::ImportCubemap failed to load '%s'", path.c_str() );
    }
    return srcImage;
}

bool vaIBLProbe::Import( vaRenderDeviceContext & renderContext, const Scene::IBLProbe & probeData )
{
    Reset( );
    renderContext; 
    string srcFilePath = vaFileTools::CleanupPath( probeData.ImportFilePath, false, false );

    if( !vaFileTools::FileExists( srcFilePath ) )
        srcFilePath = vaCore::GetMediaRootDirectoryNarrow() + srcFilePath;

    if( !vaFileTools::FileExists( srcFilePath ) )
    {
        VA_WARN( "vaIBLProbe::Import - file '%s' could not be found", probeData.ImportFilePath.c_str() );
        //assert( false );
        return false;
    }

    auto srcCube = ImportCubemap( renderContext, srcFilePath, 0 );

    if( srcCube == nullptr )
    {
        assert( false );
        return false;
    }

    m_fullImportedPath  = srcFilePath;
    m_capturedData      = probeData;
    // m_capturedData.Location.Axis = vaMatrix3x3::Identity;
    m_intensity = 1.0f; // really arbitrary, possibly just keep it at 1?
    
    auto retValue = Process( renderContext, srcCube );
    if( !retValue )
        Reset();
    return retValue;
}

bool vaIBLProbe::Process( vaRenderDeviceContext & renderContext, const shared_ptr<vaTexture> & srcCube )
{
    int reflRes = vaIBLCubemapPreFilter::c_defaultReflRoughCubeFirstMIPSize;

    m_skyboxTexture = vaTexture::Create2D( GetRenderDevice( ), m_skyboxFormat, srcCube->GetSizeX(), srcCube->GetSizeY(), 0, 6, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget,
        vaResourceAccessFlags::Default, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::Cubemap );

    vaRenderOutputs outputs;
    vaVector4 colorAdd = { m_capturedData.AmbientColor * m_capturedData.AmbientColorIntensity, 0.0f };
    for( int face = 0; face < 6; face++ )
    {
        auto facemip0ViewSrc = vaTexture::CreateView( srcCube, srcCube->GetBindSupportFlags( ), vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::None,
            0, 1, face, 1 );
        auto facemip0ViewDst = vaTexture::CreateView( m_skyboxTexture, m_skyboxTexture->GetBindSupportFlags( ), vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::None,
            0, 1, face, 1 );

        auto result = renderContext.StretchRect( facemip0ViewDst, facemip0ViewSrc, vaVector4::Zero, vaVector4::Zero, false, vaBlendMode::Opaque, {1,1,1,1}, colorAdd );
        if( result != vaDrawResultFlags::None )
        {
            assert( false ); Reset( ); return false;
        }
    }

    GetRenderDevice( ).GetPostProcess( ).GenerateCubeMIPs( renderContext, m_skyboxTexture );

    //////////////////////////////////////////////////////////////////////////
    // irradiance
    {
#if IBL_IRRADIANCE_SOURCE == IBL_IRRADIANCE_SH
        const int irradianceSrcRes = 128;
        int levelDiff = std::max( 0, vaMath::FloorLog2( m_skyboxTexture->GetSizeX( ) / irradianceSrcRes ) );
        auto irradianceInputCubeView = vaTexture::CreateView( m_skyboxTexture, vaTextureFlags::None, levelDiff, 1, 0, 6 );

        m_irradianceSHCalculator->ComputeSH( renderContext, irradianceInputCubeView );

        std::array<vaVector3, 9> SH = m_irradianceSHCalculator->GetSH( renderContext );
        const int numCoefs = vaIrradianceSHCalculator::c_numSHBands * vaIrradianceSHCalculator::c_numSHBands;
        for( int i = 0; i < numCoefs; i++ )
        {
            // assert( vaVector3::NearEqual( m_irradianceCoefs[i], SH[i], 0.0001f ) );
            m_irradianceCoefs[i] = SH[i];
        }
#elif IBL_IRRADIANCE_SOURCE == IBL_IRRADIANCE_CUBEMAP
        const int irradianceRes = vaIBLCubemapPreFilter::c_defaultIrradianceBaseCubeSize;
        int levelDiff = std::max( 0u, vaMath::FloorLog2( m_skyboxTexture->GetSizeX( ) / irradianceRes ) );
        auto irradianceInputCubeView = vaTexture::CreateView( m_skyboxTexture, vaTextureFlags::Cubemap, levelDiff );

        m_irradianceMap = vaTexture::Create2D( GetRenderDevice( ), m_irradianceMapFormat, irradianceRes, irradianceRes, 0, 6, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess | vaResourceBindSupportFlags::RenderTarget, vaResourceAccessFlags::Default
            , vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::Cubemap );

        std::vector<shared_ptr<vaTexture>> destCubeMIPLevels;
        destCubeMIPLevels.push_back( vaTexture::CreateView( m_irradianceMap, vaTextureFlags::Cubemap, 0, 1, 0, 6 ) );

        m_irradiancePreFilter->Init( irradianceRes, irradianceRes, vaIBLCubemapPreFilter::c_defaultSamplesPerTexel, vaIBLCubemapPreFilter::FilterType::Irradiance );
        m_irradiancePreFilter->Process( renderContext, destCubeMIPLevels, irradianceInputCubeView );

        GetRenderDevice( ).GetPostProcess( ).GenerateCubeMIPs( renderContext, m_irradianceMap );
#else
#error IBL_IRRADIANCE_SOURCE not correctly defined / supported
#endif
    }
    //////////////////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////////////////////
    // reflections
    {
        //assert( m_skyboxTexture->GetSizeX( ) >= m_reflectionsMap->GetSizeX( ) );
        int levelDiff = std::max( 0u, vaMath::FloorLog2( m_skyboxTexture->GetSizeX( ) / reflRes ) );
        shared_ptr<vaTexture> srcView = vaTexture::CreateView( m_skyboxTexture, vaTextureFlags::Cubemap, levelDiff );
        int reflMIPs = vaMath::FloorLog2( reflRes ) + 1;

#if IBL_INTEGRATION_ALGORITHM == IBL_INTEGRATION_PREFILTERED_CUBEMAP
        reflMIPs -= vaMath::FloorLog2( vaIBLCubemapPreFilter::c_defaultReflRoughCubeLastMIPSize );
        m_reflectionsMap = vaTexture::Create2D( GetRenderDevice( ), m_reflectionsMapFormat, reflRes, reflRes, reflMIPs, 6, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess, vaResourceAccessFlags::Default
            , vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::Cubemap );
        m_maxReflMIPLevel = reflMIPs - 1;
        std::vector<shared_ptr<vaTexture>> destCubeMIPLevels;
        for( int i = 0; i < reflMIPs; i++ )
            destCubeMIPLevels.push_back( vaTexture::CreateView( m_reflectionsMap, vaTextureFlags::Cubemap, i, 1, 0, 6 ) );
        
        m_reflectionsPreFilter->Init( reflRes, vaIBLCubemapPreFilter::c_defaultReflRoughCubeLastMIPSize, vaIBLCubemapPreFilter::c_defaultSamplesPerTexel, vaIBLCubemapPreFilter::FilterType::ReflectionsRoughness );
        m_reflectionsPreFilter->Process( renderContext, destCubeMIPLevels, srcView );

#elif IBL_INTEGRATION_ALGORITHM == IBL_INTEGRATION_IMPORTANCE_SAMPLING
        m_reflectionsMap = vaTexture::Create2D( GetRenderDevice( ), vaResourceFormat::R16G16B16A16_FLOAT, reflRes, reflRes, reflMIPs, 6, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget, vaResourceAccessFlags::Default
            , vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::Cubemap );
        m_maxReflMIPLevel = reflMIPs - 1;

        for( int face = 0; face < 6; face++ )
        {
            auto facemip0ViewSrc = vaTexture::CreateView( m_skyboxTexture, vaTextureFlags::None, levelDiff, 1, face, 1 );
            auto facemip0ViewDst = vaTexture::CreateView( m_reflectionsMap, vaTextureFlags::None, 0, 1, face, 1 );

            auto result = renderContext.CopySRVToRTV( facemip0ViewDst, facemip0ViewSrc );
            if( result != vaDrawResultFlags::None )
            {
                assert( false ); Reset( ); return false;
            }
        }
        GetRenderDevice( ).GetPostProcess( ).GenerateCubeMIPs( renderContext, m_reflectionsMap );
#else
    #error IBL_INTEGRATION_ALGORITHM not correctly defined / supported
#endif
    }
  
    // ok just for debugging show 1 level
    //m_skyboxTexture = vaTexture::CreateView( m_reflectionsMap, vaTextureFlags::Cubemap, 7, 1 );
    //m_skyboxTexture = vaTexture::CreateView( m_skyboxTexture, vaTextureFlags::Cubemap, 1, 1 );
    //
    //////////////////////////////////////////////////////////////////////////

    m_hasContents = true;
    
    return true;
}

void vaIBLProbe::SetToSkybox( vaSkybox & skybox )
{
    assert( HasContents() );
    if( !HasContents() )
        return;

    skybox.SetCubemap( m_skyboxTexture );
    skybox.Settings().Rotation          = vaMatrix3x3::Identity;// m_capturedData.Orientation;
    skybox.Settings().ColorMultiplier   = m_intensity;
}

void vaIBLProbe::UIPanelTick( vaApplicationBase& application )
{
    application;
#ifdef VA_IMGUI_INTEGRATION_ENABLED

    ImGui::Text( "Enabled: %s", ((HasContents())?("true"):("false")) );
    if( ImGui::Button( "Reset", {-1, 0} ) )
        Reset();
#endif // VA_IMGUI_INTEGRATION_ENABLED
}

vaDrawResultFlags vaIBLProbe::Capture( vaRenderDeviceContext & renderContext, const Scene::IBLProbe & captureData, const CubeFaceCaptureCallback & faceCapture, int cubeFaceResolution )
{
    Reset( );
    
    m_capturedData                  = captureData;
    const vaVector3 & position      = captureData.Position;
    const vaMatrix3x3 & rotation    = vaMatrix3x3::Identity; rotation; // captureData.Orientation;
    float clipNear                  = captureData.ClipNear;
    float clipFar                   = captureData.ClipFar;

    auto captureCube = vaTexture::Create2D( GetRenderDevice( ), vaResourceFormat::R16G16B16A16_FLOAT, cubeFaceResolution, cubeFaceResolution, 1, 6, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget,
        vaResourceAccessFlags::Default, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::Cubemap );

    shared_ptr<vaTexture> cubeFaceViews[6];

    for( int face = 0; face < 6; face++ )
        cubeFaceViews[face] = vaTexture::CreateView( captureCube, vaTextureFlags::None, 0, 1, face, 1 );

    if( m_cubeCaptureScratchDepth == nullptr || m_cubeCaptureScratchDepth->GetSizeX() != captureCube->GetSizeX() || m_cubeCaptureScratchDepth->GetSizeY() != captureCube->GetSizeY() )
    {
        m_cubeCaptureScratchDepth = vaTexture::Create2D( GetRenderDevice(), vaResourceFormat::D32_FLOAT, cubeFaceResolution, cubeFaceResolution, 1, 1, 1, vaResourceBindSupportFlags::DepthStencil );
    }


    vaCameraBase cameraFrontCubeFace;

    cameraFrontCubeFace.SetYFOV( 90.0f / 180.0f * VA_PIf );
    cameraFrontCubeFace.SetNearPlaneDistance( clipNear );
    cameraFrontCubeFace.SetFarPlaneDistance( clipFar );
    cameraFrontCubeFace.SetViewport( vaViewport( captureCube->GetSizeX( ), captureCube->GetSizeY( ) ) );
    cameraFrontCubeFace.SetPosition( position );

    vaDrawResultFlags drawResults = vaDrawResultFlags::None;

    //shared_ptr<vaTexture>* destinationCubeDSVs = m_cubeCaptureScratchDepth;
    //shared_ptr<vaTexture> * destinationCubeRTVs = m_cubemapArrayRTVs;
    {
        VA_TRACE_CPUGPU_SCOPE( CubemapDepthOnly, renderContext );

        // vaRenderOutputs renderOutputs;

        // vaVector3 position = cameraFrontCubeFace.GetPosition( );
        vaCameraBase tempCamera = cameraFrontCubeFace;

        // draw all 6 faces - this should get optimized to GS in the future
        for( int i = 0; i < 6; i++ )
        {
            vaVector3 lookAtDir, upVec;

            // see https://msdn.microsoft.com/en-us/library/windows/desktop/bb204881(v=vs.85).aspx
            switch( i )
            {
            case 0: // positive x (+y up)
                lookAtDir   = vaVector3( 1.0f, 0.0f, 0.0f );
                upVec       = vaVector3( 0.0f, 1.0f, 0.0f );
                break;
            case 1: // negative x (+y up)
                lookAtDir   = vaVector3( -1.0f, 0.0f, 0.0f );
                upVec       = vaVector3( 0.0f, 1.0f, 0.0f );
                break;
            case 2: // positive y (-z up)
                lookAtDir   = vaVector3( 0.0f, 1.0f, 0.0f );
                upVec       = vaVector3( 0.0f, 0.0f, -1.0f );
                break;
            case 3: // negative y (z up)
                lookAtDir   = vaVector3( 0.0f, -1.0f, 0.0f );
                upVec       = vaVector3( 0.0f, 0.0f, 1.0f );
                break;
            case 4: // positive z (y up)
                lookAtDir   = vaVector3( 0.0f, 0.0f, 1.0f );
                upVec       = vaVector3( 0.0f, 1.0f, 0.0f );
                break;
            case 5: // negative z (y up)
                lookAtDir   = vaVector3( 0.0f, 0.0f, -1.0f );
                upVec       = vaVector3( 0.0f, 1.0f, 0.0f );
                break;
            }

            // 
            // lookAtDir   = vaVector3::TransformNormal( lookAtDir, rotation );
            // upVec       = vaVector3::TransformNormal( upVec, rotation );

            tempCamera.SetOrientationLookAt( position + lookAtDir, upVec );
            tempCamera.Tick( 0, false );

            drawResults |= faceCapture( renderContext, tempCamera, m_cubeCaptureScratchDepth, cubeFaceViews[i] );
            if( drawResults != vaDrawResultFlags::None )
            {
                VA_LOG( "Attempting to capture cube face but some of the assets not yet loaded or shaders not yet compiled - aborting" );
                break;
            }
        }
    }

    if( drawResults != vaDrawResultFlags::None )
    {
        Reset();
        return drawResults;
    }

    m_intensity = 1.0f;

    drawResults |= Process( renderContext, captureCube ) ? ( vaDrawResultFlags::None ) : ( vaDrawResultFlags::UnspecifiedError );
    m_skyboxTexture = nullptr; // don't use captured data as a skybox - doesn't really make much sense
    return drawResults;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline vaVector2 Hammersley( uint32_t i, float iN ) 
{
    constexpr float tof = 0.5f / 0x80000000U;
    uint32_t bits = i;
    bits = ( bits << 16u ) | ( bits >> 16u );
    bits = ( ( bits & 0x55555555u ) << 1u ) | ( ( bits & 0xAAAAAAAAu ) >> 1u );
    bits = ( ( bits & 0x33333333u ) << 2u ) | ( ( bits & 0xCCCCCCCCu ) >> 2u );
    bits = ( ( bits & 0x0F0F0F0Fu ) << 4u ) | ( ( bits & 0xF0F0F0F0u ) >> 4u );
    bits = ( ( bits & 0x00FF00FFu ) << 8u ) | ( ( bits & 0xFF00FF00u ) >> 8u );
    return { i * iN, bits * tof };
}

constexpr const double F_PI = 3.14159265358979323846264338327950288;

static vaVector3 HemisphereImportanceSampleDggx( const vaVector2 & u, float a ) 
{ // pdf = D(a) * cosTheta
    const float phi = 2.0f * (float)F_PI * u.x;
    // NOTE: (aa-1) == (a-1)(a+1) produces better fp accuracy
    const float cosTheta2 = ( 1 - u.y ) / ( 1 + ( a + 1 ) * ( ( a - 1 ) * u.y ) );
    const float cosTheta = std::sqrt( cosTheta2 );
    const float sinTheta = std::sqrt( 1 - cosTheta2 );
    return { sinTheta * std::cos( phi ), sinTheta * std::sin( phi ), cosTheta };
}

static float DistributionGGX( float NoH, float linearRoughness ) 
{
    // NOTE: (aa-1) == (a-1)(a+1) produces better fp accuracy
    float a = linearRoughness;
    float f = ( a - 1 ) * ( ( a + 1 ) * ( NoH * NoH ) ) + 1;
    return ( a * a ) / ( (float)F_PI * f * f );
}

template<typename T>
static inline constexpr T log4( T x ) 
{
    // log2(x)/log2(4)
    // log2(x)/2
    return std::log2( x ) * T( 0.5 );
}

static vaVector3 hemisphereCosSample( vaVector2 u ) 
{  // pdf = cosTheta / F_PI;
    const float phi = 2.0f * (float)F_PI * u.x;
    const float cosTheta2 = 1 - u.y;
    const float cosTheta = std::sqrt( cosTheta2 );
    const float sinTheta = std::sqrt( 1 - cosTheta2 );
    return { sinTheta * std::cos( phi ), sinTheta * std::sin( phi ), cosTheta };
}

void vaIBLCubemapPreFilter::Init( uint32 outputBaseSize, uint32 outputMinSize, uint32 samplesPerTexel, vaIBLCubemapPreFilter::FilterType filterType )
{
    assert( filterType != vaIBLCubemapPreFilter::FilterType::Unknown );

    if( m_outputBaseSize == outputBaseSize && m_numSamples == samplesPerTexel && m_filterType == filterType )
        return; // all good, no need to re-init

    VA_LOG( "vaIBLCubemapPreFilter::Init( outputBaseSize == %d, samplesPerPixel == %d ) - this shouldn't be called every frame", outputBaseSize, samplesPerTexel );

    Reset();

    if( !vaMath::IsPowOf2( outputBaseSize ) )
        { assert( false ); VA_ERROR( "vaIBLCubemapPreFilter::Init( outputBaseSize == %d, samplesPerPixel == %d ) - outputBaseSize must be power of 2", outputBaseSize, samplesPerTexel ); }
    if( outputBaseSize < 4 || outputBaseSize > 4096 || samplesPerTexel < 1 || samplesPerTexel > 32768 )
        { assert( false ); VA_ERROR( "vaIBLCubemapPreFilter::Init( outputBaseSize == %d, samplesPerPixel == %d ) - params out of range (outputBaseSize < 4 || outputBaseSize > 4096 || samplesPerTexel < 1 || samplesPerTexel > 32768)", outputBaseSize, samplesPerTexel ); }

    m_filterType    = filterType;

    m_outputBaseSize= outputBaseSize;
    m_numSamples    = samplesPerTexel;
    
    m_numMIPLevels = vaMath::FloorLog2( m_outputBaseSize / outputMinSize )+1;

    m_levels.resize( m_numMIPLevels );

    const size_t dim0 = m_outputBaseSize;
    const float omegaP = ( 4.0f * (float)F_PI ) / float( 6 * dim0 * dim0 );

    uint32 numSamples = m_numSamples;
    uint32 maxSampleMIPLevel = vaMath::FloorLog2( m_outputBaseSize ); // this is for sampling part; in this case outputBaseSize is also inputBaseSize
    for( uint32 level = 0; level < m_numMIPLevels; level++ ) 
    {
        const uint32 dim    = m_outputBaseSize >> level;

        // we do a very mild min-roughness pass on the first level for reflections - it doesn't
        // need many samples so start with m_numSamples/4
        if( level == 0 && filterType == vaIBLCubemapPreFilter::FilterType::ReflectionsRoughness )
            numSamples = m_numSamples/4;
        
        // level 1 always uses full sample count
        if( level == 1 )
            numSamples = m_numSamples;

        // starting at level 2, we increase the number of samples per level
        // this helps as the filter gets wider, and since there are 4x less work
        // per level, this doesn't slow things down a lot.
        if( level >= 2 ) 
            numSamples *= 2;

        // limit the number of samples to a max sane value
        numSamples = std::min( numSamples, 16384U );

        LevelInfo & levelInfo = m_levels[level];
        levelInfo.Samples.clear( );

        levelInfo.Size = dim;

        if( filterType == vaIBLCubemapPreFilter::FilterType::ReflectionsRoughness )
        {
            const float lod = vaMath::Saturate( level / ( (float)m_numMIPLevels - 1.0f ) );

            // see perceptualRoughnessToRoughness - linear_roughness = perceptual_roughness^2
            
            // from shaders, but a bit more relaxed
            // #define MIN_PERCEPTUAL_ROUGHNESS 0.045
            #define MIN_ROUGHNESS            (0.002025 * 0.33)

            // map the lod to a linear_roughness,  here we're using ^2, but other mappings are possible.
            // ==> lod = sqrt(linear_roughness)
            const float linearRoughness = std::max( lod * lod, (float)MIN_ROUGHNESS );

            assert( m_numMIPLevels > 2 );   // doesn't really make sense to pre-filter for only 1 or 2 levels
            
#if 0       // old approach was to do no pre-filter for level 0; new approach does some minimal pre-filtering for L0 to match min roughness
            // no pre-filtering for level 0
            if( level == 0 )
                levelInfo.Samples.push_back( { {0, 0, 1}, 1, 0 } ); // vector up, weight 1, mip level 0
            else
#endif
            {
                levelInfo.Samples.reserve( numSamples );

                // index of the sample to use
                // our goal is to use maxNumSamples for which NoL is > 0
                // to achieve this, we might have to try more samples than
                // maxNumSamples
                for( size_t sampleIndex = 0; sampleIndex < numSamples; sampleIndex++ ) 
                {

                    // get Hammersley distribution for the half-sphere
                    const vaVector2 u = Hammersley( uint32_t( sampleIndex ), 1.0f / (float)numSamples );

                    // Importance sampling GGX - Trowbridge-Reitz
                    const vaVector3 H = HemisphereImportanceSampleDggx( u, linearRoughness );

        #if 0
                    // This produces the same result that the code below using the the non-simplified
                    // equation. This let's us see that N == V and that L = -reflect(V, H)
                    // Keep this for reference.
                    const float3 N = { 0, 0, 1 };
                    const float3 V = N;
                    const float3 L = 2 * dot( H, V ) * H - V;
                    const float NoL = dot( N, L );
                    const float NoH = dot( N, H );
                    const float NoH2 = NoH * NoH;
                    const float NoV = dot( N, V );
        #else
                    const float NoH = H.z;
                    const float NoH2 = H.z * H.z;
                    const float NoL = 2 * NoH2 - 1;
                    const vaVector3 L( 2 * NoH * H.x, 2 * NoH * H.y, NoL );
        #endif

                    if( NoL > 0 ) 
                    {
                        const float pdf = DistributionGGX( NoH, linearRoughness ) / 4;

                        // K is a LOD bias that allows a bit of overlapping between samples
                        constexpr float K = 4;
                        const float omegaS = 1 / ( numSamples * pdf );
                        const float l = float( log4( omegaS ) - log4( omegaP ) + log4( K ) );
                        const float mipLevel = vaMath::Clamp( float( l ), 0.0f, (float)maxSampleMIPLevel );

                        const float brdf_NoL = float( NoL );

                        levelInfo.Samples.push_back( { L, brdf_NoL, mipLevel } );
                    }
                }
            }
        }
        else if( filterType == vaIBLCubemapPreFilter::FilterType::Irradiance )
        {
            levelInfo.Samples.reserve( numSamples );

            // index of the sample to use
            // our goal is to use maxNumSamples for which NoL is > 0
            // to achieve this, we might have to try more samples than
            // maxNumSamples
            for( size_t sampleIndex = 0; sampleIndex < numSamples; sampleIndex++ )
            {
                // get Hammersley distribution for the half-sphere
                const vaVector2 u = Hammersley( uint32_t( sampleIndex ), 1.0f / (float)numSamples );
                const vaVector3 L = hemisphereCosSample( u );
                const vaVector3 N = { 0, 0, 1 };
                const float NoL = vaVector3::Dot( N, L );

                if( NoL > 0 ) 
                {
                    constexpr const double F_1_PI = 0.318309886183790671537767526745028724;
                    float pdf = NoL * (float)F_1_PI;

                    constexpr float K = 4;
                    const float omegaS = 1.0f / ( numSamples * pdf );
                    const float l = float( log4( omegaS ) - log4( omegaP ) + log4( K ) );
                    const float mipLevel = vaMath::Clamp( float( l ), 0.0f, (float)maxSampleMIPLevel );

                    levelInfo.Samples.push_back( { L, 1.0f, mipLevel } );
                }
                else
                {
                    assert( false );
                }
            }
        }
        else
        {
            assert( false ); // unsupported filter type?
        }

        {
            float weightSum = 0;
            for( auto& entry : levelInfo.Samples )
                weightSum += entry.Weight;
            for( auto& entry : levelInfo.Samples )
                entry.Weight /= weightSum;
            // we can sample the cubemap in any order, sort by the weight, it could improve fp precision
            std::sort( levelInfo.Samples.begin( ), levelInfo.Samples.end( ), [ ]( SampleInfo const& lhs, SampleInfo const& rhs ) { return lhs.Weight < rhs.Weight; } );
        }

        std::vector<vaVector4> packedSamples;
        packedSamples.resize( levelInfo.Samples.size( ) );
        for( int j = 0; j < (int)levelInfo.Samples.size( ); j++ )
        {
            SampleInfo& si = levelInfo.Samples[j];
            vaVector4& psi = packedSamples[j];
            psi = SampleInfo::Pack( si );
            SampleInfo upsi = SampleInfo::Unpack( psi );

            assert( vaVector3::NearEqual( upsi.L, si.L, 5e-4f ) );
            vaMath::NearEqual( upsi.Weight, si.Weight );
            vaMath::NearEqual( upsi.MIPLevel, si.MIPLevel );
        }

        int twidth = 8192;
        int theight = ( (int)levelInfo.Samples.size( ) + 8191 ) / 8192;

        vaVector4* data = new vaVector4[twidth * theight];
        memset( data, 0, twidth * theight * sizeof( vaVector4 ) );
        memcpy( data, packedSamples.data( ), packedSamples.size( ) * sizeof( vaVector4 ) );

        levelInfo.SamplesTexture = vaTexture::Create2D( GetRenderDevice( ), vaResourceFormat::R32G32B32A32_FLOAT, twidth, theight, 1, 1, 1, vaResourceBindSupportFlags::ShaderResource, vaResourceAccessFlags::Default,
            vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::None, vaTextureContentsType::GenericLinear,
            data, 8192 * sizeof( vaVector4 ) );
        delete[] data;

        levelInfo.CSThreadGroupSize = std::min( levelInfo.Size, 16U );

        const vaShaderMacroContaner shaderMacros = {    { "IBL_ROUGHNESS_PREFILTER_NUM_SAMPLES", vaStringTools::Format( "(%d)", (int)levelInfo.Samples.size() ) },
                                                        { "IBL_ROUGHNESS_PREFILTER_THREADGROUPSIZE", vaStringTools::Format( "(%d)", (int)levelInfo.CSThreadGroupSize ) },
                                                    };
        levelInfo.CSCubePreFilter = GetRenderDevice().CreateModule< vaComputeShader>(); // get the platform dependent object
        levelInfo.CSCubePreFilter->CompileFromFile( "Lighting/vaIBL.hlsl", "CSCubePreFilter", shaderMacros, false );
    }

}

void vaIBLCubemapPreFilter::Process( vaRenderDeviceContext & renderContext, std::vector<shared_ptr<vaTexture>> destCubeMIPLevels, shared_ptr<vaTexture>& srcCube )
{
    if( m_levels.size() == 0 )
        { assert( false ); VA_WARN( "vaIBLCubemapPreFilter::Process - filter not initialized, unable to run!" ); return; }    

    if( m_levels.size() != destCubeMIPLevels.size() )
        { assert( false ); VA_WARN( "vaIBLCubemapPreFilter::Process - destCubeMIPLevels number of levels different from what the filter was initialized to; can't run the filter" ); return; }    

    if( m_outputBaseSize != (uint32)destCubeMIPLevels[0]->GetSizeX() )
        { assert( false ); VA_WARN( "vaIBLCubemapPreFilter::Process - destCube dimensions different from what the filter was initialized to; can't run the filter" ); return; }    
    
    uint32 srcSize = (uint32)srcCube->GetSizeX();
    if( srcSize != m_outputBaseSize ) //|| !vaMath::IsPowOf2(srcSize) )
        { assert( false ); VA_WARN( "vaIBLCubemapPreFilter::Process - srcCube dimensions must match dest cube size; can't run the filter" ); return; }    
    
    int srcMIPs = srcCube->GetMipLevels();
    assert( srcMIPs >= destCubeMIPLevels.size() ); srcMIPs;

    for( uint32 level = 0; level < m_numMIPLevels; level++ )
    {
        // first level is roughness == 0 so no filtering needed - but we use the same path which copies cube mip 0 from source
        LevelInfo& levelInfo = m_levels[level];

        levelInfo.CSCubePreFilter->WaitFinishIfBackgroundCreateActive( );

        vaComputeItem computeItem;
        vaRenderOutputs outputs;
        computeItem.ComputeShader = levelInfo.CSCubePreFilter;
        outputs.UnorderedAccessViews[IBL_FILTER_CUBE_FACES_ARRAY_UAV_SLOT] = destCubeMIPLevels[level];
        computeItem.ShaderResourceViews[IBL_FILTER_GENERIC_TEXTURE_SLOT_0]     = levelInfo.SamplesTexture;
        computeItem.ShaderResourceViews[IBL_FILTER_CUBE_TEXTURE_SLOT]          = srcCube;
        
        assert( (levelInfo.Size % levelInfo.CSThreadGroupSize) == 0 );

        computeItem.SetDispatch( levelInfo.Size / levelInfo.CSThreadGroupSize, levelInfo.Size / levelInfo.CSThreadGroupSize, 6 );
        renderContext.ExecuteSingleItem( computeItem, outputs, nullptr );
    }

    //int srcMIPs = vaMath::FloorLog2( srcSize ) + 1;
    //if( srcMIPs != srcCube->GetMipLevels() )
    //    { assert( false ); VA_WARN( "vaIBLCubemapPreFilter::Process - srcCube mip count does not match the number required; can't run the filter" ); return; }    


}

void vaIBLCubemapPreFilter::Reset( )
{
    m_outputBaseSize    = 0;
    m_numSamples        = 0;
    m_numMIPLevels      = 0;
    m_filterType        = vaIBLCubemapPreFilter::FilterType::Unknown;
    m_levels.clear();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

vaIrradianceSHCalculator::vaIrradianceSHCalculator( vaRenderDevice& renderDevice ) : vaRenderingModule( renderDevice )
{
    Reset();

    // see filament CubemapSH.cpp/.h, CubemapSH::computeSH
    int numCoefs = c_numSHBands * c_numSHBands;

    const vaShaderMacroContaner shaderMacros = { { "IBL_NUM_SH_BANDS", vaStringTools::Format( "(%d)", c_numSHBands )} };

    m_CSComputeSH = GetRenderDevice().CreateModule< vaComputeShader>( ); // get the platform dependent object
    m_CSComputeSH->CompileFromFile( "Lighting/vaIBL.hlsl", "CSComputeSH", shaderMacros, false );

    m_CSPostProcessSH = GetRenderDevice().CreateModule< vaComputeShader>( ); // get the platform dependent object
    m_CSPostProcessSH->CompileFromFile( "Lighting/vaIBL.hlsl", "CSPostProcessSH", shaderMacros, false );

    m_SH = vaTexture::Create1D( renderDevice, vaResourceFormat::R32_UINT, numCoefs * 3, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::UnorderedAccess );
    m_SHCPUReadback = vaTexture::Create1D( renderDevice, m_SH->GetResourceFormat( ), numCoefs * 3, 1, 1, vaResourceBindSupportFlags::None, vaResourceAccessFlags::CPURead );

    // std::vector<float> K = Ki( c_numSHBands );
    // 
    // // apply truncated cos (irradiance)
    // bool irradianceScaling = true;
    // if( irradianceScaling ) 
    // {
    //     for( uint32 l = 0; l < c_numSHBands; l++ ) {
    //         const float truncatedCosSh = ComputeTruncatedCosSh( l );
    //         K[SHindex( 0, l )] *= truncatedCosSh;
    //         for( size_t m = 1; m <= l; m++ ) {
    //             K[SHindex( -int32(m), l )] *= truncatedCosSh;
    //             K[SHindex( int32(m), l )] *= truncatedCosSh;
    //         }
    //     }
    // }
    // m_SHScalingK = vaTexture::Create1D( renderDevice, vaResourceFormat::R32_FLOAT, numCoefs*3, 1, 1, vaResourceBindSupportFlags::ShaderResource, vaResourceAccessFlags::Default, 
    //                                     vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaTextureFlags::None, vaTextureContentsType::GenericLinear,
    //                                     K.data() );
}
vaIrradianceSHCalculator::~vaIrradianceSHCalculator( )
{
}
void vaIrradianceSHCalculator::Reset( )
{
    m_SHComputed = false;
}

void vaIrradianceSHCalculator::ComputeSH( vaRenderDeviceContext& renderContext, shared_ptr<vaTexture>& sourceCube )
{
    m_SH->ClearUAV( renderContext, 0u );

    const uint32 threadGroupSizeXY = 16;
    int cubeDim = sourceCube->GetSizeX( ); assert( cubeDim == sourceCube->GetSizeY( ) );
    assert( ( cubeDim % threadGroupSizeXY ) == 0 );

    // main compute pass
    {
        m_CSComputeSH->WaitFinishIfBackgroundCreateActive( );
        vaComputeItem computeItem;
        vaRenderOutputs outputs;
        computeItem.ComputeShader = m_CSComputeSH;
        outputs.UnorderedAccessViews[IBL_FILTER_UAV_SLOT] = m_SH;
        computeItem.ShaderResourceViews[IBL_FILTER_CUBE_FACES_ARRAY_TEXTURE_SLOT] = sourceCube;
        //computeItem.ShaderResourceViews[IBL_FILTER_SCALING_FACTOR_K_TEXTURE_SLOT]  = m_SHScalingK;

        computeItem.SetDispatch( cubeDim / threadGroupSizeXY, cubeDim / threadGroupSizeXY, 6 );
        //computeItem.SetDispatch( 1, 1, 1 );
        renderContext.ExecuteSingleItem( computeItem, outputs, nullptr );
    }

#if 0
    m_SHCPUReadback->CopyFrom( renderContext, m_SH );
    if( m_SHCPUReadback->TryMap( renderContext, vaResourceMapType::Read, false ) )
    {
        std::vector<vaTextureMappedSubresource>& mappedData = m_SHCPUReadback->GetMappedData( );
        vaVector3* SHData = reinterpret_cast<vaVector3*>( mappedData[0].Buffer );
        SHData;
        //for( int i = 0; i < 9; i++ )
        //    SH[i] = SHData[i];
        m_SHCPUReadback->Unmap( renderContext );
    }
#endif

    // postprocess - previously done on the CPU like this:
    //      WindowSH( SH, c_numSHBands, 0.0f );
    //      PreprocessSHForShader( SH, c_numSHBands );
    {
        m_CSPostProcessSH->WaitFinishIfBackgroundCreateActive( );
        vaComputeItem computeItem;
        vaRenderOutputs outputs;
        computeItem.ComputeShader = m_CSPostProcessSH;
        outputs.UnorderedAccessViews[IBL_FILTER_UAV_SLOT] = m_SH;
        //computeItem.ShaderResourceViews[IBL_FILTER_SCALING_FACTOR_K_TEXTURE_SLOT] = m_SHScalingK;
        computeItem.SetDispatch( 1, 1, 1 );
        renderContext.ExecuteSingleItem( computeItem, outputs, nullptr );
    }
}

std::array<vaVector3, 9> vaIrradianceSHCalculator::GetSH( vaRenderDeviceContext& renderContext )
{
    std::array<vaVector3, 9> SH;
    m_SHCPUReadback->CopyFrom( renderContext, m_SH );
    if( m_SHCPUReadback->TryMap( renderContext, vaResourceMapType::Read, false ) )
    {
        std::vector<vaTextureMappedSubresource>& mappedData = m_SHCPUReadback->GetMappedData( );
        vaVector3* SHData = reinterpret_cast<vaVector3*>( mappedData[0].Buffer );
        for( int i = 0; i < 9; i++ )
            SH[i] = SHData[i];
        m_SHCPUReadback->Unmap( renderContext );
    }
    return SH;
}











































#if 0

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// from "filament\libs\ibl\src\CubemapSH.cpp"
static inline constexpr uint32 SHindex( int32 m, uint32 l ) 
{
    return l * ( l + 1 ) + m;
}
//
/*
 * returns n! / d!
 */
static constexpr float factorial(uint32 n, uint32 d) 
{
   d = std::max(uint32(1), d);
   n = std::max(uint32(1), n);
   float r = 1.0;
   if (n == d) {
       // intentionally left blank
   } else if (n > d) {
       for ( ; n>d ; n--) {
           r *= n;
       }
   } else {
       for ( ; d>n ; d--) {
           r *= d;
       }
       r = 1.0f / r;
   }
   return r;
}
//
constexpr const double F_2_SQRTPI = 1.12837916709551257389615890312154517;
constexpr const double F_SQRT2    = 1.41421356237309504880168872420969808;
constexpr const double F_PI       = 3.14159265358979323846264338327950288;
constexpr const double F_1_PI     = 0.318309886183790671537767526745028724;
constexpr const double F_SQRT1_2  = 0.707106781186547524400844362104849039;
constexpr const float  M_SQRT_3   = 1.7320508076f;
//
/*
 * SH scaling factors:
 *  returns sqrt((2*l + 1) / 4*pi) * sqrt( (l-|m|)! / (l+|m|)! )
 */
static float Kml(int32 m, uint32 l) 
{
    m = m < 0 ? -m : m;  // abs() is not constexpr
    const float K = (2 * l + 1) * factorial(uint32(l - m), uint32(l + m));
    return float(std::sqrt(K) * (F_2_SQRTPI * 0.25));
}
//
static std::vector<float> Ki(uint32 numBands) 
{
    const uint32 numCoefs = numBands * numBands;
    std::vector<float> K(numCoefs);
    for (uint32 l = 0; l < numBands; l++) {
        K[SHindex(0, l)] = Kml(0, l);
        for (uint32 m = 1; m <= l; m++) {
            K[SHindex(m, l)] =
            K[SHindex(-int32(m), l)] = float(F_SQRT2 * Kml(m, l));
        }
    }
    return K;
}
//
// < cos(theta) > SH coefficients pre-multiplied by 1 / K(0,l)
constexpr float ComputeTruncatedCosSh( uint32 l ) 
{
    if( l == 0 ) {
        return (float)F_PI;
    }
    else if( l == 1 ) {
        return float(2 * F_PI / 3);
    }
    else if( l & 1u ) {
        return 0.0f;
    }
    const uint32 l_2 = l / 2;
    float A0 = ( ( l_2 & 1u ) ? 1.0f : -1.0f ) / ( ( l + 2 ) * ( l - 1 ) );
    float A1 = factorial( l, l_2 ) / ( factorial( l_2, 1 ) * ( 1 << l ) );
    return float(2 * F_PI * A0 * A1);
}
//
/*
 * SH from environment with high dynamic range (or high frequencies -- high dynamic range creates
 * high frequencies) exhibit "ringing" and negative values when reconstructed.
 * To mitigate this, we need to low-pass the input image -- or equivalently window the SH by
 * coefficient that tapper towards zero with the band.
 *
 * We use ideas and techniques from
 *    Stupid Spherical Harmonics (SH)
 *    Deringing Spherical Harmonics
 * by Peter-Pike Sloan
 * https://www.ppsloan.org/publications/shdering.pdf
 *
 */
static float SincWindow(uint32 l, float w)
{
    if (l == 0) {
        return 1.0f;
    } else if (l >= w) {
        return 0.0f;
    }

    // we use a sinc window scaled to the desired window size in bands units
    // a sinc window only has zonal harmonics
    float x = (float(F_PI) * l) / w;
    x = std::sin(x) / x;

    // The convolution of a SH function f and a ZH function h is just the product of both
    // scaled by 1 / K(0,l) -- the window coefficients include this scale factor.

    // Taking the window to power N is equivalent to applying the filter N times
    return std::pow(x, 4);
}
//
void multiply3( float out[3], const float M[3][3], const float x[3] ) 
{
    out[0] = M[0][0] * x[0] + M[1][0] * x[1] + M[2][0] * x[2];
    out[1] = M[0][1] * x[0] + M[1][1] * x[1] + M[2][1] * x[2];
    out[2] = M[0][2] * x[0] + M[1][2] * x[1] + M[2][2] * x[2];
};
/*
 * utilities to rotate very low order spherical harmonics (up to 3rd band)
 */
void RotateSphericalHarmonicBand1(float out[3], const float band1[3], const float M[3][3]) 
{
    // inverse() is not constexpr -- so we pre-calculate it in mathematica
    //
    //    constexpr float3 N0{ 1, 0, 0 };
    //    constexpr float3 N1{ 0, 1, 0 };
    //    constexpr float3 N2{ 0, 0, 1 };
    //
    //    constexpr mat3f A1 = { // this is the projection of N0, N1, N2 to SH space
    //            float3{ -N0.y, N0.z, -N0.x },
    //            float3{ -N1.y, N1.z, -N1.x },
    //            float3{ -N2.y, N2.z, -N2.x }
    //    };
    //
    //    const mat3f invA1 = inverse(A1);

    const float invA1TimesK[3][3] = {
            {  0, -1,  0 },
            {  0,  0,  1 },
            { -1,  0,  0 }
    };
    
    // below can't be constexpr
    const float * MN0 = M[0];  // M * N0;
    const float * MN1 = M[1];  // M * N1;
    const float * MN2 = M[2];  // M * N2;
    const float R1OverK[3][3] = {
            { -MN0[1], MN0[2], -MN0[0] },
            { -MN1[1], MN1[2], -MN1[0] },
            { -MN2[1], MN2[2], -MN2[0] }
    };

    float temp0[3];
    multiply3( temp0, invA1TimesK, band1 );
    multiply3( out, R1OverK, temp0 );
    //return R1OverK * (invA1TimesK * band1);
}
//
static void multiply5( float out[5], const float M[5][5], const float x[5] )
{
    out[0] = M[0][0] * x[0] + M[1][0] * x[1] + M[2][0] * x[2] + M[3][0] * x[3] + M[4][0] * x[4];
    out[1] = M[0][1] * x[0] + M[1][1] * x[1] + M[2][1] * x[2] + M[3][1] * x[3] + M[4][1] * x[4];
    out[2] = M[0][2] * x[0] + M[1][2] * x[1] + M[2][2] * x[2] + M[3][2] * x[3] + M[4][2] * x[4];
    out[3] = M[0][3] * x[0] + M[1][3] * x[1] + M[2][3] * x[2] + M[3][3] * x[3] + M[4][3] * x[4];
    out[4] = M[0][4] * x[0] + M[1][4] * x[1] + M[2][4] * x[2] + M[3][4] * x[3] + M[4][4] * x[4];
};
//
// This projects a vec3 to SH2/k space (i.e. we premultiply by 1/k)
// below can't be constexpr
void project5( float out[5], const float s[3] )
{
    out[0] = ( s[1] * s[0] );
    out[1] = -( s[1] * s[2] );
    out[2] = 1 / ( 2 * M_SQRT_3 ) * ( ( 3 * s[2] * s[2] - 1 ) );
    out[3] = -( s[2] * s[0] );
    out[4] = 0.5f * ( ( s[0] * s[0] - s[1] * s[1] ) );
}
//
void RotateSphericalHarmonicBand2( float result[5], const float band2[5], const float M[3][3]) 
{
    //constexpr float M_SQRT_3  = 1.7320508076f;
    constexpr float n = (float)F_SQRT1_2;

    //  Below we precompute (with help of Mathematica):
    //    constexpr float3 N0{ 1, 0, 0 };
    //    constexpr float3 N1{ 0, 0, 1 };
    //    constexpr float3 N2{ n, n, 0 };
    //    constexpr float3 N3{ n, 0, n };
    //    constexpr float3 N4{ 0, n, n };
    //    constexpr float M_SQRT_PI = 1.7724538509f;
    //    constexpr float M_SQRT_15 = 3.8729833462f;
    //    constexpr float k = M_SQRT_15 / (2.0f * M_SQRT_PI);
    //    --> k * inverse(mat5{project(N0), project(N1), project(N2), project(N3), project(N4)})
    float invATimesK[5][5] = {
            {    0,        1,   2,   0,  0 },
            {   -1,        0,   0,   0, -2 },
            {    0, M_SQRT_3,   0,   0,  0 },
            {    1,        1,   0,  -2,  0 },
            {    2,        1,   0,   0,  0 }
    };

    // this is: invA * k * band2
    // 5x5 matrix by vec5 (this a lot of zeroes and constants, which the compiler should eliminate)
    float invATimesKTimesBand2[5];
    multiply5(invATimesKTimesBand2, invATimesK, band2);

    // this is: mat5{project(N0), project(N1), project(N2), project(N3), project(N4)} / k
    // (the 1/k comes from project(), see above)
    float ROverK[5][5]; 
    project5(ROverK[0], M[0]);                  // M * N0
    project5(ROverK[1], M[2]);                  // M * N1
    vaVector3 k0 = n * ( vaVector3( M[0] ) + vaVector3( M[1] ) );
    vaVector3 k1 = n * ( vaVector3( M[0] ) + vaVector3( M[2] ) );
    vaVector3 k2 = n * ( vaVector3( M[1] ) + vaVector3( M[2] ) );
    project5( ROverK[2], &k0.x );     // M * N2
    project5( ROverK[3], &k1.x );     // M * N3
    project5( ROverK[4], &k2.x );     // M * N4

    // notice how "k" disappears
    // this is: (R / k) * (invA * k) * band2 == R * invA * band2
    multiply5( result, ROverK, invATimesKTimesBand2 );
}
//
void WindowSH_RotateSh3Bands( float sh[9], vaMatrix3x3 M ) 
{
    const float b0 = sh[0];
    const vaVector3 band1{ sh[1], sh[2], sh[3] };
    vaVector3 b1; 
    RotateSphericalHarmonicBand1( &b1.x, &band1.x, M.m );
    const float band2[5] = { sh[4], sh[5], sh[6], sh[7], sh[8] };
    float b2[5];
    RotateSphericalHarmonicBand2( b2, band2, M.m );
    sh[0] = b0;
    sh[1] = b1[0];
    sh[2] = b1[1];
    sh[3] = b1[2];
    sh[4] = b2[0];
    sh[5] = b2[1];
    sh[6] = b2[2];
    sh[7] = b2[3];
    sh[8] = b2[4];
};
//
// this is the function we're trying to minimize
float WindowSH_func( float a, float b, float c, float d, float x ) 
{
    // first term accounts for ZH + |m| = 2, second terms for |m| = 1
    return ( a * x * x + b * x + c ) + ( d * x * std::sqrt( 1 - x * x ) );
};

// This is func' / func'' -- this was computed with Mathematica
float WindowSH_increment( float a, float b, float c, float d, float x ) 
{   c;
    return ( x * x - 1 ) * ( d - 2 * d * x * x + ( b + 2 * a * x ) * std::sqrt( 1 - x * x ) )
        / ( 3 * d * x - 2 * d * x * x * x - 2 * a * std::pow( 1 - x * x, 1.5f ) );

};
//
float WindowSH_SHMin( float f[9] ) 
{
    // See "Deringing Spherical Harmonics" by Peter-Pike Sloan
    // https://www.ppsloan.org/publications/shdering.pdf

    constexpr float M_SQRT_PI = 1.7724538509f;
    constexpr float M_SQRT_5 = 2.2360679775f;
    constexpr float M_SQRT_15 = 3.8729833462f;
    constexpr float A[9] = {
                  1.0f / ( 2.0f * M_SQRT_PI ),    // 0: 0  0
            -M_SQRT_3 / ( 2.0f * M_SQRT_PI ),    // 1: 1 -1
             M_SQRT_3 / ( 2.0f * M_SQRT_PI ),    // 2: 1  0
            -M_SQRT_3 / ( 2.0f * M_SQRT_PI ),    // 3: 1  1
             M_SQRT_15 / ( 2.0f * M_SQRT_PI ),    // 4: 2 -2
            -M_SQRT_15 / ( 2.0f * M_SQRT_PI ),    // 5: 2 -1
             M_SQRT_5 / ( 4.0f * M_SQRT_PI ),    // 6: 2  0
            -M_SQRT_15 / ( 2.0f * M_SQRT_PI ),    // 7: 2  1
             M_SQRT_15 / ( 4.0f * M_SQRT_PI )     // 8: 2  2
    };

    // first this to do is to rotate the SH to align Z with the optimal linear direction
    const vaVector3 dir = vaVector3::Normalize( vaVector3{ -f[3], -f[1], f[2] } );
    const vaVector3 z_axis = -dir;
    const vaVector3 x_axis = vaVector3::Normalize( vaVector3::Cross( z_axis, vaVector3{ 0, 1, 0 } ) );
    const vaVector3 y_axis = vaVector3::Cross( x_axis, z_axis );
    const vaMatrix3x3 M = vaMatrix3x3{ x_axis, y_axis, -z_axis }.Transposed();

    WindowSH_RotateSh3Bands( f, M );
    // here we're guaranteed to have normalize(float3{ -f[3], -f[1], f[2] }) == { 0, 0, 1 }


    // Find the min for |m| = 2
    // ------------------------
    //
    // Peter-Pike Sloan shows that the minimum can be expressed as a function
    // of z such as:  m2min = -m2max * (1 - z^2) =  m2max * z^2 - m2max
    //      with m2max = A[8] * std::sqrt(f[8] * f[8] + f[4] * f[4]);
    // We can therefore include this in the ZH min computation (which is function of z^2 as well)
    float m2max = A[8] * std::sqrt( f[8] * f[8] + f[4] * f[4] );

    // Find the min of the zonal harmonics
    // -----------------------------------
    //
    // This comes from minimizing the function:
    //      ZH(z) = (A[0] * f[0])
    //            + (A[2] * f[2]) * z
    //            + (A[6] * f[6]) * (3 * s.z * s.z - 1)
    //
    // We do that by finding where it's derivative d/dz is zero:
    //      dZH(z)/dz = a * z^2 + b * z + c
    //      which is zero for z = -b / 2 * a
    //
    // We also needs to check that -1 < z < 1, otherwise the min is either in z = -1 or 1
    //
    const float a = 3 * A[6] * f[6] + m2max;
    const float b = A[2] * f[2];
    const float c = A[0] * f[0] - A[6] * f[6] - m2max;

    const float zmin = -b / ( 2.0f * a );
    const float m0min_z = a * zmin * zmin + b * zmin + c;
    const float m0min_b = std::min( a + b + c, a - b + c );

    const float m0min = ( a > 0 && zmin >= -1 && zmin <= 1 ) ? m0min_z : m0min_b;

    // Find the min for l = 2, |m| = 1
    // -------------------------------
    //
    // Note l = 1, |m| = 1 is guaranteed to be 0 because of the rotation step
    //
    // The function considered is:
    //        Y(x, y, z) = A[5] * f[5] * s.y * s.z
    //                   + A[7] * f[7] * s.z * s.x
    float d = A[4] * std::sqrt( f[5] * f[5] + f[7] * f[7] );

    // the |m|=1 function is minimal in -0.5 -- use that to skip the Newton's loop when possible
    float minimum = m0min - 0.5f * d;
    if( minimum < 0 ) 
    {
        // We could be negative, to find the minimum we will use Newton's method
        // See https://en.wikipedia.org/wiki/Newton%27s_method_in_optimization

        float dz;
        float z = float(-F_SQRT1_2);   // we start guessing at the min of |m|=1 function
        int loopCount = 0;
        do {
            minimum = WindowSH_func( a, b, c, d, z ); // evaluate our function
            dz = WindowSH_increment( a, b, c, d, z ); // refine our guess by this amount
            z = z - dz;
            // exit if z goes out of range, or if we have reached enough precision
            loopCount++;
        } while( (std::abs( z ) <= 1) && (std::abs( dz ) > 1e-5f) && (loopCount<16) );

        if( std::abs( z ) > 1 ) {
            // z was out of range
            minimum = std::min( WindowSH_func( a, b, c, d, 1 ), WindowSH_func( a, b, c, d, -1 ) );
        }
    }
    return minimum;
};
//
static void WindowSH_Windowing( float out[9], float f[9], float cutoff, const uint32 numBands )
{
    for( int i = 0; i < 9; i++ )
        out[i] = f[i];
    for( int32 l = 0; l < (int32)numBands; l++ ) 
    {
        float w = SincWindow( l, cutoff );
        out[SHindex( 0, l )] *= w;
        for( uint32 m = 1; m <= (uint32)l; m++ ) 
        {
            out[SHindex( -int32(m), l )] *= w;
            out[SHindex( int32(m), l )] *= w;
        }
    }
}
//
static void WindowSH( std::unique_ptr<vaVector3[]>& sh, uint32 numBands, float cutoff ) 
{
    if( cutoff == 0 ) 
    { 
        // auto windowing (default)
        if( numBands > 3 ) 
        {
            // auto-windowing works only for 1, 2 or 3 bands
            assert( false );
            //slog.e << "--sh-window=auto can't work with more than 3 bands. Disabling." << io::endl;
            return;
        }

        cutoff = (float)numBands * 4 + 1; // start at a large band
        // We need to process each channel separately
        float SH[9];
        for( uint32 channel = 0; channel < 3; channel++ ) 
        {
            for( uint32 i = 0; i < numBands * numBands; i++ ) 
                SH[i] = sh[i][channel];

            // find a cut-off band that works
            float l = (float)numBands;
            float r = cutoff;
            for( uint32 i = 0; i < 16 && l + 0.1f < r; i++ ) 
            {
                float m = 0.5f * ( l + r );
                float SHTemp[9];
                WindowSH_Windowing( SHTemp, SH, m, numBands );
                if( WindowSH_SHMin( SHTemp ) < 0 ) 
                {
                    r = m;
                }
                else 
                {
                    l = m;
                }
            }
            cutoff = std::min( cutoff, l );
        }
    }

    //slog.d << cutoff << io::endl;
    for( int32 l = 0; l < (int32)numBands; l++ ) 
    {
        float w = SincWindow( l, cutoff );
        sh[SHindex( 0, l )] *= w;
        for( size_t m = 1; m <= l; m++ ) 
        {
            sh[SHindex( -int32(m), l )] *= w;
            sh[SHindex( int32(m), l )] *= w;
        }
    }
}
//
/*
 * This computes the 3-bands SH coefficients of the Cubemap convoluted by the
 * truncated cos(theta) (i.e.: saturate(s.z)), pre-scaled by the reconstruction
 * factors.
 */
static void PreprocessSHForShader( std::unique_ptr<vaVector3[]>& SH, uint32 _numBands )
{
    constexpr size_t numBands = 3;
    constexpr size_t numCoefs = numBands * numBands;
    assert( _numBands == numBands ); _numBands;

    // Coefficient for the polynomial form of the SH functions -- these were taken from
    // "Stupid Spherical Harmonics (SH)" by Peter-Pike Sloan
    // They simply come for expanding the computation of each SH function.
    //
    // To render spherical harmonics we can use the polynomial form, like this:
    //          c += sh[0] * A[0];
    //          c += sh[1] * A[1] * s.y;
    //          c += sh[2] * A[2] * s.z;
    //          c += sh[3] * A[3] * s.x;
    //          c += sh[4] * A[4] * s.y * s.x;
    //          c += sh[5] * A[5] * s.y * s.z;
    //          c += sh[6] * A[6] * (3 * s.z * s.z - 1);
    //          c += sh[7] * A[7] * s.z * s.x;
    //          c += sh[8] * A[8] * (s.x * s.x - s.y * s.y);
    //
    // To save math in the shader, we pre-multiply our SH coefficient by the A[i] factors.
    // Additionally, we include the lambertian diffuse BRDF 1/pi.

    constexpr float M_SQRT_PI = 1.7724538509f;
    constexpr float M_SQRT_5  = 2.2360679775f;
    constexpr float M_SQRT_15 = 3.8729833462f;
    constexpr float A[numCoefs] = {
                  1.0f / (2.0f * M_SQRT_PI),    // 0  0
            -M_SQRT_3  / (2.0f * M_SQRT_PI),    // 1 -1
             M_SQRT_3  / (2.0f * M_SQRT_PI),    // 1  0
            -M_SQRT_3  / (2.0f * M_SQRT_PI),    // 1  1
             M_SQRT_15 / (2.0f * M_SQRT_PI),    // 2 -2
            -M_SQRT_15 / (2.0f * M_SQRT_PI),    // 3 -1
             M_SQRT_5  / (4.0f * M_SQRT_PI),    // 3  0
            -M_SQRT_15 / (2.0f * M_SQRT_PI),    // 3  1
             M_SQRT_15 / (4.0f * M_SQRT_PI)     // 3  2
    };

    for (size_t i = 0; i < numCoefs; i++)
        SH[i] *= float(A[i] * F_1_PI);
}
#endif

/*
bool UIPanelTick( const string & uniqueID, Scene::IBLProbe & probeData, vaIBLProbe::UIContext & probeUIContext, vaApplicationBase & application )
{
    bool hadChanges = false;
    // ImGui::PushID(  )
    //probeUIContext.PanelOpen = ImGui::CollapsingHeader( probeUIContext.Name.c_str( ), ImGuiTreeNodeFlags_Framed );
    //if( probeUIContext.PanelOpen )
    {
        vaVector3 pos, ypr, size; // yaw pitch roll
        pos = probeData.Location.Center;
        probeData.Location.Axis.DecomposeRotationYawPitchRoll( ypr.x, ypr.y, ypr.z ); ypr = vaVector3::RadianToDegree( ypr );
        size = probeData.Location.Extents * 2.0f;

        if( ImGui::InputFloat3( "Position", &pos.x ) )
            { probeData.Location.Center = pos; hadChanges = true; }

        if( ImGui::InputFloat3( "Rotation", &ypr.x ) )
            { ypr = vaVector3::DegreeToRadian(ypr); probeData.Location.Axis = vaMatrix3x3::FromYawPitchRoll( ypr.x, ypr.y, ypr.z ); hadChanges = true; }

        if( ImGui::InputFloat3( "Size", &size.x ) )
            { probeData.Location.Extents = size / 2.0f; hadChanges = true; }

        ImGui::Combo( "3D movement mode", (int*)&probeUIContext.GuizmoOperationType, "Translation\0Rotation\0Scale\0\0" );
        probeUIContext.GuizmoOperationType = vaMath::Clamp( probeUIContext.GuizmoOperationType, 0, 2 );

        vaDebugCanvas3D& canvas3D = application.GetRenderDevice().GetCanvas3D();
        canvas3D.DrawBox( probeData.Location, 0xFF00FF00, 0x20808000 );
    }
    return hadChanges;
}
*/
/*
bool vaIBLProbe::UICanvasDraw( Scene::IBLProbe & probeData, UIContext & probeUIContext, vaCameraBase& camera, vaDebugCanvas2D& canvas2D, vaDebugCanvas3D& canvas3D )
{
    canvas2D; canvas3D;
    if( !probeUIContext.PanelOpen )
        return false;

    bool hadChanges = false;

    auto view = camera.GetViewMatrix();
    auto proj = camera.GetProjMatrix(); //ComputeNonReversedZProjMatrix();

    vaMatrix4x4 probeMat = probeData.Location.ToScaledTransform();
    
    // // LH to RH
    // proj.m[2][2] = -proj.m[2][2];
    // proj.m[2][3] = -proj.m[2][3];

    ImGuizmo::Manipulate( &view._11, &proj._11, (ImGuizmo::OPERATION)probeUIContext.GuizmoOperationType, ImGuizmo::WORLD, &probeMat._11 ); //, NULL, useSnap ? &snap.x : NULL );

    probeData.Location = vaOrientedBoundingBox::FromScaledTransform( probeMat );

    hadChanges = true;

    return hadChanges;
}
*/

