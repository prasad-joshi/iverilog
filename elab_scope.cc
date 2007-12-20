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

# include  "config.h"
# include  "compiler.h"
# include  "netmisc.h"
# include  <iostream>
# include  <stdio.h>

/*
 * Elaboration happens in two passes, generally. The first scans the
 * pform to generate the NetScope tree and attach it to the Design
 * object. The methods in this source file implement the elaboration
 * of the scopes.
 */

# include  "Module.h"
# include  "PEvent.h"
# include  "PExpr.h"
# include  "PGate.h"
# include  "PGenerate.h"
# include  "PTask.h"
# include  "PWire.h"
# include  "Statement.h"
# include  "netlist.h"
# include  "util.h"
# include  <typeinfo>
# include  <assert.h>

bool Module::elaborate_scope(Design*des, NetScope*scope,
			     const replace_t&replacements) const
{
      if (debug_scopes) {
	    cerr << get_fileline() << ": debug: Elaborate scope "
		 << scope_path(scope) << "." << endl;
      }

	// Generate all the parameters that this instance of this
	// module introduces to the design. This loop elaborates the
	// parameters, but doesn't evaluate references to
	// parameters. This scan practically locates all the
	// parameters and puts them in the parameter table in the
	// design.

	// No expressions are evaluated, yet. For now, leave them in
	// the pform and just place a NetEParam placeholder in the
	// place of the elaborated expression.

      typedef map<perm_string,param_expr_t>::const_iterator mparm_it_t;
      typedef map<pform_name_t,PExpr*>::const_iterator pform_parm_it_t;


	// This loop scans the parameters in the module, and creates
	// stub parameter entries in the scope for the parameter name.

      for (mparm_it_t cur = parameters.begin()
		 ; cur != parameters.end() ;  cur ++) {

	    NetEParam*tmp = new NetEParam;
	    tmp->set_line(*((*cur).second.expr));
	    tmp->cast_signed( (*cur).second.signed_flag );

	    scope->set_parameter((*cur).first, tmp, 0, 0, false);
      }

      for (mparm_it_t cur = localparams.begin()
		 ; cur != localparams.end() ;  cur ++) {

	    NetEParam*tmp = new NetEParam;
	    tmp->set_line(*((*cur).second.expr));
	    if ((*cur).second.msb)
		  tmp->cast_signed( (*cur).second.signed_flag );

	    scope->set_parameter((*cur).first, tmp, 0, 0, false);
      }


	// Now scan the parameters again, this time elaborating them
	// for use as parameter values. This is after the previous
	// scan so that local parameter names can be used in the
	// r-value expressions.

      for (mparm_it_t cur = parameters.begin()
		 ; cur != parameters.end() ;  cur ++) {

	    PExpr*ex = (*cur).second.expr;
	    assert(ex);

	    NetExpr*val = ex->elaborate_pexpr(des, scope);
	    NetExpr*msb = 0;
	    NetExpr*lsb = 0;
	    bool signed_flag = (*cur).second.signed_flag;

	      /* If the parameter declaration includes msb and lsb,
		 then use them to calculate a width for the
		 result. Then make sure the constant expression of the
		 parameter value is coerced to have the correct
		 and defined width. */
	    if ((*cur).second.msb) {
		  msb = (*cur).second.msb ->elaborate_pexpr(des, scope);
		  assert(msb);
		  lsb = (*cur).second.lsb ->elaborate_pexpr(des, scope);
	    }

	    if (signed_flag) {
		    /* If explicitly signed, then say so. */
		  val->cast_signed(true);
	    } else if ((*cur).second.msb) {
		    /* If there is a range, then the signedness comes
		       from the type and not the expression. */
		  val->cast_signed(signed_flag);
	    } else {
		    /* otherwise, let the expression describe
		       itself. */
		  signed_flag = val->has_sign();
	    }

	    val = scope->set_parameter((*cur).first, val,
				       msb, lsb, signed_flag);
	    assert(val);
	    delete val;
      }

	/* run parameter replacements that were collected from the
	   containing scope and meant for me. */
      for (replace_t::const_iterator cur = replacements.begin()
		 ; cur != replacements.end() ;  cur ++) {

	    NetExpr*val = (*cur).second;
	    if (val == 0) {
		  cerr << get_fileline() << ": internal error: "
		       << "Missing expression in parameter replacement for "
		       << (*cur).first;
	    }
	    assert(val);
	    if (debug_scopes) {
		  cerr << get_fileline() << ": debug: "
		       << "Replace " << (*cur).first
		       << " with expression " << *val
		       << " from " << val->get_fileline() << "." << endl;
	    }
	    bool flag = scope->replace_parameter((*cur).first, val);
	    if (! flag) {
		  cerr << val->get_fileline() << ": warning: parameter "
		       << (*cur).first << " not found in "
		       << scope_path(scope) << "." << endl;
	    }
      }

      for (mparm_it_t cur = localparams.begin()
		 ; cur != localparams.end() ;  cur ++) {

	    PExpr*ex = (*cur).second.expr;
	    assert(ex);

	    NetExpr*val = ex->elaborate_pexpr(des, scope);
	    NetExpr*msb = 0;
	    NetExpr*lsb = 0;
	    bool signed_flag = false;

	      /* If the parameter declaration includes msb and lsb,
		 then use them to calculate a width for the
		 result. Then make sure the constant expression of the
		 parameter value is coerced to have the correct
		 and defined width. */
	    if ((*cur).second.msb) {
		  msb = (*cur).second.msb ->elaborate_pexpr(des, scope);
		  assert(msb);
		  lsb = (*cur).second.lsb ->elaborate_pexpr(des, scope);
		  signed_flag = (*cur).second.signed_flag;
	    }

	    val->cast_signed(signed_flag);
	    val = scope->set_parameter((*cur).first, val,
				       msb, lsb, signed_flag);
	    assert(val);
	    delete val;
      }

	// Run through the defparams for this module, elaborate the
	// expressions in this context and save the result is a table
	// for later final override.

	// It is OK to elaborate the expressions of the defparam here
	// because Verilog requires that the expressions only use
	// local parameter names. It is *not* OK to do the override
	// here because the parameter receiving the assignment may be
	// in a scope not discovered by this pass.

      for (pform_parm_it_t cur = defparms.begin()
		 ; cur != defparms.end() ;  cur ++ ) {

	    PExpr*ex = (*cur).second;
	    assert(ex);

	    NetExpr*val = ex->elaborate_pexpr(des, scope);
	    if (val == 0) continue;
	    scope->defparams[(*cur).first] = val;
      }

	// Evaluate the attributes. Evaluate them in the scope of the
	// module that the attribute is attached to. Is this correct?
      unsigned nattr;
      attrib_list_t*attr = evaluate_attributes(attributes, nattr, des, scope);

      for (unsigned idx = 0 ;  idx < nattr ;  idx += 1)
	    scope->attribute(attr[idx].key, attr[idx].val);

      delete[]attr;

	// Generate schemes can create new scopes in the form of
	// generated code. Scan the generate schemes, and *generate*
	// new scopes, which is slightly different from simple
	// elaboration.

      typedef list<PGenerate*>::const_iterator generate_it_t;
      for (generate_it_t cur = generate_schemes.begin()
		 ; cur != generate_schemes.end() ; cur ++ ) {
	    (*cur) -> generate_scope(des, scope);
      }


	// Tasks introduce new scopes, so scan the tasks in this
	// module. Create a scope for the task and pass that to the
	// elaborate_scope method of the PTask for detailed
	// processing.

      typedef map<perm_string,PTask*>::const_iterator tasks_it_t;

      for (tasks_it_t cur = tasks_.begin()
		 ; cur != tasks_.end() ;  cur ++ ) {

	    hname_t use_name( (*cur).first );
	    if (scope->child(use_name)) {
		  cerr << get_fileline() << ": error: task/scope name "
		       << use_name << " already used in this context."
		       << endl;
		  des->errors += 1;
		  continue;
	    }
	    NetScope*task_scope = new NetScope(scope, use_name,
					       NetScope::TASK);
	    (*cur).second->elaborate_scope(des, task_scope);
      }


	// Functions are very similar to tasks, at least from the
	// perspective of scopes. So handle them exactly the same
	// way.

      typedef map<perm_string,PFunction*>::const_iterator funcs_it_t;

      for (funcs_it_t cur = funcs_.begin()
		 ; cur != funcs_.end() ;  cur ++ ) {

	    hname_t use_name( (*cur).first );
	    if (scope->child(use_name)) {
		  cerr << get_fileline() << ": error: function/scope name "
		       << use_name << " already used in this context."
		       << endl;
		  des->errors += 1;
		  continue;
	    }
	    NetScope*func_scope = new NetScope(scope, use_name,
					       NetScope::FUNC);
	    (*cur).second->elaborate_scope(des, func_scope);
      }


	// Gates include modules, which might introduce new scopes, so
	// scan all of them to create those scopes.

      typedef list<PGate*>::const_iterator gates_it_t;
      for (gates_it_t cur = gates_.begin()
		 ; cur != gates_.end() ;  cur ++ ) {

	    (*cur) -> elaborate_scope(des, scope);
      }


	// initial and always blocks may contain begin-end and
	// fork-join blocks that can introduce scopes. Therefore, I
	// get to scan processes here.

      typedef list<PProcess*>::const_iterator proc_it_t;

      for (proc_it_t cur = behaviors_.begin()
		 ; cur != behaviors_.end() ;  cur ++ ) {

	    (*cur) -> statement() -> elaborate_scope(des, scope);
      }

	// Scan through all the named events in this scope. We do not
	// need anything more than the current scope to do this
	// elaboration, so do it now. This allows for normal
	// elaboration to reference these events.

      for (map<perm_string,PEvent*>::const_iterator et = events.begin()
		 ; et != events.end() ;  et ++ ) {

	    (*et).second->elaborate_scope(des, scope);
      }

      return des->errors == 0;
}

bool PGenerate::generate_scope(Design*des, NetScope*container)
{
      switch (scheme_type) {
	  case GS_LOOP:
	    return generate_scope_loop_(des, container);

	  case GS_CONDIT:
	    return generate_scope_condit_(des, container, false);

	  case GS_ELSE:
	    return generate_scope_condit_(des, container, true);

	  default:
	    cerr << get_fileline() << ": sorry: Generate of this sort"
		 << " is not supported yet!" << endl;
	    return false;
      }
}

/*
 * This is the elaborate scope method for a generate loop.
 */
bool PGenerate::generate_scope_loop_(Design*des, NetScope*container)
{
	// We're going to need a genvar...
      int genvar;

	// The initial value for the genvar does not need (nor can it
	// use) the genvar itself, so we can evaluate this expression
	// the same way any other paramter value is evaluated.
      NetExpr*init_ex = elab_and_eval(des, container, loop_init, -1);
      NetEConst*init = dynamic_cast<NetEConst*> (init_ex);
      if (init == 0) {
	    cerr << get_fileline() << ": error: Cannot evaluate genvar"
		 << " init expression: " << *loop_init << endl;
	    des->errors += 1;
	    return false;
      }

      genvar = init->value().as_long();
      delete init_ex;

      if (debug_elaborate)
	    cerr << get_fileline() << ": debug: genvar init = " << genvar << endl;

      container->genvar_tmp = loop_index;
      container->genvar_tmp_val = genvar;
      NetExpr*test_ex = elab_and_eval(des, container, loop_test, -1);
      NetEConst*test = dynamic_cast<NetEConst*>(test_ex);
      assert(test);
      while (test->value().as_long()) {

	      // The actual name of the scope includes the genvar so
	      // that each instance has a unique name in the
	      // container. The format of using [] is part of the
	      // Verilog standard.
	    hname_t use_name (scope_name, genvar);
	    if (container->child(use_name)) {
		  cerr << get_fileline() << ": error: block/scope name "
		       << use_name << " already used in this context."
		       << endl;
		  des->errors += 1;
		  return false;
	    }
	    if (debug_elaborate)
		  cerr << get_fileline() << ": debug: "
		       << "Create generated scope " << use_name << endl;

	    NetScope*scope = new NetScope(container, use_name,
					  NetScope::GENBLOCK);

	      // Set in the scope a localparam for the value of the
	      // genvar within this instance of the generate
	      // block. Code within this scope thus has access to the
	      // genvar as a constant.
	    {
		  verinum genvar_verinum(genvar);
		  genvar_verinum.has_sign(true);
		  NetEConstParam*gp = new NetEConstParam(scope,
							 loop_index,
							 genvar_verinum);
		  scope->set_localparam(loop_index, gp);

		  if (debug_elaborate)
			cerr << get_fileline() << ": debug: "
			     << "Create implicit localparam "
			     << loop_index << " = " << genvar_verinum << endl;
	    }

	    elaborate_subscope_(des, scope);

	      // Calculate the step for the loop variable.
	    NetExpr*step_ex = elab_and_eval(des, container, loop_step, -1);
	    NetEConst*step = dynamic_cast<NetEConst*>(step_ex);
	    assert(step);
	    if (debug_elaborate)
		  cerr << get_fileline() << ": debug: genvar step from "
		       << genvar << " to " << step->value().as_long() << endl;

	    genvar = step->value().as_long();
	    container->genvar_tmp_val = genvar;
	    delete step;
	    delete test_ex;
	    test_ex = elab_and_eval(des, container, loop_test, -1);
	    test = dynamic_cast<NetEConst*>(test_ex);
	    assert(test);
      }

	// Clear the genvar_tmp field in the scope to reflect that the
	// genvar is no longer value for evaluating expressions.
      container->genvar_tmp = perm_string();

      return true;
}

bool PGenerate::generate_scope_condit_(Design*des, NetScope*container, bool else_flag)
{
      NetExpr*test_ex = elab_and_eval(des, container, loop_test, -1);
      NetEConst*test = dynamic_cast<NetEConst*> (test_ex);
      assert(test);

	// If the condition evaluates as false, then do not create the
	// scope.
      if (test->value().as_long() == 0 && !else_flag
	  || test->value().as_long() != 0 && else_flag) {
	    if (debug_elaborate)
		  cerr << get_fileline() << ": debug: Generate condition "
		       << (else_flag? "(else)" : "(if)")
		       << " value=" << test->value() << ": skip generation"
		       << endl;
	    delete test_ex;
	    return true;
      }

      hname_t use_name (scope_name);
      if (container->child(use_name)) {
	    cerr << get_fileline() << ": error: block/scope name "
		 << scope_name << " already used in this context."
		 << endl;
	    des->errors += 1;
	    return false;
      }
      if (debug_elaborate)
	    cerr << get_fileline() << ": debug: Generate condition "
		 << (else_flag? "(else)" : "(if)")
		 << " value=" << test->value() << ": Generate scope="
		 << use_name << endl;

      NetScope*scope = new NetScope(container, use_name,
				    NetScope::GENBLOCK);

      elaborate_subscope_(des, scope);

      return true;
}

void PGenerate::elaborate_subscope_(Design*des, NetScope*scope)
{
	// Scan the generated scope for nested generate schemes,
	// and *generate* new scopes, which is slightly different
	// from simple elaboration.

      typedef list<PGenerate*>::const_iterator generate_it_t;
      for (generate_it_t cur = generates.begin()
		 ; cur != generates.end() ; cur ++ ) {
	    (*cur) -> generate_scope(des, scope);
      }

	// Scan the generated scope for gates that may create
	// their own scopes.
      typedef list<PGate*>::const_iterator pgate_list_it_t;
      for (pgate_list_it_t cur = gates.begin()
		 ; cur != gates.end() ;  cur ++) {
	    (*cur) ->elaborate_scope(des, scope);
      }

	// Save the scope that we created, for future use.
      scope_list_.push_back(scope);
}

void PGModule::elaborate_scope_mod_(Design*des, Module*mod, NetScope*sc) const
{
      if (get_name() == "") {
	    cerr << get_fileline() << ": error: Instantiation of module "
		 << mod->mod_name() << " requires an instance name." << endl;
	    des->errors += 1;
	    return;
      }

	// Missing module instance names have already been rejected.
      assert(get_name() != "");

	// Check for duplicate scopes. Simply look up the scope I'm
	// about to create, and if I find it then somebody beat me to
	// it.

      if (sc->child(hname_t(get_name()))) {
	    cerr << get_fileline() << ": error: Instance/Scope name " <<
		  get_name() << " already used in this context." <<
		  endl;
	    des->errors += 1;
	    return;
      }

	// check for recursive instantiation by scanning the current
	// scope and its parents. Look for a module instantiation of
	// the same module, but farther up in the scope.

      for (NetScope*scn = sc ;  scn ;  scn = scn->parent()) {
	    if (scn->type() != NetScope::MODULE)
		  continue;

	    if (strcmp(mod->mod_name(), scn->module_name()) != 0)
		  continue;

	    cerr << get_fileline() << ": error: You cannot instantiate "
		 << "module " << mod->mod_name() << " within itself." << endl;

	    cerr << get_fileline() << ":      : The offending instance is "
		 << scope_path(sc) << "." << get_name() << " within "
		 << scope_path(scn) << "." << endl;

	    des->errors += 1;
	    return;
      }

      NetExpr*mse = msb_ ? elab_and_eval(des, sc, msb_, -1) : 0;
      NetExpr*lse = lsb_ ? elab_and_eval(des, sc, lsb_, -1) : 0;
      NetEConst*msb = dynamic_cast<NetEConst*> (mse);
      NetEConst*lsb = dynamic_cast<NetEConst*> (lse);

      assert( (msb == 0) || (lsb != 0) );

      long instance_low  = 0;
      long instance_high = 0;
      long instance_count  = 1;
      bool instance_array = false;

      if (msb) {
	    instance_array = true;
	    instance_high = msb->value().as_long();
	    instance_low  = lsb->value().as_long();
	    if (instance_high > instance_low)
		  instance_count = instance_high - instance_low + 1;
	    else
		  instance_count = instance_low - instance_high + 1;

	    delete mse;
	    delete lse;
      }

      NetScope::scope_vec_t instances (instance_count);
      if (debug_scopes) {
	    cerr << get_fileline() << ": debug: Create " << instance_count
		 << " instances of " << get_name()
		 << "." << endl;
      }

	// Run through the module instances, and make scopes out of
	// them. Also do parameter overrides that are done on the
	// instantiation line.
      for (int idx = 0 ;  idx < instance_count ;  idx += 1) {

	    hname_t use_name (get_name());

	    if (instance_array) {
		  int instance_idx = idx;
		  if (instance_low < instance_high)
			instance_idx = instance_low + idx;
		  else
			instance_idx = instance_low - idx;

		  use_name = hname_t(get_name(), instance_idx);
	    }

	    if (debug_scopes) {
		  cerr << get_fileline() << ": debug: Module instance " << use_name
		       << " becomes child of " << scope_path(sc)
		       << "." << endl;
	    }

	      // Create the new scope as a MODULE with my name.
	    NetScope*my_scope = new NetScope(sc, use_name, NetScope::MODULE);
	    my_scope->set_module_name(mod->mod_name());
	    my_scope->default_nettype(mod->default_nettype);

	    instances[idx] = my_scope;

	      // Set time units and precision.
	    my_scope->time_unit(mod->time_unit);
	    my_scope->time_precision(mod->time_precision);
	    des->set_precision(mod->time_precision);

	      // Look for module parameter replacements. The "replace" map
	      // maps parameter name to replacement expression that is
	      // passed. It is built up by the ordered overrides or named
	      // overrides.

	    typedef map<perm_string,PExpr*>::const_iterator mparm_it_t;
	    map<perm_string,PExpr*> replace;


	      // Positional parameter overrides are matched to parameter
	      // names by using the param_names list of parameter
	      // names. This is an ordered list of names so the first name
	      // is parameter 0, the second parameter 1, and so on.

	    if (overrides_) {
		  assert(parms_ == 0);
		  list<perm_string>::const_iterator cur
			= mod->param_names.begin();
		  unsigned idx = 0;
		  for (;;) {
			if (idx >= overrides_->count())
			      break;
			if (cur == mod->param_names.end())
			      break;

			replace[*cur] = (*overrides_)[idx];

			idx += 1;
			cur ++;
		  }
	    }

	      // Named parameter overrides carry a name with each override
	      // so the mapping into the replace list is much easier.
	    if (parms_) {
		  assert(overrides_ == 0);
		  for (unsigned idx = 0 ;  idx < nparms_ ;  idx += 1)
			replace[parms_[idx].name] = parms_[idx].parm;

	    }


	    Module::replace_t replace_net;

	      // And here we scan the replacements we collected. Elaborate
	      // the expression in my context, then replace the sub-scope
	      // parameter value with the new expression.

	    for (mparm_it_t cur = replace.begin()
		       ; cur != replace.end() ;  cur ++ ) {

		  PExpr*tmp = (*cur).second;
		    // No expression means that the parameter is not
		    // replaced at all.
		  if (tmp == 0)
			continue;
		  NetExpr*val = tmp->elaborate_pexpr(des, sc);
		  replace_net[(*cur).first] = val;
	    }

	      // This call actually arranges for the description of the
	      // module type to process this instance and handle parameters
	      // and sub-scopes that might occur. Parameters are also
	      // created in that scope, as they exist. (I'll override them
	      // later.)
	    mod->elaborate_scope(des, my_scope, replace_net);

      }

	/* Stash the instance array of scopes into the parent
	   scope. Later elaboration passes will use this vector to
	   further elaborate the array. */
      sc->instance_arrays[get_name()] = instances;
}

/*
 * The isn't really able to create new scopes, but it does create the
 * event name in the current scope, so can be done during the
 * elaborate_scope scan. Note that the name_ of the PEvent object has
 * no hierarchy, but neither does the NetEvent, until it is stored in
 * the NetScope object.
 */
void PEvent::elaborate_scope(Design*des, NetScope*scope) const
{
      NetEvent*ev = new NetEvent(name_);
      ev->set_line(*this);
      scope->add_event(ev);
}

void PFunction::elaborate_scope(Design*des, NetScope*scope) const
{
      assert(scope->type() == NetScope::FUNC);

      if (statement_)
	    statement_->elaborate_scope(des, scope);
}

void PTask::elaborate_scope(Design*des, NetScope*scope) const
{
      assert(scope->type() == NetScope::TASK);

      if (statement_)
	    statement_->elaborate_scope(des, scope);
}


/*
 * The base statement does not have sub-statements and does not
 * introduce any scope, so this is a no-op.
 */
void Statement::elaborate_scope(Design*, NetScope*) const
{
}

/*
 * When I get a behavioral block, check to see if it has a name. If it
 * does, then create a new scope for the statements within it,
 * otherwise use the current scope. Use the selected scope to scan the
 * statements that I contain.
 */
void PBlock::elaborate_scope(Design*des, NetScope*scope) const
{
      NetScope*my_scope = scope;

      if (name_ != 0) {
	    hname_t use_name(name_);
	    if (scope->child(use_name)) {
		  cerr << get_fileline() << ": error: block/scope name "
		       << use_name << " already used in this context."
		       << endl;
		  des->errors += 1;
		  return;
	    }
	    my_scope = new NetScope(scope, use_name, bl_type_==BL_PAR
				    ? NetScope::FORK_JOIN
				    : NetScope::BEGIN_END);
      }

      for (unsigned idx = 0 ;  idx < list_.count() ;  idx += 1)
	    list_[idx] -> elaborate_scope(des, my_scope);

}

/*
 * The case statement itself does not introduce scope, but contains
 * other statements that may be named blocks. So scan the case items
 * with the elaborate_scope method.
 */
void PCase::elaborate_scope(Design*des, NetScope*scope) const
{
      assert(items_);
      for (unsigned idx = 0 ;  idx < (*items_).count() ;  idx += 1) {
	    assert( (*items_)[idx] );

	    if (Statement*sp = (*items_)[idx]->stat)
		  sp -> elaborate_scope(des, scope);
      }
}

/*
 * The conditional statement (if-else) does not introduce scope, but
 * the statements of the clauses may, so elaborate_scope the contained
 * statements.
 */
void PCondit::elaborate_scope(Design*des, NetScope*scope) const
{
      if (if_)
	    if_ -> elaborate_scope(des, scope);

      if (else_)
	    else_ -> elaborate_scope(des, scope);
}

/*
 * Statements that contain a further statement but do not
 * intrinsically add a scope need to elaborate_scope the contained
 * statement.
 */
void PDelayStatement::elaborate_scope(Design*des, NetScope*scope) const
{
      if (statement_)
	    statement_ -> elaborate_scope(des, scope);
}

/*
 * Statements that contain a further statement but do not
 * intrinsically add a scope need to elaborate_scope the contained
 * statement.
 */
void PEventStatement::elaborate_scope(Design*des, NetScope*scope) const
{
      if (statement_)
	    statement_ -> elaborate_scope(des, scope);
}

/*
 * Statements that contain a further statement but do not
 * intrinsically add a scope need to elaborate_scope the contained
 * statement.
 */
void PForever::elaborate_scope(Design*des, NetScope*scope) const
{
      if (statement_)
	    statement_ -> elaborate_scope(des, scope);
}

/*
 * Statements that contain a further statement but do not
 * intrinsically add a scope need to elaborate_scope the contained
 * statement.
 */
void PForStatement::elaborate_scope(Design*des, NetScope*scope) const
{
      if (statement_)
	    statement_ -> elaborate_scope(des, scope);
}

/*
 * Statements that contain a further statement but do not
 * intrinsically add a scope need to elaborate_scope the contained
 * statement.
 */
void PRepeat::elaborate_scope(Design*des, NetScope*scope) const
{
      if (statement_)
	    statement_ -> elaborate_scope(des, scope);
}

/*
 * Statements that contain a further statement but do not
 * intrinsically add a scope need to elaborate_scope the contained
 * statement.
 */
void PWhile::elaborate_scope(Design*des, NetScope*scope) const
{
      if (statement_)
	    statement_ -> elaborate_scope(des, scope);
}
