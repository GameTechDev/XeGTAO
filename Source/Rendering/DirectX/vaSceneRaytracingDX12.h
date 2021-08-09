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

#include "Rendering/vaSceneRaytracing.h"

#include "Rendering/DirectX/vaRenderDeviceDX12.h"
#include "Rendering/DirectX/vaRenderDeviceContextDX12.h"

#include "Rendering/DirectX/vaRenderBuffersDX12.h"

namespace Vanilla
{
    class vaSceneRenderer;

    class vaSceneRaytracingDX12 : public vaSceneRaytracing
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );

    public:

    private:

        std::vector<D3D12_RAYTRACING_INSTANCE_DESC>
                                                m_instanceDescsDX12CPU;

        // we've got to multi-buffer these because the older ones must be kept alive until they finish rendering
        shared_ptr<vaUploadBufferDX12>          m_instanceDescsDX12GPU[vaRenderDevice::c_BackbufferCount];

        // shared_ptr<vaShaderLibrary>             m_testLS;
        // 
        // // Root signatures
        // // ComPtr<ID3D12RootSignature>         m_raytracingLocalRootSignature;
        // 
        // // State object
        // ComPtr<ID3D12StateObject>               m_dxrStateObject;
        // 
        // // Shader tables
        // shared_ptr<vaRenderBuffer>              m_missShaderTable;
        // shared_ptr<vaRenderBuffer>              m_hitGroupShaderTable;
        // shared_ptr<vaRenderBuffer>              m_rayGenShaderTable;


    protected:
        vaSceneRaytracingDX12( const vaRenderingModuleParams & params );
    public:
        ~vaSceneRaytracingDX12( );

    protected:
        //virtual void                        DoSomething( vaRenderDeviceContext & renderContextconst shared_ptr<vaTexture> & outResults, const vaDrawAttributes & drawAttributes ) override;

    protected:
        friend class vaRenderDeviceContextBaseDX12;

        virtual void                            PreRenderUpdateInternal( vaRenderDeviceContext & context, const std::unordered_set<vaFramePtr<vaRenderMesh>> & meshes, const std::unordered_set<vaFramePtr<vaRenderMaterial>> & materials ) override;
        virtual void                            PostRenderCleanupInternal( ) override;
    };

}
