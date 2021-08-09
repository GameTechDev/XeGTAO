///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaShader.h"

#include "Rendering/vaRenderDevice.h"

#include "Core/System/vaFileTools.h"

using namespace Vanilla;

std::vector<vaShader *> vaShader::s_allShaderList;
mutex vaShader::s_allShaderListMutex;
std::atomic_int vaShader::s_activelyCompilingShaderCount = 0;
std::atomic_int64_t vaShader::s_lastUniqueShaderContentsID = -1;

vaShader::vaShader( const vaRenderingModuleParams & params ) : vaRenderingModule( params )
{ 
    m_lastLoadedFromCache = false;

    {
        std::unique_lock<mutex> shaderListLock( s_allShaderListMutex );
        s_allShaderList.push_back( this ); 
        m_allShaderListIndex = (int)s_allShaderList.size()-1;
    }
}

vaShader::~vaShader( ) 
{ 
    // you probably need to finish task in all child classes before destroying outputs
    {
        std::unique_lock btlock( m_backgroundCreationTaskMutex );
        assert( m_backgroundCreationTask == nullptr || vaBackgroundTaskManager::GetInstance().IsFinished(m_backgroundCreationTask) );
    }


    {
        std::unique_lock<mutex> shaderListLock( s_allShaderListMutex );
        assert( s_allShaderList[m_allShaderListIndex] == this );
        if( s_allShaderList.size( ) - 1 != m_allShaderListIndex )
        {
            int lastIndex = (int)s_allShaderList.size()-1;
            s_allShaderList[lastIndex]->m_allShaderListIndex = m_allShaderListIndex;
            std::swap( s_allShaderList[m_allShaderListIndex], s_allShaderList[lastIndex] );
        }
        s_allShaderList.pop_back( );

        if( s_allShaderList.size( ) == 0 )
        {
            s_allShaderList.shrink_to_fit();
        }
    }
}

//
void vaShader::CreateShaderFromFile( const string & _filePath, const string & entryPoint, const std::vector<pair<string, string>> & macros, bool forceImmediateCompile )
{
    wstring filePath = vaStringTools::SimpleWiden(_filePath);
    const string shaderModel = string(GetSMPrefix())+"_"+GetSMVersion();
    assert( filePath != L"" && /*entryPoint != "" &&*/ shaderModel != "" );

    std::unique_lock btlock( m_backgroundCreationTaskMutex );
    vaBackgroundTaskManager::GetInstance().WaitUntilFinished( m_backgroundCreationTask );

    // clean the current contents
    Clear( false );

    // Set creation params
    {
        std::unique_lock allShaderDataLock( m_allShaderDataMutex ); 
        m_state                 = State::Uncooked;
        m_uniqueContentsID      = -1;
        m_shaderCode            = "";
        m_shaderFilePath        = filePath;
        m_entryPoint            = entryPoint;
        m_shaderModel           = shaderModel;
        m_macros                = macros;
        m_forceImmediateCompile = forceImmediateCompile;
        m_lastError             = "";
    }

    // increase the number BEFORE launching the threads (otherwise it is 0 while we're waiting for the compile thread to spawn)
    { /*std::unique_lock<mutex> shaderListLock( s_allShaderListMutex );*/ s_activelyCompilingShaderCount++; }

    auto shaderCompileLambda = [thisPtr = this]( vaBackgroundTaskManager::TaskContext & ) 
    {
        {
            std::unique_lock allShaderDataLock( thisPtr->m_allShaderDataMutex ); 
            thisPtr->CreateShader( ); 
        }
        { /*std::unique_lock<mutex> shaderListLock( s_allShaderListMutex );*/ s_activelyCompilingShaderCount--; }
        return true;   
    };

#ifndef VA_SHADER_BACKGROUND_COMPILATION_ENABLE
    forceImmediateCompile = true;
#endif
    if( forceImmediateCompile )
        shaderCompileLambda( vaBackgroundTaskManager::TaskContext() );
    else
        vaBackgroundTaskManager::GetInstance().Spawn( m_backgroundCreationTask, vaStringTools::Format("Compiling shader %s %s %s", vaStringTools::SimpleNarrow(m_shaderFilePath).c_str(), m_entryPoint.c_str(), m_shaderModel.c_str() ).c_str(), 
            vaBackgroundTaskManager::SpawnFlags::UseThreadPool, shaderCompileLambda );
}

void vaShader::CreateShaderFromBuffer( const string & shaderCode, const string & entryPoint, const std::vector<pair<string, string>> & macros, bool forceImmediateCompile )
{
    const string shaderModel = string( GetSMPrefix( ) ) + "_" + GetSMVersion( );
    assert( shaderCode != "" && entryPoint != "" && shaderModel != "" );

    std::unique_lock btlock( m_backgroundCreationTaskMutex );
    vaBackgroundTaskManager::GetInstance().WaitUntilFinished( m_backgroundCreationTask );

    // clean the current contents
    Clear( false );

    // Set creation params
    {
        std::unique_lock allShaderDataLock( m_allShaderDataMutex ); 
        m_state                 = State::Uncooked;
        m_uniqueContentsID      = -1;
        m_shaderCode            = shaderCode;
        m_shaderFilePath        = L"";
        m_entryPoint            = entryPoint;
        m_shaderModel           = shaderModel;
        m_macros                = macros;
        m_forceImmediateCompile = forceImmediateCompile;
        m_lastError             = "";
    }

    // increase the number BEFORE launching the threads (otherwise it is 0 while we're waiting for the compile thread to spawn)
    { /*std::unique_lock<mutex> shaderListLock( s_allShaderListMutex );*/ s_activelyCompilingShaderCount++; }

    auto shaderCompileLambda = [thisPtr = this]( vaBackgroundTaskManager::TaskContext & ) 
    {
        {
            std::unique_lock allShaderDataLock( thisPtr->m_allShaderDataMutex ); 
            thisPtr->CreateShader( ); 
        }
        { /*std::unique_lock<mutex> shaderListLock( s_allShaderListMutex );*/ s_activelyCompilingShaderCount--; }
        return true;   
    };

#ifndef VA_SHADER_BACKGROUND_COMPILATION_ENABLE
    forceImmediateCompile = true;
#endif
    if( forceImmediateCompile )
        shaderCompileLambda( vaBackgroundTaskManager::TaskContext() );
    else
        vaBackgroundTaskManager::GetInstance().Spawn( m_backgroundCreationTask, vaStringTools::Format("Compiling shader %s %s %s", vaStringTools::SimpleNarrow(m_shaderFilePath).c_str(), m_entryPoint.c_str(), m_shaderModel.c_str() ).c_str(), 
            vaBackgroundTaskManager::SpawnFlags::UseThreadPool, shaderCompileLambda );
}
//
void vaShader::Reload( ) 
{ 
    std::unique_lock btlock( m_backgroundCreationTaskMutex );
    vaBackgroundTaskManager::GetInstance().WaitUntilFinished( m_backgroundCreationTask );

    bool forceImmediateCompile;
    { std::unique_lock allShaderDataLock( m_allShaderDataMutex ); forceImmediateCompile = m_forceImmediateCompile; }

    // nothing to do here
    if( ( m_shaderFilePath.size( ) == 0 ) && ( m_shaderCode.size( ) == 0 ) )
        return;

    // increase the number BEFORE launching the threads (otherwise it is 0 while we're waiting for the compile thread to spawn)
    { /*std::unique_lock<mutex> shaderListLock( s_allShaderListMutex );*/ s_activelyCompilingShaderCount++; }

    auto shaderCompileLambda = [this]( vaBackgroundTaskManager::TaskContext & ) 
    {
        {
            std::unique_lock allShaderDataLock( m_allShaderDataMutex ); 
            DestroyShader( );   // cooked -> uncooked (also uncooked with error -> uncooked with no error ready to compile)
            CreateShader( ); 
        }
        { /*std::unique_lock<mutex> shaderListLock( s_allShaderListMutex );*/ s_activelyCompilingShaderCount--; }
        return true;   
    };

#ifndef VA_SHADER_BACKGROUND_COMPILATION_ENABLE
    forceImmediateCompile = true;
#endif
    if( forceImmediateCompile )
        shaderCompileLambda( vaBackgroundTaskManager::TaskContext() );
    else
        vaBackgroundTaskManager::GetInstance().Spawn( m_backgroundCreationTask, vaStringTools::Format("Compiling shader %s %s %s", vaStringTools::SimpleNarrow(m_shaderFilePath).c_str(), m_entryPoint.c_str(), m_shaderModel.c_str() ).c_str(), 
            vaBackgroundTaskManager::SpawnFlags::UseThreadPool, shaderCompileLambda );
}
//
void vaShader::WaitFinishIfBackgroundCreateActive( )
{
    std::unique_lock btlock( m_backgroundCreationTaskMutex );
    vaBackgroundTaskManager::GetInstance().WaitUntilFinished( m_backgroundCreationTask );
}

//
void vaShader::ReloadAll( )
{
    assert( vaRenderDevice::IsRenderThread() );  // only supported from main thread for now

#ifndef VA_SHADER_BACKGROUND_COMPILATION_ENABLE
    vaLog::GetInstance( ).Add( LOG_COLORS_SHADERS, L"Recompiling shaders..." );
#else
    vaLog::GetInstance( ).Add( LOG_COLORS_SHADERS, L"Recompiling shaders (spawning multithreaded recompile)..." );
#endif

    {
        std::unique_lock<mutex> shaderListLock( GetAllShaderListMutex( ) );
#ifndef VA_SHADER_BACKGROUND_COMPILATION_ENABLE
        int totalLoaded = (int)GetAllShaderList( ).size( );
        int totalLoadedFromCache = 0;
#endif
        for( size_t i = 0; i < GetAllShaderList( ).size( ); i++ )
        {
            GetAllShaderList( )[i]->Reload( );
#ifndef VA_SHADER_BACKGROUND_COMPILATION_ENABLE
            if( GetAllShaderList( )[i]->IsLoadedFromCache( ) )
                totalLoadedFromCache++;
#endif
        }
#ifndef VA_SHADER_BACKGROUND_COMPILATION_ENABLE
        vaLog::GetInstance( ).Add( LOG_COLORS_SHADERS, L"... %d shaders reloaded (%d from cache)", totalLoaded, totalLoadedFromCache );
#endif
    }
}

void vaShader::GetMacrosAsIncludeFile( string & outString )
{
    //std::unique_lock<mutex> allShaderDataLock( m_allShaderDataMutex ); 
    // m_allShaderDataMutex.assert_locked_by_caller();

    outString.clear( );
    for( int i = 0; i < m_macros.size( ); i++ )
    {
        outString += "#define " + m_macros[i].first + " " + m_macros[i].second + "\n";
    }
}

void vaShader::DumpDisassembly( const string & fileName )
{
    string txt = GetDisassembly( );
    string path = vaFileTools::CleanupPath( fileName, false );
    if( !vaFileTools::PathHasDirectory(path) )
        path = vaCore::GetExecutableDirectoryNarrow() + path;
    string info = m_entryPoint + " " + m_shaderModel;

    if( vaStringTools::WriteTextFile( path, txt ) )
        VA_LOG_SUCCESS( "Shader disassembly for %s saved to '%s'", info.c_str(), path.c_str() );
    else
        VA_LOG_ERROR( "Error while trying to write shader disassembly for %s to '%s'", info.c_str(), path.c_str() );

}


vaShaderManager::vaShaderManager( vaRenderDevice & device )  : vaRenderingModule( vaRenderingModuleParams( device ) ) 
{ 
#ifdef VA_SHADER_BACKGROUND_COMPILATION_ENABLE
    auto progressInfoLambda = [this]( vaBackgroundTaskManager::TaskContext & context ) 
    {
        while( !context.ForceStop )
        {
            int numberOfCompilingShaders = vaShader::GetNumberOfCompilingShaders();
            if( numberOfCompilingShaders != 0 )
            {
                std::unique_lock<mutex> shaderListLock( vaShader::GetAllShaderListMutex(), std::try_to_lock );
                if( shaderListLock.owns_lock() )
                {
                    context.Progress = 1.0f - float(numberOfCompilingShaders) / float(vaShader::GetAllShaderList().size()-1);
                }
                context.HideInUI = false;
            }
            else
            {
                context.HideInUI = true;
            }
            vaThreading::Sleep(100);
        }
        return true;
    };
    vaBackgroundTaskManager::GetInstance().Spawn( m_backgroundShaderCompilationProgressIndicator, "Compiling shaders...", vaBackgroundTaskManager::SpawnFlags::ShowInUI, progressInfoLambda );
#endif
}
vaShaderManager::~vaShaderManager( )
{ 
#ifdef VA_SHADER_BACKGROUND_COMPILATION_ENABLE
    vaBackgroundTaskManager::GetInstance().MarkForStopping( m_backgroundShaderCompilationProgressIndicator );
    vaBackgroundTaskManager::GetInstance().WaitUntilFinished( m_backgroundShaderCompilationProgressIndicator );
#endif
}
