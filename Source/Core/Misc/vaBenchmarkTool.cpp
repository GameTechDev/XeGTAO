///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaBenchmarkTool.h"
#include "..\System\vaFileStream.h"
#include "..\vaStringTools.h"

using namespace Vanilla;

vaBenchmarkTool::vaBenchmarkTool( ) : m_currentRunIndex( 0 ), m_currentRunSetupDone( false )
{
    m_active                = false;
    m_timeFromStart         = 0.0f;
    m_currentSampleCount    = 0;
}
vaBenchmarkTool::~vaBenchmarkTool( )
{
    assert( !m_active );
}

bool vaBenchmarkTool::Run(  const std::vector<RunDefinition> & benchmarks )
{
    assert( !m_active );
    if( m_active )
        return false;

    m_benchmarkRuns         = benchmarks;

    m_currentRunIndex       = -1;

    m_active                = true;

    StartNextOrStop();

    m_runStartTime          = std::time( nullptr );

    return true;
}

void vaBenchmarkTool::Tick( float deltaTime )
{
    if( !m_active )
        return;

    if( !m_currentRunSetupDone )
    {
        m_currentRun.SettingsSetupCallback( m_currentRun );
        m_currentRunSetupDone = true;
    }

    m_timeFromStart += deltaTime;

    int sampleCountExpected = vaMath::Min( (int)(m_timeFromStart / m_currentRun.SamplingPeriod), m_currentRun.SamplingTotalCount );
    while( m_currentSampleCount < sampleCountExpected )
    {
        m_currentRun.CollectSamplesCallback( m_currentRun, m_sampleCache );
        assert( m_sampleCache.size() == m_currentRun.MetricNames.size() );  // not allowed to change number of metrics!

        if( m_sampleCache.size( ) != m_currentRun.MetricNames.size( ) )
        {
            Stop();
            return;
        }

        for( size_t i = 0; i < m_sampleCache.size(); i++ )
        {
            m_currentMetricsSampleLog[i].push_back( m_sampleCache[i] );
        }

        m_currentSampleCount++;
    }

    if( m_currentSampleCount >= m_currentRun.SamplingTotalCount )
    {
        assert( m_currentSampleCount == m_currentRun.SamplingTotalCount );
        FinishCurrent();
        StartNextOrStop();
    }
}

void vaBenchmarkTool::StartNextOrStop( )
{
    m_currentRunIndex++;

    if( m_currentRunIndex >= m_benchmarkRuns.size( ) )
        Stop();
    else
    {
        m_currentRun            = m_benchmarkRuns[m_currentRunIndex];
        m_timeFromStart         = -m_currentRun.DelayStartTime;
        m_currentSampleCount    = 0;
        m_currentRunSetupDone   = false;


        m_sampleCache.resize( m_currentRun.MetricNames.size() );
        m_avgMinMaxCache.resize( m_currentRun.MetricNames.size() );
        m_currentMetricsSampleLog.resize( m_currentRun.MetricNames.size() );
    }
}

void vaBenchmarkTool::FinishCurrent( )
{
    bool incorrectSampleCount = m_currentSampleCount != m_currentRun.SamplingTotalCount;
    assert( !incorrectSampleCount );

    if( !incorrectSampleCount )
    { 
        // calculate average/min/mag!
        for( size_t i = 0; i < m_avgMinMaxCache.size(); i++ )
        {
            m_avgMinMaxCache[i].Average = 0.0f;
            m_avgMinMaxCache[i].Minimum = VA_FLOAT_HIGHEST;
            m_avgMinMaxCache[i].Maximum = VA_FLOAT_LOWEST;
            assert( m_currentMetricsSampleLog[i].size() == m_currentSampleCount );
            if( m_currentMetricsSampleLog[i].size() != m_currentSampleCount )
            {
                incorrectSampleCount = true;
                break;
            }
            for( size_t j = 0; j < m_currentMetricsSampleLog[i].size(); j++ )
            {
                m_avgMinMaxCache[i].Average += m_currentMetricsSampleLog[i][j];
                m_avgMinMaxCache[i].Minimum = vaMath::Min( m_avgMinMaxCache[i].Minimum, m_currentMetricsSampleLog[i][j] );
                m_avgMinMaxCache[i].Maximum = vaMath::Max( m_avgMinMaxCache[i].Maximum, m_currentMetricsSampleLog[i][j] );
            }
            m_avgMinMaxCache[i].Average /= (float)m_currentSampleCount;
        }
    }

    // report results
    if( !incorrectSampleCount )
        m_currentRun.FinishedCallback( m_currentRun, m_currentRunIndex, (int)m_benchmarkRuns.size(), m_currentMetricsSampleLog, m_avgMinMaxCache );

    m_currentRun            = RunDefinition();
    m_timeFromStart         = 0.0f;
    m_currentSampleCount    = 0;
    m_currentRunSetupDone   = false;
    m_sampleCache.clear();
    m_currentMetricsSampleLog.clear();
}

void vaBenchmarkTool::Stop( )
{
    if( !m_active )
        return;

    // reset
    m_currentRun = RunDefinition();
    m_timeFromStart         = 0.0f;
    m_currentSampleCount    = 0;
    m_currentRunSetupDone   = false;
    m_active                = false;
}

void vaBenchmarkTool::WriteResultsCSV( const wstring & fileName, bool append, const RunDefinition & runDef, int currentIndex, int totalCount, const std::vector<std::vector<float>>& metricsSamples, const std::vector<AverageMinMax>& metricsAverages )
{
    vaFileStream outFile;
    outFile.Open( fileName, (append)?(FileCreationMode::Append):(FileCreationMode::Create) );

    if( !append )
    {
        outFile.WriteTXT( vaCore::GetCPUIDName() );
        outFile.WriteTXT( "\r\n" );
    }

    outFile.WriteTXT( "\r\n" );

    // first row (info)
    outFile.WriteTXT( vaStringTools::Format( "\r\nBenchmark run %d of %d; name: '%s'", currentIndex, totalCount, runDef.Name.c_str() ) );
    if( runDef.LongInfo.size() > 0 )
        outFile.WriteTXT( vaStringTools::Format( "; details: '%s'", runDef.Name.c_str() ) );

    // outFile.WriteTXT( "\r\n" );
    // outFile.WriteTXT( "\r\n" );
    outFile.WriteTXT( ", " );

    // column titles
    for( size_t i = 0; i < runDef.MetricNames.size(); i++ )
        outFile.WriteTXT( runDef.MetricNames[i] + ", " );

    outFile.WriteTXT( "\r\n" );

    // all samples
    if( metricsSamples.size() > 0 )
    {
        for( int j = 0; j < runDef.SamplingTotalCount; j++ )
        {
            outFile.WriteTXT( vaStringTools::Format( "%05d,      ", j ) );
            for( size_t i = 0; i < metricsSamples.size(); i++ )
                outFile.WriteTXT( vaStringTools::Format( "%.2f, ", metricsSamples[i][j] ) );

            outFile.WriteTXT( "\r\n" );
        }
    }

    // averages
    if( metricsAverages.size() > 0 )
    { 
        outFile.WriteTXT( "averages, " );
        for( size_t i = 0; i < metricsAverages.size(); i++ )
            outFile.WriteTXT( vaStringTools::Format( "%.2f, ", metricsAverages[i].Average ) );
        outFile.WriteTXT( "\r\n minimums, " );
        for( size_t i = 0; i < metricsAverages.size(); i++ )
            outFile.WriteTXT( vaStringTools::Format( "%.2f, ", metricsAverages[i].Minimum ) );
        outFile.WriteTXT( "\r\n maximums, " );
        for( size_t i = 0; i < metricsAverages.size(); i++ )
            outFile.WriteTXT( vaStringTools::Format( "%.2f, ", metricsAverages[i].Maximum ) );
        outFile.WriteTXT( "\r\n" );
    }

    //outFile.WriteTXT( )
}
