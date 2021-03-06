/*
 * Copyright (C) 2010-2011 Cary R. (cygcary@yahoo.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

# include <inttypes.h>
# include <string.h>
# include "config.h"
# include "vlog95_priv.h"
# include "ivl_alloc.h"

static char*get_time_const(int time_value)
{
      switch (time_value) {
	case   2: return "100s";
	case   1: return "10s";
	case   0: return "1s";
	case  -1: return "100ms";
	case  -2: return "10ms";
	case  -3: return "1ms";
	case  -4: return "100us";
	case  -5: return "10us";
	case  -6: return "1us";
	case  -7: return "100ns";
	case  -8: return "10ns";
	case  -9: return "1ns";
	case -10: return "100ps";
	case -11: return "10ps";
	case -12: return "1ps";
	case -13: return "100fs";
	case -14: return "10fs";
	case -15: return "1fs";
	default:
	    fprintf(stderr, "Invalid time constant value %d.\n", time_value);
	    return "N/A";
      }
}

void emit_func_return(ivl_signal_t sig)
{
      if (ivl_signal_dimensions(sig) > 0) {
	    fprintf(stderr, "%s:%u: vlog95 error: A function cannot return "
	                    "an array.\n", ivl_signal_file(sig),
	                    ivl_signal_lineno(sig));
	    vlog_errors += 1;
      } else if (ivl_signal_integer(sig)) {
	    fprintf(vlog_out, " integer");
      } else if (ivl_signal_data_type(sig) == IVL_VT_REAL) {
	    fprintf(vlog_out, " real");
      } else {
	    int msb = ivl_signal_msb(sig);
	    int lsb = ivl_signal_lsb(sig);
	    if (msb != 0 || lsb != 0) fprintf(vlog_out, " [%d:%d]", msb, lsb);
      }
}

void emit_sig_file_line(ivl_signal_t sig)
{
      if (emit_file_line) {
	    fprintf(vlog_out, " /* %s:%u */",
	                      ivl_signal_file(sig),
	                      ivl_signal_lineno(sig));
      }
}

static void emit_sig_id(ivl_signal_t sig)
{
      emit_id(ivl_signal_basename(sig));
      fprintf(vlog_out, ";");
      emit_sig_file_line(sig);
      fprintf(vlog_out, "\n");
}

void emit_var_def(ivl_signal_t sig)
{
      if (ivl_signal_local(sig)) return;
      fprintf(vlog_out, "%*c", indent, ' ');
      if (ivl_signal_integer(sig)) {
	    fprintf(vlog_out, "integer ");
	    emit_sig_id(sig);
	    if (ivl_signal_dimensions(sig) > 0) {
		  fprintf(stderr, "%s:%u: vlog95 error: Integer arrays (%s) "
		                  "are not supported.\n", ivl_signal_file(sig),
		                  ivl_signal_lineno(sig),
		                  ivl_signal_basename(sig));
		  vlog_errors += 1;
	    }
      } else if (ivl_signal_data_type(sig) == IVL_VT_REAL) {
	    fprintf(vlog_out, "real ");
	    emit_sig_id(sig);
	    if (ivl_signal_dimensions(sig) > 0) {
		  fprintf(stderr, "%s:%u: vlog95 error: Real arrays (%s) "
		                  "are not supported.\n", ivl_signal_file(sig),
		                  ivl_signal_lineno(sig),
		                  ivl_signal_basename(sig));
		  vlog_errors += 1;
	    }
      } else {
	    int msb = ivl_signal_msb(sig);
	    int lsb = ivl_signal_lsb(sig);
	    fprintf(vlog_out, "reg ");
	    if (ivl_signal_signed(sig)) {
		  if (allow_signed) {
			fprintf(vlog_out, "signed ");
		  } else {
			fprintf(stderr, "%s:%u: vlog95 error: Signed registers "
			                "(%s) are not supported.\n",
			                ivl_signal_file(sig),
			                ivl_signal_lineno(sig),
			                ivl_signal_basename(sig));
			vlog_errors += 1;
		  }
	    }
	    if (msb != 0 || lsb != 0) fprintf(vlog_out, "[%d:%d] ", msb, lsb);
	    emit_id(ivl_signal_basename(sig));
	    if (ivl_signal_dimensions(sig) > 0) {
		  unsigned wd_count = ivl_signal_array_count(sig);
		  int first = ivl_signal_array_base(sig);
		  int last = first + wd_count - 1;
		  if (ivl_signal_array_addr_swapped(sig)) {
			fprintf(vlog_out, " [%d:%d]", last, first);
		  } else {
			fprintf(vlog_out, " [%d:%d]", first, last);
		  }
	    }
	    fprintf(vlog_out, ";");
	    emit_sig_file_line(sig);
	    fprintf(vlog_out, "\n");
      }
}

/*
 * Keep a list of constants that drive nets and need to be emitted as
 * a continuous assignment.
*/
static ivl_signal_t *net_consts = 0;
static unsigned num_net_consts = 0;

static void add_net_const_to_list(ivl_signal_t net_const)
{
      num_net_consts += 1;
      net_consts = realloc(net_consts, num_net_consts * sizeof(ivl_signal_t));
      net_consts[num_net_consts-1] = net_const;
}

static unsigned emit_and_free_net_const_list(ivl_scope_t scope)
{
      unsigned idx;
      for (idx = 0; idx < num_net_consts; idx += 1) {
	    emit_signal_net_const_as_ca(scope, net_consts[idx]);
      }
      free(net_consts);
      net_consts = 0;
      idx = num_net_consts != 0;
      num_net_consts = 0;
      return idx;
}

static void save_net_constants(ivl_scope_t scope, ivl_signal_t sig)
{
      ivl_nexus_t nex = ivl_signal_nex(sig, 0);
      unsigned idx, count = ivl_nexus_ptrs(nex);
      for (idx = 0; idx < count; idx += 1) {
	    ivl_nexus_ptr_t nex_ptr = ivl_nexus_ptr(nex, idx);
	    ivl_net_const_t net_const = ivl_nexus_ptr_con(nex_ptr);
	    if (! net_const) continue;
	    if (scope != ivl_const_scope(net_const)) continue;
	    add_net_const_to_list(sig);
      }
}

void emit_net_def(ivl_scope_t scope, ivl_signal_t sig)
{
      int msb = ivl_signal_msb(sig);
      int lsb = ivl_signal_lsb(sig);
      if (ivl_signal_local(sig)) return;
      fprintf(vlog_out, "%*c", indent, ' ');
      if (ivl_signal_data_type(sig) == IVL_VT_REAL){
	    fprintf(vlog_out, "wire ");
	    emit_sig_id(sig);
	    fprintf(stderr, "%s:%u: vlog95 error: Real nets (%s) are "
	                    "not supported.\n", ivl_signal_file(sig),
	                    ivl_signal_lineno(sig), ivl_signal_basename(sig));
	    vlog_errors += 1;
      } else if (ivl_signal_dimensions(sig) > 0) {
	    fprintf(vlog_out, "wire ");
	    if (msb != 0 || lsb != 0) fprintf(vlog_out, "[%d:%d] ", msb, lsb);
	    emit_sig_id(sig);
	    fprintf(stderr, "%s:%u: vlog95 error: Array nets (%s) are "
	                    "not supported.\n", ivl_signal_file(sig),
	                    ivl_signal_lineno(sig), ivl_signal_basename(sig));
	    vlog_errors += 1;
      } else {
	    switch(ivl_signal_type(sig)) {
	      case IVL_SIT_TRI:
	      case IVL_SIT_UWIRE:
// HERE: Need to add support for supply nets. Probably supply strength
//       with a constant 0/1 driver for all the bits.
		  fprintf(vlog_out, "wire ");
		  break;
	      case IVL_SIT_TRI0:
		  fprintf(vlog_out, "tri0 ");
		  break;
	      case IVL_SIT_TRI1:
		  fprintf(vlog_out, "tri1 ");
		  break;
	      case IVL_SIT_TRIAND:
		  fprintf(vlog_out, "wand ");
		  break;
	      case IVL_SIT_TRIOR:
		  fprintf(vlog_out, "wor ");
		  break;
	      default:
		  fprintf(vlog_out, "<unknown> ");
		  fprintf(stderr, "%s:%u: vlog95 error: Unknown net type "
	                    "(%d).\n", ivl_signal_file(sig),
	                    ivl_signal_lineno(sig), (int)ivl_signal_type(sig));
		  vlog_errors += 1;
		  break;
	    }
	    if (ivl_signal_signed(sig)) {
		  if (allow_signed) {
			fprintf(vlog_out, "signed ");
		  } else {
			fprintf(stderr, "%s:%u: vlog95 error: Signed nets (%s) "
			                "are not supported.\n",
			                ivl_signal_file(sig),
			                ivl_signal_lineno(sig),
			                ivl_signal_basename(sig));
			vlog_errors += 1;
		  }
	    }
	    if (msb != 0 || lsb != 0) fprintf(vlog_out, "[%d:%d] ", msb, lsb);
	    emit_sig_id(sig);
	      /* A constant driving a net does not create an lpm or logic
	       * element in the design so save them from the definition. */
	    save_net_constants(scope, sig);
      }
}

static void emit_mangled_name(ivl_scope_t scope, unsigned root)
{
	/* If the module has parameters and it's not a root module then it
	 * may not be unique so we create a mangled name version instead.
	 * The mangled name is of the form:
	 *   <module_name>[<full_instance_scope>]. */
      if (ivl_scope_params(scope) && ! root) {
	    char *name;
	    size_t len = strlen(ivl_scope_name(scope)) +
	                 strlen(ivl_scope_tname(scope)) + 3;
	    name = (char *)malloc(len);
	    (void) strcpy(name, ivl_scope_tname(scope));
	    (void) strcat(name, "[");
	    (void) strcat(name, ivl_scope_name(scope));
	    (void) strcat(name, "]");
	    assert(name[len-1] == 0);
	      /* Emit the mangled name as an escaped identifier. */
	    fprintf(vlog_out, "\\%s ", name);
	    free(name);
      } else {
	    emit_id(ivl_scope_tname(scope));
      }
}

/*
 * This function is called for each process in the design so that we
 * can extract the processes for the given scope.
 */
static int find_process(ivl_process_t proc, ivl_scope_t scope)
{
      if (scope == ivl_process_scope(proc)) emit_process(scope, proc);
      return 0;
}

void emit_scope_variables(ivl_scope_t scope)
{
      unsigned idx, count;
      assert(! num_net_consts);
	/* Output the parameters for this scope. */
      count = ivl_scope_params(scope);
      for (idx = 0; idx < count; idx += 1) {
	    ivl_parameter_t par = ivl_scope_param(scope, idx);
	    ivl_expr_t pex = ivl_parameter_expr(par);
	    fprintf(vlog_out, "%*cparameter ", indent, ' ');
	    emit_id(ivl_parameter_basename(par));
	    fprintf(vlog_out, " = ");
	    emit_expr(scope, pex, 0);
	    fprintf(vlog_out, ";");
	    if (emit_file_line) {
		  fprintf(vlog_out, " /* %s:%u */",
		                    ivl_parameter_file(par),
		                    ivl_parameter_lineno(par));
	    }
	    fprintf(vlog_out, "\n");
      }
      if (count) fprintf(vlog_out, "\n");

	/* Output the signals for this scope. */
      count = ivl_scope_sigs(scope);
      for (idx = 0; idx < count; idx += 1) {
	    ivl_signal_t sig = ivl_scope_sig(scope, idx);
	    if (ivl_signal_type(sig) == IVL_SIT_REG) {
		    /* Do not output the implicit function return register. */
		  if (ivl_scope_type(scope) == IVL_SCT_FUNCTION &&
                      strcmp(ivl_signal_basename(sig),
		              ivl_scope_tname(scope)) == 0) continue;
		  emit_var_def(sig);
	    } else {
		  emit_net_def(scope, sig);
	    }
      }
      if (count) fprintf(vlog_out, "\n");

	/* Output the named events for this scope. */
      count = ivl_scope_events(scope);
      for (idx = 0; idx < count; idx += 1) {
	    ivl_event_t event = ivl_scope_event(scope, idx);
	      /* If this event has any type of edge sensitivity then it is
	       * not a named event. */
	    if (ivl_event_nany(event)) continue;
	    if (ivl_event_npos(event)) continue;
	    if (ivl_event_nneg(event)) continue;
	    fprintf(vlog_out, "%*cevent ", indent, ' ');
	    emit_id(ivl_event_basename(event));
	    fprintf(vlog_out, ";");
	    if (emit_file_line) {
		  fprintf(vlog_out, " /* %s:%u */",
		                    ivl_event_file(event),
		                    ivl_event_lineno(event));
	    }
	    fprintf(vlog_out, "\n");
      }
      if (count) fprintf(vlog_out, "\n");
      if (emit_and_free_net_const_list(scope)) fprintf(vlog_out, "\n");
}

static void emit_scope_file_line(ivl_scope_t scope)
{
      if (emit_file_line) {
	    fprintf(vlog_out, " /* %s:%u */",
	                      ivl_scope_file(scope),
	                      ivl_scope_lineno(scope));
      }
}

static void emit_module_ports(ivl_scope_t scope)
{
      unsigned idx, count = ivl_scope_ports(scope);

      if (count == 0) return;

      fprintf(vlog_out, "(");
      emit_nexus_as_ca(scope, ivl_scope_mod_port(scope, 0));
      for (idx = 1; idx < count; idx += 1) {
	    fprintf(vlog_out, ", ");
	    emit_nexus_as_ca(scope, ivl_scope_mod_port(scope, idx));
      }
      fprintf(vlog_out, ")");
}

static ivl_signal_t get_port_from_nexus(ivl_scope_t scope, ivl_nexus_t nex)
{
      assert(nex);
      unsigned idx, count = ivl_nexus_ptrs(nex);
      ivl_signal_t sig = 0;
      for (idx = 0; idx < count; idx += 1) {
	    ivl_nexus_ptr_t nex_ptr = ivl_nexus_ptr(nex, idx);
	    ivl_signal_t t_sig = ivl_nexus_ptr_sig(nex_ptr);
	    if (t_sig) {
		  if (ivl_signal_scope(t_sig) != scope) continue;
		  assert(! sig);
		  sig = t_sig;
	    }
      }
      return sig;
}

static void emit_sig_type(ivl_signal_t sig)
{
      ivl_signal_type_t type = ivl_signal_type(sig);
      assert(ivl_signal_dimensions(sig) == 0);
	/* Check to see if we have a variable (reg) or a net. */
      if (type == IVL_SIT_REG) {
	    if (ivl_signal_integer(sig)) {
		  fprintf(vlog_out, " integer");
	    } else if (ivl_signal_data_type(sig) == IVL_VT_REAL) {
		  fprintf(vlog_out, " real");
	    } else {
		  int msb = ivl_signal_msb(sig);
		  int lsb = ivl_signal_lsb(sig);
		  if (ivl_signal_signed(sig)) {
			if (allow_signed) {
			      fprintf(vlog_out, " signed");
			} else {
			      fprintf(stderr, "%s:%u: vlog95 error: Signed "
			                      "ports (%s) are not supported.\n",
			                      ivl_signal_file(sig),
			                      ivl_signal_lineno(sig),
			                      ivl_signal_basename(sig));
			      vlog_errors += 1;
			}
		  }
		  if (msb != 0 || lsb != 0) {
			fprintf(vlog_out, " [%d:%d]", msb, lsb);
		  }
	    }
      } else {
	    assert((type == IVL_SIT_TRI) ||
	           (type == IVL_SIT_TRI0) ||
	           (type == IVL_SIT_TRI1));
	    if (ivl_signal_data_type(sig) == IVL_VT_REAL) {
		  fprintf(stderr, "%s:%u: vlog95 error: Real net ports (%s) "
		                  "are not supported.\n",
		                  ivl_signal_file(sig),
		                  ivl_signal_lineno(sig),
		                  ivl_signal_basename(sig));
		  vlog_errors += 1;
	    } else {
		  int msb = ivl_signal_msb(sig);
		  int lsb = ivl_signal_lsb(sig);
		  if (ivl_signal_signed(sig)) {
			if (allow_signed) {
			      fprintf(vlog_out, " signed");
			} else {
			      fprintf(stderr, "%s:%u: vlog95 error: Signed net "
			                      "ports (%s) are not supported.\n",
			                      ivl_signal_file(sig),
			                      ivl_signal_lineno(sig),
			                      ivl_signal_basename(sig));
			      vlog_errors += 1;
			}
		  }
		  if (msb != 0 || lsb != 0) {
			fprintf(vlog_out, " [%d:%d]", msb, lsb);
		  }
	    }
      }
}

static void emit_port(ivl_signal_t port)
{
      assert(port);
      fprintf(vlog_out, "%*c", indent, ' ');
      switch (ivl_signal_port(port)) {
	case IVL_SIP_INPUT:
	    fprintf(vlog_out, "input");
	    break;
	case IVL_SIP_OUTPUT:
	    fprintf(vlog_out, "output");
	    break;
	case IVL_SIP_INOUT:
	    fprintf(vlog_out, "inout");
	    break;
	default:
	    fprintf(vlog_out, "<unknown>");
	    fprintf(stderr, "%s:%u: vlog95 error: Unknown port direction (%d) "
	                    "for signal %s.\n", ivl_signal_file(port),
	                    ivl_signal_lineno(port), (int)ivl_signal_port(port),
	                    ivl_signal_basename(port));
	    vlog_errors += 1;
	    break;
      }
      emit_sig_type(port);
      fprintf(vlog_out, " ");
      emit_id(ivl_signal_basename(port));
      fprintf(vlog_out, ";");
      emit_sig_file_line(port);
      fprintf(vlog_out, "\n");
}

static void emit_module_port_defs(ivl_scope_t scope)
{
      unsigned idx, count = ivl_scope_ports(scope);
      for (idx = 0; idx < count; idx += 1) {
	    ivl_nexus_t nex = ivl_scope_mod_port(scope, idx);
	    ivl_signal_t port = get_port_from_nexus(scope, nex);
	    if (port) emit_port(port);
	    else {
		  fprintf(vlog_out, "<missing>");
		  fprintf(stderr, "%s:%u: vlog95 error: Could not find signal "
	                    "definition for port (%u) of module %s.\n",
		            ivl_scope_file(scope), ivl_scope_lineno(scope),
	                    idx + 1, ivl_scope_basename(scope));
		  vlog_errors += 1;
	    }
      }
      if (count) fprintf(vlog_out, "\n");
}

static void emit_module_call_expr(ivl_scope_t scope, unsigned idx)
{
      ivl_nexus_t nex = ivl_scope_mod_port(scope, idx);
      ivl_signal_t port = get_port_from_nexus(scope, nex);
	/* For an input port we need to emit the driving expression. */
      if (ivl_signal_port(port) == IVL_SIP_INPUT) {
	    emit_nexus_port_driver_as_ca(ivl_scope_parent(scope),
	                                 ivl_signal_nex(port, 0));
	/* For an output we need to emit the signal the output is driving. */
      } else {
	    emit_nexus_as_ca(ivl_scope_parent(scope), ivl_signal_nex(port, 0));
      }
}

static void emit_module_call_expressions(ivl_scope_t scope)
{
      unsigned idx, count = ivl_scope_ports(scope);
      if (count == 0) return;
      emit_module_call_expr(scope, 0);
      for (idx = 1; idx < count; idx += 1) {
	    fprintf(vlog_out, ", ");
	    emit_module_call_expr(scope, idx);
      }
}

static void emit_task_func_port_defs(ivl_scope_t scope)
{
      unsigned idx, count = ivl_scope_ports(scope);
      unsigned start = ivl_scope_type(scope) == IVL_SCT_FUNCTION;
      for (idx = start; idx < count; idx += 1) {
	    ivl_signal_t port = ivl_scope_port(scope, idx);
	    emit_port(port);
      }
      if (count) fprintf(vlog_out, "\n");
}

/*
 * This search method may be slow for a large structural design with a
 * large number of gate types. That's not what this converter was built
 * for so this is probably OK. If this becomes an issue then we need a
 * better method/data structure.
*/
static const char **scopes_emitted = 0;
static unsigned num_scopes_emitted = 0;

static unsigned scope_has_been_emitted(ivl_scope_t scope)
{
      unsigned idx;
      for (idx = 0; idx < num_scopes_emitted; idx += 1) {
	    if (! strcmp(ivl_scope_tname(scope), scopes_emitted[idx])) return 1;
      }
      return 0;
}

static void add_scope_to_list(ivl_scope_t scope)
{
      num_scopes_emitted += 1;
      scopes_emitted = realloc(scopes_emitted, num_scopes_emitted *
                                               sizeof(char *));
      scopes_emitted[num_scopes_emitted-1] = ivl_scope_tname(scope);
}

void free_emitted_scope_list()
{
      free(scopes_emitted);
      scopes_emitted = 0;
      num_scopes_emitted = 0;
}

/*
 * A list of module scopes that need to have their definition emitted when
 * the current root scope (module) is finished is kept here.
 */
static ivl_scope_t *scopes_to_emit = 0;
static unsigned num_scopes_to_emit = 0;
static unsigned emitting_scopes = 0;

int emit_scope(ivl_scope_t scope, ivl_scope_t parent)
{
      ivl_scope_type_t sc_type = ivl_scope_type(scope);
      unsigned is_auto = ivl_scope_is_auto(scope);
      unsigned idx, count;

	/* Output the scope definition. */
      switch (sc_type) {
	case IVL_SCT_MODULE:
	    assert(!is_auto);
	      /* This is an instantiation. */
	    if (parent) {
		  assert(indent != 0);
		    /* If the module has parameters then it may not be unique
		     * so we create a mangled name version instead. */
		  fprintf(vlog_out, "\n%*c", indent, ' ');
		  emit_mangled_name(scope, !parent && !emitting_scopes);
		  fprintf(vlog_out, " ");
		  emit_id(ivl_scope_basename(scope));
		  fprintf(vlog_out, "(");
		  emit_module_call_expressions(scope);
		  fprintf(vlog_out, ");");
		  emit_scope_file_line(scope);
		  fprintf(vlog_out, "\n");
		  num_scopes_to_emit += 1;
		  scopes_to_emit = realloc(scopes_to_emit, num_scopes_to_emit *
		                                           sizeof(ivl_scope_t));
		  scopes_to_emit[num_scopes_to_emit-1] = scope;
		  return 0;
	    }
	    assert(indent == 0);
	      /* Set the time scale for this scope. */
	    fprintf(vlog_out, "\n`timescale %s/%s\n",
	                      get_time_const(ivl_scope_time_units(scope)),
	                      get_time_const(ivl_scope_time_precision(scope)));
	    if (ivl_scope_is_cell(scope)) {
		  fprintf(vlog_out, "`celldefine\n");
	    }
	    fprintf(vlog_out, "/* This module was originally defined in "
	                      "file %s at line %u. */\n",
	                      ivl_scope_def_file(scope),
	                      ivl_scope_def_lineno(scope));
	    fprintf(vlog_out, "module ");
	    emit_mangled_name(scope, !parent && !emitting_scopes);
	    emit_module_ports(scope);
	    break;
	case IVL_SCT_FUNCTION:
	    assert(indent != 0);
	    fprintf(vlog_out, "\n%*cfunction", indent, ' ');
	    assert(ivl_scope_ports(scope) >= 2);
	      /* The function return information is the zero port. */
	    emit_func_return(ivl_scope_port(scope, 0));
	    fprintf(vlog_out, " ");
	    emit_id(ivl_scope_tname(scope));
	    if (is_auto) {
		  fprintf(stderr, "%s:%u: vlog95 error: Automatic functions "
	                    "(%s) are not supported.\n", ivl_scope_file(scope),
	                    ivl_scope_lineno(scope), ivl_scope_tname(scope));
		  vlog_errors += 1;
	    }
	    break;
	case IVL_SCT_TASK:
	    assert(indent != 0);
	    fprintf(vlog_out, "\n%*ctask ", indent, ' ');
	    emit_id(ivl_scope_tname(scope));
	    if (is_auto) {
		  fprintf(stderr, "%s:%u: vlog95 error: Automatic tasks "
	                    "(%s) are not supported.\n", ivl_scope_file(scope),
	                    ivl_scope_lineno(scope), ivl_scope_tname(scope));
		  vlog_errors += 1;
	    }
	    break;
	case IVL_SCT_BEGIN:
	case IVL_SCT_FORK:
	    assert(indent != 0);
	    return 0; /* A named begin/fork is handled in line. */
	default:
	    fprintf(stderr, "%s:%u: vlog95 error: Unsupported scope type "
	                    "(%d) named: %s.\n", ivl_scope_file(scope),
	                    ivl_scope_lineno(scope), sc_type,
	                    ivl_scope_tname(scope));
	    vlog_errors += 1;
	    return 0;
      }
      fprintf(vlog_out, ";");
      emit_scope_file_line(scope);
      fprintf(vlog_out, "\n");
      indent += indent_incr;

	/* Output the scope ports for this scope. */
      if (sc_type == IVL_SCT_MODULE) {
	    emit_module_port_defs(scope);
      } else {
	    emit_task_func_port_defs(scope);
      }

      emit_scope_variables(scope);

      if (sc_type == IVL_SCT_MODULE) {
	      /* Output the LPM devices. */
	    count = ivl_scope_lpms(scope);
	    for (idx = 0; idx < count; idx += 1) {
		  emit_lpm(scope, ivl_scope_lpm(scope, idx));
	    }

	      /* Output any logic devices. */
	    count = ivl_scope_logs(scope);
	    for (idx = 0; idx < count; idx += 1) {
		  emit_logic(scope, ivl_scope_log(scope, idx));
	    }

	      /* Output any switch (logic) devices. */
	    count = ivl_scope_switches(scope);
	    for (idx = 0; idx < count; idx += 1) {
		  emit_tran(scope, ivl_scope_switch(scope, idx));
	    }

	      /* Output the initial/always blocks for this module. */
	    ivl_design_process(design, (ivl_process_f)find_process, scope);
      }

	/* Output the function/task body. */
      if (sc_type == IVL_SCT_TASK || sc_type == IVL_SCT_FUNCTION) {
	    emit_stmt(scope, ivl_scope_def(scope));
      }

	/* Now print out any sub-scopes. */
      ivl_scope_children(scope, (ivl_scope_f*) emit_scope, scope);

	/* Output the scope ending. */
      assert(indent >= indent_incr);
      indent -= indent_incr;
      switch (sc_type) {
	case IVL_SCT_MODULE:
	    assert(indent == 0);
	    fprintf(vlog_out, "endmodule  /* ");
	    emit_mangled_name(scope, !parent && !emitting_scopes);
	    fprintf(vlog_out, " */\n");
	    if (ivl_scope_is_cell(scope)) {
		  fprintf(vlog_out, "`endcelldefine\n");
	    }
	      /* If this is a root scope then emit any saved instance scopes.
	       * Save any scope that does not have parameters/a mangled name
	       * to a list so we don't print duplicate module definitions. */
	    if (!emitting_scopes) {
		  emitting_scopes = 1;
		  for (idx =0; idx < num_scopes_to_emit; idx += 1) {
			ivl_scope_t scope_to_emit = scopes_to_emit[idx];
			if (scope_has_been_emitted(scope_to_emit)) continue;
			(void) emit_scope(scope_to_emit, 0);
			  /* If we used a mangled name then the instance is
			   * unique so don't add it to the list. */
			if (ivl_scope_params(scope_to_emit)) continue;
			add_scope_to_list(scope_to_emit);
		  }
		  free(scopes_to_emit);
		  scopes_to_emit = 0;
		  num_scopes_to_emit = 0;
		  emitting_scopes = 0;
	    }
	    break;
	case IVL_SCT_FUNCTION:
	    fprintf(vlog_out, "%*cendfunction  /* %s */\n", indent, ' ',
	                      ivl_scope_tname(scope));
	    break;
	case IVL_SCT_TASK:
	    fprintf(vlog_out, "%*cendtask  /* %s */\n", indent, ' ',
	                      ivl_scope_tname(scope));
	    break;
	default:
	    assert(0);
	    break;
      }
      return 0;
}
