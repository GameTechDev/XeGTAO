///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Core/vaCoreIncludes.h"

#include "Rendering/vaSceneLighting.h"

#include "Rendering/vaRenderMesh.h"
#include "Rendering/vaRenderMaterial.h"

//#include "Rendering/DirectX/vaDirectXIncludes.h"

#include "Rendering/DirectX/vaRenderDeviceContextDX12.h"
// #include "Rendering/DirectX/vaTextureDX11.h"
// #include "Rendering/DirectX/vaRenderBuffersDX11.h"

using namespace Vanilla;

namespace Vanilla
{
    class vaSceneLightingDX12 : public vaSceneLighting
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );

    private:

    protected:
        explicit vaSceneLightingDX12( const vaRenderingModuleParams & params );
        ~vaSceneLightingDX12( );

        //virtual void                            ApplyDirectionalAmbientLighting( vaDrawAttributes & drawAttributes, vaGBuffer & GBuffer ) override   { drawContext; GBuffer; assert( false ); }
        //virtual void                            ApplyDynamicLighting( vaDrawAttributes & drawAttributes, vaGBuffer & GBuffer ) override              { drawContext; GBuffer; assert( false ); }
        //virtual void                            ApplyTonemap( vaDrawAttributes & drawAttributes, vaGBuffer & GBuffer );

    public:
    };
}

vaSceneLightingDX12::vaSceneLightingDX12( const vaRenderingModuleParams & params ) : vaSceneLighting( params )
{
    params; // unreferenced
}

vaSceneLightingDX12::~vaSceneLightingDX12( )
{
}

void RegisterLightingDX12( )
{
    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX12, vaSceneLighting, vaSceneLightingDX12 );
}
