///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Core/vaCoreIncludes.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Adapted to C++ by Filip Strugar, 2011, based on C# code by Renaud Bedard
// http://theinstructionlimit.com/?p=417
//
// Adapted from java source by Herman Tulleken
// http://www.luma.co.za/labs/2008/02/27/poisson-disk-sampling/
//
// The algorithm is from the "Fast Poisson Disk Sampling in Arbitrary Dimensions" paper by Robert Bridson
// http://www.cs.ubc.ca/~rbridson/docs/bridson-siggraph07-poissondisk.pdf

namespace Vanilla
{
    class vaPoissonDiskGenerator
    {
    private:
        struct Settings
        {
            vaVector2   TopLeft;
            vaVector2   LowerRight;
            vaVector2   Center;
            vaVector2   Dimensions;
            float       RejectionSqDistance;
            float       MinimumDistance;
            float       CellSize;
            int         MaxDecimals;
            int         GridWidth;
            int         GridHeight;
            bool        FirstPointAtCenter;
        };

        struct State
        {
            const Settings      CurrentSettings;

            vaVector2 * const   Grid;

            std::vector<vaVector2>   ActivePoints;
            std::vector<vaVector2>   Points;
            vaRandom            Random;

            explicit State( const Settings & settings ) : CurrentSettings( settings ), Grid( new vaVector2[ settings.GridWidth * settings.GridHeight ] ), Random( 0 )
            {
                int res = s_lastRandomSeed.fetch_add( 1 ); // vaThread::Interlocked_Increment( &s_lastRandomSeed );
                Random.Seed( res );

                for( size_t i = 0; i < CurrentSettings.GridWidth * CurrentSettings.GridHeight; i++ )
                    Grid[i] = vaVector2( VA_FLOAT_HIGHEST, VA_FLOAT_HIGHEST );
            }
            State( const State & ) = delete;

            ~State( )
            {
                delete[] Grid;
            }

            vaVector2 &         GridAt( int x, int y )
            {
                assert( x >= 0 && x < CurrentSettings.GridWidth );
                assert( y >= 0 && y < CurrentSettings.GridHeight );
                return Grid[ x + y * CurrentSettings.GridWidth ];
            }

        private:
            // State( ) { assert( false ); }
            // State( const State & copy ) { assert( false ); }
        };

        struct SearchGlobalThreadsState
        {
            mutex               Mutex;
            
            std::vector<vaVector2>   GlobalBest;
            std::vector<int>         AllCountsSoFar;
            int                 SearchTarget;
            bool                Break;
            int                 RemainingJobCount;

            explicit SearchGlobalThreadsState( int jobCount ) : RemainingJobCount( jobCount ) { SearchTarget = -1; Break = false; }
        };

        struct SearchThreadState
        {
            SearchGlobalThreadsState &  GlobalState;
            vaVector2                   Center;
            float                       Radius;
            float                       MinimumDistance;
            int                         PointsPerIteration;
            int                         MaxDecimals;
            bool                        FirstPointAtCenter;

            SearchThreadState( SearchGlobalThreadsState & globalState, vaVector2 center, float radius, float minimumDistance, int pointsPerIteration, int maxDecimals, bool firstPointAtCenter )
                : GlobalState( globalState )
            {
                Center                  = center;
                Radius                  = radius;
                MinimumDistance         = minimumDistance;
                PointsPerIteration      = pointsPerIteration;
                MaxDecimals             = maxDecimals;
                FirstPointAtCenter      = firstPointAtCenter;
            }
        };

    public:
        static const int                    c_DefaultPointsPerIteration = 30;
        
        static std::atomic_int32_t          s_lastRandomSeed;

    private:
        vaPoissonDiskGenerator( )           { }
        ~vaPoissonDiskGenerator( )          { }

    public:
        static void SampleCircle( vaVector2 center, float radius, float minimumDistance, std::vector<vaVector2> & outResults )
        {
            return SampleCircle( center, radius, minimumDistance, c_DefaultPointsPerIteration, 16, false, outResults );
        }
        static void SampleCircle( vaVector2 center, float radius, float minimumDistance, int pointsPerIteration, int maxDecimals, bool firstPointAtCenter, std::vector<vaVector2> & outResults )
        {
            return Sample( center - vaVector2(radius, radius), center + vaVector2(radius, radius), radius, minimumDistance, pointsPerIteration, maxDecimals, firstPointAtCenter, outResults );
        }

        static void SampleRectangle( vaVector2 topLeft, vaVector2 lowerRight, float minimumDistance, std::vector<vaVector2> & outResults )
        {
            return SampleRectangle( topLeft, lowerRight, minimumDistance, c_DefaultPointsPerIteration, outResults );
        }
        static void SampleRectangle( vaVector2 topLeft, vaVector2 lowerRight, float minimumDistance, int pointsPerIteration, std::vector<vaVector2> & outResults )
        {
            return Sample( topLeft, lowerRight, 0.0f, minimumDistance, pointsPerIteration, 16, false, outResults );
        }

        static void Sample( vaVector2 topLeft, vaVector2 lowerRight, float rejectionDistance, float minimumDistance, int pointsPerIteration, int maxDecimals, bool firstPointAtCenter, std::vector<vaVector2> & outResults )
	    {
            Settings settings;

            settings.TopLeft                = topLeft;
            settings.LowerRight             = lowerRight;
            settings.Dimensions             = lowerRight - topLeft;
            settings.Center                 = (topLeft + lowerRight) * 0.5f;
            settings.CellSize               = minimumDistance / vaMath::Sqrt( 2.0f );
            settings.MinimumDistance        = minimumDistance;
            settings.RejectionSqDistance    = rejectionDistance * rejectionDistance;
            settings.MaxDecimals            = maxDecimals;
            settings.FirstPointAtCenter     = firstPointAtCenter;

            settings.GridWidth              = (int) (settings.Dimensions.x / settings.CellSize) + 1;
            settings.GridHeight             = (int) (settings.Dimensions.y / settings.CellSize) + 1;

            State state( settings );

		    AddFirstPoint( state );

            while( state.ActivePoints.size() != 0 )
		    {
                int listIndex = state.Random.NextIntRange( 0, (int)state.ActivePoints.size() );

                vaVector2 point = state.ActivePoints[listIndex];
			    bool found = false;

                for( int k = 0; k < pointsPerIteration; k++ )
				    found |= AddNextPoint( point, state );

			    if( !found )
                {
                    state.ActivePoints.erase( state.ActivePoints.begin() + listIndex );
                }
		    }

            outResults = state.Points;
	    }

        static void SearchCircleByParams( vaVector2 center, float radius, int searchTarget, bool firstPointAtCenter, bool deleteCenterPoint, std::vector<vaVector2> & outResults, float & outMinDistance );

    private:

        static vaVector2i Denormalize( vaVector2 point, vaVector2 origin, double cellSize )
        {
            return vaVector2i( (int) ((point.x - origin.x) / cellSize), (int) ((point.y - origin.y) / cellSize) );
        }

        static void AddFirstPoint( State & state )
        {
            bool added = false;
            while( !added )
            {
                vaVector2 p;
                if( state.CurrentSettings.FirstPointAtCenter )
                {
                   p.x = state.CurrentSettings.TopLeft.x + 0.5f * state.CurrentSettings.Dimensions.x;
                   p.y = state.CurrentSettings.TopLeft.y + 0.5f * state.CurrentSettings.Dimensions.y;
                }
                else
                {
                   float d;
                   d = state.CurrentSettings.Dimensions.x * state.Random.NextFloat();
                   //d = Math.Round( d, state.CurrentSettings.MaxDecimals );
                   p.x = state.CurrentSettings.TopLeft.x + d;

                   d = state.CurrentSettings.Dimensions.y * state.Random.NextFloat();
                   //d = Math.Round( d, state.CurrentSettings.MaxDecimals );
                   p.y = state.CurrentSettings.TopLeft.y + d;

                   if( state.CurrentSettings.RejectionSqDistance != 0.0f && ( ( state.CurrentSettings.Center - p ).LengthSq() > state.CurrentSettings.RejectionSqDistance ) )
                      continue;
                }
                added = true;

                vaVector2i index = Denormalize(p, state.CurrentSettings.TopLeft, state.CurrentSettings.CellSize);

                state.GridAt( index.x, index.y ) = p;

                state.ActivePoints.push_back(p);
                state.Points.push_back(p);
            } 
        }

        static bool AddNextPoint( vaVector2 point, State & state )
	    {
		    bool found = false;
            vaVector2 q = GenerateRandomAround( point, state.CurrentSettings.MinimumDistance, state.CurrentSettings.MaxDecimals, state );

            if (q.x >= state.CurrentSettings.TopLeft.x && q.x < state.CurrentSettings.LowerRight.x && 
                q.y > state.CurrentSettings.TopLeft.y && q.y < state.CurrentSettings.LowerRight.y &&
                ((state.CurrentSettings.RejectionSqDistance == 0) || ( (state.CurrentSettings.Center - q ).LengthSq() <= state.CurrentSettings.RejectionSqDistance) ) )
		    {
                vaVector2i qIndex = Denormalize(q, state.CurrentSettings.TopLeft, state.CurrentSettings.CellSize);
			    bool tooClose = false;

                for( int i = (int)vaMath::Max(0, qIndex.x - 2); i < vaMath::Min(state.CurrentSettings.GridWidth, qIndex.x + 3) && !tooClose; i++ )
                    for( int j = (int)vaMath::Max(0, qIndex.y - 2); j < vaMath::Min(state.CurrentSettings.GridHeight, qIndex.y + 3) && !tooClose; j++ )
                    {
                        // empty grid, early out
                        if( state.GridAt(i, j).x == VA_FLOAT_HIGHEST )
                            continue;

                        if( ( state.GridAt(i, j) - q).Length() < state.CurrentSettings.MinimumDistance )
							tooClose = true;
                    }

			    if( !tooClose )
			    {
				    found = true;
				    state.ActivePoints.push_back(q);
				    state.Points.push_back(q);
                    state.GridAt(qIndex.x, qIndex.y) = q;
			    }
		    }
		    return found;
	    }

        static vaVector2 GenerateRandomAround( vaVector2 center, float minimumDistance, int maxDecimals, State & state )
        {
            float d = state.Random.NextFloat();
            float radius = minimumDistance + minimumDistance * d;

            d = state.Random.NextFloat();
            float angle = VA_PIf * 2.0f * d;

            float newX = radius * vaMath::Sin( angle );
            float newY = radius * vaMath::Cos( angle );
            
            maxDecimals; // not sure why I removed this - maybe it was incorrect
            //newX = vaMath::Round( newX, maxDecimals );
            //newY = vaMath::Round( newY, maxDecimals );

            return vaVector2( (float) (center.x + newX), (float) (center.y + newY) );
        }

        static void PoissonThreadProc( void * threadParam );

    };


}
