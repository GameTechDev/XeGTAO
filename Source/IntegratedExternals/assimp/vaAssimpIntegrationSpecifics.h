// included by all assimp .c/.cpp files but no other engine files - used to disable warnings or similar

#pragma once

#include "vaConfig.h"

#pragma warning ( disable : 4996 4702 4457 4458 4244 4706 4463 4189 4505 4127 4100 4456 4459 4245 4701 4267 4389 4714 4315 4131 4057 4310 4458 4275 4101 )

#define OPENDDL_NO_USE_CPP11
#define ASSIMP_BUILD_NO_C4D_IMPORTER
#define ASSIMP_IMPORTER_GLTF_USE_OPEN3DGC 1
#define ASSIMP_BUILD_DLL_EXPORT
#define _SCL_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#define OPENDDLPARSER_BUILD
#define ASSIMP_BUILD_NO_OWN_ZLIB
// removing currently unused functionality
#define ASSIMP_BUILD_NO_EXPORT
#define ASSIMP_BUILD_NO_3DS_IMPORTER
#define ASSIMP_BUILD_NO_AC_IMPORTER
#define ASSIMP_BUILD_NO_AMF_IMPORTER
#define ASSIMP_BUILD_NO_ASE_IMPORTER
#define ASSIMP_BUILD_NO_B3D_IMPORTER
#define ASSIMP_BUILD_NO_ASSBIN_IMPORTER
#define ASSIMP_BUILD_NO_COB_IMPORTER
#define ASSIMP_BUILD_NO_COLLADA_IMPORTER
#define ASSIMP_BUILD_NO_CSM_IMPORTER
#define ASSIMP_BUILD_NO_3MF_IMPORTER
#define ASSIMP_BUILD_NO_DXF_IMPORTER
#define ASSIMP_BUILD_NO_GLTF_EXPORTER
#define ASSIMP_BUILD_NO_HMP_IMPORTER
#define ASSIMP_BUILD_NO_IFC_IMPORTER
#define ASSIMP_BUILD_NO_IRR_IMPORTER
#define ASSIMP_BUILD_NO_LWO_IMPORTER
#define ASSIMP_BUILD_NO_MD2_IMPORTER
#define ASSIMP_BUILD_NO_MD3_IMPORTER
#define ASSIMP_BUILD_NO_MD5_IMPORTER
#define ASSIMP_BUILD_NO_MDC_IMPORTER
#define ASSIMP_BUILD_NO_MDL_IMPORTER
#define ASSIMP_BUILD_NO_MMD_IMPORTER
#define ASSIMP_BUILD_NO_MS3D_IMPORTER
#define ASSIMP_BUILD_NO_NDO_IMPORTER
#define ASSIMP_BUILD_NO_NFF_IMPORTER
#define ASSIMP_BUILD_NO_OGRE_IMPORTER
#define ASSIMP_BUILD_NO_OPENGEX_EXPORTER
#define ASSIMP_BUILD_NO_OPENGEX_IMPORTER
#define ASSIMP_BUILD_NO_PLY_IMPORTER
#define ASSIMP_BUILD_NO_Q3BSP_IMPORTER
#define ASSIMP_BUILD_NO_Q3D_IMPORTER
#define ASSIMP_BUILD_NO_RAW_IMPORTER
#define ASSIMP_BUILD_NO_SMD_IMPORTER
#define ASSIMP_BUILD_NO_STL_IMPORTER
#define ASSIMP_BUILD_NO_X3D_IMPORTER
#define ASSIMP_BUILD_NO_X_IMPORTER
#define ASSIMP_BUILD_NO_XGL_IMPORTER
#define ASSIMP_BUILD_NO_M3D_IMPORTER
#define ASSIMP_BUILD_NO_IRRMESH_IMPORTER
#define ASSIMP_BUILD_NO_LWS_IMPORTER
#define ASSIMP_BUILD_NO_OFF_IMPORTER
#define ASSIMP_BUILD_NO_SIB_IMPORTER
#define ASSIMP_BUILD_NO_TERRAGEN_IMPORTER
#define ASSIMP_BUILD_NO_3D_IMPORTER // unreal
//#define ASSIMP_BUILD_NO_OBJ_IMPORTER

#ifdef NDEBUG
//#define CMAKE_INTDIR="Release"
#else
//#define CMAKE_INTDIR="Debug"
#endif



//#pragma include_alias("IntegratedExternals/include/assimp/scene.h", "assimp/scene.h" )
//#pragma include_alias("IntegratedExternals/include/assimp/DefaultLogger.hpp", "assimp/DefaultLogger.hpp" )
//#pragma include_alias("IntegratedExternals/include/assimp/DefaultIOStream.h", "assimp/DefaultIOStream.h" )
//
//$(ProjectDir)..\..\Modules\IntegratedExternals\include\assimp\
//$(ProjectDir)..\..\Modules\IntegratedExternals\assimp\include\
//$(ProjectDir)..\..\Modules\IntegratedExternals\include\zlib\
//$(ProjectDir)..\..\Modules\IntegratedExternals\assimp\contrib\rapidjson\include
//$(ProjectDir)..\..\Modules\IntegratedExternals\assimp\contrib\
//$(ProjectDir)..\..\Modules\IntegratedExternals\assimp\contrib\unzip\
//$(ProjectDir)..\..\Modules\IntegratedExternals\assimp\contrib\irrXML\
//$(ProjectDir)..\..\Modules\IntegratedExternals\assimp\code\contrib\openddlparser\include\
