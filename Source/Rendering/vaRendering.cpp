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

void vaShaderItemGlobals::Validate( ) const
{
#ifdef _DEBUG
    for( int i = 0; i < ShaderResourceViews.size(); i++ )
    {  if( ShaderResourceViews[i] != nullptr ) assert( (ShaderResourceViews[i]->GetBindSupportFlags() & vaResourceBindSupportFlags::ShaderResource ) != 0 ); }
    for( int i = 0; i < ConstantBuffers.size(); i++ )
    {  if( ConstantBuffers[i] != nullptr ) assert( (ConstantBuffers[i]->GetBindSupportFlags() & vaResourceBindSupportFlags::ConstantBuffer ) != 0 ); }
    for( int i = 0; i < UnorderedAccessViews.size(); i++ )
    {  if( UnorderedAccessViews[i] != nullptr ) assert( (UnorderedAccessViews[i]->GetBindSupportFlags() & vaResourceBindSupportFlags::UnorderedAccess ) != 0 ); }
    if( RaytracingAcceleationStructSRV != nullptr )
        assert( (RaytracingAcceleationStructSRV->GetBindSupportFlags() & vaResourceBindSupportFlags::RaytracingAccelerationStructure) != 0 );
#endif
}

void vaGraphicsItem::Validate( ) const
{
#ifdef _DEBUG
    for( int i = 0; i < ShaderResourceViews.size(); i++ )
    {  if( ShaderResourceViews[i] != nullptr ) assert( (ShaderResourceViews[i]->GetBindSupportFlags() & vaResourceBindSupportFlags::ShaderResource ) != 0 ); }
    for( int i = 0; i < ConstantBuffers.size(); i++ )
    {  if( ConstantBuffers[i] != nullptr ) assert( (ConstantBuffers[i]->GetBindSupportFlags() & vaResourceBindSupportFlags::ConstantBuffer ) != 0 ); }
    if( VertexBuffer != nullptr )
        assert( (VertexBuffer->GetBindSupportFlags() & vaResourceBindSupportFlags::VertexBuffer) != 0 );
    if( IndexBuffer != nullptr )
        assert( (IndexBuffer->GetBindSupportFlags() & vaResourceBindSupportFlags::IndexBuffer) != 0 );
#endif
}

void vaComputeItem::Validate( ) const
{
    assert( ComputeShader != nullptr );
    for( int i = 0; i < ShaderResourceViews.size(); i++ )
    {  if( ShaderResourceViews[i] != nullptr ) assert( (ShaderResourceViews[i]->GetBindSupportFlags() & vaResourceBindSupportFlags::ShaderResource ) != 0 ); }
    for( int i = 0; i < ConstantBuffers.size(); i++ )
    {  if( ConstantBuffers[i] != nullptr ) assert( (ConstantBuffers[i]->GetBindSupportFlags() & vaResourceBindSupportFlags::ConstantBuffer ) != 0 ); }

    switch( ComputeType )
    {
    case( vaComputeItem::Dispatch ): 
        // No threads will be dispatched, because at least one of {ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ} is 0. This is probably not intentional?
        assert( DispatchParams.ThreadGroupCountX != 0 && DispatchParams.ThreadGroupCountY != 0 && DispatchParams.ThreadGroupCountZ != 0 );
        break;
    case( vaComputeItem::DispatchIndirect ): 
    {   
        assert( DispatchIndirectParams.BufferForArgs != nullptr );
    } break;
    default:
        assert( false );
        break;
    }

}

void vaRaytraceItem::Validate( ) const
{
    assert( RayGen != "" );
    for( int i = 0; i < ShaderResourceViews.size(); i++ )
    {  if( ShaderResourceViews[i] != nullptr ) assert( (ShaderResourceViews[i]->GetBindSupportFlags() & vaResourceBindSupportFlags::ShaderResource ) != 0 ); }
    for( int i = 0; i < ConstantBuffers.size(); i++ )
    {  if( ConstantBuffers[i] != nullptr ) assert( (ConstantBuffers[i]->GetBindSupportFlags() & vaResourceBindSupportFlags::ConstantBuffer ) != 0 ); }
}