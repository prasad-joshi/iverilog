#ifndef __HName_H
#define __HName_H
/*
 * Copyright (c) 2001-2010 Stephen Williams (steve@icarus.com)
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

# include  <iostream>
# include  <list>
# include  "StringHeap.h"
# include  <climits>
#ifdef __GNUC__
#if __GNUC__ > 2
using namespace std;
#endif
#endif

/*
 * This class represents a component of a Verilog hierarchical name. A
 * hierarchical component contains a name string (represented here
 * with a perm_string) and an optional signed number. This signed
 * number is used if the scope is part of an array, for example an
 * array of module instances or a loop generated scope.
 */

class hname_t {

    public:
      hname_t ();
      explicit hname_t (perm_string text);
      explicit hname_t (perm_string text, int num);
      hname_t (const hname_t&that);
      ~hname_t();

      hname_t& operator= (const hname_t&);

	// Return the string part of the hname_t.
      perm_string peek_name(void) const;

      bool has_number() const;
      int peek_number() const;

    private:
      perm_string name_;
	// If the number is anything other than INT_MIN, then this is
	// the numeric part of the name. Otherwise, it is not part of
	// the name at all.
      int number_;

    private: // not implemented
};

inline hname_t::~hname_t()
{
}

inline perm_string hname_t::peek_name(void) const
{
      return name_;
}

inline int hname_t::peek_number() const
{
      return number_;
}

inline bool hname_t::has_number() const
{
      return number_ != INT_MIN;
}

extern bool operator <  (const hname_t&, const hname_t&);
extern bool operator == (const hname_t&, const hname_t&);
extern ostream& operator<< (ostream&, const hname_t&);

inline bool operator != (const hname_t&l, const hname_t&r)
{ return ! (l == r); }

inline ostream& operator<< (ostream&out, const list<hname_t>&ll)
{
      list<hname_t>::const_iterator cur = ll.begin();
      out << *cur;
      ++ cur;
      while (cur != ll.end()) {
	    out << "." << *cur;
	    ++ cur;
      }
      return out;
}

#endif
