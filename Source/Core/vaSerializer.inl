///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Default specializations
#define VA_DEFAULT_SERIALIZER_ADAPTER( _Type )                                          \
template< > struct vaSerializerAdapter< _Type >                                         \
{                                                                                       \
    static const char *     Type( )  { return ""; }                                     \
    static bool             Serialize( vaSerializer & serializer, _Type & value )       \
    {                                                                                   \
        if( serializer.IsReading() )                                                    \
        {                                                                               \
            try                                                                         \
            {                                                                           \
                serializer.JSON().get_to<_Type>( value );                               \
            }                                                                           \
            catch (...)                                                                 \
            { assert( false && "json exception on read" ); return false; }              \
        }                                                                               \
        else if( serializer.IsWriting() )                                               \
        {                                                                               \
            try                                                                         \
            {                                                                           \
                serializer.JSON() = value;                                              \
            }                                                                           \
            catch (...)                                                                 \
            { assert( false && "json exception on write" ); return false; }             \
        }                                                                               \
        else { assert( false && "not reading, not writing either?" ); return false; }   \
        return true;                                                                    \
    }                                                                                   \
};

VA_DEFAULT_SERIALIZER_ADAPTER( std::string  )
VA_DEFAULT_SERIALIZER_ADAPTER( sbyte        )
VA_DEFAULT_SERIALIZER_ADAPTER( byte         )
VA_DEFAULT_SERIALIZER_ADAPTER( int16        )
VA_DEFAULT_SERIALIZER_ADAPTER( uint16       )
VA_DEFAULT_SERIALIZER_ADAPTER( int32        )
VA_DEFAULT_SERIALIZER_ADAPTER( uint32       )
VA_DEFAULT_SERIALIZER_ADAPTER( int64        )
VA_DEFAULT_SERIALIZER_ADAPTER( uint64       )
// VA_DEFAULT_SERIALIZER_ADAPTER( float        ) // <- these are done via string serialization below to support inf and NaN; see https://github.com/nlohmann/json/issues/70#issuecomment-285958089 for more info
// VA_DEFAULT_SERIALIZER_ADAPTER( double       ) // <- these are done via string serialization below to support inf and NaN; see https://github.com/nlohmann/json/issues/70#issuecomment-285958089 for more info
VA_DEFAULT_SERIALIZER_ADAPTER( bool         )

// this one could be done automatically via SFINAE, but I didn't have time to code & debug it :)
#define VA_STRING_SERIALIZER_ADAPTER( _Type, _ValueToString, _StringToValue )               \
template< > struct vaSerializerAdapter< _Type >                                             \
{                                                                                           \
    static const char * Type( ) { return ""; }                                              \
    static bool         Serialize( vaSerializer & serializer, _Type & value )               \
    {                                                                                       \
        if( serializer.IsReading( ) )                                                       \
        {                                                                                   \
            try                                                                             \
                { return _StringToValue( serializer.JSON( ).get<std::string>( ), value ); } \
            catch( ... )                                                                    \
                { assert( false ); return false; }                                          \
        }                                                                                   \
        else if( serializer.IsWriting( ) )                                                  \
        {                                                                                   \
            try                                                                             \
                { serializer.JSON( ) = S_ValueToString(value); }                            \
            catch( ... )                                                                    \
                { assert( false ); return false; }                                          \
        }                                                                                   \
        else { assert( false ); return false; }                                             \
        return true;                                                                        \
    }                                                                                       \
};

#if 1 // This is the untyped way of implementing a "raw" (using json directly) vaSerializerAdapter - probably a better option
template< > struct vaSerializerAdapter< vaGUID >
{
    static const char * Type( ) { return ""; }
    static bool         Serialize( vaSerializer& serializer, vaGUID& value )
    {
        if( serializer.IsReading( ) )
        {
            try 
                { value = vaGUID::FromString( serializer.JSON( ).get<std::string>( ) ); }
            catch( ... )
                { assert( false ); return false; }
        }
        else if( serializer.IsWriting( ) )
        {
            try
                { serializer.JSON( ) = value.ToString( ); }
            catch( ... )
                { assert( false ); return false; }
        }
        else { assert( false ); return false; }
        return true;
    }
};
#else // This is the typed way of implementing a "raw" (using json directly) vaSerializerAdapter
template< > struct vaSerializerAdapter< vaGUID >
{
    static const char * Type( )                 { return "vaGUID"; }
    static bool         Serialize( vaSerializer & serializer, vaGUID & value )
    {
        if( serializer.IsReading( ) )
        {
            try
            {
                json jtemp = serializer.JSON( )["value"];
                if( jtemp.is_null() ) <- ?
                    return false;
                value = vaGUID::FromString( jtemp.get<std::string>( ) );
            }
            catch (...)
            { assert( false ); return false; }
        }
        else if( serializer.IsWriting( ) )
        {
            try
            {
                serializer.JSON( )["value"] = value.ToString();
            }
            catch (...)
            { assert( false ); return false; }
        }
        else { assert( false ); return false; }
        return true;
    }
};
#endif

template< typename ValueType >
inline bool vaSerializer::Serialize( const string & key, ValueType & value )
{
    assert( key != "!type" );                               // This one is reserved and can't be used!

    if( m_isReading )
    {
        auto jtemp = (key=="")?(m_json):(m_json[ key ]);
        if( jtemp.is_null() )
            return false;   // this is ok, the value just isn't there
        vaSerializer readNode = vaSerializer( std::move(jtemp), true );
        if( readNode.Type() != vaSerializerAdapter<ValueType>::Type() )
            { assert( false ); return false; }              // Type mismatch is probably a code error so we assert and return false; null node is ok - just return false
        return vaSerializerAdapter<ValueType>::Serialize( readNode, value );
    }
    else if( m_isWriting )
    {
        if( (key=="" && !m_json.empty()) || m_json.contains( key ) )
            { assert( false ); return false; }              // Convention: if overwriting existing key (or key-less data), assert and return false. 
        vaSerializer writeNode = vaSerializer::OpenWrite( vaSerializerAdapter<ValueType>::Type() );
        if( !vaSerializerAdapter<ValueType>::Serialize( writeNode, value ) )
            { assert( false ); return false; }              // Not being able to write is probably a code error so we assert and return false
        if( key=="" )
            m_json = writeNode.m_json;
        else
            m_json[key] = writeNode.m_json;
        return true;
    }
    else { assert( false ); return false; }
}

template< typename ValueType >
inline bool vaSerializer::Serialize( const string & key, ValueType & value, const ValueType & defaultValue ) 
{ 
    bool retVal = Serialize<ValueType>( key, value );
    if( IsWriting( ) ) 
        return retVal;
    else if( IsReading( ) ) 
    {
        if( !retVal )
            value = defaultValue;
        return true;
    }
    else { assert( false ); return false; }
}

// This version allows for using a custom callback to serialize a generic object (presumably captured in the lambda)
inline bool vaSerializer::Serialize( const string & key, const string & typeName, const std::function< bool(vaSerializer & serializer)> & serialize )
{
    assert( key != "!type" );                               // This one is reserved and can't be used!

    if( m_isReading )
    {
        auto jtemp = (key=="")?(m_json):(m_json[ key ]);
        vaSerializer readNode = vaSerializer( std::move(jtemp), true );
        if( readNode.Type() != typeName )
            { assert( false ); return false; }              // Type mismatch is probably a code error so we assert and return false
        return serialize( readNode );
    }
    else if( m_isWriting )
    {
        if( (key=="" && !m_json.empty()) || m_json.contains( key ) )
            { assert( false ); return false; }              // Convention: if overwriting existing key (or key-less data), assert and return false. 
        vaSerializer writeNode = vaSerializer::OpenWrite( typeName );
        if( !serialize( writeNode ) )
            { assert( false ); return false; }              // Not being able to write is probably a code error so we assert and return false
        if( key == "" )
            m_json = writeNode.m_json;
        else
            m_json[key] = writeNode.m_json;
        return true;
    }
    else { assert( false ); return false; }

}


template< typename ValueType >
inline bool vaSerializer::SerializePtr( const string & key, shared_ptr<ValueType> & object, const std::function< shared_ptr<ValueType> ( ) > & newObj )
{
    assert( key != "!type" );                               // This one is reserved and can't be used!

    if( m_isReading )
    {
        assert( object == nullptr );
        object = newObj( );
    }
    else if( m_isWriting )
    {
        if( object == nullptr )
            { assert( false ); return false; }
    }
    else
        { assert( false ); return false; }
    return Serialize<ValueType>( key, *object );
}

template< typename BaseType >
inline bool vaSerializer::SerializeDynamicPtr( const string & key, shared_ptr<BaseType> & object, 
    const std::function< shared_ptr<BaseType>( const string & typeName ) > & newObj, 
    const std::function< const char * ( const BaseType & object ) > & typeOf, 
    const std::function< bool( const string & typeName, vaSerializer & serializer, BaseType & object ) > & serialize )
{
    assert( key != "!type" );                               // This one is reserved and can't be used!

    if( m_isReading )
    {
        assert( object == nullptr );                        // Convention: if pointer is non-null, although there's no issues on serialization side, there's a good chance it's an user error / unintentional.
        auto jtemp = (key=="")?(m_json):(m_json[ key ]);
        if( jtemp.is_null( ) )
            return false;
        vaSerializer readNode( std::move(jtemp), true );
        object = newObj( readNode.Type() );
        return serialize( readNode.Type(), readNode, *object );
    }
    else if( m_isWriting )
    {
        if( object == nullptr  )
            { assert( false ); return false; }              // Convention: saving null dynamic pointer not allowed because we can't figure out the type.
        if( (key=="" && !m_json.empty()) || m_json.contains( key ) )
            { assert( false ); return false; }              // Convention: if overwriting existing key (or key-less data), assert and return false. 
        vaSerializer writeNode = vaSerializer::OpenWrite( typeOf(*object) );
        if( !serialize( writeNode.Type(), writeNode, *object ) )
            return false;
        if( key == "" )
            m_json = writeNode.m_json;
        else
            m_json[key] = writeNode.m_json;
        return true;
    }
    else
        { assert( false ); return false; }
}

template< typename ValueType >
inline bool vaSerializer::SerializeVector( const string & key, std::vector<ValueType> & valueVector, const ValueType & initValue )
{
    assert( key != "!type" );                               // This one is reserved and can't be used!
    assert( key != "" );                                    // 'Keyless' direct storage not supported for arrays/vectors

    if( m_isReading )
    {
        assert( valueVector.size() == 0 );                  // Convention: if vector is not empty, although there's no issues on serialization side, there's a good chance it's an user error / unintentional.
        if( !m_json.contains(key) )
            return false;
        auto jtemp = m_json[ key ];
        if( jtemp.is_null( ) )
        {
            valueVector.clear();
            return true;
        }
        if( !jtemp.is_array( ) )
            { assert( false ); return false; }              // Json node is there but it's not an array? This is likely an user error / unintentional!
        valueVector.resize( jtemp.size(), initValue );
        for( size_t i = 0; i < valueVector.size(); i++ )
        {
            auto itemTemp = jtemp[i];
            if( itemTemp.is_null( ) )
                { assert( false ); return false; }          // Convention: null array item? it's likely data corruption or user error, so assert.
            vaSerializer readNode( std::move( itemTemp ), true );
            if( !vaSerializerAdapter<ValueType>::Serialize( readNode, valueVector[i] ) )
                { assert( false ); return false; }          // Convention: if serialization of an individual array item failed, it's likely data corruption or user error, so assert.
        }
        return true;
    }
    else if( m_isWriting )
    {
        if( m_json.contains( key ) )
            { assert( false ); return false; }              // Convention: if key already in, assert and return false. Also, input is null is user error.
        json jarr; size_t valueVectorSize = valueVector.size();
        for( size_t i = 0; i < valueVectorSize; i++ )
        {
            vaSerializer writeNode = vaSerializer::OpenWrite( vaSerializerAdapter<ValueType>::Type() );
            if( !vaSerializerAdapter<ValueType>::Serialize( writeNode, valueVector[i] ) )
                { assert( false ); return false; }          // Convention: Not being able to write is probably a code error so we assert and return false;
            jarr.push_back( writeNode.m_json );
        }
        m_json[key] = jarr;
        return true;
    }
    assert( false );
    return false;
}

template< typename ValueType >
inline bool vaSerializer::SerializeVector( const string & key, std::vector<ValueType> & valueVector, const std::vector<ValueType> & defaultValue, const ValueType & initValue )
{
    bool retVal = SerializeVector<ValueType>( key, valueVector, initValue );
    if( IsWriting( ) ) 
        return retVal;
    else if( IsReading( ) ) 
    {
        if( !retVal )
            valueVector = defaultValue;
        return true;
    }
    assert( false );
    return false;
}

template< typename ValueType >
inline bool vaSerializer::SerializeArray( const string & key, ValueType * valueArray, size_t arrayCount )
{
    assert( key != "!type" );                               // This one is reserved and can't be used!
    assert( key != "" );                                    // 'Keyless' direct storage not supported for arrays/vectors
    assert( valueArray != nullptr );
    assert( arrayCount != 0 );                              // Convention: possibly an error? If real use case, remove assert!

    if( m_isReading )
    {
        if( !m_json.contains( key ) )
            return false;
        auto jtemp = m_json[key];
        if( jtemp.is_null( ) )
        {
            assert( arrayCount == 0 );
            return arrayCount == 0;
        }
        if( !jtemp.is_array( ) || jtemp.size( ) != arrayCount )
            { assert( false ); return false; }              // Json node is there but it's not an array, or there is a size mismatch? This is likely an user error / unintentional!
        for( size_t i = 0; i < arrayCount; i++ )
        {
            auto itemTemp = jtemp[i];
            if( itemTemp.is_null( ) )
                { assert( false ); return false; }          // Convention: null array item? it's likely data corruption or user error, so assert.
            vaSerializer readNode( std::move( itemTemp ), true );
            if( !vaSerializerAdapter<ValueType>::Serialize( readNode, valueArray[i] ) )
                { assert( false ); return false; }          // Convention: if serialization of an individual array item failed, it's likely data corruption or user error, so assert.
        }
        return true;
    }
    else if( m_isWriting )
    {
        if( m_json.contains( key ) )
            { assert( false ); return false; }              // Convention: if key already in, assert and return false. Also, input is null is user error.
        json jarr;
        for( size_t i = 0; i < arrayCount; i++ )
        {
            vaSerializer writeNode = vaSerializer::OpenWrite( vaSerializerAdapter<ValueType>::Type() );
            if( !vaSerializerAdapter<ValueType>::Serialize( writeNode, valueArray[i] ) )
                { assert( false ); return false; }          // Convention: Not being able to write is probably a code error so we assert and return false;
            jarr.push_back( writeNode.m_json );
        }
        m_json[key] = jarr;
        return true;
    }
    assert( false );
    return false;
}

template< typename ValueType >
inline bool vaSerializer::SerializePtrVector( const string & key, std::vector<shared_ptr<ValueType>> & ptrVector, const std::function< shared_ptr<ValueType> ( ) > & newObj )
{
    assert( key != "!type" );                               // This one is reserved and can't be used!
    assert( key != "" );                                    // 'Keyless' direct storage not supported for arrays/vectors

    if( m_isReading )
    {
        assert( ptrVector.size() == 0 );                    // Convention: if vector is not empty, although there's no issues on serialization side, there's a good chance it's an user error / unintentional.
        if( !m_json.contains( key ) )
            return false;
        auto jtemp = m_json[key];
        if( jtemp.is_null( ) )
        {
            valueVector.clear( );
            return true;
        }
        if( !jtemp.is_array( ) )
            { assert( false ); return false; }              // Json node is there but it's not an array? This is likely an user error / unintentional!
        ptrVector.resize( jtemp.size() );
        for( size_t i = 0; i < ptrVector.size(); i++ )
        {
            auto itemTemp = jtemp[i];
            if( itemTemp.is_null( ) )
                { assert( false ); return false; }          // Convention: null array item? it's likely data corruption or user error, so assert.
            vaSerializer readNode( std::move( itemTemp ), true );
            ptrVector[i] = newObj( );
            if( !vaSerializerAdapter<ValueType>::Serialize( readNode, *ptrVector[i] ) )
                { assert( false ); return false; }          // Convention: if serialization of an individual array item failed, it's likely data corruption or user error, so assert.
        }
        return true;
    }
    else if( m_isWriting )
    {
        if( m_json.contains( key ) )
            { assert( false ); return false; }              // Convention: if key already in, assert and return false. Also, input is null is user error.
        json jarr; size_t ptrVectorSize = ptrVector.size();
        for( size_t i = 0; i < ptrVectorSize; i++ )
        {
            vaSerializer writeNode = vaSerializer::OpenWrite( vaSerializerAdapter<ValueType>::Type() );
            if( !vaSerializerAdapter<ValueType>::Serialize( writeNode, *ptrVector[i] ) )
                { assert( false ); return false; }          // Convention: Not being able to write is probably a code error so we assert and return false;
            jarr.push_back( writeNode.m_json );
        }
        m_json[key] = jarr;
        return true;
    }
    assert( false );
    return false;
}

template< typename BaseType >
inline bool vaSerializer::SerializeDynamicPtrVector( const string & key, std::vector<shared_ptr<BaseType>> & ptrVector,
    const std::function< shared_ptr<BaseType>( const string & typeName ) > & newObj,
    const std::function< const char * ( const BaseType & object ) > & typeOf,
    const std::function< bool( const string & typeName, vaSerializer & serializer, BaseType & object ) > & serialize )
{
    assert( key != "!type" );                               // This one is reserved and can't be used!
    assert( key != "" );                                    // 'Keyless' direct storage not supported for arrays/vectors

    if( m_isReading )
    {
        assert( ptrVector.size() == 0 );                    // Convention: if vector is not empty, although there's no issues on serialization side, there's a good chance it's an user error / unintentional.
        if( !m_json.contains( key ) )
            return false;
        auto jtemp = m_json[key];
        if( jtemp.is_null( ) )
        {
            valueVector.clear( );
            return true;
        }
        if( !jtemp.is_array( ) )
            { assert( false ); return false; }              // Json node is there but it's not an array? This is likely an user error / unintentional!
        ptrVector.resize( jtemp.size() );
        for( size_t i = 0; i < ptrVector.size(); i++ )
        {
            auto itemTemp = jtemp[i];
            if( itemTemp.is_null( ) )
                { assert( false ); return false; }          // Convention: null array item? it's likely data corruption or user error, so assert.
            vaSerializer readNode( std::move( itemTemp ), true );
            ptrVector[i] = newObj( readNode.Type( ) );
            if( !serialize( readNode.Type(), readNode, *ptrVector[i] ) )
                { assert( false ); return false; }          // Convention: if serialization of an individual array item failed, it's likely data corruption or user error, so assert.
        }
        return true;
    }
    else if( m_isWriting )
    {
        if( m_json.contains( key ) )
            { assert( false ); return false; }              // Convention: if key already in, assert and return false. Also, input is null is user error.
        json jarr; size_t ptrVectorSize = ptrVector.size();
        for( size_t i = 0; i < ptrVectorSize; i++ )
        {
            vaSerializer writeNode = vaSerializer::OpenWrite( typeOf( *ptrVector[i] ) );
            if( !serialize( writeNode.Type(), writeNode, *ptrVector[i] ) )
                { assert( false ); return false; }          // Convention: Not being able to write is probably a code error so we assert and return false;
            jarr.push_back( writeNode.m_json );
        }
        m_json[key] = jarr;
        return true;
    }
    assert( false );
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////
// vaSerializer adapters!
//
// Two easy options: 
//  - do the to-string/from-string approach bind with VA_STRING_SERIALIZER_ADAPTER
//  - manually specialize vaSerializerAdapter and rely on key-values
// Second option is more readable but takes more space in json.
////////////////////////////////////////////////////////////////////////////////////////////////

// Handling floats/doubles manually to correctly support +inf/-inf/NaN; see https://github.com/nlohmann/json/issues/70#issuecomment-285958089 for more info
std::string      S_ValueToString( const float & value );
bool             S_StringToValue( const std::string & str, float & value );
VA_STRING_SERIALIZER_ADAPTER( float, S_ValueToString, S_StringToValue )
std::string      S_ValueToString( const double & value );
bool             S_StringToValue( const std::string & str, double & value );
VA_STRING_SERIALIZER_ADAPTER( double, S_ValueToString, S_StringToValue )

std::string      S_ValueToString( const vaVector3 & value );
bool             S_StringToValue( const std::string & str, vaVector3 & value );
VA_STRING_SERIALIZER_ADAPTER( vaVector3, S_ValueToString, S_StringToValue )

std::string      S_ValueToString( const vaVector4 & value );
bool             S_StringToValue( const std::string & str, vaVector4 & value );
VA_STRING_SERIALIZER_ADAPTER( vaVector4, S_ValueToString, S_StringToValue )

std::string      S_ValueToString( const vaMatrix3x3 & value );
bool             S_StringToValue( const std::string & str, vaMatrix3x3 & value );
VA_STRING_SERIALIZER_ADAPTER( vaMatrix3x3, S_ValueToString, S_StringToValue )

std::string      S_ValueToString( const vaMatrix4x4 & value );
bool             S_StringToValue( const std::string & str, vaMatrix4x4 & value );
VA_STRING_SERIALIZER_ADAPTER( vaMatrix4x4, S_ValueToString, S_StringToValue )

template< >     struct vaSerializerAdapter < vaOrientedBoundingBox >
{
    static const char *     Type( )     { return ""; }
    static bool             Serialize( vaSerializer & serializer, vaOrientedBoundingBox & value )
    {
        if( !serializer.Serialize( "Center",    value.Center ) )    return false;
        if( !serializer.Serialize( "Extents",   value.Extents ) )   return false;
        if( !serializer.Serialize( "Axis",      value.Axis ) )      return false;
        return true;
    }
};

template< >     struct vaSerializerAdapter < vaBoundingBox >
{
    static const char *     Type( )     { return ""; }
    static bool             Serialize( vaSerializer & serializer, vaBoundingBox & value )
    {
        if( !serializer.Serialize( "Min",       value.Min ) )    return false;
        if( !serializer.Serialize( "Size",      value.Size ) )   return false;
        return true;
    }
};

template< >     struct vaSerializerAdapter < vaBoundingSphere >
{
    static const char *     Type( )     { return ""; }
    static bool             Serialize( vaSerializer & serializer, vaBoundingSphere & value )
    {
        if( !serializer.Serialize( "Center",    value.Center ) )    return false;
        if( !serializer.Serialize( "Radius",    value.Radius ) )    return false;
        return true;
    }
};


//////////////////////////////////////////////////////////////////////////
// USE EXAMPLE BELOW - used for testing while developing

#if 0
struct TestObjectBase
{
    int     a = 1;
    bool                    S_Serialize( vaSerializer& serializer )
    {
        return serializer.Serialize( "a", a, 0 );
    }

    virtual const char* GetType( ) const = 0;
};

struct TestObjectOne : TestObjectBase
{
    float   b = 2;

    TestObjectOne( ) { }
    TestObjectOne( int _a, float _b ) { a = _a; b = _b; }

    // internal support for vaSerializer SerializeObject
    static const char* S_Type( ) { return "TestObjectOne"; }
    bool                    S_Serialize( vaSerializer& serializer )
    {
        TestObjectBase::S_Serialize( serializer );
        serializer.Serialize( "b", b, 0.0f );
        return true;
    }
    virtual const char* GetType( ) const override { return S_Type( ); }
};

struct TestObjectTwo : TestObjectBase
{
    string  c = "3";

    TestObjectTwo( ) { }
    TestObjectTwo( int _a, const string& _c ) { a = _a; c = _c; }

    // internal support for vaSerializer SerializeObject
    static const char* S_Type( ) { return "TestObjectTwo"; }
    bool                    S_Serialize( vaSerializer& serializer )
    {
        TestObjectBase::S_Serialize( serializer );
        serializer.Serialize( "c", c, string( "0" ) );
        return true;
    }
    virtual const char* GetType( ) const override { return S_Type( ); }
};

const char* DiscriminatorGetType( const TestObjectBase& object )
{
    return object.GetType( );
}

shared_ptr<TestObjectBase> DiscriminatorNew( const string& typeName )
{
    if( typeName == "TestObjectOne" )
        return std::make_shared<TestObjectOne>( );
    if( typeName == "TestObjectTwo" )
        return std::make_shared<TestObjectTwo>( );
    else
    {
        assert( false ); return false;
    }
}

bool DiscriminatorSerialize( const string& typeName, vaSerializer& serializer, TestObjectBase& object )
{
    if( typeName == "TestObjectOne" )
        return static_cast<TestObjectOne&>( object ).S_Serialize( serializer );
    if( typeName == "TestObjectTwo" )
        return static_cast<TestObjectTwo&>( object ).S_Serialize( serializer );
    else
    {
        assert( false ); return false;
    }
}


bool Scene::SaveJSON( entt::registry& registry, const string& filePath, std::function<bool( entt::entity entity )> filter )
{
    vaSerializer serializer = vaSerializer::OpenWrite( "VanillaScene" );

    serializer.Serialize<string>( "name", registry.ctx<Scene::Name>( ) );

    int a = 1;
    serializer.Serialize<int>( "arg", a );

    serializer.Serialize<vaGUID>( "uid", registry.ctx<Scene::UID>( ) );

    TestObjectTwo TOTWO( 42, "42" );
    serializer.Serialize<TestObjectTwo>( "TOTWO", TOTWO );

    shared_ptr<TestObjectOne> TOONE = std::make_shared<TestObjectOne>( 77, 77.77f );
    serializer.SerializePtr<TestObjectOne>( "TOONE", TOONE );

    std::vector<shared_ptr<TestObjectOne>> v0 = { TOONE, std::make_shared<TestObjectOne>( 88, 88.88f ) };
    serializer.SerializePtrVector<TestObjectOne>( "V0", v0 );

    shared_ptr<TestObjectOne> x = std::make_shared<TestObjectOne>( );
    shared_ptr<TestObjectTwo> y = std::make_shared<TestObjectTwo>( );
    x->b = 42;
    y->c = "42";

    serializer.SerializeDynamicPtr<TestObjectBase>( "objOne", std::static_pointer_cast<TestObjectBase>( x ), DiscriminatorNew, DiscriminatorGetType, DiscriminatorSerialize );
    serializer.SerializeDynamicPtr<TestObjectBase>( "objTwo", std::static_pointer_cast<TestObjectBase>( y ), DiscriminatorNew, DiscriminatorGetType, DiscriminatorSerialize );

    std::vector<shared_ptr<TestObjectBase>> v1 = { std::make_shared<TestObjectTwo>( 55, "55" ), std::make_shared<TestObjectOne>( 99, 99.99f ) };
    serializer.SerializeDynamicPtrVector<TestObjectBase>( "V4", v1, DiscriminatorNew, DiscriminatorGetType, DiscriminatorSerialize );

    std::vector<int> vec1 = { 1, 2, 4, 5, 8, 9 };
    serializer.SerializeVector<int>( "NiceNumbers", vec1 );

    std::vector<TestObjectTwo> vec2 = { {1, "1"}, {2, "2"} };
    serializer.SerializeVector<TestObjectTwo>( "NiceNumbers2", vec2 );

    string arr1[3] = { "bla", "blo", "blimp" };
    serializer.SerializeArray<string>( "NiceStrings", arr1, countof( arr1 ) );

    serializer.Write( filePath );

    return false;
}

bool Scene::LoadJSON( entt::registry& registry, const string& filePath )
{
    vaSerializer serializer = vaSerializer::OpenRead( filePath, "VanillaScene" );

    serializer.Serialize<string>( "name", registry.ctx_or_set<Scene::Name>( ), "no_name" );

    int a;
    serializer.Serialize<int>( "arg", a, 3 );

    serializer.Serialize<vaGUID>( "uid", registry.ctx_or_set<Scene::UID>( ) );

    TestObjectTwo TOTWO;
    serializer.Serialize<TestObjectTwo>( "TOTWO", TOTWO );

    shared_ptr<TestObjectOne> TOONE;
    serializer.SerializePtr<TestObjectOne>( "TOONE", TOONE );

    std::vector<shared_ptr<TestObjectOne>> v0;
    serializer.SerializePtrVector<TestObjectOne>( "V0", v0 );

    shared_ptr<TestObjectBase> x; // = std::make_shared<TestObjectOne>( );
    shared_ptr<TestObjectBase> y; // = std::make_shared<TestObjectTwo>( );

    serializer.SerializeDynamicPtr<TestObjectBase>( "objOne", x, DiscriminatorNew, DiscriminatorGetType, DiscriminatorSerialize );
    serializer.SerializeDynamicPtr<TestObjectBase>( "objTwo", y, DiscriminatorNew, DiscriminatorGetType, DiscriminatorSerialize );

    std::vector<int> vec1;
    serializer.SerializeVector<int>( "NiceNumbers", vec1, { 3, 4, 5 } );

    std::vector<TestObjectTwo> vec2;
    serializer.SerializeVector<TestObjectTwo>( "NiceNumbers2", vec2 );

    std::vector<shared_ptr<TestObjectBase>> v4;
    serializer.SerializeDynamicPtrVector<TestObjectBase>( "V4", v4, DiscriminatorNew, DiscriminatorGetType, DiscriminatorSerialize );

    string arr1[3];
    serializer.SerializeArray<string>( "NiceStrings", arr1, countof( arr1 ) );

    return false;
}
#endif