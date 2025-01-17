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

#include <exception>
#include <iostream>
#include <limits>

#include <lagrange/Logger.h>
#include <lagrange/utils/warning.h>

namespace lagrange {

/**
 * Perform safe cast from `SourceType` to `TargetType`, where "safe" means:
 *   * Type compatibility.
 *   * Over/under flow for numerical types.
 *   * Sign change caused by casting.
 *   * Large numerical error for floating point casts.
 *
 * Example usage:
 *
 *   int x = lagrange::safe_cast<int>(-1.0);    // good.
 *   int x = lagrange::safe_cast<int>("-1.0");  // fail because incompatible types.
 *   size_t x = lagrange::safe_cast<size_t>(-1.0); // fail because sign change.
 *   int x = lagrange::safe_cast<int>(-1.5);    // fail because truncation error.
 */
template <typename TargetType, typename SourceType>
constexpr std::enable_if_t<!std::is_same<SourceType, TargetType>::value, TargetType> safe_cast(
    SourceType value)
{
    TargetType value_2 = static_cast<TargetType>(value);
    SourceType value_3 = static_cast<SourceType>(value_2);

    if ((value_2 >= 0) != (value >= 0)) {
        // Sign changed.  Not good.
        logger().error("Casting failed: from {} to {} causes a sign change...", value, value_2);
        throw std::bad_cast();
    } else if (value_3 == value) {
        // Lossless cast. :D
        return value_2;
    } else {
        // Lossy cast... Check for casting error.
        constexpr SourceType EPS =
            static_cast<SourceType>(std::numeric_limits<TargetType>::epsilon());

        // generates warning C4146: "unary minus operator applied to unsigned type, result still
        // unsigned" this cannot happen, as we check for unsigned types above.
        LA_DISABLE_WARNING_BEGIN
        LA_DISABLE_WARNING_MSVC(4146)
        const SourceType value_abs = value_3 > 0 ? value_3 : -value_3;
        LA_DISABLE_WARNING_END

        const SourceType scaled_eps = value_abs >= (SourceType)1 ? EPS * value_abs : EPS;
        if (value_3 > value && value_3 < value + scaled_eps)
            return value_2;
        else if (value_3 < value && value_3 + scaled_eps > value)
            return value_2;
        else {
            // Cast is likely not valid...
            logger().error(
                "Casting failed: from {} to {} will incur error ({}) larger than {}",
                value,
                value_2,
                value - value_2,
                scaled_eps);
            throw std::bad_cast();
        }
    }
} // safe_cast()

/**
 * Specialization for TargetType == SourceType
 */
template <typename T>
constexpr T safe_cast(T value)
{
    return value;
} // safe_cast()

/**
 * Specialization for bool
 */
template <typename TargetType>
constexpr TargetType safe_cast(bool value)
{
    // do we really want to allow this?
    return static_cast<TargetType>(value);
} // safe_cast()

/**
 * Casting an enum to scalar and vice versa
 * These are only to be used for assigning enums as (mesh) attributes,
 * or to be used for reading back enums that were saved as mesh
 * attributes.
 */
template <typename T, typename U>
constexpr T safe_cast_enum(const U u)
{
    static_assert(
        std::is_enum<T>::value || std::is_enum<U>::value,
        "At least one of the types should be an enum");
    static_assert(
        (std::is_enum<T>::value && std::is_enum<U>::value) == std::is_same<T, U>::value,
        "Casting one enum to another is prohibitied");

    const auto tu = static_cast<T>(static_cast<std::int64_t>(u));
    const auto utu = static_cast<U>(static_cast<std::int64_t>(tu));
    const auto tutu = static_cast<T>(static_cast<std::int64_t>(utu));
    if (u == utu && tutu == tu) {
        return tu;
    } else {
        throw std::bad_cast();
        return T();
    }
} // safe_cast_enum()

} // namespace lagrange
