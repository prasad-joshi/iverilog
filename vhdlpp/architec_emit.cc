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
# include  "vsignal.h"
# include  <iostream>
# include  <typeinfo>
# include  <cassert>

int Scope::emit_signals(ostream&out, Entity*entity, Architecture*arc)
{
      int errors = 0;

      for (map<perm_string,Signal*>::iterator cur = signals_.begin()
		 ; cur != signals_.end() ; ++cur) {

	    errors += cur->second->emit(out, entity, arc);
      }

      return errors;
}

int Architecture::emit(ostream&out, Entity*entity)
{
      int errors = 0;

      for (map<perm_string,struct const_t>::iterator cur = constants_.begin()
		 ; cur != constants_.end() ; ++cur) {

	    out << "localparam " << cur->first << " = ";
	    errors += cur->second.val->emit(out, entity, this);
	    out << ";" << endl;
      }

      errors += emit_signals(out, entity, this);

      for (list<Architecture::Statement*>::iterator cur = statements_.begin()
		 ; cur != statements_.end() ; ++cur) {

	    errors += (*cur)->emit(out, entity, this);
      }

      return errors;
}

int Architecture::Statement::emit(ostream&out, Entity*, Architecture*)
{
      out << " // " << get_fileline() << ": internal error: "
	  << "I don't know how to emit this statement! "
	  << "type=" << typeid(*this).name() << endl;
      return 1;
}

int SignalAssignment::emit(ostream&out, Entity*ent, Architecture*arc)
{
      int errors = 0;

      assert(rval_.size() == 1);
      Expression*rval = rval_.front();

      out << "// " << get_fileline() << endl;
      out << "assign ";
      errors += lval_->emit(out, ent, arc);
      out << " = ";

      errors += rval->emit(out, ent, arc);

      out << ";" << endl;
      return errors;
}

int ComponentInstantiation::emit(ostream&out, Entity*ent, Architecture*arc)
{
      int errors = 0;

      out << cname_ << " " << iname_ << "(";
      const char*comma = "";
      for (map<perm_string,Expression*>::iterator cur = port_map_.begin()
		 ; cur != port_map_.end() ; ++cur) {
	      // Skip unconnected ports
	    if (cur->second == 0)
		  continue;
	    out << comma << "." << cur->first << "(";
	    errors += cur->second->emit(out, ent, arc);
	    out << ")";
	    comma = ", ";
      }
      out << ");" << endl;

      return errors;
}

int ProcessStatement::emit(ostream&out, Entity*ent, Architecture*arc)
{
      int errors = 0;

      out << "always";

      if (sensitivity_list_.size() > 0) {
	    out << " @(";
	    const char*comma = 0;
	    for (list<Expression*>::iterator cur = sensitivity_list_.begin()
		       ; cur != sensitivity_list_.end() ; ++cur) {

		  if (comma) out << comma;
		  errors += (*cur)->emit(out, ent, arc);
		  comma = ", ";
	    }
	    out << ")";
      }

      out << " begin" << endl;

      for (list<SequentialStmt*>::iterator cur = statements_list_.begin()
		 ; cur != statements_list_.end() ; ++cur) {
	    (*cur)->emit(out, ent, arc);
      }

      out << "end" << endl;
      return errors;

}
