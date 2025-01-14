/*
 * Copyright 2011-2020 Gregory Banks
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "spiegel.hxx"
#include "np/spiegel/dwarf/state.hxx"
#include "np/spiegel/dwarf/walker.hxx"
#include "np/spiegel/dwarf/entry.hxx"
#include "np/spiegel/dwarf/enumerations.hxx"
#include "np/spiegel/platform/common.hxx"
#include "np/util/log.hxx"
#include <algorithm>

namespace np {
namespace spiegel {
using namespace std;
using namespace np::util;

value_t
value_t::make_invalid()
{
    value_t v;
    memset(&v, 0, sizeof(v));
    return v;
}

value_t
value_t::make_void()
{
    value_t v;
    memset(&v, 0, sizeof(v));
    v.which = type_t::TC_VOID;
    return v;
}

value_t
value_t::make_sint(int64_t i)
{
    value_t v;
    memset(&v, 0, sizeof(v));
    v.which = type_t::TC_SIGNED_LONG_LONG;
    v.val.vsint = i;
    return v;
}

value_t
value_t::make_sint(int32_t i)
{
    value_t v;
    memset(&v, 0, sizeof(v));
    v.which = type_t::TC_SIGNED_LONG_LONG;
    v.val.vsint = i;
    return v;
}

value_t
value_t::make_pointer(void *p)
{
    value_t v;
    memset(&v, 0, sizeof(v));
    v.which = type_t::TC_POINTER;
    v.val.vpointer = p;
    return v;
}

unsigned int
type_t::get_classification() const
{
    if (ref_ == np::spiegel::dwarf::reference_t::null)
	return TC_VOID;

    np::spiegel::dwarf::walker_t w(ref_);
    const np::spiegel::dwarf::entry_t *e = w.move_next();
    if (!e)
	return TC_INVALID;	// TODO: exception

    switch (e->get_tag())
    {
    case DW_TAG_base_type:
	{
	    uint32_t encoding = e->get_uint32_attribute(DW_AT_encoding);
	    uint32_t byte_size = e->get_uint32_attribute(DW_AT_byte_size);
	    switch (encoding)
	    {
	    case DW_ATE_float:
		return TC_MAJOR_FLOAT|byte_size;
	    case DW_ATE_signed:
	    case DW_ATE_signed_char:
		return TC_MAJOR_INTEGER|_TC_SIGNED|byte_size;
	    case DW_ATE_unsigned:
	    case DW_ATE_unsigned_char:
		return TC_MAJOR_INTEGER|_TC_UNSIGNED|byte_size;
	    }
	    break;
	}
    case DW_TAG_typedef:
    case DW_TAG_volatile_type:
    case DW_TAG_const_type:
	return type_t(e->get_reference_attribute(DW_AT_type), factory_).get_classification();
    case DW_TAG_pointer_type:
	return TC_POINTER;
    case DW_TAG_reference_type:
	return TC_REFERENCE;
    case DW_TAG_structure_type:
	return TC_STRUCT;
    case DW_TAG_union_type:
	return TC_UNION;
    case DW_TAG_class_type:
	return TC_CLASS;
    case DW_TAG_enumeration_type:
	{
	    uint32_t byte_size = e->get_uint32_attribute(DW_AT_byte_size);
	    if (!byte_size)
		byte_size = sizeof(int);
	    return TC_MAJOR_INTEGER|_TC_SIGNED|byte_size;
	}
    case DW_TAG_array_type:
	return TC_ARRAY;
    }
    return TC_INVALID;	// TODO: exception
}

string
type_t::get_classification_as_string() const
{
    unsigned int cln = get_classification();
    if (cln == TC_INVALID)
	return "invalid";

    static const char * const major_names[] =
	{ "0", "void", "pointer", "array", "integer", "float", "compound", "7" };
    string s = major_names[(cln >> 8) & 0x7];

    if ((cln & _TC_MAJOR_MASK) == TC_MAJOR_INTEGER)
	s += (cln & _TC_SIGNED ? "|signed" : "|unsigned");

    cln &= ~(_TC_MAJOR_MASK|_TC_SIGNED);

    char buf[32];
    snprintf(buf, sizeof(buf), "|%u", cln);
    s += buf;

    return s;
}

unsigned int
type_t::get_sizeof() const
{
    if (ref_ == np::spiegel::dwarf::reference_t::null)
	return 0;   // sizeof(void)

    np::spiegel::dwarf::walker_t w(ref_);
    const np::spiegel::dwarf::entry_t *e = w.move_next();
    if (!e)
	return 0;	// TODO: exception

    uint32_t byte_size = e->get_uint32_attribute(DW_AT_byte_size);

    switch (e->get_tag())
    {
    case DW_TAG_array_type:
	if (!byte_size)
	{
	    uint32_t element_size = type_t(e->get_reference_attribute(DW_AT_type), factory_).get_sizeof();
	    for (e = w.move_down() ; e ; e = w.move_next())
	    {
		uint32_t count;
		if (e->get_tag() == DW_TAG_subrange_type &&
		    ((count = e->get_uint32_attribute(DW_AT_count)) ||
		     (count = e->get_uint32_attribute(DW_AT_upper_bound))))
		{
		    byte_size = (byte_size ? byte_size : 1) * count;
		}
	    }
	    byte_size *= element_size;
	}
	break;
    case DW_TAG_typedef:
    case DW_TAG_volatile_type:
    case DW_TAG_const_type:
	return type_t(e->get_reference_attribute(DW_AT_type), factory_).get_sizeof();
    case DW_TAG_pointer_type:
    case DW_TAG_reference_type:
	return _NP_ADDRSIZE;
    }
    return byte_size;
}

string
type_t::get_name() const
{
    if (ref_ == np::spiegel::dwarf::reference_t::null)
	return "";

    np::spiegel::dwarf::walker_t w(ref_);
    const np::spiegel::dwarf::entry_t *e = w.move_next();
    if (!e)
	return "wtf?";

    const char *name = e->get_string_attribute(DW_AT_name);
    switch (e->get_tag())
    {
    case DW_TAG_base_type:
    case DW_TAG_typedef:
    case DW_TAG_structure_type:
    case DW_TAG_union_type:
    case DW_TAG_class_type:
    case DW_TAG_enumeration_type:
    case DW_TAG_namespace_type:
	return (name ? name : "");
	break;
    case DW_TAG_pointer_type:
    case DW_TAG_reference_type:
    case DW_TAG_volatile_type:
    case DW_TAG_const_type:
    case DW_TAG_array_type:
    case DW_TAG_subroutine_type:
	return "";
    default:
	return np::spiegel::dwarf::tagnames.to_name(e->get_tag());
    }
}

string
type_t::to_string(string inner) const
{
    if (ref_ == np::spiegel::dwarf::reference_t::null)
	return (inner.length() ? "void " + inner : "void");

    np::spiegel::dwarf::walker_t w(ref_);
    const np::spiegel::dwarf::entry_t *e = w.move_next();
    if (!e)
	return (inner.length() ? "wtf? " + inner : "wtf?");

    string s;

    const char *name = e->get_string_attribute(DW_AT_name);
    switch (e->get_tag())
    {
    case DW_TAG_base_type:
    case DW_TAG_typedef:
	s = (name ? name : "wtf?");
	break;
    case DW_TAG_pointer_type:
	return type_t(e->get_reference_attribute(DW_AT_type), factory_).to_string("*" + inner);
    case DW_TAG_reference_type:
	return type_t(e->get_reference_attribute(DW_AT_type), factory_).to_string("&" + inner);
    case DW_TAG_volatile_type:
	return type_t(e->get_reference_attribute(DW_AT_type), factory_).to_string("volatile " + inner);
    case DW_TAG_const_type:
	return type_t(e->get_reference_attribute(DW_AT_type), factory_).to_string("const " + inner);
    case DW_TAG_structure_type:
	s = string("struct ") + (name ? name : "{...}");
	break;
    case DW_TAG_union_type:
	s = string("union ") + (name ? name : "{...}");
	break;
    case DW_TAG_class_type:
	s = string("class ") + (name ? name : "{...}");
	break;
    case DW_TAG_enumeration_type:
	s = string("enum ") + (name ? name : "{...}");
	break;
    case DW_TAG_namespace_type:
	s = string("namespace ") + (name ? name : "{...}");
	break;
    case DW_TAG_array_type:
	{
	    type_t element_type(e->get_reference_attribute(DW_AT_type), factory_);
	    bool found = false;
	    for (e = w.move_down() ; e ; e = w.move_next())
	    {
		uint32_t count;
		if (e->get_tag() == DW_TAG_subrange_type &&
		    ((count = e->get_uint32_attribute(DW_AT_count)) ||
		     (count = e->get_uint32_attribute(DW_AT_upper_bound))))
		{
		    // TODO: unuglify this
		    char buf[32];
		    snprintf(buf, sizeof(buf), "[%u]", count);
		    inner += buf;
		    found = true;
		}
	    }
	    if (!found)
		inner += "[]";
	    return element_type.to_string(inner);
	}
    case DW_TAG_subroutine_type:
	{
	    // TODO: elide the parenethese around inner if it's an identifier
	    type_t return_type(e->get_reference_attribute(DW_AT_type), factory_);
	    inner = "(" + inner + ")(";
	    bool ellipsis = false;
	    unsigned int nparam = 0;
	    for (e = w.move_down() ; e ; e = w.move_next())
	    {
		if (!ellipsis && e->get_tag() == DW_TAG_formal_parameter)
		{
		    if (nparam++)
			inner += ", ";
		    const char *param = e->get_string_attribute(DW_AT_name);
		    if (!param)
			param = "";
		    inner += type_t(e->get_reference_attribute(DW_AT_type), factory_).to_string(param);
		}
		else if (e->get_tag() == DW_TAG_unspecified_parameters)
		{
		    ellipsis = true;
		    if (nparam++)
			inner += ", ";
		    inner += "...";
		}
	    }
	    inner += ")";
	    return return_type.to_string(inner);
	}
    default:
	s = np::spiegel::dwarf::tagnames.to_name(e->get_tag());
	break;
    }
    if (inner.length())
	s += " " + inner;
    return s;
}

string
type_t::to_string() const
{
    return to_string(string(""));
}

state_t::state_t()
{
    state_ = new np::spiegel::dwarf::state_t();
}

state_t::~state_t()
{
    delete state_;
}

bool
state_t::add_self()
{
    return state_->add_self();
}

bool
state_t::add_executable(const char *filename)
{
    return state_->add_executable(filename);
}

compile_unit_t *
state_t::cu_from_lower(np::spiegel::dwarf::compile_unit_t *lcu)
{
    compile_unit_t *cu = compile_unit_t::to_upper(lcu);
    if (!cu)
    {
        cu = new compile_unit_t(lcu->make_root_reference(), factory_);
        lcu->set_upper(cu);
    }
    return cu;
}

vector<compile_unit_t *>
state_t::get_compile_units()
{
    vector<np::spiegel::compile_unit_t *> res;

    for (auto &i : state_->get_compile_units())
        res.push_back(cu_from_lower(i));
    return res;
}

static bool
compare_functions_by_address(function_t *fa, function_t *fb)
{
    return (fa->get_address() < fb->get_address());
}

vector<function_t *>
compile_unit_t::get_functions()
{
    np::spiegel::dwarf::walker_t w(lower()->make_root_reference());
    // move to DW_TAG_compile_unit
    w.move_next();

    vector<function_t *> res;

    // scan children of DW_TAG_compile_unit for functions
    for (const np::spiegel::dwarf::entry_t *e = w.move_down() ; e ; e = w.move_next())
    {
	if (e->get_tag() != DW_TAG_subprogram ||
	    !e->get_string_attribute(DW_AT_name))
	    continue;

	res.push_back(factory_.make_function(w));
    }
    // Ensure the functions are reported in address order, even
    // when the DWARF info has them recorded in some other order.
    // Seen in Debian Buster.
    std::sort(res.begin(), res.end(), compare_functions_by_address);
    return res;
}

void
compile_unit_t::dump_types()
{
    np::spiegel::dwarf::walker_t w(ref_);
    w.move_next();
    for (const np::spiegel::dwarf::entry_t *e = w.move_down() ; e ; e = w.move_next())
    {
	// filter for all types
	const char *tagname = np::spiegel::dwarf::tagnames.to_name(e->get_tag());
	switch (e->get_tag())
	{
	case DW_TAG_base_type:
	case DW_TAG_reference_type:
	case DW_TAG_pointer_type:
	case DW_TAG_array_type:
	case DW_TAG_structure_type:
	case DW_TAG_union_type:
	case DW_TAG_class_type:
	case DW_TAG_const_type:
	case DW_TAG_volatile_type:
	case DW_TAG_enumeration_type:
	case DW_TAG_subroutine_type:
	case DW_TAG_typedef:
	    break;
	case DW_TAG_subprogram:
	case DW_TAG_variable:
	    continue;
	default:
	    printf("    WTF??? %s\n", tagname);
	    continue;
	}
	printf("    Type %s at 0x%x\n", tagname, e->get_offset());
	type_t type(w.get_reference(), factory_);
	printf("        to_string=\"%s\"\n", type.to_string().c_str());
	printf("        sizeof=%u\n", type.get_sizeof());
	printf("        classification=%u (%s)\n",
		type.get_classification(),
		type.get_classification_as_string().c_str());
    }
}

member_t::member_t(np::spiegel::dwarf::walker_t &w, _factory_t &factory)
 :  _cacheable_t(w.get_reference(), factory),
    name_(w.get_entry()->get_string_attribute(DW_AT_name))
{
    if (!name_)
	name_ = "";
}

const compile_unit_t *
member_t::get_compile_unit() const
{
    dwarf::compile_unit_offset_tuple_t res = ref_.resolve();
    assert(res._cu);
    return static_cast<compile_unit_t*>(res._cu->get_upper());
}

bool
function_t::populate()
{
    np::spiegel::dwarf::walker_t w(ref_);
    // move to DW_TAG_subprogram
    const np::spiegel::dwarf::entry_t *e = w.move_next();
    if (!e)
	return false;

    low_pc_ = e->get_uint64_attribute(DW_AT_low_pc);
    is_declaration_ = e->get_uint64_attribute(DW_AT_declaration);

    return true;
}

string
function_t::get_full_name() const
{
    np::spiegel::dwarf::state_t *state = np::spiegel::dwarf::state_t::instance();
    return state->get_full_name(ref_);
}

type_t *
function_t::get_return_type() const
{
    np::spiegel::dwarf::walker_t w(ref_);
    const np::spiegel::dwarf::entry_t *e = w.move_next();
    return factory_.make_type(e->get_reference_attribute(DW_AT_type));
}

vector<type_t*>
function_t::get_parameter_types() const
{
    vector<type_t *> res;

    np::spiegel::dwarf::walker_t w(ref_);
    const np::spiegel::dwarf::entry_t *e = w.move_next();
    for (e = w.move_down() ; e ; e = w.move_next())
    {
	if (e->get_tag() == DW_TAG_formal_parameter)
	    res.push_back(factory_.make_type(e->get_reference_attribute(DW_AT_type)));
	else if (e->get_tag() == DW_TAG_unspecified_parameters)
	    break;
    }
    return res;
}

vector<const char *>
function_t::get_parameter_names() const
{
    vector<const char *> res;

    np::spiegel::dwarf::walker_t w(ref_);
    const np::spiegel::dwarf::entry_t *e = w.move_next();
    for (e = w.move_down() ; e ; e = w.move_next())
    {
	if (e->get_tag() == DW_TAG_formal_parameter)
	{
	    const char *name = e->get_string_attribute(DW_AT_name);
	    if (!name || !*name)
		name = "<unknown>";
	    res.push_back(name);
	}
	else if (e->get_tag() == DW_TAG_unspecified_parameters)
	    break;
    }
    return res;
}

bool
function_t::has_unspecified_parameters() const
{
    np::spiegel::dwarf::walker_t w(ref_);
    const np::spiegel::dwarf::entry_t *e = w.move_next();
    for (e = w.move_down() ; e ; e = w.move_next())
    {
	if (e->get_tag() == DW_TAG_unspecified_parameters)
	    return true;
    }
    return false;
}

string
function_t::to_string() const
{
    np::spiegel::dwarf::walker_t w(ref_);
    const np::spiegel::dwarf::entry_t *e = w.move_next();
    type_t return_type(e->get_reference_attribute(DW_AT_type), factory_);
    string inner = name_;
    inner += "(";
    unsigned int nparam = 0;
    for (e = w.move_down() ; e ; e = w.move_next())
    {
	if (e->get_tag() == DW_TAG_formal_parameter)
	{
	    if (nparam++)
		inner += ", ";
	    const char *param = e->get_string_attribute(DW_AT_name);
	    if (!param)
		param = "";
	    inner += type_t(e->get_reference_attribute(DW_AT_type), factory_).to_string(param);
	}
	else if (e->get_tag() == DW_TAG_unspecified_parameters)
	{
	    if (nparam++)
		inner += ", ";
	    inner += "...";
	    break;
	}
    }
    inner += ")";
    return return_type.to_string(inner);
}

// Return the recorded address of the function, or 0 if the function is not
// defined in this compile unit (in which case, good luck finding it in
// some other compile unit).  Note this needs to be turned into an
// instrumentable address to instrument or invoke it.
//
// TODO: this can be wrong if the DIE has DW_AT_ranges
addr_t
function_t::get_address() const
{
    return low_pc_;
}

// Return the live address of the function, or 0 if the function is not
// defined in this compile unit (in which case, good luck finding it in
// some other compile unit).  Note this needs to be turned into an
// instrumentable address to instrument or invoke it.
addr_t
function_t::get_live_address() const
{
    np::spiegel::dwarf::walker_t w(ref_);
    const np::spiegel::dwarf::entry_t *e = w.move_next();
    return w.live_address(e->get_address_attribute(DW_AT_low_pc));
}

bool
function_t::is_declaration() const
{
    return is_declaration_;
}


#if SPIEGEL_DYNAMIC
value_t
function_t::invoke(vector<value_t> args __attribute__((unused))) const
{
    // TODO: check that DW_AT_calling_convention == DW_CC_normal
    // TODO: check that we're talking to self
    addr_t addr = get_live_address();
    if (!addr)
	return value_t::make_invalid();

    // Hacky special cases, enough to get NP working without
    // writing the general purpose platform ABI invoke()
    if (get_parameter_types().size() > 0)
	return value_t::make_invalid();
    switch (get_return_type()->get_classification())
    {
    case type_t::TC_VOID:
	((void (*)(void))addr)();
	return value_t::make_void();
    case type_t::TC_SIGNED_INT:
	return value_t::make_sint(((int32_t (*)(void))addr)());
    case type_t::TC_POINTER:
	return value_t::make_pointer(((void *(*)(void))addr)());
    }
    return value_t::make_invalid();
//     return np::spiegel::platform::invoke(addr, args);
}
#endif

bool
state_t::describe_address(addr_t addr, class location_t &loc)
{
    addr = state_->recorded_address(addr);

    np::spiegel::dwarf::reference_t curef;
    np::spiegel::dwarf::reference_t funcref;
    if (!state_->describe_address(addr, curef,
				 funcref, loc.offset_))
	return false;

    dwarf::compile_unit_t *cu = 0;
    if (curef == np::spiegel::dwarf::reference_t::null)
    {
        dwarf::compile_unit_offset_tuple_t res = funcref.resolve();
        assert(res._cu);
        cu = res._cu;
    }
    else
    {
        dwarf::compile_unit_offset_tuple_t res = curef.resolve();
        assert(res._cu);
        cu = res._cu;
    }
    if (!cu->get_source_line(addr, &loc.filename_, &loc.line_, &loc.column_))
        return false;
    loc.compile_unit_ = cu_from_lower(cu);
    loc.function_ = factory_.make_function(funcref);
    return true;
}


_cacheable_t *
_factory_t::find(np::spiegel::dwarf::reference_t ref)
{
    std::map<np::spiegel::dwarf::reference_t, _cacheable_t*>::const_iterator i = cache_.find(ref);
    if (i == cache_.end())
	return 0;
    return i->second;
}

_cacheable_t *
_factory_t::add(_cacheable_t *cc)
{
    cache_[cc->ref()] = cc;
    return cc;
}

type_t *
_factory_t::make_type(np::spiegel::dwarf::reference_t ref)
{
    _cacheable_t *cc = find(ref);
    if (!cc)
	cc = add(new type_t(ref, *this));
    return (type_t *)cc;
}

function_t *
_factory_t::make_function(np::spiegel::dwarf::walker_t &w)
{
    function_t *fn = (function_t *)find(w.get_reference());
    if (!fn)
    {
        fn = new function_t(w, *this);
        if (!fn->populate())
        {
            delete fn;
            return 0;
        }
        add(fn);
    }
    return fn;
}

function_t *
_factory_t::make_function(np::spiegel::dwarf::reference_t ref)
{
    if (ref == np::spiegel::dwarf::reference_t::null)
	return 0;
    np::spiegel::dwarf::walker_t w(ref);
    const np::spiegel::dwarf::entry_t *e = w.move_next();
    return (e ? make_function(w) : 0);
}

std::string
state_t::describe_stacktrace()
{
    string s;
    vector<addr_t> stack = np::spiegel::platform::get_stacktrace();
    bool first = true;
    for (addr_t addr : stack)
    {
	s += (first ? "at " : "by ");
	s += HEX(addr);
	s += ":";
	location_t loc;
	if (describe_address(addr, loc))
	{
	    if (loc.function_)
	    {
		s += " ";
		s += loc.function_->get_full_name();
	    }

	    if (loc.compile_unit_)
	    {
		s += " (";
		s += loc.filename_;
		if (loc.line_)
		{
		    s += ":";
		    s += dec(loc.line_);
		}
		s += ")";
	    }
	    if (loc.function_ && loc.function_->get_name() == "main")
            {
                s += "\n";
                break;
            }
	}
	s += "\n";
	first = false;
    }
    return s;
}

void
state_t::dump_structs()
{
    state_->dump_structs();
}

void
state_t::dump_functions()
{
    state_->dump_functions();
}

void
state_t::dump_variables()
{
    state_->dump_variables();
}

void
state_t::dump_info(bool preorder, bool paths)
{
    state_->dump_info(preorder, paths);
}

void
state_t::dump_abbrevs()
{
    state_->dump_abbrevs();
}

// close the namespaces
}; };
