
#include "vaPropertyContainer.h"

#include "IntegratedExternals/vaImguiIntegration.h"

using namespace Vanilla;

void vaPropertyContainer::PropertyItemBool::ImGuiEdit( int numDecimals )
{
    numDecimals; // unreferenced

    if( !m_isUIVisible ) return;

#ifdef VA_IMGUI_INTEGRATION_ENABLED

    if( m_isUIEditable )
        ImGui::Checkbox( m_name.c_str(), &m_value );
    else
    {
        ImGui::LabelText( "%s", ((m_value)?"true":"false") );
    }
#endif
}

void vaPropertyContainer::PropertyItemInt32::ImGuiEdit( int numDecimals )
{
    numDecimals; // unreferenced

    if( !m_isUIVisible ) return;

#ifdef VA_IMGUI_INTEGRATION_ENABLED

    if( m_isUIEditable )
    {
        int32 tmpValue = m_value;
        if( ImGui::InputInt( m_name.c_str(), &tmpValue, m_editStep, m_editStep * 10, ImGuiInputTextFlags_EnterReturnsTrue ) )
            m_value = vaMath::Clamp( tmpValue, m_minVal, m_maxVal );
    }
    else
    {
        ImGui::LabelText( "%s", "%d", m_value );
    }
#endif
}

void vaPropertyContainer::PropertyItemUInt32::ImGuiEdit( int numDecimals )
{
    numDecimals; // unreferenced

    if( !m_isUIVisible ) return;
#ifdef VA_IMGUI_INTEGRATION_ENABLED

    if( m_isUIEditable )
    {
        //ImGui::InputInt( m_name.c_str(), &m_value, m_editStep, m_editStep * 10 );
        //m_value = vaMath::Clamp( m_value, m_minVal, m_maxVal );
        ImGui::LabelText( "%s (uint32 editing not supported)", "%u", m_value );
    }
    else
    {
        ImGui::LabelText( "%s", "%u", m_value );
    }
#endif
}

void vaPropertyContainer::PropertyItemInt64::ImGuiEdit( int numDecimals )
{
    numDecimals; // unreferenced

    if( !m_isUIVisible ) return;

#ifdef VA_IMGUI_INTEGRATION_ENABLED

    if( m_isUIEditable )
    {
        //ImGui::InputInt( m_name.c_str(), &m_value, m_editStep, m_editStep * 10 );
        //m_value = vaMath::Clamp( m_value, m_minVal, m_maxVal );
        ImGui::LabelText( "%s (int64 editing not supported)", "%lld", m_value );
    }
    else
    {
        ImGui::LabelText( "%s", "%lld", m_value );
    }
#endif
}

void vaPropertyContainer::PropertyItemFloat::ImGuiEdit( int numDecimals )
{
    if( !m_isUIVisible ) return;
    numDecimals;
#ifdef VA_IMGUI_INTEGRATION_ENABLED

    if( m_isUIEditable )
    {
        float tmpValue = (float)m_value;
        string fmt = vaStringTools::Format("%%.%df", numDecimals);
        if( ImGui::InputFloat( m_name.c_str(), &tmpValue, m_editStep, m_editStep * 10, fmt.c_str(), ImGuiInputTextFlags_EnterReturnsTrue ) )
            m_value = vaMath::Clamp( tmpValue, m_minVal, m_maxVal );
    }
    else
    {
        ImGui::LabelText( "%s", "%.4f", m_value );
    }
#endif
}

void vaPropertyContainer::PropertyItemDouble::ImGuiEdit( int numDecimals )
{
    if( !m_isUIVisible ) return;
    numDecimals;
#ifdef VA_IMGUI_INTEGRATION_ENABLED
    if( m_isUIEditable )
    {
        float tmpValue = (float)m_value;
        string fmt = vaStringTools::Format("%%.%df", numDecimals);
        if( ImGui::InputFloat( m_name.c_str( ), &tmpValue, (float)m_editStep, (float)m_editStep * 10, fmt.c_str() ), ImGuiInputTextFlags_EnterReturnsTrue )
        {
            m_value = vaMath::Clamp( (double)tmpValue, m_minVal, m_maxVal );
        }
    }
    else
    {
        float tmpValue = (float)m_value;
        ImGui::LabelText( "%s", "%.4f", tmpValue );
    }
#endif
}

void vaPropertyContainer::PropertyItemString::ImGuiEdit( int numDecimals )
{
    numDecimals; // unreferenced

    if( !m_isUIVisible ) return;

#ifdef VA_IMGUI_INTEGRATION_ENABLED

    if( m_isUIEditable )
    {
        assert( false ); // never tested
        const int editBufferSize = 2048;
        if( m_value.size() < editBufferSize )
        {
            char buffer[editBufferSize];
            memcpy( buffer, m_value.c_str(), m_value.size() );
            if( ImGui::InputText( m_name.c_str(), buffer, sizeof(buffer) ) )
                m_value = buffer;
        }
    }
    else
    {
        ImGui::LabelText( "%s", "%s", m_value.c_str() );
    }
#endif
}

bool vaPropertyContainer::Serialize( vaXMLSerializer & serializer )
{
    assert( serializer.GetVersion() > 0 );
    //if( !serializer.SerializeOpenChildElement( m_name.c_str() ) )
    //    return false;

    for( auto & p : m_properties )
    {
        //if( serializer.Serialize( p->Name().c_str(), *p ) )
        if( !p->NamedSerialize( serializer ) )
        {
            assert( false );
            return false;
        }
    }

    //if( !serializer.SerializePopToParentElement( m_name.c_str( ) ) )
    //    { assert( false ); return false; };

    return true;
}

void vaPropertyContainer::UIPropertiesItemTick( vaApplicationBase &, bool, bool )
{
    for( auto & p : m_properties )
    {
        p->ImGuiEdit( m_numDecimals );
    }
}