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
#include <lagrange/ui/components/Selection.h>
#include <lagrange/ui/components/SelectionContext.h>

namespace lagrange {
namespace ui {

class Keybinds;


bool deselect_all(Registry& registry);
bool dehover_all(Registry& registry);


inline bool is_selected(const Registry& registry, Entity e)
{
    return registry.has<Selected>(e);
}

inline bool is_hovered(const Registry& registry, Entity e)
{
    return registry.has<Hovered>(e);
}

inline decltype(auto) selected_view(Registry& registry)
{
    return registry.view<Selected>();
}

inline decltype(auto) hovered_view(Registry& registry)
{
    return registry.view<Hovered>();
}


std::vector<Entity> collect_selected(const Registry& registry);
std::vector<Entity> collect_hovered(const Registry& registry);


bool set_selected(
    Registry& registry,
    Entity e,
    SelectionBehavior behavior = SelectionBehavior::SET);

bool set_hovered(Registry& registry, Entity e, SelectionBehavior behavior = SelectionBehavior::SET);

bool select(Registry& registry, Entity e);

bool deselect(Registry& registry, Entity e);

bool hover(Registry& registry, Entity e);

bool dehover(Registry& registry, Entity e);


SelectionContext& get_selection_context(Registry& r);

const SelectionContext& get_selection_context(const Registry& r);

/* Utils*/

SelectionBehavior selection_behavior(const Keybinds& keybinds);
bool are_selection_keys_down(const Keybinds& keybinds);
bool are_selection_keys_pressed(const Keybinds& keybinds);
bool are_selection_keys_released(const Keybinds& keybinds);


SelectionBehavior selection_behavior(const Registry& r);
bool are_selection_keys_down(const Registry& r);
bool are_selection_keys_pressed(const Registry& r);
bool are_selection_keys_released(const Registry& r);


} // namespace ui
} // namespace lagrange
