/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Taskflow:                     https://github.com/cpp-taskflow/cpp-taskflow
// Taskflow license:             MIT license (https://github.com/cpp-taskflow/cpp-taskflow/blob/master/LICENSE)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "IntegratedExternals/vaTaskflowIntegration.h"

#include <fstream>

#include "Core/vaProfiler.h"

#ifdef VA_TASKFLOW_INTEGRATION_ENABLED

namespace Vanilla
{
    class vaTFObserver : public tf::ExecutorObserverInterface 
    {
    public:
        friend class tf::Executor;
        virtual ~vaTFObserver( ) = default;

        virtual void set_up( size_t num_workers ) override { num_workers; };
        virtual void on_entry( size_t worker_id, tf::TaskView task_view ) override    
        { 
            if( task_view.name().length() == 0 ) 
                return; 
            worker_id; 
            auto context = vaTracer::LocalThreadContext(); 
            context->OnBegin( context->MapName( task_view.name() ) );  
        };
#ifdef _DEBUG
        virtual void on_exit( size_t worker_id, tf::TaskView task_view ) override     
        { 
            if( task_view.name().length() == 0 ) return; 
            worker_id; 
            auto context = vaTracer::LocalThreadContext(); 
            vaTracer::LocalThreadContext()->OnEnd( context->MapName( task_view.name() ) );
        };
#else
        virtual void on_exit( size_t worker_id, tf::TaskView task_view ) override
        { 
            if( task_view.name().length() == 0 ) return; worker_id; task_view; vaTracer::LocalThreadContext()->OnEnd( );  
        };
#endif
    };
}

using namespace Vanilla;

vaTF::vaTF( int threadsToUse ) : m_executor( threadsToUse, [] (size_t id) { vaThreading::SetThreadName( vaStringTools::Format( "TaskFlowThread%03d", (uint32)id ) ); } )
{
#if 1 // actually, let's not enable this by default
#ifdef VA_SCOPE_TRACE_ENABLED
    StartTracing();
#endif
#endif
}

vaTF::~vaTF( )
{
    m_executor.wait_for_all();
    if( IsTracing() )
        StopTracing( "" );
}

bool vaTF::StartTracing( )
{
    assert( m_observer == nullptr );
    if( m_observer != nullptr )
        return false;

    m_observer = m_executor.make_observer<vaTFObserver>();
    return true;
}

bool vaTF::StopTracing( string dumpFilePath )
{
    assert( m_observer != nullptr );
    if( m_observer == nullptr )
        return false;

    bool ret = true;

    m_executor.wait_for_all();

//    if( dumpFilePath != "" )
//    { 
//        //std::ofstream ofs( dumpFilePath );
//        std::ostringstream ofs;
//        m_observer->dump( ofs );            // dump the timeline to a JSON file
//        
//        vaFileStream fileOut;
//        if( fileOut.Open( dumpFilePath, FileCreationMode::Create, FileAccessMode::Write ) )
//            fileOut.WriteTXT( ofs.str() );
//        else
//            ret = false;
//    }

    m_executor.remove_observer( m_observer );
    m_observer = nullptr;
    return ret;
}

#endif