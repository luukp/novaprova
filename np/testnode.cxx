#include "np/testnode.hxx"
#include "np/redirect.hxx"
#include "spiegel/tok.hxx"

namespace np {
using namespace std;

testnode_t::testnode_t(const char *name)
 :  name_(name ? xstrdup(name) : 0)
{
}

testnode_t::~testnode_t()
{
    while (children_)
    {
	testnode_t *child = children_;
	children_ = child->next_;
	delete child;
    }

    xfree(name_);
}

testnode_t *
testnode_t::make_path(string name)
{
    testnode_t *parent = this;
    const char *part;
    testnode_t *child;
    testnode_t **tailp;
    spiegel::tok_t tok(name.c_str(), "/");

    while ((part = tok.next()))
    {
	for (child = parent->children_, tailp = &parent->children_ ;
	     child ;
	     tailp = &child->next_, child = child->next_)
	{
	    if (!strcmp(child->name_, part))
		break;
	}
	if (!child)
	{
	    child = new testnode_t(part);
	    *tailp = child;
	    child->parent_ = parent;
	}

	parent = child;
    }
    return child;
}

void
testnode_t::set_function(functype_t ft, spiegel::function_t *func)
{
    if (funcs_[ft])
	fprintf(stderr, "np: WARNING: duplicate %s functions: "
			"%s:%s and %s:%s\n",
			as_string(ft),
			funcs_[ft]->get_compile_unit()->get_absolute_path().c_str(),
			funcs_[ft]->get_name().c_str(),
			func->get_compile_unit()->get_absolute_path().c_str(),
			func->get_name().c_str());
    else
	funcs_[ft] = func;
}

void
testnode_t::add_mock(spiegel::function_t *target, spiegel::function_t *mock)
{
    add_mock(target->get_address(), mock->get_address());
}

void
testnode_t::add_mock(spiegel::addr_t target, spiegel::addr_t mock)
{
    intercepts_.push_back(new redirect_t(target, mock));
}

static void
indent(int level)
{
    for ( ; level ; level--)
	fputs("    ", stderr);
}

void
testnode_t::dump(int level) const
{
    indent(level);
    if (name_)
    {
	fprintf(stderr, "%s (full %s)\n",
		name_, get_fullname().c_str());
    }

    for (int type = 0 ; type < FT_NUM_SINGULAR ; type++)
    {
	if (funcs_[type])
	{
	    indent(level);
	    fprintf(stderr, "  %s=%s:%s\n",
			    as_string((functype_t)type),
			    funcs_[type]->get_compile_unit()->get_absolute_path().c_str(),
			    funcs_[type]->get_name().c_str());
	}
    }

    for (testnode_t *child = children_ ; child ; child = child->next_)
	child->dump(level+1);
}

string
testnode_t::get_fullname() const
{
    string full = "";

    for (const testnode_t *a = this ; a ; a = a->parent_)
    {
	if (!a->name_)
	    continue;
	if (a != this)
	    full = "." + full;
	full = a->name_ + full;
    }

    return full;
}

testnode_t *
testnode_t::detach_common()
{
    testnode_t *tn;

    for (tn = this ;
         !tn->intercepts_.size() &&
         !tn->parameters_.size() &&
	 tn->children_ && !tn->children_->next_ ;
	 tn = tn->children_)
	;
    /* tn now points at the highest node with more than 1 child */

    tn->parent_->children_ = 0;
    assert(!tn->next_);
    tn->parent_ = 0;

    return tn;
}

list<spiegel::function_t*>
testnode_t::get_fixtures(functype_t type) const
{
    list<spiegel::function_t*> fixtures;

    /* Run FT_BEFORE from outermost in, and FT_AFTER
     * from innermost out */
    for (const testnode_t *a = this ; a ; a = a->parent_)
    {
	if (!a->funcs_[type])
	    continue;
	if (type == FT_BEFORE)
	    fixtures.push_front(a->funcs_[type]);
	else
	    fixtures.push_back(a->funcs_[type]);
    }
    return fixtures;
}

testnode_t *
testnode_t::find(const char *nm)
{
    if (name_ && get_fullname() == nm)
	return this;

    for (testnode_t *child = children_ ; child ; child = child->next_)
    {
	testnode_t *found = child->find(nm);
	if (found)
	    return found;
    }

    return 0;
}

void
testnode_t::pre_run() const
{
    /* Install intercepts from innermost out */
    for (const testnode_t *a = this ; a ; a = a->parent_)
    {
	vector<spiegel::intercept_t*>::const_iterator itr;
	for (itr = a->intercepts_.begin() ; itr != a->intercepts_.end() ; ++itr)
	    (*itr)->install();
    }
}

void
testnode_t::post_run() const
{
    /*
     * Uninstall intercepts from innermost out.  Probably we should do
     * this from outermost in to do it in the opposite order to
     * pre_fixture(), but the order doesn't really matter.  The order
     * *does* matter for installation, as the install order will be the
     * execution order should any intercepts double up.
     */
    for (const testnode_t *a = this ; a ; a = a->parent_)
    {
	vector<spiegel::intercept_t*>::const_iterator itr;
	for (itr = a->intercepts_.begin() ; itr != a->intercepts_.end() ; ++itr)
	    (*itr)->uninstall();
    }
}

testnode_t::preorder_iterator &
testnode_t::preorder_iterator::operator++()
{
    if (node_->children_)
	node_ = node_->children_;
    else if (node_ != base_ && node_->next_)
	node_ = node_->next_;
    else if (node_->parent_ != base_ && node_->parent_)
	node_ = node_->parent_->next_;
    else
	node_ = 0;
    return *this;
}

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

testnode_t::parameter_t::parameter_t(const char *n, char **v, const char *vals)
 :  name_(xstrdup(n)),
    variable_(v)
{
    /* TODO: need a split() function */
    spiegel::tok_t valtok(vals, ", \t");
    const char *val;
    while ((val = valtok.next()))
	values_.push_back(xstrdup(val));
}

testnode_t::parameter_t::~parameter_t()
{
    xfree(name_);
    vector<char*>::iterator i;
    for (i = values_.begin() ; i != values_.end() ; ++i)
	free(*i);
}

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

void testnode_t::assignment_t::apply() const
{
    free(*param_->variable_);
    *param_->variable_ = xstrdup(param_->values_[idx_]);
}

void testnode_t::assignment_t::unapply() const
{
    free(*param_->variable_);
    *param_->variable_ = 0;
}

string testnode_t::assignment_t::as_string() const
{
    return string(param_->name_) + "=" + param_->values_[idx_];
}

// Bump the assignment vector to the next next value in order, clearing
// the vector and returning true if we run off the end of the values.
bool bump(std::vector<testnode_t::assignment_t> &a)
{
    vector<testnode_t::assignment_t>::iterator i;
    for (i = a.begin() ; i != a.end() ; ++i)
    {
	if (++i->idx_ < i->param_->values_.size())
	    return false;
	i->idx_ = 0;
    }
    a.clear();
    return true;
}

int operator==(const std::vector<testnode_t::assignment_t> &a,
	       const std::vector<testnode_t::assignment_t> &b)
{
    if (a.size() != b.size())
	return 0;

    std::vector<testnode_t::assignment_t>::const_iterator ia;
    std::vector<testnode_t::assignment_t>::const_iterator ib;
    for (ia = a.begin(), ib = b.begin() ; ia != a.end() ; ++ia, ++ib)
	if (ia->idx_ != ib->idx_)
	    return 0;
    return 1;
}

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

void
testnode_t::add_parameter(const char *name, char **var, const char *vals)
{
    parameters_.push_back(new parameter_t(name, var, vals));
}

vector<testnode_t::assignment_t>
testnode_t::create_assignments() const
{
    vector<assignment_t> assigns;

    for (const testnode_t *a = this ; a ; a = a->parent_)
    {
	vector<parameter_t*>::const_iterator i;
	for (i = a->parameters_.begin() ; i != a->parameters_.end() ; ++i)
	    assigns.push_back(assignment_t(*i));
    }
    return assigns;
}

// close the namespace
};