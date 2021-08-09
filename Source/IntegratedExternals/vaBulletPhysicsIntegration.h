#pragma once

#include "Core/vaCore.h"

#ifdef VA_BULLETPHYSICS_INTEGRATION_ENABLED

#include "Core/vaMath.h"


#include "IntegratedExternals\bullet\btBulletDynamicsCommon.h"
#include "IntegratedExternals\bullet\btBulletCollisionCommon.h"

#include "IntegratedExternals\bullet\BulletCollision\CollisionShapes\btHeightfieldTerrainShape.h"
//
//#include "BulletDynamics/MLCPSolvers/btDantzigSolver.h"
//#include "BulletDynamics/MLCPSolvers/btSolveProjectedGaussSeidel.h"
//#include "BulletDynamics/MLCPSolvers/btMLCPSolver.h"
//#include "BulletDynamics/ConstraintSolver/btHingeConstraint.h"
//#include "BulletDynamics/ConstraintSolver/btSliderConstraint.h"
//
//#include "../CommonInterfaces/CommonExampleInterface.h"
//#include "LinearMath/btAlignedObjectArray.h"


// Due to size, included manually through this file; also provides va <-> bt glue where needed

// From documentation:

// Description of the library
// Bullet Physics is a professional open source collision detection, rigid body and soft body dynamics library written in portable C++.The library is primarily designed for use in games, visual effects and robotic simulation.The library is free for commercial use under the ZLib license.
// Main Features
// - Discrete and continuous collision detection including ray and convex sweep test.Collision shapes include concave and convex meshes and all basic primitives
// - Maximal coordinate 6 - degree of freedom rigid bodies( btRigidBody ) connected by constraints( btTypedConstraint ) as well as generalized coordinate multi - bodies( btMultiBody ) connected by mobilizers using the articulated body algorithm.
// - Fast and stable rigid body dynamics constraint solver, vehicle dynamics, character controller and slider, hinge, generic 6DOF and cone twist constraint for ragdolls
// - Soft Body dynamics for cloth, rope and deformable volumes with two - way interaction with rigid bodies, including constraint support
// - Open source C++ code under Zlib license and free for any commercial use on all platforms including PLAYSTATION 3, XBox 360, Wii, PC, Linux, Mac OSX, Android and iPhone
// - Maya Dynamica plugin, Blender integration, native binary.bullet serialization and examples how to import URDF, Wavefront.obj and Quake.bsp files.
// - Many examples showing how to use the SDK.All examples are easy to browse in the OpenGL 3 example browser.Each example can also be compiled without graphics.
// - Quickstart Guide, Doxygen documentation, wiki and forum complement the examples.
// Contact and Support ? Public forum for support and feedback is available at http://bulletphysics.org 

namespace Vanilla
{
    inline btVector3 btvaBridge( const vaVector3 & v )                                  { return btVector3( v.x, v.y, v.z ); }
    inline vaVector3 btvaBridge( const btVector3 & v )                                  { return vaVector3( v.getX(), v.getY(), v.getZ() ); }
    inline btQuaternion btvaBridge( const vaQuaternion & v )                            { return btQuaternion( v.x, v.y, v.z, v.w ); }
    inline vaQuaternion btvaBridge( const btQuaternion & v )                            { return vaQuaternion( v.getX(), v.getY( ), v.getZ( ), v.getW() ); }
    inline btTransform btvaBridge( const vaQuaternion & rot, const vaVector3 & trans )  { return btTransform( btvaBridge( rot ), btvaBridge( trans ) ); }
    inline btTransform btvaBridge( const vaMatrix4x4 & trans )                          { return btvaBridge( vaQuaternion::FromRotationMatrix(trans), trans.GetTranslation() ); }
    inline vaMatrix4x4 btvaBridge( const btTransform & trans )                          { return vaMatrix4x4::FromRotationTranslation( btvaBridge( trans.getRotation() ), btvaBridge( trans.getOrigin() ) ); }
}

#endif