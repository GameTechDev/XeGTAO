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

#include "Scene/vaCameraBase.h"

#include "Core/vaUIDObject.h"

#include "Core/vaProfiler.h"

#include "Core/vaConcurrency.h"

#include "Rendering/Shaders/vaSharedTypes.h"
#include "Rendering/Shaders/vaConversions.h"

namespace Vanilla
{
    class vaRenderDevice;
    class vaRenderingModule;
    class vaRenderingModuleAPIImplementation;
    struct vaRenderOutputs;
    //class vaRenderDevice;
    //class vaRenderDeviceContext;
    class vaRenderGlobals;
    class vaSceneLighting;
    class vaSceneRaytracing;
    class vaAssetPack;
    class vaTexture;
    class vaXMLSerializer;
    class vaPixelShader;
    class vaShaderLibrary;
    class vaConstantBuffer;
    class vaDynamicVertexBuffer;
    // class vaVertexBuffer;
    // class vaIndexBuffer;
    class vaVertexShader;
    class vaGeometryShader;
    class vaHullShader;
    class vaDomainShader;
    class vaPixelShader;
    class vaComputeShader;
    class vaShaderResource;
    class vaRenderBuffer;

    // Not sure this belongs here but whatever
    enum class vaFullscreenState : int32
    {
        Unknown                 = 0,
        Windowed                = 1,
        Fullscreen              = 2,
        FullscreenBorderless    = 3
    };

    enum class vaBlendMode : int32
    {
        Opaque,
        Additive,
        AlphaBlend,
        PremultAlphaBlend,
        Mult,
        OffscreenAccumulate,    // for later compositing with PremultAlphaBlend - see https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch23.html (23.5 Alpha Blending) for more detail (although they have a mistake in their blend states...)
    };

    // This has some overlap in the meaning with vaBlendMode but is a higher-level abstraction; blend mode only defines color blending operation while LayerMode
    // defines blend mode, draw order (solid and alpha tested go first, then decal, then transparencies), depth buffer use and etc.
    enum class vaLayerMode : int32
    {
        Opaque              = 0,        // Classic opaque geometry      (writes into depth, overwrites color)
        AlphaTest           = 1,        // Opaque but uses alpha test   (writes into depth, overwrites color)
        Decal               = 2,        // has to be placed upon existing opaque geometry; always drawn before all other transparencies (drawing order/visibility guaranteed by depth buffer), alpha-blends into color, doesn't write into depth but has depth test enabled, doesn't ignore SSAO
        Transparent         = 3,        // transparent geometry; sorted by distance, alpha-blends into color, doesn't write into depth but has depth test enabled, ignores SSAO
        MaxValue
    };

    enum class vaPrimitiveTopology : int32
    {
        PointList,
        LineList,
        TriangleList,
        TriangleStrip,
    };

    // don't change these enum values - they are expected to be what they're set to.
    enum class vaComparisonFunc : int32
    {
        Never           = 1,
        Less            = 2,
        Equal           = 3,
        LessEqual       = 4,
        Greater         = 5,
        NotEqual        = 6,
        GreaterEqual    = 7,
        Always          = 8
    };

    enum class vaFillMode : int32
    {
        Wireframe       = 2,
        Solid           = 3
    };

    // analogous to D3D12_SHADING_RATE
    enum class vaShadingRate : int32
    {
        ShadingRate1X1  = 0,
        ShadingRate1X2  = 0x1,
        ShadingRate2X1  = 0x4,
        ShadingRate2X2  = 0x5,
        ShadingRate2X4  = 0x6,
        ShadingRate4X2  = 0x9,
        ShadingRate4X4  = 0xa
    };

    // Some of the predefined sampler types - defined in vaStandardSamplers.hlsl shader include on the shader side
    enum class vaStandardSamplerType : int32
    {
        // TODO: maybe implement mirror but beware of messing up existing data files; also need to update global samplers...
        PointClamp,
        PointWrap,
        //PointMirror,
        LinearClamp,
        LinearWrap,
        //LinearMirror,
        AnisotropicClamp,
        AnisotropicWrap,
        //AnisotropicMirror,

        MaxValue
    };

    enum class vaDrawResultFlags : uint32
    {
        None                        = 0,            // means all ok
        UnspecifiedError            = ( 1 << 0 ),   // this is a bug / data error that has been handled graciously but will not go away
        ShadersStillCompiling       = ( 1 << 1 ),   // also means PSO still compiling
        AssetsStillLoading          = ( 1 << 2 ),
        PendingVisualDependencies   = ( 1 << 3 ),   // this is a somewhat generic "some significant subsystems like shadow maps or IBLs have not yet been updates so visuals will be grossly incorrect"
    };
    BITFLAG_ENUM_CLASS_HELPER( vaDrawResultFlags );

    enum class vaRenderMaterialShaderType
    {
        DepthOnly       = 0,            // Z-pre-pass; pure depth only for shadow maps and etc
        RichPrepass     = 1,            // Z-pre-pass+ (for ex, outputs normals as well - could dump a proper gbuffer)
        Forward         = 2,
        //Deferred        = 2,          // removed for now
    };

    // This is currently only used internally when/if vaDrawAttributes is used during BeginItems; this is where all constants, SRVs, UAVs, whatnot get
    // set from vaDrawAttributes and vaRenderGlobals and etc; 
    // In the future, if there's need to manually fill these it should be trivial to add BeginItems variant only taking vaShaderItemGlobals arguments!
    // In the future, this might be where viewport and render targets get set as well. This would simplify resource transitions.
    struct vaShaderItemGlobals
    {
        static const uint32                 ShaderResourceViewsShaderSlotBase   = SHADERGLOBAL_SRV_SLOT_BASE;
        static const uint32                 ConstantBuffersShaderSlotBase       = SHADERGLOBAL_CBV_SLOT_BASE;
        static const uint32                 UnorderedAccessViewsShaderSlotBase  = SHADERGLOBAL_UAV_SLOT_BASE;

        // SRVs
        std::array<vaFramePtr<vaShaderResource>, SHADERGLOBAL_SRV_SLOT_COUNT>
                                            ShaderResourceViews;

        // CONSTANT BUFFERS - first is usually ShaderGlobalConstants, second one is usually ShaderLightingConstants and the rest is usually unused
        std::array<vaFramePtr<vaConstantBuffer>, SHADERGLOBAL_CBV_SLOT_COUNT>
                                            ConstantBuffers;

        // UAVs (this isn't fully supported for pixel shaders due to mismatch with API support)
        std::array<vaFramePtr<vaShaderResource>, SHADERGLOBAL_UAV_SLOT_COUNT>
                                            UnorderedAccessViews;       // I did not add provision for hidden counter resets - if needed better use a different approach (InterlockedX on a separate untyped UAV)

        vaFramePtr<vaRenderBuffer>          RaytracingAcceleationStructSRV;
    };

    // This is a platform-independent layer for -immediate- rendering of a single draw call - it's not fully featured or designed
    // for high performance; for additional features there's a provision for API-dependent custom callbacks; for more 
    // performance/reducing overhead the alternative is to provide API-specific rendering module implementations.
    struct vaGraphicsItem   // todo: maybe rename to vaShaderGraphicsItem?
    {
        enum class DrawType : uint8
        {
            DrawSimple,                   // Draw non-indexed, non-instanced primitives.
            // DrawAuto,                       // Draw geometry of an unknown size.
            DrawIndexed,                    // Draw indexed, non-instanced primitives.
            // DrawIndexedInstanced,           // Draw indexed, instanced primitives.
            // DrawIndexedInstancedIndirect,   // Draw indexed, instanced, GPU-generated primitives.
            // DrawInstanced,                  // Draw non-indexed, instanced primitives.
            // DrawInstancedIndirect,          //  Draw instanced, GPU-generated primitives.
        };

        DrawType                            DrawType                = DrawType::DrawSimple;

        // TOPOLOGY
        vaPrimitiveTopology                 Topology                = vaPrimitiveTopology::TriangleList;

        // BLENDING 
        vaBlendMode                         BlendMode               = vaBlendMode::Opaque;
        //vaVector4                           BlendFactor;
        //uint32                              BlendMask;

        // DEPTH
        bool                                DepthEnable             = false;
        bool                                DepthWriteEnable        = false;
        vaComparisonFunc                    DepthFunc               = vaComparisonFunc::Always;

        // STENCIL
        // <to add>

        // FILLMODE
        vaFillMode                          FillMode                = vaFillMode::Solid;

        // CULLMODE
        vaFaceCull                          CullMode                = vaFaceCull::Back;

        // MISC RASTERIZER
        bool                                FrontCounterClockwise   = false;
        // bool                                MultisampleEnable       = true;
        // bool                                ScissorEnable           = true;

        // Check vaRenderDevice::GetCapabilities().VariableShadingRateTier1 for support; if not supported this parameter is just ignored.
        vaShadingRate                       ShadingRate             = vaShadingRate::ShadingRate1X1;    // VRS 1x1 mean just normal shading

        // SHADER(s)
        vaFramePtr<vaVertexShader>          VertexShader;
        vaFramePtr<vaGeometryShader>        GeometryShader;
        vaFramePtr<vaHullShader>            HullShader;
        vaFramePtr<vaDomainShader>          DomainShader;
        vaFramePtr<vaPixelShader>           PixelShader;

        // SAMPLERS
        // not handled by API independent layer yet but default ones set with SetStandardSamplers - see SHADERGLOBAL_SHADOWCMP_SAMPLERSLOT and SHADERGLOBAL_POINTCLAMP_SAMPLERSLOT and others

        // CONSTANT BUFFERS - first one is usually used for the ShaderInstanceConstants and the remaining two are usually unused
        std::array<vaFramePtr<vaConstantBuffer>, 3>
                                            ConstantBuffers;

        // SRVs - there's only 6 since switch to bindless; they're expensive to use for stuff like drawing 10,000 objects but totally fine for postprocess or similar
        std::array<vaFramePtr<vaShaderResource>, 6>
                                            ShaderResourceViews;

        // VERTICES/INDICES
        vaFramePtr<vaShaderResource>        VertexBuffer;
        // uint                                VertexBufferByteStride  = 0;    // 0 means pick up stride from vaVertexBuffer itself
        // uint                                VertexBufferByteOffset  = 0;
        vaFramePtr<vaShaderResource>        IndexBuffer;

        uint32                              InstanceIndex           = 0xFFFFFFFF;   // avoids setting a whole constant buffer to send one parameter; this can be unused or can be used to sample instance constants

        uint32                              GenericRootConst        = 0;            // accessible from shaders as g_genericRootConst

        struct DrawSimpleParams
        {
            uint32                              VertexCount         = 0;    // (DrawSimple only) Number of vertices to draw.
            uint32                              StartVertexLocation = 0;    // (DrawSimple only) Index of the first vertex, which is usually an offset in a vertex buffer.
        }                                   DrawSimpleParams;
        struct DrawIndexedParams
        {
            uint32                              IndexCount          = 0;    // (DrawIndexed only) Number of indices to draw.
            uint32                              StartIndexLocation  = 0;    // (DrawIndexed only) The location of the first index read by the GPU from the index buffer.
            int32                               BaseVertexLocation  = 0;    // (DrawIndexed only) A value added to each index before reading a vertex from the vertex buffer.
        }                                   DrawIndexedParams;
        
        // this is currently disabled for simplicity but can be re-enabled
        // // Callback to insert any API-specific overrides or additional tweaks
        // std::function<bool( const vaGraphicsItem &, vaRenderDeviceContext & )> PreDrawHook;
        // std::function<void( const vaGraphicsItem &, vaRenderDeviceContext & )> PostDrawHook;

        // Helpers
        void                                SetDrawSimple( int vertexCount, int startVertexLocation )                           { this->DrawType = DrawType::DrawSimple; DrawSimpleParams.VertexCount = vertexCount; DrawSimpleParams.StartVertexLocation = startVertexLocation; }
        void                                SetDrawIndexed( uint indexCount, uint startIndexLocation, int baseVertexLocation )  { this->DrawType = DrawType::DrawIndexed; DrawIndexedParams.IndexCount = indexCount; DrawIndexedParams.StartIndexLocation = startIndexLocation; DrawIndexedParams.BaseVertexLocation = baseVertexLocation; }
    };

    struct vaComputeItem
    {
        enum ComputeType
        {
            Dispatch,
            DispatchIndirect,
        };

        ComputeType                         ComputeType             = Dispatch;

        vaFramePtr<vaComputeShader>         ComputeShader;

        // CONSTANT BUFFERS
        std::array<vaFramePtr<vaConstantBuffer>, array_size(vaGraphicsItem::ConstantBuffers)>
                                            ConstantBuffers;                // keep the same count as in for render items for convenience and debugging safety

        // SRVs
        std::array<vaFramePtr<vaShaderResource>, array_size(vaGraphicsItem::ShaderResourceViews)>
                                            ShaderResourceViews;            // keep the same count as in for render items for convenience and debugging safety

        // these do nothing in DX11 (it's implied but can be avoided with custom IHV extensions I think) but in DX12 they
        // add a "commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV( nullptr ) )"
        bool                                GlobalUAVBarrierBefore  = true; // using defaults (true) can obviously be highly inefficient but it's a safe
        bool                                GlobalUAVBarrierAfter   = true; // using defaults (true) can obviously be highly inefficient but it's a safe

        struct DispatchParams
        {
            uint32                              ThreadGroupCountX = 0;
            uint32                              ThreadGroupCountY = 0;
            uint32                              ThreadGroupCountZ = 0;
        }                                   DispatchParams;

        struct DispatchIndirectParams
        {
            vaFramePtr<vaShaderResource>        BufferForArgs;
            uint32                              AlignedOffsetForArgs = 0;
        }                                   DispatchIndirectParams;

        uint32                              GenericRootConst        = 0;    // accessible from shaders as g_genericRootConst

        // Helpers
        void                                SetDispatch( uint32 threadGroupCountX, uint32 threadGroupCountY = 1, uint32 threadGroupCountZ = 1 ) { this->ComputeType = Dispatch; DispatchParams.ThreadGroupCountX = threadGroupCountX; DispatchParams.ThreadGroupCountY = threadGroupCountY; DispatchParams.ThreadGroupCountZ = threadGroupCountZ; }
        void                                SetDispatchIndirect( vaFramePtr<vaShaderResource> bufferForArgs, uint32 alignedOffsetForArgs )      { this->ComputeType = DispatchIndirect; DispatchIndirectParams.BufferForArgs = bufferForArgs; DispatchIndirectParams.AlignedOffsetForArgs = alignedOffsetForArgs; }
    };

    struct vaRaytraceItem
    {
        // In the current implementation, leaving AnyHit and/or ClosestHit undefined ("") uses material's own 
        // shaders via a shader table.
        // There is also support for callable shaders but they are currently hard-coded and not nicely exposed. This will get updated
        // if needed.
        vaFramePtr<vaShaderLibrary>         ShaderLibrary;
        // Shader entry points from the above ShaderLibrary
        string                              RayGen;                         // max length 63 chars
        string                              Miss;                           // max length 63 chars; miss shader index 0
        string                              MissSecondary;                  // max length 63 chars; miss shader index 1 (for ex., visibility rays)
        string                              AnyHit;                         // max length 63 chars; leave at "" for default MaterialAnyHit to be used
        string                              ClosestHit;                     // max length 63 chars; leave at "" for default MaterialClosestHit to be used

        // Shader entry points from material's own library entry points (per-material) which are used if AnyHit and ClosestHit are not defined
        string                              MaterialAnyHit       = "AnyHitAlphaTest";
        string                              MaterialClosestHit;
        
        string                              ShaderEntryMaterialCallable;        // Callable can be useful when for per-material custom shading; if undefined, callable table not created; only 1 per material supported for now
        string                              MaterialMissCallable;    // Miss shader-based API path to allow for callables that support TraceRay; use VA_RAYTRACING_SHADER_MISSCALLABLES_SHADE_OFFSET and null acceleration structure to invoke; see https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#callable-shaders

        // CONSTANT BUFFERS
        std::array<vaFramePtr<vaConstantBuffer>, array_size(vaGraphicsItem::ConstantBuffers)>
                                            ConstantBuffers;                // keep the same count as in for render items for convenience and debugging safety
        // SRVs
        std::array<vaFramePtr<vaShaderResource>, array_size(vaGraphicsItem::ShaderResourceViews)>
                                            ShaderResourceViews;            // keep the same count as in for render items for convenience and debugging safety

        // these do nothing in DX11 (it's implied but can be avoided with custom IHV extensions I think) but in DX12 they
        // add a "commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV( nullptr ) )"
        bool                                GlobalUAVBarrierBefore  = true;     // using defaults (true) can obviously be highly inefficient but it's a safe
        bool                                GlobalUAVBarrierAfter   = true;     // using defaults (true) can obviously be highly inefficient but it's a safe

        uint32                              DispatchWidth           = 0;
        uint32                              DispatchHeight          = 0;
        uint32                              DispatchDepth           = 0;

        uint32                              MaxRecursionDepth       = 1;
        uint32                              MaxPayloadSize          = 0;

        uint32                              GenericRootConst        = 0;            // accessible from shaders as g_genericRootConst

        void                                SetDispatch( uint32 width, uint32 height = 1, uint32 depth = 1 )         { this->DispatchWidth = width, this->DispatchHeight = height; this->DispatchDepth = depth; }
    };

    // Used for more complex rendering when there's camera, lighting, various other settings - not needed by many systems
    struct vaDrawAttributes
    {
        enum class RenderFlags : uint32
        {
            None                                                = 0,
            DebugWireframePass                                  = ( 1 << 0 ),   // consider using vaDrawAttributes::RenderFlags::SetZOffsettedProjMatrix as well
            SetZOffsettedProjMatrix                             = ( 1 << 1 ),
        };

        struct GlobalSettings
        {
            vaVector3                   WorldBase               = vaVector3( 0.0f, 0.0f, 0.0f );    // global world position offset for shading; used to make all shading computation close(r) to (0,0,0) for precision purposes
            vaVector2                   Noise                   = vaVector2( 0.0f, 0.0f );          // additional noise value 
            float                       MIPOffset               = 0.0f;                             // global texture mip offset for those subsystems that support it
            float                       SpecialEmissiveScale    = 1.0f;                             // special emissive is used for materials that directly output point light's brightness if below 'radius'; this feature is not wanted in some cases to avoid duplicating the light emission (such as when drawing into environment maps)
            float                       SpecularAAScale         = 1.0f;                             // specular AA increases roughness based on projected curvature; this can be used to tweak it globally!
            bool                        DisableGI               = false;

            // This will enable collecting cursor hover info (there's a small cost to it). See vaRenderGlobals::DigestCursorHoverInfo / GetCursorHoverInfo 
            bool                        CursorHoverInfoCollect  = true;
            vaVector2i                  CursorViewportPos       = {-1, -1};                         // for ex. = vaInputMouseBase::GetCurrent()->GetCursorClientPosDirect()

                                                                                                    // This will enable collecting generic float arrays - there's a non-trivial cost to enabling it. See vaRenderGlobals::DigestGenericDataCapture / GetLastGenericDataCaptured
            bool                        GenericDataCollect      = false;
        };

        vaDrawAttributes( const vaCameraBase & camera, vaDrawAttributes::RenderFlags renderFlags = vaDrawAttributes::RenderFlags::None, vaSceneLighting * lighting = nullptr, vaSceneRaytracing * raytracing = nullptr, const GlobalSettings & settings = GlobalSettings() )
            : Camera( camera ), RenderFlagsAttrib( renderFlags ), Lighting( lighting ), Raytracing( raytracing ), Settings( settings ) { }

        // Mandatory
        const vaCameraBase &            Camera;                 // Currently selected camera - includes the viewport

                                                                // Optional/settings
        vaDrawAttributes::RenderFlags   RenderFlagsAttrib;

        vaSceneLighting *               Lighting;
        vaSceneRaytracing *             Raytracing;

        //vaVector4                       ViewspaceDepthOffsets   = vaVector4( 0.0f, 0.0f, 0.0f, 0.0f );      // used for depth bias for shadow maps and similar; .xy are flat, .zw are slope based; .xz are absolute (world space) and .zw are scaled by camera-to-vertex distance
        GlobalSettings                  Settings;

        // use these for setting additional buffers; these will further get filled in by vaSceneLighting, vaSceneRaytracing, mesh/material managers and etc. so watch for overlap
        vaShaderItemGlobals             BaseGlobals;
    };

    BITFLAG_ENUM_CLASS_HELPER( vaDrawAttributes::RenderFlags );

    // Used to transfer back everything required for a draw call from vaRenderMaterial but could be used from elsewhere. 
    // Since it can be used multiple times within-frame (but not past the frame, due to vaFramePtr), it can be useful for caching or passing around.
    // Will extend as needed.
    struct vaRenderMaterialData
    {
        vaFramePtr<vaVertexShader>          VertexShader;
        vaFramePtr<vaGeometryShader>        GeometryShader;
        vaFramePtr<vaHullShader>            HullShader;
        vaFramePtr<vaDomainShader>          DomainShader;
        vaFramePtr<vaPixelShader>           PixelShader;

        vaFaceCull                          CullMode = vaFaceCull::Back;

        // these don't get applied to render material but are used elsewhere
        bool                                IsWireframe;
        bool                                IsTransparent;
        bool                                CastShadows;

        void                                Apply( vaGraphicsItem & dstItem/*, uint & dstMaterialGlobalIndex*/ ) const
        {
            dstItem.VertexShader        = VertexShader;
            dstItem.GeometryShader      = GeometryShader;
            dstItem.HullShader          = HullShader;
            dstItem.DomainShader        = DomainShader;
            dstItem.PixelShader         = PixelShader;
            dstItem.CullMode            = CullMode;
        }
    };
    
    class vaShadowmap;
    class vaRenderMeshDrawList;

    // base type for forwarding vaRenderingModule constructor parameters
    struct vaRenderingModuleParams
    {
        vaRenderDevice &        RenderDevice;
        void * const            UserParam0;
        void * const            UserParam1;
        vaRenderingModuleParams( vaRenderDevice & device, void * userParam0 = nullptr, void * userParam1 = nullptr ) : RenderDevice( device ), UserParam0( userParam0 ), UserParam1( userParam1 ) { }
        virtual ~vaRenderingModuleParams( ) { }

        template< typename CastToType >
        CastToType const  &            As( ) const // a.k.a. SafeRefCast
        {
#ifdef _DEBUG
            CastToType const * ret = dynamic_cast<CastToType const *>( this );
            assert( ret != NULL );
            return *ret;
#else
            return *static_cast<const CastToType *>( this );
#endif
        }

        template< typename ParamType >
        shared_ptr<ParamType>   GetParam( ) const { std::dynamic_pointer_cast<ParamType>(Params); }
    };

    class vaRenderingModuleRegistrar : public vaSingletonBase < vaRenderingModuleRegistrar >
    {
        friend class vaRenderDevice;
        friend class vaCore;
    private:
        vaRenderingModuleRegistrar( )   { }
        ~vaRenderingModuleRegistrar( )  { }

        struct ModuleInfo
        {
            std::function< vaRenderingModule * ( const vaRenderingModuleParams & )> 
                                                ModuleCreateFunction;

            explicit ModuleInfo( const std::function< vaRenderingModule * ( const vaRenderingModuleParams & )> & moduleCreateFunction )
                : ModuleCreateFunction( moduleCreateFunction ) { }
        };

        std::map< std::string, ModuleInfo >     m_modules;
        std::recursive_mutex                    m_mutex;

    public:
        static void                             RegisterModule( const std::string & deviceTypeName, const std::string & name, std::function< vaRenderingModule * ( const vaRenderingModuleParams & )> moduleCreateFunction );
        static vaRenderingModule *              CreateModule( const std::string & deviceTypeName, const std::string & name, const vaRenderingModuleParams & params );
        //void                                  CreateModuleArray( int inCount, vaRenderingModule * outArray[] );

        template< typename ModuleType >
        static ModuleType *                     CreateModuleTyped( const std::string & name, const vaRenderingModuleParams & params );

        template< typename ModuleType >
        static ModuleType *                     CreateModuleTyped( const std::string & name, vaRenderDevice & deviceObject, const void * userParam0 );

        template< typename ModuleType >
        static ModuleType *                     CreateModuleTyped( const std::string & name, vaRenderDevice & deviceObject, const void * userParam0, const void * userParam1 );


//    private:
//        static void                             CreateSingletonIfNotCreated( );
//        static void                             DeleteSingleton( );
    };

    class vaRenderingModule : virtual public vaFramePtrTag
    {
        string                              m_renderingModuleTypeName;

    protected:
        friend class vaRenderingModuleRegistrar;
        friend class vaRenderingModuleAPIImplementation;

        vaRenderDevice &                    m_renderDevice;

        // global mutex for when locking on per-vaRenderingModule granularity is enough
        // using low-contention version with 7 instances, should be enough in most cases but also isn't as expensive to unique-lock when needed, as the default 31 one is
        mutable lc_shared_mutex<>           m_mutex;    

    protected:
        vaRenderingModule( const vaRenderingModuleParams & params ) : m_renderDevice( params.RenderDevice )   {  }
                                            
//                                            vaRenderingModule( const char * renderingCounterpartID );
    public:
        virtual                             ~vaRenderingModule( )                                                       { }

    private:
        // called only by vaRenderingModuleRegistrar::CreateModule
        void                                InternalRenderingModuleSetTypeName( const string & name )                   { m_renderingModuleTypeName = name; }

    public:
        const char *                        GetRenderingModuleTypeName( )                                               { return m_renderingModuleTypeName.c_str(); }
        vaRenderDevice &                    GetRenderDevice( )                                                          { return m_renderDevice; }
        vaRenderDevice &                    GetRenderDevice( ) const                                                    { return m_renderDevice; }

        auto &                              Mutex( ) const                                                              { return m_mutex; }

    public:
        template< typename CastToType >
        CastToType                          SafeCast( )
        {
#ifdef _DEBUG
            CastToType ret = dynamic_cast<CastToType>( this );
            assert( ret != NULL );
            return ret;
#else
            return static_cast< CastToType >( this );
#endif
        }

    };

    template< typename DeviceType, typename ModuleType, typename ModuleAPIImplementationType >
    class vaRenderingModuleAutoRegister
    {
    public:
        explicit vaRenderingModuleAutoRegister( )
        {
            vaRenderingModuleRegistrar::RegisterModule( typeid(DeviceType).name(), typeid(ModuleType).name(), &CreateNew );
        }
        ~vaRenderingModuleAutoRegister( ) { }

    private:
        static inline vaRenderingModule *                   CreateNew( const vaRenderingModuleParams & params )
        { 
            return static_cast<vaRenderingModule*>( new ModuleAPIImplementationType( params ) ); 
        }
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Module registration API
    //
    // for APIs/platforms that don't require specialization, just simply register with this
    // VA_RENDERING_MODULE_REGISTER( SkyBox );
#define  VA_RENDERING_MODULE_REGISTER_GENERIC( ModuleType ) \
    vaRenderingModuleAutoRegister< vaRenderDevice, ModuleType, ModuleType > autoReg##ModuleType##_Generic;
    //
    // for APIs/platforms that require specialization, use this, for ex:
    // VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX11, SkyBox, SkyBoxDX11 );
#define VA_RENDERING_MODULE_REGISTER( DeviceType, ModuleType, ModuleAPIImplementationType )                                                    \
        vaRenderingModuleAutoRegister< DeviceType, ModuleType, ModuleAPIImplementationType > autoReg##ModuleType_##ModuleAPIImplementationType;
    //
    // Generic and non-generic can be used together; the factory will first try to find a specialized version and if not found look for
    // the generic one.
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A helper for implementing vaRenderingModule and vaRenderingModuleAPIImplementation 
#define     VA_RENDERING_MODULE_MAKE_FRIENDS( )                                                                               \
    template< typename DeviceType, typename ModuleType, typename ModuleAPIImplementationType > friend class vaRenderingModuleAutoRegister;       \
    friend class vaRenderingModuleRegistrar;                                                                                            

    // A helper used to create rendering module and its counterpart at runtime
    // use example: 
    //      Tree * newTree = VA_RENDERING_MODULE_CREATE( Tree );
    // in addition, parameters can be passed through a pointer to vaRenderingModuleParams object; for example:
    //      vaTextureConstructorParams params( vaCore::GUIDCreate( ) );
    //      vaTexture * texture = VA_RENDERING_MODULE_CREATE( vaTexture, &params );
#define VA_RENDERING_MODULE_CREATE( ModuleType, Param )         vaRenderingModuleRegistrar::CreateModuleTyped<ModuleType>( typeid(ModuleType).name(), Param )
#define VA_RENDERING_MODULE_CREATE_SHARED( ModuleType, Param )  std::shared_ptr< ModuleType >( vaRenderingModuleRegistrar::CreateModuleTyped<ModuleType>( typeid(ModuleType).name(), Param ) )

    template< typename ModuleType >
    inline ModuleType * vaRenderingModuleRegistrar::CreateModuleTyped( const std::string & name, const vaRenderingModuleParams & params )
    {
        ModuleType * ret = NULL;
        vaRenderingModule * createdModule;
        createdModule = CreateModule( typeid(params.RenderDevice).name(), name, params );
        if( createdModule == nullptr )
            createdModule = CreateModule( typeid(vaRenderDevice).name(), name, params );

        ret = dynamic_cast<ModuleType*>( createdModule );
        if( ret == NULL )
        {
            wstring wname = vaStringTools::SimpleWiden( name );
            wstring wtypename = vaStringTools::SimpleWiden( typeid(params.RenderDevice).name() );
            VA_WARN( L"vaRenderingModuleRegistrar::CreateModuleTyped failed for '%s'; have you done VA_RENDERING_MODULE_REGISTER( %s, %s, your_type )? ", wname.c_str(), wtypename.c_str(), wname.c_str() );
        }

        return ret;
    }

    template< typename ModuleType >
    inline ModuleType * vaRenderingModuleRegistrar::CreateModuleTyped( const std::string & name, vaRenderDevice & deviceObject, const void * userParam0 )
    {
        return CreateModuleTyped<ModuleType>( name, vaRenderingModuleParams(deviceObject, userParam0) );
    }

    template< typename ModuleType >
    inline ModuleType * vaRenderingModuleRegistrar::CreateModuleTyped( const std::string & name, vaRenderDevice & deviceObject, const void * userParam0, const void * userParam1 )
    {
        return CreateModuleTyped<ModuleType>( name, vaRenderingModuleParams(deviceObject, userParam0, userParam1) );
    }

    // TODO: upgrade to variadic template when you figure out how :P
    template< typename T >
    class vaAutoRenderingModuleInstance
    {
        shared_ptr<T> const   m_instance;

    public:
        vaAutoRenderingModuleInstance( const vaRenderingModuleParams & params ) : m_instance( shared_ptr<T>( vaRenderingModuleRegistrar::CreateModuleTyped<T>( typeid(T).name(), params ) ) ) { }
        vaAutoRenderingModuleInstance( vaRenderDevice & device )                : m_instance( shared_ptr<T>( vaRenderingModuleRegistrar::CreateModuleTyped<T>( typeid(T).name(), device ) ) ) { }
        ~vaAutoRenderingModuleInstance()    { }

        T & operator*( ) const
        {	// return reference to object
            assert( m_instance != nullptr ); return ( *m_instance );
        }

        const shared_ptr<T> & operator->( ) const
        {	// return pointer to class object
            assert( m_instance != nullptr ); return m_instance;
        }

        const shared_ptr<T> & get( ) const
        {	// return pointer to class object
            assert( m_instance != nullptr ); return m_instance;
        }

        operator const shared_ptr<T> & ( ) const
        {	// return pointer to class object
            assert( m_instance != nullptr ); return m_instance;
        }

        operator vaFramePtr<T> ( ) const
        {	// return pointer to class object
            assert( m_instance != nullptr ); return m_instance;
        }

        void destroy( )
        {
            m_instance = nullptr;
        }
    };

    template< class _Ty >
    using vaAutoRMI = vaAutoRenderingModuleInstance< _Ty >;

    // these should go into a separate header (vaAsset.h?)
    struct vaAsset;
    enum class vaAssetType : int32
    {
        Texture,
        RenderMesh,
        RenderMaterial,

        MaxVal
    };
    class vaAssetResource : public vaUIDObject
    {
    private:
        vaAsset *           m_parentAsset;

        int64               m_UI_ShowSelectedAppTickIndex   = -1;
    
    protected:
        vaAssetResource( const vaGUID & uid ) : vaUIDObject( uid )                  { m_parentAsset = nullptr; }
        virtual ~vaAssetResource( )                                                 { assert( m_parentAsset == nullptr ); }
    
    public:
        vaAsset *           GetParentAsset( )                                       { return m_parentAsset; }
        const vaAsset *     GetParentAsset( ) const                                 { return m_parentAsset; }
        template< typename AssetResourceType >
        AssetResourceType*  GetParentAsset( )                                       { return dynamic_cast<AssetResourceType*>( m_parentAsset ); }

        virtual vaAssetType GetAssetType( ) const                                   = 0;

        virtual bool        LoadAPACK( vaStream & inStream )                               = 0;
        virtual bool        SaveAPACK( vaStream & outStream )                                                       = 0;
        virtual bool        SerializeUnpacked( vaXMLSerializer & serializer, const string & assetFolder )          = 0;

        // just for UI display
        int64               GetUIShowSelectedAppTickIndex( ) const                  { return m_UI_ShowSelectedAppTickIndex; }
        void                SetUIShowSelectedAppTickIndex( int64 showSelectedFrame ){ m_UI_ShowSelectedAppTickIndex = showSelectedFrame; }

        virtual bool        UIPropertiesDraw( vaApplicationBase & application )     = 0;

    protected:
        friend struct vaAsset;
        virtual void        SetParentAsset( vaAsset * asset )         
        { 
            // there can be only one asset resource linked to one asset; this is one (of the) way to verify it:
            if( asset == nullptr )
            {
                assert( m_parentAsset != nullptr );
            }
            else
            {
                assert( m_parentAsset == nullptr );
            }
            m_parentAsset = asset; 
        }

    public:
        virtual void        RegisterUsedAssetPacks( std::function<void( const vaAssetPack & )> registerFunction );
    };

    inline vaVector2 vaShadingRateToVector2( vaShadingRate shadingRate )
    {
        vaVector2 ShadingRate = { 0,0 };
        switch( shadingRate )
        {
        case vaShadingRate::ShadingRate1X1: ShadingRate.x = 1; ShadingRate.y = 1;   break;
        case vaShadingRate::ShadingRate1X2: ShadingRate.x = 1; ShadingRate.y = 2;   break;
        case vaShadingRate::ShadingRate2X1: ShadingRate.x = 2; ShadingRate.y = 1;   break;
        case vaShadingRate::ShadingRate2X2: ShadingRate.x = 2; ShadingRate.y = 2;   break;
        case vaShadingRate::ShadingRate2X4: ShadingRate.x = 2; ShadingRate.y = 4;   break;
        case vaShadingRate::ShadingRate4X2: ShadingRate.x = 4; ShadingRate.y = 2;   break;
        case vaShadingRate::ShadingRate4X4: ShadingRate.x = 4; ShadingRate.y = 4;   break;
        default: assert( false ); break;
        }
        return ShadingRate;
    }

    inline string vaStandardSamplerTypeToShaderName( vaStandardSamplerType samplerType )
    {
        switch( samplerType )
        {
        case vaStandardSamplerType::PointClamp:         return "g_samplerPointClamp";        break;
        case vaStandardSamplerType::PointWrap:          return "g_samplerPointWrap";         break;
        case vaStandardSamplerType::LinearClamp:        return "g_samplerLinearClamp";       break;
        case vaStandardSamplerType::LinearWrap:         return "g_samplerLinearWrap";        break;
        case vaStandardSamplerType::AnisotropicClamp:   return "g_samplerAnisotropicClamp";  break;
        case vaStandardSamplerType::AnisotropicWrap:    return "g_samplerAnisotropicWrap";   break;
        default: assert( false ); return "g_samplerPointClamp"; break;
        }
    }

    inline string vaStandardSamplerTypeToUIName( vaStandardSamplerType samplerType )
    {
        switch( samplerType )
        {
        case vaStandardSamplerType::PointClamp:         return "PointClamp";        break;
        case vaStandardSamplerType::PointWrap:          return "PointWrap";         break;
        case vaStandardSamplerType::LinearClamp:        return "LinearClamp";       break;
        case vaStandardSamplerType::LinearWrap:         return "LinearWrap";        break;
        case vaStandardSamplerType::AnisotropicClamp:   return "AnisotropicClamp";  break;
        case vaStandardSamplerType::AnisotropicWrap:    return "AnisotropicWrap";   break;
        default: assert( false ); return "error"; break;
        }
    }

    inline string vaLayerModeToUIName( vaLayerMode value )
    {
        switch( value )
        {
        case vaLayerMode::Opaque:           return "Opaque";        break;
        case vaLayerMode::AlphaTest:        return "AlphaTest";     break;
        case vaLayerMode::Decal:            return "Decal";         break;
        case vaLayerMode::Transparent:      return "Transparent";   break;
        default: assert( false ); return "error"; break;
        }
    }

    inline string vaDrawResultFlagsUIName( vaDrawResultFlags value )
    {
        if( value == vaDrawResultFlags::None )
            return "None";
        string out = "";
        if( (value & vaDrawResultFlags::UnspecifiedError            ) != 0 )
            out += string( ( out.size( ) == 0 ) ? ( "" ) : ( ", " ) ) + "UnspecifiedError";
        if( (value & vaDrawResultFlags::ShadersStillCompiling       ) != 0 )
            out += string( ( out.size( ) == 0 ) ? ( "" ) : ( ", " ) ) + "ShadersStillCompiling";
        if( (value & vaDrawResultFlags::AssetsStillLoading          ) != 0 )
            out += string( ( out.size( ) == 0 ) ? ( "" ) : ( ", " ) ) + "AssetsStillLoading";
        if( (value & vaDrawResultFlags::PendingVisualDependencies   ) != 0 )
            out += string( ( out.size( ) == 0 ) ? ( "" ) : ( ", " ) ) + "PendingVisualDependencies";
        
        return out;
    }

    
}
