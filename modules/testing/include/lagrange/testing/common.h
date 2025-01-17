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

// An alternative to injecting asset locations as command line macros,
// is using a generated header.
#ifdef LA_TESTING_USE_CONFIG
#include <lagrange/testing/config.h>
#endif

#include <lagrange/fs/filesystem.h>
#include <lagrange/io/load_mesh.h>

#include <catch2/catch.hpp>

namespace lagrange {
namespace testing {

///
/// Gets the absolute path to a file in the test data directory. This function asserts that the file
/// exists.
///
/// @param[in]  relative_path  Relative path to the file.
///
/// @return     Absolute path to the file.
///
fs::path get_data_path(const fs::path &relative_path);

///
/// Loads a mesh from the test data directory.
///
/// @param[in]  relative_path  Relative path of the file to load.
///
/// @tparam     MeshType       Mesh type.
///
/// @return     A unique_ptr to the newly allocated mesh.
///
template <typename MeshType>
std::unique_ptr<MeshType> load_mesh(const fs::path& relative_path)
{
    auto result = lagrange::io::load_mesh<MeshType>(get_data_path(relative_path));
    REQUIRE(result);
    return result;
}
extern template std::unique_ptr<TriangleMesh3D> load_mesh(const fs::path&);
extern template std::unique_ptr<QuadMesh3D> load_mesh(const fs::path&);

///
/// Set up MKL Conditional Numerical Reproducibility to ensure maximum compatibility between
/// devices. This is only called before setting up unit tests that depend on reproducible numerical
/// results. Otherwise, the behavior can be controlled by the environment variable MKL_CBWR. See [1]
/// for additional information. This function has no effect if Lagrange is compiled without MKL.
///
/// [1]:
/// https://software.intel.com/content/www/us/en/develop/articles/introduction-to-the-conditional-numerical-reproducibility-cnr.html
///
void setup_mkl_reproducibility();

} // namespace testing
} // namespace lagrange
