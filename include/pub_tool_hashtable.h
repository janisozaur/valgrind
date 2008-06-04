
/*--------------------------------------------------------------------*/
/*--- A hash table implementation.            pub_tool_hashtable.h ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2005-2007 Nicholas Nethercote
      njn@valgrind.org

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

#ifndef __PUB_TOOL_HASHTABLE_H
#define __PUB_TOOL_HASHTABLE_H

/* Generic type for a separately-chained hash table.  Via a kind of dodgy
   C-as-C++ style inheritance, tools can extend the VgHashNode type, so long
   as the first two fields match the sizes of these two fields.  Requires
   a bit of casting by the tool. */

// Problems with this data structure:
// - Separate chaining gives bad cache behaviour.  Hash tables with linear
//   probing give better cache behaviour.

typedef
   struct _VgHashNode {
      struct _VgHashNode * next;
      UWord              key;
   }
   VgHashNode;

typedef struct _VgHashTable * VgHashTable;

/* Make a new table.  Allocates the memory with VG_(calloc)(), so can
   be freed with VG_(free)().  The table starts small but will
   periodically be expanded.  This is transparent to the users of this
   module. */
extern VgHashTable VG_(HT_construct) ( HChar* name );

/* Count the number of nodes in a table. */
extern Int VG_(HT_count_nodes) ( VgHashTable table );

/* Add a node to the table. */
extern void VG_(HT_add_node) ( VgHashTable t, void* node );

/* Looks up a VgHashNode in the table.  Returns NULL if not found. */
extern void* VG_(HT_lookup) ( VgHashTable table, UWord key );

/* Removes a VgHashNode from the table.  Returns NULL if not found. */
extern void* VG_(HT_remove) ( VgHashTable table, UWord key );

/* Allocates a suitably-sized array, copies all the hashtable elements
   into it, then returns both the array and the size of it.  This is
   used by the memory-leak detector.  The array must be freed with
   VG_(free). */
extern VgHashNode** VG_(HT_to_array) ( VgHashTable t, /*OUT*/ UInt* n_elems );

/* Reset the table's iterator to point to the first element. */
extern void VG_(HT_ResetIter) ( VgHashTable table );

/* Return the element pointed to by the iterator and move on to the
   next one.  Returns NULL if the last one has been passed, or if
   HT_ResetIter() has not been called previously.  Asserts if the
   table has been modified (HT_add_node, HT_remove) since
   HT_ResetIter.  This guarantees that callers cannot screw up by
   modifying the table whilst iterating over it (and is necessary to
   make the implementation safe; specifically we must guarantee that
   the table will not get resized whilst iteration is happening.
   Since resizing only happens as a result of calling HT_add_node,
   disallowing HT_add_node during iteration should give the required
   assurance. */
extern void* VG_(HT_Next) ( VgHashTable table );

/* Destroy a table. */
extern void VG_(HT_destruct) ( VgHashTable t );


#endif   // __PUB_TOOL_HASHTABLE_H

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
