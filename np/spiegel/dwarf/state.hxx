#ifndef __np_spiegel_dwarf_state_hxx__
#define __np_spiegel_dwarf_state_hxx__ 1

#include "np/spiegel/common.hxx"
#include "np/spiegel/spiegel.hxx"
#include "np/util/rangetree.hxx"
#include "section.hxx"
#include "reference.hxx"
#include "enumerations.hxx"

namespace np {
namespace spiegel {
namespace dwarf {

class walker_t;
class compile_unit_t;

class state_t
{
public:
    state_t();
    ~state_t();

    bool add_self();
    bool add_executable(const char *filename);

    void dump_structs();
    void dump_functions();
    void dump_variables();
    void dump_info(bool preorder, bool paths);
    void dump_abbrevs();

    /* Call this function to prepare an index which will speed up
     * all later calls to describe_address().  Or don't call it,
     * if you don't need to build stack traces. */
    void prepare_address_index();

    bool describe_address(np::spiegel::addr_t addr,
			  reference_t &curef,
			  unsigned int &lineno,
			  reference_t &funcref,
			  unsigned int &offset) const;
    std::string get_full_name(reference_t ref);

    // state_t is a Singleton
    static state_t *instance() { return instance_; }

    const std::vector<compile_unit_t*> &get_compile_units() const { return compile_units_; }
    compile_unit_t *get_compile_unit(reference_t ref) const
    {
	return compile_units_[ref.cu];
    }

private:
    struct linkobj_t
    {
	linkobj_t(const char *n, uint32_t idx)
	 :  filename_(np::util::xstrdup(n)),
	    index_(idx)
	{
	    memset(sections_, 0, sizeof(sections_));
	}
	~linkobj_t()
	{
	    unmap_sections();
	    free(filename_);
	}

	char *filename_;
	uint32_t index_;
	section_t sections_[DW_sec_num];
	std::vector<section_t> mappings_;
	std::vector<np::spiegel::mapping_t> system_mappings_;

	bool map_sections();
	void unmap_sections();
    };

    linkobj_t *get_linkobj(const char *filename);
    bool read_linkobjs();
    bool read_compile_units(linkobj_t *);
    void insert_ranges(const walker_t &w, reference_t funcref);
    bool is_within(np::spiegel::addr_t addr, const walker_t &w,
		   unsigned int &offset) const;

    static state_t *instance_;

    std::vector<linkobj_t*> linkobjs_;
    std::vector<compile_unit_t*> compile_units_;
    np::util::rangetree<addr_t, reference_t> address_index_;

    friend class walker_t;
    friend class compile_unit_t;
};


// close namespaces
}; }; };

#endif // __np_spiegel_dwarf_state_hxx__
