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

#include "IntegratedExternals/vaNLJsonIntegration.h"

namespace Vanilla
{

    // This is a wrapper that for nlohmann::json that exposes a simple serialization interface.
    // It reserves "!type" key for (optionally) defining type.
    class vaSerializer
    {
        nlohmann::json          m_json;

        bool                    m_isReading = false;
        bool                    m_isWriting = false;

        string                  m_type      = "";

    protected:
        // generic
        vaSerializer( nlohmann::json && src, bool forReading );

        // move
        vaSerializer( vaSerializer && src );

        // this one is for writing
        vaSerializer( const string & type );

    public:
        ~vaSerializer( );

    protected:
        // only to be used by the vaSerializerAdapter!
        template< typename _Type > friend  struct vaSerializerAdapter;
        nlohmann::json &        JSON( )                                                     { return m_json; }
        const nlohmann::json &  JSON( ) const                                               { return m_json; }

    public:
        bool                    Write( vaStream & stream );
        bool                    Write( const string & filePath );

    public:
        static vaSerializer     OpenWrite( const string & type = "" )                       { return vaSerializer(type); }
        static vaSerializer     OpenRead( const string & filePath, const string & assertType = "" );

    public:
        const string &          Type( ) const                                               { return m_type; }
        bool                    IsReading( ) const                                          { return m_isReading; }
        bool                    IsWriting( ) const                                          { return m_isWriting; }
        bool                    Has( const string & key ) const                             { return m_json.contains( key ); }

        template< typename ValueType >
        bool                    Serialize( const string & key, ValueType & value );
        // This version allows for a default value which will be returned if the value is missing during the read, and always return true.
        template< typename ValueType >
        bool                    Serialize( const string & key, ValueType & value, const ValueType & defaultValue );
        
        // This version allows for using a custom callback to serialize a generic object (presumably captured in the lambda)
        bool                    Serialize( const string & key, const string & typeName, const std::function< bool(vaSerializer & serializer)> & serialize );

        //////////////////////////////////////////////////////////////////////////
        // These handle serialization of objects by pointer for known types.
        //
        // <disabling raw pointers for now because there was no need but should be easy to implement if required>
        //
        // template< typename ValueType >
        // bool                    SerializeObjectPtr( const string & key, ValueType * & object, const std::function< ValueType*() > & newObj = [](){ return new ValueType(); } );
        //
        // in IsWriting mode, object has to be non-nullptr; in IsReadingMode object has to be nullptr and will be constructed
        template< typename ValueType >
        bool                    SerializePtr( const string & key, shared_ptr<ValueType> & object, const std::function< shared_ptr<ValueType> ( ) > & newObj = [ ]( ) { return std::make_shared<ValueType>( ); } );
        
        //////////////////////////////////////////////////////////////////////////
        // These can serialize any object (that matches the base type) but it does require the user to provide callbacks for handling the types.
        template< typename BaseType >
        bool                    SerializeDynamicPtr( const string & key, shared_ptr<BaseType> & object, 
            const std::function< shared_ptr<BaseType> ( const string & typeName ) > & newObj, 
            const std::function< const char * ( const BaseType & object ) > & typeOf, 
            const std::function< bool ( const string & typeName, vaSerializer & serializer, BaseType & object ) > & serialize );

        
        //////////////////////////////////////////////////////////////////////////
        // Vectors & arrays
        template< typename ValueType >
        bool                    SerializeVector( const string & key, std::vector<ValueType> & valueVector, const ValueType & initValue = ValueType() );
        template< typename ValueType >
        bool                    SerializeVector( const string & key, std::vector<ValueType> & valueVector, const std::vector<ValueType> & defaultValue, const ValueType & initValue = ValueType() );
        template< typename ValueType >
        bool                    SerializeArray( const string & key, ValueType * valueArray, size_t arrayCount );
        //
        template< typename ValueType >
        bool                    SerializePtrVector( const string & key, std::vector<shared_ptr<ValueType>> & ptrVector, const std::function< shared_ptr<ValueType> ( ) > & newObj = [ ]( ) { return std::make_shared<ValueType>( ); } );
        template< typename BaseType >
        bool                    SerializeDynamicPtrVector( const string & key, std::vector<shared_ptr<BaseType>> & ptrVector,
            const std::function< shared_ptr<BaseType>( const string & typeName ) > & newObj,
            const std::function< const char * ( const BaseType & object ) > & typeOf,
            const std::function< bool( const string & typeName, vaSerializer & serializer, BaseType & object ) > & serialize );
    };

    // This has to be template-specialized for every type that wants to support serialization with vaSerializer. 
    // You have to either support non-specialized version that has provides static S_Type and S_Serialize functions, or 
    // support them within a specialization of vaSerializerAdapter below.
    //
    // If "Type" returns "", the "!type" key is not written to the storage, and type checking and SerializePtrDynamic
    // are not supported. Useful for simple ('value') types.
    template< typename _Type >
    struct vaSerializerAdapter
    {
        static const char *     Type( )                                                     
        { 
            return _Type::S_Type(); 
        }
        static bool             Serialize( vaSerializer & serializer, _Type & value ) 
        { 
            // compile error below? you likely need to implement vaSerializerAdapter for your type! if it's simple (supported by nlohmann::json out of the box), just use VA_DEFAULT_SERIALIZER_ADAPTER macro
            return value.S_Serialize( serializer );
        }
    };

    #include "vaSerializer.inl"
}

