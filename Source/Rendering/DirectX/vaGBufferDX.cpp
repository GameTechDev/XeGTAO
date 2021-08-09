///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if 0 // obsolete, left as placeholder

#include "Core/vaCoreIncludes.h"

#include "Rendering/vaGBuffer.h"

#include "Rendering/DirectX/vaRenderDeviceDX11.h"
#include "Rendering/DirectX/vaRenderDeviceDX12.h"


namespace Vanilla
{

    class vaGBufferDX11 : public vaGBuffer
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );

    private:

    protected:
        explicit vaGBufferDX11( const vaRenderingModuleParams & params ) : vaGBuffer( params.RenderDevice ) { }
        ~vaGBufferDX11( ) { }

    public:
    };

    class vaGBufferDX12 : public vaGBuffer
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );

    private:

    protected:
        explicit vaGBufferDX12( const vaRenderingModuleParams & params ) : vaGBuffer( params.RenderDevice ) { }
        ~vaGBufferDX12( ) { }

    public:
    };

}

using namespace Vanilla;

#endif // #if 0 // obsolete, left as placeholder

void RegisterGBufferDX11( )
{
// obsolete, left as placeholder
//    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX11, vaGBuffer, vaGBufferDX11 );
}

void RegisterGBufferDX12( )
{
// obsolete, left as placeholder
//    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX12, vaGBuffer, vaGBufferDX12 );
}
