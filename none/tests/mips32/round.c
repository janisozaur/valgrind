#include <stdio.h>

typedef enum {
   CEILWS=0, CEILWD,
   FLOORWS, FLOORWD,
   ROUNDWS, ROUNDWD,
   TRUNCWS, TRUNCWD
} flt_dir_op_t;

typedef enum {
   CVTDS, CVTDW,
   CVTSD, CVTSW,
   CVTWS, CVTWD
} flt_round_op_t;

typedef enum {
   TO_NEAREST=0, TO_ZERO, TO_PLUS_INFINITY, TO_MINUS_INFINITY } round_mode_t;
char *round_mode_name[] = { "near", "zero", "+inf", "-inf" };


const char *flt_dir_op_names[] = {
   "ceil.w.s", "ceil.w.d",
   "floor.w.s", "floor.w.d",
   "round.w.s", "round.w.d",
   "trunc.w.s", "trunc.w.d"
};

const char *flt_round_op_names[] = {
   "cvt.d.s", "cvt.d.w",
   "cvt.s.d", "cvt.s.w",
   "cvt.w.s", "cvt.w.d"
};

const double fs_d[] = {
   0, 456.2489562, 3, -1,
   1384.6, -7.2945676, 1000000000, -5786.47,
   1752, 0.0024575, 0.00000001, -248562.76,
   -45786.476, 456.2489562, 34.00046, 45786.476,
   1752065, 107, -45667.24, -7.2945676,
   -347856.475, 356047.56, -1.0, 23.04,
};

const float fs_f[] = {
   0, 456.2489562, 3, -1,
   1384.6, -7.2945676, 1000000000, -5786.47,
   1752, 0.0024575, 0.00000001, -248562.76,
   -45786.476, 456.2489562, 34.00046, 45786.476,
   1752065, 107, -45667.24, -7.2945676,
   -347856.475, 356047.56, -1.0, 23.04,
};

const int fs_w[] = {
   0, 456, 3, -1,
   0xffffffff, 356, 1000000000, -5786,
   1752, 24575, 10, -248562,
   -45786, 456, 34, 45786,
   1752065, 107, -45667, -7,
   -347856, 0x80000000, 0xFFFFFFF, 23,
};

#define BINOP(op) \
        __asm__ volatile( \
					op" %0, %1, %2\n\t" \
					: "=f"(fd) : "f"(f) , "f"(fB));

#define UNOPdd(op) \
        fd_d = 0;  \
        __asm__ volatile( \
					op" %0, %1\n\t" \
					: "=f"(fd_d) : "f"(fs_d[i]));

#define UNOPff(op) \
        fd_f = 0;  \
        __asm__ volatile( \
					op" %0, %1\n\t" \
					: "=f"(fd_f) : "f"(fs_f[i]));

#define UNOPfd(op) \
        fd_d = 0;  \
        __asm__ volatile( \
					op" %0, %1\n\t" \
					: "=f"(fd_d) : "f"(fs_f[i]));

#define UNOPdf(op) \
        fd_f = 0;  \
        __asm__ volatile( \
					op" %0, %1\n\t" \
					: "=f"(fd_f) : "f"(fs_d[i]));

#define UNOPfw(op) \
        fd_w = 0;  \
        __asm__ volatile( \
					op" $f0, %1\n\t" \
					"mfc1 %0, $f0\n\t" \
					: "=r"(fd_w) : "f"(fs_f[i]) \
					: "$f0");

#define UNOPdw(op) \
        fd_w = 0;  \
        __asm__ volatile( \
					op" $f0, %1\n\t" \
					"mfc1 %0, $f0\n\t" \
					: "=r"(fd_w) : "f"(fs_d[i]) \
					: "$f0");

#define UNOPwd(op) \
        fd_d = 0;  \
        __asm__ volatile( \
                    "mtc1 %1, $f0\n\t" \
					op" %0, $f0\n\t" \
					: "=f"(fd_d) : "r"(fs_w[i]) \
					: "$f0", "$f1");

#define UNOPwf(op) \
        fd_f = 0;  \
        __asm__ volatile( \
                    "mtc1 %1, $f0\n\t" \
					op" %0, $f0\n\t" \
					: "=f"(fd_f) : "r"(fs_w[i]) \
					: "$f0");

void set_rounding_mode(round_mode_t mode)
{
	switch(mode) {
	case TO_NEAREST:
		__asm__ volatile("cfc1 $t0, $31\n\t"
		             "srl $t0, 2\n\t"
		             "sll $t0, 2\n\t"
		             "ctc1 $t0, $31\n\t");
		             
		break;
	case TO_ZERO:
		__asm__ volatile("cfc1 $t0, $31\n\t"
		             "srl $t0, 2\n\t"
		             "sll $t0, 2\n\t"
		             "addiu $t0, 1\n\t"
		             "ctc1 $t0, $31\n\t");
		break;
	case TO_PLUS_INFINITY:
		__asm__ volatile("cfc1 $t0, $31\n\t"
		             "srl $t0, 2\n\t"
		             "sll $t0, 2\n\t"
		             "addiu $t0, 2\n\t"
		             "ctc1 $t0, $31\n\t");
		break;
	case TO_MINUS_INFINITY:
		__asm__ volatile("cfc1 $t0, $31\n\t"
		             "srl $t0, 2\n\t"
		             "sll $t0, 2\n\t"
		             "addiu $t0, 3\n\t"
		             "ctc1 $t0, $31\n\t");
		break;
	}
}

int directedRoundingMode(flt_dir_op_t op) {
   int fd_w = 0;
   int i;
   for (i = 0; i < 24; i++) {
      switch(op) {
         case CEILWS:
              UNOPfw("ceil.w.s");
              printf("%s %d %f\n", flt_dir_op_names[op], fd_w, fs_f[i]);
              break;
         case CEILWD:
              UNOPdw("ceil.w.d");
              printf("%s %d %lf\n", flt_dir_op_names[op], fd_w, fs_d[i]);
              break;
         case FLOORWS:
              UNOPfw("floor.w.s");
              printf("%s %d %f\n", flt_dir_op_names[op], fd_w, fs_f[i]);
              break;
         case FLOORWD:
              UNOPdw("floor.w.d");
              printf("%s %d %lf\n", flt_dir_op_names[op], fd_w, fs_d[i]);
              break;
         case ROUNDWS:
              UNOPfw("round.w.s");
              printf("%s %d %f\n", flt_dir_op_names[op], fd_w, fs_f[i]);
              break;
         case ROUNDWD:
              UNOPdw("round.w.d");
              printf("%s %d %lf\n", flt_dir_op_names[op], fd_w, fs_d[i]);
              break;
         case TRUNCWS:
              UNOPfw("trunc.w.s");
              printf("%s %d %f\n", flt_dir_op_names[op], fd_w, fs_f[i]);
              break;
         case TRUNCWD:
              UNOPdw("trunc.w.d");
              printf("%s %d %lf\n", flt_dir_op_names[op], fd_w, fs_d[i]);
              break;
        default:
            printf("error\n");
            break;
        }
    }
   return 0;
}

int FCSRRoundingMode(flt_round_op_t op1) 
{
   double fd_d = 0;
   float fd_f = 0;
   int fd_w = 0;
   int i;
   round_mode_t rm;
   for (rm = TO_NEAREST; rm <= TO_MINUS_INFINITY; rm ++)
   { 
      set_rounding_mode(rm);
      printf("roundig mode: %s\n", round_mode_name[rm]);
      for (i = 0; i < 24; i++)
      {
         set_rounding_mode(rm);
         switch(op1) {
            case CVTDS:
                 UNOPfd("cvt.d.s");
                 printf("%s %lf %lf\n", flt_round_op_names[op1], fd_d, fs_f[i]);
                 break;
            case CVTDW:
                 UNOPwd("cvt.d.w");
                 printf("%s %lf %d\n", flt_round_op_names[op1], fd_d, fs_w[i]);
                 break;
            case CVTSD:
                 UNOPdf("cvt.s.d");
                 printf("%s %f %lf\n", flt_round_op_names[op1], fd_f, fs_d[i]);
                 break;
            case CVTSW:
                 UNOPwf("cvt.s.w");
                 printf("%s %f %d\n", flt_round_op_names[op1], fd_f, fs_w[i]);
                 break;
            case CVTWS:
                 UNOPfw("cvt.w.s");
                 printf("%s %d %f\n", flt_round_op_names[op1], fd_w, fs_f[i]);
                 break;
            case CVTWD:
                 UNOPdw("cvt.w.d");
                 printf("%s %d %lf\n", flt_round_op_names[op1], fd_w, fs_d[i]);
                 break;
            default:
                 printf("error\n");
                 break;
         }
      }
   }
   return 0;
}

int main()
{
   flt_dir_op_t op;
   flt_round_op_t op1;

   printf("-------------------------- %s --------------------------\n",
        "test FPU Conversion Operations Using a Directed Rounding Mode");
   for (op = CEILWS; op <= TRUNCWD; op++) {
      directedRoundingMode(op);
   }
   
   printf("-------------------------- %s --------------------------\n",
        "test FPU Conversion Operations Using the FCSR Rounding Mode");
   for (op1 = CVTDS; op1 <= CVTWD; op1++) {
      FCSRRoundingMode(op1);
   }
   return 0;
}

