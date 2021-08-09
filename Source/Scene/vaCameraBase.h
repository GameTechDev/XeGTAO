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
    // these can be obtained from vaCameraBase::GetLODSettings
    struct vaLODSettings
    {
        // Point from which to compute LOD!
        vaVector3                           Reference       = vaVector3( std::numeric_limits<float>::infinity( ), std::numeric_limits<float>::infinity( ), std::numeric_limits<float>::infinity( ) );

        // Compute LOD assuming the final output's Y FOV will be this
        float                               ReferenceYFOV   = std::numeric_limits<float>::infinity( );

        // And modify the LOD selection by the scale (which could be a fixed value, '1080 / m_viewport.Height' or something more complex)
        float                               Scale           = 0.0f;

        float                               MaxViewDistance = std::numeric_limits<float>::infinity( );
    };

    class vaCameraControllerBase;

    class vaCameraBase : public std::enable_shared_from_this<vaCameraBase>
    {
    protected:
        // Secondary values (updated by Tick() )
        vaMatrix4x4                     m_worldTrans;
        vaMatrix4x4                     m_viewTrans;
        vaMatrix4x4                     m_projTrans;
        vaVector3                       m_direction;
        //
        // Primary values
        vaQuaternion                    m_orientation;
        vaVector3                       m_position;
        vaVector2                       m_subpixelOffset;   // a.k.a. jitter!
        float                           m_YFOV;
        float                           m_XFOV;
        float                           m_aspect;
        float                           m_nearPlane;
        float                           m_farPlane;
        vaViewport                      m_viewport;
        bool                            m_YFOVMain;
        bool                            m_useReversedZ;
        //
        float                           m_defaultEV100      = 0.0f;
        float                           m_defaultHDRClamp   = 20000.0f;
        //
        // attached controller
        std::shared_ptr < vaCameraControllerBase >
                                        m_controller;
        //
    public:
        vaCameraBase( );
        virtual ~vaCameraBase( );
        //
        // everything but the m_controller gets copied!
        vaCameraBase( const vaCameraBase & other );
        vaCameraBase & operator = ( const vaCameraBase & other );
        //
    public:
        // All angles in radians!
        float                           GetYFOV( ) const                                    { return m_YFOV; }
        float                           GetXFOV( ) const                                    { return m_XFOV; }
        float                           GetAspect( ) const                                  { return m_aspect; }
        //                              
        void                            SetYFOV( float yFov )                               { m_YFOV = yFov; m_YFOVMain = true;    UpdateSecondaryFOV( ); }
        void                            SetXFOV( float xFov )                               { m_XFOV = xFov; m_YFOVMain = false;   UpdateSecondaryFOV( ); }
        bool                            GetYFOVMain( ) const                                { return m_YFOVMain; }
        //
        void                            SetNearPlaneDistance( float nearPlane )             { m_nearPlane = nearPlane; }
        void                            SetFarPlaneDistance( float farPlane )               { m_farPlane = farPlane; }
        //
        float                           GetNearPlaneDistance( ) const                       { return m_nearPlane; }
        float                           GetFarPlaneDistance( ) const                        { return m_farPlane; }
        //
        vaPlane                         GetNearPlane( ) const;
        vaPlane                         GetFarPlane( ) const;
        //
        void                            SetPosition( const vaVector3 & newPos )             { m_position = newPos; }
        void                            SetOrientation( const vaQuaternion & newOri )       { m_orientation = newOri; }
        void                            SetOrientationLookAt( const vaVector3 & lookAtPos, const vaVector3 & upVector = vaVector3( 0.0f, 0.0f, 1.0f ) );
        void                            SetDirection( const vaVector3 & direction, const vaVector3 & upVector = vaVector3( 0.0f, 0.0f, 1.0f ) );
        //
        // this will update all internal parameters from the given world transform
        void                            SetFromWorldMatrix( const vaMatrix4x4 & worldTransform );
        void                            SetFromViewMatrix( const vaMatrix4x4 & viewTransform );
        //
        const vaVector3 &               GetPosition( ) const                                { return m_position; }
        const vaQuaternion &            GetOrientation( ) const                             { return m_orientation; }
        const vaVector3 &               GetDirection( ) const                               { return m_direction; }
        vaVector3                       GetRightVector( ) const                             { return m_worldTrans.GetAxisX(); }
        vaVector3                       GetUpVector( ) const                                { return -m_worldTrans.GetAxisY(); }
        //                              
        void                            CalcFrustumPlanes( vaPlane planes[6] ) const;
        //                   
        const vaMatrix4x4 &             GetWorldMatrix( ) const                             { return m_worldTrans; }
        const vaMatrix4x4 &             GetViewMatrix( ) const                              { return m_viewTrans; }
        const vaMatrix4x4 &             GetProjMatrix( ) const                              { return m_projTrans; }
        // same as GetWorldMatrix !!
        const vaMatrix4x4 &             GetInvViewMatrix( ) const                           { return m_worldTrans; }
        //                      
        // add Z bias - useful for stuff like drawing wireframe on top of existing geometry
        vaMatrix4x4                     ComputeZOffsettedProjMatrix( float zModMul = 1.0002f, float zModAdd = 0.0002f ) const;
        //
        // this will return the 'traditional' LH projection matrix regardless of m_useReversedZ setting (so, same as with m_useReversedZ == false)
        vaMatrix4x4                     ComputeNonReversedZProjMatrix( ) const;
        //                         
        // Using "ReversedZ" uses projection matrix that results in inverted NDC z, to better match floating point depth buffer precision.
        // Highly advisable if using floating point depth, but requires "inverted" depth testing and care in other parts.
        void                            SetUseReversedZ( bool useReversedZ )                { m_useReversedZ = useReversedZ; }
        bool                            GetUseReversedZ( ) const                            { return m_useReversedZ; }
        //
        vaVector3                       Project( const vaVector3 & v ) const;
        vaVector3                       Unproject( const vaVector3 & v ) const;
        //
        /*
        void                            FillSelectionParams( Vanilla::vaSRMSelectionParams & selectionParams );
        void                            FillRenderParams( Vanilla::vaSRMRenderParams & renderParams );
        */
        //
        virtual void                    SetViewport( vaViewport & viewport );
        const vaViewport &              GetViewport( ) const                                { return m_viewport; }
        //
        int                             GetViewportWidth( ) const                           { return m_viewport.Width; }
        int                             GetViewportHeight( ) const                          { return m_viewport.Height; }
        //
        // a.k.a. 'jitter'
        void                            SetSubpixelOffset( vaVector2 & subpixelOffset = vaVector2( 0.0f, 0.0f ) );
        vaVector2                       GetSubpixelOffset( ) const                          { return m_subpixelOffset; }
        //
        // void                            UpdateFrom( vaCameraBase & copyFrom );
        //
        bool                            Load( vaStream & inStream );
        bool                            Save( vaStream & outStream ) const;
        //
        bool                            Load( const string & fileName );
        bool                            Save( const string & fileName ) const;
        //
        const std::shared_ptr<vaCameraControllerBase> &
                                        GetAttachedController( )                    { return m_controller; }
        void                            AttachController( const std::shared_ptr<vaCameraControllerBase> & cameraController );
        
        // hasFocus only relevant if there's an attached controller - perhaps the controller should get this state from elsewhere
        void                            Tick( float deltaTime, bool hasFocus );

		// if getting all camera setup from outside (such as from VR headset), use this to set the pose and projection and update all dependencies
        void                            TickManual( const vaVector3 & position, const vaQuaternion & orientation, const vaMatrix4x4 & projection );

        // Compute worldspace position and direction of a ray going from the screenPos from near to far.
        // Don't forget to include 0.5 offsets to screenPos if converting from integer position (for ex., mouse coords) for the correct center of the pixel (if such precision is important).
        void                            GetScreenWorldRay( const vaVector2 & screenPos, vaVector3 & outRayPos, vaVector3 & outRayDir ) const;
        void                            GetScreenWorldRay( const vaVector2i & screenPos, vaVector3 & outRayPos, vaVector3 & outRayDir ) const   { return GetScreenWorldRay( vaVector2( screenPos.x + 0.5f, screenPos.y + 0.5f ), outRayPos, outRayDir ); }
        vaVector2                       WorldToScreen( const vaVector3 & worldPos ) const ;

        virtual float                   GetEV100( bool includeExposureCompensation ) const                  { assert(includeExposureCompensation); includeExposureCompensation; return m_defaultEV100; }
        virtual float                   GetPreExposureMultiplier( bool includeExposureCompensation ) const  { return std::exp2f( GetEV100( includeExposureCompensation) ); };
        virtual float                   GetHDRClamp( ) const                                                { return m_defaultHDRClamp; }

        virtual vaLODSettings           GetLODSettings( ) const;

    protected:
        void                            UpdateSecondaryFOV( );
        void                            GetFOVs( float & XFOV, float & YFOV ) const;    // same as above except 'const'
        //
        virtual void                    SetAspect( float aspect )                    { m_aspect = aspect; }
        //*/
    };

}
