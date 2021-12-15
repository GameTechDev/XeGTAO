///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaAssetImporter.h"
#include "Rendering/vaStandardShapes.h"

#include "IntegratedExternals/vaAssimpIntegration.h"

#include "Core/System/vaFileTools.h"

#include "Rendering/vaRenderMesh.h"
#include "Rendering/vaRenderMaterial.h"

#include "Rendering/Effects/vaPostProcess.h"


// #include <d3d11_1.h>
// #include <DirectXMath.h>
// #pragma warning(disable : 4324 4481)
//
// #include <exception>

//#include <mutex>

using namespace Vanilla;

#ifdef VA_ASSIMP_INTEGRATION_ENABLED

static inline vaVector4     AsVA( const aiColor4D & val     )       { return vaVector4( val.r, val.g, val.b, val.a );   }
static inline vaVector3     AsVA( const aiVector3D & val )          { return vaVector3( val.x, val.y, val.z ); }
static inline vaVector3     AsVA( const aiColor3D & val )           { return vaVector3( val.r, val.g, val.b ); }
//static inline vaMatrix4x4   VAFromAI( const aiMatrix4x4 & val )    { return vaMatrix4x4( val.a1, val.a2, val.a3, val.a4, val.b1, val.b2, val.b3, val.b4, val.c1, val.c2, val.c3, val.c4, val.d1, val.d2, val.d3, val.d4 ); }
static inline vaMatrix4x4   AsVA( const aiMatrix4x4 & val )         { return vaMatrix4x4( val.a1, val.b1, val.c1, val.d1, val.a2, val.b2, val.c2, val.d2, val.a3, val.b3, val.c3, val.d3, val.a4, val.b4, val.c4, val.d4 ); }

namespace 
{ 
    class myLogInfoStream : public Assimp::LogStream
    {
        vaAssetImporter::ImporterContext & importerContext;
    public:
            myLogInfoStream(vaAssetImporter::ImporterContext & importerContext) : importerContext(importerContext)  { }
            ~myLogInfoStream()  { }

            void write(const char* message)
            {
                string messageStr = vaStringTools::Trim( message, "\n" );
                VA_LOG( "Assimp info    : %s", messageStr.c_str() );
                importerContext.AddLog( "AI: " + messageStr + "\n" );
            }
    };
    class myLogWarningStream : public Assimp::LogStream
    {
        vaAssetImporter::ImporterContext & importerContext;
    public:
            myLogWarningStream(vaAssetImporter::ImporterContext & importerContext) : importerContext(importerContext)  { }
            ~myLogWarningStream()  { }

            void write(const char* message)
            {
                string messageStr = vaStringTools::Trim( message, "\n" );
                VA_LOG( "Assimp warning : %s", messageStr.c_str() );
                importerContext.AddLog( "AIWarn: " + messageStr + "\n" );
            }
    };
    class myLogErrorStream : public Assimp::LogStream
    {
        vaAssetImporter::ImporterContext & importerContext;
    public:
            myLogErrorStream(vaAssetImporter::ImporterContext & importerContext) : importerContext(importerContext)  { }
            ~myLogErrorStream()  { }

            void write(const char* message)
            {
                string messageStr = vaStringTools::Trim( message, "\n" );
                VA_LOG_ERROR( "Assimp error   : %s", messageStr.c_str() );
                importerContext.AddLog( "AIErr:  " + messageStr + "\n" );
            }
    };
    class myProgressHandler: public Assimp::ProgressHandler
    {
        vaAssetImporter::ImporterContext& importerContext;
    public:
        myProgressHandler( vaAssetImporter::ImporterContext& importerContext ) : importerContext( importerContext ) { }
        ~myProgressHandler( ) { }

        virtual bool Update( float percentage = -1.f ) override
        {
            if( percentage != -1.f )
            {
                importerContext.SetProgress( 0.667f * percentage );
            }

            return !importerContext.IsAborted();
        }
    };
    class myLoggersRAII
    {
        Assimp::LogStream *         m_logInfoStream   ;
        Assimp::LogStream *         m_logWarningStream;
        Assimp::LogStream *         m_logErrorStream  ;
        Assimp::ProgressHandler *   m_progressHandler;

    public:
        myLoggersRAII( vaAssetImporter::ImporterContext & importerContext )
        {
            Assimp::DefaultLogger::create( "AsiimpLog.txt", Assimp::Logger::NORMAL, aiDefaultLogStream_FILE );
            m_logInfoStream       = importerContext.Settings.EnableLogInfo      ? (new myLogInfoStream(importerContext)    ):(nullptr);
            m_logWarningStream    = importerContext.Settings.EnableLogWarning   ? (new myLogWarningStream(importerContext) ):(nullptr);
            m_logErrorStream      = importerContext.Settings.EnableLogError     ? (new myLogErrorStream(importerContext)   ):(nullptr);
            m_progressHandler     = new myProgressHandler(importerContext);
            // set loggers
            if( m_logInfoStream     != nullptr )  Assimp::DefaultLogger::get()->attachStream( m_logInfoStream,        Assimp::Logger::Info ); // | Assimp::Logger::Debugging );
            if( m_logWarningStream  != nullptr )  Assimp::DefaultLogger::get()->attachStream( m_logWarningStream,     Assimp::Logger::Warn );
            if( m_logErrorStream    != nullptr )  Assimp::DefaultLogger::get()->attachStream( m_logErrorStream,       Assimp::Logger::Err );
            //if( m_progressHandler   != nullptr )  Assimp::ProgressHandler::get()->
            //Assimp::ProgressHandler
        }

        ~myLoggersRAII( )
        {
            if( m_logInfoStream       != nullptr )  Assimp::DefaultLogger::get()->detatchStream( m_logInfoStream,        Assimp::Logger::Info ); // | Assimp::Logger::Debugging );
            if( m_logWarningStream    != nullptr )  Assimp::DefaultLogger::get()->detatchStream( m_logWarningStream,     Assimp::Logger::Warn );
            if( m_logErrorStream      != nullptr )  Assimp::DefaultLogger::get()->detatchStream( m_logErrorStream,       Assimp::Logger::Err );
            if( m_logInfoStream       != nullptr )  delete m_logInfoStream   ;
            if( m_logWarningStream    != nullptr )  delete m_logWarningStream;
            if( m_logErrorStream      != nullptr )  delete m_logErrorStream  ;
            delete m_progressHandler;
            Assimp::DefaultLogger::kill();
        }

        Assimp::ProgressHandler *   GetProgressHandler( )   { return m_progressHandler; }
    };


    struct LoadingTempStorage
    {
        struct LoadedTexture
        {
            const aiTexture *                   AssimpTexture;
            shared_ptr<vaAssetTexture>          Texture;
            string                              OriginalPath;
            vaTextureLoadFlags                  TextureLoadFlags;
            vaTextureContentsType               TextureContentsType;

            LoadedTexture( const aiTexture * assimpTexture, const shared_ptr<vaAssetTexture> & texture, const string & originalPath, vaTextureLoadFlags textureLoadFlags, vaTextureContentsType textureContentsType )
                : AssimpTexture( assimpTexture ), Texture(texture), OriginalPath(originalPath), TextureLoadFlags( textureLoadFlags ), TextureContentsType( textureContentsType )
            { }
        };

        struct LoadedMaterial
        {
            const aiMaterial *                  AssimpMaterial;
            shared_ptr<vaAssetRenderMaterial>   Material;

            LoadedMaterial( const aiMaterial * assimpMaterial, const shared_ptr<vaAssetRenderMaterial> & material )
                : AssimpMaterial( assimpMaterial ), Material( material ) 
            { }
        };

        struct LoadedMesh
        {
            const aiMesh *                      AssimpMesh;
            shared_ptr<vaAssetRenderMesh>       Mesh;

            LoadedMesh( const aiMesh * assimpMesh, const shared_ptr<vaAssetRenderMesh> & mesh )
                : AssimpMesh( assimpMesh ), Mesh( mesh ) 
            { }
        };

        string                                      ImportDirectory;
        string                                      ImportFileName;
        string                                      ImportExt;

        std::vector<LoadedTexture>                  LoadedTextures;
        std::vector<LoadedMaterial>                 LoadedMaterials;
        std::vector<LoadedMesh>                     LoadedMeshes;

        shared_ptr<vaAssetRenderMaterial>           FindMaterial( const aiMaterial * assimpMaterial )
        {
            for( int i = 0; i < LoadedMaterials.size(); i++ )
            {
                if( LoadedMaterials[i].AssimpMaterial == assimpMaterial )
                    return LoadedMaterials[i].Material;
            }
            return nullptr;
        }

        shared_ptr<vaAssetRenderMesh>               FindMesh( const aiMesh * assimpMesh )
        {
            for( int i = 0; i < LoadedMeshes.size(); i++ )
            {
                if( LoadedMeshes[i].AssimpMesh == assimpMesh )
                    return LoadedMeshes[i].Mesh;
            }
            return nullptr;
        }
    };
}

//#pragma warning ( suppress: 4505 ) // unreferenced local function has been removed
static shared_ptr<vaAssetTexture> FindOrLoadTexture( aiTexture * assimpTexture, const string & _path, LoadingTempStorage & tempStorage, vaAssetImporter::ImporterContext & importerContext, vaTextureLoadFlags textureLoadFlags, vaTextureContentsType textureContentsType, bool & createdNew )
{
    createdNew = false;

    string originalPath = vaStringTools::ToLower(_path);

    string filePath = originalPath;

    for( int i = 0; i < tempStorage.LoadedTextures.size(); i++ )
    {
        if( (originalPath == tempStorage.LoadedTextures[i].OriginalPath) && (textureLoadFlags == tempStorage.LoadedTextures[i].TextureLoadFlags) && (textureContentsType == tempStorage.LoadedTextures[i].TextureContentsType) )
        {
            assert( assimpTexture == tempStorage.LoadedTextures[i].AssimpTexture );
            return tempStorage.LoadedTextures[i].Texture;
        }
    }

    string outDir, outName, outExt;
    vaFileTools::SplitPath( filePath, &outDir, &outName, &outExt );

    bool foundDDS = outExt == ".dds";
    if( !foundDDS && (importerContext.Settings.TextureOnlyLoadDDS || importerContext.Settings.TextureTryLoadDDS) )
    {
        string filePathDDS = outDir + outName + ".dds";
        if( vaFileTools::FileExists( filePathDDS ) )
        {
            filePath = filePathDDS;
            foundDDS = true;
        }
        else
        {
            filePathDDS = tempStorage.ImportDirectory + outDir + outName + ".dds";
            if( vaFileTools::FileExists( filePathDDS ) )
            {
                filePath = filePathDDS;
                foundDDS = true;
            }
        }
    }

    if( !foundDDS && importerContext.Settings.TextureOnlyLoadDDS )
    {
        VA_LOG( "VaAssetImporter_Assimp : TextureOnlyLoadDDS true but no .dds texture found when looking for '%s'", filePath.c_str() );
        return nullptr;
    }

    if( !vaFileTools::FileExists( filePath ) )
    {
        filePath = tempStorage.ImportDirectory + outDir + outName + "." + outExt;
        if( !vaFileTools::FileExists( filePath ) )
        {
            VA_LOG( "VaAssetImporter_Assimp - Unable to find texture '%s'", filePath.c_str() );
            return nullptr;
        }
    }

    shared_ptr<vaAssetTexture> textureAssetOut;
    if( !importerContext.AsyncInvokeAtBeginFrame( [ & ]( vaRenderDevice & renderDevice, vaAssetImporter::ImporterContext & importerContext )
    {
        shared_ptr<vaTexture> textureOut = vaTexture::CreateFromImageFile( renderDevice, filePath, textureLoadFlags, vaResourceBindSupportFlags::ShaderResource, textureContentsType );

        // this is valid because all of this happens after BeginFrame was called on the device but before main application/sample starts rendering anything
        vaRenderDeviceContext& renderContext = *renderDevice.GetMainContext( );

        if( textureContentsType == vaTextureContentsType::SingleChannelLinearMask && vaResourceFormatHelpers::GetChannelCount( textureOut->GetResourceFormat() ) > 1 )
        {
            vaResourceFormat outFormat = vaResourceFormat::Unknown;
            switch( textureOut->GetResourceFormat( ) )
            {
            case( vaResourceFormat::R8G8B8A8_UNORM ):
            case( vaResourceFormat::B8G8R8A8_UNORM ):
                outFormat = vaResourceFormat::R8_UNORM; break;
            }

            shared_ptr<vaTexture> singleChannelTextureOut = (outFormat==vaResourceFormat::Unknown)?(nullptr):(vaTexture::Create2D( renderDevice, outFormat, textureOut->GetWidth( ), textureOut->GetHeight( ), 1, 1, 1, 
                vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget, vaResourceAccessFlags::Default, outFormat, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic,
                textureOut->GetFlags(), textureOut->GetContentsType() ) );

            if( singleChannelTextureOut != nullptr && renderDevice.GetPostProcess( ).MergeTextures( renderContext, singleChannelTextureOut, textureOut, nullptr, nullptr, "float4( srcA.x, 0, 0, 0 )" ) == vaDrawResultFlags::None )
            {
                VA_LOG( "VaAssetImporter_Assimp - Successfully removed unnecessary color channels for '%s' texture", filePath.c_str( ) );
                textureOut = singleChannelTextureOut;
            }
        }

        if( textureOut != nullptr && importerContext.Settings.TextureGenerateMIPs )
        {
            if( textureOut->GetMipLevels() == 1 )
            {
                auto mipmappedTexture = vaTexture::TryCreateMIPs( renderContext, textureOut );
                if( mipmappedTexture != nullptr )
                {
                    VA_LOG( "VaAssetImporter_Assimp - Successfully created MIPs for '%s' texture", filePath.c_str( ) );
                    textureOut = mipmappedTexture;
                }
                else
                    VA_LOG( "VaAssetImporter_Assimp - Error while creating MIPs for '%s'", filePath.c_str( ) );
            }
            else
            {
                VA_LOG( "VaAssetImporter_Assimp - Texture '%s' already has %d mip levels!", filePath.c_str( ), textureOut->GetMipLevels() );
            }
        }

        if( textureOut == nullptr )
        {
            VA_LOG( "VaAssetImporter_Assimp - Error while loading '%s'", filePath.c_str( ) );
            return false;
        }

        assert( vaThreading::IsMainThread( ) ); // remember to lock asset global mutex and switch these to 'false'
        textureAssetOut = importerContext.AssetPack->Add( textureOut, importerContext.AssetPack->FindSuitableAssetName( importerContext.Settings.AssetNamePrefix + outName, true ), true );

        tempStorage.LoadedTextures.push_back( LoadingTempStorage::LoadedTexture( assimpTexture, textureAssetOut, originalPath, textureLoadFlags, textureContentsType ) );

        return true;
    } ) )
        return nullptr;

    createdNew = true;

    // if( outContent != nullptr )
    //     outContent->LoadedAssets.push_back( textureAssetOut );

    VA_LOG_SUCCESS( "Assimp texture '%s' loaded ok.", filePath.c_str() );

    return textureAssetOut;
}

static bool TexturesIdentical( aiTextureType texType0, unsigned texIndex0, aiTextureType texType1, unsigned texIndex1, aiMaterial* assimpMaterial )
{
    aiString            tex0Path;
    aiTextureMapping    tex0Mapping = aiTextureMapping_UV;
    uint32              tex0UVIndex = 0;
    float               tex0BlendFactor = 0.0f;
    aiTextureOp         tex0Op = aiTextureOp_Add;
    aiTextureMapMode    tex0MapModes[2] = { aiTextureMapMode_Wrap, aiTextureMapMode_Wrap };
    aiTextureFlags      tex0Flags = (aiTextureFlags)0;
    aiString            tex1Path;
    aiTextureMapping    tex1Mapping = aiTextureMapping_UV;
    uint32              tex1UVIndex = 0;
    float               tex1BlendFactor = 0.0f;
    aiTextureOp         tex1Op = aiTextureOp_Add;
    aiTextureMapMode    tex1MapModes[2] = { aiTextureMapMode_Wrap, aiTextureMapMode_Wrap };
    aiTextureFlags      tex1Flags = (aiTextureFlags)0;

    if( aiReturn_SUCCESS != aiGetMaterialTexture( assimpMaterial, texType0, texIndex0, &tex0Path, &tex0Mapping, &tex0UVIndex, &tex0BlendFactor, &tex0Op, tex0MapModes, (unsigned int*)&tex0Flags ) )
        return false;
    if( aiReturn_SUCCESS != aiGetMaterialTexture( assimpMaterial, texType1, texIndex1, &tex1Path, &tex1Mapping, &tex1UVIndex, &tex1BlendFactor, &tex1Op, tex1MapModes, (unsigned int*)&tex1Flags ) )
        return false;

    return (tex0Path == tex1Path) && (tex0Mapping == tex1Mapping) && (tex0UVIndex==tex1UVIndex) && (tex0BlendFactor==tex1BlendFactor) && (tex0Op==tex1Op) && (tex0MapModes[0]==tex1MapModes[0]) && (tex0MapModes[1]==tex1MapModes[1]) && (tex0Flags == tex1Flags);
}

static string ImportTextureNode( vaRenderMaterial & vanillaMaterial, const string & inputTextureNodeName, vaTextureContentsType contentsType, aiTextureType texType, unsigned int texIndex, LoadingTempStorage & tempStorage, vaAssetImporter::ImporterContext & importerContext, aiMaterial* assimpMaterial, bool & createdNew )
{
    aiString            _pathTmp;
    aiTextureMapping    texMapping = aiTextureMapping_UV;
    uint32              texUVIndex = 0;
    float               texBlendFactor = 0.0f;
    aiTextureOp         texOp = aiTextureOp_Add;
    aiTextureMapMode    texMapModes[2] = { aiTextureMapMode_Wrap, aiTextureMapMode_Wrap };
    aiTextureFlags      texFlags = (aiTextureFlags)0;

    //aiReturn texFound = assimpMaterial->GetTexture( texType, texIndex, &pathTmp, &texMapping, &texUVIndex, &texBlendFactor, &texOp, &texMapMode );
    if( aiReturn_SUCCESS != aiGetMaterialTexture( assimpMaterial, texType, texIndex, &_pathTmp, &texMapping, &texUVIndex, &texBlendFactor, &texOp, texMapModes, (unsigned int*)&texFlags ) )
        return "";
    string texPath = _pathTmp.C_Str();

    vaTextureLoadFlags  textureLoadFlags = vaTextureLoadFlags::PresumeDataIsLinear;
    if( contentsType == vaTextureContentsType::GenericColor )
        textureLoadFlags = vaTextureLoadFlags::PresumeDataIsSRGB;

    if( texMapping != aiTextureMapping_UV )
    {
        VA_LOG( "Importer warning: Texture 's' mapping mode not supported (only aiTextureMapping_UV supported), skipping", texPath.c_str( ) );
        return "";
    }

    if( texUVIndex > 1 )
    {
        assert( false ); // it's actually supported, but dots are not connected
        VA_LOG( "Importer warning: Texture 's' UV index out of supported range (this is easy to upgrade), skipping", texPath.c_str() );
        return "";
    }

    if( ( texFlags & aiTextureFlags_Invert ) != 0 )
    { assert( false ); } // not implemented

    if( ( texFlags & aiTextureFlags_UseAlpha ) != 0 )
    { assert( false ); } // not implemented

    if( ( texFlags & aiTextureFlags_IgnoreAlpha ) != 0 )
    { assert( false ); } // not implemented

    // string texCountSuffix = ( texIndex == 0 ) ? ( "" ) : ( vaStringTools::Format( "%d", texIndex ) );

    // We always want sRGB with diffuse/albedo, specular, ambient and reflection colors (not sure about emissive so I'll leave it in; not sure about HDR textures either)
    // Alpha channel is always linear anyway, so for cases where we store opacity in diffuse.a (albedo.a) or 'shininess' in specular.a that's still fine
    if( texType == aiTextureType_DIFFUSE || texType == aiTextureType_SPECULAR || texType == aiTextureType_AMBIENT || texType == aiTextureType_EMISSIVE || texType == aiTextureType_REFLECTION )
    {
        assert( (textureLoadFlags & vaTextureLoadFlags::PresumeDataIsSRGB) != 0 );
        assert( contentsType == vaTextureContentsType::GenericColor );
    }
    // Normals
    else if( texType == aiTextureType_NORMALS )
    {
        assert( (textureLoadFlags & vaTextureLoadFlags::PresumeDataIsLinear) != 0 );
        assert( contentsType == vaTextureContentsType::NormalsXY_UNORM );
    }
    // For height, opacity and others it only makes sense that they were stored in linear
    else if( texType == aiTextureType_LIGHTMAP )
    {
        assert( ( textureLoadFlags & vaTextureLoadFlags::PresumeDataIsLinear ) != 0 );
        assert( contentsType == vaTextureContentsType::GenericLinear );
    }
    else if( texType == aiTextureType_HEIGHT || texType == aiTextureType_OPACITY || texType == aiTextureType_AMBIENT_OCCLUSION || texType == aiTextureType_DISPLACEMENT || texType == aiTextureType_SHININESS )
    {
        assert( ( textureLoadFlags & vaTextureLoadFlags::PresumeDataIsLinear ) != 0 );
        assert( contentsType == vaTextureContentsType::SingleChannelLinearMask );
    }
    else
    {
        // not implemented
        assert( texType == aiTextureType_UNKNOWN );
    }

    shared_ptr<vaAssetTexture> newTextureAsset = FindOrLoadTexture( nullptr, texPath.c_str(), tempStorage, importerContext, textureLoadFlags, contentsType, createdNew );

    if( ( newTextureAsset == nullptr ) || ( newTextureAsset->GetTexture( ) == nullptr ) )
    {
        VA_LOG_WARNING( "Assimp warning: Texture '%s' could not be imported, skipping", texPath.c_str() );
        return "";
    }

    if( texMapModes[0] != texMapModes[1] )
    {
        VA_LOG_WARNING( "Assimp warning: Texture '%s' has mismatched U & V texMapModes (%d, %d) - using first one for both", texPath.c_str(), texMapModes[0], texMapModes[1] );
        return "";
    }

    vaStandardSamplerType samplerType = vaStandardSamplerType::AnisotropicWrap;
    if( texMapModes[0] == aiTextureMapMode_Wrap )
        samplerType = vaStandardSamplerType::AnisotropicWrap;
    else if( texMapModes[0] == aiTextureMapMode_Clamp )
        samplerType = vaStandardSamplerType::AnisotropicClamp;
    else if( texMapModes[0] == aiTextureMapMode_Mirror )
    {
        VA_LOG_WARNING( "Assimp warning: Texture '%s' is using 'mirror' UV sampling mode but it is not supported by the materials", texPath.c_str() );
        return "";
    }
    else if( texMapModes[0] == aiTextureMapMode_Decal )
    {
        VA_LOG_WARNING( "Assimp warning: Texture '%s' is using 'decal' UV sampling mode but it is not supported by the materials", texPath.c_str() );
        return "";
    }

    // string texName;
    // vaFileTools::SplitPath( texPath, nullptr, &texName, nullptr );
    
    string texNodeName = vanillaMaterial.FindAvailableNodeName( inputTextureNodeName.c_str() );
    bool allOk = vanillaMaterial.SetTextureNode( texNodeName, *newTextureAsset->GetTexture(), samplerType, texUVIndex );
    if( !allOk )
    {
        assert( false );
        VA_LOG_WARNING( "Unable to create texture node for '%s'", texPath.c_str() );
    }

    if( allOk )
        return texNodeName;
    else
        return "";
}

static string ImportTextureNode( vaRenderMaterial& vanillaMaterial, const string& inputTextureNodeName, vaTextureContentsType contentsType, aiTextureType texType, unsigned int texIndex, LoadingTempStorage& tempStorage, vaAssetImporter::ImporterContext& importerContext, aiMaterial* assimpMaterial )
{
    bool createdNew = false;
    return ImportTextureNode( vanillaMaterial, inputTextureNodeName, contentsType, texType, texIndex, tempStorage, importerContext, assimpMaterial, createdNew );
}


static bool ProcessTextures( const aiScene* loadedScene, LoadingTempStorage & tempStorage, vaAssetImporter::ImporterContext & importerContext )
{
    loadedScene;
    tempStorage;
    importerContext;

    if( loadedScene->HasTextures( ) )
    {
        VA_LOG_ERROR( "Assimp error: Support for meshes with embedded textures is not implemented" );
        return false;
        // for( int i = 0; i < loadedScene->mNumTextures; i++ )
        // {
        //     aiTexture * texture = loadedScene->mTextures[i];
        // }
    }

/*
	for( unsigned int m = 0; m < loadedScene->mNumMaterials; m++ )
	{
		int texIndex = 0;
		aiReturn texFound = AI_SUCCESS;

		aiString path;	// filename

		while (texFound == AI_SUCCESS)
		{
			texFound = loadedScene->mMaterials[m]->GetTexture( aiTextureType_DIFFUSE, texIndex, &path );
			//textureIdMap[path.data] = NULL; //fill map with textures, pointers still NULL yet

            FindOrLoadTexture( path.data, tempStorage, importerContext, outContent, true, false );

			texIndex++;
		}
	}
    */


    return true;
}

static bool ProcessMaterials( const aiScene* loadedScene, LoadingTempStorage & tempStorage, vaAssetImporter::ImporterContext & importerContext )
{
    #define VERIFY_SUCCESS_OR_BREAK( x )      \
    do                                              \
    {                                               \
    if( !(x==aiReturn::aiReturn_SUCCESS) )          \
    {                                               \
        assert( false );                            \
        break;                                      \
    }                                               \
    } while( false )

    for( unsigned int mi = 0; mi < loadedScene->mNumMaterials; mi++ )
	{
        aiMaterial * assimpMaterial = loadedScene->mMaterials[mi];

        string materialName;
        {
            aiString        matName( "unnamed" );
            VERIFY_SUCCESS_OR_BREAK( assimpMaterial->Get( AI_MATKEY_NAME, matName ) );
            materialName = matName.data;
            VA_LOG( "Assimp processing material '%s'", materialName.c_str( ) );
        }

        // I don't think that this is actually useful for anything anymore
        aiShadingMode   matShadingModel = aiShadingMode_Flat; matShadingModel;
        /*VERIFY_SUCCESS_OR_BREAK( */assimpMaterial->Get( AI_MATKEY_SHADING_MODEL, *(unsigned int*)&matShadingModel )/* )*/;

        shared_ptr<vaRenderMaterial> newMaterial;
        
        if( !importerContext.AsyncInvokeAtBeginFrame( [ & ]( vaRenderDevice& renderDevice, vaAssetImporter::ImporterContext& importerContext )
        {
            newMaterial = renderDevice.GetMaterialManager().CreateRenderMaterial();

            int matGLTFSpecGlossModel = 0;
            if( assimpMaterial->Get( AI_MATKEY_GLTF_PBRSPECULARGLOSSINESS, matGLTFSpecGlossModel ) != aiReturn_SUCCESS )
                matGLTFSpecGlossModel = 0;
            if( matGLTFSpecGlossModel )
            {
                VA_WARN( "gltf2 specular glosiness model not supported yet" );
            }

            vaRenderMaterial::MaterialSettings matSettings = newMaterial->GetMaterialSettings( );

            // some defaults
            //matSettings.ReceiveShadows      = true;
            matSettings.CastShadows         = true;
            matSettings.AlphaTestThreshold  = 0.3f; // use 0.3 instead of 0.5; 0.5 can sometimes make things almost invisible if alpha was stored in .rgb (instead of .a) and there was a linear<->sRGB mixup; can always be tweaked later

            {
                int matTwosided = 0;                                // Specifies whether meshes using this material must be rendered without backface culling. 0 for false, !0 for true.
                assimpMaterial->Get( AI_MATKEY_TWOSIDED, matTwosided );
                matSettings.FaceCull = ( matTwosided == 0 ) ? ( vaFaceCull::Back ) : ( vaFaceCull::None );
            }

            {
                int             matWireframe = 0;                                // Specifies whether wireframe rendering must be turned on for the material. 0 for false, !0 for true.	
                assimpMaterial->Get( AI_MATKEY_ENABLE_WIREFRAME, *(unsigned int*)&matWireframe );
                // matSettings.Wireframe = matWireframe != 0;
            }

            aiColor4D baseColor;

            bool mergeOpacityMaskToColor = false;

            // First check whether this is a GLTF_PBR
            if( (assimpMaterial->Get( AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_FACTOR, baseColor ) == aiReturn_SUCCESS) && (matGLTFSpecGlossModel == 0) )
            {
                int matUnlit = 0;
                if( assimpMaterial->Get( AI_MATKEY_GLTF_UNLIT, matUnlit ) != aiReturn_SUCCESS )
                    matUnlit = 0;

                if( matUnlit != 0 )
                    newMaterial->SetupFromPreset( "FilamentUnlit" );
                else
                    newMaterial->SetupFromPreset( "FilamentStandard" );

                float metallic  = 1.0;
                float roughness = 1.0;
                float occlusion = 1.0;

                if( !assimpMaterial->Get( AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLIC_FACTOR, metallic ) == aiReturn_SUCCESS )  { assert( false ); }
                if( !assimpMaterial->Get( AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR, roughness ) == aiReturn_SUCCESS ) { assert( false ); }
                //if( !assimpMaterial->Get( AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_OCCLUSION_FACTOR, occlusion ) == aiReturn_SUCCESS ) { assert( false ); }
                
                // see https://github.com/KhronosGroup/glTF/tree/master/specification/2.0 for the specs

                newMaterial->SetInputSlotDefaultValue( "BaseColor", vaVector4::SRGBToLinear( AsVA( baseColor ) ) );
                string baseColorTextureName = ImportTextureNode( *newMaterial, "BaseColorTex", vaTextureContentsType::GenericColor, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_TEXTURE, tempStorage, importerContext, assimpMaterial );
                if( baseColorTextureName != "" )
                    newMaterial->ConnectInputSlotWithNode( "BaseColor", baseColorTextureName ) ;

                newMaterial->SetInputSlotDefaultValue( "Roughness", roughness );
                newMaterial->SetInputSlotDefaultValue( "Metallic", metallic );
                newMaterial->SetInputSlotDefaultValue( "AmbientOcclusion", occlusion );

                // From the specs:
                // "The metallic-roughness texture. The metalness values are sampled from the B channel. The roughness values are sampled from the G channel. 
                // These values are linear. If other channels are present (R or A), they are ignored for metallic-roughness calculations."
                
                // Additionally, (ambient) occlusion could be the same in which case let's just load it as one
                // (there's more info at https://github.com/KhronosGroup/glTF/issues/857 and related threads)
                if( TexturesIdentical( AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE, aiTextureType_LIGHTMAP, 0, assimpMaterial ) )
                {
                    string omrTextureName = ImportTextureNode( *newMaterial, "OcclMetalRoughTex", vaTextureContentsType::GenericLinear, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE, tempStorage, importerContext, assimpMaterial );
                    if( omrTextureName != "" )
                    {
                        assert( roughness == 1.0f );    
                        assert( metallic == 1.0f );
                        assert( occlusion == 1.0f );

                        float occlusionTextureStrength = 1.0f;
                        if( !assimpMaterial->Get( AI_MATKEY_GLTF_TEXTURE_STRENGTH(aiTextureType_LIGHTMAP, 0), occlusionTextureStrength ) == aiReturn_SUCCESS ) { assert( false ); }
                        assert( occlusionTextureStrength == 1.0f ); // different from 1.0f not implemented yet !!! if changing make sure to change the alone one below !!!

                        newMaterial->ConnectInputSlotWithNode( "Roughness", omrTextureName, "y" );
                        newMaterial->ConnectInputSlotWithNode( "Metallic", omrTextureName, "z" );
                        newMaterial->ConnectInputSlotWithNode( "AmbientOcclusion", omrTextureName, "x" );   // not 100% sure how to know whether to use this or not
                    }
                    else
                    { assert( false ); }
                }
                else
                {
                    string mrTextureName = ImportTextureNode( *newMaterial, "MetallicRoughnessTex", vaTextureContentsType::GenericLinear, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE, tempStorage, importerContext, assimpMaterial );
                    if( mrTextureName != "" )
                    {
                        newMaterial->ConnectInputSlotWithNode( "Roughness", mrTextureName, "y" );
                        newMaterial->ConnectInputSlotWithNode( "Metallic", mrTextureName, "z" );
                    }
                    string occlusionTextureName = ImportTextureNode( *newMaterial, "OcclusionTex", vaTextureContentsType::GenericLinear, aiTextureType_LIGHTMAP, 0, tempStorage, importerContext, assimpMaterial );
                    if( occlusionTextureName != "" )
                    {
                        float occlusionTextureStrength = 1.0f;
                        if( !assimpMaterial->Get( AI_MATKEY_GLTF_TEXTURE_STRENGTH( aiTextureType_LIGHTMAP, 0 ), occlusionTextureStrength ) == aiReturn_SUCCESS ) { assert( false ); }
                        assert( occlusionTextureStrength == 1.0f ); // different from 1.0f not implemented yet !!! if changing make sure to change the combined one above!!!

                        newMaterial->ConnectInputSlotWithNode( "AmbientOcclusion", occlusionTextureName, "x" );
                    }
                }

                {
                    string normalmapTextureName = ImportTextureNode( *newMaterial, "NormalmapTex", vaTextureContentsType::NormalsXY_UNORM, aiTextureType_NORMALS, 0, tempStorage, importerContext, assimpMaterial );
                    if( normalmapTextureName != "" )
                    {
                        float textureScale = 1.0f;
                        if( !assimpMaterial->Get( AI_MATKEY_GLTF_TEXTURE_SCALE( aiTextureType_NORMALS, 0 ), textureScale ) == aiReturn_SUCCESS ) { assert( false ); }
                        if( textureScale != 1.0f )
                        {
                            // assert( textureScale == 1.0f ); // different from 1.0f not implemented yet !!!
                            VA_WARN( "AI_MATKEY_GLTF_TEXTURE_SCALE set to %.3f but only 1.0 (no scaling) currently supported!", textureScale );
                        }
                        newMaterial->ConnectInputSlotWithNode( "Normal", normalmapTextureName );
                    }
                }

                {
                    // The RGB components of the emissive color of the material. These values are linear. If an emissiveTexture is specified, this value is multiplied with the texel values.
                    aiColor3D emissiveColor;
                    assimpMaterial->Get( AI_MATKEY_COLOR_EMISSIVE, emissiveColor );
                    newMaterial->SetInputSlotDefaultValue( "emissiveColor", vaVector3::SRGBToLinear( AsVA( emissiveColor ) ) );

                    string emissiveTex = ImportTextureNode( *newMaterial, "EmissiveTex", vaTextureContentsType::GenericColor, aiTextureType_EMISSIVE, 0, tempStorage, importerContext, assimpMaterial );
                    if( emissiveTex != "" )
                    {
                        newMaterial->ConnectInputSlotWithNode( "EmissiveColor", emissiveTex );
                        assert( emissiveColor.r == 1.0f && emissiveColor.g == 1.0f && emissiveColor.b == 1.0f );  // different from 1.0f not implemented yet !!!
                    }
                }

                {
                    aiString _alphaMode;
                    assimpMaterial->Get( AI_MATKEY_GLTF_ALPHAMODE, _alphaMode ); 
                    string alphaMode = _alphaMode.C_Str();
                    assimpMaterial->Get( AI_MATKEY_GLTF_ALPHACUTOFF, matSettings.AlphaTestThreshold );
                    if( alphaMode == "OPAQUE" )
                    {
                        matSettings.LayerMode = vaLayerMode::Opaque;
                    } 
                    else if( alphaMode == "MASK" )
                    {
                        matSettings.LayerMode = vaLayerMode::AlphaTest;
                    }
                    else if( alphaMode == "BLEND" )
                    {
                        matSettings.LayerMode = vaLayerMode::Transparent;
                    } 
                    else { assert( false ); }
                }
            }
            else if( ( assimpMaterial->Get( AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_FACTOR, baseColor ) == aiReturn_SUCCESS ) && ( matGLTFSpecGlossModel != 0 ) )
            {
                VA_LOG( "Support for GLTF SpecGloss model not implemented although placeholder is here and it shouldn't be too difficult! (perhaps merge it with regular PBR above)" );
                newMaterial->SetupFromPreset( "FilamentSpecGloss" );
            }
            else
            {
                // just use this as a default
                newMaterial->SetupFromPreset( "FilamentSpecGloss" );

                aiColor3D       matColorDiffuse         = aiColor3D( 1.0f, 1.0f, 1.0f );
                aiColor3D       matColorSpecular        = aiColor3D( 0.0f, 0.0f, 0.0f );
                aiColor3D       matColorAmbient         = aiColor3D( 0.0f, 0.0f, 0.0f );
                aiColor3D       matColorEmissive        = aiColor3D( 0.0f, 0.0f, 0.0f );
                // aiColor3D       matColorTransparent     = aiColor3D( 0.0f, 0.0f, 0.0f );    // for transparent materials / not sure if it is intended for subsurface scattering?
                float           matSpecularPow          = 1.0f;                             // SHININESS - Defines the shininess of a phong-shaded material. This is actually the exponent of the phong specular equation; SHININESS=0 is equivalent to SHADING_MODEL=aiShadingMode_Gouraud.
                float           matSpecularMul          = 1.0f;                             // SHININESS_STRENGTH - Scales the specular color of the material.This value is kept separate from the specular color by most modelers, and so do we.
                float           matTransparencyFactor   = 0.0f;                             // from FBX docs: "This property is used to make a surface more or less opaque (0 = opaque, 1 = transparent)." http://docs.autodesk.com/FBX/2014/ENU/FBX-SDK-Documentation/cpp_ref/class_fbx_surface_lambert.html#ac47de888da8afd942fcfb6ccfbe28dda
                float           matOpacity              = 1.0f;
                // aiColor3D       matColorReflective      = aiColor3D( 0.0f, 0.0f, 0.0f );    // not sure what this is
                // float           matReflectivity         = 1.0f;
                // float           matRefractI             = 1.0f;                             // Defines the Index Of Refraction for the material. That's not supported by most file formats.	Might be of interest for raytracing.
                // float           matBumpScaling          = 1.0f;
                // float           matDisplacementScaling  = 1.0f;
                //aiBlendMode     matBlendMode            = aiBlendMode_Default;            // Blend mode - this enum is ignored, it's not useful (only supports 'normal' and 'additive' blending)
                    

                assimpMaterial->Get( AI_MATKEY_COLOR_DIFFUSE,       matColorDiffuse             );
                assimpMaterial->Get( AI_MATKEY_COLOR_SPECULAR,      matColorSpecular            );
                assimpMaterial->Get( AI_MATKEY_COLOR_AMBIENT,       matColorAmbient             );
                assimpMaterial->Get( AI_MATKEY_COLOR_EMISSIVE,      matColorEmissive            );
                // assimpMaterial->Get( AI_MATKEY_COLOR_TRANSPARENT,   matColorTransparent         );    // this gets baked into AI_MATKEY_OPACITY by the FBX importer
                assimpMaterial->Get( AI_MATKEY_SHININESS,           matSpecularPow              );
                assimpMaterial->Get( AI_MATKEY_SHININESS_STRENGTH,  matSpecularMul              );
                assimpMaterial->Get( AI_MATKEY_TRANSPARENCYFACTOR,  matTransparencyFactor       );
                assimpMaterial->Get( AI_MATKEY_OPACITY,             matOpacity                  );
                // assimpMaterial->Get( AI_MATKEY_COLOR_REFLECTIVE,    matColorReflective          );
                // assimpMaterial->Get( AI_MATKEY_REFLECTIVITY,        matReflectivity             );
                // assimpMaterial->Get( AI_MATKEY_REFRACTI,            matRefractI                 );
                // assimpMaterial->Get( AI_MATKEY_BUMPSCALING,         matBumpScaling              );
                // assimpMaterial->Get( "$mat.displacementscaling", 0, 0, matDisplacementScaling   );
                //assimpMaterial->Get( AI_MATKEY_BLEND_FUNC, *(unsigned int*)&matBlendMode );
                //assimpMaterial->Get( AI_MATKEY_GLOBAL_BACKGROUND_IMAGE "?bg.global",0,0

                // I don't actually know if there's a convention - some resources say the range goes from [2,2048], some say [2, 1024] - since it looks like my specific FBX goes from [2, 1024], let's just use that 
                float matGlossiness = (std::log2f( matSpecularPow ) - 1.0f) / 10.0f; // <- currently set for [2, 2048], change '/10.0' to '/9.0' for [2, 1024]
                matGlossiness = vaMath::Saturate(matGlossiness);

                VA_LOG( "" );
                VA_LOG( "Assimp material input analysis for '%s'", materialName.c_str( ) );
                VA_LOG( "   matColorDiffuse          %.3f, %.3f, %.3f", matColorDiffuse.r    , matColorDiffuse.g    , matColorDiffuse.b       );
                VA_LOG( "   matColorSpecular         %.3f, %.3f, %.3f", matColorSpecular.r   , matColorSpecular.g   , matColorSpecular.b      );
                VA_LOG( "   matColorAmbient          %.3f, %.3f, %.3f", matColorAmbient.r    , matColorAmbient.g    , matColorAmbient.b       );
                VA_LOG( "   matColorEmissive         %.3f, %.3f, %.3f", matColorEmissive.r   , matColorEmissive.g   , matColorEmissive.b      );
                // VA_LOG( "   matColorTransparent      %.3f, %.3f, %.3f", matColorTransparent.r, matColorTransparent.g, matColorTransparent.b   );
                // VA_LOG( "   matColorReflective       %.3f, %.3f, %.3f", matColorReflective.r , matColorReflective.g , matColorReflective.b    );   
                VA_LOG( "   matSpecularPow           %.3f      (matGlossiness: %.3f)", matSpecularPow, matGlossiness         );   
                VA_LOG( "   matSpecularMul           %.3f", matSpecularMul         );   
                VA_LOG( "   matTransparencyFactor    %.3f", matTransparencyFactor  );   
                VA_LOG( "   matOpacity               %.3f", matOpacity             );   
                // VA_LOG( "   matReflectivity          %.3f", matReflectivity        );   
                // VA_LOG( "   matRefractI              %.3f", matRefractI            );   
                // VA_LOG( "   matBumpScaling           %.3f", matBumpScaling         );   
                // VA_LOG( "   matDisplacementScaling   %.3f", matDisplacementScaling );
                VA_LOG( "" );


                // I'm not sure how to handle matTransparencyFactor

                if( matOpacity < 1.0f )
                {
                    matSettings.LayerMode = vaLayerMode::Transparent;
                }

                // opacity mask
                string opacityMaskTextureName;
                //bool shouldRemoveOpacityAssetIfMerged = false;
                {
                    opacityMaskTextureName = ImportTextureNode( *newMaterial, "OpacityTex", vaTextureContentsType::SingleChannelLinearMask, aiTextureType_OPACITY, 0, tempStorage, importerContext, assimpMaterial );
                    if( opacityMaskTextureName != "" )
                    {
                        newMaterial->SetInputSlot( "Opacity", 1.0f, false, false );    // we use 1.0 because deffuseAndAlpha above already picked up opacity in .alpha and will get multiplied by the output of this whole Opacity thing
                        newMaterial->ConnectInputSlotWithNode( "Opacity", opacityMaskTextureName );
                        matSettings.LayerMode = vaLayerMode::AlphaTest;
                        mergeOpacityMaskToColor = false;

                        if( matOpacity == 0.0f )
                        {
                            VA_LOG_WARNING( "Assimp warning: in '%s' material, opacity value is set to 0 (makes no sense) and there's an opacity mask texture - resetting opacity value to 1", materialName.c_str() );
                            matOpacity = 1.0f;
                        }
                    }
                }

                if( matSettings.LayerMode == vaLayerMode::Transparent && ( matOpacity == 0.0f ) )
                {
                    VA_LOG_WARNING( "Assimp warning: in '%s' material, opacity value is set to 0 (makes no sense) and there's no opacity mask texture - resetting opacity value to something visible", materialName.c_str( ) );
                    matOpacity = 0.5f;
                }

                // diffuse color
                {
                    vaVector4 diffuseAndAlpha = vaVector4( vaVector3::SRGBToLinear( AsVA( matColorDiffuse ) ), matOpacity );
                    newMaterial->SetInputSlotDefaultValue( "BaseColor", diffuseAndAlpha );
                    string baseColorTextureName = ImportTextureNode( *newMaterial, "BaseColorTex", vaTextureContentsType::GenericColor, aiTextureType_DIFFUSE, 0, tempStorage, importerContext, assimpMaterial );
                    if( baseColorTextureName != "" )
                    {
                        newMaterial->ConnectInputSlotWithNode( "BaseColor", baseColorTextureName );

#if 0
                        // all of this should happen in the ImportTextureNode instead - or actually don't waste time on it at all, just convert to single channel alpha and be done with it
                        if( mergeOpacityMaskToColor )
                        {
                            auto opacitySlot = newMaterial->FindInputSlot("Opacity");
                            auto baseTextureNode    = newMaterial->FindNode<vaRenderMaterial::TextureNode>( baseColorTextureName );
                            auto opacityTextureNode = newMaterial->FindNode<vaRenderMaterial::TextureNode>( opacityMaskTextureName );
                            shared_ptr<vaTexture> baseTexture    = (baseTextureNode!=nullptr)?(baseTextureNode->GetTexture( )):(nullptr);
                            shared_ptr<vaTexture> opacityTexture = (opacityTextureNode!=nullptr)?(opacityTextureNode->GetTexture( )):(nullptr);

                            vaResourceFormat outFormat = vaResourceFormat::Unknown;
                            switch( baseTexture->GetResourceFormat( ) )
                            {
                                case( vaResourceFormat::R8G8B8A8_UNORM_SRGB ): 
                                case( vaResourceFormat::B8G8R8A8_UNORM_SRGB ):
                                case( vaResourceFormat::BC1_UNORM_SRGB ): 
                                case( vaResourceFormat::BC2_UNORM_SRGB ):
                                case( vaResourceFormat::BC7_UNORM_SRGB ):
                                    outFormat = vaResourceFormat::R8G8B8A8_UNORM_SRGB; break;
                            default:
                                assert( false );
                                VA_LOG_WARNING( "Assimp warning: in '%s' material, mergeOpacityMaskToColor was requested but failed to work", materialName.c_str( ) );
                                break;
                            }

                            vaAsset * baseTextureAsset = baseTexture->GetParentAsset();
                            vaAssetTexture * opacityTextureAsset = opacityTexture->GetParentAsset<vaAssetTexture>();

                            if( opacitySlot.has_value( ) && opacitySlot->GetType( ) == vaRenderMaterial::ValueTypeIndex::Scalar && std::get<float>( opacitySlot->GetDefaultValue( ) ) == 1.0f
                                && baseTexture != nullptr && opacityTexture != nullptr && outFormat != vaResourceFormat::Unknown && baseTextureAsset != nullptr )
                            {
                                // this is valid because all of this happens after BeginFrame was called on the device but before main application/sample starts rendering anything
                                vaRenderDeviceContext & renderContext = *renderDevice.GetMainContext( );

                                // one could get all this out and make it more general
                                shared_ptr<vaTexture> outTexture = vaTexture::Create2D( renderDevice, outFormat, baseTexture->GetWidth(), baseTexture->GetHeight(), 1, 1, 1, vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget, vaResourceAccessFlags::Default, outFormat );

                                if( renderDevice.GetPostProcess( ).MergeTextures( renderContext, outTexture, baseTexture, opacityTexture, nullptr ) == vaDrawResultFlags::None )
                                {
                                    if( importerContext.Settings.TextureGenerateMIPs )
                                    {
                                        auto mipmappedTexture = vaTexture::TryCreateMIPs( renderContext, outTexture );
                                        if( mipmappedTexture != nullptr )
                                            outTexture = mipmappedTexture;
                                        else
                                            VA_LOG( "VaAssetImporter_Assimp - Error while creating MIPs for merged opacity / base color texture '%s'", materialName.c_str( ) );
                                    }

                                    baseTextureAsset->ReplaceAssetResource( outTexture );

                                    newMaterial->RemoveNode( opacityMaskTextureName, true );
                                    newMaterial->RemoveInputSlot( "Opacity", true );

                                    if( shouldRemoveOpacityAssetIfMerged )
                                    {
                                        bool foundOK = false;
                                        for( int i = 0; i < tempStorage.LoadedTextures.size( ); i++ )
                                        {
                                            if( tempStorage.LoadedTextures[i].Texture.get( ) == opacityTextureAsset )
                                            {
                                                tempStorage.LoadedTextures.erase( tempStorage.LoadedTextures.begin() + i );
                                                foundOK = true;
                                                break;
                                            }
                                        }
                                        assert( foundOK );

                                        importerContext.AssetPack->Remove( opacityTextureAsset, true );
                                    }
                                    

                                    VA_LOG( "Assimp: in '%s' material, Opacity mask texture was successfully merged into BaseColor", materialName.c_str( ) );
                                }
                                else
                                {
                                    assert( false );
                                    VA_LOG_WARNING( "Assimp warning: in '%s' material, mergeOpacityMaskToColor was requested but Merge failed to work", materialName.c_str( ) );
                                }
                            }
                            else
                            {
                                assert( false );
                                VA_LOG_WARNING( "Assimp warning: in '%s' material, mergeOpacityMaskToColor was requested but failed to work", materialName.c_str( ) );
                            }
                        }
#endif
                    }
                }

                // specular color & glosiness
                {
                    newMaterial->SetInputSlot( "SpecularColor", vaVector3::SRGBToLinear( AsVA( matColorSpecular ) ), true, true );

                    // instead of 'Glossiness' we use 'InvGlossiness' which is 1-Glossiness which is basically roughness - just to match Amazon Lumberyard Bistro textures setup
                    newMaterial->RemoveInputSlot( "Glossiness", true );

                    //newMaterial->SetInputSlot( "Glossiness", matGlossiness, true, false );
                    newMaterial->SetInputSlot( "InvGlossiness", 1.0f - matGlossiness, true, false );
                    
                    string specGlossColorTextureName = ImportTextureNode( *newMaterial, "SpecularColorTex", vaTextureContentsType::GenericColor, aiTextureType_SPECULAR, 0, tempStorage, importerContext, assimpMaterial );
                    if( specGlossColorTextureName != "" )
                    {
                        newMaterial->ConnectInputSlotWithNode( "SpecularColor", specGlossColorTextureName, "xyz" );
                        newMaterial->ConnectInputSlotWithNode( "InvGlossiness", specGlossColorTextureName, "w" );
                    }
                }

                // normals
                {
                    string normalmapTextureName = ImportTextureNode( *newMaterial, "NormalmapTex", vaTextureContentsType::NormalsXY_UNORM, aiTextureType_NORMALS, 0, tempStorage, importerContext, assimpMaterial );
                    if( normalmapTextureName != "" )
                    {
                        float textureScale = 1.0f;
                        if( !assimpMaterial->Get( AI_MATKEY_GLTF_TEXTURE_SCALE( aiTextureType_NORMALS, 0 ), textureScale ) == aiReturn_SUCCESS ) { }
                        assert( textureScale == 1.0f ); // different from 1.0f not implemented yet !!!
                        newMaterial->ConnectInputSlotWithNode( "Normal", normalmapTextureName );
                    }
                }

                // emissive not added yet (no specific reason, just no example of it in Bistro dataset)
                {
                    if( AsVA( matColorEmissive ).Length( ) > 0 )
                    {
                        // SetInputSlot( "EmissiveColor", vaVector3( 1.0f, 1.0f, 1.0f ), true );
                        // SetInputSlot( "EmissiveIntensity", 0.0f, false );
                        assert( false );
                    }
                }

                // ambient occlusion
                {
                    newMaterial->SetInputSlotDefaultValue( "AmbientOcclusion", 1.0f );
                    string AOTextureName = ImportTextureNode( *newMaterial, "AmbientOcclusionTex", vaTextureContentsType::SingleChannelLinearMask, aiTextureType_AMBIENT_OCCLUSION, 0, tempStorage, importerContext, assimpMaterial );
                    if( AOTextureName != "" )
                    {
                        assert( false ); // this probably works but hasn't been tested yet!
                        newMaterial->ConnectInputSlotWithNode( "AmbientOcclusion", AOTextureName, "x" );
                    }
                }
            }
            
            if( matSettings.LayerMode == vaLayerMode::AlphaTest )
                matSettings.FaceCull = vaFaceCull::None; // this seems to work better

            if( matSettings.LayerMode == vaLayerMode::Transparent || matSettings.LayerMode == vaLayerMode::AlphaTest )
            {
                matSettings.FaceCull = vaFaceCull::None; // "none" is here only because of the incomplete transparencies solution on the rendering side
                matSettings.CastShadows = false;
            }
            if( mergeOpacityMaskToColor )
            {
                VA_LOG_WARNING( "Assimp warning: in '%s' material, opacity mask needs to get merged into the baseColor", materialName.c_str() );
            }

            newMaterial->SetMaterialSettings( matSettings );

            assert( vaThreading::IsMainThread() ); // remember to lock asset global mutex and switch these to 'false'
            materialName = importerContext.AssetPack->FindSuitableAssetName( importerContext.Settings.AssetNamePrefix + materialName, true );

            assert( vaThreading::IsMainThread() ); // remember to lock asset global mutex and switch these to 'false'
            auto materialAsset = importerContext.AssetPack->Add( newMaterial, materialName, true );

            VA_LOG_SUCCESS( "    material '%s' added", materialName.c_str() );

            tempStorage.LoadedMaterials.push_back( LoadingTempStorage::LoadedMaterial( assimpMaterial, materialAsset ) );

            return true;
        } ) )
            return false;

        // This below is not the way to do it - instead add a "SyncAndFlush" function to vaRenderDevice - check the comment there
        //
        // // do one empty loop to let the above one clear up some resources that only get cleaned up after full two frames - yeah, a bit weird 
        // if( !importerContext.AsyncInvokeAtBeginFrame( [ & ]( vaRenderDevice& , vaAssetImporter::ImporterContext&  ) 
        //     { return true; 
        //     } ) )
        //     { assert( false ); return false; }

	}

    return true;
}

static bool ProcessMeshes( const aiScene* loadedScene, LoadingTempStorage & tempStorage, vaAssetImporter::ImporterContext & importerContext )
{
	for( unsigned int mi = 0; mi < loadedScene->mNumMeshes; mi++ )
	{
        aiMesh * assimpMesh = loadedScene->mMeshes[mi];

        bool hasTangentBitangents = assimpMesh->HasTangentsAndBitangents();

        VA_LOG( "Assimp processing mesh '%s'", assimpMesh->mName.data );

        if( !assimpMesh->HasFaces() )
        { 
            assert( false );
            VA_LOG_ERROR( "Assimp error: mesh '%s' has no faces, skipping.", assimpMesh->mName.data );
            continue;
        }

        if( assimpMesh->mPrimitiveTypes != aiPrimitiveType_TRIANGLE )
        { 
            VA_LOG_WARNING( "Assimp warning: mesh '%s' reports non-triangle primitive types - those will be skipped during import.", assimpMesh->mName.data );
        }

        if( !assimpMesh->HasPositions() )
        { 
            assert( false );
            VA_LOG_ERROR( "Assimp error: mesh '%s' does not have positions, skipping.", assimpMesh->mName.data );
            continue;
        }
        if( !assimpMesh->HasNormals() )
        { 
            //assert( false );
            VA_LOG_ERROR( "Assimp error: mesh '%s' does not have normals, skipping.", assimpMesh->mName.data );
            continue;
        }

        std::vector<vaVector3>   vertices;
        std::vector<uint32>      colors;
        std::vector<vaVector3>   normals;
        //std::vector<vaVector4>   tangents;  // .w holds handedness
        std::vector<vaVector2>   texcoords0;
        std::vector<vaVector2>   texcoords1;

        vertices.resize( assimpMesh->mNumVertices );
        colors.resize( vertices.size( ) );
        normals.resize( vertices.size( ) );
        //tangents.resize( vertices.size( ) );
        texcoords0.resize( vertices.size( ) );
        texcoords1.resize( vertices.size( ) );


        for( int i = 0; i < (int)vertices.size(); i++ )
            vertices[i] = vaVector3( assimpMesh->mVertices[i].x, assimpMesh->mVertices[i].y, assimpMesh->mVertices[i].z );

        if( assimpMesh->HasVertexColors( 0 ) )
        {
            for( int i = 0; i < (int)colors.size(); i++ )
                colors[i] = vaVector4::ToRGBA( vaVector4( assimpMesh->mColors[0][i].r, assimpMesh->mColors[0][i].g, assimpMesh->mColors[0][i].b, assimpMesh->mColors[0][i].a ) );
        }
        else
        {
            for( int i = 0; i < (int)colors.size(); i++ )
                colors[i] = vaVector4::ToRGBA( vaVector4( 1.0f, 1.0f, 1.0f, 1.0f ) );
        }

        for( int i = 0; i < (int)vertices.size(); i++ )
            normals[i] = vaVector3( assimpMesh->mNormals[i].x, assimpMesh->mNormals[i].y, assimpMesh->mNormals[i].z );

        if( hasTangentBitangents )
        {
            VA_LOG_WARNING( "Assimp importer warning: mesh '%s' has (co)tangent space in the vertices but these are not supported by VA (generated in the pixel shader)", assimpMesh->mName.data );
            // for( int i = 0; i < (int)tangents.size(); i++ )
            // {
            //     vaVector3 tangent   = vaVector3( assimpMesh->mTangents[i].x, assimpMesh->mTangents[i].y, assimpMesh->mTangents[i].z );
            //     vaVector3 bitangent = vaVector3( assimpMesh->mBitangents[i].x, assimpMesh->mBitangents[i].y, assimpMesh->mBitangents[i].z );
            //     float handedness = vaVector3::Dot( bitangent, vaVector3::Cross( normals[i], tangent ).Normalized() );
            //     tangents[i] = vaVector4( tangent, handedness );
            // }
        }
        else
        {
            // // VA_LOG_WARNING( "Assimp importer warning: mesh '%s' has no tangent space - ok if no normal maps used", assimpMesh->mName.data );
            // 
            // for( size_t i = 0; i < tangents.size( ); i++ )
            // {
            //     vaVector3 bitangent = ( vertices[i] + vaVector3( 0.0f, 0.0f, -5.0f ) ).Normalized( );
            //     if( vaMath::Abs( vaVector3::Dot( bitangent, normals[i] ) ) > 0.9f )
            //         bitangent = ( vertices[i] + vaVector3( -5.0f, 0.0f, 0.0f ) ).Normalized( );
            //     tangents[i] = vaVector4( vaVector3::Cross( bitangent, normals[i] ).Normalized( ), 1.0f );
            // }
        }

        std::vector<vaVector2> * uvsOut[] = { &texcoords0, &texcoords1 };

        for( int uvi = 0; uvi < 2; uvi++ )
        {
            std::vector<vaVector2> & texcoords = *uvsOut[uvi];

            if( assimpMesh->HasTextureCoords( uvi ) )
            {
                for( int i = 0; i < (int)texcoords0.size(); i++ )
                    texcoords[i] = vaVector2( assimpMesh->mTextureCoords[uvi][i].x, assimpMesh->mTextureCoords[uvi][i].y );
            }
            else
            {
                for( int i = 0; i < (int)texcoords0.size(); i++ )
                    texcoords[i] = vaVector2( 0.0f, 0.0f );
            }
        }

        bool indicesOk = true;
        std::vector<uint32>      indices;
        indices.reserve( assimpMesh->mNumFaces * 3 );
        for( int i = 0; i < (int)assimpMesh->mNumFaces; i ++ )
        {
            if( assimpMesh->mFaces[i].mNumIndices != 3 )
            {
                continue;
                //assert( false );
                //VA_LOG_ERROR( "Assimp error: mesh '%s' face has incorrect number of indices (3), skipping.", assimpMesh->mName.data );
                //indicesOk = false;
                //break;
            }
            indices.push_back( assimpMesh->mFaces[i].mIndices[0] );
            indices.push_back( assimpMesh->mFaces[i].mIndices[1] );
            indices.push_back( assimpMesh->mFaces[i].mIndices[2] );
        }
        if( !indicesOk )
            continue;

        auto materialAsset = tempStorage.FindMaterial( loadedScene->mMaterials[assimpMesh->mMaterialIndex] );
        auto material = materialAsset->GetRenderMaterial();

        
        shared_ptr<vaAssetRenderMesh> newAsset;
        if( !importerContext.AsyncInvokeAtBeginFrame( [ & ]( vaRenderDevice& renderDevice, vaAssetImporter::ImporterContext& importerContext )
        {
            //shared_ptr<vaRenderMesh> newMesh = vaRenderMesh::Create( importerContext.BaseTransform, vertices, normals, tangents, texcoords0, texcoords1, indices, vaWindingOrder::Clockwise );
            shared_ptr<vaRenderMesh> newMesh = vaRenderMesh::Create( renderDevice, vaMatrix4x4::Identity, vertices, normals, texcoords0, texcoords1, indices, vaWindingOrder::Clockwise );
            newMesh->SetMaterial( material );
            //newMesh->SetTangentBitangentValid( hasTangentBitangents );

            string newMeshName = assimpMesh->mName.data;
            if( newMeshName == "" )
                newMeshName = materialAsset->Name() + "_mesh";  // empty name? just used $materialname$_mesh - but don't add AssetNamePrefix because it was already added to material
            else
                newMeshName = importerContext.Settings.AssetNamePrefix + newMeshName;

            newMeshName = importerContext.AssetPack->FindSuitableAssetName( newMeshName, true );

            assert( vaThreading::IsMainThread() ); // remember to lock asset global mutex and switch these to 'false'
            newAsset = importerContext.AssetPack->Add( newMesh, newMeshName, true );

            VA_LOG_SUCCESS( "    mesh '%s' added", newMeshName.c_str() );
            return true;
        } ) )
            return false;

        tempStorage.LoadedMeshes.push_back( LoadingTempStorage::LoadedMesh( assimpMesh, newAsset ) );
    }

    return true;
}

static bool ProcessNodesRecursive( const aiScene * loadedScene, aiNode * mNode, LoadingTempStorage & tempStorage, vaAssetImporter::ImporterContext & importerContext, entt::entity parentEntity )
{
    string      name        = mNode->mName.C_Str();
    vaMatrix4x4 transform   = AsVA( mNode->mTransformation );

    entt::entity newEntity = importerContext.Scene->CreateEntity( name, transform, parentEntity );

    for( int i = 0; i < (int)mNode->mNumMeshes; i++ )
    {
        aiMesh * meshPtr = loadedScene->mMeshes[ mNode->mMeshes[i] ];
        shared_ptr<vaAssetRenderMesh> meshAsset = tempStorage.FindMesh( meshPtr );
        if( meshAsset == nullptr || meshAsset->GetRenderMesh() == nullptr )
        {
            VA_LOG_WARNING( "Node %s can't find mesh %d that was supposed to be loaded", name.c_str(), i );
        }
        else
        {
            vaGUID renderMeshID = meshAsset->GetRenderMesh()->UIDObject_GetUID();
            if( mNode->mNumMeshes == 1 )
                importerContext.Scene->Registry().emplace<Scene::RenderMesh>( newEntity, renderMeshID );
            else
                importerContext.Scene->CreateEntity( vaStringTools::Format("mesh_%04d", i), vaMatrix4x4::Identity, newEntity, renderMeshID );
        }
    }

    for( int i = 0; i < (int)mNode->mNumChildren; i++ )
    {
        bool ret = ProcessNodesRecursive( loadedScene, mNode->mChildren[i], tempStorage, importerContext, newEntity );
        if( !ret )
        {
            VA_LOG_ERROR( "Node %s child %d fatal processing error", name.c_str(), i );
            return false;
        }
    }
    return true;
}

static bool ProcessSceneNodes( const aiScene* loadedScene, LoadingTempStorage & tempStorage, vaAssetImporter::ImporterContext & importerContext )
{
    vaScene & outScene = *importerContext.Scene;

    entt::entity lightsParent = outScene.CreateEntity( "Lights" );

    for( int i = 0; i < (int)loadedScene->mNumLights; i++ )
    {
        aiLight & light = *loadedScene->mLights[i];

        vaMatrix3x3 rot = vaMatrix3x3::Identity;
        if( light.mType != aiLightSource_AMBIENT )
        {
            rot.Row( 0 ) = AsVA( light.mDirection );
            rot.Row( 1 ) = vaVector3::Cross( AsVA( light.mUp ), rot.Row( 0 ) );
            rot.Row( 2 ) = AsVA( light.mUp );
            if( rot.Row( 1 ).Length( ) < 0.99f )
                vaVector3::ComputeOrthonormalBasis( rot.Row( 0 ), rot.Row( 1 ), rot.Row( 2 ) );
        }
        vaMatrix4x4 trans = vaMatrix4x4::FromRotationTranslation( rot, AsVA(light.mPosition) );

        entt::entity lightEntity = outScene.CreateEntity( /*vaStringTools::Format("light_%04d", i)*/light.mName.C_Str(), trans, lightsParent );

        Scene::LightBase lightBase = Scene::LightBase::Make();

        lightBase.Color         = AsVA( light.mColorDiffuse );
        lightBase.Intensity     = 1.0f;
        vaColor::NormalizeLuminance( lightBase.Color, lightBase.Intensity );
        lightBase.FadeFactor    = 1.0f;

        switch( light.mType )
        {
        case( aiLightSource_AMBIENT ):
        {
            auto & newLight = outScene.Registry( ).emplace<Scene::LightAmbient>( lightEntity, lightBase );
            newLight;
        } break;
        case( aiLightSource_DIRECTIONAL ):
        {
            assert( false ); // create a point light "far away" that is representative of the directional light
            // how far away can it be without messing up precision?
            // auto & newLight = outScene.Registry( ).emplace<Scene::LightDirectional>( lightEntity, lightBase );
            // // newLight.AngularRadius  = light.AngularRadius;
            // // newLight.HaloSize       = light.HaloSize;
            // // newLight.HaloFalloff    = light.HaloFalloff;
            // newLight.CastShadows    = true;
        } break;
        case( aiLightSource_POINT ):
        {
            auto & newLight = outScene.Registry( ).emplace<Scene::LightPoint>( lightEntity, lightBase );
            newLight.Radius         = std::max( 0.0001f, light.mSize.Length() );
            newLight.Range          = std::sqrtf(10000.0f * lightBase.Intensity); // not sure what to do about this - we don't use light.mAttenuationConstant/mAttenuationLinear/mAttenuationQuadratic
            newLight.SpotInnerAngle = 0.0f;
            newLight.SpotOuterAngle = 0.0f;
            newLight.CastShadows    = false;
        } break;
        case( aiLightSource_SPOT ):
        {
            auto & newLight = outScene.Registry( ).emplace<Scene::LightPoint>( lightEntity, lightBase );
            newLight.Radius         = std::max( 0.0001f, light.mSize.Length() );
            newLight.Range          = std::sqrtf(10000.0f * lightBase.Intensity); // not sure what to do about this - we don't use light.mAttenuationConstant/mAttenuationLinear/mAttenuationQuadratic
            newLight.SpotInnerAngle = light.mAngleInnerCone; assert( light.mAngleInnerCone <= VA_PIf );
            newLight.SpotOuterAngle = light.mAngleOuterCone; assert( light.mAngleOuterCone <= VA_PIf );
            newLight.CastShadows    = false;
        } break;

        case( aiLightSource_UNDEFINED ): 
        case( aiLightSource_AREA ): 
        default:
            VA_WARN( "Unrecognized or unsupported light type for light '%s'", light.mName.C_Str() ); 
            outScene.DestroyEntity( lightEntity, false );
            break;
        }
    }

    vaMatrix4x4 transform = importerContext.BaseTransform * AsVA( loadedScene->mRootNode->mTransformation );
    
    entt::entity sceneRoot = outScene.CreateEntity( "Scene", transform );

    return ProcessNodesRecursive( loadedScene, loadedScene->mRootNode, tempStorage, importerContext, sceneRoot );
}

static bool ProcessScene( /*const string & path, */ const aiScene* loadedScene, LoadingTempStorage & tempStorage, vaAssetImporter::ImporterContext & importerContext )
{
    if( importerContext.IsAborted() )
        return false;

    if( !ProcessTextures( loadedScene, tempStorage, importerContext ) )
        return false;

    if( importerContext.IsAborted( ) )
        return false;

    if( !ProcessMaterials( loadedScene, tempStorage, importerContext ) )
        return false;

    if( importerContext.IsAborted( ) )
        return false;

    if( !ProcessMeshes( loadedScene, tempStorage, importerContext ) )
        return false;

    if( importerContext.IsAborted( ) )
        return false;

    // this must happen in the main thread
    if( !importerContext.AsyncInvokeAtBeginFrame( [ & ]( vaRenderDevice& , vaAssetImporter::ImporterContext& importerContext )
    {
        return ProcessSceneNodes( loadedScene, tempStorage, importerContext );
    } ) )
        return false;

    return true;
}

bool LoadFileContents_Assimp( const string & path, vaAssetImporter::ImporterContext & importerContext )
{
    /*
    for( int i = 0; i <= 100; i++ )
    {
        importerContext->SetProgress( i / 100.0f );
        if( context.ForceStop || importerContext->IsAborted( ) )
        {
            importerContext->AddLog( "Aborted!\n" );
            return false;
        }

        importerContext->AsyncInvokeAtBeginFrame( [ ]( vaAssetImporter::ImporterContext& importerContext )
        {
            importerContext.AddLog( vaStringTools::Format( "TickFrom BeginFrame(%.3f)!\n", 1.0f ) );
            return true;
        } );

        vaThreading::Sleep( 100 );
        if( i % 10 == 0 )
            importerContext->AddLog( "Argh!\n" );
    }
*/

    // setup our loggers
    myLoggersRAII loggersRAII( importerContext );

    // construct global importer and exporter instances
	Assimp::Importer importer;
    importer.SetProgressHandler( loggersRAII.GetProgressHandler() );
	
	// aiString s;
    // imp.GetExtensionList(s);
    // vaLog::GetInstance().Add( "Assimp extension list: %s", s.data );

    const aiScene* loadedScene = nullptr;

    LoadingTempStorage tempStorage;
    vaFileTools::SplitPath( vaStringTools::ToLower( path ), &tempStorage.ImportDirectory, &tempStorage.ImportFileName, &tempStorage.ImportExt );

    {
        vaTimerLogScope timerLog( vaStringTools::Format( "Assimp parsing '%s'", path.c_str( ) ) );

        string apath = path;

        unsigned int flags = 0;
        //flags |= aiProcess_CalcTangentSpace;          // switching to shader-based (co)tangent compute
        flags |= aiProcess_JoinIdenticalVertices;
        flags |= aiProcess_ImproveCacheLocality;
        flags |= aiProcess_LimitBoneWeights;
        flags |= aiProcess_RemoveRedundantMaterials;
        flags |= aiProcess_Triangulate;
        flags |= aiProcess_GenUVCoords;
        flags |= aiProcess_SortByPType;                 // to more easily exclude (or separately handle) lines & points
        // flags |= aiProcess_FindDegenerates;          // causes issues
        // flags |= aiProcess_FixInfacingNormals;       // causes issues

        flags |= aiProcess_FindInvalidData;
        flags |= aiProcess_ValidateDataStructure;
        // flags |= aiProcess_TransformUVCoords;        // not yet needed

        if( importerContext.Settings.AISplitLargeMeshes )
            flags |= aiProcess_SplitLargeMeshes;
        if( importerContext.Settings.AIFindInstances )
            flags |= aiProcess_FindInstances;
         if( importerContext.Settings.AIOptimizeMeshes )
             flags |= aiProcess_OptimizeMeshes;
        if( importerContext.Settings.AIOptimizeGraph )
            flags |= aiProcess_OptimizeGraph;
        if( importerContext.Settings.AIFLipUVs )
            flags |= aiProcess_FlipUVs;

        flags |= aiProcess_ConvertToLeftHanded;
        
        if( importerContext.Settings.AIForceGenerateNormals )
            importerContext.Settings.AIGenerateNormalsIfNeeded = true;

        // flags |= aiProcess_RemoveComponent;
        int removeComponentFlags = 0; //aiComponent_CAMERAS | aiComponent_LIGHTS; // aiComponent_COLORS

        if( importerContext.Settings.AIGenerateNormalsIfNeeded )
        {
            if( importerContext.Settings.AIGenerateSmoothNormalsIfGenerating )
            {
                flags |= aiProcess_GenSmoothNormals; 
                importer.SetPropertyFloat( AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, importerContext.Settings.AIGenerateSmoothNormalsSmoothingAngle );
            }
            else
            {
                flags |= aiProcess_GenNormals; 
            }
            if( importerContext.Settings.AIForceGenerateNormals )
            {
                flags |= aiProcess_RemoveComponent;
                removeComponentFlags |= aiComponent_NORMALS;
            }
       }

        importer.SetPropertyInteger( AI_CONFIG_PP_RVC_FLAGS, removeComponentFlags );

        importer.SetPropertyBool( "GLOB_MEASURE_TIME", true );

        //importer.SetProgressHandler()

        loadedScene = importer.ReadFile( apath.c_str(), flags );
        importer.SetProgressHandler( nullptr );
        if( loadedScene == nullptr )
        {
            VA_LOG_ERROR( importer.GetErrorString() );
            return false; 
        }
    }

    if( !importerContext.IsAborted() )
    {
        vaTimerLogScope timerLog( vaStringTools::Format( "Importing Assimp scene...", path.c_str( ) ) );

        bool ret = ProcessScene( loadedScene, tempStorage, importerContext );
        
        return ret;
    }

    return false;
}

#else

bool LoadFileContents_Assimp( const string & path, vaAssetImporter::ImporterContext & importerContext )
{
    path; importerContext; 
    assert( false );
    return false;
}

#endif