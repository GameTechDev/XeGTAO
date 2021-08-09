///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaCameraBase.h"

#include "vaCameraControllers.h"

#include "IntegratedExternals/vaImguiIntegration.h"

//#define HACKY_FLYTHROUGH_RECORDER
#ifdef HACKY_FLYTHROUGH_RECORDER
#include "Core/vaInput.h"
#endif

using namespace Vanilla;

#pragma warning( disable : 4996 )


vaCameraBase::vaCameraBase( )
{
    m_YFOVMain          = true;
    m_YFOV              = 60.0f / 180.0f * VA_PIf;
    m_XFOV              = 0.0f;
    m_aspect            = 1.0f;
    m_nearPlane         = 0.01f;
    m_farPlane          = 100000.0f;
    m_viewport          = vaViewport( 64, 64 );
    m_position          = vaVector3( 0.0f, 0.0f, 0.0f );
    m_orientation       = vaQuaternion::Identity;
    //
    m_viewTrans         = vaMatrix4x4::Identity;
    m_projTrans         = vaMatrix4x4::Identity;
    //
    m_useReversedZ      = true;
    //
    m_subpixelOffset    = vaVector2( 0.0f, 0.0f );
    //
    UpdateSecondaryFOV( );
}
//
vaCameraBase::~vaCameraBase( )
{
}
//
vaCameraBase::vaCameraBase( const vaCameraBase & other )
{
    *this = other;
}

vaCameraBase & vaCameraBase::operator = ( const vaCameraBase & other )
{
    assert( this != &other );

    // this assert is a reminder that you might have to update below if you have added new variables
    int dbgSizeOfSelf = sizeof(*this);
    assert( dbgSizeOfSelf == 368 ); dbgSizeOfSelf;

    m_YFOV              = other.m_YFOV;
    m_XFOV              = other.m_XFOV;
    m_YFOVMain          = other.m_YFOVMain;
    m_aspect            = other.m_aspect;
    m_nearPlane         = other.m_nearPlane;
    m_farPlane          = other.m_farPlane;
    m_viewport          = other.m_viewport;
    m_useReversedZ      = other.m_useReversedZ;
    m_position          = other.m_position;
    m_orientation       = other.m_orientation;
    m_worldTrans        = other.m_worldTrans;
    m_viewTrans         = other.m_viewTrans;
    m_projTrans         = other.m_projTrans;
    m_direction         = other.m_direction;
    m_subpixelOffset    = other.m_subpixelOffset;
    m_defaultEV100      = other.GetEV100( true );
    m_defaultHDRClamp   = other.GetHDRClamp( );
    return *this;
}

bool vaCameraBase::Load( const string & fileName )
{
    vaFileStream fileIn;
    if( fileIn.Open( fileName, FileCreationMode::Open ) )
        return Load( fileIn );
    return false;
}

bool vaCameraBase::Save( const string & fileName ) const
{
    vaFileStream fileOut;
    if( fileOut.Open( fileName, FileCreationMode::Create ) )
        return Save( fileOut );
    return false;
}

//
#define RETURN_FALSE_IF_FALSE( x ) if( !(x) ) return false;
//
// void vaCameraBase::UpdateFrom( vaCameraBase & copyFrom )
// {
//     m_YFOVMain          = copyFrom.m_YFOVMain      ;
//     m_YFOV              = copyFrom.m_YFOV          ;
//     m_XFOV              = copyFrom.m_XFOV          ;
//     m_aspect            = copyFrom.m_aspect        ;
//     m_nearPlane         = copyFrom.m_nearPlane     ;
//     m_farPlane          = copyFrom.m_farPlane      ;
//     m_viewportWidth     = copyFrom.m_viewportWidth ;
//     m_viewportHeight    = copyFrom.m_viewportHeight; 
//     m_position          = copyFrom.m_position      ;
//     m_orientation       = copyFrom.m_orientation   ;
//     UpdateSecondaryFOV( );
// }
//
bool vaCameraBase::Load( vaStream & inStream )
{
    // we're not serializing viewport as this depends on the current outputs
    m_viewport = vaViewport(0,0);
    float dummyAspect;
    int dummyWidth;
    int dummyHeight;

    RETURN_FALSE_IF_FALSE( inStream.ReadValue( m_YFOVMain )       );
    RETURN_FALSE_IF_FALSE( inStream.ReadValue( m_YFOV )           );
    RETURN_FALSE_IF_FALSE( inStream.ReadValue( m_XFOV )           );
    RETURN_FALSE_IF_FALSE( inStream.ReadValue( dummyAspect )      );
    RETURN_FALSE_IF_FALSE( inStream.ReadValue( m_nearPlane )      );
    RETURN_FALSE_IF_FALSE( inStream.ReadValue( m_farPlane )       );
    RETURN_FALSE_IF_FALSE( inStream.ReadValue( dummyWidth )  );
    RETURN_FALSE_IF_FALSE( inStream.ReadValue( dummyHeight ) );
    RETURN_FALSE_IF_FALSE( inStream.ReadValue( m_position )       );
    RETURN_FALSE_IF_FALSE( inStream.ReadValue( m_orientation )    );
    UpdateSecondaryFOV( );
    return true;
}
//
bool vaCameraBase::Save( vaStream & outStream ) const
{
    RETURN_FALSE_IF_FALSE( outStream.WriteValue( m_YFOVMain        )  );
    RETURN_FALSE_IF_FALSE( outStream.WriteValue( m_YFOV            )  );
    RETURN_FALSE_IF_FALSE( outStream.WriteValue( m_XFOV            )  );
    RETURN_FALSE_IF_FALSE( outStream.WriteValue( m_aspect          )  );
    RETURN_FALSE_IF_FALSE( outStream.WriteValue( m_nearPlane       )  );
    RETURN_FALSE_IF_FALSE( outStream.WriteValue( m_farPlane        )  );
    RETURN_FALSE_IF_FALSE( outStream.WriteValue( m_viewport.Width   )  );   // these not supposed to be serialized but I don't want to break the format
    RETURN_FALSE_IF_FALSE( outStream.WriteValue( m_viewport.Height  )  );   // these not supposed to be serialized but I don't want to break the format
    RETURN_FALSE_IF_FALSE( outStream.WriteValue( m_position        )  );
    RETURN_FALSE_IF_FALSE( outStream.WriteValue( m_orientation     )  );
    return true;
}
//
void vaCameraBase::AttachController( const std::shared_ptr<vaCameraControllerBase> & cameraController )
{
    if( cameraController == nullptr && m_controller != nullptr )
    {
        m_controller->CameraAttached( nullptr );
        m_controller = nullptr;
        return;
    }

    if( cameraController != nullptr )
    {
        {   // new controller is currently attached to another camera?
            auto alreadyAttachedCamera = cameraController->GetAttachedCamera( );
            if( alreadyAttachedCamera != nullptr )
            {
                // detach previously attached
                alreadyAttachedCamera->AttachController( nullptr );
            }
        }
        {   // camera has an existing controller attached?
            auto alreadyAttachedCamera = (m_controller==nullptr)?(nullptr):(m_controller->GetAttachedCamera( ));
            if( alreadyAttachedCamera != nullptr )
            {
                // detach previously attached
                alreadyAttachedCamera->AttachController( nullptr );
            }
        }
        m_controller = cameraController;
        m_controller->CameraAttached( this->shared_from_this( ) );
    }
    else
    {
        m_controller = cameraController;
    }

}
//
void vaCameraBase::Tick( float deltaTime, bool hasFocus )
{
    if( (m_controller != nullptr) && (deltaTime != 0.0f) )
        m_controller->CameraTick( deltaTime, *this, hasFocus );

    UpdateSecondaryFOV( );

    const vaQuaternion & orientation = GetOrientation( );
//    const vaVector3 & position = GetPosition( );
    
    m_worldTrans = vaMatrix4x4::FromQuaternion( orientation );
    m_worldTrans.SetTranslation( m_position );

    m_direction = m_worldTrans.GetAxisZ();  // forward - I'm not sure this is ok, Forward should be +X

    m_viewTrans = m_worldTrans.Inversed( );

    if( m_useReversedZ )
        m_projTrans = vaMatrix4x4::PerspectiveFovLH( m_YFOV, m_aspect, m_farPlane, m_nearPlane );
    else
        m_projTrans = vaMatrix4x4::PerspectiveFovLH( m_YFOV, m_aspect, m_nearPlane, m_farPlane );

    if( m_subpixelOffset != vaVector2( 0, 0 ) )
    {
        m_projTrans = m_projTrans * vaMatrix4x4::Translation( 2.0f * m_subpixelOffset.x / (float)m_viewport.Width, -2.0f * m_subpixelOffset.y / (float)m_viewport.Height, 0.0f );
    }

    // a hacky way to record camera flythroughs!
#ifdef HACKY_FLYTHROUGH_RECORDER
    if( vaInputKeyboardBase::GetCurrent( )->IsKeyClicked( ( vaKeyboardKeys )'K' ) && vaInputKeyboardBase::GetCurrent( )->IsKeyDown( KK_CONTROL ) )
    {
        vaFileStream fileOut;
        if( fileOut.Open( vaCore::GetExecutableDirectory( ) + L"camerakeys.txt", FileCreationMode::Append ) )
        {
            string newKey = vaStringTools::Format( "m_cameraFlythroughController->AddKey( vaCameraControllerFlythrough::Keyframe( vaVector3( %.3ff, %.3ff, %.3ff ), vaQuaternion( %.3ff, %.3ff, %.3ff, %.3ff ), keyTime ) ); keyTime+=keyTimeStep;\n\n", 
                m_position.x, m_position.y, m_position.z, m_orientation.x, m_orientation.y, m_orientation.z, m_orientation.w );
            fileOut.WriteTXT( newKey );
            VA_LOG( "Logging camera key: %s", newKey.c_str() );
        }
    }
    if( vaInputKeyboardBase::GetCurrent( )->IsKeyClicked( KK_SPACE ) )
    {
        vaFileStream fileOut;
        if( fileOut.Open( vaCore::GetExecutableDirectory( ) + L"randompoints.txt", FileCreationMode::Append ) )
        {
            string newKey = vaStringTools::Format( "list.push_back( vaVector3( %.3ff, %.3ff, %.3ff ) );\n", m_position.x, m_position.y, m_position.z );
            fileOut.WriteTXT( newKey );
            VA_LOG( "Logging camera position: %s", newKey.c_str( ) );
        }
    }
#endif
}
//
void vaCameraBase::TickManual( const vaVector3 & position, const vaQuaternion & orientation, const vaMatrix4x4 & projection )
{
    assert( m_controller == nullptr );

    m_position          = position;
    m_orientation       = orientation;
    m_projTrans         = projection;

    m_worldTrans = vaMatrix4x4::FromQuaternion( orientation );
    m_worldTrans.SetTranslation( m_position );
    m_direction = m_worldTrans.GetAxisZ();  // forward
    m_viewTrans = m_worldTrans.Inversed( );

    float tanHalfFOVY   = 1.0f / m_projTrans.m[1][1];
    float tanHalfFOVX   = 1.0F / m_projTrans.m[0][0];
    m_YFOV      = atanf( tanHalfFOVY ) * 2;
    m_aspect    = tanHalfFOVX / tanHalfFOVY;
    m_XFOV      = m_YFOV / m_aspect;
}
//
void vaCameraBase::SetSubpixelOffset( vaVector2 & subpixelOffset )
{
    m_subpixelOffset = subpixelOffset; 

    // Need to update projection matrix on every subpixel offset change
    if( m_useReversedZ )
        m_projTrans = vaMatrix4x4::PerspectiveFovLH( m_YFOV, m_aspect, m_farPlane, m_nearPlane );
    else
        m_projTrans = vaMatrix4x4::PerspectiveFovLH( m_YFOV, m_aspect, m_nearPlane, m_farPlane );

    if( m_subpixelOffset != vaVector2( 0, 0 ) )
    {
        m_projTrans = m_projTrans * vaMatrix4x4::Translation( 2.0f * m_subpixelOffset.x / (float)m_viewport.Width, -2.0f * m_subpixelOffset.y / (float)m_viewport.Height, 0.0f );
    }
}
//
void vaCameraBase::SetViewport( vaViewport & viewport )
{
    m_viewport = viewport;
    m_aspect = m_viewport.Width / (float)m_viewport.Height;
}
//
void vaCameraBase::CalcFrustumPlanes( vaPlane planes[6] ) const
{
    vaMatrix4x4 cameraViewProj = m_viewTrans * m_projTrans;

    vaGeometry::CalculateFrustumPlanes( planes, cameraViewProj );
}
//
vaPlane vaCameraBase::GetNearPlane( ) const 
{ 
    // can also be derived from m_position & m_direction

    vaMatrix4x4 cameraViewProj = m_viewTrans * m_projTrans;

    // Near clipping plane
    return vaPlane(
        cameraViewProj( 0, 3 ) - cameraViewProj( 0, 2 ),
        cameraViewProj( 1, 3 ) - cameraViewProj( 1, 2 ),
        cameraViewProj( 2, 3 ) - cameraViewProj( 2, 2 ),
        cameraViewProj( 3, 3 ) - cameraViewProj( 3, 2 ) ).PlaneNormalized( );
}
vaPlane vaCameraBase::GetFarPlane( ) const 
{ 
    // can also be derived from m_position & m_direction

    vaMatrix4x4 cameraViewProj = m_viewTrans * m_projTrans;

    // Far clipping plane
    return vaPlane(
        cameraViewProj( 0, 2 ),
        cameraViewProj( 1, 2 ),
        cameraViewProj( 2, 2 ),
        cameraViewProj( 3, 2 ) ).PlaneNormalized( );
}
//
vaMatrix4x4 vaCameraBase::ComputeZOffsettedProjMatrix( float zModMul, float zModAdd ) const
{
    float XFOV, YFOV;
    GetFOVs( XFOV, YFOV );

    float modNear   = m_nearPlane * zModMul + zModAdd;
    float modFar    = m_farPlane * zModMul + zModAdd;

    if( m_useReversedZ )
        return vaMatrix4x4::PerspectiveFovLH( YFOV, m_aspect, modFar, modNear );
    else
        return vaMatrix4x4::PerspectiveFovLH( YFOV, m_aspect, modNear, modFar );
}
//
vaMatrix4x4 vaCameraBase::ComputeNonReversedZProjMatrix( ) const
{
    float XFOV, YFOV;
    GetFOVs( XFOV, YFOV );
    return vaMatrix4x4::PerspectiveFovLH( YFOV, m_aspect, m_nearPlane, m_farPlane );
}
//
void vaCameraBase::SetOrientationLookAt( const vaVector3 & lookAtPos, const vaVector3 & upVector )
{
    vaMatrix4x4 lookAt = vaMatrix4x4::LookAtLH( m_position, lookAtPos, upVector );

    SetOrientation( vaQuaternion::FromRotationMatrix( lookAt ).Inversed( ) );
}
//
void vaCameraBase::SetDirection( const vaVector3 & direction, const vaVector3 & upVector )
{
    SetOrientationLookAt( m_position + direction, upVector );
}
//
void vaCameraBase::SetFromWorldMatrix( const vaMatrix4x4 & worldTransform )
{
    vaVector3 scale, translation; vaQuaternion orientation;
    worldTransform.Decompose( scale, orientation, translation );
    SetPosition( translation );
    SetOrientation( orientation );
}
//
void vaCameraBase::SetFromViewMatrix( const vaMatrix4x4 & viewTransform )
{
    SetFromWorldMatrix( viewTransform.FastTransformInversed() );
}
//
void vaCameraBase::GetFOVs( float& XFOV, float& YFOV ) const
{
    if( m_YFOVMain )
    {
        XFOV = m_YFOV * m_aspect;
        YFOV = m_YFOV;
    }
    else
    {
        XFOV = m_XFOV;
        YFOV = m_XFOV / m_aspect;
    }
}
//
void vaCameraBase::UpdateSecondaryFOV( )
{
    if( m_YFOVMain )
    {
        m_XFOV = m_YFOV * m_aspect;
    }
    else
    {
        m_YFOV = m_XFOV / m_aspect;
    }
}
//
void vaCameraBase::GetScreenWorldRay( const vaVector2 & screenPos, vaVector3 & outRayPos, vaVector3 & outRayDir ) const
{
    vaVector3 screenNearNDC = vaVector3( screenPos.x / m_viewport.Width * 2.0f - 1.0f, 1.0f - screenPos.y / m_viewport.Height * 2.0f, m_useReversedZ?1.0f:0.0f );
    vaVector3 screenFarNDC = vaVector3( screenPos.x / m_viewport.Width * 2.0f - 1.0f, 1.0f - screenPos.y / m_viewport.Height * 2.0f, m_useReversedZ?0.0f:1.0f );

    vaMatrix4x4 viewProj = GetViewMatrix( ) * GetProjMatrix( );
    vaMatrix4x4 viewProjInv;

    if( !viewProj.Inverse( viewProjInv ) )
    {
        assert( false );
        return;
    }
    outRayPos = vaVector3::TransformCoord( screenNearNDC, viewProjInv );
    outRayDir = (vaVector3::TransformCoord( screenFarNDC, viewProjInv ) - outRayPos).Normalized();
}

vaVector2 vaCameraBase::WorldToScreen( const vaVector3& worldPos ) const
{
    vaMatrix4x4 viewProj = GetViewMatrix( ) * GetProjMatrix( );

    vaVector3 ret = vaVector3::TransformCoord( worldPos, viewProj );

    return { ( ret.x * 0.5f + 0.5f ) * m_viewport.Width + 0.5f, ( 0.5f - ret.y * 0.5f ) * m_viewport.Height + 0.5f };
}

vaLODSettings vaCameraBase::GetLODSettings( ) const
{
    vaLODSettings ret;
    ret.Reference       = m_position;
    ret.ReferenceYFOV   = GetYFOV( );
    ret.Scale           = 1.0f;       // 1080 / m_viewport.Height
    ret.MaxViewDistance = m_farPlane;

    return ret;
}

vaVector3 vaCameraBase::Project( const vaVector3 & v ) const
{
    return vaVector3::Project( v, m_viewport, m_projTrans, m_viewTrans, vaMatrix4x4::Identity );
}

vaVector3 vaCameraBase::Unproject( const vaVector3 & v ) const
{
    return vaVector3::Unproject( v, m_viewport, m_projTrans, m_viewTrans, vaMatrix4x4::Identity );
}
