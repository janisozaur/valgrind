
/*--------------------------------------------------------------------*/
/*--- Cachegrind: every but the simulation itself.                 ---*/
/*---                                                    cg_main.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Cachegrind, a Valgrind tool for cache
   profiling programs.

   Copyright (C) 2002-2004 Nicholas Nethercote
      njn25@cam.ac.uk

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

#include "tool.h"
//#include "vg_profile.c"

#include "cg_arch.h"
#include "cg_sim.c"

/*------------------------------------------------------------*/
/*--- Constants                                            ---*/
/*------------------------------------------------------------*/

#define MIN_LINE_SIZE         16
#define FILE_LEN              256
#define FN_LEN                256

/*------------------------------------------------------------*/
/*--- Profiling events                                     ---*/
/*------------------------------------------------------------*/

typedef 
   enum { 
      VgpGetLineCC = VgpFini+1,
      VgpCacheSimulate,
      VgpCacheResults
   } 
   VgpToolCC;

/*------------------------------------------------------------*/
/*--- Types and Data Structures                            ---*/
/*------------------------------------------------------------*/

typedef struct _CC CC;
struct _CC {
   ULong a;
   ULong m1;
   ULong m2;
};

//------------------------------------------------------------
// Primary data structure #1: CC table
// - Holds the per-source-line hit/miss stats, grouped by file/function/line.
// - hash(file, hash(fn, hash(line+CC)))
// - Each hash table is separately chained.
// - The array sizes below work fairly well for Konqueror.
// - Lookups done by instr_addr, which is converted immediately to a source
//   location.
// - Traversed for dumping stats at end in file/func/line hierarchy.

#define N_FILE_ENTRIES        251
#define   N_FN_ENTRIES         53
#define N_LINE_ENTRIES         37

typedef struct _lineCC lineCC;
struct _lineCC {
   Int      line;
   CC       Ir;
   CC       Dr;
   CC       Dw;
   lineCC*  next;
};

typedef struct _fnCC fnCC;
struct _fnCC {
   Char*   fn;
   fnCC*   next;
   lineCC* lines[N_LINE_ENTRIES];
};

typedef struct _fileCC fileCC;
struct _fileCC {
   Char*   file;
   fileCC* next;
   fnCC*   fns[N_FN_ENTRIES];
};

// Top level of CC table.  Auto-zeroed.
static fileCC *CC_table[N_FILE_ENTRIES];

//------------------------------------------------------------
// Primary data structre #2: Instr-info table
// - Holds the cached info about each instr that is used for simulation.
// - table(BB_start_addr, list(instr_info))
// - For each BB, each instr_info in the list holds info about the
//   instruction (instr_size, instr_addr, etc), plus a pointer to its line
//   CC.  This node is what's passed to the simulation function.
// - When BBs are discarded the relevant list(instr_details) is freed.

typedef struct _instr_info instr_info;
struct _instr_info {
   Addr    instr_addr;
   UChar   instr_size;
   UChar   data_size;
   lineCC* parent;       // parent line-CC
};

typedef struct _BB_info BB_info;
struct _BB_info {
   BB_info*   next;              // next field
   Addr       BB_addr;           // key
   Int        n_instrs;
   instr_info instrs[0];
};

VgHashTable instr_info_table;    // hash(Addr, BB_info)

//------------------------------------------------------------
// Stats
static Int  distinct_files      = 0;
static Int  distinct_fns        = 0;
static Int  distinct_lines      = 0;
static Int  distinct_instrs     = 0;

static Int  full_debug_BBs      = 0;
static Int  file_line_debug_BBs = 0;
static Int  fn_debug_BBs        = 0;
static Int  no_debug_BBs        = 0;

static Int  BB_retranslations   = 0;

/*------------------------------------------------------------*/
/*--- CC table operations                                  ---*/
/*------------------------------------------------------------*/

static void get_debug_info(Addr instr_addr, Char file[FILE_LEN],
                           Char fn[FN_LEN], Int* line)
{
   Bool found_file_line = VG_(get_filename_linenum)(instr_addr, file,
                                                    FILE_LEN, line);
   Bool found_fn        = VG_(get_fnname)(instr_addr, fn, FN_LEN);

   if (!found_file_line) {
      VG_(strcpy)(file, "???");
      *line = 0;
   }
   if (!found_fn) {
      VG_(strcpy)(fn,  "???");
   }
   if (found_file_line) {
      if (found_fn) full_debug_BBs++;
      else          file_line_debug_BBs++;
   } else {
      if (found_fn) fn_debug_BBs++;
      else          no_debug_BBs++;
   }
}

static UInt hash(Char *s, UInt table_size)
{
   const int hash_constant = 256;
   int hash_value = 0;
   for ( ; *s; s++)
      hash_value = (hash_constant * hash_value + *s) % table_size;
   return hash_value;
}

static __inline__ 
fileCC* new_fileCC(Char filename[], fileCC* next)
{
   // Using calloc() zeroes the fns[] array
   fileCC* cc = VG_(calloc)(1, sizeof(fileCC));
   cc->file   = VG_(strdup)(filename);
   cc->next   = next;
   return cc;
}

static __inline__ 
fnCC* new_fnCC(Char fn[], fnCC* next)
{
   // Using calloc() zeroes the lines[] array
   fnCC* cc = VG_(calloc)(1, sizeof(fnCC));
   cc->fn   = VG_(strdup)(fn);
   cc->next = next;
   return cc;
}

static __inline__ 
lineCC* new_lineCC(Int line, lineCC* next)
{
   // Using calloc() zeroes the Ir/Dr/Dw CCs and the instrs[] array
   lineCC* cc = VG_(calloc)(1, sizeof(lineCC));
   cc->line   = line;
   cc->next   = next;
   return cc;
}

static __inline__ 
instr_info* new_instr_info(Addr instr_addr, lineCC* parent, instr_info* next)
{
   // Using calloc() zeroes instr_size and data_size
   instr_info* ii = VG_(calloc)(1, sizeof(instr_info));
   ii->instr_addr = instr_addr;
   ii->parent     = parent; 
   return ii;
}

// Do a three step traversal: by file, then fn, then line.
// In all cases prepends new nodes to their chain.  Returns a pointer to the
// line node, creates a new one if necessary.
static lineCC* get_lineCC(Addr orig_addr)
{
   fileCC *curr_fileCC;
   fnCC   *curr_fnCC;
   lineCC *curr_lineCC;
   Char    file[FILE_LEN], fn[FN_LEN];
   Int     line;
   UInt    file_hash, fn_hash, line_hash;

   get_debug_info(orig_addr, file, fn, &line);

   VGP_PUSHCC(VgpGetLineCC);

   // level 1
   file_hash = hash(file, N_FILE_ENTRIES);
   curr_fileCC   = CC_table[file_hash];
   while (NULL != curr_fileCC && !VG_STREQ(file, curr_fileCC->file)) {
      curr_fileCC = curr_fileCC->next;
   }
   if (NULL == curr_fileCC) {
      CC_table[file_hash] = curr_fileCC = 
         new_fileCC(file, CC_table[file_hash]);
      distinct_files++;
   }

   // level 2
   fn_hash = hash(fn, N_FN_ENTRIES);
   curr_fnCC   = curr_fileCC->fns[fn_hash];
   while (NULL != curr_fnCC && !VG_STREQ(fn, curr_fnCC->fn)) {
      curr_fnCC = curr_fnCC->next;
   }
   if (NULL == curr_fnCC) {
      curr_fileCC->fns[fn_hash] = curr_fnCC = 
         new_fnCC(fn, curr_fileCC->fns[fn_hash]);
      distinct_fns++;
   }

   // level 3
   line_hash   = line % N_LINE_ENTRIES;
   curr_lineCC = curr_fnCC->lines[line_hash];
   while (NULL != curr_lineCC && line != curr_lineCC->line) {
      curr_lineCC = curr_lineCC->next;
   }
   if (NULL == curr_lineCC) {
      curr_fnCC->lines[line_hash] = curr_lineCC = 
         new_lineCC(line, curr_fnCC->lines[line_hash]);
      distinct_lines++;
   }

   VGP_POPCC(VgpGetLineCC);
   return curr_lineCC;
}

/*------------------------------------------------------------*/
/*--- Cache simulation functions                           ---*/
/*------------------------------------------------------------*/

static REGPARM(1)
void log_1I_0D_cache_access(instr_info* n)
{
   //VG_(printf)("1I_0D: CCaddr=0x%x, iaddr=0x%x, isize=%u\n",
   //            n, n->instr_addr, n->instr_size)
   VGP_PUSHCC(VgpCacheSimulate);
   cachesim_I1_doref(n->instr_addr, n->instr_size, 
                     &n->parent->Ir.m1, &n->parent->Ir.m2);
   n->parent->Ir.a++;
   VGP_POPCC(VgpCacheSimulate);
}

static REGPARM(2)
void log_1I_1Dr_cache_access(instr_info* n, Addr data_addr)
{
   //VG_(printf)("1I_1Dr: CCaddr=%p, iaddr=%p, isize=%u, daddr=%p, dsize=%u\n",
   //            n, n->instr_addr, n->instr_size, data_addr, n->data_size)
   VGP_PUSHCC(VgpCacheSimulate);
   cachesim_I1_doref(n->instr_addr, n->instr_size, 
                     &n->parent->Ir.m1, &n->parent->Ir.m2);
   n->parent->Ir.a++;

   cachesim_D1_doref(data_addr, n->data_size, 
                     &n->parent->Dr.m1, &n->parent->Dr.m2);
   n->parent->Dr.a++;
   VGP_POPCC(VgpCacheSimulate);
}

static REGPARM(2)
void log_1I_1Dw_cache_access(instr_info* n, Addr data_addr)
{
   //VG_(printf)("1I_1Dw: CCaddr=%p, iaddr=%p, isize=%u, daddr=%p, dsize=%u\n",
   //            n, n->instr_addr, n->instr_size, data_addr, n->data_size)
   VGP_PUSHCC(VgpCacheSimulate);
   cachesim_I1_doref(n->instr_addr, n->instr_size, 
                     &n->parent->Ir.m1, &n->parent->Ir.m2);
   n->parent->Ir.a++;

   cachesim_D1_doref(data_addr, n->data_size, 
                     &n->parent->Dw.m1, &n->parent->Dw.m2);
   n->parent->Dw.a++;
   VGP_POPCC(VgpCacheSimulate);
}

static REGPARM(3)
void log_1I_2D_cache_access(instr_info* n, Addr data_addr1, Addr data_addr2)
{
   //VG_(printf)("1I_2D: CCaddr=%p, iaddr=%p, isize=%u, daddr1=%p, daddr2=%p, dsize=%u\n",
   //            n, n->instr_addr, n->instr_size, data_addr1, data_addr2, n->data_size)
   VGP_PUSHCC(VgpCacheSimulate);
   cachesim_I1_doref(n->instr_addr, n->instr_size,
                     &n->parent->Ir.m1,  &n->parent->Ir.m2);
   n->parent->Ir.a++;

   cachesim_D1_doref(data_addr1, n->data_size,
                     &n->parent->Dr.m1, &n->parent->Dr.m2);
   n->parent->Dr.a++;
   cachesim_D1_doref(data_addr2, n->data_size,
                     &n->parent->Dw.m1, &n->parent->Dw.m2);
   n->parent->Dw.a++;
   VGP_POPCC(VgpCacheSimulate);
}

/*------------------------------------------------------------*/
/*--- Instrumentation                                      ---*/
/*------------------------------------------------------------*/

#if 0
static
BB_info* get_BB_info(UCodeBlock* cb_in, Addr orig_addr, Bool* bb_seen_before)
{
   Int          i, n_instrs;
   UInstr*      u_in;
   BB_info*     bb_info;
   VgHashNode** dummy;
   
   // Count number of x86 instrs in BB
   n_instrs = 1;     // start at 1 because last x86 instr has no INCEIP
   for (i = 0; i < VG_(get_num_instrs)(cb_in); i++) {
      u_in = VG_(get_instr)(cb_in, i);
      if (INCEIP == u_in->opcode) n_instrs++;
   }

   // Get the BB_info
   bb_info = (BB_info*)VG_(HT_get_node)(instr_info_table, orig_addr, &dummy);
   *bb_seen_before = ( NULL == bb_info ? False : True );
   if (*bb_seen_before) {
      // BB must have been translated before, but flushed from the TT
      tl_assert(bb_info->n_instrs == n_instrs );
      BB_retranslations++;
   } else {
      // BB never translated before (at this address, at least;  could have
      // been unloaded and then reloaded elsewhere in memory)
      bb_info = 
         VG_(calloc)(1, sizeof(BB_info) + n_instrs*sizeof(instr_info)); 
      bb_info->BB_addr = orig_addr;
      bb_info->n_instrs = n_instrs;
      VG_(HT_add_node)( instr_info_table, (VgHashNode*)bb_info );
      distinct_instrs++;
   }
   return bb_info;
}
#endif

static
void do_details( instr_info* n, Bool bb_seen_before,
                 Addr instr_addr, Int instr_size, Int data_size )
{
   lineCC* parent = get_lineCC(instr_addr);
   if (bb_seen_before) {
      tl_assert( n->instr_addr == instr_addr );
      tl_assert( n->instr_size == instr_size );
      tl_assert( n->data_size  == data_size );
      // Don't assert that (n->parent == parent)... it's conceivable that
      // the debug info might change;  the other asserts should be enough to
      // detect anything strange.
   } else {
      n->instr_addr = instr_addr;
      n->instr_size = instr_size;
      n->data_size  = data_size;
      n->parent     = parent;
   }
}

static Bool is_valid_data_size(Int data_size)
{
   return (4 == data_size || 2  == data_size || 1 == data_size || 
           8 == data_size || 10 == data_size || MIN_LINE_SIZE == data_size);
}

#if 0
// Instrumentation for the end of each x86 instruction.
static
void end_of_x86_instr(UCodeBlock* cb, instr_info* i_node, Bool bb_seen_before,
                      UInt instr_addr, UInt instr_size, UInt data_size,
                      Int t_read,  Int t_read_addr, 
                      Int t_write, Int t_write_addr)
{
   Addr    helper;
   Int     argc;
   Int     t_CC_addr, 
           t_data_addr1 = INVALID_TEMPREG,
           t_data_addr2 = INVALID_TEMPREG;

   tl_assert(instr_size >= MIN_INSTR_SIZE && 
             instr_size <= MAX_INSTR_SIZE);

#define IS_(X)      (INVALID_TEMPREG != t_##X##_addr)
#define INV(qqt)    (INVALID_TEMPREG == (qqt))

   // Work out what kind of x86 instruction it is
   if (!IS_(read) && !IS_(write)) {
      tl_assert( 0 == data_size );
      tl_assert(INV(t_read) && INV(t_write));
      helper = (Addr) & log_1I_0D_cache_access;
      argc = 1;

   } else if (IS_(read) && !IS_(write)) {
      tl_assert( is_valid_data_size(data_size) );
      tl_assert(!INV(t_read) && INV(t_write));
      helper = (Addr) & log_1I_1Dr_cache_access;
      argc = 2;
      t_data_addr1 = t_read_addr;

   } else if (!IS_(read) && IS_(write)) {
      tl_assert( is_valid_data_size(data_size) );
      tl_assert(INV(t_read) && !INV(t_write));
      helper = (Addr) & log_1I_1Dw_cache_access;
      argc = 2;
      t_data_addr1 = t_write_addr;

   } else {
      tl_assert(IS_(read) && IS_(write));
      tl_assert( is_valid_data_size(data_size) );
      tl_assert(!INV(t_read) && !INV(t_write));
      if (t_read == t_write) {
         helper = (Addr) & log_1I_1Dr_cache_access;
         argc = 2;
         t_data_addr1 = t_read_addr;
      } else {
         helper = (Addr) & log_1I_2D_cache_access;
         argc = 3;
         t_data_addr1 = t_read_addr;
         t_data_addr2 = t_write_addr;
      }
   }
#undef IS_
#undef INV

   // Setup 1st arg: CC addr
   do_details( i_node, bb_seen_before, instr_addr, instr_size, data_size );
   t_CC_addr = newTemp(cb);
   uInstr2(cb, MOV,   4, Literal, 0, TempReg, t_CC_addr);
   uLiteral(cb, (Addr)i_node);

   // Call the helper
   if      (1 == argc)
      uInstr1(cb, CCALL, 0, TempReg, t_CC_addr);
   else if (2 == argc)
      uInstr2(cb, CCALL, 0, TempReg, t_CC_addr, 
                            TempReg, t_data_addr1);
   else if (3 == argc)
      uInstr3(cb, CCALL, 0, TempReg, t_CC_addr, 
                            TempReg, t_data_addr1,
                            TempReg, t_data_addr2);
   else
      VG_(tool_panic)("argc... not 1 or 2 or 3?");

   uCCall(cb, helper, argc, argc, False);
}
#endif

#if 0
UCodeBlock* TL_(instrument)(UCodeBlock* cb_in, Addr orig_addr)
{
   UCodeBlock* cb;
   UInstr*     u_in;
   Int         i, bb_info_i;
   BB_info*    bb_info;
   Bool        bb_seen_before = False;
   Int         t_read_addr, t_write_addr, t_read, t_write;
   Addr        x86_instr_addr = orig_addr;
   UInt        x86_instr_size, data_size = 0;
   Bool        instrumented_Jcc = False;

   bb_info = get_BB_info(cb_in, orig_addr, &bb_seen_before);
   bb_info_i = 0;

   cb = VG_(setup_UCodeBlock)(cb_in);

   t_read_addr = t_write_addr = t_read = t_write = INVALID_TEMPREG;

   for (i = 0; i < VG_(get_num_instrs)(cb_in); i++) {
      u_in = VG_(get_instr)(cb_in, i);

      // We want to instrument each x86 instruction with a call to the
      // appropriate simulation function, which depends on whether the
      // instruction does memory data reads/writes.  x86 instructions can
      // end in three ways, and this is how they are instrumented:
      //
      // 1. UCode, INCEIP   --> UCode, Instrumentation, INCEIP
      // 2. UCode, JMP      --> UCode, Instrumentation, JMP
      // 3. UCode, Jcc, JMP --> UCode, Instrumentation, Jcc, JMP
      //
      // The last UInstr in a BB is always a JMP.  Jccs, when they appear,
      // are always second last.  This is checked with assertions.
      // Instrumentation must go before any jumps.  (JIFZ is the exception;
      // if a JIFZ succeeds, no simulation is done for the instruction.)
      //
      // x86 instruction sizes are obtained from INCEIPs (for case 1) or
      // from .extra4b field of the final JMP (for case 2 & 3).

      if (instrumented_Jcc) tl_assert(u_in->opcode == JMP);

      switch (u_in->opcode) {

         // For memory-ref instrs, copy the data_addr into a temporary to be
         // passed to the cachesim_* helper at the end of the instruction.
         case LOAD: 
         case SSE3ag_MemRd_RegWr:
            t_read      = u_in->val1;
            t_read_addr = newTemp(cb);
            uInstr2(cb, MOV, 4, TempReg, u_in->val1,  TempReg, t_read_addr);
            data_size = u_in->size;
            VG_(copy_UInstr)(cb, u_in);
            break;

         case FPU_R:
         case MMX2_MemRd:
            t_read      = u_in->val2;
            t_read_addr = newTemp(cb);
            uInstr2(cb, MOV, 4, TempReg, u_in->val2,  TempReg, t_read_addr);
            data_size = u_in->size; 
            VG_(copy_UInstr)(cb, u_in);
            break;
            break;

         case MMX2a1_MemRd:
         case SSE2a_MemRd:
         case SSE2a1_MemRd:
         case SSE3a_MemRd:
         case SSE3a1_MemRd:
            t_read = u_in->val3;
            t_read_addr = newTemp(cb);
            uInstr2(cb, MOV, 4, TempReg, u_in->val3,  TempReg, t_read_addr);
            data_size = u_in->size;
            VG_(copy_UInstr)(cb, u_in);
            break;

         // Note that we must set t_write_addr even for mod instructions;
         // That's how the code above determines whether it does a write.
         // Without it, it would think a mod instruction is a read.
         // As for the MOV, if it's a mod instruction it's redundant, but it's
         // not expensive and mod instructions are rare anyway. */
         case STORE:
         case FPU_W:
         case MMX2_MemWr:
            t_write      = u_in->val2;
            t_write_addr = newTemp(cb);
            uInstr2(cb, MOV, 4, TempReg, u_in->val2, TempReg, t_write_addr);
            data_size = u_in->size;
            VG_(copy_UInstr)(cb, u_in);
            break;

         case SSE2a_MemWr:
         case SSE3a_MemWr:
            t_write = u_in->val3;
            t_write_addr = newTemp(cb);
            uInstr2(cb, MOV, 4, TempReg, u_in->val3, TempReg, t_write_addr);
            data_size = u_in->size; 
            VG_(copy_UInstr)(cb, u_in);
            break;

         // INCEIP: insert instrumentation
         case INCEIP:
            x86_instr_size = u_in->val1;
            goto instrument_x86_instr;

         // JMP: insert instrumentation if the first JMP
         case JMP:
            if (instrumented_Jcc) {
               tl_assert(CondAlways == u_in->cond);
               tl_assert(i+1 == VG_(get_num_instrs)(cb_in));
               VG_(copy_UInstr)(cb, u_in);
               instrumented_Jcc = False;     // rest
               break;
            } else {
               // The first JMP... instrument.
               if (CondAlways != u_in->cond) {
                  tl_assert(i+2 == VG_(get_num_instrs)(cb_in));
                  instrumented_Jcc = True;
               } else {
                  tl_assert(i+1 == VG_(get_num_instrs)(cb_in));
               }
               // Get x86 instr size from final JMP.
               x86_instr_size = VG_(get_last_instr)(cb_in)->extra4b;
               goto instrument_x86_instr;
            }

            // Code executed at the end of each x86 instruction.
         instrument_x86_instr:
            // Large (eg. 28B, 108B, 512B) data-sized instructions will be
            // done inaccurately but they're very rare and this avoids
            // errors from hitting more than two cache lines in the
            // simulation.
            if (data_size > MIN_LINE_SIZE) data_size = MIN_LINE_SIZE;

            end_of_x86_instr(cb, &bb_info->instrs[ bb_info_i ], bb_seen_before,
                             x86_instr_addr, x86_instr_size, data_size,
                             t_read, t_read_addr, t_write, t_write_addr);

            // Copy original UInstr (INCEIP or JMP)
            VG_(copy_UInstr)(cb, u_in);

            // Update loop state for next x86 instr
            bb_info_i++;
            x86_instr_addr += x86_instr_size;
            t_read_addr = t_write_addr = t_read = t_write = INVALID_TEMPREG;
            data_size = 0;
            break;

         default:
            VG_(copy_UInstr)(cb, u_in);
            break;
      }
   }

   // BB address should be the same as the first instruction's address.
   tl_assert(bb_info->BB_addr == bb_info->instrs[0].instr_addr );
   tl_assert(bb_info_i == bb_info->n_instrs);

   VG_(free_UCodeBlock)(cb_in);
   return cb;

#undef INVALID_DATA_SIZE
}
#endif

IRBB* TL_(instrument) ( IRBB* bb_in, VexGuestLayout* layout, IRType hWordTy )
{
   VG_(message)(Vg_DebugMsg, "Cachegrind is not yet ready to handle Vex IR");
   VG_(exit)(1);
}

/*------------------------------------------------------------*/
/*--- Cache configuration                                  ---*/
/*------------------------------------------------------------*/

#define UNDEFINED_CACHE     ((cache_t) { -1, -1, -1 }) 

static cache_t clo_I1_cache = UNDEFINED_CACHE;
static cache_t clo_D1_cache = UNDEFINED_CACHE;
static cache_t clo_L2_cache = UNDEFINED_CACHE;

/* Checks cache config is ok;  makes it so if not. */
static 
void check_cache(cache_t* cache, Char *name)
{
   /* First check they're all powers of two */
   if (-1 == VG_(log2)(cache->size)) {
      VG_(message)(Vg_UserMsg,
         "error: %s size of %dB not a power of two; aborting.",
         name, cache->size);
      VG_(exit)(1);
   }

   if (-1 == VG_(log2)(cache->assoc)) {
      VG_(message)(Vg_UserMsg,
         "error: %s associativity of %d not a power of two; aborting.",
         name, cache->assoc);
      VG_(exit)(1);
   }

   if (-1 == VG_(log2)(cache->line_size)) {
      VG_(message)(Vg_UserMsg,
         "error: %s line size of %dB not a power of two; aborting.",
         name, cache->line_size);
      VG_(exit)(1);
   }

   /* Then check line size >= 16 -- any smaller and a single instruction could
    * straddle three cache lines, which breaks a simulation assertion and is
    * stupid anyway. */
   if (cache->line_size < MIN_LINE_SIZE) {
      VG_(message)(Vg_UserMsg,
         "error: %s line size of %dB too small; aborting.", 
         name, cache->line_size);
      VG_(exit)(1);
   }

   /* Then check cache size > line size (causes seg faults if not). */
   if (cache->size <= cache->line_size) {
      VG_(message)(Vg_UserMsg,
         "error: %s cache size of %dB <= line size of %dB; aborting.",
         name, cache->size, cache->line_size);
      VG_(exit)(1);
   }

   /* Then check assoc <= (size / line size) (seg faults otherwise). */
   if (cache->assoc > (cache->size / cache->line_size)) {
      VG_(message)(Vg_UserMsg,
         "warning: %s associativity > (size / line size); aborting.", name);
      VG_(exit)(1);
   }
}

static 
void configure_caches(cache_t* I1c, cache_t* D1c, cache_t* L2c)
{
#define DEFINED(L)   (-1 != L.size  || -1 != L.assoc || -1 != L.line_size)

   Int n_clos = 0;

   // Count how many were defined on the command line.
   if (DEFINED(clo_I1_cache)) { n_clos++; }
   if (DEFINED(clo_D1_cache)) { n_clos++; }
   if (DEFINED(clo_L2_cache)) { n_clos++; }

   // Set the cache config (using auto-detection, if supported by the
   // architecture)
   VGA_(configure_caches)( I1c, D1c, L2c, (3 == n_clos) );

   // Then replace with any defined on the command line.
   if (DEFINED(clo_I1_cache)) { *I1c = clo_I1_cache; }
   if (DEFINED(clo_D1_cache)) { *D1c = clo_D1_cache; }
   if (DEFINED(clo_L2_cache)) { *L2c = clo_L2_cache; }

   // Then check values and fix if not acceptable.
   check_cache(I1c, "I1");
   check_cache(D1c, "D1");
   check_cache(L2c, "L2");

   if (VG_(clo_verbosity) > 1) {
      VG_(message)(Vg_UserMsg, "Cache configuration used:");
      VG_(message)(Vg_UserMsg, "  I1: %dB, %d-way, %dB lines",
                               I1c->size, I1c->assoc, I1c->line_size);
      VG_(message)(Vg_UserMsg, "  D1: %dB, %d-way, %dB lines",
                               D1c->size, D1c->assoc, D1c->line_size);
      VG_(message)(Vg_UserMsg, "  L2: %dB, %d-way, %dB lines",
                               L2c->size, L2c->assoc, L2c->line_size);
   }
#undef CMD_LINE_DEFINED
}

/*------------------------------------------------------------*/
/*--- TL_(fini)() and related function                     ---*/
/*------------------------------------------------------------*/

// Total reads/writes/misses.  Calculated during CC traversal at the end.
// All auto-zeroed.
static CC Ir_total;
static CC Dr_total;
static CC Dw_total;

static Char* cachegrind_out_file;

static void file_err ( void )
{
   VG_(message)(Vg_UserMsg,
      "error: can't open cache simulation output file `%s'",
      cachegrind_out_file );
   VG_(message)(Vg_UserMsg,
      "       ... so simulation results will be missing.");
}

static void fprint_lineCC(Int fd, lineCC* n)
{
   Char buf[512];
   VG_(sprintf)(buf, "%u %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
                      n->line,
                      n->Ir.a, n->Ir.m1, n->Ir.m2, 
                      n->Dr.a, n->Dr.m1, n->Dr.m2,
                      n->Dw.a, n->Dw.m1, n->Dw.m2);
   VG_(write)(fd, (void*)buf, VG_(strlen)(buf));

   Ir_total.a += n->Ir.a;  Ir_total.m1 += n->Ir.m1;  Ir_total.m2 += n->Ir.m2;
   Dr_total.a += n->Dr.a;  Dr_total.m1 += n->Dr.m1;  Dr_total.m2 += n->Dr.m2;
   Dw_total.a += n->Dw.a;  Dw_total.m1 += n->Dw.m1;  Dw_total.m2 += n->Dw.m2;
}

static void fprint_CC_table_and_calc_totals(void)
{
   Int     fd;
   Char    buf[512];
   fileCC *curr_fileCC;
   fnCC   *curr_fnCC;
   lineCC *curr_lineCC;
   Int     i, j, k;

   VGP_PUSHCC(VgpCacheResults);

   fd = VG_(open)(cachegrind_out_file, VKI_O_CREAT|VKI_O_TRUNC|VKI_O_WRONLY,
                                       VKI_S_IRUSR|VKI_S_IWUSR);
   if (fd < 0) {
      // If the file can't be opened for whatever reason (conflict
      // between multiple cachegrinded processes?), give up now.
      file_err(); 
      return;
   }

   // "desc:" lines (giving I1/D1/L2 cache configuration).  The spaces after
   // the 2nd colon makes cg_annotate's output look nicer.
   VG_(sprintf)(buf, "desc: I1 cache:         %s\n"
                     "desc: D1 cache:         %s\n"
                     "desc: L2 cache:         %s\n",
                     I1.desc_line, D1.desc_line, L2.desc_line);
   VG_(write)(fd, (void*)buf, VG_(strlen)(buf));

   // "cmd:" line
   VG_(strcpy)(buf, "cmd:");
   VG_(write)(fd, (void*)buf, VG_(strlen)(buf));
   for (i = 0; i < VG_(client_argc); i++) {
       VG_(write)(fd, " ", 1);
       VG_(write)(fd, VG_(client_argv)[i], VG_(strlen)(VG_(client_argv)[i]));
   }
   // "events:" line
   VG_(sprintf)(buf, "\nevents: Ir I1mr I2mr Dr D1mr D2mr Dw D1mw D2mw\n");
   VG_(write)(fd, (void*)buf, VG_(strlen)(buf));

   // Six loops here:  three for the hash table arrays, and three for the
   // chains hanging off the hash table arrays.
   for (i = 0; i < N_FILE_ENTRIES; i++) {
      curr_fileCC = CC_table[i];
      while (curr_fileCC != NULL) {
         VG_(sprintf)(buf, "fl=%s\n", curr_fileCC->file);
         VG_(write)(fd, (void*)buf, VG_(strlen)(buf));

         for (j = 0; j < N_FN_ENTRIES; j++) {
            curr_fnCC = curr_fileCC->fns[j];
            while (curr_fnCC != NULL) {
               VG_(sprintf)(buf, "fn=%s\n", curr_fnCC->fn);
               VG_(write)(fd, (void*)buf, VG_(strlen)(buf));

               for (k = 0; k < N_LINE_ENTRIES; k++) {
                  curr_lineCC = curr_fnCC->lines[k];
                  while (curr_lineCC != NULL) {
                     fprint_lineCC(fd, curr_lineCC);
                     curr_lineCC = curr_lineCC->next;
                  }
               }
               curr_fnCC = curr_fnCC->next;
            }
         }
         curr_fileCC = curr_fileCC->next;
      }
   }

   // Summary stats must come after rest of table, since we calculate them
   // during traversal.  */ 
   VG_(sprintf)(buf, "summary: "
                     "%llu %llu %llu %llu %llu %llu %llu %llu %llu\n", 
                     Ir_total.a, Ir_total.m1, Ir_total.m2,
                     Dr_total.a, Dr_total.m1, Dr_total.m2,
                     Dw_total.a, Dw_total.m1, Dw_total.m2);
   VG_(write)(fd, (void*)buf, VG_(strlen)(buf));
   VG_(close)(fd);
}

static UInt ULong_width(ULong n)
{
   UInt w = 0;
   while (n > 0) {
      n = n / 10;
      w++;
   }
   return w + (w-1)/3;   // add space for commas
}

static
void percentify(Int n, Int ex, Int field_width, char buf[]) 
{
   int i, len, space;
    
   VG_(sprintf)(buf, "%d.%d%%", n / ex, n % ex);
   len = VG_(strlen)(buf);
   space = field_width - len;
   if (space < 0) space = 0;     /* Allow for v. small field_width */
   i = len;

   /* Right justify in field */
   for (     ; i >= 0;    i--)  buf[i + space] = buf[i];
   for (i = 0; i < space; i++)  buf[i] = ' ';
}

void TL_(fini)(Int exitcode)
{
   static char buf1[128], buf2[128], buf3[128], fmt [128];

   CC D_total;
   ULong L2_total_m, L2_total_mr, L2_total_mw,
         L2_total, L2_total_r, L2_total_w;
   Int l1, l2, l3;
   Int p;

   fprint_CC_table_and_calc_totals();

   if (VG_(clo_verbosity) == 0) 
      return;

   /* I cache results.  Use the I_refs value to determine the first column
    * width. */
   l1 = ULong_width(Ir_total.a);
   l2 = ULong_width(Dr_total.a);
   l3 = ULong_width(Dw_total.a);

   /* Make format string, getting width right for numbers */
   VG_(sprintf)(fmt, "%%s %%,%dld", l1);
   
   VG_(message)(Vg_UserMsg, fmt, "I   refs:     ", Ir_total.a);
   VG_(message)(Vg_UserMsg, fmt, "I1  misses:   ", Ir_total.m1);
   VG_(message)(Vg_UserMsg, fmt, "L2i misses:   ", Ir_total.m2);

   p = 100;

   if (0 == Ir_total.a) Ir_total.a = 1;
   percentify(Ir_total.m1 * 100 * p / Ir_total.a, p, l1+1, buf1);
   VG_(message)(Vg_UserMsg, "I1  miss rate: %s", buf1);
                
   percentify(Ir_total.m2 * 100 * p / Ir_total.a, p, l1+1, buf1);
   VG_(message)(Vg_UserMsg, "L2i miss rate: %s", buf1);
   VG_(message)(Vg_UserMsg, "");

   /* D cache results.  Use the D_refs.rd and D_refs.wr values to determine the
    * width of columns 2 & 3. */
   D_total.a  = Dr_total.a  + Dw_total.a;
   D_total.m1 = Dr_total.m1 + Dw_total.m1;
   D_total.m2 = Dr_total.m2 + Dw_total.m2;
       
   /* Make format string, getting width right for numbers */
   VG_(sprintf)(fmt, "%%s %%,%dld  (%%,%dld rd + %%,%dld wr)", l1, l2, l3);

   VG_(message)(Vg_UserMsg, fmt, "D   refs:     ", 
                            D_total.a, Dr_total.a, Dw_total.a);
   VG_(message)(Vg_UserMsg, fmt, "D1  misses:   ",
                            D_total.m1, Dr_total.m1, Dw_total.m1);
   VG_(message)(Vg_UserMsg, fmt, "L2d misses:   ",
                            D_total.m2, Dr_total.m2, Dw_total.m2);

   p = 10;
   
   if (0 == D_total.a)   D_total.a = 1;
   if (0 == Dr_total.a) Dr_total.a = 1;
   if (0 == Dw_total.a) Dw_total.a = 1;
   percentify( D_total.m1 * 100 * p / D_total.a,  p, l1+1, buf1);
   percentify(Dr_total.m1 * 100 * p / Dr_total.a, p, l2+1, buf2);
   percentify(Dw_total.m1 * 100 * p / Dw_total.a, p, l3+1, buf3);
   VG_(message)(Vg_UserMsg, "D1  miss rate: %s (%s   + %s  )", buf1, buf2,buf3);

   percentify( D_total.m2 * 100 * p / D_total.a,  p, l1+1, buf1);
   percentify(Dr_total.m2 * 100 * p / Dr_total.a, p, l2+1, buf2);
   percentify(Dw_total.m2 * 100 * p / Dw_total.a, p, l3+1, buf3);
   VG_(message)(Vg_UserMsg, "L2d miss rate: %s (%s   + %s  )", buf1, buf2,buf3);
   VG_(message)(Vg_UserMsg, "");

   /* L2 overall results */

   L2_total   = Dr_total.m1 + Dw_total.m1 + Ir_total.m1;
   L2_total_r = Dr_total.m1 + Ir_total.m1;
   L2_total_w = Dw_total.m1;
   VG_(message)(Vg_UserMsg, fmt, "L2 refs:      ",
                            L2_total, L2_total_r, L2_total_w);

   L2_total_m  = Dr_total.m2 + Dw_total.m2 + Ir_total.m2;
   L2_total_mr = Dr_total.m2 + Ir_total.m2;
   L2_total_mw = Dw_total.m2;
   VG_(message)(Vg_UserMsg, fmt, "L2 misses:    ",
                            L2_total_m, L2_total_mr, L2_total_mw);

   percentify(L2_total_m  * 100 * p / (Ir_total.a + D_total.a),  p, l1+1, buf1);
   percentify(L2_total_mr * 100 * p / (Ir_total.a + Dr_total.a), p, l2+1, buf2);
   percentify(L2_total_mw * 100 * p / Dw_total.a, p, l3+1, buf3);
   VG_(message)(Vg_UserMsg, "L2 miss rate:  %s (%s   + %s  )", buf1, buf2,buf3);
            

   // Various stats
   if (VG_(clo_verbosity) > 1) {
       int BB_lookups = full_debug_BBs      + fn_debug_BBs +
                        file_line_debug_BBs + no_debug_BBs;
      
       VG_(message)(Vg_DebugMsg, "");
       VG_(message)(Vg_DebugMsg, "Distinct files:   %d", distinct_files);
       VG_(message)(Vg_DebugMsg, "Distinct fns:     %d", distinct_fns);
       VG_(message)(Vg_DebugMsg, "Distinct lines:   %d", distinct_lines);
       VG_(message)(Vg_DebugMsg, "Distinct instrs:  %d", distinct_instrs);
       VG_(message)(Vg_DebugMsg, "BB lookups:       %d", BB_lookups);
       VG_(message)(Vg_DebugMsg, "With full      debug info:%3d%% (%d)", 
                    full_debug_BBs    * 100 / BB_lookups,
                    full_debug_BBs);
       VG_(message)(Vg_DebugMsg, "With file/line debug info:%3d%% (%d)", 
                    file_line_debug_BBs * 100 / BB_lookups,
                    file_line_debug_BBs);
       VG_(message)(Vg_DebugMsg, "With fn name   debug info:%3d%% (%d)", 
                    fn_debug_BBs * 100 / BB_lookups,
                    fn_debug_BBs);
       VG_(message)(Vg_DebugMsg, "With no        debug info:%3d%% (%d)", 
                    no_debug_BBs      * 100 / BB_lookups,
                    no_debug_BBs);
       VG_(message)(Vg_DebugMsg, "BBs Retranslated: %d", BB_retranslations);
   }
   VGP_POPCC(VgpCacheResults);
}

/*--------------------------------------------------------------------*/
/*--- Discarding BB info                                           ---*/
/*--------------------------------------------------------------------*/

// Called when a translation is invalidated due to code unloading.
void TL_(discard_basic_block_info) ( Addr a, SizeT size )
{
   VgHashNode** prev_next_ptr;
   VgHashNode*  bb_info;

   if (0) VG_(printf)( "discard_basic_block_info: %p, %llu\n", a, (ULong)size);

   // Get BB info, remove from table, free BB info.  Simple!
   bb_info = VG_(HT_get_node)(instr_info_table, a, &prev_next_ptr);
   tl_assert(NULL != bb_info);
   *prev_next_ptr = bb_info->next;
   VG_(free)(bb_info);
}

/*--------------------------------------------------------------------*/
/*--- Command line processing                                      ---*/
/*--------------------------------------------------------------------*/

static void parse_cache_opt ( cache_t* cache, char* opt )
{
   int   i = 0, i2, i3;

   // Option argument looks like "65536,2,64".
   // Find commas, replace with NULs to make three independent 
   // strings, then extract numbers, put NULs back.  Yuck.
   while (VG_(isdigit)(opt[i])) i++;
   if (',' == opt[i]) {
      opt[i++] = '\0';
      i2 = i;
   } else goto bad;
   while (VG_(isdigit)(opt[i])) i++;
   if (',' == opt[i]) {
      opt[i++] = '\0';
      i3 = i;
   } else goto bad;
   while (VG_(isdigit)(opt[i])) i++;
   if ('\0' != opt[i]) goto bad;

   cache->size      = (Int)VG_(atoll)(opt);
   cache->assoc     = (Int)VG_(atoll)(opt + i2);
   cache->line_size = (Int)VG_(atoll)(opt + i3);

   opt[i2-1] = ',';
   opt[i3-1] = ',';
   return;

  bad:
   VG_(bad_option)(opt);
}

Bool TL_(process_cmd_line_option)(Char* arg)
{
   // 5 is length of "--I1="
   if      (VG_CLO_STREQN(5, arg, "--I1="))
      parse_cache_opt(&clo_I1_cache, &arg[5]);
   else if (VG_CLO_STREQN(5, arg, "--D1="))
      parse_cache_opt(&clo_D1_cache, &arg[5]);
   else if (VG_CLO_STREQN(5, arg, "--L2="))
      parse_cache_opt(&clo_L2_cache, &arg[5]);
   else
      return False;

   return True;
}

void TL_(print_usage)(void)
{
   VG_(printf)(
"    --I1=<size>,<assoc>,<line_size>  set I1 cache manually\n"
"    --D1=<size>,<assoc>,<line_size>  set D1 cache manually\n"
"    --L2=<size>,<assoc>,<line_size>  set L2 cache manually\n"
   );
}

void TL_(print_debug_usage)(void)
{
   VG_(printf)(
"    (none)\n"
   );
}

/*--------------------------------------------------------------------*/
/*--- Setup                                                        ---*/
/*--------------------------------------------------------------------*/

void TL_(pre_clo_init)(void)
{
   Char* base_dir = NULL;

   VG_(details_name)            ("Cachegrind");
   VG_(details_version)         (NULL);
   VG_(details_description)     ("an I1/D1/L2 cache profiler");
   VG_(details_copyright_author)(
      "Copyright (C) 2002-2004, and GNU GPL'd, by Nicholas Nethercote et al.");
   VG_(details_bug_reports_to)  (VG_BUGS_TO);
   VG_(details_avg_translation_sizeB) ( 155 );

   VG_(needs_basic_block_discards)();
   VG_(needs_command_line_options)();

   /* Get working directory */
   tl_assert( VG_(getcwd_alloc)(&base_dir) );

   /* Block is big enough for dir name + cachegrind.out.<pid> */
   cachegrind_out_file = VG_(malloc)((VG_(strlen)(base_dir) + 32)*sizeof(Char));
   VG_(sprintf)(cachegrind_out_file, "%s/cachegrind.out.%d",
                base_dir, VG_(getpid)());
   VG_(free)(base_dir);

   instr_info_table = VG_(HT_construct)();
}

void TL_(post_clo_init)(void)
{
   cache_t I1c, D1c, L2c; 

   configure_caches(&I1c, &D1c, &L2c);

   cachesim_I1_initcache(I1c);
   cachesim_D1_initcache(D1c);
   cachesim_L2_initcache(L2c);

   VGP_(register_profile_event)(VgpGetLineCC,     "get-lineCC");
   VGP_(register_profile_event)(VgpCacheSimulate, "cache-simulate");
   VGP_(register_profile_event)(VgpCacheResults,  "cache-results");
}

VG_DETERMINE_INTERFACE_VERSION(TL_(pre_clo_init), 0)

/*--------------------------------------------------------------------*/
/*--- end                                                cg_main.c ---*/
/*--------------------------------------------------------------------*/
