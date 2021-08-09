///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaRendering.h"

#include "vaRenderingIncludes.h"

#include "Rendering/vaRenderMesh.h"
#include "Rendering/vaRenderMaterial.h"

#include "Core/System/vaFileTools.h"


using namespace Vanilla;

void vaRenderingModuleRegistrar::RegisterModule( const std::string & deviceTypeName, const std::string & name, std::function< vaRenderingModule * ( const vaRenderingModuleParams & )> moduleCreateFunction )
{
    std::unique_lock<std::recursive_mutex> lockRegistrarMutex( GetInstance().m_mutex );

    assert( name != "" );
    string cleanedUpName = deviceTypeName + "<=>" + name;

    auto it = GetInstance().m_modules.find( cleanedUpName );
    if( it != GetInstance().m_modules.end( ) )
    {
        VA_ERROR( L"vaRenderingCore::RegisterModule - cleanedUpName '%s' already registered!", cleanedUpName.c_str() );
        return;
    }
    GetInstance().m_modules.insert( std::pair< std::string, ModuleInfo >( cleanedUpName, ModuleInfo( moduleCreateFunction ) ) );
}

vaRenderingModule * vaRenderingModuleRegistrar::CreateModule( const std::string & deviceTypeName, const std::string & name, const vaRenderingModuleParams & params )
{
    std::unique_lock<std::recursive_mutex> lockRegistrarMutex( GetInstance().m_mutex );

    string cleanedUpName = deviceTypeName + "<=>" + name;
    auto it = GetInstance().m_modules.find( cleanedUpName );
    if( it == GetInstance().m_modules.end( ) )
        return nullptr;

    vaRenderingModule * ret = ( *it ).second.ModuleCreateFunction( params );

    ret->InternalRenderingModuleSetTypeName( cleanedUpName );

    return ret;
}

//void vaRenderingTools::IHO_Draw( ) 
//{ 
//}