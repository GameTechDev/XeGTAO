///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef VA_SHADER_SHARED_TYPES_HLSL
#define VA_SHADER_SHARED_TYPES_HLSL

#include "vaShaderCore.h"

#include "vaPostProcessShared.h"

#ifndef VA_COMPILED_AS_SHADER_CODE
namespace Vanilla
{
#endif

#define SHADER_INSTANCE_INDEX_ROOT_CONSTANT_SLOT            16
#define SHADER_GENERIC_ROOT_CONSTANT_SLOT                   17

// for vaShaderItemGlobals (for stuff that is set less frequently than vaGraphicsItem/vaComputeItem - mostly used for vaDrawAttributes-based subsystems)
#define SHADERGLOBAL_SRV_SLOT_BASE                          32
#define SHADERGLOBAL_SRV_SLOT_COUNT                         16
#define SHADERGLOBAL_CBV_SLOT_BASE                           8
#define SHADERGLOBAL_CBV_SLOT_COUNT                          3          // so far we never needed more
#define SHADERGLOBAL_UAV_SLOT_BASE                           8
#define SHADERGLOBAL_UAV_SLOT_COUNT                          4
#define SHADERGLOBAL_SRV_SLOT_RAYTRACING_ACCELERATION       48
// #define SHADERGLOBAL_SAMPLER_SLOT_BASE                       4          // not actually used yet!

//////////////////////////////////////////////////////////////////////////
// PREDEFINED GLOBAL SAMPLER SLOTS
#define SHADERGLOBAL_SHADOWCMP_SAMPLERSLOT                   9
#define SHADERGLOBAL_POINTCLAMP_SAMPLERSLOT                 10
#define SHADERGLOBAL_POINTWRAP_SAMPLERSLOT                  11
#define SHADERGLOBAL_LINEARCLAMP_SAMPLERSLOT                12
#define SHADERGLOBAL_LINEARWRAP_SAMPLERSLOT                 13
#define SHADERGLOBAL_ANISOTROPICCLAMP_SAMPLERSLOT           14
#define SHADERGLOBAL_ANISOTROPICWRAP_SAMPLERSLOT            15
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// Generic float data in/out used mostly to get debugging data out of shaders
#define SHADERGLOBAL_GENERICDATACAPTURE_COLUMNS             256             // must be above 4 and below 8192
#define SHADERGLOBAL_GENERICDATACAPTURE_ROWS                4096            // must be above 1 and below 8192-1; texture y will be +1, first row is for the atomic counter
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// PREDEFINED CONSTANTS BUFFER SLOTS
//#define SHADERINSTANCE_CONSTANTSBUFFERSLOT                  0
//#define RENDERMESHMATERIAL_CONSTANTSBUFFERSLOT              1
#define SKYBOX_CONSTANTSBUFFERSLOT                          0
#define ZOOMTOOL_CONSTANTSBUFFERSLOT                        0
#define CDLOD2_CONSTANTS_BUFFERSLOT                         2
//
// global/system constant slots go from EXTENDED_SRV_CBV_UAV_SLOT_BASE to EXTENDED_SRV_CBV_UAV_SLOT_BASE+...16?
#define SHADERGLOBAL_CONSTANTSBUFFERSLOT                    (0)
#define LIGHTINGGLOBAL_CONSTANTSBUFFERSLOT                  (1)
// #define LIGHTINGGLOBAL_DISTANTIBL_CONSTANTSBUFFERSLOT      (SHADERGLOBAL_CBV_SLOT_BASE + 2)
//
#define SHADERGLOBAL_SHADER_FEEDBACK_STATIC_UAV_SLOT        (0)
#define SHADERGLOBAL_SHADER_FEEDBACK_DYNAMIC_UAV_SLOT       (1)
#define SHADERGLOBAL_GENERIC_OUTPUT_DATA_UAV_SLOT           (2)
//
// need to do this so X_CONCATENATER-s work
#define SHADERGLOBAL_CONSTANTSBUFFERSLOT_V                  8
#define LIGHTINGGLOBAL_CONSTANTSBUFFERSLOT_V                9
//
#define SHADERGLOBAL_SHADER_FEEDBACK_STATIC_UAV_SLOT_V       8
#define SHADERGLOBAL_SHADER_FEEDBACK_DYNAMIC_UAV_SLOT_V      9
#define SHADERGLOBAL_GENERIC_OUTPUT_DATA_UAV_SLOT_V         10
//
#if     ((SHADERGLOBAL_CBV_SLOT_BASE+SHADERGLOBAL_CONSTANTSBUFFERSLOT)              != SHADERGLOBAL_CONSTANTSBUFFERSLOT_V)                  \
    ||  ((SHADERGLOBAL_CBV_SLOT_BASE+LIGHTINGGLOBAL_CONSTANTSBUFFERSLOT)            != LIGHTINGGLOBAL_CONSTANTSBUFFERSLOT_V)                \
                                                                                                                                            \
    ||  ((SHADERGLOBAL_UAV_SLOT_BASE+SHADERGLOBAL_SHADER_FEEDBACK_STATIC_UAV_SLOT)  != SHADERGLOBAL_SHADER_FEEDBACK_STATIC_UAV_SLOT_V)      \
    ||  ((SHADERGLOBAL_UAV_SLOT_BASE+SHADERGLOBAL_SHADER_FEEDBACK_DYNAMIC_UAV_SLOT) != SHADERGLOBAL_SHADER_FEEDBACK_DYNAMIC_UAV_SLOT_V)     \
    ||  ((SHADERGLOBAL_UAV_SLOT_BASE+SHADERGLOBAL_GENERIC_OUTPUT_DATA_UAV_SLOT)     != SHADERGLOBAL_GENERIC_OUTPUT_DATA_UAV_SLOT_V) 
    #error _V values above not in sync, just fix them up please
#endif

// PREDEFINED CONSTANTS BUFFER SLOTS
//////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////
// PREDEFINED SHADER RESOURCE VIEW SLOTS
//
// 10 should be enough for materials, right, right?
#define RENDERMATERIAL_MAX_TEXTURES                         16
#define RENDERMATERIAL_MAX_INPUT_SLOTS                      8
#define RENDERMATERIAL_MAX_NODES                            16
#define RENDERMATERIAL_MAX_SHADER_CONSTANTS                 (RENDERMATERIAL_MAX_INPUT_SLOTS+RENDERMATERIAL_MAX_NODES)
//
#define CDLOD2_TEXTURE_SLOT0                                10
#define CDLOD2_TEXTURE_SLOT1                                11
#define CDLOD2_TEXTURE_SLOT2                                12
#define CDLOD2_TEXTURE_OVERLAYMAP_0                         13
//
#define SIMPLE_PARTICLES_VIEWSPACE_DEPTH                    10
//
//////////////////////////////////////////////////////////////////////////

// global texture slots go from EXTENDED_SRV_CBV_UAV_SLOT_BASE to EXTENDED_SRV_CBV_UAV_SLOT_BASE+ ... 8?
#define SHADERGLOBAL_INSTANCE_CONSTANTBUFFERS_TEXTURESLOT   (0)
#define SHADERGLOBAL_MESH_CONSTANTBUFFERS_TEXTURESLOT       (1)
#define SHADERGLOBAL_MATERIAL_CONSTANTBUFFERS_TEXTURESLOT   (2)
#define SHADERGLOBAL_LIGHTING_CUBE_SHADOW_TEXTURESLOT       (3)
#define LIGHTINGGLOBAL_LOCALIBL_REFROUGHMAP_TEXTURESLOT     (4)        // <- this is 'modern' reflection map
#define LIGHTINGGLOBAL_LOCALIBL_IRRADIANCEMAP_TEXTURESLOT   (5)        // <- this is 'modern' irradiance map (using either this or SH)
#define LIGHTINGGLOBAL_DISTANTIBL_REFROUGHMAP_TEXTURESLOT   (6)        // <- this is 'modern' reflection map
#define LIGHTINGGLOBAL_DISTANTIBL_IRRADIANCEMAP_TEXTURESLOT (7)        // <- this is 'modern' irradiance map (using either this or SH)
#define SHADERGLOBAL_AOMAP_TEXTURESLOT                      (8)        // <- this is the SSAO (for now)
#define SHADERGLOBAL_MATERIAL_DFG_LOOKUPTABLE_TEXTURESLOT   (9)

#define LIGHTINGGLOBAL_SIMPLELIGHTS_SLOT                    (10)
#define LIGHTINGGLOBAL_LIGHT_TREE_SLOT                      (11)
#define LIGHTINGGLOBAL_UNUSED_SLOT                          (12)

#define SHADERGLOBAL_DEPTH_TEXTURESLOT                      (13)

//#define SHADERGLOBAL_RAYTRACING_ACCELERATION_STRUCT         (15)        // raytracing acceleration structure

// this is so annoying but I don't know how to resolve it - in order to be used in 'T_CONCATENATER', it has to be a number string token or something like that so "(base+n)" doesn't work
#define SHADERGLOBAL_INSTANCE_CONSTANTBUFFERS_TEXTURESLOT_V     32
#define SHADERGLOBAL_MESH_CONSTANTBUFFERS_TEXTURESLOT_V         33
#define SHADERGLOBAL_MATERIAL_CONSTANTBUFFERS_TEXTURESLOT_V     34
#define SHADERGLOBAL_LIGHTING_CUBE_SHADOW_TEXTURESLOT_V         35
#define LIGHTINGGLOBAL_LOCALIBL_REFROUGHMAP_TEXTURESLOT_V       36
#define LIGHTINGGLOBAL_LOCALIBL_IRRADIANCEMAP_TEXTURESLOT_V     37
#define LIGHTINGGLOBAL_DISTANTIBL_REFROUGHMAP_TEXTURESLOT_V     38
#define LIGHTINGGLOBAL_DISTANTIBL_IRRADIANCEMAP_TEXTURESLOT_V   39
#define SHADERGLOBAL_AOMAP_TEXTURESLOT_V                        40
#define SHADERGLOBAL_MATERIAL_DFG_LOOKUPTABLE_TEXTURESLOT_V     41
#define LIGHTINGGLOBAL_SIMPLELIGHTS_SLOT_V                      42
#define LIGHTINGGLOBAL_LIGHT_TREE_SLOT_V                         43
#define LIGHTINGGLOBAL_UNUSED_SLOT_V                          44
#define SHADERGLOBAL_DEPTH_TEXTURESLOT_V                        45
//#define SHADERGLOBAL_RAYTRACING_ACCELERATION_STRUCT_V           47
#if (   (SHADERGLOBAL_SRV_SLOT_BASE+SHADERGLOBAL_MATERIAL_DFG_LOOKUPTABLE_TEXTURESLOT   ) != SHADERGLOBAL_MATERIAL_DFG_LOOKUPTABLE_TEXTURESLOT_V    \
    ||  (SHADERGLOBAL_SRV_SLOT_BASE+SHADERGLOBAL_MESH_CONSTANTBUFFERS_TEXTURESLOT       ) != SHADERGLOBAL_MESH_CONSTANTBUFFERS_TEXTURESLOT_V    \
    ||  (SHADERGLOBAL_SRV_SLOT_BASE+SHADERGLOBAL_MATERIAL_CONSTANTBUFFERS_TEXTURESLOT   ) != SHADERGLOBAL_MATERIAL_CONSTANTBUFFERS_TEXTURESLOT_V    \
    ||  (SHADERGLOBAL_SRV_SLOT_BASE+SHADERGLOBAL_INSTANCE_CONSTANTBUFFERS_TEXTURESLOT   ) != SHADERGLOBAL_INSTANCE_CONSTANTBUFFERS_TEXTURESLOT_V    \
    ||  (SHADERGLOBAL_SRV_SLOT_BASE+SHADERGLOBAL_LIGHTING_CUBE_SHADOW_TEXTURESLOT       ) != SHADERGLOBAL_LIGHTING_CUBE_SHADOW_TEXTURESLOT_V        \
    ||  (SHADERGLOBAL_SRV_SLOT_BASE+LIGHTINGGLOBAL_LOCALIBL_REFROUGHMAP_TEXTURESLOT     ) != LIGHTINGGLOBAL_LOCALIBL_REFROUGHMAP_TEXTURESLOT_V      \
    ||  (SHADERGLOBAL_SRV_SLOT_BASE+LIGHTINGGLOBAL_LOCALIBL_IRRADIANCEMAP_TEXTURESLOT   ) != LIGHTINGGLOBAL_LOCALIBL_IRRADIANCEMAP_TEXTURESLOT_V    \
    ||  (SHADERGLOBAL_SRV_SLOT_BASE+LIGHTINGGLOBAL_DISTANTIBL_REFROUGHMAP_TEXTURESLOT   ) != LIGHTINGGLOBAL_DISTANTIBL_REFROUGHMAP_TEXTURESLOT_V    \
    ||  (SHADERGLOBAL_SRV_SLOT_BASE+LIGHTINGGLOBAL_DISTANTIBL_IRRADIANCEMAP_TEXTURESLOT ) != LIGHTINGGLOBAL_DISTANTIBL_IRRADIANCEMAP_TEXTURESLOT_V  \
    ||  (SHADERGLOBAL_SRV_SLOT_BASE+SHADERGLOBAL_AOMAP_TEXTURESLOT                      ) != SHADERGLOBAL_AOMAP_TEXTURESLOT_V                       \
    ||  (SHADERGLOBAL_SRV_SLOT_BASE+LIGHTINGGLOBAL_SIMPLELIGHTS_SLOT                    ) != LIGHTINGGLOBAL_SIMPLELIGHTS_SLOT_V                     \
    ||  (SHADERGLOBAL_SRV_SLOT_BASE+LIGHTINGGLOBAL_LIGHT_TREE_SLOT                      ) != LIGHTINGGLOBAL_LIGHT_TREE_SLOT_V                        \
    ||  (SHADERGLOBAL_SRV_SLOT_BASE+LIGHTINGGLOBAL_UNUSED_SLOT                        ) != LIGHTINGGLOBAL_UNUSED_SLOT_V                         \
    ||  (SHADERGLOBAL_SRV_SLOT_BASE+SHADERGLOBAL_DEPTH_TEXTURESLOT                      ) != SHADERGLOBAL_DEPTH_TEXTURESLOT_V                       \
        )
//|| ( SHADERGLOBAL_SRV_SLOT_BASE + SHADERGLOBAL_RAYTRACING_ACCELERATION_STRUCT ) != SHADERGLOBAL_RAYTRACING_ACCELERATION_STRUCT_V          
    #error _V values above not in sync, just fix them up please
#endif


struct ShaderGlobalConstants
{
    vaMatrix4x4             View;                   // a.k.a. world to view
    vaMatrix4x4             ViewInv;                // a.k.a. view to world
    vaMatrix4x4             Proj;                   // a.k.a. view to projection
    vaMatrix4x4             ProjInv;                // a.k.a. projection to view
    vaMatrix4x4             ViewProj;               // a.k.a. world to projection
    vaMatrix4x4             ViewProjInv;            // a.k.a. projection to world
    vaMatrix4x4             ReprojectionMatrix;     // see vaDrawAttributes::GlobalSettings::ReprojectionMatrix

    vaVector4               WorldBase;              // global world position offset for shading; used to make all shading computation close(r) to (0,0,0) for precision purposes
    vaVector4               PreviousWorldBase;      // same as WorldBase except a frame old (same old frame used to compute ReprojectionMatrix)
    vaVector4               CameraDirection;
    vaVector4               CameraRightVector;
    vaVector4               CameraUpVector;
    vaVector4               CameraWorldPosition;    // drawContext.Camera.GetPosition() - drawContext.WorldBase; WARNING: THIS DOES NOT CONTAIN THE JITTER: TODO: FIX IT
    vaVector4               CameraSubpixelOffset;   // .xy contains value of subpixel offset used for supersampling/TAA (otheriwse knows as jitter) or (0,0) if no jitter enabled; .zw are 0

    vaVector2               ViewportSize;           // ViewportSize.x, ViewportSize.y
    vaVector2               ViewportPixelSize;      // 1.0 / ViewportSize.x, 1.0 / ViewportSize.y
    vaVector2               ViewportHalfSize;       // ViewportSize.x * 0.5, ViewportSize.y * 0.5
    vaVector2               ViewportPixel2xSize;    // 2.0 / ViewportSize.x, 2.0 / ViewportSize.y

    vaVector2               DepthUnpackConsts;
    vaVector2               CameraTanHalfFOV;
    vaVector2               CameraNearFar;
    vaVector2               Noise;

    vaVector2               FOVXY;                  // { camera.GetXFOV(), camera.GetYFOV() }
    vaVector2               PixelFOVXY;             // { camera.GetXFOV() / camera.ViewportSize.x, camera.camera.GetYFOV() / camera.ViewportSize.y } // these 2 are actually identical because pixels are square

    float                   GlobalMIPOffset;
    float                   GlobalSpecularAAScale;
    float                   GlobalSpecialEmissiveScale;
    float                   HDRClamp;

    float                   TransparencyPass;               // TODO: remove
    float                   WireframePass;                  // 1 if vaDrawAttributes::RenderFlags::DebugWireframePass set, otherwise 0
    float                   EV100;                          // see https://google.github.io/filament/Filament.html#lighting/directlighting/pre-exposedlights
    float                   PreExposureMultiplier;          // most of the lighting is already already pre-exposed on the CPU side, but not all


    float                   TimeFract;              // fractional part of total time from starting the app
    float                   TimeFmod3600;           // remainder of total time from starting the app divided by an hour (3600 seconds) - leaves you with at least 2 fractional part decimals of precision
    float                   SinTime2Pi;             // sine of total time from starting the app times 2*PI
    float                   SinTime1Pi;             // sine of total time from starting the app times PI

    vaVector2               CursorViewportPosition;
    uint                    CursorHoverItemCaptureEnabled;
    uint                    CursorKeyClicked;       // 0 - not clicked, (1<<0) - left mouse button -> the rest isn't implemented yet

    int                     GenericDataCollectEnabled;
    float                   RaytracingMIPOffset;
    int                     AlphaTAAHackEnabled;
    int                     FrameIndexMod64;

    vaVector2               CameraJitterDelta;      // see vaDrawAttributes::GlobalSettings::CameraJitterDelta
    vaVector2               Dummy0;

    // // push (or pull) vertices by this amount away (towards) camera; to avoid z fighting for debug wireframe or for the shadow drawing offset
    // // 'absolute' is in world space; 'relative' means 'scale by distance to camera'
    // float                   ViewspaceDepthOffsetFlatAbs;
    // float                   ViewspaceDepthOffsetFlatRel;
    // float                   ViewspaceDepthOffsetSlopeAbs;
    // float                   ViewspaceDepthOffsetSlopeRel;
};

struct ShaderSkyboxConstants
{
    vaMatrix4x4         ProjToWorld;
    vaMatrix4x4         CubemapRotate;
    vaVector4           ColorMul;
};

struct SimpleSkyConstants
{
    vaMatrix4x4             ProjToWorld;

    vaVector4               SunDir;

    vaVector4               SkyColorLow;
    vaVector4               SkyColorHigh;

    vaVector4               SunColorPrimary;
    vaVector4               SunColorSecondary;

    float                   SkyColorLowPow;
    float                   SkyColorLowMul;

    float                   SunColorPrimaryPow;
    float                   SunColorPrimaryMul;
    float                   SunColorSecondaryPow;
    float                   SunColorSecondaryMul;

    float                   Dummy0;
    float                   Dummy1;
};

// Who/what/where this draw call originated from (which scene, which entity, which mesh, which material, etc.)
struct DrawOriginInfo
{
    uint                    SceneID;            // see vaRuntimeID - but assuming there's no more than 2^32 scenes ever (there's an assert though)
    uint                    EntityID;           // entt::entity
    uint                    MeshAssetID;        // see vaRuntimeID - searchable through asset manager; actually only 32bit at the moment to reduce complexity, should be enough for now
    uint                    MaterialAssetID;    // see vaRuntimeID - searchable through asset manager; actually only 32bit at the moment to reduce complexity, should be enough for now

#ifndef VA_COMPILED_AS_SHADER_CODE
    static constexpr std::uint32_t NullSceneRuntimeID   = 0xFFFFFFFF;
    static constexpr std::uint32_t NullSceneEntityID    = 0xFFFFFFFF;
    static constexpr std::uint32_t NullAssetID          = 0xFFFFFFFF;
    bool operator == ( const Vanilla::DrawOriginInfo & other )
    {   return SceneID == other.SceneID && EntityID == other.EntityID && MeshAssetID == other.MeshAssetID && MaterialAssetID == other.MaterialAssetID; }
#endif
};

// Making this any bigger is very costly so consider creating a separate table for something like CustomMaterialParams
struct ShaderInstanceConstants
{
    // world transform
    vaMatrix4x3             World;
    vaMatrix4x3             PreviousWorld;  // previous frame's transform world
    
    // since we now support non-uniform scale, we need the 'normal matrix' to keep normals correct 
    // (for more info see : https://www.scratchapixel.com/lessons/mathematics-physics-for-computer-graphics/geometry/transforming-normals or http://www.lighthouse3d.com/tutorials/glsl-12-tutorial/the-normal-matrix/ )
    vaMatrix4x3             NormalWorld;    // 3 floats are unused here - consider reusing for something

    DrawOriginInfo          OriginInfo;

    // Easy way to transmit some per-object (per selection item) value that can have a different meaning based on materials/etc. 
    // Can also be used as an index into a separate table ('asuint') or whatever.
    // vaVector4        CustomParam; 

    uint                    MaterialGlobalIndex;        // <- these two can be packed to uint16
    uint                    MeshGlobalIndex;            // <- these two can be packed to uint16
    // used for highlights, wireframe, lights, etc; rgb is added, alpha multiplies the original; for ex: " finalColor.rgb = finalColor.rgb * instance.EmissiveAdd.a + instance.EmissiveAdd.rgb; "
    uint                    EmissiveAddPacked;          // packed into R10G10B10FLOAT_A2_UNORM

#define VA_INSTANCE_FLAG_TRANSPARENT            ( 1 << 0 )
    uint                    Flags;

    vaVector3               EmissiveMultiplier;         // if using light to drive emissive
    float                   Dummy;                      
};

// Per-mesh constants
struct ShaderMeshConstants
{
    uint                    IndexBufferBindlessIndex;       // these are for bindless access
    uint                    VertexBufferBindlessIndex;      // these are for bindless access
    uint                    FrontFaceIsClockwise;           // which one is the "front" face? used for normal computation - corresponds to "vaRenderMesh::m_frontFaceWinding == vaWindingOrder::Clockwise"
    uint                    Dummy1;
#ifndef VA_COMPILED_AS_SHADER_CODE
    inline void             Invalidate( )           { IndexBufferBindlessIndex = 0xFFFFFFFF; VertexBufferBindlessIndex = 0xFFFFFFFF; }
#endif
};

// Per-material constants
struct ShaderMaterialConstants
{
    // texture bindless indices: using uint4 for tight packing on HLSL side (somewhat ugly syntax)
#ifdef VA_COMPILED_AS_SHADER_CODE
    uint4                   BindlessSRVIndicesPacked[RENDERMATERIAL_MAX_TEXTURES/4];
    // to unpack, use: " static uint BindlessSRVIndices[16] = (uint[16])BindlessSRVIndicesPacked; "
#else
    uint                    BindlessSRVIndices[RENDERMATERIAL_MAX_TEXTURES];
#endif

    vaVector4               Constants[RENDERMATERIAL_MAX_SHADER_CONSTANTS];

    uint                    ShaderTableIndex;           // see vaRenderMaterial::m_shaderTableIndex
    float                   AlphaTestThreshold;
    float                   VA_RM_LOCALIBL_NORMALBIAS;  // these two are hacks and will go away in the future - they used to be macros so keeping the naming convention
    float                   VA_RM_LOCALIBL_BIAS      ;  // these two are hacks and will go away in the future - they used to be macros so keeping the naming convention
    float                   IndexOfRefraction;
    float                   NEETranslucentAlpha;        // 1 if disabled
    float                   Padding0;
    float                   Padding1;

#ifndef VA_COMPILED_AS_SHADER_CODE
    inline void             Invalidate( )           
    { 
        ShaderTableIndex = 0;
        for( int i = 0; i < countof(BindlessSRVIndices); i++ )  BindlessSRVIndices[i]   = 0xFFFFFFFF; 
        for( int i = 0; i < countof(Constants); i++ )           Constants[i]            = {0,0,0,0}; 
        AlphaTestThreshold = 0.0f; VA_RM_LOCALIBL_NORMALBIAS = 0; VA_RM_LOCALIBL_BIAS = 0; IndexOfRefraction = 0; NEETranslucentAlpha = 0;
    }
#endif
};

#ifndef VA_COMPILED_AS_SHADER_CODE
    inline bool operator == ( const ShaderMeshConstants & left, const ShaderMeshConstants & right ) noexcept            { return std::memcmp( &left, &right, sizeof( left ) ) == 0; }
    inline bool operator != ( const ShaderMeshConstants & left, const ShaderMeshConstants & right ) noexcept            { return std::memcmp( &left, &right, sizeof( left ) ) != 0; }
    inline bool operator == ( const ShaderMaterialConstants & left, const ShaderMaterialConstants & right ) noexcept    { return std::memcmp( &left, &right, sizeof( left ) ) == 0; }
    inline bool operator != ( const ShaderMaterialConstants & left, const ShaderMaterialConstants & right ) noexcept    { return std::memcmp( &left, &right, sizeof( left ) ) != 0; }
#endif

// struct GBufferConstants
// {
//     float               Dummy0;
//     float               Dummy1;
//     float               Dummy2;
//     float               Dummy3;
// };

struct ZoomToolShaderConstants
{
    vaVector4               SourceRectangle;

    int                     ZoomFactor;
    float                   Dummy1;
    float                   Dummy2;
    float                   Dummy3;
};


struct CursorHoverInfo
{
    DrawOriginInfo          OriginInfo;
    vaVector3               WorldspacePos;
    float                   ViewspaceDepth;
};

// Static part for shader feedback - this always gets copied to readback buffer and read 
struct ShaderFeedbackStatic
{
    const static int        MaxCursorHoverInfoItems = 16;

    CursorHoverInfo         CursorHoverInfoItems[MaxCursorHoverInfoItems];

    int                     CursorHoverInfoCounter;
    int                     DynamicItemCounter;
    int                     GenericCounter;         // used by DebugCounter shader function
    int                     Dummy1;

    uint                    AssertFlag;
    uint                    AssertPayloadUINT;
    float                   AssertPayloadFLOAT;
    uint                    OnceFlag;

//    // used by materials or etc. - using uint4 for tight packing on HLSL side
//#ifdef VA_COMPILED_AS_SHADER_CODE
//    uint4                   AssertItemsPacked[4];
//    // to unpack, use: " static uint AssertItems[16] = (uint[16])AssertItemsPacked; "
//#else
//    uint                    AssertItems[16];
//#endif
};

// Dynamic part for shader feedback - this ALWAYS gets copied to readback buffer but only ShaderFeedbackStatic::DynamicItemCounter number get read/processed
struct ShaderFeedbackDynamic
{
    //there's a constant per-frame size cost to copying these back to the CPU memory; 16 * 1024 is already pushing it a bit - some compression needed for more!
    const static int        MaxItems                    = 16 * 1024;

    enum Types
    {
        Type_LogTextNewLine = 0,
        Type_LogTextUINT    = 1,
        Type_LogTextUINT4   = 2,
        Type_LogTextFLT     = 3,
        Type_LogTextFLT2    = 4,
        Type_LogTextFLT3    = 5,
        Type_LogTextFLT4    = 6,
        Type_2DLine         = 7,
        Type_2DCircle       = 8,
        Type_2DRectangle    = 9,
        Type_2DTextUINT     = 10,
        Type_2DTextUINT4    = 11,
        Type_2DTextFLT      = 12,
        Type_2DTextFLT4     = 13,
        Type_3DTextUINT     = 14,
        Type_3DTextUINT4    = 15,
        Type_3DTextFLT      = 16,
        Type_3DTextFLT4     = 17,
        Type_3DLine         = 18,
        Type_3DSphere       = 19,
        Type_3DBox          = 20,
        Type_3DCylinder     = 21,
        Type_3DArrow        = 22,     
        Type_3DSphereCone   = 23,
        Type_3DLightViz     = 24,
        Type_MaxVal
    };

    vaVector4               Ref0;
    vaVector4               Ref1;
    vaVector4               Color;
    uint                    Type;
    uint                    Param0;
    float                   Param1;
    float                   Param2;
};

// used for various visualizations
enum class RasterizerDebugViewType : uint
{
    None                            ,
    ViewspaceDepth                  ,
    ScreenspaceNormal               ,
    AmbientOcclusion                ,
    MotionVectors                   ,
    MaxValue
};

// used for various visualizations
enum class PathTracerDebugViewType : uint
{
    None                            ,
    BounceIndex                     ,            // Could be used as OverdrawCount for rasterization
    ViewspaceDepth                  ,            // viewspace depth (in path tracing, only primary ray a.k.a. bounce 0)
    SurfacePropsBegin               ,            // everything between SurfacePropsBegin and SurfacePropsEnd reduces the bounce count to 0
    GeometryTexcoord0               = SurfacePropsBegin,
    GeometryNormalNonInterpolated   ,
    GeometryNormalInterpolated      ,
    GeometryTangentInterpolated     ,
    GeometryBitangentInterpolated   ,
    ShadingNormal                   ,
    MaterialBaseColor               ,
    MaterialBaseColorAlpha          ,
    MaterialEmissive                ,
    MaterialMetalness               ,
    MaterialRoughness               ,
    MaterialReflectance             ,
    MaterialAmbientOcclusion        ,
    ReflectivityEstimate            ,
    NEELightPDF                     ,
    BouncePropsBegin                ,               // 'BouncePropsXXX' are an exception that require bounce count 1
    BounceSpecularness              = BouncePropsBegin,
    BouncePDF                       ,
    BounceRefracted                 ,
    BouncePropsEnd                  = BounceRefracted,
    MaterialID                      ,
    ShaderID                        ,
    SurfacePropsEnd                 = ShaderID,
    DenoiserAuxAlbedo               ,
    DenoiserAuxNormals              ,
    DenoiserAuxMotionVectors        ,
    MaxValue
};

#ifndef VA_COMPILED_AS_SHADER_CODE
} // namespace Vanilla
#endif

#ifdef VA_COMPILED_AS_SHADER_CODE

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Bindless texture access
Texture2D                       g_bindlessTex2D[]           : register( t0, space1 );
//
// Bindless generic byte address buffer access
// These are used to read index (uin32) & vertex (vaRenderMesh::StandardVertex) buffers directy (for raytracing for ex.); 
ByteAddressBuffer               g_bindlessBAB[]     : register( t0, space2 );
// (Using ByteAddressBuffer for now until GetResourceFromHeap mechanics SM6.6 "unlocks" easier bindless access, see
// https://github.com/microsoft/DirectXShaderCompiler/issues/1067); 
//struct PrimitiveIndices { uint    a, b, c; };
//StructuredBuffer<uint>                          g_bindlessIndices[]     : register( t1, space1 );
//StructuredBuffer<RenderMeshVertex> g_bindlessVertices[]    : register( t2, space1 );
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Global insta


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Global buffers
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

cbuffer GlobalConstantsBuffer                       : register( B_CONCATENATER( SHADERGLOBAL_CONSTANTSBUFFERSLOT_V ) )
{
    ShaderGlobalConstants                   g_globals;
}

RWTexture2D<uint> g_GenericOutputDataUAV            : register( U_CONCATENATER( SHADERGLOBAL_GENERIC_OUTPUT_DATA_UAV_SLOT_V ) );

RWStructuredBuffer<ShaderFeedbackStatic>
                    g_shaderFeedbackStatic          : register( U_CONCATENATER( SHADERGLOBAL_SHADER_FEEDBACK_STATIC_UAV_SLOT_V ) );
RWStructuredBuffer<ShaderFeedbackDynamic>
                    g_shaderFeedbackDynamic         : register( U_CONCATENATER( SHADERGLOBAL_SHADER_FEEDBACK_DYNAMIC_UAV_SLOT_V ) );

StructuredBuffer<ShaderMeshConstants>       g_meshConstants         : register( T_CONCATENATER( SHADERGLOBAL_MESH_CONSTANTBUFFERS_TEXTURESLOT_V ) );
StructuredBuffer<ShaderMaterialConstants>   g_materialConstants     : register( T_CONCATENATER( SHADERGLOBAL_MATERIAL_CONSTANTBUFFERS_TEXTURESLOT_V ) );
StructuredBuffer<ShaderInstanceConstants>   g_instanceConstants     : register( T_CONCATENATER( SHADERGLOBAL_INSTANCE_CONSTANTBUFFERS_TEXTURESLOT_V ) );

#ifndef VA_RAYTRACING
struct ShaderInstanceIndexConstant  { uint InstanceIndex; };
ConstantBuffer<ShaderInstanceIndexConstant> g_instanceIndex         : register( B_CONCATENATER( SHADER_INSTANCE_INDEX_ROOT_CONSTANT_SLOT ) );
#endif

Texture2D<float>                            g_globalDepthTexture    : register( T_CONCATENATER( SHADERGLOBAL_DEPTH_TEXTURESLOT_V ) );

struct ShaderGenericRootConstant    { uint Value; };
ConstantBuffer<ShaderGenericRootConstant>   g_genericRootConstBuff  : register( B_CONCATENATER( SHADER_GENERIC_ROOT_CONSTANT_SLOT ) );
static const uint                           g_genericRootConst = g_genericRootConstBuff.Value;


// removing this because it doesn't work from callable shaders and etc. and can cause more confusion than help
//uint GetInstanceIndex( )
//{
//#ifndef VA_RAYTRACING
//    return g_instanceIndex.InstanceIndex;
//#else
//    return InstanceIndex();
//#endif
//}

// 'instanceIndex' is either g_instanceIndex.InstanceIndex during rasterization or InstanceIndex() during raytracing (only supported hit shaders)
ShaderInstanceConstants     LoadInstanceConstants( uint instanceIndex )
{
    return g_instanceConstants[ instanceIndex ];
}


#endif

#endif // VA_SHADER_SHARED_TYPES_HLSL