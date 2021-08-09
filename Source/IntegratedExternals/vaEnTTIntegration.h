#pragma once

// see https://github.com/skypjack/entt/issues/121
// #pragma warning(push)
// #pragma warning(disable:4307)

#include "IntegratedExternals/entt/entity/registry.hpp"

// #pragma warning(pop)




// see https://github.com/skypjack/entt/issues/121
#if defined(_MSC_VER)
#define HashedString(x) \
		__pragma(warning(push)) \
		__pragma(warning(disable:4307)) \
		entt::hashed_string{ #x } \
		__pragma(warning(pop))
#else
#define HashedString(x) entt::hashed_string{ #x }
#endif

namespace Vanilla
{
	// TODO: get rid of this, rework use cases
	inline uint32_t entity_to_index( entt::entity entity )			{ return entt::to_integral(entity) & entt::entt_traits<entt::id_type>::entity_mask; }
}

namespace entt
{
    //class
}

