// Copyright (c) 2020, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

#include "HitCommon.hlsli"
#include "../RandomGenerator.hlsli"
#include "../LightTreeUtilities.hlsli"

#define MAX_LIGHT_SAMPLES 32

struct LightHeapData
{
	int    TLASNodeID; //negative indiates the BLAS pointed to by TLAS leaf
	int    BLASNodeID;
	int    sampledTLASNodeID;
	int    sampledBLASNodeID;
	float  error;			// temporarily stores the error
	float3  color;
};

struct OneLevelLightHeapData
{
	int NodeID;
	int sampledNodeID;
	float error;
	float3 color;
};

struct OneLevelLightHeapSimpleData
{
	int NodeID;
	float error;
};

cbuffer SLCConstants : register(b0)
{
	float3 viewerPos;
	float sceneRadius;
	int TLASLeafStartIndex; // this is LeafStartIndex for one level tree
	float errorLimit;
	int frameId;
	int pickType;
	int maxLightSamples;
	int VertexStride;
	int numMeshLightTriangles;
	int oneLevelTree;
	int cutSharingSize;
	int interleaveRate;
	float invNumPaths;
	int passId;
	float shadowBiasScale;
#ifdef EXPLORE_DISTANCE_TYPE
	int distanceType;
#endif
	int gUseMeshLight;
}

cbuffer BoundConstants : register(b1)
{
	float3 corner;
	int pad0;
	float3 dimension;
	float sceneLightBoundRadius;
}

// for mesh lights
StructuredBuffer<uint> g_MeshLightIndexBuffer : register(t13);
StructuredBuffer<EmissiveVertex> g_MeshLightVertexBuffer : register(t14);
StructuredBuffer<MeshLightInstancePrimtive> g_MeshLightInstancePrimitiveBuffer : register(t15);
// for VPLs
StructuredBuffer<float4> g_lightPositions : register(t16);
StructuredBuffer<float4> g_lightNormals : register(t17);
StructuredBuffer<float4> g_lightColors : register(t18);

StructuredBuffer<BLASInstanceHeader> g_BLASHeaders : register(t9);
StructuredBuffer<Node> TLAS : register(t10);
StructuredBuffer<Node> BLAS : register(t11);

Texture2D<float4> texPosition : register(t32);
Texture2D<float4> texNormal : register(t33);
Texture2D<float4> texAlbedo : register(t34);
Texture2D<float4> texSpecular : register(t35);
RWTexture2D<float3> Result : register(u12);
RWStructuredBuffer<int> DebugBuffer : register(u13);
// for shadow estimator
StructuredBuffer<int> g_lightcutBuffer : register(t64);
StructuredBuffer<float> g_lightcutCDFBuffer : register(t65);

Texture2D<float4> emissiveTextures[] : register(t0, space1);

#include "SLCHelperFunctions.hlsli"

float4 computeNodeOneLevel(float3 p, float3 N, float3 V, out float3 hdcolor, int nodeID, inout RandomSequence rng)
{
	int dummy;
	return computeNodeOneLevelHelper(p, N, V, dummy, hdcolor, nodeID, rng, false);
}

float4 computeNodeOneLevel(float3 p, float3 N, float3 V, out OneLevelLightHeapData hd, int nodeID, inout RandomSequence rng)
{
	hd.NodeID = nodeID;
	float3 hdcolor;
	int nid;
	float4 rayDesc = computeNodeOneLevelHelper(p, N, V, nid, hdcolor, nodeID, rng, true);
	hd.sampledNodeID = nid;
	hd.color = hdcolor;
	hd.error = errorFunction(-1, nodeID, p, N, V, TLAS, BLAS, g_BLASHeaders, TLASLeafStartIndex
	);
	return rayDesc;
}

float4 computeNode(float3 p, float3 N, float3 V, out float3 hdcolor, int TLASNodeID, int BLASNodeID, inout RandomSequence rng)
{
	int dummy0, dummy1;
	float dummy2;
	return computeNodeHelper(p, N, V, dummy0, dummy1, dummy2, hdcolor, TLASNodeID, BLASNodeID, rng, false);
};

float4 computeNode(float3 p, float3 N, float3 V, out LightHeapData hd, int TLASNodeID, int BLASNodeID, inout RandomSequence rng)
{
	hd.TLASNodeID = TLASNodeID;
	hd.BLASNodeID = BLASNodeID;

	hd.sampledTLASNodeID = -1;
	hd.sampledBLASNodeID = -1;

	float4 rayDesc = computeNodeHelper(p, N, V, hd.sampledTLASNodeID, hd.sampledBLASNodeID, hd.error, hd.color, TLASNodeID, BLASNodeID, rng, true);
	return rayDesc;
};

inline void EvaluateShadowRays(int startId, int endId, float3 p, float3 N,
	float3 colors[MAX_LIGHT_SAMPLES], float4 rayDescs[MAX_LIGHT_SAMPLES], inout float3 color)
{
	for (int i = startId; i < endId; i++)
	{
		ShadowRayPayload payload;
		RayDesc rd = {
			p + N * shadowBiasScale,
			0,
			rayDescs[i].xyz,
			rayDescs[i].w
		};
		payload.RayHitT = rayDescs[i].w;
		TraceRay(g_accel, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
			~0, 0, 1, 0, rd, payload);

		if (payload.RayHitT == rayDescs[i].w)
		{
			color += colors[i];
		}
	}
}

inline void EvaluateShadowRay(float3 p, float3 N,
	float3 unshadowedColor, float4 rayDesc, inout float3 color)
{
	ShadowRayPayload payload;
	RayDesc rd = {
		p + N * shadowBiasScale,
		0,
		rayDesc.xyz,
		rayDesc.w
	};
	payload.RayHitT = rayDesc.w;
	TraceRay(g_accel, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
		~0, 0, 1, 0, rd, payload);

	if (payload.RayHitT == rayDesc.w)
	{
		color += unshadowedColor;
	}
}


[shader("raygeneration")]
void RayGen()
{
	int2 pixelPos = DispatchRaysIndex().xy;

	float3 p = texPosition[pixelPos].xyz;
	float3 N = texNormal[pixelPos].xyz;
	float3 V = normalize(viewerPos - p);

	float3 totalcolor = 0;

	RandomSequence rng;

	RandomSequence_Initialize(rng, DispatchRaysDimensions().x * pixelPos.y + pixelPos.x, maxLightSamples*frameId + passId);
	rng.Type = 0;

	for (int sampleId = 0; sampleId < 1; sampleId++)
	{
		float3 color = 0;
		if (pickType == 0)
		{
			if (gUseMeshLight)
				color += computeRandomMeshLightSample(p, N, rng) / maxLightSamples;
			else
				color += computeRandomVPL(p, N, rng) / maxLightSamples;
		}
		else if (pickType == 1)
		{
			float3 hdc;
			float4 rayDesc;

			if (oneLevelTree)
			{
				rayDesc = computeNodeOneLevel(p, N, V, hdc, 1, rng);
			}
			else
			{
				rayDesc = computeNode(p, N, V, hdc, 1, -1, rng);
			}
			EvaluateShadowRay(p, N, hdc, rayDesc, color);
			color /= maxLightSamples;
		}
		else
		{
			// cut is precomputed
			if (cutSharingSize > 0)
			{
				int multiplier = oneLevelTree ? 1 : 2;

				int scrWidth = DispatchRaysDimensions().x;

				int startAddr = multiplier * MAX_CUT_NODES * ((pixelPos.y / cutSharingSize) * ((scrWidth + cutSharingSize - 1) / cutSharingSize) + pixelPos.x / cutSharingSize);

				int cutNodeId = passId;

				int startId = 0;
				int endId = maxLightSamples;

				if (interleaveRate > 1)
				{
					int interleaveGroupSize = interleaveRate * interleaveRate;
					int interleaveSamplingId = (frameId + interleaveRate * (pixelPos.y % interleaveRate) + (pixelPos.x % interleaveRate)) % interleaveGroupSize;

					float ratio = float(maxLightSamples) / interleaveGroupSize;
					startId = int(interleaveSamplingId * ratio);    // to prevent floating point error
					endId = interleaveSamplingId == interleaveGroupSize - 1 ? maxLightSamples : int((interleaveSamplingId + 1) * ratio);

					if (startId + passId >= endId) break;
					else
					{
						cutNodeId = startId + passId;
					}
				}
				else
				{
					if (cutNodeId >= maxLightSamples) return;
				}

				float3 hdc;
				float4 rayDesc;

				if (oneLevelTree)
				{
					int nodeID = g_lightcutBuffer[startAddr + cutNodeId];
					if (nodeID < 0) break;
					rayDesc = computeNodeOneLevel(p, N, V, hdc, nodeID, rng);
				}
				else
				{
					int TLASNodeID = g_lightcutBuffer[startAddr + 2 * cutNodeId];
					int BLASNodeID = g_lightcutBuffer[startAddr + 2 * cutNodeId + 1];
					if (TLASNodeID < 0) break;
					rayDesc = computeNode(p, N, V, hdc, TLASNodeID, BLASNodeID, rng);
				}

				EvaluateShadowRay(p, N, hdc, rayDesc, color);
				color *= interleaveRate * interleaveRate;

			}
			else
			{
				if (oneLevelTree)
				{
					// select a cut first
					float4 rayDescs[MAX_LIGHT_SAMPLES];
					float3 colors[MAX_LIGHT_SAMPLES];
					int numLights = 1;
					OneLevelLightHeapSimpleData heap[MAX_CUT_NODES + 1];
					heap[1].NodeID = 1;
					heap[1].error = 1e27;
					int maxId = 1;
					int lightcutNodes[MAX_LIGHT_SAMPLES];
					lightcutNodes[0] = 1;
					while (numLights < maxLightSamples)
					{
						int id = maxId;
						int NodeID = heap[id].NodeID;

#ifdef CPU_BUILDER
						int pChild = BLAS[NodeID].ID;
#else
						int pChild = NodeID << 1;
#endif
						int sChild = pChild + 1;

						lightcutNodes[id - 1] = pChild;
						heap[id].NodeID = pChild;
						heap[id].error = errorFunction(-1, pChild, p, N, V, TLAS, BLAS, g_BLASHeaders, TLASLeafStartIndex
						);

						// check bogus light
						if (BLAS[sChild].intensity > 0)
						{
							numLights++;
							lightcutNodes[numLights - 1] = sChild;
							heap[numLights].NodeID = sChild;
							heap[numLights].error = errorFunction(-1, sChild, p, N, V, TLAS, BLAS, g_BLASHeaders, TLASLeafStartIndex
							);
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

					// write lightcut nodes

					int startId = 0;
					int endId = numLights;
					for (int i = 0; i < numLights; i++)
					{
						int nodeID = lightcutNodes[i];
						float3 hdc;
						rayDescs[i] = computeNodeOneLevel(p, N, V, hdc, nodeID, rng);
						colors[i] = hdc;
					}

					EvaluateShadowRays(startId, endId, p, N, colors, rayDescs, color);
				}
				else
				{
					LightHeapData heap[MAX_LIGHT_SAMPLES + 1];

					int numLights = 0;

					computeNode(p, N, V, heap[1], 1, -1, rng);

					numLights = 1;
					color = heap[1].color;
					float colorIntens = GetColorIntensity(color);

					int maxId = 1;

					while (numLights < maxLightSamples)
					{
						int id = maxId;

						int p_TLASNodeID = heap[id].TLASNodeID;
						int p_BLASNodeID = heap[id].BLASNodeID;
						int s_TLASNodeID = p_TLASNodeID;
						int s_BLASNodeID = p_BLASNodeID;

						int pChild = 0;
						int sChild = 0;

						int BLASOffset = -1;
						int BLASId = -1; // actually BLASInstance

						TwoLevelTreeGetChildrenInfo(p_TLASNodeID, p_BLASNodeID, s_TLASNodeID, s_BLASNodeID,
							pChild, sChild, BLASOffset, BLASId, TLAS, BLAS, g_BLASHeaders, TLASLeafStartIndex, true, heap[id].sampledTLASNodeID, heap[id].sampledBLASNodeID);

						color = color - heap[id].color;

						float prob0;

						if (sChild != -1)
						{
							firstChildWeight(p, N, V, prob0, pChild, sChild, BLASOffset);
						}
						else
						{
							prob0 = 1.0;
						}

						heap[id].color *= prob0;
						heap[id].TLASNodeID = p_TLASNodeID;
						heap[id].BLASNodeID = p_BLASNodeID;

						if (BLASId > 0)
						{
							float3x3 rotT = transpose(g_BLASHeaders[BLASId].rotation);
							float3 p_transformed = (1.f / g_BLASHeaders[BLASId].scaling) * mul(rotT, p - g_BLASHeaders[BLASId].translation);
							float3 N_transformed = mul(rotT, N);
							float3 V_transformed = mul(rotT, V);
							heap[id].error = errorFunction(pChild, BLASId, p_transformed, N_transformed, V_transformed, TLAS, BLAS, g_BLASHeaders, TLASLeafStartIndex
							);
						}
						else
						{
							heap[id].error = errorFunction(pChild, BLASId, p, N, V, TLAS, BLAS, g_BLASHeaders, TLASLeafStartIndex
							);
						}
						color = color + heap[id].color;

						if (sChild != -1)
						{
							numLights++;

							computeNode(p, N, V, heap[numLights], s_TLASNodeID, s_BLASNodeID, rng);
							color = color + heap[numLights].color;
						}

						colorIntens = GetColorIntensity(color);

						// find maxId
						float maxError = 0;
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

			}
		}
		totalcolor += color;
	}
	if (any(isinf(totalcolor)) || any(isnan(totalcolor))) totalcolor = 0;


#ifdef GROUND_TRUTH
	Result[pixelPos] = passId == 0 && frameId == 0 ? totalcolor : (passId == 0 ? Result[pixelPos] * frameId / (frameId + 1.f) + totalcolor / (frameId + 1.f) : Result[pixelPos] + totalcolor / (frameId + 1.f));

#else
	Result[pixelPos] = passId == 0 ? totalcolor : Result[pixelPos] + totalcolor;
#endif

}
