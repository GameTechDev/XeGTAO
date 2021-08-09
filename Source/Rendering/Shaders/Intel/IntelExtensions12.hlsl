/*===================== begin_copyright_notice ==================================

INTEL CONFIDENTIAL
Copyright 2020
Intel Corporation All Rights Reserved.

The source code contained or described herein and all documents related to the
source code ("Material") are owned by Intel Corporation or its suppliers or
licensors. Title to the Material remains with Intel Corporation or its suppliers
and licensors. The Material contains trade secrets and proprietary and confidential
information of Intel or its suppliers and licensors. The Material is protected by
worldwide copyright and trade secret laws and treaty provisions. No part of the
Material may be used, copied, reproduced, modified, published, uploaded, posted,
transmitted, distributed, or disclosed in any way without Intel's prior express
written permission.

No license under any patent, copyright, trade secret or other intellectual
property right is granted to or conferred upon you by disclosure or delivery
of the Materials, either expressly, by implication, inducement, estoppel or
otherwise. Any license under such intellectual property rights must be express
and approved by Intel in writing.

======================= end_copyright_notice ==================================*/

//
// Intel extension buffer structure, generic interface for
//   0-operand extensions (e.g. sync opcodes)
//   1-operand unary operations (e.g. render target writes)
//   2-operand binary operations (future extensions)
//   3-operand ternary operations (future extensions)
//
struct IntelExtensionStruct
{
    uint   opcode; 	// opcode to execute
    uint   rid;		// resource ID
    uint   sid;		// sampler ID

    float4 src0f;	// float source operand  0
    float4 src1f;	// float source operand  0
    float4 src2f;	// float source operand  0
    float4 dst0f;	// float destination operand

    uint4  src0u;
    uint4  src1u;
    uint4  src2u;
    uint4  dst0u;

    float  pad[181]; // total length 864
};

//
// extension opcodes
//

// Define RW buffer for Intel extensions.
// Application should bind null resource, operations will be ignored.
RWStructuredBuffer<IntelExtensionStruct> g_IntelExt : register( u63 );

//
// Initialize Intel HLSL Extensions
// This method should be called before any other extension function
//
void IntelExt_Init()
{
    uint4 init = { 0x63746e69, 0x6c736c68, 0x6e747865, 0x32313030 }; // intc hlsl extn 0012
    g_IntelExt[0].src0u = init;
}

