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

// EnTT components must be both move constructible and move assignable!

namespace Vanilla
{
    class vaSerializer;
    class vaApplicationBase;
    class vaSceneComponentRegistry;
    class vaRenderMesh;
    struct vaRenderMeshLODInitializer;
    class vaDebugCanvas2D;
    class vaDebugCanvas3D;

    // entt-specific scene namespace
    namespace Scene
    {
        // this is the application<->component UI glue for editing properties of individual components
        struct UIArgs
        {
            vaApplicationBase &             Application;
            shared_ptr<void> &              UIContextRef;
            bool                            Opened;
            bool                            HasFocus;
            entt::registry &                Registry;
            entt::entity                    Entity;

            // A little helper to cast (and create if needed) the shared_ptr<void> to UI context type - see usage for idea.
            // I couldn't think of a worse name <shrug>.
            template< typename TheType >
            TheType & JazzUpContext(  )
            {
                if( UIContextRef == nullptr )
                    UIContextRef = std::make_shared<TheType>( );
                return *std::static_pointer_cast<TheType>( UIContextRef );
            }
        };

        // Makes the component visible in Properties panel and elsewhere. This will allow creation/destruction through UI even without it implementing UITick.
        // When UITick function is implemented, it implies UIVisible flag.
        struct UIVisible { };
        struct UIAddRemoveResetDisabled { };


        // This is for enforcing custom access rights at runtime (such as that components can only be destroyed by setting a DestroyTag and not directly).
        // A lot of this is ignored in Release builds, but not all. It can have 3 global states (see AccessPermissions::State) and more fine grained 
        // per-component access for concurrency.
        struct AccessPermissions
        {
            enum class State
            {
                // This state is only to allow entity (and component) destruction - nothing else.
                SerializedDelete,                               

                // This is a general purpose single threaded state: allows for creation of entities, addition of new components and etc.
                Serialized,                                     

                // This is a state that allows for various systems to access registry from many threads; it does not allow for creation
                // of entities, components - just reading and modification is permitted with per-component "locking"
                Concurrent                                      
            };

            AccessPermissions( );
            AccessPermissions( AccessPermissions & copy ) = delete;

            struct AllType  {};
            struct NoType   {};

        private:
            State                   m_state                         = State::Serialized;
            bool                    m_canCreateEntity               = true;

            // 0 means not locked by anyone, -1 means locked for read-write (can be locked by only one) and 1 means read-only
            std::vector<int>        m_locks;

            std::mutex              m_masterMutex;

        public:
            bool                    CanDestroyEntity( ) const       { return m_state == State::SerializedDelete; }
            bool                    CanCreateEntity( ) const        { return m_state == State::Serialized; }
            //
            bool                    Serialized( ) const             { return m_state == State::Serialized || m_state == State::SerializedDelete; }
            //
            // if ComponentType is const then this is a read access check; otherwise it's read/write!
            template< typename ComponentType = NoType >
            bool                    CanAccessComponent( ) const;
            //
            // allows recursive expansion of arguments - the result is Arg1 && Arg2 && ...
            template< typename First, typename Second, typename... Rest >
            bool                    CanAccessComponent( ) const;
            //
            void                    SetState( State newState );
            State                   GetState( ) const               { return m_state; }
            //
            template< typename ComponentType = NoType >
            static void             Export( std::vector<int> & readWriteComponents, std::vector<int> & readComponents );
            template< typename First, typename Second, typename... Rest >
            static void             Export( std::vector<int> & readWriteComponents, std::vector<int> & readComponents );
            //
            template<  typename... AccessedComponents >
            static std::pair< std::vector<int>, std::vector<int> >
                                    ExportPairLists( )                  { std::pair<std::vector<int>, std::vector<int>> locks; Scene::AccessPermissions::Export< AccessedComponents... >( locks.first, locks.second ); return locks; }
            //
            bool                    TryAcquire( const std::vector<int> & readWriteComponents, const std::vector<int> & readComponents );
            void                    Release( const std::vector<int> & readWriteComponents, const std::vector<int> & readComponents );
            //
            std::mutex &            MasterMutex( )                  { return m_masterMutex; }
        };

        // static storage for the component Runtime ID
        template< typename ComponentType > struct ComponentRuntimeID 
        { 
            static_assert( !std::is_volatile_v<ComponentType>, "No volatile types plz" ); static_assert( !std::is_const_v<ComponentType>, "No const types plz" ); static_assert( !std::is_pointer_v<ComponentType>, "No pointer types plz" ); static_assert( !std::is_reference_v<ComponentType>, "No references types plz" );
            friend class Vanilla::vaSceneComponentRegistry; friend struct Components; static std::atomic_int  s_runtimeID; 
        };
        template< typename ComponentType >
        std::atomic_int  ComponentRuntimeID<ComponentType>::s_runtimeID = -1;

        struct Components
        {
            // all other getters here
            template< typename ComponentType >
            static int              TypeIndex( );
            static int              TypeIndex( const string & name );

            static int              TypeCount( );
            static int              TypeUseCount( int typeIndex, entt::registry & registry );      // total number of created components for the given type

            static const string &   TypeName( int typeIndex );
            static string           DetailedTypeInfo( int typeIndex );

            static bool             HasUITypeInfo( int typeIndex );
            static const char *     UITypeInfo( int typeIndex );

            static bool             Has( int typeIndex, entt::registry & registry, entt::entity entity );
            static void             EmplaceOrReplace( int typeIndex, entt::registry & registry, entt::entity entity );
            static void             Remove( int typeIndex, entt::registry & registry, entt::entity entity );

            static bool             UIVisible( int typeIndex );
            static bool             UIAddRemoveResetDisabled( int typeIndex );

            static bool             HasSerialize( int typeIndex );
            static bool             Serialize( int typeIndex, entt::registry & registry, entt::entity entity, class vaSerializer & serializer );

            static bool             HasValidate( int typeIndex );
            static void             Validate( int typeIndex, entt::registry & registry, entt::entity entity );

            static bool             HasUITick( int typeIndex );
            static void             UITick( int typeIndex, entt::registry & registry, entt::entity entity, Scene::UIArgs & uiArgs );

            static bool             HasUIDraw( int typeIndex );
            static void             UIDraw( int typeIndex, entt::registry & registry, entt::entity entity, vaDebugCanvas2D & canvas2D, vaDebugCanvas3D & canvas3D );

            static void             Reset( int typeIndex, entt::registry & registry, entt::entity entity );

            // templated versions of HasXXX/XXX pairs, add as needed
            template< typename ComponentType >
            static bool             HasUIDraw( )
                { return HasUIDraw(TypeIndex<ComponentType>()); }
            template< typename ComponentType >
            static void             UIDraw( entt::registry & registry, entt::entity entity, vaDebugCanvas2D & canvas2D, vaDebugCanvas3D & canvas3D )
                { return UIDraw(TypeIndex<ComponentType>(), registry, entity, canvas2D, canvas3D ); }
        };

        template< typename ComponentType >                          
        static int Components::TypeIndex( )                        
        { 
            int retVal = ComponentRuntimeID< std::remove_const_t<ComponentType> >::s_runtimeID; 
            if( retVal == -1 )
            {
                VA_LOG( "Unable to find TypeIndex for type '%s' - did you forget to RegisterComponent in vaSceneComponentRegistry::vaSceneComponentRegistry()? ", typeid(ComponentType).name() );
                assert( retVal >= 0 ); // see ^above^
            }
            return retVal; 
        }

    }

    // To avoid figuring out type handling now, I'm just going to use a global, thread-safe registry for obtaining component manipulation functions
    class vaSceneComponentRegistry : protected vaSingletonBase < vaSceneComponentRegistry >
    {
        struct ComponentTypeInfo
        {
            int         TypeIndex;  
            string      NameID;     // just the simplest component name; used for UI and serialization and etc - must be unique!
            string      TypeName;   // typeid(type).name()
            bool        UIVisible;
            bool        UIAddRemoveResetDisabled;

            std::function<bool( entt::registry & registry, entt::entity entity )>
                        HasCallback                 = {};
            std::function<void( entt::registry & registry, entt::entity entity ) >
                        EmplaceOrReplaceCallback    = {};
            std::function<void( entt::registry & registry, entt::entity entity ) >
                        RemoveCallback              = {};
            std::function<int( entt::registry & registry ) >
                        TotalCountCallback          = {};

            // std::function<void*( )> constructor/destructor?
            std::function<bool( entt::registry & registry, entt::entity entity, class vaSerializer & serializer )>
                        SerializerCallback          = {};
            std::function<void( entt::registry & registry, entt::entity entity, Scene::UIArgs & uiArgs )>
                        UITickCallback              = {};
            std::function<void( entt::registry & registry, entt::entity entity, vaDebugCanvas2D & canvas2D, vaDebugCanvas3D & canvas3D )>
                        UIDrawCallback              = {};
            std::function<const char *( )>
                        UITypeInfoCallback          = {};
            std::function<void( entt::registry & registry, entt::entity entity )>
                        ResetCallback               = {};
            std::function<void( entt::registry & registry, entt::entity entity )>
                        ValidateCallback            = {};
        };

        std::vector<ComponentTypeInfo>   m_components;

    protected:
        friend class vaScene;
        friend struct Scene::Components;
        vaSceneComponentRegistry( );
        ~vaSceneComponentRegistry( );

    protected:
        // !! this can only be called from the vaSceneComponentRegistry constructor !!
        template< typename ComponentType >
        void                    RegisterComponentInternal( string nameID = "" );

    protected:
        // friend struct Scene::Component;
        // returns 0-based index that is unique during the application instance; returns -1 on error
        int                     FindComponentTypeIndex( const string & nameID );

    public:
        template< typename ComponentType >
        static void             RegisterComponent( const string & nameID = "" );
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Some helper template functions
    //
    // SFINAE - https://en.cppreference.com/w/cpp/language/sfinae / https://stackoverflow.com/questions/87372/check-if-a-class-has-a-member-function-of-a-given-signature
    template<typename C>
    struct component_has_serialize
    {
    private:
        template<typename T>
        static constexpr auto check( T* ) -> typename std::is_same<
            decltype( std::declval<T>( ).Serialize( std::declval<vaSerializer&>( ) ) ), bool
            //                           ^^name^^^               ^^^^arguments^^^^     ^retval^  
        >::type;  // attempt to call it and see if the return type is correct

        template<typename>  static constexpr std::false_type check( ... );
        typedef decltype( check<C>( 0 ) ) type;
    public:
        static constexpr bool value = type::value;
    };
    //
    template<typename C>
    struct component_has_uitick
    {
    private:
        template<typename T>
        static constexpr auto check( T* ) -> typename std::is_same<
            decltype( std::declval<T>( ).UITick( std::declval< Scene::UIArgs& >( ) ) ), void
            //                           ^name^                ^^^arguments^^          ^retval^
        >::type;  // attempt to call it and see if the return type is correct

        template<typename>  static constexpr std::false_type check( ... );
        typedef decltype( check<C>( 0 ) ) type;
    public:
        static constexpr bool value = type::value;
    };
    //
    template<typename C>
    struct component_has_uitypeinfo
    {
    private:
        template<typename T>
        static constexpr auto check( T* ) -> typename std::is_same<
            decltype( std::declval<T>( ).UITypeInfo(             ) ), const char *
            //                           ^name^      ^arguments^      ^^^retval^^^
        >::type;  // attempt to call it and see if the return type is correct

        template<typename>  static constexpr std::false_type check( ... );
        typedef decltype( check<C>( 0 ) ) type;
    public:
        static constexpr bool value = type::value;
    };
    //
    template<typename C>
    struct component_has_reset
    {
    private:
        template<typename T>
        static constexpr auto check( T* ) -> typename std::is_same<
            decltype( std::declval<T>( ).Reset( std::declval< entt::registry& >( ), std::declval< entt::entity >( ) ) ), void
            //                           ^name^               ^^^^^^^^^^^^^^^^^^^^^^arguments^^^^^^^^^^^^^^^^^^^^^^     ^retval^
        >::type;  // attempt to call it and see if the return type is correct

        template<typename>  static constexpr std::false_type check( ... );
        typedef decltype( check<C>( 0 ) ) type;
    public:
        static constexpr bool value = type::value;
    };
    //
    template<typename C>
    struct component_has_validate
    {
    private:
        template<typename T>
        static constexpr auto check( T* ) -> typename std::is_same<
            decltype( std::declval<T>( ).Validate( std::declval< entt::registry& >( ), std::declval< entt::entity >( ) ) ), void
            //                           ^name^                ^^^^^^^^^^^^^^^^^^^^^^arguments^^^^^^^^^^^^^^^^^^^^^^^      ^retval^
        >::type;  // attempt to call it and see if the return type is correct

        template<typename>  static constexpr std::false_type check( ... );
        typedef decltype( check<C>( 0 ) ) type;
    public:
        static constexpr bool value = type::value;
    };
    //
    template<typename C>
    struct component_has_uidraw
    {
    private:
        template<typename T>
        static constexpr auto check( T* ) -> typename std::is_same<
            decltype( std::declval<T>( ).UIDraw( std::declval< entt::registry& >( ), std::declval< entt::entity >( ), std::declval< vaDebugCanvas2D& >( ), std::declval< vaDebugCanvas3D& >( ) ) ), void
            //                           ^name^                ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^arguments^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^   ^retval^
        >::type;  // attempt to call it and see if the return type is correct

        template<typename>  static constexpr std::false_type check( ... );
        typedef decltype( check<C>( 0 ) ) type;
    public:
        static constexpr bool value = type::value;
    };
    //
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Inlines!
    //
    
    template< typename ComponentType >
    inline void vaSceneComponentRegistry::RegisterComponentInternal( string nameID )
    {
        string typeName = typeid( ComponentType ).name( );
        if( nameID == "" )
        {
            size_t ls = typeName.rfind( "::" );
            nameID = ( ls != string::npos )?( typeName.substr(ls+2) ):( typeName );
        }
        if( FindComponentTypeIndex( nameID ) != -1 )
        {
            VA_ERROR( "Component type '%s' collision, unable to RegisterComponent", typeName.c_str( ) );
        }
        for( int i = 0; i < m_components.size( ); i++ )
        {
            if( m_components[i].NameID == nameID )
            {
                assert( false );
                VA_ERROR( "Component type '%s' had a name '%s' collision, unable to RegisterComponent", typeName.c_str(), nameID.c_str() );
            }
        }
        string logInfo = "Registering component '" + typeName + "'; ";
        ComponentTypeInfo typeInfo;
        typeInfo.TypeIndex      = (int)m_components.size( );
        Scene::ComponentRuntimeID<ComponentType>::s_runtimeID = typeInfo.TypeIndex;
        if constexpr( std::is_base_of_v< Scene::UIVisible, ComponentType> )
            typeInfo.UIVisible  = true;
        else
            typeInfo.UIVisible  = false;
        if constexpr( std::is_base_of_v< Scene::UIAddRemoveResetDisabled, ComponentType> )
            typeInfo.UIAddRemoveResetDisabled = true;
        else
            typeInfo.UIAddRemoveResetDisabled = false;
        
        typeInfo.TypeName           = typeName;
        typeInfo.NameID             = nameID;
        typeInfo.HasCallback        = [ ] ( entt::registry & registry, entt::entity entity )    { return registry.any_of<ComponentType>( entity ); };
        typeInfo.EmplaceOrReplaceCallback 
                                    = [ ] ( entt::registry & registry, entt::entity entity )    { registry.emplace_or_replace<ComponentType>( entity ); };
        typeInfo.RemoveCallback     = [ ] ( entt::registry & registry, entt::entity entity )    { registry.remove<ComponentType>( entity ); };
        typeInfo.TotalCountCallback = [ ] ( entt::registry & registry )                         { return (int)registry.size<ComponentType>( ); };
        if constexpr( component_has_serialize<ComponentType>::value )
        {
            if constexpr( std::is_empty_v<ComponentType> )
            {
                // registry.get doesn't work on empty types so handle it manually; the call doesn't actually need to happen (because there's
                // nothing to serialize) but it's used for asserting for components that shouldn't be present at serialization and etc. so
                // leave it in.
                typeInfo.SerializerCallback = [ ]( entt::registry &, entt::entity, vaSerializer & serializer ) -> bool
                { ComponentType dummy; return dummy.Serialize( serializer ); };
                logInfo += "has empty type (tag) serializer; ";
            }
            else
            {
                typeInfo.SerializerCallback = [ ] ( entt::registry& registry, entt::entity entity, vaSerializer & serializer ) -> bool
                    { return registry.get<ComponentType>(entity).Serialize( serializer ); };
                logInfo += "has serializer; ";
            }
        }
        if constexpr( component_has_uitick<ComponentType>::value )
        {
            typeInfo.UIVisible          = true;
            typeInfo.UITickCallback     = [ ] ( entt::registry& registry, entt::entity entity, Scene::UIArgs & uiArgs ) -> void
                { registry.get<ComponentType>( entity ).UITick( uiArgs ); };
            logInfo += "has UI handler; ";
        }
        if constexpr( component_has_uitypeinfo<ComponentType>::value )
        {
            typeInfo.UITypeInfoCallback = [ ]( ) -> const char *
            { return ComponentType::UITypeInfo( ); };
            logInfo += "has UI type info handler; ";
        }
        if constexpr( component_has_reset<ComponentType>::value )
        {
            typeInfo.ResetCallback = [ ]( entt::registry & registry, entt::entity entity ) -> void
            { registry.get<ComponentType>( entity ).Reset( registry, entity ); };
            logInfo += "has Reset handler; ";
        }
        if constexpr( component_has_validate<ComponentType>::value )
        {
            typeInfo.ValidateCallback = [ ]( entt::registry & registry, entt::entity entity ) -> void
            { registry.get<ComponentType>( entity ).Validate( registry, entity ); };
            logInfo += "has Validate handler; ";
        }
        if constexpr( component_has_uidraw<ComponentType>::value )
        {
            typeInfo.UIDrawCallback = [ ]( entt::registry& registry, entt::entity entity, vaDebugCanvas2D & canvas2D, vaDebugCanvas3D & canvas3D ) -> void
            { registry.get<ComponentType>( entity ).UIDraw( registry, entity, canvas2D, canvas3D ); };
            logInfo += "has Validate handler; ";
        }
        
        VA_LOG( logInfo );
        m_components.push_back( typeInfo );
    }

    template< typename ComponentType >
    inline void vaSceneComponentRegistry::RegisterComponent( const string & nameID )
    {
        if( vaSceneComponentRegistry::GetInstancePtr( ) == nullptr )
            new vaSceneComponentRegistry( );
        vaSceneComponentRegistry::GetInstance().RegisterComponentInternal<ComponentType>( nameID );
    }


    inline int vaSceneComponentRegistry::FindComponentTypeIndex( const string & nameID )
    {
        // if this ever gets slow, use a map <shrug>
        for( int i = 0; i < m_components.size( ); i++ )
            if( m_components[i].NameID == nameID )
                return i;
        return -1;
    }

    template< >
    inline bool Scene::AccessPermissions::CanAccessComponent<Scene::AccessPermissions::NoType>( ) const
    {
        return true;
    }

    template< typename ComponentType >
    inline bool Scene::AccessPermissions::CanAccessComponent( ) const 
    { 
        if( Serialized( ) )
            return true;

        constexpr bool readOnly = std::is_const_v< ComponentType >;

        if constexpr( std::is_same_v< ComponentType, AllType > )
        {
            for( int i = 0; i < m_locks.size(); i++ )
            {
                if constexpr( readOnly )
                {
                    if( m_locks[i] == 0 ) 
                        return false;        // read-only locks are > 0, read-write locks are 0
                }
                else
                {
                    if( m_locks[i] != -1 )
                        return false;
                }
            }
            return true;
        }
        else
        {
            if constexpr( readOnly )
                return m_locks[Components::TypeIndex<ComponentType>()] != 0;        // read-only locks are > 0, read-write locks are 0
            else
                return m_locks[Components::TypeIndex<ComponentType>()] == -1;       // read-write locks are -1
        }
    }

    template< typename First, typename Second, typename... Rest >
    inline bool Scene::AccessPermissions::CanAccessComponent( ) const 
    {
        bool retVal = CanAccessComponent<First>( ) && CanAccessComponent<Second>( );
        if constexpr( sizeof...( Rest ) != 0 ) 
        {
            return retVal && CanAccessComponent<First>( ) && CanAccessComponent<Second>( ) && CanAccessComponent<Rest...>( );
        }
        return retVal;
    }

    template< typename ComponentType >
    inline void Scene::AccessPermissions::Export( std::vector<int> & readWriteComponents, std::vector<int> & readComponents )
    {
        if constexpr( !std::is_same_v< ComponentType, NoType > )
        {
            if constexpr( std::is_const_v< ComponentType > ) 
                readComponents.push_back( Components::TypeIndex<ComponentType>() );
            else
                readWriteComponents.push_back( Components::TypeIndex<ComponentType>() );
        }
    }

    template< typename First, typename Second, typename... Rest >
    inline void Scene::AccessPermissions::Export( std::vector<int> & readWriteComponents, std::vector<int> & readComponents )
    {
        Export<First>( readWriteComponents, readComponents );
        Export<Second>( readWriteComponents, readComponents );
        if constexpr( sizeof...( Rest ) != 0 )
        {
            return Export<Rest...>( readWriteComponents, readComponents );
        }
    }


}
