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

#include <lagrange/fs/filesystem.h>
#include <lagrange/ui/Entity.h>
#include <lagrange/ui/components/IBL.h>

namespace lagrange {
namespace ui {

/// Utility functions for Image Based Lights (IBLs)

/// @brief  Generates Image Based Light from image file or .ibl file given by path.
///         Throws std::runtime_error on failure.
/// @param path
/// @param resolution
/// @return IBL
IBL generate_ibl(const fs::path& path, size_t resolution = 1024);

/// @brief  Generates Image Based Light from given rectangular texture.
///         Throws std::runtime_error on failure.
/// @param path
/// @param resolution
/// @return IBL
IBL generate_ibl(const std::shared_ptr<Texture>& background_texture, size_t resolution = 1024);

/// @brief Returns first <IBL> entity found in registry. If there are none, returns invalid Entity
/// @param registry
/// @return Entity
Entity get_ibl_entity(const Registry& registry);

/// @brief Returns pointer to the first IBL found in the registry. Nullptr if there is none
/// @param registry
/// @return IBL pointer
const IBL* get_ibl(const Registry& registry);

/// @copydoc
IBL* get_ibl(Registry& registry);

/// @brief Adds IBL to the scene
Entity add_ibl(Registry& registry, IBL ibl);

/// @brief Removes all ibls
void clear_ibl(Registry& registry);

} // namespace ui
} // namespace lagrange