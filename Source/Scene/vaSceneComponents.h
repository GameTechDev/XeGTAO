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

#include "IntegratedExternals/vaEnTTIntegration.h"

#include "vaSceneTypes.h"

#include "vaSceneComponentCore.h"

// EnTT components must be both move constructible and move assignable!

namespace Vanilla
{
    class vaXMLSerializer;
    class vaMemoryStream;
    class vaCameraBase;

    // entt-specific scene namespace
    namespace Scene
    {
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // System components 
        //
        // UIDs are unique and don't have to be part of an entity; registry.ctx<Scene::UID> is always there and specifies scene UID
        struct UID: public vaGUID           { };
        //
        // Names are not unique and don't have to be part of an entity; registry.ctx<Scene::Name> is always there and specifies scene name
        // Name is the only component (other than Relationship) that gets serialized outside of standard serialization path to enable easier debugging.
        struct Name : public std::string    { };
        //
        // Simple tag that specifies that the component is scheduled to be destroyed!
        // It should never get serialized or be present at serialization: if it does, it will assert.
        struct DestroyTag                   { bool Serialize( vaSerializer & serializer ); };
        //
        // This is a ctx (global) only tag that is set if there's currently an entity being destroyed - useful to prevent callbacks from
        // destroy event reactively adding new components or stuff.
        struct BeingDestroyed
        { 
            entt::entity                    Entity;
        };
        //
        // Automatic tag that exists on all entities with a Relationship and no Relationship::Parent
        // struct RelationshipRootTag          { };
        //
        // The parent/child relationship implementation based on "Unconstrained model" from https://skypjack.github.io/2019-06-25-ecs-baf-part-4/ / https://skypjack.github.io/2019-08-20-ecs-baf-part-4-insights/
        // This implementation uses doubly-linked list of siblings which is not circular. First element in the list will be parent's 'FirstChild' and have entt::null for the PrevSibling.
        // IMPORTANT: This component cannot be manipulated in any way other than indirectly through vaScene functions (Create..., SetParent, etc.). It is also manually serialized.
        struct Relationship
        {
            static const int                c_MaxDepthLevels        = 16;                   // Disallow tree depths higher than this - simplifies a lot of things on the implementation side
            static const int                c_MaxDepthValue         = c_MaxDepthLevels-1;   // (max value that Relationship::Depth can have)

            //Relationship( entt::entity parent, entt::entity firstChild, entt::entity prevSibling, entt::entity nextSibling, int childrenCount ) : Parent( parent ), FirstChild( firstChild ), PrevSibling( prevSibling ), NextSibling( nextSibling ), ChildrenCount( childrenCount ) { }
            entt::entity                    Parent                  = entt::null;
            entt::entity                    FirstChild              = entt::null;
            entt::entity                    PrevSibling             = entt::null;
            entt::entity                    NextSibling             = entt::null;
            int32                           ChildrenCount           = 0;
            int32                           Depth                   = 0;                    // tree depth

            bool IsValid( entt::registry & registry );

            bool operator == ( const Relationship & other )         { return this->Parent == other.Parent && this->FirstChild == other.FirstChild && this->PrevSibling == other.PrevSibling && this->NextSibling == other.NextSibling && this->ChildrenCount == other.ChildrenCount && this->Depth == other.Depth; }
        };
        //
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // transforms and bounds

        // Setting this means TransformLocal has changed and TransformWorld is going to change.
        // Setting this flag does not automatically propagate it down to children - this needs to be done manually 
        // using SetTransformDirtyRecursive/SetTransformDirtyRecursiveSafe.
        // In future this might get handled automatically?
        // Does not need to get serialized.
        struct TransformDirtyTag            { }; // bool Serialize( vaSerializer & serializer ); };

        // 
        struct TransformLocalIsWorldTag : public UIVisible 
        { 
            static const char *             UITypeInfo( )           { return
                "If this component is attached, the TransformLocal -> TransformWorld will ignore parent's world;"
                "this is for stuff like physics components that drive worldspace locations regardless of parent/child relationships"
                "If an entity has no parent, this tag makes no difference."; }

            bool                            Serialize( vaSerializer & serializer );
        };
        
        struct TransformLocal : public vaMatrix4x4, public UIVisible
        { 
            //removed for performance reasons
            //TransformLocal( )                                               { static_cast<vaMatrix4x4&>(*this) = vaMatrix4x4::Identity; }
            //TransformLocal( const vaMatrix4x4 & copy )                      { static_cast<vaMatrix4x4&>(*this) = copy; }
            const TransformLocal & operator =( const vaMatrix4x4 & copy )   { static_cast<vaMatrix4x4&>(*this) = copy; return *this; }

            void                            UITick( UIArgs & uiArgs );

            void                            Reset( entt::registry & registry, entt::entity entity );

            bool                            Serialize( vaSerializer & serializer );
        };

        struct TransformWorld : public vaMatrix4x4, public UIVisible
        { 
            const TransformWorld & operator =( const vaMatrix4x4 & copy )   { static_cast<vaMatrix4x4&>(*this) = copy; return *this; }

            void                            UITick( UIArgs & uiArgs );

            bool                            Serialize( vaSerializer & serializer );
        };

        // Automatically set on WorldBounds creation and every TransformWorld change and needs to be set by components that
        // WorldBounds captures.
        // Does not need to get serialized.
        struct WorldBoundsDirtyTag          { }; //{ bool Serialize( vaSerializer& serializer ); };

        // Worldspace bounding box for the entity. It does not include children entities! If an entity has something that has bounds (like CustomBoundingBox or MeshList),
        // a TransformWorld, this will automatically be created. If it doesn't have
        // components that create 'bounds', it will just remain a vaBoundingBox::Degenerate.
        // Does not need to get serialized.
        struct WorldBounds : public UIVisible, public UIAddRemoveResetDisabled
        { 
            vaBoundingBox                   AABB                = vaBoundingBox::Degenerate;
            vaBoundingSphere                BS                  = vaBoundingSphere::Degenerate;

            // returns false if update failed or partially failed - this means we've got to try to re-update next frame
            bool                            Update( const entt::registry & registry, entt::entity entity ) noexcept;
            void                            UIDraw( const entt::registry & registry, entt::entity entity, vaDebugCanvas2D & canvas2D, vaDebugCanvas3D & canvas3D );

            // bool                            Serialize( vaSerializer & serializer );
        };

        // Custom local space axis aligned bounding box - this is user-updated and intended for area markers or anything similar.
        struct CustomBoundingBox : public vaBoundingBox, public UIVisible
        {
            CustomBoundingBox( const vaBoundingBox& c )                             { *static_cast<vaBoundingBox*>( this ) = c; }
            CustomBoundingBox( )                                                    { *this = vaBoundingBox( { -0.5f, -0.5f, -0.5f }, {1.0f, 1.0f, 1.0f} ); }
            operator vaBoundingBox& ( )                                             { return *static_cast<vaBoundingBox*>( this ); }
            CustomBoundingBox & operator = ( const vaBoundingBox & c )              { *static_cast<vaBoundingBox*>( this ) = c; return *this; }
            //
            void                            UITick( UIArgs & uiArgs );
            void                            UIDraw( const entt::registry & registry, entt::entity entity, vaDebugCanvas2D & canvas2D, vaDebugCanvas3D & canvas3D );

            bool                            Serialize( vaSerializer & serializer );
        };
        
//        // Makes the node movable from the UI - this will write directly into TransformLocal
//        struct UIMoveTool
//        {
//            static uint32                   s_uniqueID;
//
//            uint32                          UniqueID;
//            
//            UIMoveTool( ) : UniqueID( s_uniqueID++ )                        { }
//        };

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // Rendering and stuff
        //

        // ****************************************** LIGHTS ******************************************
        // Used as a base by all lights below
        struct LightBase
        {
            // Color of emitted light, as a linear RGB color (should ideally be normalized to 1 luminance).
            // UI should expose it as an sRGB color or a color temperature.
            vaVector3                       Color       = vaVector3( 1, 1, 1 );
            // Brightness, with the actual unit depending on the type of light (see types above)
            float                           Intensity   = 1.0f;
            // Simple way to enable/disable or fade-in / fade-out the light - value must be [0, 1], it effectively multiplies 'Intensity' and 0 disables the light.
            float                           FadeFactor  = 1.0f;
        protected:    // can't be used on its own
            LightBase()                     { }

            void                            UITickColor( UIArgs & uiArgs );
            void                            ValidateColor( );

            bool                            Serialize( vaSerializer & serializer );

        public:
            static LightBase                Make( )                         { return LightBase(); }
        };
        //
        // Simple omni directional light that is identical to image-based lighting cubemap with solid color.
        // In practice there's no sense having more than one but if there's multiple ambient lights then they are just summed up together.
        // Unit is 'illuminance' in lux (lx) or lm/m^2 (I think? or is it just Luminance - cd/m^2? not sure)
        struct LightAmbient     : LightBase, UIVisible
        {
            static const char*              UITypeInfo( )   { return "Basic ambient light"; }

                                            LightAmbient( )                                                 {}
                                            LightAmbient( const LightBase & base ) : LightBase(base)        {}

            void                            UITick( UIArgs & uiArgs );
            void                            Validate( entt::registry & registry, entt::entity entity );

            bool                            Serialize( vaSerializer & serializer );
        };
        //
        // <removing - simplifying everything>
        // // A directional light source has a well-defined direction but is infinitely far away. That's quite a good approximation for sun light.
        // // Unit is 'illuminance' in lux (lx) or lm/m^2.
        // // Direction is taken from entity's TransformWorld +X axis.
        // struct LightDirectional : LightBase, UIVisible
        // {
        //     static const char*              UITypeInfo( )   { return "Directional light with optional sunlight (spherical shape source) capability"; }
        // 
        //     // area light stuff only used for directional sunlight case at the moment - based on Filament implementation
        //     float                           AngularRadius   = 0;                // angular radius of the sun in degrees; defaults to 0deg (disabled) or 0.545deg when enabled. The Sun as seen from Earth has an angular size of 0.526deg to 0.545deg
        //     float                           HaloSize        = 10.0f;            // angular radius of the sun halo defined in multiples 
        //     float                           HaloFalloff     = 80.0f;            // exponent used for the falloff of the sun halo (default is 80)
        //     //
        //     bool                            CastShadows     = true;
        // 
        //                                     LightDirectional( )                                             {}
        //                                     LightDirectional( const LightBase & base ) : LightBase( base )  {}
        // 
        //     void                            UITick( UIArgs & uiArgs );
        //     void                            UIDraw( const entt::registry & registry, entt::entity entity, vaDebugCanvas2D & canvas2D, vaDebugCanvas3D & canvas3D );
        //     void                            Validate( entt::registry & registry, entt::entity entity );
        // 
        //     bool                            Serialize( vaSerializer & serializer );
        // };
        //
        // A basic point light source has a well-defined position in space but no direction - it emits light in all directions. It is extended by 
        // Spot light functionality (if 'SpotInnerAngle' and 'SpotOuterAngle' are not 0 or PI), which controls how much light is emitted based
        // on the angle from the direction.
        // Unit is luminous power (lm).
        // Direction is taken from entity's TransformWorld +X axis.
        struct LightPoint : LightBase, UIVisible
        {
            static const char*              UITypeInfo( )   { return "Basic point or spot light"; }

            // in some ways these are considered spherical lights: this is the distance from which to start attenuating or compute umbra/penumbra/antumbra / compute specular (making this into a 'sphere' light) - useful to avoid near-infinities for when close-to-point lights
            float                           Size            = 0.1f;
            float                           RTSizeModifier  = 1.0f;             // modifies Size for ray tracing direction (but not distance) - makes RT shadows sharper; this is a 'bodge' because Size is usually larger than the light to encompass the whole light casting geometry due to shadow maps and 'special emissive' materials that pick up intensity from light; will probably be removed in the future
            // max range at which they are effective regardless of other parameters; influences performance and shadow quality (don't set to too high or shadow maps will not work)
            float                           Range           = 200.0f;
            //
            float                           SpotInnerAngle  = 0;                // angle from Direction below which the spot light has the full intensity (a.k.a. inner cone angle)
            float                           SpotOuterAngle  = 0;                // angle from Direction below which the spot light intensity starts dropping (a.k.a. outer cone angle)
            //
            bool                            CastShadows     = false;

                                            LightPoint( )                                                   {}
                                            LightPoint( const LightBase & base ) : LightBase( base )        {}

            void                            UITick( UIArgs & uiArgs );
            void                            UIDraw( const entt::registry & registry, entt::entity entity, vaDebugCanvas2D & canvas2D, vaDebugCanvas3D & canvas3D );
            void                            Validate( entt::registry & registry, entt::entity entity );

            bool                            Serialize( vaSerializer & serializer );
        };
        // ********************************************************************************************

        // This one indicates that 
        struct MaterialPicksLightEmissive : public UIVisible
        {
            static const char* UITypeInfo( ) { return
                "If this component is attached, and there are LightPoint and RenderMesh components attached, the rendered"
                "material will emit light's color * intensity, scaled by the component's IntensityMultiplier parameter."; }

            float                           IntensityMultiplier     = 100.0f;
            float                           OriginalMultiplier      = 0.0f;

            void                            UITick( UIArgs& uiArgs );
            void                            Validate( entt::registry & registry, entt::entity entity );

            bool                            Serialize( vaSerializer & serializer );
        };

        // simple list of renderable mesh-es attached to this entity - there's no LODding or anything fancy-schmancy for now
        struct RenderMesh
        {
            static const char *             UITypeInfo( )           { return
                "The simplest way to render a mesh: attach it through a RenderMesh component!"; }
            
            vaGUID                          MeshUID                 = vaGUID::Null;
            vaGUID                          OverrideMaterialUID     = vaGUID::Null;
            //mutable weak_ptr<vaRenderMesh>  CachedMesh;
            
            float                           VisibilityRange         = std::numeric_limits<float>::max();

            // this changes whenever internal stuff changes - for updating any cached stuff and etc. Also changes on every load.
            int                             GetContentsVersion( ) const { return -1; }

            // this isn't cheap so please cache
            vaBoundingBox                   GetAABB( ) const;

            shared_ptr<vaRenderMesh>        GetMesh( ) const  ;
            vaFramePtr<vaRenderMesh>        GetMeshFP( ) const;

            void                            UITick( UIArgs & uiArgs );

            RenderMesh( )                                           { }
            RenderMesh( const vaGUID & meshID, const vaGUID & overrideMaterialID = vaGUID::Null );
            RenderMesh( const RenderMesh & copy )                   = default;
            RenderMesh & operator = ( const RenderMesh & copy )     = default;

            bool                            Serialize( vaSerializer & serializer );
        };

        struct RenderCamera
        {
            static const char *             UITypeInfo( )           { return
                "Camera used to render stuff"; }

            // At the moment using this to just store vaCameraBase data - this is not very sophisticated and will be more tightly linked to the actual vaCameraBase/vaRenderCamera in the future
            // Camera transform is actually 
            shared_ptr<vaMemoryStream>      Data;

            bool                            Serialize( vaSerializer & serializer );

            void                            FromCameraBase( entt::registry & registry, entt::entity entity, const vaCameraBase & source );
            void                            ToCameraBase( const entt::registry & registry, entt::entity entity, vaCameraBase & destination );
        };

        // This is mostly hardcoded / hacked from the old scene system.
        // Position: should be position of the entity; GeometryProxy/FadeoutProxies - should be child nodes perhaps? or maybe not.
        struct IBLProbe
        {
            // these define capture parameters
            vaVector3                       Position                = vaVector3( 0, 0, 0 );
            //vaMatrix3x3                 Orientation                 = vaMatrix3x3::Identity;
            float                           ClipNear                = 0.1f;
            float                           ClipFar                 = 1000.0f;

            // If enabled, proxy OBB is used to define fadeout boundaries (weight) and control parallax
            vaOrientedBoundingBox           GeometryProxy           = vaOrientedBoundingBox( vaVector3( 0, 0, 0 ), vaVector3( 1, 1, 1 ), vaMatrix3x3::Identity );
            bool                            UseGeometryProxy        = false;

            // (ignored for distant IBLs for now - should be moved to LocalIBLProbe only)
            vaOrientedBoundingBox           FadeOutProxy            = vaOrientedBoundingBox( vaVector3( 0, 0, 0 ), vaVector3( 1, 1, 1 ), vaMatrix3x3::Identity );

            // gets baked into IBL
            vaVector3                       AmbientColor            = vaVector3( 0, 0, 0 );
            float                           AmbientColorIntensity   = 0.0f;

            // If set, probe is imported from file instead of captured from scene
            string                          ImportFilePath          = "";

            // Whether it's enabled / required
            bool                            Enabled                 = false;

            bool                            operator == ( const IBLProbe & eq ) const
            {
                return this->Position == eq.Position && this->ClipNear == eq.ClipNear && this->ClipFar == eq.ClipFar
                    && this->GeometryProxy == eq.GeometryProxy && this->UseGeometryProxy == eq.UseGeometryProxy
                    && this->FadeOutProxy == eq.FadeOutProxy
                    && this->AmbientColor == eq.AmbientColor && this->AmbientColorIntensity == eq.AmbientColorIntensity
                    && this->ImportFilePath == eq.ImportFilePath && this->Enabled == eq.Enabled;
            }
            bool                            operator != ( const IBLProbe & eq ) const { return !( *this == eq ); }

            // helper stuff - resolves paths to relative paths or something like that, I have no idea, go check the code :P
            void                            SetImportFilePath( const string & importFilePath, bool updateEnabled = true );

            void                            UITick( UIArgs & uiArgs );
            bool                            Serialize( vaXMLSerializer & serializer );
            bool                            Serialize( vaSerializer & serializer );
        };

        struct DistantIBLProbe              : IBLProbe              { };
        struct LocalIBLProbe                : IBLProbe              { };

        // 
        struct IgnoreByIBLTag : public UIVisible 
        { 
            static const char *             UITypeInfo( )                           { return
                "If this component is attached, the RenderMesh or other renderables in the entity get ignored when drawing an IBL probe"; }
            bool                            Serialize( vaSerializer & )             { return true; }    // <- this just means it gets automatically loaded/saved 
        };


        // Simple easy to use and control fog. One per scene allowed for now.
        struct FogSphere
        {
            vaVector3                       Center              = { 0, 0, 0 };      // set to camera position for regular fog   - TODO: pick up from world transform
            vaVector3                       Color               = { 0, 0, 0.2f };   // fog color
            float                           RadiusInner         = 1.0f;             // distance at which to start blending towards FogColor
            float                           RadiusOuter         = 100.0f;           // distance at which to fully blend to FogColor
            float                           BlendCurvePow       = 0.5f;             // fogK = pow( (distance - RadiusInner) / (RadiusOuter - RadiusInner), BlendCurvePow ) * BlendMultiplier
            float                           BlendMultiplier     = 0.1f;             // fogK = pow( (distance - RadiusInner) / (RadiusOuter - RadiusInner), BlendCurvePow ) * BlendMultiplier
            bool                            UseCustomCenter     = false;
            bool                            Enabled             = false;

            void                            Validate( );
            void                            UITick( UIArgs & uiArgs );
            bool                            Serialize( vaSerializer & serializer );
        };

        // Simple way to have a skybox - just set a cubemap. Other option is to use DistantIBL. Will get influenced by TransformWorld!
        struct SkyboxTexture
        {
            string                          Path                = "";               // if set then load from path (cubemap)
            vaGUID                          UID                 = vaGUID::Null;     // if TexturePath not set then use this texture asset UID
            float                           ColorMultiplier     = 1.0f;
            bool                            Enabled             = false;

            void                            Validate( );
            void                            UITick( UIArgs & uiArgs );
            bool                            Serialize( vaSerializer & serializer );
        };

        //
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    }

}
