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

#include "Rendering/DirectX/vaDirectXIncludes.h"

#include "Rendering/vaPrimitiveShapeRenderer.h"

#include "Rendering/DirectX/vaRenderDeviceDX11.h"
#include "Rendering/DirectX/vaRenderDeviceDX12.h"

namespace Vanilla
{

    class vaPrimitiveShapeRendererDX11 : public vaPrimitiveShapeRenderer
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );

    private:

    protected:
        explicit vaPrimitiveShapeRendererDX11( const vaRenderingModuleParams & params ) : vaPrimitiveShapeRenderer( params ) { }
        ~vaPrimitiveShapeRendererDX11( ) { }

    public:
    };

    class vaPrimitiveShapeRendererDX12 : public vaPrimitiveShapeRenderer
    {
        VA_RENDERING_MODULE_MAKE_FRIENDS( );

    private:

    protected:
        explicit vaPrimitiveShapeRendererDX12( const vaRenderingModuleParams & params ) : vaPrimitiveShapeRenderer( params ) { }
        ~vaPrimitiveShapeRendererDX12( ) { }

    public:
    };
}

using namespace Vanilla;

void RegisterPrimitiveShapeRendererDX11( )
{
    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX11, vaPrimitiveShapeRenderer, vaPrimitiveShapeRendererDX11 );
}

void RegisterPrimitiveShapeRendererDX12( )
{
    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX12, vaPrimitiveShapeRenderer, vaPrimitiveShapeRendererDX12 );
}

