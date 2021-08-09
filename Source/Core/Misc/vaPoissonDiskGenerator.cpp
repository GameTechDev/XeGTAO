///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaPoissonDiskGenerator.h"

// #include "Core/System/vaThreadPool.h"

using namespace Vanilla;

std::atomic_int32_t vaPoissonDiskGenerator::s_lastRandomSeed = 0;

void vaPoissonDiskGenerator::PoissonThreadProc( void * threadParam )
{
    SearchThreadState * ptsPtr = static_cast<SearchThreadState*>( threadParam );
    SearchThreadState & pts = *ptsPtr;

    {
        std::lock_guard<mutex> lock( pts.GlobalState.Mutex );
   
        if( pts.GlobalState.Break )
        {
            pts.GlobalState.RemainingJobCount--;
            delete ptsPtr;
            return;
        }
    }

    std::vector<vaVector2> points;
    
    vaPoissonDiskGenerator::SampleCircle( pts.Center, pts.Radius, pts.MinimumDistance, pts.PointsPerIteration, pts.MaxDecimals, pts.FirstPointAtCenter, points );

    {
        std::lock_guard<mutex> lock( pts.GlobalState.Mutex );

        if( pts.GlobalState.Break )
        {
            pts.GlobalState.RemainingJobCount--;
            delete ptsPtr;
            return;
        }

        if( pts.GlobalState.SearchTarget == -1 )
        {
            if( points.size() > pts.GlobalState.GlobalBest.size() )
                pts.GlobalState.GlobalBest = points;
        }
        else
        {
            if( points.size() == pts.GlobalState.SearchTarget )
            {
                pts.GlobalState.Break = true;
                pts.GlobalState.GlobalBest = points;
            }
        }

        pts.GlobalState.AllCountsSoFar.push_back( (int)points.size() );
        
        pts.GlobalState.RemainingJobCount--;
    }

    delete ptsPtr;
}


void vaPoissonDiskGenerator::SearchCircleByParams( vaVector2 center, float radius, int searchTarget, bool firstPointAtCenter, bool deleteCenterPoint, std::vector<vaVector2> & outResults, float & outMinDistance )
{
//    const int jobThreadCount = (int)vaMath::Min( vaThreadPool::GetInstance().GetThreadCount(), vaThreadPool::c_maxPossibleThreads );

    //vaThreadPool::GetInstance().AddJob( doChunk, j );

    if( !firstPointAtCenter )
        deleteCenterPoint = false;

    if( deleteCenterPoint )
        searchTarget++;

    int finalPointsPerIteration     = 0;

    int         pointsPerIteration  = (int)searchTarget / 3 + 1;
    float       currentMinDistance  = 0.4f;
    float       minDistModifier     = 0.3f;

    bool        lastDirectionUp     = false;

    int failsafeSearchIterationCount = 1000;

    for( int ll = 0; ll < failsafeSearchIterationCount; ll++ )
    {
        const int parallelIterationsPerStep = 32;

        SearchGlobalThreadsState globalThreadsState( parallelIterationsPerStep );

        globalThreadsState.SearchTarget = searchTarget;

        for( int i = 0; i < parallelIterationsPerStep; i++ )
        {
            SearchThreadState * sts = new SearchThreadState( globalThreadsState, center, radius, currentMinDistance, pointsPerIteration, 7, firstPointAtCenter );
            // vaThreadPool::GetInstance().AddJob( PoissonThreadProc, (void*) sts );
            PoissonThreadProc( (void*) sts );
        }

        for( ;; )
        {
            globalThreadsState.Mutex.lock();
            if( globalThreadsState.RemainingJobCount == 0 )
            {
                globalThreadsState.Mutex.unlock();
                break;
            }
            globalThreadsState.Mutex.unlock();

            vaThreading::YieldProcessor();
        }

        if( (int)globalThreadsState.GlobalBest.size() == globalThreadsState.SearchTarget )
        {
            // found it, exit!
            outResults                      = globalThreadsState.GlobalBest;
            outMinDistance                  = currentMinDistance;
            finalPointsPerIteration         = pointsPerIteration;
            break;
        }
        else
        {
            int countBelow = 0;
            int countAbove = 0;
            int totalCount = (int)globalThreadsState.AllCountsSoFar.size();
            assert( totalCount == parallelIterationsPerStep );
            for( int i = 0; i < totalCount; i++ )
            {
                if( globalThreadsState.AllCountsSoFar[i] > globalThreadsState.SearchTarget )
                    countAbove++;
                else
                    countBelow++;
                assert( globalThreadsState.AllCountsSoFar[i] != globalThreadsState.SearchTarget );
            }
            float ratio = (float)(countAbove - countBelow) / (float)totalCount;

            // if wildly changing direction of search, slightly reduce the distance modifier
            bool newDirectionUp = ratio > 0;
            if( lastDirectionUp != newDirectionUp )
            {
                lastDirectionUp = newDirectionUp;
                if( vaMath::Abs( ratio ) > 0.9f )
                    minDistModifier *= 0.7f;
            }

            float finalModifier = 1.0f + minDistModifier * ratio;
            currentMinDistance *= finalModifier;
        }

    }
    
    if( deleteCenterPoint && outResults.size() > 0 )
    {
        outResults.erase( outResults.begin() + 0 );
    }
    // if( checkBoxDeleteCenterPoint.Checked )
    // {
    //     CurrentPoints.RemoveAll( x => ( x.LengthSq( ) < 0.00001f ) );
    // }
    // 
    // if( checkBoxSortCW.Checked )
    // {
    //     CurrentPoints.Sort( ( x, y ) => ( AngleAroundZero( x ).CompareTo( AngleAroundZero( y ) ) ) );
    // }

}
