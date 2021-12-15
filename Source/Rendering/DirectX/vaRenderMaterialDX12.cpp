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

#include "vaRenderMaterialDX12.h"

#include "Rendering/DirectX/vaRenderDeviceContextDX12.h"
#include "Rendering/DirectX/vaRenderBuffersDX12.h"

using namespace Vanilla;

vaRenderMaterialManagerDX12::vaRenderMaterialManagerDX12( const vaRenderingModuleParams & params ) : vaRenderMaterialManager( params )
{
    params.RenderDevice.e_BeforeEndFrame.AddWithToken( m_aliveToken, [ thisPtr = this ]( vaRenderDevice & )
        { thisPtr->EndFrameCleanup( ); } );

}

void vaRenderMaterialManagerDX12::UpdateAndSetToGlobals( vaRenderDeviceContext & renderContext, vaShaderItemGlobals & shaderItemGlobals, const vaDrawAttributes * drawAttributes )
{
    // if raytracing enabled, this collects all callable shaders exposed by materials - one per material unfortunately (even though many will have identical shaders)
    // and collates them for later use when creating shader tables and raytracing PSOs

    // only needed if raytracing enabled, and only update once per frame (data is safe for the duration of the frame)
    if( drawAttributes != nullptr && drawAttributes->Raytracing != nullptr && m_callableShaderTableLastFrameIndex < GetRenderDevice().GetCurrentFrameIndex() )
    {
        // no one touches materials now!
        std::shared_lock manager( Mutex() );

        // this doesn't necessarily mean any materials change but in most cases it does
        bool needsRebuild = false;//m_callableShaderTable == nullptr;
        needsRebuild |= m_globalCallablesTable.size() != m_materials.Size();

        // resize to SIZE, not the count - using the sparse array setup!
        m_globalCallablesTable.resize( m_materials.Size() );

        if( needsRebuild )
            m_uniqueCallablesTable.clear();

        for( uint32 sparseIndex : m_materials.PackedArray( ) )
        {
            vaRenderMaterial & material = *m_materials.At( sparseIndex );
            CallableShaders & entry = m_globalCallablesTable[ sparseIndex ];

            // We can update all materials here - even the ones that can't be rendered; this will reduce the differences between
            // the tables and reduce PSO rebuilds, but can be a lot more costly (will be performed for all loaded assets).
            // Sticking with that for now - one can actually disable this by just removing the PreRenderUpdate below because
            // the vaSceneRenderInstanceProcessor calls it on all used materials before this step.
            material.PreRenderUpdate( renderContext );

            // material ID changed on this sparse index - a material got deleted and another got added; that's fine, rebuild required
            needsRebuild |= ( material.UIDObject_GetUID() != entry.MaterialID );
            entry.MaterialID = material.UIDObject_GetUID();

            vaFramePtr<vaShaderLibrary> shader; string uniqueID; int uniqueTableIndex;
            material.GetCallableShaderLibrary( shader, uniqueID, uniqueTableIndex );
            if( uniqueTableIndex >= m_uniqueCallablesTable.size() ) // can be increased by material.PreRenderUpdate above!
                m_uniqueCallablesTable.resize( uniqueTableIndex+1 );
            int64 prevLibraryUniqueContentsID = entry.LibraryUniqueContentsID;
            vaShader::State state = (shader == nullptr)?( vaShader::State::Empty ):(AsDX12( *shader ).GetShader( entry.LibraryBlob, entry.LibraryUniqueContentsID ));
            if( state != vaShader::State::Cooked )
            {
                needsRebuild |= prevLibraryUniqueContentsID != -1;
                entry.LibraryUniqueContentsID = -1;
                assert( entry.LibraryBlob == nullptr );
                entry.Reset();
                m_uniqueCallablesTable[uniqueTableIndex] = entry;
                // AsDX12(material).SetCallableShaderTableIndex( 0 );
                continue;
            }
            entry.UniqueIDString    = vaStringTools::SimpleWiden( uniqueID );

            assert( uniqueTableIndex != -1 );
            m_uniqueCallablesTable[uniqueTableIndex] = entry;
            // // Ok now find unique, slot them into the table and that's it!
            // auto res = m_uniqueCallablesMap.insert( {entry.LibraryBlob, 0} );
            // if( res.second )
            // {
            //     res.first->second = (int)m_uniqueCallablesTable.size();
            //     m_uniqueCallablesTable.push_back( entry );
            // }
            // Update unique index
            // AsDX12(material).SetCallableShaderTableIndex( res.first->second );

            needsRebuild |= entry.LibraryUniqueContentsID != prevLibraryUniqueContentsID;
        }

        // Callable group shader table
        if( needsRebuild )
            m_callableShaderTableUniqueContentsID++;

        m_callableShaderTableLastFrameIndex = GetRenderDevice().GetCurrentFrameIndex();
    }



    vaRenderMaterialManager::UpdateAndSetToGlobals( renderContext, shaderItemGlobals, drawAttributes );
}

void vaRenderMaterialManagerDX12::EndFrameCleanup( )
{
    if( m_callableShaderTableLastFrameIndex == GetRenderDevice().GetCurrentFrameIndex() )
    {
        // clear is probably enough - no time to debug now :D
        for( int i = 0; i < m_uniqueCallablesTable.size(); i++ )
            m_uniqueCallablesTable[i].Reset();
        m_uniqueCallablesTable.clear();
        for( int i = 0; i < m_globalCallablesTable.size(); i++ )
            m_globalCallablesTable[i].Reset();
    }
}

void RegisterRenderMaterialDX12( )
{
    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX12, vaRenderMaterial, vaRenderMaterialDX12 );
    VA_RENDERING_MODULE_REGISTER( vaRenderDeviceDX12, vaRenderMaterialManager, vaRenderMaterialManagerDX12 );
}

