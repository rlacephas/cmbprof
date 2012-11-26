/* APPLE LOCAL file v7 merge */
/* Test the `vget_lanef32' ARM Neon intrinsic.  */
/* This file was autogenerated by neon-testgen.  */

/* { dg-do assemble } */
/* { dg-require-effective-target arm_neon_ok } */
/* { dg-options "-save-temps -O0 -mfpu=neon -mfloat-abi=softfp" } */

#include "arm_neon.h"

void test_vget_lanef32 (void)
{
  float32_t out_float32_t;
  float32x2_t arg0_float32x2_t;

  out_float32_t = vget_lane_f32 (arg0_float32x2_t, 1);
}

/* { dg-final { scan-assembler "vmov\.f32\[ 	\]+\[rR\]\[0-9\]+, \[dD\]\[0-9\]+\\\[\[0-9\]+\\\]!?\(\[ 	\]+@\[a-zA-Z0-9 \]+\)?\n" } } */
/* { dg-final { cleanup-saved-temps } } */
