/*
 * Copyright 2020 Adobe. All rights reserved.
 * This file is licensed to you under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License. You may obtain a copy
 * of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under
 * the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
 * OF ANY KIND, either express or implied. See the License for the specific language
 * governing permissions and limitations under the License.
 */
#pragma once

#include <lagrange/ui/Entity.h>
#include <lagrange/ui/utils/pair_hash.h>

namespace lagrange {
namespace ui {

class Tools;



template <typename T>
entt::id_type register_tool_type(
    const std::string& display_name,
    const std::string& icon,
    const std::string& keybind)
{
    using namespace entt::literals;

    entt::meta<T>().type();
    entt::meta<T>()
        .prop("display_name"_hs, display_name)
        .prop("icon"_hs, icon)
        .prop("keybind"_hs, keybind);

    return entt::type_id<T>().hash();
}


/*
    Container for Tool systems
*/
class Tools
{
public:
    template <typename ToolType, typename ElementType>
    void register_tool(const System& tool_system)
    {
        const auto k = key<ToolType, ElementType>();
        m_tool_systems[k] = tool_system;

        if (std::find(m_tool_types.begin(), m_tool_types.end(), k.first) == m_tool_types.end()) {
            m_tool_types.push_back(k.first);
        }

        if (std::find(m_element_types.begin(), m_element_types.end(), k.second) ==
            m_element_types.end()) {
            m_element_types.push_back(k.second);
        }
    }

    template <typename ToolType, typename ElementType>
    void run(Registry& registry)
    {
        m_tool_systems[key<ToolType, ElementType>()](registry);
    }

    void run(entt::id_type tool_type, entt::id_type element_type, Registry& registry)
    {
        m_tool_systems[KeyType{tool_type, element_type}](registry);
    }

    //? store in context maybe?
    bool run_current(Registry& registry) const
    {
        auto it = m_tool_systems.find(m_current_key);
        if (m_tool_systems.find(m_current_key) == m_tool_systems.end()) return false;
        it->second(registry);
        return true;
    }


    const std::vector<entt::id_type>& get_element_types() const { return m_element_types; }

    const std::vector<entt::id_type>& get_tool_types() const { return m_tool_types; }

    entt::id_type current_tool_type() const { return m_current_key.first; }
    entt::id_type current_element_type() const { return m_current_key.second; }

    


    void set_current_element_type(entt::id_type element_type)
    {
        m_current_key = KeyType{m_current_key.first, element_type};
    }

    void set_current_tool_type(entt::id_type tool_type)
    {
        m_current_key = KeyType{tool_type, m_current_key.second};
    }

    template <typename T>
    void set_current_element_type()
    {
        set_current_element_type(entt::type_id<T>().hash());
    }

    template <typename T>
    void set_current_tool_type()
    {
        set_current_tool_type(entt::type_id<T>().hash());
    }

private:
    using KeyType = std::pair<entt::id_type, entt::id_type>;

    template <typename ToolType, typename ElementType>
    static constexpr KeyType key()
    {
        return {entt::type_id<ToolType>().hash(), entt::type_id<ElementType>().hash()};
    }
    std::unordered_map<KeyType, System, utils::pair_hash> m_tool_systems;
    std::vector<entt::id_type> m_tool_types;
    std::vector<entt::id_type> m_element_types;

    KeyType m_current_key;
};


} // namespace ui
} // namespace lagrange