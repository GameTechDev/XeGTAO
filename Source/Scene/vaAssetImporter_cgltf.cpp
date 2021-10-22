///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com), Steve Mccalla <stephen.mccalla@intel.com>
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define CGLTF_IMPLEMENTATION
#pragma warning ( push )
#pragma warning ( disable: 4996 )
#include "IntegratedExternals\cgltf\cgltf.h"
#pragma warning( pop)

#include "vaAssetImporter.h"
#include "Rendering/vaStandardShapes.h"

// #include "IntegratedExternals/vaAssimpIntegration.h"

#include "Core/System/vaFileTools.h"

#include "Rendering/vaRenderMesh.h"
#include "Rendering/vaRenderMaterial.h"

// #include "Rendering/Effects/vaPostProcess.h"


using namespace Vanilla;

namespace
{

    struct LoadingTempStorage
    {
        struct LoadedTexture
        {
            shared_ptr<vaAssetTexture>          Texture;
            string                              OriginalPath;
            vaTextureLoadFlags                  TextureLoadFlags;
            vaTextureContentsType               TextureContentsType;

            LoadedTexture( const shared_ptr<vaAssetTexture>& texture, const string& originalPath, vaTextureLoadFlags textureLoadFlags, vaTextureContentsType textureContentsType )
                : Texture( texture ), OriginalPath( originalPath ), TextureLoadFlags( textureLoadFlags ), TextureContentsType( textureContentsType )
            { }
        };

        struct LoadedMaterial
        {
            const cgltf_material* GLTFMaterial;
            shared_ptr<vaAssetRenderMaterial>   Material;

            LoadedMaterial( const cgltf_material* gltfMaterial, const shared_ptr<vaAssetRenderMaterial>& material )
                : GLTFMaterial( gltfMaterial ), Material( material )
            { }
        };

        struct LoadedMesh
        {
            const cgltf_primitive* GLTFPrimitive;
            shared_ptr<vaAssetRenderMesh>       Mesh;

            LoadedMesh( const cgltf_primitive* gltfPrimitive, const shared_ptr<vaAssetRenderMesh>& mesh )
                : GLTFPrimitive( gltfPrimitive ), Mesh( mesh )
            { }
        };

        string                                      ImportDirectory;
        string                                      ImportFileName;
        string                                      ImportExt;

        std::vector<LoadedTexture>                  LoadedTextures;
        std::vector<LoadedMaterial>                 LoadedMaterials;
        std::vector<LoadedMesh>                     LoadedMeshes;

        shared_ptr<vaAssetRenderMaterial>           FindMaterial( const cgltf_material* gltfMaterial )
        {
            for (const auto& material : LoadedMaterials)
            {
                if (material.GLTFMaterial == gltfMaterial)
                    return material.Material;
            }
            return nullptr;
        }

        shared_ptr<vaAssetRenderMesh>               FindMesh( const cgltf_primitive* gltfPrimitive )
        {
            for (const auto& mesh : LoadedMeshes)
            { 
                if (mesh.GLTFPrimitive == gltfPrimitive)
                    return mesh.Mesh;
            }
            return nullptr;
        }
    };
}

static inline vaVector4     Vec4AsVA( const cgltf_float* val     )       { return vaVector4( val[0], val[1], val[2], val[3] );   }
static inline vaVector3     Vec3AsVA( const cgltf_float* val )          { return vaVector3(val[0], val[1], val[2]); }
//static inline vaVector3     AsVA( const aiColor3D & val )           { return vaVector3( val.r, val.g, val.b ); }
////static inline vaMatrix4x4   VAFromAI( const aiMatrix4x4 & val )    { return vaMatrix4x4( val.a1, val.a2, val.a3, val.a4, val.b1, val.b2, val.b3, val.b4, val.c1, val.c2, val.c3, val.c4, val.d1, val.d2, val.d3, val.d4 ); }
//static inline vaMatrix4x4   AsVA(const cgltf_float* val) { return vaMatrix4x4(val.a1, val.b1, val.c1, val.d1, val.a2, val.b2, val.c2, val.d2, val.a3, val.b3, val.c3, val.d3, val.a4, val.b4, val.c4, val.d4); }

// TODO: pretty sure we're row major by default..
static inline vaMatrix4x4   Mat4x4AsVA(const cgltf_float* val) { return vaMatrix4x4(val[0], val[1], val[2], val[3], val[4], val[5], val[6], val[7], val[8], val[9], val[10], val[11], val[12], val[13], val[14], val[15]); }

void 
RemoveChannels()
{
    // TODO: need some assets to test this 
    VA_LOG("VaAssetImporter_GLTF - RemoveChannels called");
    assert(false);
    //vaResourceFormat outFormat = vaResourceFormat::Unknown;
    //switch (textureOut->GetResourceFormat())
    //{
    //case(vaResourceFormat::R8G8B8A8_UNORM):
    //case(vaResourceFormat::B8G8R8A8_UNORM):
    //    outFormat = vaResourceFormat::R8_UNORM; break;
    //}

    //shared_ptr<vaTexture> singleChannelTextureOut = (outFormat == vaResourceFormat::Unknown) ? (nullptr) : (vaTexture::Create2D(renderDevice, outFormat, textureOut->GetWidth(), textureOut->GetHeight(), 1, 1, 1,
    //    vaResourceBindSupportFlags::ShaderResource | vaResourceBindSupportFlags::RenderTarget, vaResourceAccessFlags::Default, outFormat, vaResourceFormat::Automatic, vaResourceFormat::Automatic, vaResourceFormat::Automatic,
    //    textureOut->GetFlags(), textureOut->GetContentsType()));

    //if (singleChannelTextureOut != nullptr && renderDevice.GetPostProcess().MergeTextures(renderContext, singleChannelTextureOut, textureOut, nullptr, nullptr, "float4( srcA.x, 0, 0, 0 )") == vaDrawResultFlags::None)
    //{
    //    VA_LOG("VaAssetImporter_GLTF - Successfully removed unnecessary color channels for '%s' texture", filePath.c_str());
    //    textureOut = singleChannelTextureOut;
    //}
}

void
GenerateMips(vaRenderDeviceContext& renderContext, shared_ptr<vaTexture>& textureOut, const string& filePath)
{
    if (textureOut->GetMipLevels() == 1)
    {
        auto mipmappedTexture = vaTexture::TryCreateMIPs(renderContext, textureOut);
        if (mipmappedTexture != nullptr)
        {
            VA_LOG("VaAssetImporter_GLTF - Successfully created MIPs for '%s' texture", filePath.c_str());
            textureOut = mipmappedTexture;
        }
        else
            VA_LOG("VaAssetImporter_GLTF - Error while creating MIPs for '%s'", filePath.c_str());
    }
    else
    {
        VA_LOG("VaAssetImporter_GLTF - Texture '%s' already has %d mip levels!", filePath.c_str(), textureOut->GetMipLevels());
    }
}



static shared_ptr<vaAssetTexture> FindOrLoadTexture(const string& _path, LoadingTempStorage& tempStorage, vaAssetImporter::ImporterContext& importerContext,
    vaTextureLoadFlags textureLoadFlags, vaTextureContentsType textureContentsType, bool& createdNew)
{
    createdNew = false;
    string originalPath = vaStringTools::ToLower(_path);
    string filePath = originalPath;
    string outDir, outName, outExt;

    for (int i = 0; i < tempStorage.LoadedTextures.size(); i++)
    {
        if ((originalPath == tempStorage.LoadedTextures[i].OriginalPath) && (textureLoadFlags == tempStorage.LoadedTextures[i].TextureLoadFlags) && (textureContentsType == tempStorage.LoadedTextures[i].TextureContentsType))
        {
            return tempStorage.LoadedTextures[i].Texture;
        }
    }
    vaFileTools::SplitPath(filePath, &outDir, &outName, &outExt);


    if (!vaFileTools::FileExists(filePath))
    {
        filePath = tempStorage.ImportDirectory + outDir + outName + "." + outExt;
        if (!vaFileTools::FileExists(filePath))
        {
            VA_LOG("VaAssetImporter_GLTF - Unable to find texture '%s'", filePath.c_str());
            return nullptr;
        }
    }

    shared_ptr<vaAssetTexture> textureAssetOut;
    if (!importerContext.AsyncInvokeAtBeginFrame([&](vaRenderDevice& renderDevice, vaAssetImporter::ImporterContext& importerContext)
    {
        shared_ptr<vaTexture> textureOut = vaTexture::CreateFromImageFile(renderDevice, filePath, textureLoadFlags, vaResourceBindSupportFlags::ShaderResource, textureContentsType);

        if (textureOut == nullptr)
        {
            VA_LOG("VaAssetImporter_GLTF - Error while loading '%s'", filePath.c_str());
            return false;
        }

        // this is valid because all of this happens after BeginFrame was called on the device but before main application/sample starts rendering anything
        vaRenderDeviceContext& renderContext = *renderDevice.GetMainContext();

        if (textureContentsType == vaTextureContentsType::SingleChannelLinearMask && vaResourceFormatHelpers::GetChannelCount(textureOut->GetResourceFormat()) > 1)
        {
            RemoveChannels();
        }

        if (textureOut != nullptr && importerContext.Settings.TextureGenerateMIPs)
        {
            GenerateMips(renderContext, textureOut, filePath);
        }

        if (textureOut == nullptr)
        {
            VA_LOG("VaAssetImporter_GLTF - Error while loading '%s'", filePath.c_str());
            return false;
        }

        assert(vaThreading::IsMainThread()); // remember to lock asset global mutex and switch these to 'false'
        textureAssetOut = importerContext.AssetPack->Add(textureOut, importerContext.AssetPack->FindSuitableAssetName(importerContext.Settings.AssetNamePrefix + outName, true), true);
        
        tempStorage.LoadedTextures.push_back(LoadingTempStorage::LoadedTexture(textureAssetOut, originalPath, textureLoadFlags, textureContentsType));

        return true;
    }))
        return nullptr;

    createdNew = true;

    VA_LOG_SUCCESS("GLtf texture '%s' loaded ok.", filePath.c_str());

    return textureAssetOut;
}

//static bool TexturesIdentical( aiTextureType texType0, unsigned texIndex0, aiTextureType texType1, unsigned texIndex1, aiMaterial* assimpMaterial )
//{
//    aiString            tex0Path;
//    aiTextureMapping    tex0Mapping = aiTextureMapping_UV;
//    uint32              tex0UVIndex = 0;
//    float               tex0BlendFactor = 0.0f;
//    aiTextureOp         tex0Op = aiTextureOp_Add;
//    aiTextureMapMode    tex0MapModes[2] = { aiTextureMapMode_Wrap, aiTextureMapMode_Wrap };
//    aiTextureFlags      tex0Flags = (aiTextureFlags)0;
//    aiString            tex1Path;
//    aiTextureMapping    tex1Mapping = aiTextureMapping_UV;
//    uint32              tex1UVIndex = 0;
//    float               tex1BlendFactor = 0.0f;
//    aiTextureOp         tex1Op = aiTextureOp_Add;
//    aiTextureMapMode    tex1MapModes[2] = { aiTextureMapMode_Wrap, aiTextureMapMode_Wrap };
//    aiTextureFlags      tex1Flags = (aiTextureFlags)0;
//
//    if( aiReturn_SUCCESS != aiGetMaterialTexture( assimpMaterial, texType0, texIndex0, &tex0Path, &tex0Mapping, &tex0UVIndex, &tex0BlendFactor, &tex0Op, tex0MapModes, (unsigned int*)&tex0Flags ) )
//        return false;
//    if( aiReturn_SUCCESS != aiGetMaterialTexture( assimpMaterial, texType1, texIndex1, &tex1Path, &tex1Mapping, &tex1UVIndex, &tex1BlendFactor, &tex1Op, tex1MapModes, (unsigned int*)&tex1Flags ) )
//        return false;
//
//    return (tex0Path == tex1Path) && (tex0Mapping == tex1Mapping) && (tex0UVIndex==tex1UVIndex) && (tex0BlendFactor==tex1BlendFactor) && (tex0Op==tex1Op) && (tex0MapModes[0]==tex1MapModes[0]) && (tex0MapModes[1]==tex1MapModes[1]) && (tex0Flags == tex1Flags);
//}
//



#define GL_NEAREST                        0x2600
#define GL_LINEAR                         0x2601
#define GL_NEAREST_MIPMAP_NEAREST         0x2700
#define GL_LINEAR_MIPMAP_NEAREST          0x2701
#define GL_NEAREST_MIPMAP_LINEAR          0x2702
#define GL_LINEAR_MIPMAP_LINEAR           0x2703
#define GL_REPEAT                         0x2901
#define GL_MIRRORED_REPEAT                0x8370
#define GL_CLAMP_TO_EDGE                  0x812F

vaLayerMode GLTFAlphaModeToVanilla(cgltf_alpha_mode alpha_mode)
{
    if (alpha_mode == cgltf_alpha_mode_opaque)
    {
        return vaLayerMode::Opaque;
    }
    else if (alpha_mode == cgltf_alpha_mode_mask)
    {
        return vaLayerMode::AlphaTest;
    }
    else if (alpha_mode == cgltf_alpha_mode_blend)
    {
        return vaLayerMode::Transparent;
    }
    else
    {
        VA_LOG_ERROR("GLTF warning: unknown/invalid alpha mode");
        assert(false); // unknown /invalid alpha mode!
    }
    return vaLayerMode::Opaque;
}


static string ImportTextureNode( vaRenderMaterial & vanillaMaterial, const string & inputTextureNodeName, vaTextureContentsType contentsType, LoadingTempStorage & tempStorage, 
                                vaAssetImporter::ImporterContext & importerContext, const cgltf_texture_view* gltfTexView, bool & createdNew )
{
    cgltf_texture* gltfTex = gltfTexView->texture;
    cgltf_sampler* gltfSampler = gltfTex->sampler;

    uint32              texUVIndex = 0;
    cgltf_int           texMapModes[2] = { GL_REPEAT, GL_REPEAT };

    if (gltfSampler != nullptr)
    {
        texMapModes[0] = gltfSampler->wrap_s;
        texMapModes[1] = gltfSampler->wrap_t;
    }
    //aiTextureFlags      texFlags = (aiTextureFlags)0;

    //if( aiReturn_SUCCESS != aiGetMaterialTexture( assimpMaterial, texType, &_pathTmp, &texMapping, &texUVIndex, &texBlendFactor, &texOp, texMapModes, (unsigned int*)&texFlags ) )
    //    return "";

    string texPath = gltfTexView->texture->image->uri;

    vaTextureLoadFlags  textureLoadFlags = vaTextureLoadFlags::PresumeDataIsLinear;
    if( contentsType == vaTextureContentsType::GenericColor )
        textureLoadFlags = vaTextureLoadFlags::PresumeDataIsSRGB;


    shared_ptr<vaAssetTexture> newTextureAsset = FindOrLoadTexture(texPath.c_str(), tempStorage, importerContext, textureLoadFlags, contentsType, createdNew);

    if( ( newTextureAsset == nullptr ) || ( newTextureAsset->GetTexture( ) == nullptr ) )
    {
        VA_LOG_WARNING( "GLTF importer warning: Texture '%s' could not be imported, skipping", texPath.c_str() );
        return "";
    }

    if( texMapModes[0] != texMapModes[1] )
    {
        VA_LOG_WARNING( "GLTF warning: Texture '%s' has mismatched U & V texMapModes (%d, %d) - using first one for both", texPath.c_str(), texMapModes[0], texMapModes[1] );
        return "";
    }

    vaStandardSamplerType samplerType = vaStandardSamplerType::AnisotropicWrap;
  
    if( texMapModes[0] == GL_REPEAT)
        samplerType = vaStandardSamplerType::AnisotropicWrap;
    else if( texMapModes[0] == GL_CLAMP_TO_EDGE)
        samplerType = vaStandardSamplerType::AnisotropicClamp;
    else if( texMapModes[0] == GL_MIRRORED_REPEAT)
    {
        VA_LOG_WARNING( "GLTF warning: Texture '%s' is using 'mirror' UV sampling mode but it is not supported by the materials", texPath.c_str() );
        return "";
    }
    else 
    {
        VA_LOG_WARNING( "GLTF warning: Texture '%s' is using unsupported UV sampling mode, think about supporting it", texPath.c_str() );
        return "";
    }

    
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


static string ImportTextureNode( vaRenderMaterial& vanillaMaterial, const string& inputTextureNodeName, vaTextureContentsType contentsType, 
                                LoadingTempStorage& tempStorage, vaAssetImporter::ImporterContext& importerContext, const cgltf_texture_view* gltfTexView)
{
    bool createdNew = false;
    if (gltfTexView->texture != nullptr)
    {
        return ImportTextureNode(vanillaMaterial, inputTextureNodeName, contentsType, tempStorage, importerContext, gltfTexView, createdNew);
    }
    else
    {
        return "";
    }
}

static void ProcessNormalTexture(const cgltf_material* gltfMaterial, shared_ptr<vaRenderMaterial> newMaterial, LoadingTempStorage& tempStorage, vaAssetImporter::ImporterContext& importerContext)
{
    string normalmapTextureName = ImportTextureNode(*newMaterial, "NormalmapTex", vaTextureContentsType::NormalsXY_UNORM, tempStorage, importerContext, &gltfMaterial->normal_texture);

    if (normalmapTextureName != "")
    {
        float textureScale = gltfMaterial->normal_texture.scale;
        if (textureScale != 1.0f)
        {
            // assert( textureScale == 1.0f ); // different from 1.0f not implemented yet !!!
            VA_WARN("GLTF_TEXTURE_SCALE set to %.3f but only 1.0 (no scaling) currently supported!", textureScale);
        }
        newMaterial->ConnectInputSlotWithNode("Normal", normalmapTextureName);
    }
}


static bool ProcessPBRMetallicRoughnessMaterial(const cgltf_material* gltfMaterial, shared_ptr<vaRenderMaterial> newMaterial, LoadingTempStorage& tempStorage, vaAssetImporter::ImporterContext& importerContext)
{
    if (gltfMaterial->unlit)
    {
        newMaterial->SetupFromPreset("FilamentUnlit");
    }
    else
    {
        newMaterial->SetupFromPreset("FilamentStandard");
    }

    float metallic = gltfMaterial->pbr_metallic_roughness.metallic_factor;
    float roughness = gltfMaterial->pbr_metallic_roughness.roughness_factor;
    float occlusion = 1.0;
    auto baseColor = gltfMaterial->pbr_metallic_roughness.base_color_factor;

    newMaterial->SetInputSlotDefaultValue("BaseColor", vaVector4::SRGBToLinear(vaVector4(baseColor)));

    //cgltf_texture_view base_color_texture;
    string baseColorTextureName = ImportTextureNode(*newMaterial, "BaseColorTex", vaTextureContentsType::GenericColor, tempStorage, importerContext, &gltfMaterial->pbr_metallic_roughness.base_color_texture);
    if (baseColorTextureName != "")
        newMaterial->ConnectInputSlotWithNode("BaseColor", baseColorTextureName);

    // see https://github.com/KhronosGroup/glTF/tree/master/specification/2.0 for the specs

    newMaterial->SetInputSlotDefaultValue("Roughness", roughness);
    newMaterial->SetInputSlotDefaultValue("Metallic", metallic);
    newMaterial->SetInputSlotDefaultValue("AmbientOcclusion", occlusion);

    // From the specs:
    // "The metallic-roughness texture. The metalness values are sampled from the B channel. The roughness values are sampled from the G channel. 
    // These values are linear. If other channels are present (R or A), they are ignored for metallic-roughness calculations."

    // Additionally, (ambient) occlusion could be the same in which case let's just load it as one
    // (there's more info at https://github.com/KhronosGroup/glTF/issues/857 and related threads)

    // if occlusion texture is part of same metallic roughness texture
    cgltf_texture* gltfOccTex = gltfMaterial->occlusion_texture.texture;
    if (gltfOccTex != nullptr)
    {
        cgltf_texture_view metallic_roughness_texture = gltfMaterial->pbr_metallic_roughness.metallic_roughness_texture;

        string omrTextureName = ImportTextureNode(*newMaterial, "OcclMetalRoughTex", vaTextureContentsType::GenericLinear, tempStorage, importerContext, &gltfMaterial->pbr_metallic_roughness.metallic_roughness_texture);
        if (omrTextureName != "")
        {
            assert(roughness == 1.0f);
            assert(metallic == 1.0f);
            assert(occlusion == 1.0f);

            //float occlusionTextureStrength = 1.0f;
            //assert(occlusionTextureStrength == 1.0f); // different from 1.0f not implemented yet !!! if changing make sure to change the alone one below !!!

            newMaterial->ConnectInputSlotWithNode("Roughness", omrTextureName, "y");
            newMaterial->ConnectInputSlotWithNode("Metallic", omrTextureName, "z");
            newMaterial->ConnectInputSlotWithNode("AmbientOcclusion", omrTextureName, "x");   // not 100% sure how to know whether to use this or not
        }
        newMaterial->SetInputSlotDefaultValue("AmbientOcclusion", occlusion);
    }
    else // occlusion texture is separate so we have to read two separate textures
    {
        string mrTextureName = ImportTextureNode(*newMaterial, "MetallicRoughnessTex", vaTextureContentsType::GenericLinear, tempStorage, importerContext, &gltfMaterial->pbr_metallic_roughness.metallic_roughness_texture);

        if (mrTextureName != "")
        {
            newMaterial->ConnectInputSlotWithNode("Roughness", mrTextureName, "y");
            newMaterial->ConnectInputSlotWithNode("Metallic", mrTextureName, "z");
        }
        string occlusionTextureName = ImportTextureNode(*newMaterial, "OcclusionTex", vaTextureContentsType::GenericLinear, tempStorage, importerContext, &gltfMaterial->occlusion_texture);

        if (occlusionTextureName != "")
        {
            float occlusionTextureStrength = gltfMaterial->occlusion_texture.scale; occlusionTextureStrength;
            assert(occlusionTextureStrength == 1.0f); // different from 1.0f not implemented yet !!! if changing make sure to change the combined one above!!!

            newMaterial->ConnectInputSlotWithNode("AmbientOcclusion", occlusionTextureName, "x");
        }
    }
    ProcessNormalTexture(gltfMaterial, newMaterial, tempStorage, importerContext);

    {
        // The RGB components of the emissive color of the material. These values are linear. If an emissiveTexture is specified, this value is multiplied with the texel values.        
        newMaterial->SetInputSlotDefaultValue("emissiveColor", vaVector3::SRGBToLinear(Vec3AsVA(gltfMaterial->emissive_factor)));
        string emissiveTex = ImportTextureNode(*newMaterial, "EmissiveTex", vaTextureContentsType::GenericColor, tempStorage, importerContext, &gltfMaterial->emissive_texture);

        if (emissiveTex != "")
        {
            newMaterial->ConnectInputSlotWithNode("EmissiveColor", emissiveTex);
            assert(gltfMaterial->emissive_factor[0] == 1.0f && gltfMaterial->emissive_factor[1] == 1.0f && gltfMaterial->emissive_factor[2] == 1.0f);  // different from 1.0f not implemented yet !!!
        }
    }

    // TODO: add metallic texture
    return true;
}

static bool ProcessMaterials(const cgltf_data* loadedScene, LoadingTempStorage& tempStorage, vaAssetImporter::ImporterContext& importerContext)
{

    for (unsigned int mi = 0; mi < loadedScene->materials_count; mi++)
    {
        cgltf_material* gltfMaterial = &loadedScene->materials[mi];
        string materialName = gltfMaterial->name == nullptr ? "material" : gltfMaterial->name;
        VA_LOG( "GLTF processing material '%s'", materialName.c_str( ) );

        shared_ptr<vaRenderMaterial> newMaterial;       

        if (!importerContext.AsyncInvokeAtBeginFrame([&](vaRenderDevice& renderDevice, vaAssetImporter::ImporterContext& importerContext)
        {            
            newMaterial = renderDevice.GetMaterialManager().CreateRenderMaterial();        

            vaRenderMaterial::MaterialSettings matSettings = newMaterial->GetMaterialSettings();

            //// some defaults
            //matSettings.ReceiveShadows = true;
            matSettings.CastShadows = true;
            //matSettings.AlphaTestThreshold = 0.3f; // use 0.3 instead of 0.5; 0.5 can sometimes make things almost invisible if alpha was stored in .rgb (instead of .a) and there was a linear<->sRGB mixup; can always be tweaked later
            matSettings.AlphaTestThreshold = gltfMaterial->alpha_cutoff;
            // Specifies whether meshes using this material must be rendered without backface culling. 0 for false, !0 for true.
            matSettings.FaceCull = (gltfMaterial->double_sided) ? (vaFaceCull::None) : (vaFaceCull::Back);
            // TODO: doesn't appear to be a wireframe material setting in gltf ?

            if (gltfMaterial->has_pbr_metallic_roughness)
            {
                ProcessPBRMetallicRoughnessMaterial(gltfMaterial, newMaterial, tempStorage, importerContext);
                matSettings.LayerMode = GLTFAlphaModeToVanilla(gltfMaterial->alpha_mode);
            }          
            else if (gltfMaterial->has_pbr_specular_glossiness)
            {
                VA_WARN("gltf2 specular glossiness model not supported yet");
                //ProcessPBRSpecularGlossinessMaterial()
                newMaterial->SetupFromPreset("FilamentSpecGloss");
            }
            else
            {
                // there's no such thing as default material in gltf, or is there?
                assert(false);
            }

                       
            newMaterial->SetMaterialSettings(matSettings);

            assert(vaThreading::IsMainThread()); // remember to lock asset global mutex and switch these to 'false'
            materialName = importerContext.AssetPack->FindSuitableAssetName(importerContext.Settings.AssetNamePrefix + materialName, true);

            assert(vaThreading::IsMainThread()); // remember to lock asset global mutex and switch these to 'false'
            auto materialAsset = importerContext.AssetPack->Add(newMaterial, materialName, true);

            VA_LOG_SUCCESS("    material '%s' added", materialName.c_str());

            tempStorage.LoadedMaterials.push_back(LoadingTempStorage::LoadedMaterial(gltfMaterial, materialAsset));

            return true;
        }))
        
        return false;

    }

    return true;
}


// GetBuffer - bufferData points to all data from bin, offset to get to individual pieces
// need to do offset arithmetic in bytes, then we'll cast to whatever type we're reading as

template <typename T>
T* GetBufferPtrAs(const cgltf_accessor* accessor)
{
    assert(accessor != nullptr);
    auto bufferData = (uint8*)accessor->buffer_view->buffer->data;
    bufferData += accessor->offset + accessor->buffer_view->offset;
    return (T*)bufferData;
}


// here we don't have an index buffer, so we create one and index the vertices three at a time
static void CreateIndexBuffer(std::vector<uint32>& indices, cgltf_primitive* prim)
{
    // THIS CODE IS UNTESTED, so just assert for now and find a good test case to test code with later
    assert(false); 
    // we don't know at this point how many indices we need, so we need to find out the number of vertices
    // some of this is redundant
    for (cgltf_size aindex = 0; aindex < prim->attributes_count; aindex++)
    {
        const cgltf_attribute& attribute = prim->attributes[aindex];
        const cgltf_attribute_type atype = attribute.type;
        const cgltf_accessor* accessor = attribute.data;

        // resize the index buffer to be the same as the number of vertices
        if (atype == cgltf_attribute_type_position)
        {
            indices.resize(accessor->count);
            break;
        }
    }
    // then fill in the index buffer
    int i = 0;
    for (auto index : indices)
    {
        index = i++;
    }
}

static void GetIndexBuffer(std::vector<uint32>& indices, cgltf_primitive* prim)
{
    const cgltf_accessor* accessor = prim->indices;

    // In glTF, index buffers are optional per primitive. 
    // Let's force primitives to have index buffers for the time being..This will currently crash on primitives with no indices, e.g. fox.        
    if (accessor == nullptr)
    {
        CreateIndexBuffer(indices, prim);
    }
    else
    {
        // indices can be u32, u16, or u8
        assert(accessor->component_type == cgltf_component_type_r_32u || accessor->component_type == cgltf_component_type_r_16u || accessor->component_type == cgltf_component_type_r_8u);
        indices.resize(accessor->count);

        // convert cgltf index buffer to vector<uint32>
        if (accessor->component_type == cgltf_component_type_r_16u)
        {
            auto pData = GetBufferPtrAs<uint16>(accessor);
            for (unsigned int i = 0; i < accessor->count; i++)
            {
                indices[i] = (uint32)(pData[i]);
            }
        }
        else if (accessor->component_type == cgltf_component_type_r_32u)
        {
            auto pData = GetBufferPtrAs<uint32>(accessor);
            for (unsigned int i = 0; i < accessor->count; i++)
            {
                indices[i] = (uint32)(pData[i]);
            }
        }
        else if (accessor->component_type == cgltf_component_type_r_8u)
        {
            auto pData = GetBufferPtrAs<uint8>(accessor);
            for (unsigned int i = 0; i < accessor->count; i++)
            {
                indices[i] = (uint32)(pData[i]);
            }
        }
    }
}



static void GetVertexPositionsBuffer(const cgltf_accessor* accessor, std::vector<vaVector3>& vertices)
{
    vertices.resize(accessor->count);
    //texcoords0.resize(vertices.size());
    //texcoords1.resize(vertices.size());
    auto pData = GetBufferPtrAs<vaVector3>(accessor);

    for (unsigned int i = 0; i < accessor->count; i++)
    {
        vaVector3 tmpVec(pData[i]);
        vertices[i] = tmpVec;
    }
}

static void GetNormalsBuffer(const cgltf_accessor* accessor, std::vector<vaVector3>&   normals)
{
    normals.resize(accessor->count);
    auto pData = GetBufferPtrAs<vaVector3>(accessor);

    for (unsigned int i = 0; i < accessor->count; i++)
    {
        vaVector3 tmpVec(pData[i]);
        normals[i] = tmpVec;
    }
}

static void GetVertexColorsBuffer(const cgltf_accessor* accessor, std::vector<uint32>&  colors)
{
    colors.resize(accessor->count);
    auto pData = GetBufferPtrAs<uint32>(accessor);
    for (unsigned int i = 0; i < accessor->count; i++)
    {
        colors[i] = pData[i];
    }
}

static void 
GetTexCoords(const cgltf_accessor* accessor, std::vector<vaVector2>&   texcoords)
{
    texcoords.resize(accessor->count);
    auto pData = GetBufferPtrAs<vaVector2>(accessor);
    for (unsigned int i = 0; i < accessor->count; i++)
    {
        texcoords[i] = pData[i];
    }
}

static bool ProcessMeshes(const cgltf_data* loadedScene, LoadingTempStorage& tempStorage, vaAssetImporter::ImporterContext& importerContext)
{
    if (loadedScene->meshes_count <= 0)
    {
        VA_LOG_ERROR("Error: no meshes in scene file ", " ");
        return false;
    }

    for (unsigned int mi = 0; mi < loadedScene->meshes_count; mi++)
    {
        cgltf_mesh* mesh = &loadedScene->meshes[mi];
        string newMeshName = mesh->name == nullptr ? "defaultMeshname" : mesh->name;

        // treat each prim as a separate vanilla mesh
        cgltf_size nprims = mesh->primitives_count;

        for (unsigned int pi = 0; pi < nprims; pi++)
        {
            cgltf_primitive* prim = &(mesh->primitives[pi]);
            assert(prim->type == cgltf_primitive_type_triangles);


            // In glTF, index buffers are optional per primitive. 
            // Let's force primitives to have index buffers for the time being..This will currently crash on primitives with no indices, e.g. fox.        
            std::vector<uint32> indices;
            GetIndexBuffer(indices, prim);            

            std::vector<vaVector3>   vertices;
            std::vector<uint32>      colors;
            std::vector<vaVector3>   normals;
            std::vector<vaVector2>   texcoords0;
            std::vector<vaVector2>   texcoords1;

            assert(prim->type == cgltf_primitive_type_triangles);

            // find the number of vertices, and resize our vectors
            for (cgltf_size aindex = 0; aindex < prim->attributes_count; aindex++)
            {
                const cgltf_attribute& attribute = prim->attributes[aindex];
                const int index = attribute.index;
                const cgltf_attribute_type atype = attribute.type;
                const cgltf_accessor* accessor = attribute.data;

                // we'll only handle the bare minimum to get started, then gradually add stuff            
                if (atype == cgltf_attribute_type_position)
                {
                    vertices.resize(accessor->count);
                    texcoords0.resize(vertices.size());
                    texcoords1.resize(vertices.size());
                    normals.resize(vertices.size());
                    GetVertexPositionsBuffer(accessor, vertices);
                }

                if (atype == cgltf_attribute_type_normal)
                {
                    GetNormalsBuffer(accessor, normals);
                }

                if (atype == cgltf_attribute_type_color)
                {
                    GetVertexColorsBuffer(accessor, colors);
                }

                // cgltf_attribute_type_texcoord - we have attribute.index, but this doesn't necessarily correspond to texcoord0 or texcoord1 
                // Looks like we have to check attribute.name == "TEXCOORD_0" or "TEXCOORD_1"
                if (atype == cgltf_attribute_type_texcoord)
                {
                    if (strcmp(attribute.name, "TEXCOORD_0") == 0)
                    {
                        GetTexCoords(accessor, texcoords0);
                    }
                    else if (strcmp(attribute.name, "TEXCOORD_1") == 0)
                    {
                        GetTexCoords(accessor, texcoords1);
                    }
                    else
                    {
                        VA_LOG_WARNING("AssetImporterGLTF tex coordinates found, and attribute name is %s", attribute.name);
                    }
                }

            }
            // TODO: ensure that vertices, colors, normals, texcoords0 and texcoords1 all have the same size
            auto materialAsset = tempStorage.FindMaterial(prim->material);
            if (materialAsset == nullptr)
            {
                VA_LOG_ERROR("AssetImporterGLTF: mesh '%s' can't find material, skipping.", newMeshName.c_str());
                continue;
            }
            
            auto material = materialAsset->GetRenderMaterial();

            shared_ptr<vaAssetRenderMesh> newAsset;
            if (!importerContext.AsyncInvokeAtBeginFrame([&](vaRenderDevice& renderDevice, vaAssetImporter::ImporterContext& importerContext)
            {
                shared_ptr<vaRenderMesh> newMesh = vaRenderMesh::Create(renderDevice, vaMatrix4x4::Identity, vertices, normals, texcoords0, texcoords1, indices, vaWindingOrder::Clockwise);
                newMesh->SetMaterial(material);

                newMeshName = importerContext.AssetPack->FindSuitableAssetName(newMeshName, true);

                assert(vaThreading::IsMainThread()); // remember to lock asset global mutex and switch these to 'false'
                newAsset = importerContext.AssetPack->Add(newMesh, newMeshName, true);

                VA_LOG_SUCCESS("    mesh/primitive '%s' added", newMeshName.c_str());
                return true;
            }))
                return false;

            // currently ignoring the following attributes:
            //cgltf_attribute_type_tangent
            //cgltf_attribute_type_joints,
            //cgltf_attribute_type_weights,  
            tempStorage.LoadedMeshes.push_back(LoadingTempStorage::LoadedMesh(prim, newAsset));
        }
    }

    return true;
}



// needs to be way more robust - we might just have a rotation, or just a translation, or just scaling, or some combination of all three.
static vaMatrix4x4 GetTransformFromNode(const cgltf_node* node)
{
    // if node has a matrix, convert to va matrix
    if (node->has_matrix)
    {
        return Mat4x4AsVA(node->matrix);
    }
    // otherwise see which components are present and build up transform matrix
    vaQuaternion rotation = vaQuaternion::Identity;
    if (node->has_rotation)
    {
        rotation = vaQuaternion(Vec4AsVA(node->rotation));
    }
    vaVector3 scale = vaVector3(1.0f, 1.0f, 1.0f);
    if (node->has_scale)
    {
        scale = Vec3AsVA(node->scale);
    }
    vaVector3 translation = vaVector3(0.0f, 0.0f, 0.0f);
    if (node->has_translation)
    {
        translation = Vec3AsVA(node->translation);
    }
    vaMatrix4x4 transform = vaMatrix4x4::FromScaleRotationTranslation(scale, rotation, translation);
    return transform;

}

static bool ProcessNodesRecursive(const cgltf_data* loadedScene, cgltf_node* node, LoadingTempStorage& tempStorage, vaAssetImporter::ImporterContext& importerContext, entt::entity parentEntity)
{
    string      name = node->name == nullptr ? "node" : node->name; 
    vaMatrix4x4 transform = vaMatrix4x4::Identity;
    transform = GetTransformFromNode(node);

    entt::entity newEntity = importerContext.Scene->CreateEntity(name, transform, parentEntity);

    // gltf has a single mesh per node or no mesh at all, but might have multiple primitives per mesh
    cgltf_mesh* meshPtr = node->mesh;
    if (meshPtr != nullptr)
    {
        for (unsigned int pi = 0; pi < meshPtr->primitives_count; pi++)
        {
            cgltf_primitive* primPtr = &(meshPtr->primitives[pi]);
            if (primPtr != nullptr)
            {
                shared_ptr<vaAssetRenderMesh> meshAsset = tempStorage.FindMesh(primPtr);
                if (meshAsset == nullptr || meshAsset->GetRenderMesh() == nullptr)
                {
                    VA_LOG_WARNING("Node %s can't find mesh/primitive that was supposed to be loaded", name.c_str());
                }
                else
                {
                    vaGUID renderMeshID = meshAsset->GetRenderMesh()->UIDObject_GetUID();
                    // if there's a single primitive
                    if (meshPtr->primitives_count == 1)
                        importerContext.Scene->Registry().emplace<Scene::RenderMesh>(newEntity, renderMeshID);
                    else
                        importerContext.Scene->CreateEntity(vaStringTools::Format("mesh_%04d", pi), vaMatrix4x4::Identity, newEntity, renderMeshID);
                }
            }
        }
    }

    // TODO: if we didn't get a mesh, then the previous node was just for the transform, need to propagate it
    for( int i = 0; i < (int)node->children_count; i++ )
    {
        bool ret = ProcessNodesRecursive( loadedScene, node->children[i], tempStorage, importerContext, newEntity );
        if( !ret )
        {
            VA_LOG_ERROR( "Node %s child %d fatal processing error", name.c_str(), i );
            return false;
        }
    }
    return true;
}

#if 0
//#pragma warning(push)
//#pragma warning(disable: 4100)
//static bool ProcessSceneLights(const cgltf_data* loadedScene, LoadingTempStorage& tempStorage, vaAssetImporter::ImporterContext& importerContext)
//{
//#pragma warning(pop)
    //entt::entity lightsParent = outScene.CreateEntity( "Lights" );

    //vaScene& outScene = *importerContext.Scene;

    //cgltf_light* lights = loadedScene->lights;
    //cgltf_size lights_count = loadedScene->lights_count;

    //for (int i = 0; i < (int)lights_count; i++)
    //{
    //    cgltf_light& light = loadedScene->lights[i];

    //    // gltf lights are directional, point, and spot
    //    vaMatrix3x3 rot = vaMatrix3x3::Identity;
    //    //if (light.type != aiLightSource_AMBIENT)
    //    {
    //        light.
    //        rot.Row(0) = AsVA(light.mDirection);
    //        rot.Row(1) = vaVector3::Cross(AsVA(light.mUp), rot.Row(0));
    //        rot.Row(2) = AsVA(light.mUp);
    //        if (rot.Row(1).Length() < 0.99f)
    //            vaVector3::ComputeOrthonormalBasis(rot.Row(0), rot.Row(1), rot.Row(2));
    //    }
    //    vaMatrix4x4 trans = vaMatrix4x4::FromRotationTranslation(rot, AsVA(light.mPosition));

    //    entt::entity lightEntity = outScene.CreateEntity( /*vaStringTools::Format("light_%04d", i)*/light.mName.C_Str(), trans, lightsParent);

    //    Scene::LightBase lightBase = Scene::LightBase::Make();

    //    char* name;
    //    cgltf_float color[3];
    //    cgltf_float intensity;
    //    cgltf_light_type type;
    //    cgltf_float range;
    //    cgltf_float spot_inner_cone_angle;
    //    cgltf_float spot_outer_cone_angle;
    //    cgltf_extras extras;

    //    lightBase.Color = AsVA(light.color);
    //    lightBase.Intensity = light.intensity;
    //    vaColor::NormalizeLuminance(lightBase.Color, lightBase.Intensity);
    //    lightBase.FadeFactor = 1.0f;

    //
    //    switch (light.type)
    //    {
    //    case(cgltf_light_type_directional):
    //    {
    //        assert(false); // create a point light "far away" that is representative of the directional light
    //        // how far away can it be without messing up precision?
    //        // auto & newLight = outScene.Registry( ).emplace<Scene::LightDirectional>( lightEntity, lightBase );
    //        // // newLight.AngularRadius  = light.AngularRadius;
    //        // // newLight.HaloSize       = light.HaloSize;
    //        // // newLight.HaloFalloff    = light.HaloFalloff;
    //        // newLight.CastShadows    = true;
    //    } break;
    //    case(cgltf_light_type_point):
    //    {
    //        auto& newLight = outScene.Registry().emplace<Scene::LightPoint>(lightEntity, lightBase);
    //        newLight.Size = std::max(0.0001f, light.mSize.Length());
    //        newLight.Range = std::sqrtf(10000.0f * lightBase.Intensity); // not sure what to do about this - we don't use light.mAttenuationConstant/mAttenuationLinear/mAttenuationQuadratic
    //        newLight.SpotInnerAngle = 0.0f;
    //        newLight.SpotOuterAngle = 0.0f;
    //        newLight.CastShadows = false;
    //    } break;
    //    case(cgltf_light_type_spot):
    //    {
    //        auto& newLight = outScene.Registry().emplace<Scene::LightPoint>(lightEntity, lightBase);
    //        newLight.Size = std::max(0.0001f, light.mSize.Length());
    //        newLight.Range = std::sqrtf(10000.0f * lightBase.Intensity); // not sure what to do about this - we don't use light.mAttenuationConstant/mAttenuationLinear/mAttenuationQuadratic
    //        newLight.SpotInnerAngle = light.mAngleInnerCone; assert(light.mAngleInnerCone <= VA_PIf);
    //        newLight.SpotOuterAngle = light.mAngleOuterCone; assert(light.mAngleOuterCone <= VA_PIf);
    //        newLight.CastShadows = false;
    //    } break;

    //    case(cgltf_light_type_invalid):
    //    default:
    //        VA_WARN("Unrecognized or unsupported light type for light '%s'", light.mName.C_Str());
    //        outScene.DestroyEntity(lightEntity, false);
    //        break;
    //    }
    //}
    //}
#endif


static bool ProcessSceneNodes(const cgltf_data* loadedScene, LoadingTempStorage & tempStorage, vaAssetImporter::ImporterContext & importerContext )
{
    vaScene & outScene = *importerContext.Scene;
    //ProcessSceneLights(loadedScene, tempStorage, importerContext);

    // Each scene contains an array of nodes which are the root nodes of the scene. 
    // We can potentially have multiple root nodes within a scene each with own transform. But we only display the first scene
    cgltf_scene* scene = loadedScene->scene;
    if (loadedScene->scenes_count != 1)
    {
        VA_WARN("GLTF Importer Scene count not equal to 1 in loaded scene'%d'", loadedScene->scenes_count);
    }
    // assume a single scene for now, though a scene can typically have multiple root nodes
    assert(scene != nullptr);

    // scene->nodes are the root nodes
    vaMatrix4x4 transform = importerContext.BaseTransform;  

    string sceneName = loadedScene->scenes->name == nullptr ? "Scene" : loadedScene->scenes->name;
    entt::entity sceneRoot = outScene.CreateEntity( sceneName, transform );

    // if one of the calls to ProcessNodesRecursive fails, do we want to continue with the other nodes anyway?
    bool ret_value = true;
    for (int i = 0; i < scene->nodes_count; i++)
    {
        ret_value = ret_value && ProcessNodesRecursive(loadedScene, scene->nodes[i], tempStorage, importerContext, sceneRoot);
    }
    return ret_value;
}

static bool ProcessScene( const cgltf_data * loadedScene, LoadingTempStorage & tempStorage, vaAssetImporter::ImporterContext & importerContext )
{
    loadedScene; tempStorage; importerContext;
    if( importerContext.IsAborted() ) // UI clicked cancel
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
    if (!importerContext.AsyncInvokeAtBeginFrame([&](vaRenderDevice&, vaAssetImporter::ImporterContext& importerContext)
    {
        return ProcessSceneNodes(loadedScene, tempStorage, importerContext);
    }))
        return false;


    return true;
}

void PrintCGLTFError(cgltf_result result)
{
    switch (result)
    {
    case cgltf_result_data_too_short:    VA_LOG_ERROR("cgltf_result_data_too_short"); break;
    case cgltf_result_unknown_format:    VA_LOG_ERROR("cgltf_result_unknown_format"); break;
    case cgltf_result_invalid_json:      VA_LOG_ERROR("cgltf_result_invalid_json parsing gltf file"); break;
    case cgltf_result_invalid_gltf:      VA_LOG_ERROR("cgltf_result_invalid_gltf parsing gltf file"); break;
    case cgltf_result_invalid_options:   VA_LOG_ERROR("cgltf_result_invalid_options parsing gltf file"); break;
    case cgltf_result_file_not_found:    VA_LOG_ERROR("cgltf_result_file_not_found parsing gltf file"); break;
    case cgltf_result_io_error:          VA_LOG_ERROR("cgltf_result_io_error parsing gltf file"); break;
    case cgltf_result_out_of_memory:     VA_LOG_ERROR("cgltf_result_out_of_memory parsing gltf file"); break;
    case cgltf_result_legacy_gltf:       VA_LOG_ERROR("cgltf_result_legacy_gltf parsing gltf file"); break;
    default:                             VA_LOG_ERROR("unknown error parsing gltf file");
    }
}



bool LoadFileContents_cgltf( const string & path, vaAssetImporter::ImporterContext & importerContext )
{
    LoadingTempStorage tempStorage;
    vaFileTools::SplitPath( vaStringTools::ToLower( path ), &tempStorage.ImportDirectory, &tempStorage.ImportFileName, &tempStorage.ImportExt );

    string apath = path;

    // Initialize cgltf
    cgltf_options   options; memset( &options, 0, sizeof(options) );
    cgltf_data *    data = NULL;
    cgltf_result    result;
       
    // gltf is separated into two or more files - the first is the json .gltf file, the second is the .bin file with vertex buffers, etc, and the other files are textures, etc
    // read the .gltf file first
    {
        vaTimerLogScope timerLog( vaStringTools::Format( "cgltf parsing '%s'", path.c_str( ) ) );
        result = cgltf_parse_file(&options, path.c_str(), &data);
    }

    // error handling
    if( result != cgltf_result_success )
    {
        PrintCGLTFError(result);
        return false;
    }

    if( importerContext.IsAborted( ) ) // ui clicked cancel
    {
        cgltf_free( data );
        return false;
    }

    result = cgltf_load_buffers(&options, data, path.c_str());
    if (result != cgltf_result_success)
    {
        PrintCGLTFError(result);
        return false;
    }

    result = cgltf_validate(data);
    if (result != cgltf_result_success)
    {
        PrintCGLTFError(result);
        return false;
    }


    vaTimerLogScope timerLog( vaStringTools::Format( "Importing cgltf scene...", path.c_str( ) ) );
        
    // make awesome stuff 
    bool ret = ProcessScene( data, tempStorage, importerContext );
    
    cgltf_free( data );

    return ret;
}

