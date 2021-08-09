///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vaCameraControllers.h"

#include "vaCameraBase.h"

#include "Core/vaInput.h"

#include "IntegratedExternals/vaImguiIntegration.h"

using namespace Vanilla;

void vaCameraControllerBase::CameraAttached( const shared_ptr<vaCameraBase> & camera )
{
    if( camera == nullptr )
    {
        assert( !m_attachedCamera.expired() );
        m_attachedCamera.reset();
    }
    else
    {
        assert( m_attachedCamera.expired() );
        m_attachedCamera = camera;
    }
}


vaCameraControllerFreeFlight::vaCameraControllerFreeFlight( )
    // orient the camera so that X is forward, Z is up, Y is right
    : m_baseOrientation( vaMatrix4x4::RotationZ( VA_PIf * 0.5f ) * vaMatrix4x4::RotationY( VA_PIf * 0.5f ) )
{
//   m_hasFocus              = true; // temporary
   m_accumMouseDeltaX      = 0.0f;
   m_accumMouseDeltaY      = 0.0f;
   m_accumMove             = vaVector3( 0.0f, 0.0f, 0.0f );
   m_rotationSpeed         = 0.5f;
   m_movementSpeed         = 15.0f;
   m_inputSmoothingLerpK   = 200.0f;
   m_yaw                   = 0.0f;
   m_pitch                 = 0.0f; // look towards x
   m_roll                  = 0.0f; // y is right
   m_movementSpeedAccelerationModifier = 0.0f;
   m_moveWhileNotCaptured   = true;
}

vaCameraControllerFreeFlight::~vaCameraControllerFreeFlight( )
{

}

void vaCameraControllerFreeFlight::CameraAttached( const shared_ptr<vaCameraBase> & camera )
{ 
    vaCameraControllerBase::CameraAttached( camera );

    if( camera != nullptr )
    {
        // extract yaw/pitch from attached camera
        vaMatrix4x4 debasedOrientation = m_baseOrientation.Inversed() * vaMatrix4x4::FromQuaternion( camera->GetOrientation() );
        debasedOrientation.DecomposeRotationYawPitchRoll( m_yaw, m_pitch, m_roll );

        m_roll = 0;
    }
}

void vaCameraControllerFreeFlight::CameraTick( float deltaTime, vaCameraBase & camera, bool hasFocus )
{
    if( ( vaInputMouseBase::GetCurrent( ) == NULL ) || ( vaInputKeyboardBase::GetCurrent( ) == NULL ) )
        return;

   vaVector3       objectPos = camera.GetPosition();
   vaQuaternion    objectOri = camera.GetOrientation();

   vaInputMouseBase & mouse       = *vaInputMouseBase::GetCurrent();
   vaInputKeyboardBase & keyboard = *vaInputKeyboardBase::GetCurrent( );

   {
      float smoothingLerpK = vaMath::TimeIndependentLerpF( deltaTime, m_inputSmoothingLerpK );

      ///////////////////////////////////////////////////////////////////////////
      // Update camera rotation
      vaVector2 cdelta = vaVector2( 0.0f, 0.0f );
      if( hasFocus )
          cdelta = (vaVector2)mouse.GetCursorDelta( ) * m_rotationSpeed;
      //
      // smoothing
      {
         m_accumMouseDeltaX += cdelta.x;
         m_accumMouseDeltaY += cdelta.y;
         cdelta.x = smoothingLerpK * m_accumMouseDeltaX;
         cdelta.y = smoothingLerpK * m_accumMouseDeltaY;
         m_accumMouseDeltaX = (1 - smoothingLerpK) * m_accumMouseDeltaX;
         m_accumMouseDeltaY = (1 - smoothingLerpK) * m_accumMouseDeltaY;
      }
      //
      // Rotate
      if( mouse.IsCaptured( ) )
      {
          //if( mouse.IsKeyDown( MK_Middle ) )
          if( keyboard.IsKeyDown( KK_SHIFT ) && keyboard.IsKeyDown( KK_CONTROL ) && keyboard.IsKeyDown( KK_ALT ) )
              m_roll    -= cdelta.x * 0.005f;
          else
            m_yaw		+= cdelta.x * 0.005f;

         m_pitch	   += cdelta.y * 0.003f;

         m_yaw		   = vaMath::AngleWrap( m_yaw );
         m_pitch	   = vaMath::Clamp( m_pitch, -(float)VA_PIf/2 + 1e-1f, +(float)VA_PIf/2 - 1e-1f );
         m_roll        = vaMath::AngleWrap( m_roll );
      }
#if 1
      { // round yaw/pitch/roll slightly to avoid precision errors causing non-determinism when saving/loading cameras; 5 is precise enough for 10x zoom sniping (fov/10) but stepping could potential be seen with 100x
        int precisionTrimDecimals = 5;
        float k = vaMath::Pow( 10, (float)precisionTrimDecimals );
        m_yaw   = vaMath::Round(m_yaw * k)/k;
        m_pitch = vaMath::Round(m_pitch * k)/k;
        m_roll  = vaMath::Round(m_roll * k)/k;
      }
#endif
      //
      vaMatrix4x4 cameraWorld = vaMatrix4x4::FromYawPitchRoll( m_yaw, m_pitch, m_roll );
      //
      // Move
      if( mouse.IsCaptured() || m_moveWhileNotCaptured )
      {
         ///////////////////////////////////////////////////////////////////////////
         // Update camera movement
         bool hasInput = false;
        
         float speedBoost = 1.0f;
         if( hasFocus )
         {
             // update camera range/speed changes
             if( keyboard.IsKeyDown( KK_CONTROL ) )
             {
                 float prevSpeed = m_movementSpeed;
                 if( keyboard.IsKeyDown( KK_SUBTRACT ) )      m_movementSpeed *= 0.95f;
                 if( keyboard.IsKeyDown( KK_ADD ) )           m_movementSpeed *= 1.05f;
                 m_movementSpeed = vaMath::Clamp( m_movementSpeed, 0.1f, 5000.0f );
                 if( prevSpeed != m_movementSpeed )
                 {
                     VA_LOG( "Camera speed changed to %.3f", m_movementSpeed );
                 }
             }

             // has any inputs?
             hasInput = keyboard.IsKeyDown( (vaKeyboardKeys)'W' ) || keyboard.IsKeyDown( (vaKeyboardKeys)'S' ) || keyboard.IsKeyDown( (vaKeyboardKeys)'A' ) || 
                        keyboard.IsKeyDown( (vaKeyboardKeys)'D' ) || keyboard.IsKeyDown( (vaKeyboardKeys)'Q' ) || keyboard.IsKeyDown( (vaKeyboardKeys)'E' );

             // speed boost modifiers!
             speedBoost *= ( keyboard.IsKeyDown( KK_SHIFT ) ) ? ( 20.0f ) : ( 1.0f );
             speedBoost *= keyboard.IsKeyDown( KK_CONTROL ) ? ( 0.05f ) : ( 1.0f );

             if( keyboard.IsKeyDown( KK_SHIFT ) && keyboard.IsKeyDown( KK_MENU ) )
                 speedBoost *= 20.0f;
         }

         m_movementSpeedAccelerationModifier = (hasInput)?(vaMath::Min(m_movementSpeedAccelerationModifier + deltaTime * 0.5f, 1.0f)):(0.0f);
         float moveSpeed = m_movementSpeed * deltaTime * ( 0.3f + 0.7f * m_movementSpeedAccelerationModifier ) * speedBoost;

         vaVector3       forward( cameraWorld.GetAxisX() );
         vaVector3       right( cameraWorld.GetAxisY() );
         vaVector3       up( cameraWorld.GetAxisZ() );

         vaVector3 accumMove = m_accumMove;

         if( hasFocus )
         {
             if( keyboard.IsKeyDown( (vaKeyboardKeys)'W' ) || keyboard.IsKeyDown( KK_UP )    )     accumMove += forward * moveSpeed;
             if( keyboard.IsKeyDown( (vaKeyboardKeys)'S' ) || keyboard.IsKeyDown( KK_DOWN )  )     accumMove -= forward * moveSpeed;
             if( keyboard.IsKeyDown( (vaKeyboardKeys)'D' ) || keyboard.IsKeyDown( KK_RIGHT ) )     accumMove += right * moveSpeed;
             if( keyboard.IsKeyDown( (vaKeyboardKeys)'A' ) || keyboard.IsKeyDown( KK_LEFT )  )     accumMove -= right * moveSpeed;
             if( keyboard.IsKeyDown( (vaKeyboardKeys)'Q' ) )     accumMove -= up * moveSpeed;
             if( keyboard.IsKeyDown( (vaKeyboardKeys)'E' ) )     accumMove += up * moveSpeed;
         }

         objectPos += accumMove * smoothingLerpK;
         m_accumMove = accumMove * (1-smoothingLerpK);
      }

      objectOri = vaQuaternion::FromRotationMatrix( m_baseOrientation * cameraWorld );
   }
   camera.SetPosition( objectPos );
   camera.SetOrientation( objectOri );
}


vaCameraControllerFlythrough::vaCameraControllerFlythrough( )
{
}

vaCameraControllerFlythrough::~vaCameraControllerFlythrough( )
{

}

void vaCameraControllerFlythrough::CameraAttached( const shared_ptr<vaCameraBase> & camera )
{
    vaCameraControllerBase::CameraAttached( camera );
}

bool vaCameraControllerFlythrough::FindKeys( float time, int & keyIndexFrom, int & keyIndexTo )
{
    time = vaMath::Clamp( time, 0.0f, m_totalTime );
    if( m_keys.size() == 0 )
        return false;
    keyIndexFrom    = 0;
    keyIndexTo      = 0;
    if( m_keys.size() == 1 )
        return true;
    
    // linear search - find binary in std:: algorithms when more perf needed :)
    for( keyIndexTo = 1; keyIndexTo < m_keys.size(); keyIndexTo++ )
    {
        if( m_keys[keyIndexTo].Time >= time )
            break;
    }
    keyIndexFrom = keyIndexTo-1;
    return true;
}

void vaCameraControllerFlythrough::CameraTick( float deltaTime, vaCameraBase & camera, bool hasFocus )
{
    hasFocus; //unreferenced

    if( m_keys.size( ) == 0 )
        return;

    SetPlayTime( GetPlayTime() + deltaTime * GetPlaySpeed() );

    int indexFrom, indexTo;
    if( !FindKeys( GetPlayTime(), indexFrom, indexTo ) )
        return;
    
    Keyframe & keyFrom  = m_keys[indexFrom];
    Keyframe & keyTo    = m_keys[indexTo];

    float timeBetweenKeys = keyTo.Time - keyFrom.Time;
    timeBetweenKeys = vaMath::Max( 0.00001f, timeBetweenKeys );
    float lerpK = vaMath::Clamp( (m_currentTime-keyFrom.Time) / timeBetweenKeys, 0.0f, 1.0f );
    //lerpK = vaMath::Smoothstep( lerpK );

    vaVector3 pos       = vaVector3::Lerp( keyFrom.Position, keyTo.Position, lerpK );
    vaQuaternion rot    = vaQuaternion::Slerp( keyFrom.Orientation, keyTo.Orientation, lerpK );
#if 1
    int index0      = vaMath::Max( 0, indexFrom-1 );
    int index1      = indexFrom;
    int index2      = indexTo;
    int index3      = vaMath::Min( (int)m_keys.size()-1, indexTo+1 );
    Keyframe & key0 = m_keys[index0];
    Keyframe & key1 = m_keys[index1];
    Keyframe & key2 = m_keys[index2];
    Keyframe & key3 = m_keys[index3];

    pos = vaVector3::CatmullRom( key0.Position, key1.Position, key2.Position, key3.Position, lerpK );
    //pos = vaVector3::Hermite( key1.Position, (key2.Position - key0.Position).Normalized(), key2.Position, (key3.Position - key1.Position).Normalized(), lerpK );
    //rot = vaQuaternion::Squad( key0.Orientation, key1.Orientation, key2.Orientation, key3.Orientation, lerpK );
    //rot = vaQuaternion::Slerp( key0.Orientation, key1.Orientation, lerpK );
    rot = vaQuaternion::CatmullRom( key0.Orientation, key1.Orientation, key2.Orientation, key3.Orientation, lerpK );

    m_lastUserParams = vaVector2::CatmullRom( key0.UserParams, key1.UserParams, key2.UserParams, key3.UserParams, lerpK );
#endif


    if( m_fixedUp )
    {
        vaVector3 currentUp = rot.GetAxisY();
        vaVector3 rotAxis   = vaVector3::Cross( currentUp, m_fixedUpVec );
        float rotAngle      = vaVector3::AngleBetweenVectors( currentUp, m_fixedUpVec );
        rot *= vaQuaternion::RotationAxis( rotAxis, rotAngle );
    }

    // float lf = vaMath::TimeIndependentLerpF( deltaTime, 5.0f / (currentKey.ShowTime+2.0f) );
    // 
    // pos = vaMath::Lerp( camera.GetPosition(), pos, lf );
    // rot = vaQuaternion::Slerp( camera.GetOrientation(), rot, lf );

    camera.SetPosition( pos );
    camera.SetOrientation( rot );
}

void vaCameraControllerFlythrough::AddKey( const Keyframe & newKey )
{
    std::vector< Keyframe >::iterator it = std::lower_bound( m_keys.begin( ), m_keys.end( ), newKey, ( [ this ]( const Keyframe & a, const Keyframe & b ) { return a.Time < b.Time; } ) );
    m_keys.insert( it, newKey ); // insert before iterator it

    m_totalTime = m_keys.back().Time;
}

void vaCameraControllerFlythrough::UIPropertiesItemTick( vaApplicationBase &, bool, bool )
{
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    //ImGui::Text( "Time: %", m_currentTime );
    ImGui::SliderFloat( "Playback position", &m_currentTime, 0.0f, m_totalTime );
    m_currentTime = vaMath::Clamp( m_currentTime, 0.0f, m_totalTime );
    ImGui::InputFloat( "Playback speed", &m_playSpeed, 0.2f );
    m_playSpeed = vaMath::Clamp( m_playSpeed, -10.0f, 10.0f );
#endif
}