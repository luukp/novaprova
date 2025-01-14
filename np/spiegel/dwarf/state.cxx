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
#include "np/spiegel/common.hxx"
#include "state.hxx"
#include "reader.hxx"
#include "compile_unit.hxx"
#include "link_object.hxx"
#include "walker.hxx"
#include "np/spiegel/platform/common.hxx"
#include "np/util/log.hxx"

namespace np { namespace spiegel { namespace dwarf {
using namespace std;
using namespace np::util;

static const char *get_partial_name(reference_t ref);

state_t *state_t::instance_ = 0;

state_t::state_t()
{
    assert(!instance_);
    instance_ = this;
}

state_t::~state_t()
{
    vector<link_object_t*>::iterator i;
    for (i = link_objects_.begin() ; i != link_objects_.end() ; ++i)
	delete *i;
    address_index_.clear();
    link_object_index_.clear();

    assert(instance_ == this);
    instance_ = 0;
}

bool
state_t::read_compile_units(link_object_t *lo)
{
    dprintf("reading compile units for link_object %s\n", lo->get_filename());
    reader_t infor = lo->get_section(DW_sec_info)->get_contents();
    reader_t abbrevr = lo->get_section(DW_sec_abbrev)->get_contents();
    reader_t liner = lo->get_section(DW_sec_line)->get_contents();

    compile_unit_t *cu = 0;
    for (;;)
    {
	cu = new compile_unit_t(compile_units_.size(), lo);
	if (!cu->read_header(infor))
	    break;

	cu->read_abbrevs(abbrevr);

	if (!cu->read_attributes())
	    break;

        if (!cu->read_lineno_program(liner))
            break;

	compile_units_.push_back(cu);
    }
    delete cu;
    return true;
}

static const char *
filename_is_ignored(const char *filename)
{
    static const char * const prefixes[] =
    {
	"/bin/",
	"/lib/",
	"/lib64/",
	"/usr/bin/",
	"/usr/lib/",
	"/usr/lib64/",
	"/opt/",
	"linux-gate.so",
	"linux-vdso.so",
#if defined(__APPLE__)
        /* Homebrew keg-only libraries */
        "/usr/local/opt/",
        /* Apple system libraries */
        "/System/Library/Frameworks/CoreFoundation.framework",
#endif
	NULL
    };
    const char * const *p;

    for (p = prefixes ; *p ; p++)
    {
	if (!strncmp(filename, *p, strlen(*p)))
	    return *p;
    }
    return 0;
}

bool
state_t::add_self()
{
    dprintf("Adding self executable\n");
    char *exe = np::spiegel::platform::self_exe();
    bool r = false;

    vector<np::spiegel::platform::linkobj_t> los = np::spiegel::platform::get_linkobjs();
    vector<np::spiegel::platform::linkobj_t>::iterator i;
    const char *filename;
    dprintf("converting platform linkobjs to spiegel linkobjs\n");
    for (i = los.begin() ; i != los.end() ; ++i)
    {
	dprintf("platform linkobj %s slide 0x%lx\n", i->name, i->slide);
	filename = i->name;
	if (!filename)
	    filename = exe;

        if (is_enabled_for(np::log::DEBUG))
        {
            for (auto &sm : i->mappings)
            {
                dprintf("mapping addresses 0x%lx-0x%lx -> filename %s slide 0x%lx vaddr 0x%lx",
                        (unsigned long)sm.get_map(),
                        (unsigned long)sm.get_map() + sm.get_size(),
                        filename,
                        i->slide,
                        (unsigned long)sm.get_map() - i->slide);
            }
        }

        const char *ignored_prefix = filename_is_ignored(filename);
	if (ignored_prefix)
        {
	    dprintf("matched prefix \"%s\", ignoring\n", ignored_prefix);
	    continue;
        }

	link_object_t *lo = get_link_object(filename);
        if (lo)
        {
            dprintf("platform link object has duplicate name \"%s\", skipping", filename);
        }
	else
	{
            lo = make_link_object(filename);
	    dprintf("have spiegel link_object\n");
            lo->set_system_mappings(i->mappings);
	    lo->set_slide(i->slide);
	    for (auto &sm : i->mappings)
		link_object_index_.insert((np::spiegel::addr_t)sm.get_map(),
                                          (np::spiegel::addr_t)sm.get_map() + sm.get_size(),
                                          lo);
	}
    }

    r = read_link_objects();
    if (r)
	prepare_address_index();
    free(exe);
    return r;
}

bool
state_t::add_executable(const char *filename)
{
    dprintf("Adding executable %s\n", filename);
    link_object_t *lo = get_link_object(filename);
    if (!lo)
        lo = make_link_object(filename);
    bool r = read_link_objects();
    if (r)
	prepare_address_index();
    return r;
}

link_object_t *
state_t::get_link_object(const char *filename) const
{
    if (!filename)
	return 0;

    for (link_object_t *lo : link_objects_)
    {
	if (!strcmp(lo->get_filename(), filename))
	    return lo;
    }
    return 0;
}

link_object_t *
state_t::make_link_object(const char *filename)
{
    link_object_t *lo = new link_object_t(filename, this);
    link_objects_.push_back(lo);
    return lo;
}

bool
state_t::read_link_objects()
{
    vector<link_object_t*>::iterator i;
    for (i = link_objects_.begin() ; i != link_objects_.end() ; ++i)
    {
	/* TODO: why isn't this in filename_is_ignored() ?? */
#if HAVE_VALGRIND
	/* Ignore Valgrind's preloaded dynamic objects.  No
	 * good will come of trying to poke into those.
	 */
	const char *tt = strrchr((*i)->get_filename(), '/');
	if (tt && !strncmp(tt, "/vgpreload_", 11))
	    continue;
#endif

	if (!(*i)->map_sections())
	    return false;
        /* note map_sections() can succeed but result in no sections */
        if ((*i)->has_sections() && !read_compile_units(*i))
	    return false;
    }
    return true;
}

static string
describe_type(const walker_t &ow)
{
    walker_t w = ow;
    const entry_t *e = ow.get_entry();
    reference_t ref;

    if (e->get_attribute(DW_AT_specification))
	e = w.move_to(e->get_reference_attribute(DW_AT_specification));

    ref = e->get_reference_attribute(DW_AT_type);
    if (ref == reference_t::null)
        return string("void ");

    e = w.move_to(ref);
    if (!e)
        return string("??? ");

    string s;
    const char *name = get_partial_name(w.get_reference());
    switch (e->get_tag())
    {
    case DW_TAG_base_type:
    case DW_TAG_typedef:
        s += name;
        s += " ";
	break;
    case DW_TAG_pointer_type:
	s += describe_type(w);
        s += "* ";
	break;
    case DW_TAG_volatile_type:
	s += describe_type(w);
	s += "volatile ";
	break;
    case DW_TAG_const_type:
	s += describe_type(w);
	s += "const ";
	break;
    case DW_TAG_structure_type:
        s += "struct ";
        s += (name ? name : "{...}");
        s += " ";
	break;
    case DW_TAG_union_type:
        s += "union ";
        s += (name ? name : "{...}");
        s += " ";
	break;
    case DW_TAG_class_type:
        s += "class ";
        s += (name ? name : "{...}");
        s += " ";
	break;
    case DW_TAG_enumeration_type:
        s += "enum ";
        s += (name ? name : "{...}");
        s += " ";
	break;
    case DW_TAG_namespace_type:
        s += "namespace ";
        s += (name ? name : "{...}");
        s += " ";
	break;
    case DW_TAG_array_type:
	s += describe_type(w);
	for (e = w.move_down() ; e ; e = w.move_next())
	{
	    uint32_t count;
	    if (e->get_tag() == DW_TAG_subrange_type &&
		((count = e->get_uint32_attribute(DW_AT_count)) ||
		 (count = e->get_uint32_attribute(DW_AT_upper_bound))))
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "[%u]", count);
                s += buf;
            }
	}
	s += " ";
	break;
    default:
        s += tagnames.to_name(e->get_tag());
	s += " ";
	break;
    }
    return s;
}

static string
describe_function_parameters(walker_t &w)
{
    string s = "(";
    int nparams = 0;
    bool got_ellipsis = false;
    for (const entry_t *e = w.move_down() ; e ; e = w.move_next())
    {
	if (got_ellipsis)
	    continue;
	if (e->get_tag() == DW_TAG_formal_parameter)
	{
	    if (nparams++)
                s += ", ";
	    s += describe_type(w);
	}
	else if (e->get_tag() == DW_TAG_unspecified_parameters)
	{
	    if (nparams++)
		s += ", ";
	    s += "...";
	    got_ellipsis = true;
	}
    }
    s += ")";
    return s;
}

void
state_t::dump_structs()
{
    const char *keyword;

    vector<compile_unit_t*>::iterator i;
    for (i = compile_units_.begin() ; i != compile_units_.end() ; ++i)
    {
	printf("compile_unit {\n");

	walker_t w(*i);
	w.move_next();	// at the DW_TAG_compile_unit
	while (const entry_t *e = w.move_preorder())
	{
	    switch (e->get_tag())
	    {
	    case DW_TAG_structure_type: keyword = "struct"; break;
	    case DW_TAG_union_type: keyword = "union"; break;
	    case DW_TAG_class_type: keyword = "class"; break;
	    default: continue;
	    }

	    if (e->get_attribute(DW_AT_declaration) &&
		e->get_uint32_attribute(DW_AT_declaration))
		continue;

	    const char *structname = get_partial_name(w.get_reference());
	    if (!structname)
		continue;
	    printf("%s %s {\n", keyword, get_full_name(w.get_reference()).c_str());

	    // print members
            vector<string> variables;
            vector<string> functions;
	    for (e = w.move_down() ; e ; e = w.move_next())
	    {
		const char *name = e->get_string_attribute(DW_AT_name);
		if (!name)
		    continue;

                string s;
		switch (e->get_tag())
		{
		case DW_TAG_member:
		case DW_TAG_variable:	/* seen on some older toolchains, e.g. SLES11 */
                    s += "    /*member*/ ";
                    s += describe_type(w);
		    s += " ";
                    s += name;
                    s += ";\n";
                    variables.push_back(s);
		    break;
		case DW_TAG_subprogram:
		    {
			if (!strcmp(name, structname))
			{
			    s += "    /*constructor*/ ";
			}
			else if (name[0] == '~' && !strcmp(name+1, structname))
			{
			    s += "    /*destructor*/ ";
			}
			else
			{
			    s += "    /*function*/ ";
                            s += describe_type(w);
			}
			s += name;
			s += describe_function_parameters(w);
			s += "\n";
                        functions.push_back(s);
		    }
		    break;
		default:
		    s += "    /* ";
                    s += tagnames.to_name(e->get_tag());
                    s += " */\n";
                    variables.push_back(s);
		    continue;
		}
	    }
            for (string s : variables)
                printf("%s", s.c_str());
            for (string s : functions)
                printf("%s", s.c_str());
	    printf("} %s\n", keyword);
	}

	printf("} compile_unit\n");
    }
}

void
state_t::dump_functions()
{
    printf("Functions\n");
    printf("=========\n");

    vector<compile_unit_t*>::iterator i;
    for (i = compile_units_.begin() ; i != compile_units_.end() ; ++i)
    {
	printf("compile_unit {\n");

	walker_t w(*i);
	w.move_next();	// at the DW_TAG_compile_unit
	for (const entry_t *e = w.move_down() ; e ; e = w.move_next())
	{
	    if (e->get_tag() != DW_TAG_subprogram)
		continue;

	    const char *name = e->get_string_attribute(DW_AT_name);
	    if (!name)
		continue;

            printf("%s", describe_type(w).c_str());
	    printf("%s", name);
	    printf("%s", describe_function_parameters(w).c_str());
	    printf("\n");
	}

	printf("} compile_unit\n");
    }
    printf("\n\n");
}

void
state_t::dump_variables()
{
    vector<compile_unit_t*>::iterator i;
    for (i = compile_units_.begin() ; i != compile_units_.end() ; ++i)
    {
	walker_t w(*i);
	const entry_t *e = w.move_next();	// at the DW_TAG_compile_unit

	printf("compile_unit %s {\n",
	       e->get_string_attribute(DW_AT_name));

	while ((e = w.move_preorder()))
	{
	    if (e->get_tag() != DW_TAG_variable)
		continue;
	    if (!e->get_attribute(DW_AT_location))
		continue;

            printf("%s", describe_type(w).c_str());
	    printf("%s;\n", get_full_name(w.get_reference()).c_str());
	}

	printf("} compile_unit\n");
    }
}

static void
dump_path(const walker_t &w)
{
    printf("Path:");
    vector<reference_t> path = w.get_path();
    vector<reference_t>::iterator i;
    for (i = path.begin() ; i != path.end() ; ++i)
	printf(" %s", i->as_string().c_str());
    printf("\n");
}

static void
recursive_dump(const entry_t *e, walker_t &w, unsigned depth, bool paths)
{
    assert(e->get_level() == depth);
    if (paths)
	dump_path(w);
    for (unsigned int i = 0 ; i < depth ; i++)
        fputs("  ", stdout);
    string estr = e->to_string();
    fputs(estr.c_str(), stdout);
    fputs("\n", stdout);
    for (e = w.move_down() ; e ; e = w.move_next())
	recursive_dump(e, w, depth+1, paths);
}

void
state_t::dump_info(bool preorder, bool paths)
{
    printf("Info\n");
    printf("====\n");
    vector<compile_unit_t*>::iterator i;
    for (i = compile_units_.begin() ; i != compile_units_.end() ; ++i)
    {
	printf("compile_unit {\n");
	walker_t w(*i);
	if (preorder)
	{
	    while (const entry_t *e = w.move_preorder())
	    {
		if (paths)
		    dump_path(w);
                string estr = e->to_string();
                for (unsigned int i = 0 ; i < e->get_level() ; i++)
                    fputs("  ", stdout);
                fputs(estr.c_str(), stdout);
                fputs("\n", stdout);
	    }
	}
	else
	{
	    recursive_dump(w.move_next(), w, 0, paths);
	}
	printf("} compile_unit\n");
    }
    printf("\n\n");
}

void
state_t::dump_abbrevs()
{
    printf("Abbrevs\n");
    printf("=======\n");
    vector<compile_unit_t*>::iterator i;
    for (i = compile_units_.begin() ; i != compile_units_.end() ; ++i)
    {
	printf("compile_unit {\n");
	(*i)->dump_abbrevs();
	printf("} compile_unit\n");
    }
    printf("\n\n");
}

compile_unit_offset_tuple_t
state_t::resolve_link_object_reference(const reference_t &ref) const
{
    link_object_t *lo = (link_object_t *)ref.resolver;
    uint64_t off = ref.offset;
    for (compile_unit_t *cu : compile_units_)
    {
        if (lo == cu->get_link_object() &&
            cu->get_start_offset() <= off &&
            off < cu->get_end_offset())
            return compile_unit_offset_tuple_t(cu, off - cu->get_start_offset());
    }
    return compile_unit_offset_tuple_t(0, 0);
}

void
state_t::insert_ranges(const walker_t &w, reference_t funcref)
{
    const entry_t *e = w.get_entry();
    bool has_lo = (e->get_attribute(DW_AT_low_pc) != 0);
    uint64_t lo = e->get_uint64_attribute(DW_AT_low_pc);
    bool has_hi = (e->get_attribute(DW_AT_high_pc) != 0);
    uint64_t hi = e->get_uint64_attribute(DW_AT_high_pc);
    // DW_AT_ranges is a DWARF3 attribute, but g++ generates
    // it (despite only claiming DWARF2 compliance).
    uint64_t ranges = e->get_uint64_attribute(DW_AT_ranges);

    if (has_lo && has_hi)
    {
	/* In DWARF-4, DW_AT_high_pc can be absolute or relative
	 * depending on the form it was encoded in. */
	if (w.get_dwarf_version() == 4 &&
	    e->get_attribute_form(DW_AT_high_pc) != DW_FORM_addr)
	    hi += lo;
	address_index_.insert(lo, hi, funcref);
    }
    else if (ranges)
    {
	reader_t r = w.get_section_contents(DW_sec_ranges);
	r.skip(ranges);
	np::spiegel::addr_t base = 0; // TODO: compile unit base
	np::spiegel::addr_t start, end;
	for (;;)
	{
	    if (!r.read_addr(start) || !r.read_addr(end))
		break;
	    /* (0,0) marks the end of the list */
	    if (!start && !end)
		break;
	    /* (~0,base) marks a new base address */
	    if (start == _NP_MAXADDR)
	    {
		base = end;
		continue;
	    }
	    start += base;
	    end += base;
	    address_index_.insert(start, end, funcref);
	}
    }
    else if (has_lo)
    {
	address_index_.insert(lo, funcref);
    }
}

void
state_t::prepare_address_index()
{
    for (compile_unit_t *cu : compile_units_)
    {
	walker_t w(cu);
	w.set_filter_tag(DW_TAG_subprogram);
	while (const entry_t *e = w.move_preorder())
	{
	    assert(e->get_tag() == DW_TAG_subprogram);
	    insert_ranges(w, w.get_reference());
	}
    }
}

bool
state_t::is_within(np::spiegel::addr_t addr, const walker_t &w,
		   unsigned int &offset) const
{
    const entry_t *e = w.get_entry();
    bool has_lo = (e->get_attribute(DW_AT_low_pc) != 0);
    uint64_t lo = e->get_uint64_attribute(DW_AT_low_pc);
    bool has_hi = (e->get_attribute(DW_AT_high_pc) != 0);
    uint64_t hi = e->get_uint64_attribute(DW_AT_high_pc);
    // DW_AT_ranges is a DWARF3 attribute, but g++ generates
    // it (despite only claiming DWARF2 compliance).
    uint64_t ranges = e->get_uint64_attribute(DW_AT_ranges);
    if (has_lo && has_hi)
    {
	if (addr >= lo && addr <= hi)
	{
	    offset = (addr - lo);
	    return true;
	}
	return false;
    }
    if (ranges)
    {
	reader_t r = w.get_section_contents(DW_sec_ranges);
	r.skip(ranges);
	np::spiegel::addr_t base = 0; // TODO: compile unit base
	np::spiegel::addr_t start, end;
	for (;;)
	{
	    if (!r.read_addr(start) || !r.read_addr(end))
		break;
	    /* (0,0) marks the end of the list */
	    if (!start && !end)
		break;
	    /* (~0,base) marks a new base address */
	    if (start == _NP_MAXADDR)
	    {
		base = end;
		continue;
	    }
	    start += base;
	    end += base;
	    if (addr >= start && addr < end)
	    {
		offset = addr - base;
		return true;
	    }
	}
    }
    if (has_lo)
    {
	if (addr == lo)
	{
	    offset = 0;
	    return true;
	}
	return false;
    }
    return false;
}

// XXX this method name no verb
np::spiegel::addr_t
state_t::instrumentable_address(np::spiegel::addr_t addr) const
{
    vector<link_object_t*>::const_iterator i;
    for (i = link_objects_.begin() ; i != link_objects_.end() ; ++i)
	if ((*i)->is_in_plt(addr))
	    return np::spiegel::platform::follow_plt(addr);
    return addr;
}

// XXX this method name no verb
np::spiegel::addr_t
state_t::recorded_address(np::spiegel::addr_t addr) const
{
    np::util::rangetree<addr_t, link_object_t*>::const_iterator i = link_object_index_.find(addr);
    if (i != link_object_index_.end())
        return i->second->recorded_address(addr);
    return addr;
}

bool
state_t::describe_address(np::spiegel::addr_t addr,
			  reference_t &curef,
			  reference_t &funcref,
			  unsigned int &offset) const
{
    // initialise all the results to the "dunno" case
    curef = reference_t::null;
    funcref = reference_t::null;
    offset = 0;

    if (address_index_.size())
    {
	np::util::rangetree<addr_t, reference_t>::const_iterator i = address_index_.find(addr);
	if (i == address_index_.end())
	    return false;
	offset = addr - i->first.lo;
	funcref = i->second;
	return true;
    }

    for (compile_unit_t *cu : compile_units_)
    {
	walker_t w(cu->make_root_reference());
	const entry_t *e = w.move_next();
	for (; e ; e = w.move_next())
	{
	    if (is_within(addr, w, offset))
	    {
		switch (e->get_tag())
		{
		case DW_TAG_compile_unit:
		    curef = w.get_reference();
		    e = w.move_down();
		    break;
		case DW_TAG_subprogram:
		    if (e->get_attribute(DW_AT_specification))
			e = w.move_to(e->get_reference_attribute(DW_AT_specification));
		    funcref = w.get_reference();
		    return true;
		case DW_TAG_class_type:
		case DW_TAG_structure_type:
		case DW_TAG_union_type:
		case DW_TAG_namespace_type:
		    e = w.move_down();
		    break;
		}
	    }
	}
    }
    return false;
}

string
state_t::get_full_name(reference_t ref)
{
    string full;
    walker_t w(ref);
    const entry_t *e = w.move_next();

    do
    {
	if (e->get_attribute(DW_AT_specification))
	    e = w.move_to(e->get_reference_attribute(DW_AT_specification));
	if (e->get_tag() == DW_TAG_compile_unit)
	    break;
	const char *name = e->get_string_attribute(DW_AT_name);
	if (name)
	{
	    if (full.length())
		full = string("::") + full;
	    full = string(name) + full;
	}
	e = w.move_up();
    } while (e);

    return full;
}

static const char *
get_partial_name(reference_t ref)
{
    walker_t w(ref);
    const entry_t *e = w.move_next();

    if (e->get_attribute(DW_AT_specification))
	e = w.move_to(e->get_reference_attribute(DW_AT_specification));
    return e->get_string_attribute(DW_AT_name);
}

// close namespaces
}; }; };
