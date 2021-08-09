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
#include "Core/vaGeometry.h"
#include "IntegratedExternals/vaEnTTIntegration.h"

namespace Vanilla
{
    class vaRenderMesh;
    class vaRenderMaterial;

    namespace Scene
    {
        struct WorldBounds;
    }

    // beware, the callback gets called from vaScene's worker threads (specifically, 'selections' stage)
    typedef std::function< bool( const entt::entity entity, const vaMatrix4x4 & worldTransform, const Scene::WorldBounds & worldBounds, const vaRenderMesh & mesh, const vaRenderMaterial & material, int & outBaseShadingRate ) >
        vaSceneSelectionFilterType;

}
