///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2020, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// This is a cut (heh) taken out of SLCRayGen.hlsl and other files from the RealTimeStochasticLightcuts codebase!

#include "../vaShared.hlsl"

#include "LightTreeMacros.h"

#include "RandomGenerator.hlsli"

#define SLC_MAX_LIGHT_SAMPLES 32		// not sure why but it doesn't seem like more than 32 works

//StructuredBuffer<BLASInstanceHeader> g_BLASHeaders : register(t3);
//StructuredBuffer<Node> TLAS : register(t4);
StructuredBuffer<Node> g_SLC_BLAS                       : register( T_CONCATENATER( LIGHTINGGLOBAL_SLC_BLAS_SLOT_V ) );

struct OneLevelLightHeapSimpleData
{
	int NodeID;
	float error;
};

void Swap(inout int first, inout int second)
{
	int temp = first;
	first = second;
	second = temp;
}

inline float MaxDistAlong(float3 p, float3 dir, float3 boundMin, float3 boundMax)
{
	float3 dir_p = dir * p;
	float3 mx0 = dir * boundMin - dir_p;
	float3 mx1 = dir * boundMax - dir_p;
	return max(mx0[0], mx1[0]) + max(mx0[1], mx1[1]) + max(mx0[2], mx1[2]);
}

inline float GeomTermBound(float3 p, float3 N, float3 boundMin, float3 boundMax)
{
	float nrm_max = MaxDistAlong(p, N, boundMin, boundMax);
	if (nrm_max <= 0) return 0.0f;
	float3 d = min(max(p, boundMin), boundMax) - p;
	float3 tng = d - dot(d, N) * N;
	float hyp2 = dot(tng, tng) + nrm_max * nrm_max;
	return nrm_max * rsqrt(hyp2);
}

inline float SquaredDistanceToClosestPoint(float3 p, float3 boundMin, float3 boundMax)
{
	float3 d = min(max(p, boundMin), boundMax) - p;
	return dot(d, d);
}

inline float errorFunction(int nodeID, int BLASId, float3 p, float3 N, float3 V,
//	StructuredBuffer<Node> TLAS,
	StructuredBuffer<Node> BLAS,
//	StructuredBuffer<BLASInstanceHeader> g_BLASHeaders, 
	int TLASLeafStartIndex, bool debug
)
{
	// bool IsBLASInTwoLevelTree = false;

	Node node;
	int c0 = nodeID < 0 ? BLASId : nodeID;

//	if (nodeID < 0) // for one level tree
	{
#ifndef CPU_BUILDER
		if (BLASId >= TLASLeafStartIndex)
		{
			return 0;
		}
#endif
		node = BLAS[BLASId];
#ifdef CPU_BUILDER
		if (node.ID >= TLASLeafStartIndex)
		{
			//DebugAssert( false, node.ID, BLASId );
			return 0;
		}
#endif

	}
//	else
//	{
//		if (BLASId >= 0)
//		{
//			IsBLASInTwoLevelTree = true;
//			BLASInstanceHeader header = g_BLASHeaders[BLASId];
//
//#ifndef CPU_BUILDER
//			if (nodeID >= header.numTreeLeafs)
//			{
//				return 0;
//			}
//#endif
//			node = BLAS[header.nodeOffset + nodeID];
//#ifdef CPU_BUILDER
//			if (node.ID >= 2 * header.numTreeLeafs)
//			{
//				return 0;
//			}
//#endif
//
//		}
//		else
//		{
//			node = TLAS[nodeID];
//		}
//	}

	float dlen2 = SquaredDistanceToClosestPoint(p, node.boundMin, node.boundMax);
	float SR2 = length(node.boundMax-node.boundMin) * 0.1; //g_lighting.SLC_ErrorLimit * g_lighting.SLC_SceneRadius;
	
	SR2 *= SR2;
	if (dlen2 < SR2) dlen2 = SR2; // bound the distance

	float atten = rcp(dlen2);

	float geomTerm = GeomTermBound(p, N, node.boundMin, node.boundMax);
	atten *= geomTerm;

#ifdef LIGHT_CONE
	{
		float3 nr_boundMin = 2 * p - node.boundMax;
		float3 nr_boundMax = 2 * p - node.boundMin;
		float cos0 = GeomTermBound(p, node.cone.xyz, nr_boundMin, nr_boundMax);
		atten *= max(0.f, cos(max(0.f, acos(cos0) - node.cone.w)));
	}
#endif

	float colorIntens = node.intensity;

	float res = atten * colorIntens;

	//if( debug )
	//{
	//	uint dbgCounter = DebugCounter();
	//	// DebugDraw2DText( float2( 100, 100 + dbgCounter * 20 ), float4( 1, 0.5, 0.5, 1 ), dbgCounter );
	//	// DebugDraw2DText( float2( 120, 100 + dbgCounter * 20 ), float4( 0.5, 1, 1.0, 1 ), float4( BLASId, dlen2, geomTerm, colorIntens ) );
	//
	//	DebugDraw3DBox( node.boundMin, node.boundMax, float4( GradientRainbow( dbgCounter/16.0 ), dbgCounter/16.0 ) );
	//}

	return res;
};

void SLCSelectCut( float3 worldPos, float3 worldNormal, float3 viewVec, out int lightcutNodes[SLC_MAX_LIGHT_SAMPLES], out int numLights, const int maxLightSamples, bool debug )
{
	float3 p = worldPos;
	float3 N = worldNormal;
	float3 V = viewVec;

	//if( debug )
	//	DebugText( float4( DebugCounter(), V ) );

	numLights = 1;
	OneLevelLightHeapSimpleData heap[MAX_CUT_NODES + 1];
	for( int i = 0; i < MAX_CUT_NODES + 1; i++ )
		heap[i].NodeID = -1;
	heap[1].NodeID = 1;
	heap[1].error = 1e27;
	int maxId = 1;
	;
	lightcutNodes[0] = 1;
	while (numLights < maxLightSamples)
	{
		int id = maxId;
		int NodeID = heap[id].NodeID;

#ifdef CPU_BUILDER
		int pChild = g_SLC_BLAS[NodeID].ID;
#else
		int pChild = NodeID << 1;
#endif
		int sChild = pChild + 1;

		lightcutNodes[id - 1] = pChild;
		heap[id].NodeID = pChild;
		heap[id].error = errorFunction( -1, pChild, p, N, V, g_SLC_BLAS, g_lighting.SLC_TLASLeafStartIndex, debug );

		//if( debug )
		//	DebugText( float4( DebugCounter(), id, NodeID, g_lighting.SLC_TLASLeafStartIndex ) );

		// if( debug )
		// {
		// 	uint dbgCounter = DebugCounter();
		// 	DebugDraw2DText( float2( 100, 100 + dbgCounter * 20 ), float4( 1, 0.5, 0.5, 1 ), dbgCounter );
		// 	DebugDraw2DText( float2( 120, 100 + dbgCounter * 20 ), float4( 0.5, 1, 1.0, 1 ), float4( heap[0].NodeID, heap[1].NodeID, heap[2].NodeID, heap[3].NodeID ) );
		// 	DebugDraw2DText( float2( 520, 100 + dbgCounter * 20 ), float4( 0.5, 1, 1.0, 1 ), float4( heap[4].NodeID, heap[5].NodeID, heap[6].NodeID, heap[7].NodeID ) );
		// }

		// check bogus light
		if (g_SLC_BLAS[sChild].intensity > 0)
		{
			numLights++;
			lightcutNodes[numLights - 1] = sChild;
			heap[numLights].NodeID = sChild;
			heap[numLights].error = errorFunction(-1, sChild, p, N, V, g_SLC_BLAS, g_lighting.SLC_TLASLeafStartIndex, debug );
		}

		// find maxId
		float maxError = -1e10;
		for (int i = 1; i <= numLights; i++)
		{
			if (heap[i].error > maxError)
			{
				maxError = heap[i].error;
				maxId = i;
			}
		}
		if (maxError <= 0) break;
	}
}

inline float SquaredDistanceToFarthestPoint(float3 p, float3 boundMin, float3 boundMax)
{
	float3 d = max(abs(boundMin - p), abs(boundMax - p));
	return dot(d, d);
}

inline float normalizedWeights(float l2_0, float l2_1, float intensGeom0, float intensGeom1)
{
	float ww0 = l2_1 * intensGeom0;
	float ww1 = l2_0 * intensGeom1;
	return ww0 / (ww0 + ww1);
};

bool firstChildWeight(float3 p, float3 N, float3 V, inout float prob0, int child0, int child1, int BLASOffset)
{
	Node c0;
	Node c1;
	if (BLASOffset >= 0)
	{
		c0 = g_SLC_BLAS[BLASOffset + child0];
		c1 = g_SLC_BLAS[BLASOffset + child1];
	}
	//else
	//{
	//	c0 = TLAS[child0];
	//	c1 = TLAS[child1];
	//}

	float c0_intensity = c0.intensity;
	float c1_intensity = c1.intensity;

	if (c0_intensity == 0)
	{
		if (c1_intensity == 0) return false;
		prob0 = 0;
		return true;
	}
	else if (c1_intensity == 0)
	{
		prob0 = 1;
		return true;
	}

	float3 c0_boundMin = c0.boundMin;
	float3 c0_boundMax = c0.boundMax;
	float3 c1_boundMin = c1.boundMin;
	float3 c1_boundMax = c1.boundMax;

	// Compute the weights
	float geom0 = GeomTermBound(p, N, c0_boundMin, c0_boundMax);
	float geom1 = GeomTermBound(p, N, c1_boundMin, c1_boundMax);
#ifdef LIGHT_CONE
	float3 c0r_boundMin = 2 * p - c0_boundMax;
	float3 c0r_boundMax = 2 * p - c0_boundMin;
	float3 c1r_boundMin = 2 * p - c1_boundMax;
	float3 c1r_boundMax = 2 * p - c1_boundMin;

	float cos0 = GeomTermBound(p, c0.cone.xyz, c0r_boundMin, c0r_boundMax);
	float cos1 = GeomTermBound(p, c1.cone.xyz, c1r_boundMin, c1r_boundMax);

	geom0 *= max(0.f, cos(max(0.f, acos(cos0) - c0.cone.w)));
	geom1 *= max(0.f, cos(max(0.f, acos(cos1) - c1.cone.w)));
#endif

	if (geom0 + geom1 == 0) return false;

	if (geom0 == 0)
	{
		prob0 = 0;
		return true;
	}
	else if (geom1 == 0)
	{
		prob0 = 1;
		return true;
	}

	float intensGeom0 = c0_intensity * geom0;
	float intensGeom1 = c1_intensity * geom1;

	float l2_min0;
	float l2_min1;
	l2_min0 = SquaredDistanceToClosestPoint(p, c0_boundMin, c0_boundMax);
	l2_min1 = SquaredDistanceToClosestPoint(p, c1_boundMin, c1_boundMax);

#ifdef EXPLORE_DISTANCE_TYPE
	if (distanceType == 0)
	{
		if (l2_min0 < WidthSquared(c0_boundMin, c0_boundMax) || l2_min1 < WidthSquared(c1_boundMin, c1_boundMax))
		{
			prob0 = intensGeom0 / (intensGeom0 + intensGeom1);
		}
		else
		{
			float w_max0 = normalizedWeights(l2_min0, l2_min1, intensGeom0, intensGeom1);
			prob0 = w_max0;	// closest point
		}
	}
	else if (distanceType == 1)
	{
		float3 l0 = 0.5*(c0_boundMin + c0_boundMax) - p;
		float3 l1 = 0.5*(c1_boundMin + c1_boundMax) - p;
		float w_max0 = normalizedWeights(max(0.001, dot(l0, l0)), max(0.001, dot(l1, l1)), intensGeom0, intensGeom1);
		prob0 = w_max0;	// closest point
	}
	else if (distanceType == 2) //avg weight of minmax (used in the paper)
	{
#endif
		float l2_max0 = SquaredDistanceToFarthestPoint(p, c0_boundMin, c0_boundMax);
		float l2_max1 = SquaredDistanceToFarthestPoint(p, c1_boundMin, c1_boundMax);
		float w_max0 = l2_min0 == 0 && l2_min1 == 0 ? intensGeom0 / (intensGeom0 + intensGeom1) : normalizedWeights(l2_min0, l2_min1, intensGeom0, intensGeom1);
		float w_min0 = normalizedWeights(l2_max0, l2_max1, intensGeom0, intensGeom1);
		prob0 = 0.5 * (w_max0 + w_min0);
#ifdef EXPLORE_DISTANCE_TYPE
	}
#endif
	return true;
};

inline bool traverseLightTree(inout int nid,
	StructuredBuffer<Node> nodeBuffer,
	int LeafStartIndex,
	int BLASOffset, inout float r, inout double nprob, float3 p, float3 N, float3 V)
{
	bool deadBranch = false;
	while (nid < LeafStartIndex) {
#ifdef CPU_BUILDER
		int c0_id = nodeBuffer[nid + max(0, BLASOffset)].ID;
		if (c0_id >= LeafStartIndex) {
			break;
		}
#else
		int c0_id = nid << 1;
#endif
		int c1_id = c0_id + 1;
		float prob0;

		if (firstChildWeight(p, N, V, prob0, c0_id, c1_id, BLASOffset)) {
			if (r < prob0) {
				nid = c0_id;
				r /= prob0;
				nprob *= prob0;
			}
			else {
				nid = c1_id;
				r = (r - prob0) / (1 - prob0);
				nprob *= (1 - prob0);
			}
		}
		else {
			deadBranch = true;
			break;
		}
	}
	return deadBranch;
}

ShaderLightPoint SLCGetLightFromNode( float3 worldPos, float3 worldNormal, float3 viewVec, int nodeID, inout RandomSequence rng, bool debug = false )
{
	float3 p = worldPos;
	float3 N = worldNormal;
	float3 V = viewVec;

	float			r = RandomSequence_GenerateSample1D(rng);
	double nprob	= 1;	// probability of picking that node
	int nid			= nodeID;

//	if( debug )
//	{
//        uint dbgCounter = DebugCounter();
//		DebugDraw2DText( float2( 100, 100 + dbgCounter * 20 ), float4( 1, 0.5, 0.5, 1 ), dbgCounter );
//		DebugDraw2DText( float2( 120, 100 + dbgCounter * 20 ), float4( 0.5, 1, 1.0, 1 ), float4( r, 0, 0, 0 ) );
//
//		Node n = g_SLC_BLAS[nid];
//	}

	// [branch] if( debug )
	// {
	// 	DebugDraw2DText( float2( 10, 10 ), float4( 1, 0, 0, 1 ), uint4( lightIndexOffset, g_SLC_BLAS[nid].ID, g_lighting.SLC_TLASLeafStartIndex, 0 ) );
	// 	DebugDraw3DLightViz( lightRaw.Position, lightRaw.Direction, lightRaw.Size, lightRaw.Range*0.01, lightRaw.SpotInnerAngle, lightRaw.SpotOuterAngle, lightRaw.Color * lightRaw.Intensity );
	// }

	bool deadBranch = traverseLightTree( nid, g_SLC_BLAS, g_lighting.SLC_TLASLeafStartIndex, 0, r, nprob, p, N, V );

	// for mesh slc this is triangleInstanceId
#ifdef CPU_BUILDER
	int lightIndexOffset = g_SLC_BLAS[nid].ID - g_lighting.SLC_TLASLeafStartIndex;
#else
	int lightIndexOffset = g_SLC_BLAS[nid].ID;
#endif

    ShaderLightPoint lightRaw = g_lightsPoint[lightIndexOffset];
	lightRaw.Intensity  *= g_globals.PreExposureMultiplier;
       
#if 0
	[branch] if( debug )
	{
        uint dbgCounter = DebugCounter();
		DebugDraw2DText( float2( 100, 100 + dbgCounter * 20 ), float4( 1, 0.5, 0.5, 1 ), dbgCounter );
		DebugDraw2DText( float2( 120, 100 + dbgCounter * 20 ), float4( 0.5, 1, 1.0, 1 ), float4( nid, lightIndexOffset, g_lighting.SLC_TLASLeafStartIndex, r ) );
	 	
		//DebugDraw2DText( float2( 10, 10 ), float4( 1, 0, 0, 1 ), uint4( lightIndexOffset, g_SLC_BLAS[nid].ID, g_lighting.SLC_TLASLeafStartIndex, 0 ) );

	 	DebugDraw3DLightViz( lightRaw.Position, lightRaw.Direction, lightRaw.Size, lightRaw.Range*0.01, lightRaw.SpotInnerAngle, lightRaw.SpotOuterAngle, lightRaw.Color * lightRaw.Intensity );
	}
#endif

    // // these _could_ be avoided (done earlier) but I'll do them here for simplicity for now
    // lightRaw.Position   = lightRaw.Position - g_globals.WorldBase.xyz;
    // lightRaw.Intensity  *= g_globals.PreExposureMultiplier;

	// why double?
	double one_over_prob = nprob == 0.0 ? 0.0 : 1.0 / nprob;
	lightRaw.Intensity = (float)(lightRaw.Intensity * one_over_prob);

	if( deadBranch )
		lightRaw.Intensity *= 0;

	return lightRaw;
}

// float4 rayDescs[MAX_LIGHT_SAMPLES];
// float3 colors[MAX_LIGHT_SAMPLES];
//	// write lightcut nodes
//
//	int startId = 0;
//	int endId = numLights;
//	for (int i = 0; i < numLights; i++)
//	{
//		int nodeID = lightcutNodes[i];
//		float3 hdc;
//		rayDescs[i] = computeNodeOneLevel(p, N, V, hdc, nodeID, rng);
//		colors[i] = hdc;
//	}
//
//	EvaluateShadowRays(startId, endId, p, N, colors, rayDescs, color);
//}