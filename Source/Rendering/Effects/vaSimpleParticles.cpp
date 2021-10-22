///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if 0

#include "Rendering/Effects/vaSimpleParticles.h"

#include "Rendering/vaRenderDeviceContext.h"

#include "Rendering/vaDebugCanvas.h"

#include "Rendering/vaRenderMaterial.h"

#include "Rendering/Shaders/vaSharedTypes.h"

#include "Rendering/vaTriangleMesh.h"
#include "Rendering/vaTexture.h"


#ifdef VA_GTS_INTEGRATION_ENABLED
#define USE_MULTITHREADED_PARTICLES_WITH_GTS
#endif

#ifdef USE_MULTITHREADED_PARTICLES_WITH_GTS
#include "IntegratedExternals/vaGTSIntegration.h"
#endif

#ifdef VA_TASKFLOW_INTEGRATION_ENABLED
#ifndef USE_MULTITHREADED_PARTICLES_WITH_GTS
#define USE_MULTITHREADED_PARTICLES_WITH_TF
#endif
#endif

#ifdef USE_MULTITHREADED_PARTICLES_WITH_TF
#include "IntegratedExternals/vaTaskflowIntegration.h"
#endif

#include <functional>

using namespace Vanilla;


//#pragma optimize( "gxy", on )

vaSimpleParticleSystem::vaSimpleParticleSystem( const vaRenderingModuleParams & params ) : 
    vaRenderingModule( params ),
    m_vertexShader( params ),
    m_constantBuffer( vaConstantBuffer::Create<ShaderInstanceConstants>( params.RenderDevice, "ShaderInstanceConstants" ) )

{ 
    delegate_particlesTickShader.AddWithToken( m_aliveToken, this, &vaSimpleParticleSystem::DefaultParticlesTickShader );
    delegate_drawBufferUpdateShader.AddWithToken( m_aliveToken, this, &vaSimpleParticleSystem::DefaultDrawBufferUpdateShader );

    m_lastTickEmitterCount              = 0;
    m_lastTickParticleCount             = 0;
    m_debugParticleDrawCountLimit       = -1;

    //m_lastParticleID    = (uint32)-1;
    m_lastEmitterID     = (uint32)-1;

    m_sortedAfterTick = false;

    m_lastTickID = -1;

    m_boundingBox = vaBoundingBox::Degenerate;

    m_dynamicBuffer = vaDynamicVertexBuffer::Create<vaBillboardSprite>( params.RenderDevice, m_dynamicBufferMaxElementCount, nullptr, "ParticlesVertexBuffer" );

    // default material
    {
        m_defaultMaterial = GetRenderDevice().GetMaterialManager().CreateRenderMaterial();

        // this is to fake in some subsurface scattering
        m_defaultMaterial->SetInputSlot( "SubsurfaceScatterHack", 0.3f, false, false );

        auto matSettings = m_defaultMaterial->GetMaterialSettings();
        auto shSettings = m_defaultMaterial->GetShaderSettings();

        matSettings.FaceCull                = vaFaceCull::None;
        //matSettings.AdvancedSpecularShader  = false;
        //matSettings.ReceiveShadows          = false; //?
        matSettings.LayerMode = vaLayerMode::Transparent;
        matSettings.AlphaTestThreshold      = 0.005f;

        //shSettings.VS_Standard              = std::make_pair( "vaSimpleParticles.hlsl",   "SimpleParticleVS"     );   <- VS not required for particles, handled by the vaSimpleParticles
        shSettings.GS_Standard              = std::make_pair( "vaSimpleParticles.hlsl", "SimpleParticleGS" );
        shSettings.PS_Forward               = std::make_pair( "vaSimpleParticles.hlsl", "SimpleParticlePS" );

        shSettings.BaseMacros               = { {"SIMPLE_PARTICLES_NO_LIGHTING", "1" } };

        //shSettings.PS_Forward ...
        m_defaultMaterial->SetMaterialSettings(matSettings);
        m_defaultMaterial->SetShaderSettings(shSettings);
    }

}

void vaSimpleParticleSystem::ReleaseEmitterPtr( std::shared_ptr<vaSimpleParticleEmitter> & ptr )
{ 
    assert( ptr.use_count() == 1 ); // this assert is not entirely correct for multithreaded scenarios so beware
    if( ptr.use_count() == 1 ) // this assert is not entirely correct for multithreaded scenarios so beware
    {
        m_unusedEmittersPool.push_back( ptr );
    }
    ptr = NULL;
}

void vaSimpleParticleEmitter::DefaultEmitterTickShader( vaSimpleParticleSystem & psys, vaSimpleParticleEmitter & emitter, std::vector<vaSimpleParticle> & allParticles, float deltaTime )
{
    psys; // unreferenced
    // Warning: emitter is only allowed to push back new particles in allParticles, never remove or swap them!

    emitter.RemainingEmitterLife -= deltaTime;

    if( emitter.RemainingEmitterLife < 0 )
        return;

    emitter.TimeAccumulatedSinceLastSpawn += deltaTime;

    const int maxSpawnPerFrame = 512 * 1024;
    int numberToSpawn = vaMath::Clamp( (int)(emitter.TimeAccumulatedSinceLastSpawn * emitter.Settings.SpawnFrequencyPerSecond), 0, maxSpawnPerFrame );

    if( (numberToSpawn > 0) && (emitter.Settings.SpawnFrequencyPerSecond > 0.0f) )
        emitter.TimeAccumulatedSinceLastSpawn -= (float)numberToSpawn / emitter.Settings.SpawnFrequencyPerSecond;

    if( emitter.RemainingEmitterParticleCount != INT_MAX )
        emitter.RemainingEmitterParticleCount -= numberToSpawn;

    for( int j = 0; j < numberToSpawn; j++ )
    {
        allParticles.push_back( vaSimpleParticle() );
        vaSimpleParticle & newParticle = allParticles.back();

        emitter.LastParticleID++;
        //emitter.LastParticleID &= 0xFFFF;

        newParticle.CreationID          = emitter.LastParticleID; //(psys.LastParticleID() & 0x00FFFFFF) | ((emitter.CreationID & 0xFF) << 24);
        newParticle.LifeRemaining       = vaMath::Max( 0.0f, emitter.Settings.SpawnLife + (emitter.Settings.SpawnLifeRandomAddSub )    * vaRandom::Singleton.NextFloatRange( -1.0f, 1.0f ) );
        newParticle.LifeStart           = newParticle.LifeRemaining;

        newParticle.Size                = vaMath::Max( 0.0f, emitter.Settings.SpawnSize + (emitter.Settings.SpawnSizeRandomAddSub)    * vaRandom::Singleton.NextFloatRange( -1.0f, 1.0f ) );
        newParticle.SizeChange          = emitter.Settings.SpawnSizeChange + (emitter.Settings.SpawnSizeChangeRandomAddSub)           * vaRandom::Singleton.NextFloatRange( -1.0f, 1.0f );

        newParticle.Angle               = emitter.Settings.SpawnAngle + (emitter.Settings.SpawnAngleRandomAddSub)                     * vaRandom::Singleton.NextFloatRange( -1.0f, 1.0f );
        newParticle.AngularVelocity     = emitter.Settings.SpawnAngularVelocity + (emitter.Settings.SpawnAngularVelocityRandomAddSub) * vaRandom::Singleton.NextFloatRange( -1.0f, 1.0f );

        newParticle.AffectedByGravityK  = emitter.Settings.SpawnAffectedByGravityK;
        newParticle.AffectedByWindK     = emitter.Settings.SpawnAffectedByWindK;

        if( emitter.Settings.SpawnAreaType == vaSimpleParticleEmitter::SAT_BoundingBox )
        {
            newParticle.Position        = emitter.Settings.SpawnAreaBoundingBox.RandomPointInside( vaRandom::Singleton );
        }
        else if( emitter.Settings.SpawnAreaType == vaSimpleParticleEmitter::SAT_BoundingSphere )
        {
            newParticle.Position        = emitter.Settings.SpawnAreaBoundingSphere.RandomPointInside( vaRandom::Singleton );
        }
        else
        {
            assert( false );
        }
        newParticle.Velocity        = emitter.Settings.SpawnVelocity + emitter.Settings.SpawnVelocityRandomAddSub * ( vaVector3::Random() * 2.0f - 1.0f );
        newParticle.Color           = emitter.Settings.SpawnColor + emitter.Settings.SpawnColorRandomAddSub * ( vaVector4::Random()  * 2.0f - 1.0f );
    }
}

void vaSimpleParticleSystem::DefaultParticlesTickShader( vaSimpleParticleSystem & psys, std::vector<vaSimpleParticle> & allParticles, float deltaTime )
{
    psys;

    // Warning: particle shader is NOT allowed to push back new particles to allParticles, remove them or reorder them!
    if( deltaTime == 0.0f )
        return;
    
    const int loopLength = (int)allParticles.size( );
    if( loopLength == 0 )
        return;

    auto elementTickProc = [&psys, &deltaTime, &settings = m_settings, &allParticles]( int index )
        {
            vaSimpleParticle & particle = allParticles[index];
            particle.LifeRemaining -= deltaTime;

            particle.Position += particle.Velocity * deltaTime;
            particle.Angle = vaMath::AngleWrap( particle.Angle + particle.AngularVelocity * deltaTime );
            particle.Size = vaMath::Max( 0.0f, particle.Size + particle.SizeChange * deltaTime );

            particle.Velocity += particle.AffectedByGravityK * settings.Gravity * deltaTime;
            particle.Velocity += particle.AffectedByWindK * settings.Wind * deltaTime;
            particle.Velocity = vaMath::Lerp( particle.Velocity, vaVector3( 0.0f, 0.0f, 0.0f ), vaMath::TimeIndependentLerpF( deltaTime, settings.VelocityDamping ) );

            particle.AngularVelocity = vaMath::Lerp( particle.AngularVelocity, 0.0f, vaMath::TimeIndependentLerpF( deltaTime, settings.AngularVelocityDamping ) );
        };

#ifdef USE_MULTITHREADED_PARTICLES_WITH_GTS

    gts::ParallelFor parallelFor(vaGTS::GetInstance().Scheduler());
    parallelFor(allParticles.begin(), allParticles.end(),
        [&elementTickProc](auto particleIter) { elementTickProc( *particleIter ); } );

#elif defined( USE_MULTITHREADED_PARTICLES_WITH_TF )
    vaTF::parallel_for( 0, (int)allParticles.size(), elementTickProc , 64 ).wait();
#else

    for( int i = 0; i < loopLength; i++ )
        elementTickProc( allParticles[i] );

#endif
}

void vaSimpleParticleSystem::Sort(const vaVector3& _cameraPos, bool backToFront)
{
    VA_TRACE_CPU_SCOPE( vaSimpleParticleSystem_Sort );

    m_sortedAfterTick = true;

    vaVector3 cameraPos = _cameraPos;

    const std::vector<vaSimpleParticle>& allParticles = m_particles;
    std::vector< float >& sortValues = m_particleSortValueCache;
    std::vector< int >& sortedIndices = m_particleSortedIndices;

    if (allParticles.size() == 0)
        return;

    if (allParticles.size() > sortValues.size())
        sortValues.resize(allParticles.size());
    if (m_particles.size() > m_particleSortedIndices.size())
        m_particleSortedIndices.resize(m_particles.size());

    // to disable sort
#if 0
    {
        for( size_t i = 0; i < allParticles.size( ); i++ )
        {
            sortedIndices[i] = (int)i;
        }
        m_sortedAfterTick = true;
        return;
    }
#endif


    float backToFrontMultiplier = ( backToFront ) ? ( 1.0f ) : ( -1.0f );

    struct MySortComparerBackToFront
    {
        const std::vector< float >& SortValues;
        MySortComparerBackToFront(const std::vector< float >& sortValues) : SortValues(sortValues)
        {}
        bool operator() (int i, int j)
        {
            return (SortValues[i] > SortValues[j]);
        }
    } mySortComparerBackToFront(sortValues);

    const int totalParticleCount = (int)allParticles.size();

    const int recursionDepth    = 5;    // for max 1 << recursionDepth parallel jobs in the beginning
    const int leafBucketCount   = 1 << recursionDepth;
    int bucketSize              = ( totalParticleCount + leafBucketCount - 1 ) / leafBucketCount;
    int     sortBucketBoundaries[leafBucketCount + 1];
    std::atomic_int32_t readinessFlags[ leafBucketCount * recursionDepth ];

    assert( vaMath::IsPowOf2(leafBucketCount) );

    for( int bucketIndex = 0; bucketIndex <= leafBucketCount; bucketIndex++ )
    {
        sortBucketBoundaries[ bucketIndex ] = vaMath::Min( ( bucketIndex ) * bucketSize, totalParticleCount );
    }
    for( int level = 0; level < recursionDepth; level++ )
        for( int bucketIndex = 0; bucketIndex < leafBucketCount; bucketIndex++ )
        {
            readinessFlags[ level * leafBucketCount + bucketIndex ] = 0;
        }

    std::function< void(int, int) > mergeArea = [&](int level, int bucketID)
    {
        int currentStep = 1 << level;
        //int currentCount = leafBucketCount / currentStep;

        int left        = bucketID * currentStep;
        int middle      = left + currentStep/2;
        int right       = left + currentStep;
        int indexLeft   = sortBucketBoundaries[left];
        int indexMiddle = sortBucketBoundaries[middle];
        int indexRight  = sortBucketBoundaries[right];

        std::inplace_merge( sortedIndices.begin( ) + indexLeft, sortedIndices.begin( ) + indexMiddle, sortedIndices.begin( ) + indexRight, mySortComparerBackToFront );

        if( level == recursionDepth )
            return;

        // no need to spawn further tasks recursively since only one out of two will result in new compute we can just reuse this one 
        if( readinessFlags[level * leafBucketCount + bucketID / 2].fetch_add(1) == 1 )
        {
            //int level = 0;
            mergeArea(level + 1, bucketID / 2);
        }

    };

    auto doChunk = [&](int bucketIndex)
    {
        const int startFrom = sortBucketBoundaries[bucketIndex];
        const int stopAt    = sortBucketBoundaries[bucketIndex + 1];
        for (int i = startFrom; i < stopAt; i++)
        {
            sortValues[i] = backToFrontMultiplier * (allParticles[i].Position - cameraPos).Length();
            sortedIndices[i] = (int)i;
        }
        std::sort(sortedIndices.begin() + startFrom, sortedIndices.begin() + stopAt, mySortComparerBackToFront);

        int level = 0;

        if (readinessFlags[level * leafBucketCount + bucketIndex / 2].fetch_add(1) == 1)
        {
            //int level = 0;
            mergeArea(1, bucketIndex / 2);
        }
    };

#ifdef USE_MULTITHREADED_PARTICLES_WITH_GTS
    
    gts::ParallelFor parallelFor(vaGTS::GetInstance().Scheduler());

    parallelFor(0, leafBucketCount, doChunk );

#elif defined( USE_MULTITHREADED_PARTICLES_WITH_TF )

    tf::Taskflow taskflow("parallel_sort");

#if 0
    taskflow.parallel_for( 0, leafBucketCount, 1, doChunk );
    vaTF::GetInstance().Executor().run( taskflow ).wait();
#else
    // this version should in theory schedule better but leave it unused for closer GTS vs TF comparison
    std::function< tf::Task(int, int) > recursiveTaskMaker = [&]( int beginIndex, int endIndex )
    {
        const int leafSizeMax = 1024;
        const int count = endIndex-beginIndex;
        if( count < 2 )
            return taskflow.placeholder().name("empty");

        // if the count is smaller than x, just compute distances & sort directly
        if( count < leafSizeMax )
        {
            return taskflow.emplace( [beginIndex, endIndex, &allParticles, backToFrontMultiplier, cameraPos, &sortValues, &sortedIndices, &mySortComparerBackToFront]()
            { // lambda that does the actual distance compute and sort on other threads
                for( int i = beginIndex; i < endIndex; i++ )
                {
                    sortValues[i] = backToFrontMultiplier * ( allParticles[i].Position - cameraPos ).Length( );
                    sortedIndices[i] = (int)i;
                }
                std::sort( sortedIndices.begin( ) + beginIndex, sortedIndices.begin( ) + endIndex, mySortComparerBackToFront );
            } ).name( "sort" );
        }
        else // divide into halves, let them each (L, R) sort their parts and then merge-sort them
        {
            int halfWay = beginIndex + (count+1)/2;
            tf::Task L = recursiveTaskMaker( beginIndex, halfWay );
            tf::Task R = recursiveTaskMaker( halfWay, endIndex );
            tf::Task M = taskflow.emplace( [ beginIndex, halfWay, endIndex, &sortedIndices, &mySortComparerBackToFront ]( )
            { // lambda that does the actual sort on other threads
                std::inplace_merge( sortedIndices.begin( ) + beginIndex, 
                    sortedIndices.begin( ) + halfWay, sortedIndices.begin( ) + endIndex, mySortComparerBackToFront );
            } );
            M.succeed( L, R ).name( "merge" );
            return M;
        }
    };
    auto finalTask = recursiveTaskMaker( 0, totalParticleCount );
    finalTask.name( "final_merge" );
//    string dump = taskflow.dump();
    vaTF::Executor( ).run( taskflow ).wait( );
#endif

#else

    for (int t = 0; t < leafBucketCount; t++)
        doChunk( t );

#endif

    // just plain old sort for testing the correctness of above
#if 0
    std::vector< int > testParticleSortedIndices;
    testParticleSortedIndices.resize( m_particles.size( ) );
    {
        //float distMax = VA_FLOAT_LOWEST;
        //float distMin = VA_FLOAT_HIGHEST;
        for( size_t i = 0; i < allParticles.size( ); i++ )
        {
            sortValues[i] = backToFrontMultiplier * ( allParticles[i].Position - cameraPos ).Length( );
            testParticleSortedIndices[i] = (int)i;
            //distMax = vaMath::Max( distMax, sortValues[i] );
            //distMin = vaMath::Min( distMin, sortValues[i] );
        }

        std::sort( testParticleSortedIndices.begin( ), testParticleSortedIndices.begin() + allParticles.size(), mySortComparerBackToFront );
        m_sortedAfterTick = true;
    }
    for( size_t i = 0; i < testParticleSortedIndices.size(); i++ )
    {
        if( m_particleSortedIndices[i] != testParticleSortedIndices[i] )
        {
            float l = sortValues[m_particleSortedIndices[i]];
            float r = sortValues[testParticleSortedIndices[i]];
            assert( l == r );
        }
    }
    m_particleSortedIndices = testParticleSortedIndices;
#endif
}

void vaSimpleParticleSystem::DefaultDrawBufferUpdateShader( const vaSimpleParticleSystem &, const std::vector<vaSimpleParticle> & allParticles, const std::vector< int > & sortedIndices, void * outDestinationBuffer, size_t inDestinationBufferSize )
{
    vaBillboardSprite * particleBuffer = (vaBillboardSprite *)outDestinationBuffer;

    if( inDestinationBufferSize != sizeof(vaBillboardSprite) * allParticles.size() )
    {
        // buffer size mismatch? something is wrong
        assert( false );
        return;
    }
    
    const int loopLength = (int)allParticles.size( );
    if( loopLength == 0 )
        return;

    auto elementTickProc = [&allParticles, &sortedIndices, particleBuffer, this](int i)
        {
            const vaSimpleParticle & particle = allParticles[sortedIndices[i]];
            vaBillboardSprite & outVertex = particleBuffer[i];

            outVertex.Position_CreationID = vaVector4( particle.Position, *((float*)&particle.CreationID) );

            vaVector4 color = particle.Color;

            color.w *= vaMath::Saturate( 1.0f - ( ( 1.0f - particle.LifeRemaining / particle.LifeStart ) - m_settings.FadeAlphaFrom ) / ( 1.0f - m_settings.FadeAlphaFrom ) );

            // outVertex.Color = vaVector4::ToRGBA( color );
            outVertex.Color = color;

            float ca = vaMath::Cos( particle.Angle );
            float sa = vaMath::Sin( particle.Angle );

            outVertex.Transform2D.x =  ca * particle.Size;
            outVertex.Transform2D.y = -sa * particle.Size;
            outVertex.Transform2D.z =  sa * particle.Size;
            outVertex.Transform2D.w =  ca * particle.Size;
        };

#ifdef USE_MULTITHREADED_PARTICLES_WITH_GTS

    gts::ParallelFor parallelFor(vaGTS::GetInstance().Scheduler());
    parallelFor( 0, (int)allParticles.size(), elementTickProc );

#elif defined( USE_MULTITHREADED_PARTICLES_WITH_TF )
    vaTF::parallel_for( 0, loopLength, elementTickProc, 1 );
#else

    for( int i = 0; i < loopLength; i++ )
        elementTickProc( i );

#endif
}

void vaSimpleParticleSystem::DrawDebugBoxes( )
{
    assert( false ); // need to rejiggle the debug canvas stuff
#if 0
    vaDebugCanvas3D * canvas3D = vaDebugCanvas3D::GetInstancePtr( );

    //canvas3D->DrawBox( m_boundingBox, 0x80000000, 0, &this->GetTransform() );

    bool dbgShowEmitters = true;
    bool dbgShowParticles = false;
    if( dbgShowEmitters )
    {
        for( uint32 i = 0; i < m_emitters.size( ); i++ )
        {
            if( m_emitters[i]->Settings.SpawnAreaType == vaSimpleParticleEmitter::SAT_BoundingBox )
            {
                vaOrientedBoundingBox obb = vaOrientedBoundingBox::Transform( m_emitters[i]->Settings.SpawnAreaBoundingBox, this->GetTransform( ) );
                canvas3D->DrawBox( obb, 0x80000000, 0x08FF0000 );
            }
            else if( m_emitters[i]->Settings.SpawnAreaType == vaSimpleParticleEmitter::SAT_BoundingSphere )
            {
                vaBoundingSphere bs = m_emitters[i]->Settings.SpawnAreaBoundingSphere;
                bs.Center = vaVector3::TransformCoord( bs.Center, this->GetTransform( ) );
                canvas3D->DrawSphere( bs, 0x80000000, 0x08FF0000 );
            }
        }
    }
    if( dbgShowParticles )
    {
        for( uint32 i = 0; i < m_particles.size( ); i++ )
        {
            vaVector3 halfSize = vaVector3( m_particles[i].Size * 0.5f, m_particles[i].Size * 0.5f, m_particles[i].Size * 0.5f );
            vaBoundingBox aabb( m_particles[i].Position - halfSize, halfSize * 2 );
            vaOrientedBoundingBox obb( aabb, this->GetTransform( ) );
            canvas3D->DrawBox( aabb, 0x00000000, vaVector4::ToBGRA( m_particles[i].Color ) );
        }
    }
#endif
}

void vaSimpleParticleSystem::Tick( float deltaTime )
{
    VA_TRACE_CPU_SCOPE( vaSimpleParticleSystem_Tick );
    m_lastTickID++;

    {
        VA_TRACE_CPU_SCOPE( Emitters );
        const size_t particlesBeforeEmitterShaders = m_particles.size( );
        for( size_t i = 0; i < m_emitters.size( ); i++ )
        {
            vaSimpleParticleEmitter & emitter = *m_emitters[i].get( );

            if( !m_emitters[i]->Active )
            {
                // if emitter inactive and unreferenced, reclaim the ptr!
                if( m_emitters[i].use_count() == 1 ) // this assert is not entirely correct for multithreaded scenarios so beware
                {
                    ReleaseEmitterPtr( m_emitters[i] ); // WARNING: this makes "emitter" variable a zero pointer from now on!!
                    if( m_emitters.size( ) == (i-1) )
                    {
                        m_emitters.pop_back( );
                    }
                    else
                    {
                        m_emitters[i] = m_emitters.back( );
                        m_emitters.pop_back( );
                        i--;
                    }
                }
                continue;
            }

            // emitter is only allowed to push back new particles, never remove or swap them!
            m_emitters[i]->delegate_emitterTickShader.Invoke( *this, *m_emitters[i].get(), m_particles, deltaTime );

            // emitter no longer active?
            if( ( ( emitter.RemainingEmitterLife <= 0 ) || ( emitter.RemainingEmitterParticleCount <= 0 ) ) )
            {
                emitter.Active = false;
                continue;
            }
        }
    }

    {
        VA_TRACE_CPU_SCOPE( TickShader );
        const size_t particlesBeforeParticleShader = m_particles.size( );
        delegate_particlesTickShader.Invoke( *this, m_particles, deltaTime );
        assert( particlesBeforeParticleShader == m_particles.size( ) );
    }


    // update bounding box and remove dead particles
    // TODO: !!! multithread this - also rework dead particle strategy !!!
    {
        VA_TRACE_CPU_SCOPE( UpdateBBAndDelete );

        m_boundingBox = vaBoundingBox::Degenerate;

        if( m_particles.size( ) > 0 )
        {
            vaVector3 bsize = vaVector3( m_particles[0].Size, m_particles[0].Size, m_particles[0].Size );
            m_boundingBox = vaBoundingBox( m_particles[0].Position - bsize * 0.5f, bsize );
        }

        for( int i = (int)m_particles.size( )-1; i >= 0; i-- )
        {
            vaSimpleParticle & particle = m_particles[i];

            if( particle.LifeRemaining < 0 )
            {
                if( (m_particles.size( )-1) == i )
                {
                    m_particles.pop_back( );
                    return;
                }
                else
                {
                    m_particles[i] = m_particles.back( );
                    m_particles.pop_back( );
                    i--;
                    continue;
                }
            }

            vaVector3 bsize = vaVector3( m_particles[i].Size, m_particles[i].Size, m_particles[i].Size );
            m_boundingBox = vaBoundingBox::Combine( m_boundingBox, vaBoundingBox( m_particles[i].Position - bsize * 0.5f, bsize ) );
        }
    }
    
    m_lastTickEmitterCount  = (int)m_emitters.size();
    m_lastTickParticleCount = (int)m_particles.size();

    m_sortedAfterTick = false;
}

vaDrawResultFlags vaSimpleParticleSystem::Draw( vaRenderDeviceContext & renderContext, const vaRenderOutputs & renderOutputs, const vaDrawAttributes & drawAttributes, const shared_ptr<vaTexture> & viewspaceDepthSource, vaBlendMode blendMode, vaShadingRate shadingRate )
{
    VA_TRACE_CPUGPU_SCOPE( ParticleSystem, renderContext );
    VA_TRACE_CPU_SCOPE( vaSimpleParticleSystem_Draw );
    vaDrawResultFlags ret = vaDrawResultFlags::None;

    std::vector< std::pair< std::string, std::string > > newStaticShaderMacros;

    if( newStaticShaderMacros != m_staticShaderMacros )
    {
        m_staticShaderMacros = newStaticShaderMacros;
        m_shadersDirty = true;
    }

    if( m_shadersDirty )
    {
        m_shadersDirty = false;

        std::vector<vaVertexInputElementDesc> inputElements;
        inputElements.push_back( { "SV_Position", 0,    vaResourceFormat::R32G32B32A32_FLOAT, 0, vaVertexInputElementDesc::AppendAlignedElement, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );
        inputElements.push_back( { "TEXCOORD", 0,       vaResourceFormat::R32G32B32A32_FLOAT, 0, vaVertexInputElementDesc::AppendAlignedElement, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );
        inputElements.push_back( { "COLOR", 0,          vaResourceFormat::R32G32B32A32_FLOAT, 0, vaVertexInputElementDesc::AppendAlignedElement, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );
        //inputElements.push_back( { "COLOR", 0,          vaResourceFormat::R8G8B8A8_UNORM, 0,     vaVertexInputElementDesc::AppendAlignedElement, vaVertexInputElementDesc::InputClassification::PerVertexData, 0 } );

        string shaderCode = 
            "struct GenericBillboardSpriteVertex                                                            \n"
            "{                                                                                              \n"
            "    float4 Position_CreationID  : SV_Position;                                                 \n"
            "    float4 Color                : COLOR;                                                       \n"
            "    float4 Transform2D          : TEXCOORD0;                                                   \n"
            "};                                                                                             \n"
            "GenericBillboardSpriteVertex SimpleParticleVS( const in GenericBillboardSpriteVertex input )   \n"
            "{                                                                                              \n"
            "    return input;                                                                              \n"
            "}                                                                                              \n";

        m_vertexShader->CompileVSAndILFromBuffer( shaderCode, "SimpleParticleVS", inputElements, m_staticShaderMacros, true );
    }

    if( m_particles.size() == 0 )
        return vaDrawResultFlags::None;

    if( m_buffersLastUpdateTickID != GetLastTickID() )
    {
        const std::vector<vaSimpleParticle> & particles = GetParticles( );

        m_buffersLastCountToDraw = (int)particles.size( );

        if( m_buffersLastCountToDraw > m_dynamicBufferMaxElementCount )
        {
            // buffer not even big enough for one draw
            assert( false );
            return vaDrawResultFlags::UnspecifiedError;
        }
        if( m_buffersLastCountToDraw > (m_dynamicBufferMaxElementCount * 0.4) )
        {
            // you might want to consider increasing the m_dynamicBufferMaxElementCount
            assert( false );
            return vaDrawResultFlags::UnspecifiedError;
        }

        vaResourceMapType mapType = vaResourceMapType::None;
        if( (m_dynamicBufferCurrentlyUsed + m_buffersLastCountToDraw) > m_dynamicBufferMaxElementCount )
        {
            mapType = vaResourceMapType::WriteDiscard;
            m_dynamicBufferCurrentlyUsed = 0;
        }
        else
        {
            mapType = vaResourceMapType::WriteNoOverwrite;
        }
        if( !m_dynamicBuffer->Map( mapType ) )
        {
            assert( false );
            return vaDrawResultFlags::UnspecifiedError;
        }

        vaBillboardSprite * destinationBuffer = &m_dynamicBuffer->GetMappedData<vaBillboardSprite>()[m_dynamicBufferCurrentlyUsed];

        m_buffersLastOffsetInVertices = m_dynamicBufferCurrentlyUsed;

        m_dynamicBufferCurrentlyUsed += m_buffersLastCountToDraw;

        {
            VA_TRACE_CPU_SCOPE( DrawBufferUpdate );
            delegate_drawBufferUpdateShader.Invoke( *this, particles, GetSortedIndices(), destinationBuffer, m_buffersLastCountToDraw * sizeof(vaBillboardSprite) );
        }

        m_dynamicBuffer->Unmap( );
    }

    shared_ptr<vaRenderMaterial> material = m_material;
    if( material == nullptr )
        material = m_defaultMaterial;
    if( material == nullptr )
        return vaDrawResultFlags::UnspecifiedError;

    vaGraphicsItem renderItem;

    // make sure we're not overwriting anything else, and set our constant buffers
    //renderItem.ConstantBuffers[ SHADERINSTANCE_CONSTANTSBUFFERSLOT ] = m_shaderConstants;
    
    assert( false ); // something like below needed here:
    //renderItem.ShaderResourceViews[RENDERMESH_INSTANCE_CONSTANTBUFFERS_TEXTURESLOT] = buffer

    // update per-instance constants
    ShaderInstanceConstants instanceConsts;

    vaDrawResultFlags localFlags = vaDrawResultFlags::None;

    vaRenderMaterialData materialData;
    if( !material->PreRenderUpdate( renderContext ) )
        return vaDrawResultFlags::AssetsStillLoading;

    // Read lock
    std::shared_lock materialLock( material->Mutex( ) );

    if( !material->SetToRenderData( materialData, localFlags, vaRenderMaterialShaderType::Forward, materialLock  ) )
        return vaDrawResultFlags::AssetsStillLoading;

    materialData.Apply( renderItem );

    
    {
        assert( false ); // switch to vaRenderInstance::WriteToShaderConstants
//        vaMatrix4x4 trans = GetTransform( );
//        instanceConsts.World = trans;
//
//        // this means 'do not override'
//        instanceConsts.EmissiveAdd = vaVector4( 0.0f, 0.0f, 0.0f, 1.0f );
//
//        bool isWireframe = ( ( drawAttributes.RenderFlagsAttrib & vaDrawAttributes::RenderFlags::DebugWireframePass ) != 0 );// || materialSettings.Wireframe;
//        if( isWireframe )
//            instanceConsts.EmissiveAdd = vaVector4( 0.0f, 0.0f, 1.0f, 0.0f );
//
//        m_shaderConstants.Upload( renderContext, instanceConsts );
    }

    renderItem.ShaderResourceViews[ SIMPLE_PARTICLES_VIEWSPACE_DEPTH ] = viewspaceDepthSource;

    // override vertex shader 
    renderItem.VertexShader             = m_vertexShader;

    const vaRenderMaterial::MaterialSettings & materialSettings = material->GetMaterialSettings( );

    bool isWireframe = ( ( drawAttributes.RenderFlagsAttrib & vaDrawAttributes::RenderFlags::DebugWireframePass ) != 0 ) || materialSettings.Wireframe;

    renderItem.FillMode                 = (isWireframe)?(vaFillMode::Wireframe):(vaFillMode::Solid);
    renderItem.CullMode                 = materialSettings.FaceCull;

    renderItem.BlendMode                = blendMode;

    renderItem.FrontCounterClockwise    = true;
    
    renderItem.VertexBuffer             = m_dynamicBuffer;
    renderItem.Topology                 = vaPrimitiveTopology::PointList;

    bool enableDepthTest                = true;
    bool invertDepthTest                = false;
    bool enableDepthWrite               = false;
    bool depthTestIncludesEqual         = true;
    bool depthEnable                    = enableDepthTest || enableDepthWrite;
    bool useReversedZ                   = (invertDepthTest)?(!drawAttributes.Camera.GetUseReversedZ()):(drawAttributes.Camera.GetUseReversedZ());
    vaComparisonFunc depthFunc  = vaComparisonFunc::Always;
    if( enableDepthTest )
        depthFunc = ( depthTestIncludesEqual ) ? ( ( useReversedZ )?( vaComparisonFunc::GreaterEqual ):( vaComparisonFunc::LessEqual ) ):( ( useReversedZ )?( vaComparisonFunc::Greater ):( vaComparisonFunc::Less ) );
    renderItem.DepthFunc                = depthFunc;
    renderItem.DepthEnable              = depthEnable;
    renderItem.DepthWriteEnable         = enableDepthWrite;

    renderItem.ShadingRate              = shadingRate;

    int countToDraw = (m_debugParticleDrawCountLimit>0)?(m_debugParticleDrawCountLimit):(m_buffersLastCountToDraw);

    while( countToDraw > 0 )
    {
        int countToReallyDraw = vaMath::Min( m_buffersLastCountToDraw, countToDraw );
        //dx11Context->Draw( countToReallyDraw, m_buffersLastOffsetInVertices );
        renderItem.SetDrawSimple( countToReallyDraw, m_buffersLastOffsetInVertices );
        ret |= renderContext.ExecuteSingleItem( renderItem, renderOutputs, &drawAttributes );
        countToDraw -= m_buffersLastCountToDraw;
    }

    return vaDrawResultFlags::None;
}

#endif