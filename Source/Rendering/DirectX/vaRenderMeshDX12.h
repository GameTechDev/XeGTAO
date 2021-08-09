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

#include "Rendering/DirectX/vaRenderDeviceDX12.h"

namespace Vanilla
{
    class vaRenderMeshDX12 : public vaRenderMesh
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );

    protected:
        D3D12_RAYTRACING_GEOMETRY_DESC                          m_RT_desc;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC      m_RT_BLASBuildDesc;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO   m_RT_prebuildInfo;
        shared_ptr<vaRenderBuffer>                              m_RT_BLASData;
        bool                                                    m_RT_BLASDataDirty = true;

    protected:
        vaRenderMeshDX12( const vaRenderingModuleParams & params ) : vaRenderMesh( params ) { }
        ~vaRenderMeshDX12( ) { }

    private:
        virtual void                            UpdateGPURTData( vaRenderDeviceContext & renderContext ) override;
        virtual void                            SetParentAsset( vaAsset * asset ) override;

    public:
        D3D12_RAYTRACING_GEOMETRY_DESC &                        RT_Desc( )                          { return m_RT_desc;         }
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC &    RT_BLASBuildDesc( )                 { return m_RT_BLASBuildDesc;    }
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO & RT_PrebuildInfo( )                  { return m_RT_prebuildInfo; }
        void                                                    RT_CreateBLASDataIfNeeded( );
        const shared_ptr<vaRenderBuffer> &                      RT_BLASData( )                      { return m_RT_BLASData;     }
        bool                                                    RT_BLASDataDirty( ) const           { return m_RT_BLASDataDirty; }
        void                                                    RT_SetBLASDataDirty( bool dirty )   { m_RT_BLASDataDirty = dirty; }
    };

    class vaRenderMeshManagerDX12 : public vaRenderMeshManager
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );

    private:

    protected:
        vaRenderMeshManagerDX12( const vaRenderingModuleParams & params ) : vaRenderMeshManager( params ) { }
        ~vaRenderMeshManagerDX12( ) { }

    private:
//        virtual vaDrawResultFlags       Draw( vaDrawAttributes & drawAttributes, const vaRenderMeshDrawList & list, vaBlendMode blendMode, vaRenderMeshDrawFlags drawFlags,
//                                                                std::function< void( const vaRenderMeshDrawList::Entry & entry, const vaRenderMaterial & material, vaGraphicsItem & renderItem ) > globalCustomizer = nullptr ) override;

    public:
    };

    inline vaRenderMeshDX12 &  AsDX12( vaRenderMesh & resource )   { return *resource.SafeCast<vaRenderMeshDX12*>(); }
    inline vaRenderMeshDX12 *  AsDX12( vaRenderMesh * resource )   { return resource->SafeCast<vaRenderMeshDX12*>(); }
}

