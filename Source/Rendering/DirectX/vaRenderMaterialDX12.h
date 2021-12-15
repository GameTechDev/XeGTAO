///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Rendering/vaRenderMaterial.h"
#include "Rendering/vaRenderMesh.h"

#include "Rendering/Shaders/vaRaytracingShared.h"

#include "Rendering/DirectX/vaRenderDeviceDX12.h"

namespace Vanilla
{
    class vaRenderMaterialDX12 : public vaRenderMaterial
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );
    
    protected:
    
    protected:
        vaRenderMaterialDX12( const vaRenderingModuleParams & params ) : vaRenderMaterial( params ) { }
        ~vaRenderMaterialDX12( ) { }
    
    protected:
        friend class vaRenderMaterialManagerDX12;
        //void                            SetCallableShaderTableIndex( int index )    { m_shaderTableIndex = index; }
    };

    class vaRenderMaterialManagerDX12 : public vaRenderMaterialManager
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );

    public:
        // Per-material data used to build the callable shader table for raytracing (and also to check whether it needs a re-build!)
        // (This includes 'anyhit' shaders too now)
        struct CallableShaders // TODO: rename to raytrace shaders?
        {
            static constexpr int                CallablesPerMaterial            = VA_RAYTRACING_SHADER_CALLABLES_PERMATERIAL;    // this does not include AnyHit, intersection or any other non-callables

            vaGUID                              MaterialID                      = vaGUID::Null;
            vaFramePtr<vaShaderDataDX12>        LibraryBlob                     = nullptr;
            int64                               LibraryUniqueContentsID         = -1;
            wstring                             UniqueIDString                  = L"";  // same as vaRenderMaterialCachedShaders::UniqueIDString

            // called after use to ensure no leftovers from previous frame that are not guaranteed to survive
            void Reset( )
            {
                LibraryBlob                  = nullptr;
            }
        };

    private:
        // These currently handle all per-material stuff - if a single material changes, it needs a rebuild
        // shared_ptr<vaRenderBuffer>              m_callableShaderTable;
        // Each time the m_callableShaderTable gets rebuilt, the unique contents ID is incremented
        int64                           m_callableShaderTableUniqueContentsID   = -1;
        int64                           m_callableShaderTableLastFrameIndex     = -1;

        // valid only between PreRenderUpdateInternal and PostRenderCleanup
        std::vector<CallableShaders>    m_globalCallablesTable;     // this is per-material, with duplicates
        std::vector<CallableShaders>    m_uniqueCallablesTable;     // these are unique, addressed into with 


    private:
        shared_ptr<void> const              m_aliveToken = std::make_shared<int>( 42 );    // this is only used to track object lifetime for callbacks and etc.

    protected:
        vaRenderMaterialManagerDX12( const vaRenderingModuleParams & params );
        ~vaRenderMaterialManagerDX12( ) { }

    private:
//        virtual vaDrawResultFlags       Draw( vaDrawAttributes & drawAttributes, const vaRenderMeshDrawList & list, vaBlendMode blendMode, vaRenderMeshDrawFlags drawFlags,
//                                                                std::function< void( const vaRenderMeshDrawList::Entry & entry, const vaRenderMaterial & material, vaGraphicsItem & renderItem ) > globalCustomizer = nullptr ) override;
        virtual void                    UpdateAndSetToGlobals( vaRenderDeviceContext & renderContext, vaShaderItemGlobals & shaderItemGlobals, const vaDrawAttributes * drawAttributes ) override;
        void                            EndFrameCleanup( );

    private:
        friend struct vaRaytracePSODescDX12;
        friend class vaRaytracePSODX12;
        friend class vaRenderDeviceContextBaseDX12;
        const std::vector<vaRenderMaterialManagerDX12::CallableShaders> & 
                                        GetUniqueCallablesTable( ) const        { return m_uniqueCallablesTable; }
        //const std::unordered_map<vaFramePtr<vaShaderDataDX12>, uint32> &
        //                                GetUniqueCallableLibraries( ) const     { return m_uniqueCallableLibraries; }
        int64                           GetCallablesTableID( ) const            { return m_callableShaderTableUniqueContentsID; }

    public:
    };

    inline vaRenderMaterialDX12 & AsDX12( vaRenderMaterial & resource )   { return *resource.SafeCast<vaRenderMaterialDX12*>(); }
    inline vaRenderMaterialDX12 * AsDX12( vaRenderMaterial * resource )   { return resource->SafeCast<vaRenderMaterialDX12*>(); }

    inline vaRenderMaterialManagerDX12 &  AsDX12( vaRenderMaterialManager & resource )   { return *resource.SafeCast<vaRenderMaterialManagerDX12*>(); }
    inline vaRenderMaterialManagerDX12 *  AsDX12( vaRenderMaterialManager * resource )   { return resource->SafeCast<vaRenderMaterialManagerDX12*>(); }
}

