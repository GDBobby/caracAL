#pragma once

#include <meta>
#include <ranges>
#include <utility>


#if EWE_IMGUI
#include "imgui.h"
#endif

namespace Reflect {
    template<typename T>
    concept IsEnum = std::is_enum_v<T>;

namespace Enum{

    template <IsEnum E>
    constexpr std::string_view ToString(E value) {
        template for (constexpr auto e : std::define_static_array(std::meta::enumerators_of(^^E))) {
            if (value == [:e:]) {
                return std::meta::identifier_of(e);
            }
        }

        return "<unnamed>";
    }


    template<IsEnum E>
    struct EnumData {
        using UnderlyingType = std::underlying_type_t<E>;
        std::string_view name;
        E value;

        constexpr operator E() const{
            return value;
        }

        constexpr UnderlyingType GetUnderlyingValue() const {
            return static_cast<UnderlyingType>(value);
        }
    };

    template<IsEnum E>
    consteval bool LastMemberIsHelper(){
        constexpr auto back_mem = std::meta::enumerators_of(^^E).back();
        constexpr auto back_name = std::meta::identifier_of(back_mem);
        
        //_MAX_ENUM is a vulkan enum ender, typically equal to INT_MAX
        constexpr bool contains_max = back_name.find("_MAX_ENUM") != std::string_view::npos;
        //COUNT is my own (EWE) enum ender
        constexpr bool is_count = (back_name == "COUNT");

        return contains_max || is_count;
    }

    template<IsEnum E>
    consteval auto BuildData() {
        constexpr auto members = std::define_static_array(std::meta::enumerators_of(^^E));
        constexpr auto member_count = std::meta::enumerators_of(^^E).size() - LastMemberIsHelper<E>();
        std::array<EnumData<E>, member_count> result;
        
        template for (constexpr auto i : std::views::iota(0u, member_count)) {
            result[i] = {
                .name = std::meta::identifier_of(members[i]),
                .value = std::meta::extract<E>(members[i])
            };
        }

        return result;
    }

    template<IsEnum E>
    //std::array<EnumData<E>
    constexpr auto enum_data = BuildData<E>();

    template<IsEnum E>
    std::size_t Members_Index(E val){
        for(std::size_t i = 0; i < enum_data<E>.size(); i++){
            if(enum_data<E>[i].value == val){
                return i;
            }
        }
        std::unreachable();
    }

#if EWE_IMGUI

    template <IsEnum E>
    void ImguiEnum_ForEach_Selectable(E& value){
        int index = Members_Index(value);
        for(std::size_t i = 0; i < enum_data<E>.size(); i++){
            const bool selected = index == i;
            if(ImGui::Selectable(enum_data<E>[i].name.data(), selected)){
                value = enum_data<E>[i].value;
            }
            if(selected){
                ImGui::SetItemDefaultFocus();
            }
        }
    }

    template <IsEnum E>
    void Imgui_Combo_Selectable(std::string_view name, E& value) {

        int index = Members_Index(value);
        const char* preview = enum_data<E>[index].name.data();
        if(ImGui::BeginCombo(name.data(), preview)) {
            ImguiEnum_ForEach_Selectable(value);
            ImGui::EndCombo();
        }
    }

    //this requires that each enum value is a bit flag
    template<IsEnum E>
    void Imgui_ForEach_Check(std::underlying_type_t<E>& value){
        using U_T = std::underlying_type_t<E>;
        U_T u_t = static_cast<U_T>(value);

        for(std::size_t i = 0; i < enum_data<E>.size(); i++){
            bool temp_bool = (value & enum_data<E>[i].value) == enum_data<E>[i].value;
            if(ImGui::Checkbox(enum_data<E>[i].name.data(), &temp_bool)){
                if(temp_bool){
                    u_t |= enum_data<E>[i].value;
                }
                else{
                    u_t &= ~static_cast<U_T>(enum_data<E>[i].value); 
                }
            }
        }
        
        value = static_cast<E>(u_t);
    }
#endif
} //namespace Enum
} //namespace Reflect