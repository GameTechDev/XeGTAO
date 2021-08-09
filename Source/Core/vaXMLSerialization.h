#pragma once

#include "vaCore.h"

#include <type_traits>
#include <map>

#include "IntegratedExternals\vaTinyxml2Integration.h"

namespace Vanilla
{
    class vaXMLSerializer;

    class vaXMLSerializable
    {
    protected:
        vaXMLSerializable( ) { };
        virtual ~vaXMLSerializable( ) { };

    protected:
        friend class vaXMLSerializer;

        // By informal convention, SerializeOpenChildElement / SerializePopToParentElement are done by the caller before & after calling vaXMLSerializable::Serialize
        // For scenarios where an object names itself (does open/pop element itself), it might be best not to use vaXMLSerializable but just a simple public NamedSerialize() 
        virtual bool                                Serialize( vaXMLSerializer & serializer )   = 0;
    };

    // superset of vaXMLSerializable; needed only if support for TypedSerialize / TypedSerializeArray is needed
    class vaXMLSerializableObject : public vaXMLSerializable
    {
    protected:
        friend class vaXMLSerializer;

        // the type name must match the type registered with 'vaXMLSerializer::RegisterTypeConstructor'
        virtual const char *                        GetSerializableTypeName( ) const            = 0;
    };

    typedef std::function< shared_ptr<vaXMLSerializableObject> ( ) >    vaSerializableObjectConstructor;

    // SFINAE - https://en.cppreference.com/w/cpp/language/sfinae / https://stackoverflow.com/questions/87372/check-if-a-class-has-a-member-function-of-a-given-signature
    template<typename, typename T>
    struct has_serialize 
{
        static_assert(
            std::integral_constant<T, false>::value,
            "Second template parameter needs to be of function type." );
    };
    // specialization that does the checking
    template<typename C, typename Ret, typename... Args>
    struct has_serialize<C, Ret( Args... )> {
    private:
        template<typename T>
        static constexpr auto check( T* )
            -> typename
            std::is_same<
            decltype( std::declval<T>( ).Serialize( std::declval<Args>( )... ) ),
            Ret    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
            >::type;  // attempt to call it and see if the return type is correct

        template<typename>
        static constexpr std::false_type check( ... );

        typedef decltype( check<C>( 0 ) ) type;

    public:
        static constexpr bool value = type::value;
    };

    class vaXMLSerializer
    {
    private:
        tinyxml2::XMLPrinter            m_writePrinter;
        std::vector<string>             m_writeElementNameStack;
        std::vector<std::map<string, bool>>  m_writeElementNamesMapStack;
        int                             m_writeElementStackDepth    = 0;
        bool                            m_writeElementPrevWasOpen   = false;    // used to figure out if we just wrote a leaf or another element(s)

        tinyxml2::XMLDocument           m_readDocument;
        tinyxml2::XMLElement *          m_currentReadElement        = nullptr;

        bool                            m_isReading                 = false;
        bool                            m_isWriting                 = false;

        // version history:
        // -1 - no version tracking, all saved as attributes
        //  0 - added version tracking, not using attributes anymore (intended way of using XML)
        //  1 - vaXMLSerializable::Serialize should no longer be required to open their own sub-elements (although it should still be free to do it) - this totally breaks backward compatibility
        int                             m_formatVersion             = -1;

        std::map<string, vaSerializableObjectConstructor>
                                        m_objectConstructors;

    public:
        // parse data, set to loading mode
        vaXMLSerializer( const char * inputData, size_t dataSize )
        {
            InitReadingFromBuffer( inputData, dataSize );
        }

        // parse data, set to loading mode
        explicit vaXMLSerializer( vaFileStream & fileStream )
        {
            InitReadingFromFileStream( fileStream );
        }

        // parse data, set to loading mode
        explicit vaXMLSerializer( const wstring & filePath )
        {
            vaFileStream inFile;
            if( !inFile.Open( filePath, FileCreationMode::Open ) )
                VA_LOG_ERROR( L"vaXMLSerializer::WriterSaveToFile(%s) - unable to create file for saving", filePath.c_str( ) );
            else
                InitReadingFromFileStream( inFile );
        }

        // parse data, set to loading mode
        explicit vaXMLSerializer( const string & filePath )
        {
            vaFileStream inFile;
            if( !inFile.Open( filePath, FileCreationMode::Open ) )
                VA_LOG_ERROR( "vaXMLSerializer::WriterSaveToFile(%s) - unable to create file for saving", filePath.c_str( ) );
            else
                InitReadingFromFileStream( inFile );
        }

        // open printer, set to storing mode
        vaXMLSerializer( ) : m_writePrinter( nullptr, false, 0 )
        {
            m_isWriting = true;
            m_isReading = false;
            m_currentReadElement = nullptr;

            m_writeElementNamesMapStack.push_back( std::map<string, bool>{ } );

            // version info
            if( WriterOpenElement( "vaXMLSerializer" ) )
            {
                m_formatVersion = 1;
                m_writePrinter.PushText( m_formatVersion ); 
                WriterCloseElement( "vaXMLSerializer", true );
            }
        }

        ~vaXMLSerializer( )
        {
            assert( m_currentReadElement == nullptr );      // forgot to ReadPopToParentElement?
            assert( m_writeElementNameStack.size() == 0 );  // forgot to WriteCloseElement?
            if( IsWriting() )
                { assert( m_writeElementNamesMapStack.size() == 1 ); }
            else
                { assert( m_writeElementNamesMapStack.size() == 0 ); }

            assert( m_writeElementStackDepth == 0 );        // forgot to WriteCloseElement?
        }

        void InitReadingFromFileStream( vaFileStream& fileStream )
        {
            int64 fileLength = fileStream.GetLength( );
            if( fileLength == 0 )
            {
                assert( false ); return;
            }

            if( fileStream.GetPosition( ) != 0 )
                fileStream.Seek( 0 );

            char* buffer = new char[fileLength + 1];
            if( fileStream.Read( buffer, fileLength ) )
            {
                buffer[fileLength] = 0; // null-terminate
                InitReadingFromBuffer( buffer, fileLength + 1 );
            }
            else { assert( false ); } // error reading?
            delete[] buffer;
        }

        void InitReadingFromBuffer( const char * inputData, size_t dataSize )
        {
            assert( m_isReading == false );
            assert( m_isWriting == false );
            assert( m_currentReadElement == nullptr );
            m_isReading = m_readDocument.Parse( inputData, dataSize ) == tinyxml2::XMLError::XML_SUCCESS;
            assert( m_isReading ); // error parsing?

            // version info
            if( ReaderAdvanceToChildElement( "vaXMLSerializer" ) )
            {
                if( m_currentReadElement->QueryIntText( &m_formatVersion ) != tinyxml2::XML_SUCCESS )
                { assert( false ); }        // can't read version?
                if( m_formatVersion < 1 || m_formatVersion > 1 )
                { assert( false ); }        // unsupported version
                ReaderPopToParentElement( "vaXMLSerializer" );
            }
        }

        bool                        IsReading( ) const { return m_isReading; }
        bool                        IsWriting( ) const { return m_isWriting; }

        tinyxml2::XMLPrinter &      GetWritePrinter( ) { assert( m_isWriting ); return m_writePrinter; }

        tinyxml2::XMLDocument &     GetReadDocument( ) { assert( m_isReading ); return m_readDocument; }

        tinyxml2::XMLElement *      GetCurrentReadElement( );

    private:

        bool                        ReaderAdvanceToChildElement( const char * name );
        bool                        ReaderAdvanceToSiblingElement( const char * name );
        bool                        ReaderPopToParentElement( const char * nameToVerify );

        int                         ReaderCountChildren( const char * elementName, const char * childName = nullptr );

        bool                        ReadBoolAttribute( const char * name, bool & outVal )     const;
        bool                        ReadInt32Attribute( const char * name, int32 & outVal )   const;
        bool                        ReadUInt32Attribute( const char * name, uint32 & outVal ) const;
        bool                        ReadInt64Attribute( const char * name, int64 & outVal )   const;
        bool                        ReadFloatAttribute( const char * name, float & outVal )   const;
        bool                        ReadDoubleAttribute( const char * name, double & outVal ) const;
        bool                        ReadStringAttribute( const char * name, string & outVal ) const;

        bool                        ReadAttribute( const char * name, bool & outVal )       const { return ReadBoolAttribute( name, outVal ); }
        bool                        ReadAttribute( const char * name, int32 & outVal )      const { return ReadInt32Attribute( name, outVal ); }
        bool                        ReadAttribute( const char * name, uint32 & outVal )     const { return ReadUInt32Attribute( name, outVal ); }
        bool                        ReadAttribute( const char * name, int64 & outVal )      const { return ReadInt64Attribute( name, outVal ); }
        bool                        ReadAttribute( const char * name, float & outVal )      const { return ReadFloatAttribute( name, outVal ); }
        bool                        ReadAttribute( const char * name, double & outVal )     const { return ReadDoubleAttribute( name, outVal ); }
        bool                        ReadAttribute( const char * name, string & outVal )     const { return ReadStringAttribute( name, outVal ); }
        bool                        ReadAttribute( const char * name, vaGUID & val )        const;
        bool                        ReadAttribute( const char * name, vaVector3 & val )     const;
        bool                        ReadAttribute( const char * name, vaVector4 & val )     const;
        bool                        ReadAttribute( const char * name, vaMatrix4x4 & val )     const;
        bool                        ReadAttribute( const char * name, vaOrientedBoundingBox & val )     const;

        shared_ptr<vaXMLSerializableObject>
                                    MakeType( const string& typeName );

    private:

        bool                        WriterOpenElement( const char * name, bool mustBeUnique = true );
        bool                        WriterCloseElement( const char * nameToVerify, bool compactMode = false );

        bool                        WriteAttribute( const char * name, bool  outVal ) { assert( m_isWriting ); if( !m_isWriting ) return false; m_writePrinter.PushAttribute( name, outVal ); return true; }
        bool                        WriteAttribute( const char * name, int32  outVal ) { assert( m_isWriting ); if( !m_isWriting ) return false; m_writePrinter.PushAttribute( name, outVal ); return true; }
        bool                        WriteAttribute( const char * name, uint32  outVal ) { assert( m_isWriting ); if( !m_isWriting ) return false; m_writePrinter.PushAttribute( name, outVal ); return true; }
        bool                        WriteAttribute( const char * name, int64  outVal ) { assert( m_isWriting ); if( !m_isWriting ) return false; m_writePrinter.PushAttribute( name, outVal ); return true; }
        bool                        WriteAttribute( const char * name, float  outVal ) { assert( m_isWriting ); if( !m_isWriting ) return false; m_writePrinter.PushAttribute( name, outVal ); return true; }
        bool                        WriteAttribute( const char * name, double  outVal ) { assert( m_isWriting ); if( !m_isWriting ) return false; m_writePrinter.PushAttribute( name, outVal ); return true; }
        bool                        WriteAttribute( const char * name, const string & outVal ) { assert( m_isWriting ); if( !m_isWriting ) return false; m_writePrinter.PushAttribute( name, outVal.c_str( ) ); return true; }
        bool                        WriteAttribute( const char * name, vaGUID & val );
        bool                        WriteAttribute( const char * name, vaVector3 & val );
        bool                        WriteAttribute( const char * name, vaVector4 & val );
        bool                        WriteAttribute( const char * name, vaMatrix4x4 & val );
        bool                        WriteAttribute( const char * name, vaOrientedBoundingBox & val );

    private:
        bool                        SerializeInternal( bool & val )                     { if( m_isWriting ) { m_writePrinter.PushText( val ); return true; };                                   if( m_currentReadElement == nullptr || !m_isReading ) return false; return m_currentReadElement->QueryBoolText( &val )  == tinyxml2::XML_SUCCESS; }
        bool                        SerializeInternal( int32 & val )                    { if( m_isWriting ) { m_writePrinter.PushText( val ); return true; };                                   if( m_currentReadElement == nullptr || !m_isReading ) return false; return m_currentReadElement->QueryIntText( &val )  == tinyxml2::XML_SUCCESS; }
        bool                        SerializeInternal( uint32 & val )                   { if( m_isWriting ) { m_writePrinter.PushText( val ); return true; };                                   if( m_currentReadElement == nullptr || !m_isReading ) return false; return m_currentReadElement->QueryUnsignedText( &val )  == tinyxml2::XML_SUCCESS; }
        bool                        SerializeInternal( int64 & val )                    { if( m_isWriting ) { m_writePrinter.PushText( val ); return true; };                                   if( m_currentReadElement == nullptr || !m_isReading ) return false; return m_currentReadElement->QueryInt64Text( &val )  == tinyxml2::XML_SUCCESS; }
        bool                        SerializeInternal( float & val )                    { if( m_isWriting ) { m_writePrinter.PushText( val ); return true; };                                   if( m_currentReadElement == nullptr || !m_isReading ) return false; return m_currentReadElement->QueryFloatText( &val )  == tinyxml2::XML_SUCCESS; }
        bool                        SerializeInternal( double & val )                   { if( m_isWriting ) { m_writePrinter.PushText( val ); return true; };                                   if( m_currentReadElement == nullptr || !m_isReading ) return false; return m_currentReadElement->QueryDoubleText( &val )  == tinyxml2::XML_SUCCESS; }
        bool                        SerializeInternal( string & val )                   { if( m_isWriting ) { m_writePrinter.PushText( val.c_str(), false ); return true; };                    if( m_currentReadElement == nullptr || !m_isReading ) return false; val = (m_currentReadElement->GetText( ) == nullptr)?(""):(m_currentReadElement->GetText( )); return true; }
        bool                        SerializeInternal( pair<string, string> & val )     { return SerializeInternal( "first", val.first, std::false_type() ) && SerializeInternal( "second", val.second, std::false_type() ); }
        bool                        SerializeInternal( pair<string, bool> & val )       { return SerializeInternal( "first", val.first, std::false_type() ) && SerializeInternal( "second", val.second, std::false_type() ); }
        bool                        SerializeInternal( vaGUID & val )                   { if( m_isWriting ) { m_writePrinter.PushText( vaCore::GUIDToStringA( val ).c_str() ); return true; };  if( m_currentReadElement == nullptr || !m_isReading || m_currentReadElement->GetText( ) == nullptr ) return false; val = vaCore::GUIDFromString( m_currentReadElement->GetText() ); return true; }
        bool                        SerializeInternal( vaVector3 & val )                { if( m_isWriting ) { m_writePrinter.PushText( vaVector3::ToString( val ).c_str() ); return true; };    if( m_currentReadElement == nullptr || !m_isReading || m_currentReadElement->GetText( ) == nullptr ) return false; return vaVector3::FromString( m_currentReadElement->GetText(), val ); }
        bool                        SerializeInternal( vaVector4 & val )                { if( m_isWriting ) { m_writePrinter.PushText( vaVector4::ToString( val ).c_str() ); return true; };    if( m_currentReadElement == nullptr || !m_isReading || m_currentReadElement->GetText( ) == nullptr ) return false; return vaVector4::FromString( m_currentReadElement->GetText(), val ); }
        bool                        SerializeInternal( vaMatrix4x4 & val )              { if( m_isWriting ) { m_writePrinter.PushText( vaMatrix4x4::ToString( val ).c_str() ); return true; };    if( m_currentReadElement == nullptr || !m_isReading || m_currentReadElement->GetText( ) == nullptr ) return false; return vaMatrix4x4::FromString( m_currentReadElement->GetText(), val ); }
        bool                        SerializeInternal( vaOrientedBoundingBox & val )    { if( m_isWriting ) { m_writePrinter.PushText( vaOrientedBoundingBox::ToString( val ).c_str() ); return true; };    if( m_currentReadElement == nullptr || !m_isReading || m_currentReadElement->GetText( ) == nullptr ) return false; return vaOrientedBoundingBox::FromString( m_currentReadElement->GetText(), val ); }
        bool                        SerializeInternal( vaXMLSerializable & val )        { return val.Serialize( *this ); }

        bool                        TypedSerializeInternal( shared_ptr<vaXMLSerializableObject>& object );

        template< typename ValueType >
        bool                        SerializeInternal( const char * name, ValueType & val, std::false_type )   { if( !SerializeOpenChildElement( name ) ) return false; bool retVal = SerializeInternal( val ); if( !SerializePopToParentElement( name ) ) { assert( false ); return false; }; return retVal; }
        template< typename ValueType >
        bool                        SerializeInternal( const char* name, ValueType & val, std::true_type )     { if( !SerializeOpenChildElement( name ) ) return false; bool retVal = val.Serialize( *this ); if( !SerializePopToParentElement( name ) ) { assert( false ); return false; }; return retVal; }

        // bool                        SerializeInternal( const char * name, bool & val )                  { if( !SerializeOpenChildElement( name ) ) return false; bool retVal = SerializeInternal( val ); if( !SerializePopToParentElement( name ) ) { assert( false ); return false; }; return retVal; }
        // bool                        SerializeInternal( const char * name, int32 & val )                 { if( !SerializeOpenChildElement( name ) ) return false; bool retVal = SerializeInternal( val ); if( !SerializePopToParentElement( name ) ) { assert( false ); return false; }; return retVal; }
        // bool                        SerializeInternal( const char * name, uint32 & val )                { if( !SerializeOpenChildElement( name ) ) return false; bool retVal = SerializeInternal( val ); if( !SerializePopToParentElement( name ) ) { assert( false ); return false; }; return retVal; }
        // bool                        SerializeInternal( const char * name, int64 & val )                 { if( !SerializeOpenChildElement( name ) ) return false; bool retVal = SerializeInternal( val ); if( !SerializePopToParentElement( name ) ) { assert( false ); return false; }; return retVal; }
        // bool                        SerializeInternal( const char * name, float & val )                 { if( !SerializeOpenChildElement( name ) ) return false; bool retVal = SerializeInternal( val ); if( !SerializePopToParentElement( name ) ) { assert( false ); return false; }; return retVal; }
        // bool                        SerializeInternal( const char * name, double & val )                { if( !SerializeOpenChildElement( name ) ) return false; bool retVal = SerializeInternal( val ); if( !SerializePopToParentElement( name ) ) { assert( false ); return false; }; return retVal; }
        // bool                        SerializeInternal( const char * name, string & val )                { if( !SerializeOpenChildElement( name ) ) return false; bool retVal = SerializeInternal( val ); if( !SerializePopToParentElement( name ) ) { assert( false ); return false; }; return retVal; }
        // bool                        SerializeInternal( const char * name, vaGUID & val )                { if( !SerializeOpenChildElement( name ) ) return false; bool retVal = SerializeInternal( val ); if( !SerializePopToParentElement( name ) ) { assert( false ); return false; }; return retVal; }
        // bool                        SerializeInternal( const char * name, vaVector3 & val )             { if( !SerializeOpenChildElement( name ) ) return false; bool retVal = SerializeInternal( val ); if( !SerializePopToParentElement( name ) ) { assert( false ); return false; }; return retVal; }
        // bool                        SerializeInternal( const char * name, vaVector4 & val )             { if( !SerializeOpenChildElement( name ) ) return false; bool retVal = SerializeInternal( val ); if( !SerializePopToParentElement( name ) ) { assert( false ); return false; }; return retVal; }
        // bool                        SerializeInternal( const char * name, vaMatrix4x4 & val )           { if( !SerializeOpenChildElement( name ) ) return false; bool retVal = SerializeInternal( val ); if( !SerializePopToParentElement( name ) ) { assert( false ); return false; }; return retVal; }
        // bool                        SerializeInternal( const char * name, vaOrientedBoundingBox & val ) { if( !SerializeOpenChildElement( name ) ) return false; bool retVal = SerializeInternal( val ); if( !SerializePopToParentElement( name ) ) { assert( false ); return false; }; return retVal; }
        // bool                        SerializeInternal( const char * name, vaXMLSerializable & val )     { if( !SerializeOpenChildElement( name ) ) return false; bool retVal = SerializeInternal( val ); if( !SerializePopToParentElement( name ) ) { assert( false ); return false; }; return retVal; }
        // bool                        SerializeInternal( const char * name, pair<string, string> & val )  { if( !SerializeOpenChildElement( name ) ) return false; bool retVal = SerializeInternal( val ); if( !SerializePopToParentElement( name ) ) { assert( false ); return false; }; return retVal; }
        // bool                        SerializeInternal( const char * name, pair<string, bool> & val )    { if( !SerializeOpenChildElement( name ) ) return false; bool retVal = SerializeInternal( val ); if( !SerializePopToParentElement( name ) ) { assert( false ); return false; }; return retVal; }

        bool                        TypedSerializeInternal( const char* name, shared_ptr<vaXMLSerializableObject>& object );

    public:

        int                         GetVersion( ) const                                         { return m_formatVersion; }

        void                        RegisterTypeConstructor( const string & typeName, const vaSerializableObjectConstructor & constructorFunction );

        bool                        WriterSaveToFile( vaFileStream & fileStream );
        bool                        WriterSaveToFile( const wstring & filePath );
        bool                        WriterSaveToFile( const string & filePath );

        // Serialization helpers - both reader & writer
        bool                        SerializeOpenChildElement( const char * name );
        bool                        SerializePopToParentElement( const char * nameToVerify );

        // A version of Serialize that will simply, if reading, set default if value is missing and always returns true
        template< typename ValueType >
        bool                        Serialize( const char * name, ValueType & val, const ValueType & defaultVal );
        //
        // Generic version with no default value (returns false if no value read)
        template< typename ValueType >
        bool                        Serialize( const char * name, ValueType & val )                                         { return SerializeInternal< ValueType >( name, val, std::integral_constant<bool, has_serialize<ValueType, bool(vaXMLSerializer&)>::value>() ); }
        
        //

        // string-based versions
        template< typename ValueType >
        bool                        Serialize( const string & name, ValueType & val, const ValueType & defaultVal )         { return Serialize<ValueType>( name.c_str(), val, defaultVal ); }
        template< typename ValueType >
        bool                        Serialize( const string & name, ValueType & val )                                       { return Serialize<ValueType>( name.c_str(), val ); }

        // generic array read - user-provided setupCallback will either receive number of items in the array (when isReading == true) or needs to return the 
        // count itself (when isReading == false), while itemCallback provides handling of per-item serialization 
        template< typename ContainerType  >
        bool                        SerializeArrayGeneric( const char * containerName, ContainerType & container, std::function< void( bool isReading, ContainerType & container, int & itemCount ) > setupCallback, std::function< bool( vaXMLSerializer & serializer, ContainerType & container, int index ) > itemCallback );

        template< typename ItemType >
        bool                        SerializeArray( const char * containerName, std::vector< ItemType > & elements );

        template< typename ItemType >
        bool                        SerializeArray( const char * containerName, std::vector< shared_ptr<ItemType> > & elements );

        template< typename vaXMLSerializableObjectType >
        bool                        TypedSerializeArray( const char * containerName, std::vector< shared_ptr<vaXMLSerializableObjectType> > & elements );

        template< typename vaXMLSerializableObjectType >
        bool                        TypedSerialize( const char* name, shared_ptr<vaXMLSerializableObjectType>& object );
    };

    class vaSerializerScopedOpenChild
    {
        vaXMLSerializer &           m_serializer;
        string                      m_name;
        bool                        m_openedOk;

    public:
        vaSerializerScopedOpenChild( ) = delete;
        vaSerializerScopedOpenChild( vaXMLSerializer & serializer, const char * name, bool assertOnError = true ) : m_serializer(serializer), m_name(name)     { m_openedOk = serializer.SerializeOpenChildElement( name ); assert( m_openedOk || !assertOnError ); assertOnError; }
        vaSerializerScopedOpenChild( vaXMLSerializer & serializer, const string & name, bool assertOnError = true ) : m_serializer(serializer), m_name(name)   { m_openedOk = serializer.SerializeOpenChildElement( name.c_str() ); assert( m_openedOk || !assertOnError ); assertOnError; }
        ~vaSerializerScopedOpenChild( )  { if( m_openedOk ) { bool closedOk = m_serializer.SerializePopToParentElement( m_name.c_str() ); assert( closedOk ); closedOk; } }

        bool                        IsOK( ) const { return m_openedOk; }
    };

    inline void                     vaXMLSerializer::RegisterTypeConstructor( const string& typeName, const vaSerializableObjectConstructor& constructorFunction )
    {
        bool allOk = m_objectConstructors.insert( { typeName, constructorFunction } ).second;
        allOk; assert( allOk );
    }

    inline shared_ptr<vaXMLSerializableObject> vaXMLSerializer::MakeType( const string & typeName )
    {
        auto it = m_objectConstructors.find( typeName );
        if( it == m_objectConstructors.end() )
        {
            assert( false );
            return nullptr;
        }
        return it->second( );
    }

    inline tinyxml2::XMLElement * vaXMLSerializer::GetCurrentReadElement( ) { return m_currentReadElement; }
    inline bool                   vaXMLSerializer::ReaderAdvanceToChildElement( const char * name )
    {
        assert( m_isReading ); if( !m_isReading ) return false;
        tinyxml2::XMLElement * e = ( m_currentReadElement != nullptr ) ? ( m_currentReadElement->FirstChildElement( name ) ) : ( m_readDocument.FirstChildElement( name ) );
        if( e != nullptr ) { m_currentReadElement = e; return true; } return false;
    }
    inline bool                   vaXMLSerializer::ReaderAdvanceToSiblingElement( const char * name )
    {
        assert( m_isReading ); if( !m_isReading ) return false;
        tinyxml2::XMLElement * e = ( m_currentReadElement != nullptr ) ? ( m_currentReadElement->NextSiblingElement( name ) ) : ( m_readDocument.NextSiblingElement( name ) );
        if( e != nullptr )
        {
            m_currentReadElement = e; return true;
        }
        return false;
    }
    inline bool                   vaXMLSerializer::ReaderPopToParentElement( const char * nameToVerify )
    {
        assert( m_isReading ); if( !m_isReading ) return false;
        if( nameToVerify != nullptr )
        {
            assert( m_currentReadElement != nullptr && strncmp( m_currentReadElement->Name(), nameToVerify, 32768 ) == 0 );
        }

        if( m_currentReadElement == nullptr )
            return false;
        tinyxml2::XMLElement * e = ( m_currentReadElement->Parent( ) != nullptr ) ? ( m_currentReadElement->Parent( )->ToElement( ) ) : ( nullptr );
        m_currentReadElement = e;
        return true;
    }
    inline bool                   vaXMLSerializer::ReadBoolAttribute( const char * name, bool & outVal )   const
    {
        assert( m_isReading ); if( !m_isReading ) return false;
        if( m_currentReadElement == nullptr )   return false;
        return m_currentReadElement->QueryBoolAttribute( name, &outVal ) == tinyxml2::XML_SUCCESS;
    }
    inline bool                   vaXMLSerializer::ReadInt32Attribute( const char * name, int32 & outVal )   const
    {
        assert( m_isReading ); if( !m_isReading ) return false;
        if( m_currentReadElement == nullptr )   return false;
        return m_currentReadElement->QueryIntAttribute( name, &outVal ) == tinyxml2::XML_SUCCESS;
    }
    inline bool                   vaXMLSerializer::ReadUInt32Attribute( const char * name, uint32 & outVal ) const
    {
        assert( m_isReading ); if( !m_isReading ) return false;
        if( m_currentReadElement == nullptr )   return false;
        return m_currentReadElement->QueryUnsignedAttribute( name, &outVal ) == tinyxml2::XML_SUCCESS;
    }
    inline bool                   vaXMLSerializer::ReadInt64Attribute( const char * name, int64 & outVal )   const
    {
        assert( m_isReading ); if( !m_isReading ) return false;
        if( m_currentReadElement == nullptr )   return false;
        return m_currentReadElement->QueryInt64Attribute( name, &outVal ) == tinyxml2::XML_SUCCESS;
    }
    inline bool                   vaXMLSerializer::ReadFloatAttribute( const char * name, float & outVal )   const
    {
        assert( m_isReading ); if( !m_isReading ) return false;
        if( m_currentReadElement == nullptr )   return false;
        return m_currentReadElement->QueryFloatAttribute( name, &outVal ) == tinyxml2::XML_SUCCESS;
    }
    inline bool                   vaXMLSerializer::ReadDoubleAttribute( const char * name, double & outVal ) const
    {
        assert( m_isReading ); if( !m_isReading ) return false;
        if( m_currentReadElement == nullptr )   return false;
        return m_currentReadElement->QueryDoubleAttribute( name, &outVal ) == tinyxml2::XML_SUCCESS;
    }
    inline bool                   vaXMLSerializer::ReadStringAttribute( const char * name, string & outVal ) const
    {
        assert( m_isReading ); if( !m_isReading ) return false;
        if( m_currentReadElement == nullptr )   return false;
        const tinyxml2::XMLAttribute * a = ( ( const tinyxml2::XMLElement * )m_currentReadElement )->FindAttribute( name );
        if( a != nullptr )
        {
            outVal = a->Value( );
            return true;
        }
        return false;
    }

    inline bool                 vaXMLSerializer::WriterOpenElement( const char * name, bool mustBeUnique )
    {
        assert( m_isWriting ); if( !m_isWriting ) return false;

        if( mustBeUnique && m_writeElementNamesMapStack.back( ).find( string( name ) ) != m_writeElementNamesMapStack.back( ).end( ) )
        {
            assert( false ); // element with the same name already exists - this is not permitted except for arrays
            return false;
        }

        m_writePrinter.OpenElement( name );
        m_writeElementStackDepth++;
        m_writeElementNameStack.push_back( name );
        m_writeElementPrevWasOpen = true;

        m_writeElementNamesMapStack.push_back( std::map<string, bool>{ } );  // add empty map for child element's duplicate name tracking
        return true;
    }

    inline bool                 vaXMLSerializer::WriterCloseElement( const char * nameToVerify, bool compactMode )
    {
        if( nameToVerify != nullptr )
        {
            assert( m_writeElementNameStack.size() > 0 && m_writeElementNameStack.back() == nameToVerify );
        }
        m_writeElementNamesMapStack.pop_back( ); // go back to parent element map of names
        m_writeElementNamesMapStack.back().insert( std::pair<string, bool>( m_writeElementNameStack.back(), true ) ); // add this one to the list

        assert( m_isWriting ); if( !m_isWriting ) return false;
        assert( m_writeElementStackDepth > 0 ); if( m_writeElementStackDepth <= 0 ) return false;
        m_writePrinter.CloseElement( compactMode );
        m_writeElementNameStack.pop_back();
        m_writeElementStackDepth--;
        m_writeElementPrevWasOpen = false;

        return true;
    }

    inline bool                 vaXMLSerializer::WriterSaveToFile( vaFileStream & fileStream )
    {
        assert( m_isWriting ); if( !m_isWriting ) return false;
        fileStream.Seek( 0 );
        fileStream.Truncate( );
        fileStream.Write( GetWritePrinter( ).CStr( ), GetWritePrinter( ).CStrSize( ) - 1 );
        fileStream.Flush( );
        return true;
    }

    inline bool                 vaXMLSerializer::WriterSaveToFile( const wstring & filePath )
    {
        vaFileStream outFile;
        if( !outFile.Open( filePath, FileCreationMode::Create ) )
        {
            VA_LOG_ERROR( L"vaXMLSerializer::WriterSaveToFile(%s) - unable to create file for saving", filePath.c_str( ) );
            return false;
        }
        return WriterSaveToFile( outFile );
    }

    inline bool                 vaXMLSerializer::WriterSaveToFile( const string & filePath )
    {
        vaFileStream outFile;
        if( !outFile.Open( filePath, FileCreationMode::Create ) )
        {
            VA_LOG_ERROR( "vaXMLSerializer::WriterSaveToFile(%s) - unable to create file for saving", filePath.c_str( ) );
            return false;
        }
        return WriterSaveToFile( outFile );
    }

    inline bool                 vaXMLSerializer::ReadAttribute( const char * name, vaGUID & val ) const
    {
        string uiid;
        if( !ReadStringAttribute( name, uiid ) )
            { assert(false); return false; }

        val = vaCore::GUIDFromString( uiid );
        return true;
    }

    inline bool                 vaXMLSerializer::WriteAttribute( const char * name, vaGUID & val )
    {
        return WriteAttribute( name, vaCore::GUIDToStringA( val ) );
    }

    inline bool                 vaXMLSerializer::ReadAttribute( const char * name, vaVector3 & val )     const
    {
        string valStr;
        if( !ReadStringAttribute( name, valStr ) )
            { assert(false); return false; }

        return vaVector3::FromString( valStr, val );
    }
    inline bool                 vaXMLSerializer::WriteAttribute( const char * name, vaVector3 & val )
    {
        return WriteAttribute( name, vaVector3::ToString( val ) );
    }

    inline bool                 vaXMLSerializer::ReadAttribute( const char * name, vaVector4 & val )     const
    {
        string valStr;
        if( !ReadStringAttribute( name, valStr ) )
            { assert(false); return false; }

        return vaVector4::FromString( valStr, val );
    }
    inline bool                 vaXMLSerializer::WriteAttribute( const char * name, vaVector4 & val )
    {
        return WriteAttribute( name, vaVector4::ToString( val ) );
    }

    inline bool                 vaXMLSerializer::ReadAttribute( const char* name, vaMatrix4x4 & val )     const
    {
        string valStr;
        if( !ReadStringAttribute( name, valStr ) )
        {
            assert( false ); return false;
        }

        return vaMatrix4x4::FromString( valStr, val );
    }
    inline bool                 vaXMLSerializer::WriteAttribute( const char* name, vaMatrix4x4 & val )
    {
        return WriteAttribute( name, vaMatrix4x4::ToString( val ) );
    }

    inline bool                 vaXMLSerializer::ReadAttribute( const char* name, vaOrientedBoundingBox & val )     const
    {
        string valStr;
        if( !ReadStringAttribute( name, valStr ) )
        {
            assert( false ); return false;
        }

        return vaOrientedBoundingBox::FromString( valStr, val );
    }
    inline bool                 vaXMLSerializer::WriteAttribute( const char* name, vaOrientedBoundingBox & val )
    {
        return WriteAttribute( name, vaOrientedBoundingBox::ToString( val ) );
    }

    inline bool                 vaXMLSerializer::SerializeOpenChildElement( const char * name ) 
    { 
        if( m_isWriting ) 
            return WriterOpenElement( name );   
        if( m_isReading ) 
            return ReaderAdvanceToChildElement( name );
        assert( false ); 
        return false; 
    }

    template< typename ValueType >
    inline bool                 vaXMLSerializer::Serialize( const char * name, ValueType & val, const ValueType & defaultVal ) 
    { 
        if( m_isWriting ) return SerializeInternal( name, val, std::false_type() ); 
        if( m_isReading ) 
        {
            bool ret = false;
            if( SerializeOpenChildElement( name ) )
            {
                ret = SerializeInternal( val ); 
                if( !SerializePopToParentElement( name ) ) 
                { assert( false ); return false; }; 
            }
            if( !ret )
                val = defaultVal;
            return true;    // always return true
        }
        assert( false );
        return false;
    }

    inline bool                 vaXMLSerializer::SerializePopToParentElement( const char * nameToVerify ) 
    { 
        if( m_isWriting ) 
        {
            return WriterCloseElement( nameToVerify );
        }
        if( m_isReading ) 
        {   
            return ReaderPopToParentElement( nameToVerify ); 
        }
        assert( false ); 
        return false; 
    }

    inline int vaXMLSerializer::ReaderCountChildren( const char * elementName, const char * childName )
    {
        // assert( m_formatVersion >= 0 );
        if( !m_isReading )
        {
            assert( false );
            return -1;
        }

        if( !ReaderAdvanceToChildElement( elementName ) ) 
            return -1;

        int counter = 0;
        if( ReaderAdvanceToChildElement( childName ) )
        {
            do
            {
                counter++;
            } while( ReaderAdvanceToSiblingElement( childName ) );
            ReaderPopToParentElement( childName );
        }

        SerializePopToParentElement( elementName );
        return counter;
    }

    template< typename ContainerType >
    inline bool vaXMLSerializer::SerializeArrayGeneric(  const char * containerName, ContainerType & container, std::function< void( bool isReading, ContainerType & container, int & itemCount ) > setupCallback, std::function< bool( vaXMLSerializer & serializer, ContainerType & container, int index ) > itemCallback )
    {
        // assert( m_formatVersion >= 0 );
        assert( containerName != nullptr );
        const char * itemName = "_item_";
        int itemCount = 0;

        if( IsReading() )
        {
            itemCount = ReaderCountChildren( containerName, nullptr );
            if( itemCount < 0 )
                return false;
            int itemCountCopy = itemCount;
            setupCallback( true, container, itemCountCopy );
            assert( itemCountCopy == itemCount ); // it is invalid to change itemCount in the callback during isReading
        }
        else
        {
            setupCallback( false, container, itemCount );
            if( itemCount < 0 )
                return false;
        }

        if( !SerializeOpenChildElement( containerName ) )
            return false;

        bool allOk = true;

        if( m_isReading ) 
        { 
            // just an additional sanity check
            bool isArray = false;
            if( !ReadAttribute( "array", isArray ) || !isArray )
                { assert( false ); allOk = false; }

            int counter = 0;
            if( ReaderAdvanceToChildElement( nullptr ) )
            {
                do
                {
                    allOk &= itemCallback( *this, container, counter );
                    counter++;
                } while( ReaderAdvanceToSiblingElement( nullptr ) && counter < itemCount );
                ReaderPopToParentElement( nullptr );
            }
            assert( counter == itemCount ); // must be the same as returned by ReaderCountChildren
            allOk &= counter == itemCount;
        }
        else if( m_isWriting )
        {
            // just an additional sanity check
            if( m_formatVersion >= 0 )
                WriteAttribute( "array", true );

            for( int i = 0; i < itemCount; i++ )
            {
                WriterOpenElement( itemName, false );
                allOk &= itemCallback( *this, container, i );
                WriterCloseElement( itemName, m_writeElementPrevWasOpen );  // if there were no sub-elements (itemName element contains only data) then use compact mode!
            }
        }
        else { assert( false ); allOk = false; }

        allOk &= SerializePopToParentElement( containerName );
        assert( allOk );
        return allOk;
    }

    template< typename ItemType >
    inline bool vaXMLSerializer::SerializeArray( const char * containerName, std::vector< ItemType > & elements )  
    { 
        assert( m_formatVersion >= 0 );
        return SerializeArrayGeneric<std::vector<ItemType>>( containerName, elements, 
            [ ] ( bool isReading, std::vector<ItemType> & container, int & itemCount )
            { 
                if( isReading )
                    container.resize( itemCount );
                else
                    itemCount = (int)container.size();
            }, 
            [ ] ( vaXMLSerializer & serializer, std::vector<ItemType> & container, int index ) 
            { 
                if constexpr( has_serialize<ItemType, bool(vaXMLSerializer&)>::value )
                    return container[index].Serialize( serializer );
                else
                    return serializer.SerializeInternal( container[index] ); 
            } 
            ); 
    }

    template< typename ItemType >
    inline bool vaXMLSerializer::SerializeArray( const char * containerName, std::vector< shared_ptr<ItemType> > & elements )  
    {
        assert( m_formatVersion >= 0 );
        return SerializeArrayGeneric<std::vector<shared_ptr<ItemType>>>( containerName, elements, 
            [ ] ( bool isReading, std::vector<shared_ptr<ItemType>> & container, int & itemCount )
            { 
                if( isReading )
                {
                    container.resize( itemCount );
                    for( auto & item : container )
                        item = std::make_shared<ItemType>( );
                }
                else
                    itemCount = (int)container.size();
            }, 
            [ ] ( vaXMLSerializer & serializer, std::vector<shared_ptr<ItemType>> & container, int index ) 
            { 
                return serializer.SerializeInternal( *std::static_pointer_cast<vaXMLSerializable, ItemType>( container[index] ) ); 
            } 
            ); 
    }

    template< typename vaXMLSerializableObjectType >
    bool vaXMLSerializer::TypedSerialize( const char* name, shared_ptr<vaXMLSerializableObjectType>& object )
    {
        bool retVal = false;
        shared_ptr<vaXMLSerializableObject> _object = nullptr;
        if( IsReading( ) )
        {
            retVal = TypedSerializeInternal( name, _object);
            if( retVal && _object != nullptr )
            {
                object = std::dynamic_pointer_cast<vaXMLSerializableObjectType, vaXMLSerializableObject>( _object );
                assert( object != nullptr );
                return object != nullptr;
            }
        } 
        else if( IsWriting( ) )
        {
            _object = ( object == nullptr ) ? ( nullptr ) : ( std::static_pointer_cast<vaXMLSerializableObject, vaXMLSerializableObjectType>( object ) );
            retVal = TypedSerializeInternal( name, _object );
        }

        return retVal;
    }

    inline bool vaXMLSerializer::TypedSerializeInternal( shared_ptr<vaXMLSerializableObject>& object )
    {
        string typeName = IsReading( ) ? ( "" ) : ( object->GetSerializableTypeName( ) );

        struct TypeNameConv
        {
            const string xmlNamePrefix = "_typename_";
            string ToTypeName( const string& xmlName )
            {
                if( xmlName.length( ) < xmlNamePrefix.length( ) + 1 )
                {
                    assert( false ); return "";
                }
                return xmlName.substr( xmlNamePrefix.length( ) );
            }
            string ToXmlName( const string& typeName ) { return xmlNamePrefix + typeName; }
        } typeNameConv;

        if( IsReading( ) )
        {
            if( !ReaderAdvanceToChildElement( nullptr ) )
            {
                object = nullptr;
                return true;    // this is ok, nothing to see here
            }

            typeName = typeNameConv.ToTypeName( m_currentReadElement->Name( ) );
            if( typeName == "" )
            {
                SerializePopToParentElement( nullptr ); assert( false ); return false;  // this is bad - not a type kind of serializable object
            }

            object = MakeType( typeName );
            bool allOk = object != nullptr;
            assert( allOk );
            if( allOk )
            {
                object->Serialize( *this );
                allOk = ReaderPopToParentElement( nullptr );
                assert( allOk );
            }
            return allOk;
        }
        else if( IsWriting( ) )
        {
            string xmlTypeName = typeNameConv.ToXmlName( typeName );
            bool allOk = WriterOpenElement( xmlTypeName.c_str( ), false );
            assert( allOk );
            if( allOk )
            {
                allOk &= object->Serialize( *this );
                assert( allOk );
                allOk &= WriterCloseElement( xmlTypeName.c_str( ) );
                assert( allOk );
                return allOk;
            }
        }

        return false;
    }

    inline bool vaXMLSerializer::TypedSerializeInternal( const char * name, shared_ptr<vaXMLSerializableObject> & object )
    {
        if( IsReading() )
        { assert( object == nullptr ); object = nullptr; }
        
        if( !SerializeOpenChildElement( name ) ) 
            return false; 

        // if writing and null, this should be just fine
        if( IsWriting( ) && object == nullptr )
        {
            if( !SerializePopToParentElement( name ) )
            {
                assert( false ); return false;
            };
            return true;
        }

        bool retValue = TypedSerializeInternal( object );

        if( !SerializePopToParentElement( name ) ) 
            { assert( false ); return false; };
        return retValue;
    }

    template< typename vaXMLSerializableObjectType >
    inline bool vaXMLSerializer::TypedSerializeArray( const char * containerName, std::vector< shared_ptr<vaXMLSerializableObjectType> >& elements )
    {
        assert( m_formatVersion >= 0 );
        return SerializeArrayGeneric<std::vector<shared_ptr<vaXMLSerializableObjectType>>>( containerName, elements,
            [ ]( bool isReading, std::vector<shared_ptr<vaXMLSerializableObjectType>>& container, int& itemCount )
        {
            if( isReading )
            {
                container.resize( itemCount );
                for( auto& item : container )
                    item = nullptr;
            }
            else
                itemCount = (int)container.size( );
        },
            [ ]( vaXMLSerializer& serializer, std::vector<shared_ptr<vaXMLSerializableObjectType>>& container, int index )
        {
            shared_ptr<vaXMLSerializableObject> _object = (serializer.IsWriting())?( std::static_pointer_cast<vaXMLSerializableObject, vaXMLSerializableObjectType>(container[index]) ) : (nullptr);
            bool allOk = serializer.TypedSerializeInternal( _object );
            assert( allOk );
            if( serializer.IsReading() )
                container[index] = std::static_pointer_cast<vaXMLSerializableObjectType, vaXMLSerializableObject>(_object);
            return allOk;
        }
        );
    }


}