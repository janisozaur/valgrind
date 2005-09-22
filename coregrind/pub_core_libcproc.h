
/*--------------------------------------------------------------------*/
/*--- Process-related libc stuff.              pub_core_libcproc.h ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2000-2005 Julian Seward
      jseward@acm.org

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

#ifndef __PUB_CORE_LIBCPROC_H
#define __PUB_CORE_LIBCPROC_H

//--------------------------------------------------------------------
// PURPOSE: This module contains libc code related to the process.
// It's a bit of a mixed bag.
//--------------------------------------------------------------------

#include "pub_tool_libcproc.h"

/* The directory we look for all our auxillary files in.  Useful for
   running Valgrind out of a build tree without having to do "make
   install". */
#define VALGRIND_LIB     "VALGRIND_LIB"

/* Additional command-line arguments; they are overridden by actual
   command-line option.  Each argument is separated by spaces.  There
   is no quoting mechanism.  */
#define VALGRIND_OPTS    "VALGRIND_OPTS"

/* The full name of Valgrind's stage1 (launcher) executable.  This is
   set by stage1 and read by stage2, and is used for recursive
   invokations of Valgrind on child processes. */
#define VALGRIND_LAUNCHER  "VALGRIND_LAUNCHER"


// Environment manipulations
extern Char **VG_(env_setenv)   ( Char ***envp, const Char* varname,
                                  const Char *val );
extern void   VG_(env_unsetenv) ( Char **env, const Char *varname );
extern void   VG_(env_remove_valgrind_env_stuff) ( Char** env ); 
extern Char **VG_(env_clone)    ( Char **env_clone );

// misc
extern Int  VG_(poll)( struct vki_pollfd *, UInt nfds, Int timeout);
extern void VG_(nanosleep) ( struct vki_timespec * );
extern Int  VG_(getgroups)( Int size, UInt* list );
extern Int  VG_(ptrace)( Int request, Int pid, void *addr, void *data );
extern Int  VG_(fork)( void );

// atfork
typedef void (*vg_atfork_t)(ThreadId);
extern void VG_(atfork_child)    ( vg_atfork_t child_action );
extern void VG_(do_atfork_child) ( ThreadId tid );

#endif   // __PUB_CORE_LIBCPROC_H

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
