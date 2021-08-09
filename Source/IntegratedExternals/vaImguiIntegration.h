#pragma once



#include "Core\vaCore.h"
#include "Core\vaStringTools.h"
#include "Core\vaGeometry.h"

#ifdef VA_IMGUI_INTEGRATION_ENABLED
#include "IntegratedExternals\imgui\imgui.h"
#include "IntegratedExternals\imgui\misc\cpp\imgui_stdlib.h"
#include "IntegratedExternals\imgui\ImGuizmo\ImGuizmo.h"
#endif

namespace Vanilla
{

#ifdef VA_IMGUI_INTEGRATION_ENABLED
    inline ImVec4       ImFromVA( const vaVector4 &  v )            { return ImVec4( v.x, v.y, v.z, v.w ); }
    inline ImVec4       ImFromVA( const vaVector3 &  v )            { return ImVec4( v.x, v.y, v.z, 0.0f ); }
    inline ImVec2       ImFromVA( const vaVector2 &  v )            { return ImVec2( v.x, v.y ); }
    inline vaVector4    VAFromIm( const ImVec4 &  v )               { return vaVector4( v.x, v.y, v.z, v.w ); }

    bool                ImguiEx_VectorOfStringGetter( void * strVectorPtr, int n, const char** out_text );

    inline bool         ImGuiEx_ListBox( const char * label, int & itemIndex, const std::vector<std::string> & elements, int heightInItems = -1, bool fullWidthItems = false )
    {
        if( fullWidthItems )
            ImGui::PushItemWidth(-1);
        auto ret = ImGui::ListBox( label, &itemIndex, ImguiEx_VectorOfStringGetter, (void*)&elements, (int)elements.size(), heightInItems );
        if( fullWidthItems )
            ImGui::PopItemWidth();
        return ret;
    }

    inline bool         ImGuiEx_Combo( const char * label, int & itemIndex, const std::vector<std::string> & elements )
    {
        char buffer[4096]; buffer[0] = NULL;
        int currentLoc = 0;
        for( auto & element : elements ) 
        {
            if( (currentLoc + element.size() + 2) >= sizeof(buffer) )
                break;
            memcpy( &buffer[currentLoc], element.c_str(), element.size() );
            currentLoc += (int)element.size();
            buffer[currentLoc] = 0;
            currentLoc++;
        }
        buffer[currentLoc] = 0;
        itemIndex = vaMath::Clamp( itemIndex, 0, (int)elements.size() );
        return ImGui::Combo( label, &itemIndex, buffer );
    }

    bool ImGuiEx_Transform( const char * keyID, vaMatrix4x4& transform, bool horizontal, bool readOnly );

    void ImGuiEx_VerticalSeparator( );

    bool ImGuiEx_Button( const char * label, const ImVec2 & size_arg = ImVec2(0,0), bool disabled = false );

    // Two big TTF fonts for big.. things? Created in vaApplicationBase
    ImFont *                                                    ImGetBigClearSansRegular( );
    ImFont *                                                    ImGetBigClearSansBold( );

    bool                ImGuiEx_PopupInputStringBegin( const string & label, const string & initialValue );
    bool                ImGuiEx_PopupInputStringTick( const string & label, string & outValue );

    ImVec2              ImGuiEx_CalcSmallButtonSize( const char * label );
    //bool                ImGuiEx_SmallButtonEx(const char* label, bool disabled);

    // returns -1 if no button pressed, otherwise returns index ('labels') of the button pressed
    int                 ImGuiEx_SameLineSmallButtons( const char * keyID, const std::vector<string> & labels, const std::vector<bool> & disabled = {}, bool verticalSeparator = false, const std::vector<string> & toolTips = {}, float * totalWidth = nullptr );

    //bool                ImGuiEx_SameLineMenuButton( const char* keyID ); //, ImGuiButtonFlags flags = ImGuiButtonFlags_None );

#endif
}

