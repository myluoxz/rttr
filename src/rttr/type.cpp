/************************************************************************************
*                                                                                   *
*   Copyright (c) 2014, 2015 - 2016 Axel Menzel <info@rttr.org>                     *
*                                                                                   *
*   This file is part of RTTR (Run Time Type Reflection)                            *
*   License: MIT License                                                            *
*                                                                                   *
*   Permission is hereby granted, free of charge, to any person obtaining           *
*   a copy of this software and associated documentation files (the "Software"),    *
*   to deal in the Software without restriction, including without limitation       *
*   the rights to use, copy, modify, merge, publish, distribute, sublicense,        *
*   and/or sell copies of the Software, and to permit persons to whom the           *
*   Software is furnished to do so, subject to the following conditions:            *
*                                                                                   *
*   The above copyright notice and this permission notice shall be included in      *
*   all copies or substantial portions of the Software.                             *
*                                                                                   *
*   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR      *
*   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,        *
*   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE     *
*   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER          *
*   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,   *
*   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE   *
*   SOFTWARE.                                                                       *
*                                                                                   *
*************************************************************************************/

#include "rttr/type.h"

#include "rttr/constructor.h"
#include "rttr/property.h"
#include "rttr/destructor.h"
#include "rttr/enumeration.h"
#include "rttr/method.h"

#include "rttr/detail/constructor/constructor_wrapper_base.h"
#include "rttr/detail/destructor/destructor_wrapper_base.h"
#include "rttr/detail/enumeration/enumeration_wrapper_base.h"
#include "rttr/detail/method/method_wrapper_base.h"
#include "rttr/detail/property/property_wrapper.h"
#include "rttr/rttr_enable.h"

#include "rttr/detail/type/type_database_p.h"

#include <algorithm>
#include <unordered_map>
#include <vector>
#include <memory>
#include <set>
#include <thread>
#include <mutex>
#include <cstring>
#include <cctype>
#include <utility>

using namespace std;

namespace rttr
{
namespace
{

/////////////////////////////////////////////////////////////////////////////////////////

bool rotate_char_when_whitespace_before(std::string& text, std::string::size_type& pos, char c)
{
    auto result = text.find(c, pos);
    if (result != std::string::npos && result > 0)
    {
        if (::isspace(text[result - 1]))
        {
            text[result - 1] = c;
            text[result] = ' ';
        }
        pos = result + 1;
        return true;
    }

    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////

void move_pointer_and_ref_to_type(std::string& type_name)
{
    std::string::size_type pos = 0;
    while(pos < type_name.length())
    {
        if (!rotate_char_when_whitespace_before(type_name, pos, '*') &&
            !rotate_char_when_whitespace_before(type_name, pos, '&') &&
            !rotate_char_when_whitespace_before(type_name, pos, ')'))
        {
            pos = std::string::npos;
        }
    }

    const auto non_whitespace = type_name.find_last_not_of(' ');
    type_name.resize(non_whitespace + 1);
}

} // end anonymous namespace

/////////////////////////////////////////////////////////////////////////////////////////

std::string type::normalize_orig_name(string_view name)
{
    std::string normalized_name = name.to_string();

    move_pointer_and_ref_to_type(normalized_name);
    return normalized_name;
}

/////////////////////////////////////////////////////////////////////////////////////////

type type::get_raw_type() const RTTR_NOEXCEPT
{
    return type(&m_type_data_funcs->get_raw_type());
}

/////////////////////////////////////////////////////////////////////////////////////////

type type::get_wrapped_type() const RTTR_NOEXCEPT
{
    return m_type_data_funcs->get_wrapped_type();
}

/////////////////////////////////////////////////////////////////////////////////////////

bool type::is_derived_from(const type& other) const RTTR_NOEXCEPT
{
    auto& src_raw_type = m_type_data_funcs->get_raw_type();
    auto& tgt_raw_type = other.m_type_data_funcs->get_raw_type();

    if (&src_raw_type == &tgt_raw_type)
        return true;

    for (auto& t : src_raw_type.get_class_data().m_base_types)
    {
        if (t.m_type_data_funcs == &tgt_raw_type)
        {
            return true;
        }
    }

    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////

void* type::apply_offset(void* ptr, const type& source_type, const type& target_type) RTTR_NOEXCEPT
{
    auto& src_raw_type = source_type.m_type_data_funcs->get_raw_type();
    auto& tgt_raw_type = target_type.m_type_data_funcs->get_raw_type();

    if (&src_raw_type == &tgt_raw_type || ptr == nullptr)
        return ptr;

    const detail::derived_info info = src_raw_type.get_class_data().m_derived_info_func(ptr);
    if (&info.m_type.m_type_data_funcs->get_raw_type() == &tgt_raw_type)
        return info.m_ptr;

    auto& class_list = info.m_type.m_type_data_funcs->get_raw_type().get_class_data();
    int i = 0;
    for (auto& t : class_list.m_base_types)
    {
        if (t.m_type_data_funcs == &tgt_raw_type)
        {
            return class_list.m_conversion_list[i](info.m_ptr);
        }
        ++i;
    }

    return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////

type type::get_derived_type(void* ptr, const type& source_type) RTTR_NOEXCEPT
{
    if (ptr == nullptr)
        return type();

    auto& src_raw_type = source_type.m_type_data_funcs->get_raw_type();
    const detail::derived_info info = src_raw_type.get_class_data().m_derived_info_func(ptr);
    return info.m_type;
}

/////////////////////////////////////////////////////////////////////////////////////////

array_range<type> type::get_base_classes() const RTTR_NOEXCEPT
{
    return array_range<type>(m_type_data_funcs->get_class_data().m_base_types.data(),
                             m_type_data_funcs->get_class_data().m_base_types.size());
}

/////////////////////////////////////////////////////////////////////////////////////////

array_range<type> type::get_derived_classes() const RTTR_NOEXCEPT
{
    return array_range<type>(m_type_data_funcs->get_class_data().m_derived_types.data(),
                             m_type_data_funcs->get_class_data().m_derived_types.size());
}

/////////////////////////////////////////////////////////////////////////////////////////

bool type::is_wrapper() const RTTR_NOEXCEPT
{
    return (m_type_data_funcs->get_wrapped_type().m_type_data_funcs->type_index != type::m_invalid_id);
}

/////////////////////////////////////////////////////////////////////////////////////////

type type::get_raw_array_type() const RTTR_NOEXCEPT
{
    return type(&m_type_data_funcs->get_array_raw_type());
}

/////////////////////////////////////////////////////////////////////////////////////////

array_range<type> type::get_types() RTTR_NOEXCEPT
{
    auto& type_list = detail::type_database::instance().m_type_list;
    return array_range<type>(&type_list[1], type_list.size() - 1);
}

/////////////////////////////////////////////////////////////////////////////////////////

variant type::get_metadata(const variant& key) const
{
    return detail::type_database::instance().get_metadata(*this, key);
}

/////////////////////////////////////////////////////////////////////////////////////////

constructor type::get_constructor(const std::vector<type>& args) const RTTR_NOEXCEPT
{
    return constructor(detail::type_database::instance().get_constructor(*this, args));
}

/////////////////////////////////////////////////////////////////////////////////////////

array_range<constructor> type::get_constructors() const RTTR_NOEXCEPT
{
    return detail::type_database::instance().get_constructors(*this);
}

/////////////////////////////////////////////////////////////////////////////////////////

array_range<constructor> type::get_constructors(filter_items filter) const RTTR_NOEXCEPT
{
    return detail::type_database::instance().get_constructors(*this, filter);
}

/////////////////////////////////////////////////////////////////////////////////////////

variant type::create(vector<argument> args) const
{
    auto ctor = detail::type_database::instance().get_constructor(*this, args);
    return ctor.invoke_variadic(std::move(args));
}

/////////////////////////////////////////////////////////////////////////////////////////

destructor type::get_destructor() const RTTR_NOEXCEPT
{
    return destructor(detail::type_database::instance().get_destructor(get_raw_type()));
}

/////////////////////////////////////////////////////////////////////////////////////////

bool type::destroy(variant& obj) const RTTR_NOEXCEPT
{
    return detail::type_database::instance().get_destructor(get_raw_type()).invoke(obj);
}

/////////////////////////////////////////////////////////////////////////////////////////

property type::get_property(string_view name) const RTTR_NOEXCEPT
{
    return detail::type_database::instance().get_class_property(get_raw_type(), name);
}

/////////////////////////////////////////////////////////////////////////////////////////

variant type::get_property_value(string_view name, instance obj) const
{
    const auto prop = get_property(name);
    return prop.get_value(obj);
}

/////////////////////////////////////////////////////////////////////////////////////////

variant type::get_property_value(string_view name)
{
    const auto prop = get_global_property(name);
    return prop.get_value(instance());
}

/////////////////////////////////////////////////////////////////////////////////////////

bool type::set_property_value(string_view name, instance obj, argument arg) const
{
    const auto prop = get_property(name);
    return prop.set_value(obj, arg);
}

/////////////////////////////////////////////////////////////////////////////////////////

bool type::set_property_value(string_view name, argument arg)
{
    const auto prop = get_global_property(name);
    return prop.set_value(instance(), arg);
}

/////////////////////////////////////////////////////////////////////////////////////////

array_range<property> type::get_properties() const RTTR_NOEXCEPT
{
    return detail::type_database::instance().get_class_properties(get_raw_type());
}

/////////////////////////////////////////////////////////////////////////////////////////

array_range<property> type::get_properties(filter_items filter) const RTTR_NOEXCEPT
{
    return detail::type_database::instance().get_class_properties(get_raw_type(), filter);
}

/////////////////////////////////////////////////////////////////////////////////////////

method type::get_method(string_view name) const RTTR_NOEXCEPT
{
    return detail::type_database::instance().get_class_method(get_raw_type(), name);
}

/////////////////////////////////////////////////////////////////////////////////////////

method type::get_method(string_view name, const std::vector<type>& params) const RTTR_NOEXCEPT
{
    return detail::type_database::instance().get_class_method(get_raw_type(), name, params);
}

/////////////////////////////////////////////////////////////////////////////////////////

array_range<method> type::get_methods() const RTTR_NOEXCEPT
{
    return detail::type_database::instance().get_class_methods(get_raw_type());
}

/////////////////////////////////////////////////////////////////////////////////////////

array_range<method> type::get_methods(filter_items filter) const RTTR_NOEXCEPT
{
    return detail::type_database::instance().get_class_methods(get_raw_type(), filter);
}

/////////////////////////////////////////////////////////////////////////////////////////

property type::get_global_property(string_view name) RTTR_NOEXCEPT
{
    return property(detail::type_database::instance().get_global_property(name));
}

/////////////////////////////////////////////////////////////////////////////////////////

method type::get_global_method(string_view name) RTTR_NOEXCEPT
{
    return detail::type_database::instance().get_global_method(name);
}

/////////////////////////////////////////////////////////////////////////////////////////

method type::get_global_method(string_view name, const std::vector<type>& params) RTTR_NOEXCEPT
{
    return detail::type_database::instance().get_global_method(name, params);
}

/////////////////////////////////////////////////////////////////////////////////////////

array_range<method> type::get_global_methods() RTTR_NOEXCEPT
{
    return detail::type_database::instance().get_global_methods();
}

/////////////////////////////////////////////////////////////////////////////////////////

array_range<property> type::get_global_properties() RTTR_NOEXCEPT
{
    return detail::type_database::instance().get_global_properties();
}

/////////////////////////////////////////////////////////////////////////////////////////

enumeration type::get_enumeration() const RTTR_NOEXCEPT
{
    return detail::type_database::instance().get_enumeration(*this);
}

/////////////////////////////////////////////////////////////////////////////////////////

variant type::invoke(string_view name, instance obj, std::vector<argument> args) const
{
    if (auto meth = detail::type_database::instance().get_class_method(get_raw_type(), name, args))
        return meth.invoke_variadic(obj, args);

    return variant();
}

/////////////////////////////////////////////////////////////////////////////////////////

variant type::invoke(string_view name, std::vector<argument> args)
{
    const auto& db = detail::type_database::instance();
    return db.get_global_method(name, args).invoke_variadic(instance(), args);
}

/////////////////////////////////////////////////////////////////////////////////////////

type type::get_by_name(string_view name) RTTR_NOEXCEPT
{
    return detail::type_database::instance().get_by_name(name);
}

/////////////////////////////////////////////////////////////////////////////////////////

const detail::type_converter_base* type::get_type_converter(const type& target_type) const RTTR_NOEXCEPT
{
    return detail::type_database::instance().get_converter(*this, target_type);
}

/////////////////////////////////////////////////////////////////////////////////////////

} // end namespace rttr
