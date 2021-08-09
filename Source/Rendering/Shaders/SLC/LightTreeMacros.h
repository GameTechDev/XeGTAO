#pragma once

#ifndef __cplusplus
#define HLSL
#endif

#define CPU_BUILDER

#ifdef CPU_BUILDER
//#define LIGHT_CONE
#endif

//#define GROUND_TRUTH

//#define EXPLORE_DISTANCE_TYPE

#define MAX_CUT_NODES 32

#ifndef HLSL
#include "CPUMath.h"
#pragma warning( push )
#pragma warning( disable: 4201 4310 )
#include "glm/ext.hpp"
#pragma warning( pop )

#define float2 glm::vec2
#define float3 glm::vec3
#define float4 glm::vec4
#define uint3 glm::uvec3
#define uint4 glm::uvec4
#define int4 glm::ivec4
#define float3x3 glm::mat3
#define uint unsigned int
#endif

// 48 bytes
struct Node
{
	float3 boundMin;
	float intensity;
	float3 boundMax;
	int ID;				// this is left child; right child is ID+1
#ifdef LIGHT_CONE
	float4 cone; //xyz cone axis, w cone angle
#endif
};

struct VizNode
{
	float3 boundMin;
	float3 boundMax;
	int level;
	int index;
};

struct BLASInstanceHeader
{
	float3x3 rotation;
	float3 translation;
	float scaling;
	float3 emission;
	int nodeOffset;
	int numTreeLevels; //not used
	int numTreeLeafs;
	int emitTexId; // -1 -> no texture
	int BLASId;
};

struct EmissiveVertex
{
	float3 position;
	float3 normal;
	float2 texCoord;
};

struct MeshLightInstancePrimtive
{
	int indexOffset; // indexoffset in the indinces buffer of mesh lights
	int instanceId; // points to BLASInstanceHeader
};

inline float GetColorIntensity(float3 color)
{
	return color.r + color.g + color.b;
}

inline float4 MergeCones(float4 cone1, float4 cone2)
{
	float4 ret;
#ifdef HLSL
	float3 axis_a = 0;
	float3 axis_b = 0;
#else
	float3 axis_a = float3(0);
	float3 axis_b = float3(0);
#endif

	float angle_a = 0;
	float angle_b = 0;

	if (cone1.w >= cone2.w)
	{
#ifdef HLSL
		axis_a = cone1.xyz;
		axis_b = cone2.xyz;
#else
		axis_a.x = cone1.x;
		axis_a.y = cone1.y;
		axis_a.z = cone1.z;
		axis_b.x = cone2.x;
		axis_b.y = cone2.y;
		axis_b.z = cone2.z;
#endif
		angle_a = cone1.w;
		angle_b = cone2.w;
	}
	else
	{
#ifdef HLSL
		axis_a = cone2.xyz;
		axis_b = cone1.xyz;
#else
		axis_a.x = cone2.x;
		axis_a.y = cone2.y;
		axis_a.z = cone2.z;
		axis_b.x = cone1.x;
		axis_b.y = cone1.y;
		axis_b.z = cone1.z;
#endif
		angle_a = cone2.w;
		angle_b = cone1.w;
	}

#ifndef HLSL
	using namespace std;
#endif
	float cosGamma = max(-1.f, min(1.f, dot(axis_a, axis_b)));
	float gamma = acos(cosGamma);

	if (cosGamma > 0.9999)
	{
#ifdef HLSL
		ret.xyz = axis_a;
#else
		ret.x = axis_a.x;
		ret.y = axis_a.y;
		ret.z = axis_a.z;
#endif
		ret.w = min(angle_a/* + gamma*/, PI);
		return ret;
	}

	if (cosGamma < -0.9999)
	{
#ifdef HLSL
		ret.xyz = axis_a;
#else
		ret.x = axis_a.x;
		ret.y = axis_a.y;
		ret.z = axis_a.z;
#endif
		ret.w = PI;
		return ret;
	}

	if (min(gamma + angle_b, PI) <= angle_a)
	{
#ifdef HLSL
		ret.xyz = axis_a;
#else
		ret.x = axis_a.x;
		ret.y = axis_a.y;
		ret.z = axis_a.z;
#endif
		ret.w = angle_a;
		return ret;
	}

	ret.w = (angle_a + angle_b + gamma) / 2;

	if (ret.w >= PI)
	{
#ifdef HLSL
		ret.xyz = axis_a;
#else
		ret.x = axis_a.x;
		ret.y = axis_a.y;
		ret.z = axis_a.z;
#endif
		ret.w = PI;
		return ret;
	}

	float rot = ret.w - angle_a;

	// slerp(axis_a, axis_b, rot / gamma);
	float t = rot / gamma;

#ifdef HLSL
	ret.xyz = (sin((1 - t)*gamma)*axis_a + sin(t*gamma)*axis_b) / sin(gamma);
#else
	float3 axis = slerp(axis_a, axis_b, t);
	ret.x = axis.x;
	ret.y = axis.y;
	ret.z = axis.z;
#endif
	// need normalization?
	return ret;
}

#ifndef HLSL
inline float OrientationMeasure(float4 cone)
{
	float theta_w = std::min(cone.w + PI / 2, PI);
	return 2 * PI*(1 - cos(cone.w)) + PI / 2 * (2 * theta_w*sin(cone.w) - cos(cone.w - 2 * theta_w) - 2 * cone.w*sin(cone.w) + cos(cone.w));
}
#endif

#ifndef HLSL
#undef float2
#undef float3
#undef float4
#undef uint3
#undef uint4
#undef int4
#undef float3x3
#endif