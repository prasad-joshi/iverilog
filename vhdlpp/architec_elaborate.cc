/*
 * Copyright (c) 2011 Stephen Williams (steve@icarus.com)
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

# include  "architec.h"
# include  "entity.h"
# include  "expression.h"
# include  "sequential.h"
# include  <typeinfo>
# include  <cassert>

int Architecture::elaborate(Entity*entity)
{
      int errors = 0;

      for (list<Architecture::Statement*>::iterator cur = statements_.begin()
		 ; cur != statements_.end() ; ++cur) {

	    errors += (*cur)->elaborate(entity, this);
      }

       return errors;
}

int Architecture::Statement::elaborate(Entity*, Architecture*)
{
      return 0;
}

int ComponentInstantiation::elaborate(Entity*ent, Architecture*arc)
{
      int errors = 0;

      ComponentBase*base = arc->find_component(cname_);
      if (base == 0) {
	    cerr << get_fileline() << ": error: No component declaration"
		 << " for instance " << iname_
		 << " of " << cname_ << "." << endl;
	    return 1;
      }

      map<perm_string,const InterfacePort*> port_match;

      for (map<perm_string,Expression*>::iterator cur = port_map_.begin()
		 ; cur != port_map_.end() ; ++cur) {
	    const InterfacePort*iport = base->find_port(cur->first);
	    if (iport == 0) {
		  cerr << get_fileline() << ": error: No port " << cur->first
		       << " in component " << cname_ << "." << endl;
		  errors += 1;
		  continue;
	    }

	      /* It is possible for the port to be explicitly
		 unconnected. In that case, the Expression will be nil */
	    if (cur->second)
		  cur->second->elaborate_expr(ent, arc, iport->type);
      }

      return errors;
}

/*
 * This method attempts to rewrite the process content as an
 * always-@(n-edge <expr>) version of the same statement. This makes
 * for a more natural translation to verilog, if it comes to that.
 */
int ProcessStatement::rewrite_as_always_edge_(Entity*, Architecture*)
{
	// If thare are multiple sensitivity expressions, I give up.
      if (sensitivity_list_.size() != 1)
	    return -1;

	// If there are multiple statements, I give up.
      if (statements_list_.size() != 1)
	    return -1;

      Expression*se = sensitivity_list_.front();
      SequentialStmt*stmt_raw = statements_list_.front();

	// If the statement is not an if-statement, I give up.
      IfSequential*stmt = dynamic_cast<IfSequential*> (stmt_raw);
      if (stmt == 0)
	    return -1;

	// If the "if" statement has a false clause, then give up.
      if (stmt->false_size() != 0)
	    return -1;

      const Expression*ce_raw = stmt->peek_condition();

	// We expect the condition to be <name>'event AND <name>='1'.
	// If it's not a logical AND, I give up.
      const ExpLogical*ce = dynamic_cast<const ExpLogical*> (ce_raw);
      if (ce == 0)
	    return -1;
      if (ce->logic_fun() != ExpLogical::AND)
	    return -1;

      const Expression*op1_raw = ce->peek_operand1();
      const Expression*op2_raw = ce->peek_operand2();
      if (dynamic_cast<const ExpAttribute*>(op2_raw)) {
	    const Expression*tmp = op1_raw;
	    op1_raw = op2_raw;
	    op2_raw = tmp;
      }

	// If operand1 is not an 'event attribute, I give up.
      const ExpAttribute*op1 = dynamic_cast<const ExpAttribute*>(op1_raw);
      if (op1 == 0)
	    return -1;
      if (op1->peek_attribute() != "event")
	    return -1;

      const ExpRelation*op2 = dynamic_cast<const ExpRelation*>(op2_raw);
      if (op2 == 0)
	    return -1;
      if (op2->relation_fun() != ExpRelation::EQ)
	    return -1;

      const Expression*op2a_raw = op2->peek_operand1();
      const Expression*op2b_raw = op2->peek_operand2();

      if (dynamic_cast<const ExpCharacter*>(op2a_raw)) {
	    const Expression*tmp = op2b_raw;
	    op2b_raw = op2a_raw;
	    op2a_raw = tmp;
      }

      if (! se->symbolic_compare(op1->peek_base()))
	    return -1;

      const ExpCharacter*op2b = dynamic_cast<const ExpCharacter*>(op2b_raw);
      if (op2b->value() != '1' && op2b->value() != '0')
	    return -1;

	// We've matched this pattern:
	//   process (<se>) if (<se>'event and <se> = <op2b>) then ...
	// And we can convert it to:
	//   always @(<N>edge <se>) ...

	// Replace the sensitivity expression with an edge
	// expression. The ExpEdge expression signals that this is an
	// always-@(edge) statement.
      ExpEdge*edge = new ExpEdge(op2b->value()=='1'? ExpEdge::POSEDGE : ExpEdge::NEGEDGE, se);
      assert(sensitivity_list_.size() == 1);
      sensitivity_list_.pop_front();
      sensitivity_list_.push_front(edge);

	// Replace the statement with the body of the always
	// statement, which is the true clause of the top "if"
	// statement. There should be no "else" clause.
      assert(statements_list_.size() == 1);
      statements_list_.pop_front();

      stmt->extract_true(statements_list_);

      delete stmt;

      return 0;
}

/*
 * Change the "process (<expr>) <stmt>" into "always @(<expr>) ..."
 */
int ProcessStatement::extract_anyedge_(Entity*, Architecture*)
{

      vector<Expression*> se;
      while (! sensitivity_list_.empty()) {
	    se.push_back(sensitivity_list_.front());
	    sensitivity_list_.pop_front();
      }

      for (size_t idx = 0 ; idx < se.size() ; idx += 1) {
	    ExpEdge*edge = new ExpEdge(ExpEdge::ANYEDGE, se[idx]);
	    FILE_NAME(edge, se[idx]);
	    sensitivity_list_.push_back(edge);
      }

      return 0;
}

int ProcessStatement::elaborate(Entity*ent, Architecture*arc)
{
      int errors = 0;

      if (rewrite_as_always_edge_(ent, arc) >= 0) {

      } else if (extract_anyedge_(ent, arc) >= 0) {

      } else {
      }

      for (list<SequentialStmt*>::iterator cur = statements_list_.begin()
		 ; cur != statements_list_.end() ; ++cur) {
	    errors += (*cur)->elaborate(ent, arc);
      }

      return errors;
}

int SignalAssignment::elaborate(Entity*ent, Architecture*arc)
{
      int errors = 0;

	// Elaborate the l-value expression.
      errors += lval_->elaborate_lval(ent, arc, false);

	// The elaborate_lval should have resolved the type of the
	// l-value expression. We'll use that type to elaborate the
	// r-value.
      const VType*lval_type = lval_->peek_type();
      if (lval_type == 0) {
	    if (errors == 0) errors += 1;
	    return errors;
      }

      for (list<Expression*>::const_iterator cur = rval_.begin()
		 ; cur != rval_.end() ; ++cur) {
	    (*cur)->elaborate_expr(ent, arc, lval_type);
      }

      return errors;
}
