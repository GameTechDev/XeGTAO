#include "vaImguiIntegration.h"

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "IntegratedExternals\imgui\imgui_internal.h"

using namespace Vanilla;

#ifdef VA_IMGUI_INTEGRATION_ENABLED

bool Vanilla::ImguiEx_VectorOfStringGetter( void * strVectorPtr, int n, const char** out_text )
{
    *out_text = (*(const std::vector<std::string> *)strVectorPtr)[n].c_str();
    return true;
}

namespace Vanilla
{
    static char     c_popup_InputString_Value[128] = { 0 };
    static bool     c_popup_InputString_JustOpened = false;

    bool                ImGuiEx_PopupInputStringBegin( const string & label, const string & initialValue )
    {
        if( c_popup_InputString_JustOpened )
        {
            assert( false );
            return false;
        }
        strncpy_s( c_popup_InputString_Value, initialValue.c_str(), sizeof( c_popup_InputString_Value ) - 1 );

        ImGui::OpenPopup( label.c_str() );
        c_popup_InputString_JustOpened = true;
        return true;
    }

    bool                ImGuiEx_PopupInputStringTick( const string & label, string & outValue )
    {
        ImGui::SetNextWindowContentSize(ImVec2(300.0f, 0.0f));
        if( ImGui::BeginPopupModal( label.c_str() ) )
        {
            if( c_popup_InputString_JustOpened )
            {
                ImGui::SetKeyboardFocusHere();
                c_popup_InputString_JustOpened = false;
            }

            bool enterClicked = ImGui::InputText( "New name", c_popup_InputString_Value, sizeof( c_popup_InputString_Value ), ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue );
            string newName = c_popup_InputString_Value;
        
            if( enterClicked || ImGui::Button( "Accept" ) )
            {
                if( newName != "" )
                {
                    outValue = newName;
                    ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                    return true;
                }
            }
            ImGui::SameLine();
            if( ImGui::Button( "Cancel" ) )
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
        return false;
    }

    ImVec2 ImGuiEx_CalcSmallButtonSize( const char * label )
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 label_size = ImGui::CalcTextSize(label, nullptr, true);

        return ImGui::CalcItemSize( ImVec2(0, 0), label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f * 0.0f );
    }

    bool ImGuiEx_SmallButtonEx(const char* label, bool disabled)
    {
        ImGuiContext& g = *GImGui;
        float backup_padding_y = g.Style.FramePadding.y;
        g.Style.FramePadding.y = 0.0f;
        ImGuiButtonFlags flags = ImGuiButtonFlags_AlignTextBaseLine | ImGuiButtonFlags_PressedOnClick;
        if( disabled )
        {
            flags |= ImGuiButtonFlags_Disabled;
             ImGui::PushStyleColor( ImGuiCol_Text,           ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) );
        }
        bool pressed = ImGui::ButtonEx(label, ImVec2(0, 0), flags);
        g.Style.FramePadding.y = backup_padding_y;
        if( disabled )
            ImGui::PopStyleColor( 1 );
        return pressed;
    }

    int ImGuiEx_SameLineSmallButtons( const char * keyID, const std::vector<string> & labels, const std::vector<bool> & disabled, bool verticalSeparator, const std::vector<string> & toolTips, float * totalWidth )
    {
        assert( labels.size() > 0 );
        assert( disabled.size( ) == 0 || disabled.size( ) == labels.size( ) );
        if( labels.size() == 0 )
            return -1;

        VA_GENERIC_RAII_SCOPE( ImGui::PushID( keyID );, ImGui::PopID( ); )

        float xSizeTotal = 0.5f * ImGui::GetStyle().ItemSpacing.x * (labels.size()-1);         // this measures additional spacing between items
        xSizeTotal += ((verticalSeparator)?(ImGui::GetStyle().ItemSpacing.x+1):(0));    // this measures spacing of vertical separator (if any)
        xSizeTotal -= ImGui::GetStyle().ItemSpacing.x;                                  // this pulls everything slightly to the right because reasons

        std::vector<float> xSizes(labels.size());

        for( size_t i = 0; i < labels.size(); i++ )
            xSizeTotal += xSizes[i] = ImGuiEx_CalcSmallButtonSize( labels[i].c_str() ).x;

        if( totalWidth )
            *totalWidth = xSizeTotal;

        ImGui::SameLine( std::max( 0.0f, ImGui::GetContentRegionAvail().x - xSizeTotal ) );

        if( verticalSeparator )
        {
            ImGuiEx_VerticalSeparator( ); ImGui::SameLine( );
        }

        int retVal = -1;
        for( size_t i = 0; i < labels.size( ); i++ )
        {
            if( ImGuiEx_SmallButtonEx( labels[i].c_str(), (disabled.size()==labels.size())?(disabled[i]):(false) ) )
                retVal = (int)i;
            if( toolTips.size()>i && ImGui::IsItemHovered( ) )
                ImGui::SetTooltip( toolTips[i].c_str() );
            if( (i+1) < labels.size() )
                ImGui::SameLine( 0, 0.5f * ImGui::GetStyle().ItemSpacing.x );
        }
        return retVal;
    }

    // bool ImGuiEx_SameLineMenuButton( const char * keyID )
    // {
    //     ImGuiButtonFlags flags = ImGuiButtonFlags_None;
    // 
    //     ImGuiWindow* window = ImGui::GetCurrentWindow( );
    //     if( window->SkipItems )
    //         return false;
    // 
    //     float sz = ImGui::GetFrameHeight();
    //     ImVec2 size(sz, sz);
    // 
    //     // align to right
    //     ImGui::SameLine( std::max( 0.0f, ImGui::GetContentRegionAvail( ).x - sz ) );
    // 
    //     ImGuiContext & g = *GImGui;
    //     const ImGuiID id = window->GetID( keyID );
    //     const ImRect bb( window->DC.CursorPos, window->DC.CursorPos + size );
    // 
    //     const float default_size = ImGui::GetFrameHeight( );
    //     ImGui::ItemSize( bb, ( size.y >= default_size ) ? g.Style.FramePadding.y : 0.0f );
    //     if( !ImGui::ItemAdd( bb, id ) )
    //         return false;
    // 
    //     if( window->DC.ItemFlags & ImGuiItemFlags_ButtonRepeat )
    //         flags |= ImGuiButtonFlags_Repeat;
    // 
    //     bool hovered, held;
    //     bool pressed = ImGui::ButtonBehavior( bb, id, &hovered, &held, flags );
    // 
    //     // Render
    //     const ImU32 col = ImGui::GetColorU32( ( held && hovered ) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button );
    //     ImGui::RenderNavHighlight( bb, id );
    //     ImGui::RenderFrame( bb.Min, bb.Max, col, true, g.Style.FrameRounding );
    //     ImGui::RenderArrow( bb.Min + ImVec2( ImMax( 0.0f, ( size.x - g.FontSize ) * 0.5f ), ImMax( 0.0f, ( size.y - g.FontSize ) * 0.5f ) ), ImGuiDir_Left );
    // 
    //     return pressed;
    // }

//    // Button to close a window
//    bool ImGuiEx_MenuButton( ImGuiID id, const ImVec2 & pos, float radius )
//    {
//        ImGuiContext& g = *GImGui;
//        ImGuiWindow* window = g.CurrentWindow;
//
//        // We intentionally allow interaction when clipped so that a mechanical Alt,Right,Validate sequence close a window.
//        // (this isn't the regular behavior of buttons, but it doesn't affect the user much because navigation tends to keep items visible).
//        const ImRect bb( pos - ImVec2( radius, radius ), pos + ImVec2( radius, radius ) );
//        bool is_clipped = !ImGui::ItemAdd( bb, id );
//
//        bool hovered, held;
//        bool pressed = ImGui::ButtonBehavior( bb, id, &hovered, &held );
//        if( is_clipped )
//            return pressed;
//
//        // Render
//        ImVec2 center = bb.GetCenter( );
//        if( hovered )
//            window->DrawList->AddCircleFilled( center, ImMax( 2.0f, radius ), ImGui::GetColorU32( held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered ), 9 );
//
//        float cross_extent = ( radius * 0.7071f ) - 1.0f;
//        ImU32 cross_col = ImGui::GetColorU32( ImGuiCol_Text );
//        center -= ImVec2( 0.5f, 0.5f );
//        window->DrawList->AddLine( center + ImVec2( +cross_extent, +cross_extent ), center + ImVec2( -cross_extent, -cross_extent ), cross_col, 1.0f );
//        window->DrawList->AddLine( center + ImVec2( +cross_extent, -cross_extent ), center + ImVec2( -cross_extent, +cross_extent ), cross_col, 1.0f );
//
//        return pressed;
//    }

    bool ImGuiEx_Transform( const char * keyID, vaMatrix4x4 & transform, bool horizontal, bool readOnly )
    {
        ImGui::PushID( keyID );

        ImGuiInputTextFlags sharedFlags = ImGuiInputTextFlags_EnterReturnsTrue | ((readOnly)?(ImGuiInputTextFlags_ReadOnly):(ImGuiInputTextFlags_None));

        bool hadChanges = false;
        vaVector3 pos, ypr, scale; vaMatrix3x3 rot;
        transform.Decompose( scale, rot, pos );
        rot.DecomposeRotationYawPitchRoll( ypr.z, ypr.y, ypr.x ); ypr = vaVector3::RadianToDegree( ypr );

        if( ImGui::InputFloat3( "Position", &pos.x, "%.3f", sharedFlags ) )
            hadChanges = true;

        if( horizontal )
        {
            ImGui::SameLine( ); ImGuiEx_VerticalSeparator( ); ImGui::SameLine( ); //windowSize.x / 4.0f * 1.0f ); 
        }

        if( ImGui::InputFloat3( "Rotation", &ypr.x, "%.3f", sharedFlags ) )
        {
            ypr = vaVector3::DegreeToRadian( ypr ); rot = vaMatrix3x3::FromYawPitchRoll( ypr.z, ypr.y, ypr.x ); hadChanges = true;
        }

        if( horizontal )
        {
            ImGui::SameLine( ); ImGuiEx_VerticalSeparator( ); ImGui::SameLine( ); //windowSize.x / 4.0f * 2.0f );
        }

        if( ImGui::InputFloat3( "Scale", &scale.x, "%.3f", sharedFlags ) )
            hadChanges = true;

        //ImGui::SameLine( ); //windowSize.x / 4.0f * 3.0f );

        if( hadChanges )
            transform = vaMatrix4x4::FromScaleRotationTranslation( scale, rot, pos );

        ImGui::PopID( );

        return hadChanges;
    }

    void ImGuiEx_VerticalSeparator( )
    {
        ImGui::SeparatorEx( ImGuiSeparatorFlags_Vertical );
    }

    bool ImGuiEx_Button( const char * label, const ImVec2 & size_arg , bool disabled )
    {
        ImGuiButtonFlags flags = 0;
        if( disabled )
        {
            flags |= ImGuiButtonFlags_Disabled;
            ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
        }
        bool pressed = ImGui::ButtonEx( label, size_arg, flags );
        if( disabled )
            ImGui::PopStyleColor( 1 );
        return pressed;
    }

}

#else
#endif

#ifdef VA_IMGUI_INTEGRATION_ENABLED

namespace Vanilla
{

    static ImFont *     s_bigClearSansRegular   = nullptr;
    static ImFont *     s_bigClearSansBold      = nullptr;

    ImFont *            ImGetBigClearSansRegular( )                 { return s_bigClearSansRegular; }
    ImFont *            ImGetBigClearSansBold( )                    { return s_bigClearSansBold;    }

    void                ImSetBigClearSansRegular( ImFont * font )   { s_bigClearSansRegular = font; }
    void                ImSetBigClearSansBold(    ImFont * font )   { s_bigClearSansBold    = font; }

}

#endif