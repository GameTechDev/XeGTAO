
#pragma once

#include "Core/vaCoreIncludes.h"
#include "Core/vaXMLSerialization.h"

#include "Core/vaUI.h"

namespace Vanilla
{

    // help serialize properties from/to XML and optionally provides editing via 
    class vaPropertyContainer : public vaUIPropertiesItem, public vaXMLSerializable
    {
    private:
        string const        m_name;

        int                 m_numDecimals;

    private:
        class PropertyItem
        {
        protected:
            string const        m_name;
            bool                m_hasDefault;
            bool                m_isUIVisible;
            bool                m_isUIEditable;

        protected:
            PropertyItem( const string & name, bool hasDefault, bool isUIVisible, bool isUIEditable ) : m_name( name ), m_hasDefault( hasDefault ), m_isUIVisible( isUIVisible ), m_isUIEditable( isUIEditable ) { }

        public:
            virtual ~PropertyItem( )    { }

        public:
            const string &      Name( ) const                       { return m_name; }

            virtual void        ImGuiEdit( int numDecimals )    = 0;

            template< typename T >
            bool                TemplatedNamedSerialize( T & value, bool hasDefault, T & defaultValue, vaXMLSerializer & serializer )
            {
                assert( serializer.GetVersion() > 0 );
                if( serializer.IsReading( ) )
                {
                    if( serializer.Serialize<T>( m_name.c_str(), value ) )
                        return true;
                    if( hasDefault )
                    {
                        value = defaultValue; 
                        return true;
                    }
                    return false;
                }
                if( serializer.IsWriting( ) )
                {
                    return serializer.Serialize<T>( m_name.c_str(), value );
                }
                assert( false );
                return false;
            }

            virtual bool        NamedSerialize( vaXMLSerializer & serializer ) = 0;
        };

        class PropertyItemBool : public PropertyItem
        {
            bool &              m_value;
            bool                m_defaultValue;

        public:
            PropertyItemBool( const string & name, bool & value, bool defaultValue, bool hasDefault, bool isUIVisible, bool isEditable )  : PropertyItem( name, hasDefault, isUIVisible, isEditable ), m_value( value ), m_defaultValue( defaultValue ) { };

            virtual bool        NamedSerialize( vaXMLSerializer & serializer ) override { return TemplatedNamedSerialize( m_value, m_hasDefault, m_defaultValue, serializer ); }
            virtual void        ImGuiEdit( int numDecimals );
        };

        class PropertyItemInt32 : public PropertyItem
        {
            int32 &             m_value;
            int32               m_defaultValue;
            int32               m_minVal;
            int32               m_maxVal;
            int32               m_editStep;

        public:
            PropertyItemInt32( const string & name, int32 & value, int32 defaultValue, bool hasDefault, bool isUIVisible, bool isEditable, int32 minVal = INT_MIN, int32 maxVal = INT_MAX, int32 editStep = 1 )  : PropertyItem( name, hasDefault, isUIVisible, isEditable ), m_value( value ), m_defaultValue( defaultValue ), m_minVal( minVal ), m_maxVal( maxVal ), m_editStep( editStep ) { };

            virtual bool        NamedSerialize( vaXMLSerializer & serializer ) override { return TemplatedNamedSerialize( m_value, m_hasDefault, m_defaultValue, serializer ); }
            virtual void        ImGuiEdit( int numDecimals );
        };

        class PropertyItemUInt32 : public PropertyItem
        {
            uint32 &             m_value;
            uint32               m_defaultValue;
            uint32               m_minVal;
            uint32               m_maxVal;
            uint32               m_editStep;

        public:
            PropertyItemUInt32( const string & name, uint32 & value, uint32 defaultValue, bool hasDefault, bool isUIVisible, bool isEditable, uint32 minVal = 0, uint32 maxVal = 0xFFFFFFFF, uint32 editStep = 1 )  : PropertyItem( name, hasDefault, isUIVisible, isEditable ), m_value( value ), m_defaultValue( defaultValue ), m_minVal( minVal ), m_maxVal( maxVal ), m_editStep( editStep ) { };

            virtual bool        NamedSerialize( vaXMLSerializer & serializer ) override { return TemplatedNamedSerialize( m_value, m_hasDefault, m_defaultValue, serializer ); }
            virtual void        ImGuiEdit( int numDecimals );
        };

        class PropertyItemInt64 : public PropertyItem
        {
            int64 &             m_value;
            int64               m_defaultValue;
            int64               m_minVal;
            int64               m_maxVal;
            int64               m_editStep;

        public:
            PropertyItemInt64( const string & name, int64 & value, int64 defaultValue, bool hasDefault, bool isUIVisible, bool isEditable, int64 minVal = INT64_MIN, int64 maxVal = INT64_MAX, int64 editStep = 1 )  : PropertyItem( name, hasDefault, isUIVisible, isEditable ), m_value( value ), m_defaultValue( defaultValue ), m_minVal( minVal ), m_maxVal( maxVal ), m_editStep( editStep ) { };

            virtual bool        NamedSerialize( vaXMLSerializer & serializer ) override { return TemplatedNamedSerialize( m_value, m_hasDefault, m_defaultValue, serializer ); }
            virtual void        ImGuiEdit( int numDecimals );
        };

        class PropertyItemFloat : public PropertyItem
        {
            float &             m_value;
            float               m_defaultValue;
            float               m_minVal;
            float               m_maxVal;
            float               m_editStep;

        public:
            PropertyItemFloat( const string & name, float & value, float defaultValue, bool hasDefault, bool isUIVisible, bool isEditable, float minVal = VA_FLOAT_LOWEST, float maxVal = VA_FLOAT_HIGHEST, float editStep = 0.1f )  : PropertyItem( name, hasDefault, isUIVisible, isEditable ), m_value( value ), m_defaultValue( defaultValue ), m_minVal( minVal ), m_maxVal( maxVal ), m_editStep( editStep ) { };

            virtual bool        NamedSerialize( vaXMLSerializer & serializer ) override { return TemplatedNamedSerialize( m_value, m_hasDefault, m_defaultValue, serializer ); }
            virtual void        ImGuiEdit( int numDecimals );
        };

        class PropertyItemDouble : public PropertyItem
        {
            double &             m_value;
            double               m_defaultValue;
            double               m_minVal;
            double               m_maxVal;
            double               m_editStep;

        public:
            PropertyItemDouble( const string & name, double & value, double defaultValue, bool hasDefault, bool isUIVisible, bool isEditable, double minVal = VA_DOUBLE_LOWEST, double maxVal = VA_DOUBLE_HIGHEST, double editStep = 0.1 )  : PropertyItem( name, hasDefault, isUIVisible, isEditable ), m_value( value ), m_defaultValue( defaultValue ), m_minVal( minVal ), m_maxVal( maxVal ), m_editStep( editStep ) { };

            virtual bool        NamedSerialize( vaXMLSerializer & serializer ) override { return TemplatedNamedSerialize( m_value, m_hasDefault, m_defaultValue, serializer ); }
            virtual void        ImGuiEdit( int numDecimals );
        };

        class PropertyItemString : public PropertyItem
        {
            string &            m_value;
            string              m_defaultValue;
            //bool                m_readOnly;

        public:
            PropertyItemString( const string & name, string & value, const string & defaultValue, bool hasDefault, bool isUIVisible, bool isEditable )  : PropertyItem( name, hasDefault, isUIVisible, isEditable ), m_value( value ), m_defaultValue( defaultValue ) { };

            virtual bool        NamedSerialize( vaXMLSerializer & serializer ) override { return TemplatedNamedSerialize( m_value, m_hasDefault, m_defaultValue, serializer ); }
            virtual void        ImGuiEdit( int numDecimals );
        };

        std::vector< unique_ptr<PropertyItem> >
                            m_properties;

    public:
        vaPropertyContainer( const string & name, int numDecimals = 3 ) : m_name( name ), m_numDecimals( numDecimals )     { assert( name != "" ); }
        ~vaPropertyContainer( )                                         { }

        const string &          Name( ) const                       { return m_name; }

        void                    RegisterProperty( const string & name, bool & value, bool defaultValue = false, bool hasDefault = false, bool isUIVisible = false, bool isEditable = false )                                                                                                { if( hasDefault ) value = defaultValue; m_properties.push_back( std::make_unique< PropertyItemBool>( name, value, defaultValue, hasDefault, isUIVisible, isEditable ) ); }
        void                    RegisterProperty( const string & name, int32 & value, int32 defaultValue = 0,   bool hasDefault = false, bool isUIVisible = false, bool isEditable = false, int32 minVal = INT_MIN, int32 maxVal = INT_MAX, int32 editStep = 1 )                            { if( hasDefault ) value = defaultValue; m_properties.push_back( std::make_unique< PropertyItemInt32>( name, value, defaultValue, hasDefault, isUIVisible, isEditable, minVal, maxVal, editStep ) ); }
        void                    RegisterProperty( const string & name, uint32 & value, uint32 defaultValue = 0, bool hasDefault = false, bool isUIVisible = false, bool isEditable = false, uint32 minVal = 0, uint32 maxVal = 0xFFFFFFFF, uint32 editStep = 1 )                            { if( hasDefault ) value = defaultValue; m_properties.push_back( std::make_unique< PropertyItemUInt32>( name, value, defaultValue, hasDefault, isUIVisible, isEditable, minVal, maxVal, editStep ) ); }
        void                    RegisterProperty( const string & name, int64 & value, int64 defaultValue = 0,   bool hasDefault = false, bool isUIVisible = false, bool isEditable = false, int64 minVal = INT64_MIN, int64 maxVal = INT64_MAX, int64 editStep = 1 )                        { if( hasDefault ) value = defaultValue; m_properties.push_back( std::make_unique< PropertyItemInt64>( name, value, defaultValue, hasDefault, isUIVisible, isEditable, minVal, maxVal, editStep ) ); }
        void                    RegisterProperty( const string & name, float & value, float defaultValue = 0,   bool hasDefault = false, bool isUIVisible = false, bool isEditable = false, float minVal = VA_FLOAT_LOWEST, float maxVal = VA_FLOAT_HIGHEST, float editStep = 0.1f )        { if( hasDefault ) value = defaultValue; m_properties.push_back( std::make_unique< PropertyItemFloat>( name, value, defaultValue, hasDefault, isUIVisible, isEditable, minVal, maxVal, editStep ) ); }
        void                    RegisterProperty( const string & name, double & value, double defaultValue = 0, bool hasDefault = false, bool isUIVisible = false, bool isEditable = false, double minVal = VA_DOUBLE_LOWEST, double maxVal = VA_DOUBLE_HIGHEST, double editStep = 0.1 )    { if( hasDefault ) value = defaultValue; m_properties.push_back( std::make_unique< PropertyItemDouble>( name, value, defaultValue, hasDefault, isUIVisible, isEditable, minVal, maxVal, editStep ) ); }
        void                    RegisterProperty( const string & name, string & value, string defaultValue = 0, bool hasDefault = false, bool isUIVisible = false, bool isEditable = false )                                                                                                { if( hasDefault ) value = defaultValue; m_properties.push_back( std::make_unique< PropertyItemString>( name, value, defaultValue, hasDefault, isUIVisible, isEditable ) ); }

    protected:
        bool                    Serialize( vaXMLSerializer & serializer ) override;

    public:
        virtual string                          UIPropertiesItemGetDisplayName( ) const override { return m_name; }
        virtual void                            UIPropertiesItemTick( vaApplicationBase & application, bool openMenu, bool hovered ) override;
    };

#define VA_PROPERTYCONTAINER_REGISTER( x, ... )      RegisterProperty( #x, x, __VA_ARGS__ )

}