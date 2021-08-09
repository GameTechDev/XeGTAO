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

#include "Core/vaUI.h"

namespace Vanilla
{
    class vaCameraBase;

    class vaCameraControllerBase : public std::enable_shared_from_this<vaCameraControllerBase>, public vaUIPropertiesItem
    {
    protected:
        weak_ptr< vaCameraBase >                    m_attachedCamera;

    protected:
        vaCameraControllerBase() {}
    
    public:
        virtual ~vaCameraControllerBase() {}

    protected:
        friend class vaCameraBase;

        shared_ptr<vaCameraBase>                    GetAttachedCamera( ) { return m_attachedCamera.lock(); }

        virtual void                                CameraAttached( const shared_ptr<vaCameraBase> & camera );
        virtual void                                CameraTick( float deltaTime, vaCameraBase & camera, bool hasFocus )  { deltaTime; camera; hasFocus; }

    public:
        virtual string                              UIPropertiesItemGetDisplayName( ) const override    { return "CameraControllerBase"; }
        virtual void                                UIPropertiesItemTick( vaApplicationBase &, bool, bool ) override                    { }
    };

    class vaCameraControllerFreeFlight : public vaCameraControllerBase
    {

    protected:
        float                   m_yaw;
        float                   m_pitch;
        float                   m_roll;

        // a reference for yaw pitch roll calculations: default is X is forward, Z is up, Y is right
        const vaMatrix4x4       m_baseOrientation;

        float                   m_accumMouseDeltaX;
        float                   m_accumMouseDeltaY;
        vaVector3               m_accumMove;
        float                   m_rotationSpeed;
        float                   m_movementSpeed;
        float                   m_inputSmoothingLerpK;

        float                   m_movementSpeedAccelerationModifier;

        bool                    m_moveWhileNotCaptured;

    private:

    public:
        vaCameraControllerFreeFlight( );
        virtual  ~vaCameraControllerFreeFlight( );

    protected:
        virtual void                                CameraAttached( const shared_ptr<vaCameraBase> & camera );
        virtual void                                CameraTick( float deltaTime, vaCameraBase & camera, bool hasFocus );

    public:
        void                                        SetMoveWhileNotCaptured( bool moveWhileNotCaptured )    { m_moveWhileNotCaptured = moveWhileNotCaptured; }
        bool                                        GetMoveWhileNotCaptured( )                              { return m_moveWhileNotCaptured; }

    public:
        virtual string                              UIPropertiesItemGetDisplayName( ) const override   { return "CameraControllerFreeFlight"; }
        virtual void                                UIPropertiesItemTick( vaApplicationBase &, bool, bool ) override                    { }
    };

    class vaCameraControllerFlythrough : public vaCameraControllerBase
    {
    
    public:
        struct Keyframe
        {
            vaQuaternion            Orientation;
            vaVector3               Position;
            float                   Time;
            vaVector2               UserParams;

            Keyframe( const vaVector3 & position, const vaQuaternion & orientation, float time, float userParam0 = 0.0f, float userParam1 = 0.0f ) : Position( position ), Orientation( orientation ), Time( time ), UserParams( userParam0, userParam1 ) { }
        };

    protected:
        std::vector<Keyframe>       m_keys;
        float                       m_currentTime       = 0.0f;
        float                       m_totalTime         = 0.0f;
        bool                        m_enableLoop        = true;

        float                       m_playSpeed         = 1.0f;

        vaVector2                   m_lastUserParams    = { 0, 0 };

        bool                        m_fixedUp           = true;
        vaVector3                   m_fixedUpVec        = vaVector3( 0.0f, 0.0f, 1.0f );

    private:

    public:
        vaCameraControllerFlythrough( );
        virtual  ~vaCameraControllerFlythrough( );

        void                                        AddKey( const Keyframe & key );
        
        float                                       GetTotalTime( ) const                                   { return m_totalTime; }
        float                                       GetPlayTime( ) const                                    { return m_currentTime; }
        void                                        SetPlayTime( float newTime )                            { if( m_enableLoop ) newTime = std::fmodf( newTime, m_totalTime ); m_currentTime = vaMath::Clamp( newTime, 0.0f, m_totalTime ); }
        float                                       GetPlaySpeed( ) const                                   { return m_playSpeed; }
        void                                        SetPlaySpeed( float newSpeed )                          { m_playSpeed = newSpeed; }
        bool                                        GetLoop( ) const                                        { return m_enableLoop; }
        void                                        SetLoop( bool enableLoop )                              { m_enableLoop = enableLoop; }

        // updated in CameraTick
        const vaVector2 &                           GetLastUserParams( ) const                              { return m_lastUserParams; }
        
        void                                        SetFixedUp( bool enabled, const vaVector3 & upVec = vaVector3( 0.0f, 0.0f, 1.0f ) )   { m_fixedUp = enabled; m_fixedUpVec = upVec; }

    protected:
        bool                                        FindKeys( float time, int & keyIndexFrom, int & keyIndexTo );

    protected:
        virtual void                                CameraAttached( const shared_ptr<vaCameraBase> & camera );
        virtual void                                CameraTick( float deltaTime, vaCameraBase & camera, bool hasFocus );
    
    public:
        virtual string                              UIPropertiesItemGetDisplayName( ) const override   { return "CameraControllerFocusLocationsFlythrough"; }
        virtual void                                UIPropertiesItemTick( vaApplicationBase & application, bool openMenu, bool hovered ) override;
    };

}