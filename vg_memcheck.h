
/*
   This file is part of Valgrind, an x86 protected-mode emulator 
   designed for debugging and profiling binaries on x86-Unixes.

   Copyright (C) 2000-2002 Julian Seward 
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

   The GNU General Public License is contained in the file LICENSE.
*/


#ifndef __VG_MEMCHECK_H
#define __VG_MEMCHECK_H


/* This file is for inclusion into client (your!) code.

   You can use these macros to manipulate and query memory permissions
   inside your own programs.

   See comment near the top of valgrind.h on how to use them.
*/

#include "valgrind.h"

typedef
   enum { 
      VG_USERREQ__MAKE_NOACCESS = VG_USERREQ__FINAL_DUMMY_CLIENT_REQUEST + 1, 
      VG_USERREQ__MAKE_WRITABLE,
      VG_USERREQ__MAKE_READABLE,
      VG_USERREQ__DISCARD,
      VG_USERREQ__CHECK_WRITABLE,
      VG_USERREQ__CHECK_READABLE,
      VG_USERREQ__MAKE_NOACCESS_STACK,
      VG_USERREQ__DO_LEAK_CHECK, /* untested */
   } Vg_MemCheckClientRequest;



/* Client-code macros to manipulate the state of memory. */

/* Mark memory at _qzz_addr as unaddressible and undefined for
   _qzz_len bytes.  Returns an int handle pertaining to the block
   descriptions Valgrind will use in subsequent error messages. */
#define VALGRIND_MAKE_NOACCESS(_qzz_addr,_qzz_len)               \
   ({unsigned int _qzz_res;                                      \
    VALGRIND_MAGIC_SEQUENCE(_qzz_res, 0 /* default return */,    \
                            VG_USERREQ__MAKE_NOACCESS,           \
                            _qzz_addr, _qzz_len, 0, 0);          \
    _qzz_res;                                                    \
   }) 
      
/* Similarly, mark memory at _qzz_addr as addressible but undefined
   for _qzz_len bytes. */
#define VALGRIND_MAKE_WRITABLE(_qzz_addr,_qzz_len)               \
   ({unsigned int _qzz_res;                                      \
    VALGRIND_MAGIC_SEQUENCE(_qzz_res, 0 /* default return */,    \
                            VG_USERREQ__MAKE_WRITABLE,           \
                            _qzz_addr, _qzz_len, 0, 0);          \
    _qzz_res;                                                    \
   })

/* Similarly, mark memory at _qzz_addr as addressible and defined
   for _qzz_len bytes. */
#define VALGRIND_MAKE_READABLE(_qzz_addr,_qzz_len)               \
   ({unsigned int _qzz_res;                                      \
    VALGRIND_MAGIC_SEQUENCE(_qzz_res, 0 /* default return */,    \
                            VG_USERREQ__MAKE_READABLE,           \
                            _qzz_addr, _qzz_len, 0, 0);          \
    _qzz_res;                                                    \
   })

/* Discard a block-description-handle obtained from the above three
   macros.  After this, Valgrind will no longer be able to relate
   addressing errors to the user-defined block associated with the
   handle.  The permissions settings associated with the handle remain
   in place.  Returns 1 for an invalid handle, 0 for a valid
   handle. */
#define VALGRIND_DISCARD(_qzz_blkindex)                          \
   ({unsigned int _qzz_res;                                      \
    VALGRIND_MAGIC_SEQUENCE(_qzz_res, 0 /* default return */,    \
                            VG_USERREQ__DISCARD,                 \
                            0, _qzz_blkindex, 0, 0);             \
    _qzz_res;                                                    \
   })


/* Client-code macros to check the state of memory. */

/* Check that memory at _qzz_addr is addressible for _qzz_len bytes.
   If suitable addressibility is not established, Valgrind prints an
   error message and returns the address of the first offending byte.
   Otherwise it returns zero. */
#define VALGRIND_CHECK_WRITABLE(_qzz_addr,_qzz_len)                \
   ({unsigned int _qzz_res;                                        \
    VALGRIND_MAGIC_SEQUENCE(_qzz_res, 0,                           \
                            VG_USERREQ__CHECK_WRITABLE,            \
                            _qzz_addr, _qzz_len, 0, 0);            \
    _qzz_res;                                                      \
   })

/* Check that memory at _qzz_addr is addressible and defined for
   _qzz_len bytes.  If suitable addressibility and definedness are not
   established, Valgrind prints an error message and returns the
   address of the first offending byte.  Otherwise it returns zero. */
#define VALGRIND_CHECK_READABLE(_qzz_addr,_qzz_len)                \
   ({unsigned int _qzz_res;                                        \
    VALGRIND_MAGIC_SEQUENCE(_qzz_res, 0,                           \
                            VG_USERREQ__CHECK_READABLE,            \
                            _qzz_addr, _qzz_len, 0, 0);            \
    _qzz_res;                                                      \
   })

/* Use this macro to force the definedness and addressibility of a
   value to be checked.  If suitable addressibility and definedness
   are not established, Valgrind prints an error message and returns
   the address of the first offending byte.  Otherwise it returns
   zero. */
#define VALGRIND_CHECK_DEFINED(__lvalue)                           \
   (void)                                                          \
   VALGRIND_CHECK_READABLE(                                        \
      (volatile unsigned char *)&(__lvalue),                       \
                      (unsigned int)(sizeof (__lvalue)))

/* Mark memory, intended to be on the client's stack, at _qzz_addr as
   unaddressible and undefined for _qzz_len bytes.  Does not return a
   value.  The record associated with this setting will be
   automatically removed by Valgrind when the containing routine
   exits. */
#define VALGRIND_MAKE_NOACCESS_STACK(_qzz_addr,_qzz_len)           \
   {unsigned int _qzz_res;                                         \
    VALGRIND_MAGIC_SEQUENCE(_qzz_res, 0,                           \
                            VG_USERREQ__MAKE_NOACCESS_STACK,       \
                            _qzz_addr, _qzz_len, 0, 0);            \
   }



/* Do a memory leak check mid-execution.
   Currently implemented but untested.
*/
#define VALGRIND_DO_LEAK_CHECK                                     \
   {unsigned int _qzz_res;                                         \
    VALGRIND_MAGIC_SEQUENCE(_qzz_res, 0,                           \
                            VG_USERREQ__DO_LEAK_CHECK,             \
                            0, 0, 0, 0);                           \
   }


#endif
