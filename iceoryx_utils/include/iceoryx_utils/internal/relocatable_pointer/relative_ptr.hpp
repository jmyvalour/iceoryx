// Copyright (c) 2019 by Robert Bosch GmbH. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef IOX_UTILS_RELOCATABLE_POINTER_RELATIVE_PTR_HPP
#define IOX_UTILS_RELOCATABLE_POINTER_RELATIVE_PTR_HPP

#include "base_relative_ptr.hpp"

#include <cstdint>
#include <iostream>
#include <limits>

namespace iox
{
namespace rp
{
/// @brief typed version so we can use operator->
template <typename T>
class RelativePointer : public BaseRelativePointer
{
  public:
    /// @brief constructs a RelativePointer pointing to ptr in a segment identified by id
    /// @param[in] ptr is the pointee
    /// @param[in] id is the unique id of the segment
    RelativePointer(ptr_t ptr, id_t id) noexcept;

    /// @brief constructs a RelativePointer from a given offset and segment id
    /// @param[in] offset is the offset
    /// @param[in] id is the unique id of the segment
    RelativePointer(offset_t offset, id_t id) noexcept;

    /// @brief constructs a RelativePointer pointing to ptr
    /// @param[in] ptr is the pointee
    RelativePointer(ptr_t ptr = nullptr) noexcept;

    /// @brief creates a RelativePointer from a BaseRelativePointer
    /// @param[in] other is the BaseRelativePointer
    RelativePointer(const BaseRelativePointer& other) noexcept;

    /// @brief assign this to point to the BaseRelativePointer other
    /// @param[in] other is the pointee
    /// @return reference to self
    RelativePointer& operator=(const BaseRelativePointer& other) noexcept;

    /// @brief assigns the RelativePointer to point to ptr
    /// @param[in] ptr is the pointee
    /// @return reference to self
    RelativePointer& operator=(ptr_t ptr) noexcept;

    /// @brief dereferencing operator which returns a reference to the underlying object
    /// @tparam U a template parameter to enable the dereferencing operator only for non-void T
    /// @return a reference to the underlying object
    template <typename U = T>
    typename std::enable_if<!std::is_void<U>::value, U&>::type operator*() noexcept;

    /// @brief access to the underlying object
    /// @return a pointer to the underlying object
    T* operator->() noexcept;

    /// @brief dereferencing operator which returns a const reference to the underlying object
    /// @tparam U a template parameter to enable the dereferencing operator only for non-void T
    /// @return a const reference to the underlying object
    template <typename U = T>
    typename std::enable_if<!std::is_void<U>::value, const U&>::type operator*() const noexcept;

    /// @brief read-only access to the underlying object
    /// @return a const pointer to the underlying object
    T* operator->() const noexcept;

    /// @brief access the underlying object
    /// @return a pointer to the underlying object
    T* get() const noexcept;

    /// @brief converts the RelativePointer to a pointer of the type of the underlying object
    /// @return a pointer of type T pointing to the underlying object
    operator T*() const noexcept;

    /// @brief checks if the raw pointer is equal to ptr
    /// @param[in] ptr is the pointer to be compared with the raw pointer
    /// @return true if ptr is equal to the raw pointer, otherwise false
    bool operator==(T* const ptr) const noexcept;

    /// @brief checks if the raw pointer is not equal to ptr
    /// @param[in] ptr is the pointer to be compared with the raw pointer
    /// @return true if ptr is not equal to the raw pointer, otherwise false
    bool operator!=(T* const ptr) const noexcept;
};

} // namespace rp
} // namespace iox

#include "iceoryx_utils/internal/relocatable_pointer/relative_ptr.inl"

#endif // IOX_UTILS_RELOCATABLE_POINTER_RELATIVE_PTR_HPP
