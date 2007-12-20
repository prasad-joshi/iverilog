/*
 * Copyright (c) 2000-2007 Stephen Williams (steve@icarus.com)
 *
 *    This source code is free software; you can redistribute it
 *    and/or modify it in source code form under the terms of the GNU
 *    General Public License as published by the Free Software
 *    Foundation; either version 2 of the License, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

# include "config.h"

# include  <iostream>

/*
 * This source file contains all the implementations of the Design
 * class declared in netlist.h.
 */

# include  "netlist.h"
# include  "util.h"
# include  "compiler.h"
# include  "netmisc.h"
# include  <sstream>

Design:: Design()
: errors(0), nodes_(0), procs_(0), lcounter_(0)
{
      procs_idx_ = 0;
      des_precision_ = 0;
      nodes_functor_cur_ = 0;
      nodes_functor_nxt_ = 0;
}

Design::~Design()
{
}

string Design::local_symbol(const string&path)
{
      ostringstream res;
      res << path << "." << "_L" << lcounter_;
      lcounter_ += 1;

      return res.str();
}

void Design::set_precision(int val)
{
      if (val < des_precision_)
	    des_precision_ = val;
}

int Design::get_precision() const
{
      return des_precision_;
}

uint64_t Design::scale_to_precision(uint64_t val,
				    const NetScope*scope) const
{
      int units = scope->time_unit();
      assert( units >= des_precision_ );

      while (units > des_precision_) {
	    units -= 1;
	    val *= 10;
      }

      return val;
}

NetScope* Design::make_root_scope(perm_string root)
{
      NetScope *root_scope_;
      root_scope_ = new NetScope(0, hname_t(root), NetScope::MODULE);
	/* This relies on the fact that the basename return value is
	   permallocated. */
      root_scope_->set_module_name(root_scope_->basename());
      root_scopes_.push_back(root_scope_);
      return root_scope_;
}

NetScope* Design::find_root_scope()
{
      assert(root_scopes_.front());
      return root_scopes_.front();
}

list<NetScope*> Design::find_root_scopes()
{
      return root_scopes_;
}

const list<NetScope*> Design::find_root_scopes() const
{
      return root_scopes_;
}

/*
 * This method locates a scope in the design, given its rooted
 * hierarchical name. Each component of the key is used to scan one
 * more step down the tree until the name runs out or the search
 * fails.
 */
NetScope* Design::find_scope(const std::list<hname_t>&path) const
{
      if (path.empty())
	    return 0;

      for (list<NetScope*>::const_iterator scope = root_scopes_.begin()
		 ; scope != root_scopes_.end(); scope++) {

	    NetScope*cur = *scope;
	    if (path.front() != cur->fullname())
		  continue;

	    std::list<hname_t> tmp = path;
	    tmp.pop_front();

	    while (cur) {
		  if (tmp.empty()) return cur;

		  cur = cur->child( tmp.front() );

		  tmp.pop_front();
	    }

      }

      return 0;
}

/*
 * This is a relative lookup of a scope by name. The starting point is
 * the scope parameter within which I start looking for the scope. If
 * I do not find the scope within the passed scope, start looking in
 * parent scopes until I find it, or I run out of parent scopes.
 */
NetScope* Design::find_scope(NetScope*scope, const std::list<hname_t>&path,
                             NetScope::TYPE type) const
{
      assert(scope);
      if (path.empty())
	    return scope;

      for ( ; scope ;  scope = scope->parent()) {

	    std::list<hname_t> tmp = path;

	    NetScope*cur = scope;
	    do {
		  hname_t key = tmp.front();
		    /* If we are looking for a module or we are not
		     * looking at the last path component check for
		     * a name match (second line). */
		  if (cur->type() == NetScope::MODULE
		      && (type == NetScope::MODULE || tmp.size() > 1)
		      && cur->module_name()==key.peek_name()) {

			  /* Up references may match module name */

		  } else {
			cur = cur->child( key );
			if (cur == 0) break;
		  }
		  tmp.pop_front();
	    } while (!tmp.empty());

	    if (cur) return cur;
      }

	// Last chance. Look for the name starting at the root.
      return find_scope(path);
}

/*
 * This method runs through the scope, noticing the defparam
 * statements that were collected during the elaborate_scope pass and
 * applying them to the target parameters. The implementation actually
 * works by using a specialized method from the NetScope class that
 * does all the work for me.
 */
void Design::run_defparams()
{
      for (list<NetScope*>::const_iterator scope = root_scopes_.begin();
	   scope != root_scopes_.end(); scope++)
	    (*scope)->run_defparams(this);
}

void NetScope::run_defparams(Design*des)
{
      { NetScope*cur = sub_;
        while (cur) {
	      cur->run_defparams(des);
	      cur = cur->sib_;
	}
      }

      map<pform_name_t,NetExpr*>::const_iterator pp;
      for (pp = defparams.begin() ;  pp != defparams.end() ;  pp ++ ) {
	    NetExpr*val = (*pp).second;
	    pform_name_t path = (*pp).first;

	    perm_string perm_name = peek_tail_name(path);
	    path.pop_back();

	    list<hname_t> eval_path = eval_scope_path(des, this, path);

	      /* If there is no path on the name, then the targ_scope
		 is the current scope. */
	    NetScope*targ_scope = des->find_scope(this, eval_path);
	    if (targ_scope == 0) {
		  cerr << val->get_fileline() << ": warning: scope of " <<
			path << "." << perm_name << " not found." << endl;
		  continue;
	    }

	    bool flag = targ_scope->replace_parameter(perm_name, val);
	    if (! flag) {
		  cerr << val->get_fileline() << ": warning: parameter "
		       << perm_name << " not found in "
		       << scope_path(targ_scope) << "." << endl;
	    }

      }
}

void Design::evaluate_parameters()
{
      for (list<NetScope*>::const_iterator scope = root_scopes_.begin();
	   scope != root_scopes_.end(); scope++)
	    (*scope)->evaluate_parameters(this);
}

void NetScope::evaluate_parameters(Design*des)
{
      NetScope*cur = sub_;
      while (cur) {
	    cur->evaluate_parameters(des);
	    cur = cur->sib_;
      }


	// Evaluate the parameter values. The parameter expressions
	// have already been elaborated and replaced by the scope
	// scanning code. Now the parameter expression can be fully
	// evaluated, or it cannot be evaluated at all.

      typedef map<perm_string,param_expr_t>::iterator mparm_it_t;

      for (mparm_it_t cur = parameters.begin()
		 ; cur != parameters.end() ;  cur ++) {

	    long msb = 0;
	    long lsb = 0;
	    bool range_flag = false;
	    NetExpr*expr;

	      /* Evaluate the msb expression, if it is present. */
	    expr = (*cur).second.msb;

	    if (expr) {

		  NetEConst*tmp = dynamic_cast<NetEConst*>(expr);

		  if (! tmp) {

			NetExpr*nexpr = expr->eval_tree();
			if (nexpr == 0) {
			      cerr << (*cur).second.expr->get_fileline()
				   << ": internal error: "
				   << "unable to evaluate msb expression "
				   << "for parameter " << (*cur).first << ": "
				   << *expr << endl;
			      des->errors += 1;
			      continue;
			}

			assert(nexpr);
			delete expr;
			(*cur).second.msb = nexpr;

			tmp = dynamic_cast<NetEConst*>(nexpr);
		  }

		  assert(tmp);
		  msb = tmp->value().as_long();
		  range_flag = true;
	    }

	      /* Evaluate the lsb expression, if it is present. */
	    expr = (*cur).second.lsb;
	    if (expr) {

		  NetEConst*tmp = dynamic_cast<NetEConst*>(expr);

		  if (! tmp) {

			NetExpr*nexpr = expr->eval_tree();
			if (nexpr == 0) {
			      cerr << (*cur).second.expr->get_fileline()
				   << ": internal error: "
				   << "unable to evaluate lsb expression "
				   << "for parameter " << (*cur).first << ": "
				   << *expr << endl;
			      des->errors += 1;
			      continue;
			}

			assert(nexpr);
			delete expr;
			(*cur).second.lsb = nexpr;

			tmp = dynamic_cast<NetEConst*>(nexpr);
		  }

		  assert(tmp);
		  lsb = tmp->value().as_long();

		  assert(range_flag);
	    }


	      /* Evaluate the parameter expression, if necessary. */
	    expr = (*cur).second.expr;
	    assert(expr);

	    switch (expr->expr_type()) {
		case IVL_VT_REAL:
		  if (! dynamic_cast<const NetECReal*>(expr)) {

			NetExpr*nexpr = expr->eval_tree();
			if (nexpr == 0) {
			      cerr << (*cur).second.expr->get_fileline()
				   << ": internal error: "
				   << "unable to evaluate real parameter value: "
				   << *expr << endl;
			      des->errors += 1;
			      continue;
			}

			assert(nexpr);
			delete expr;
			(*cur).second.expr = nexpr;
		  }
		  break;

		case IVL_VT_LOGIC:
		case IVL_VT_BOOL:
		  if (! dynamic_cast<const NetEConst*>(expr)) {

			  // Try to evaluate the expression.
			NetExpr*nexpr = expr->eval_tree();
			if (nexpr == 0) {
			      cerr << (*cur).second.expr->get_fileline()
				   << ": internal error: "
				   << "unable to evaluate parameter "
				   << (*cur).first
				   << " value: " <<
				    *expr << endl;
			      des->errors += 1;
			      continue;
			}

			  // The evaluate worked, replace the old
			  // expression with this constant value.
			assert(nexpr);
			delete expr;
			(*cur).second.expr = nexpr;

			  // Set the signedness flag.
			(*cur).second.expr
			      ->cast_signed( (*cur).second.signed_flag );
		  }
		  break;

		default:
		  cerr << (*cur).second.expr->get_fileline()
		       << ": internal error: "
		       << "unhandled expression type?" << endl;
		  des->errors += 1;
		  continue;
	    }

	      /* If the parameter has range information, then make
		 sure the value is set right. */
	    if (range_flag) {
		  unsigned long wid = (msb >= lsb)? msb - lsb : lsb - msb;
		  wid += 1;

		  NetEConst*val = dynamic_cast<NetEConst*>((*cur).second.expr);
		  assert(val);

		  verinum value = val->value();

		  if (! (value.has_len()
			 && (value.len() == wid)
			 && (value.has_sign() == (*cur).second.signed_flag))) {

			verinum tmp (value, wid);
			tmp.has_sign ( (*cur).second.signed_flag );
			delete val;
			val = new NetEConst(tmp);
			(*cur).second.expr = val;
		  }
	    }
      }

}

const char* Design::get_flag(const string&key) const
{
      map<string,const char*>::const_iterator tmp = flags_.find(key);
      if (tmp == flags_.end())
	    return "";
      else
	    return (*tmp).second;
}

/*
 * This method looks for a signal (reg, wire, whatever) starting at
 * the specified scope. If the name is hierarchical, it is split into
 * scope and name and the scope used to find the proper starting point
 * for the real search.
 *
 * It is the job of this function to properly implement Verilog scope
 * rules as signals are concerned.
 */
NetNet* Design::find_signal(NetScope*scope, pform_name_t path)
{
      assert(scope);

      perm_string key = peek_tail_name(path);
      path.pop_back();
      if (! path.empty()) {
	    list<hname_t> eval_path = eval_scope_path(this, scope, path);
	    scope = find_scope(scope, eval_path);
      }

      while (scope) {
	    if (NetNet*net = scope->find_signal(key))
		  return net;

	    if (scope->type() == NetScope::MODULE)
		  break;

	    scope = scope->parent();
      }

      return 0;
}

NetFuncDef* Design::find_function(NetScope*scope, const pform_name_t&name)
{
      assert(scope);

      std::list<hname_t> eval_path = eval_scope_path(this, scope, name);
      NetScope*func = find_scope(scope, eval_path, NetScope::FUNC);
      if (func && (func->type() == NetScope::FUNC))
	    return func->func_def();

      return 0;
}

NetScope* Design::find_task(NetScope*scope, const pform_name_t&name)
{
      std::list<hname_t> eval_path = eval_scope_path(this, scope, name);
      NetScope*task = find_scope(scope, eval_path, NetScope::TASK);
      if (task && (task->type() == NetScope::TASK))
	    return task;

      return 0;
}

void Design::add_node(NetNode*net)
{
      assert(net->design_ == 0);
      if (nodes_ == 0) {
	    net->node_next_ = net;
	    net->node_prev_ = net;
      } else {
	    net->node_next_ = nodes_->node_next_;
	    net->node_prev_ = nodes_;
	    net->node_next_->node_prev_ = net;
	    net->node_prev_->node_next_ = net;
      }
      nodes_ = net;
      net->design_ = this;
}

void Design::del_node(NetNode*net)
{
      assert(net->design_ == this);
      assert(net != 0);

	/* Interact with the Design::functor method by manipulating the
	   cur and nxt pointers that it is using. */
      if (net == nodes_functor_nxt_)
	    nodes_functor_nxt_ = nodes_functor_nxt_->node_next_;
      if (net == nodes_functor_nxt_)
	    nodes_functor_nxt_ = 0;

      if (net == nodes_functor_cur_)
	    nodes_functor_cur_ = 0;

	/* Now perform the actual delete. */
      if (nodes_ == net)
	    nodes_ = net->node_prev_;

      if (nodes_ == net) {
	    nodes_ = 0;
      } else {
	    net->node_next_->node_prev_ = net->node_prev_;
	    net->node_prev_->node_next_ = net->node_next_;
      }

      net->design_ = 0;
}

void Design::add_process(NetProcTop*pro)
{
      pro->next_ = procs_;
      procs_ = pro;
}

void Design::delete_process(NetProcTop*top)
{
      assert(top);
      if (procs_ == top) {
	    procs_ = top->next_;

      } else {
	    NetProcTop*cur = procs_;
	    while (cur->next_ != top) {
		  assert(cur->next_);
		  cur = cur->next_;
	    }

	    cur->next_ = top->next_;
      }

      if (procs_idx_ == top)
	    procs_idx_ = top->next_;

      delete top;
}

