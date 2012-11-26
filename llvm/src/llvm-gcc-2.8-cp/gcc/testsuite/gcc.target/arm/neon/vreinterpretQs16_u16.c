/* APPLE LOCAL file v7 merge */
/* Test the `vreinterpretQs16_u16' ARM Neon intrinsic.  */
/* This file was autogenerated by neon-testgen.  */

/* { dg-do assemble } */
/* { dg-require-effective-target arm_neon_ok } */
/* { dg-options "-save-temps -O0 -mfpu=neon -mfloat-abi=softfp" } */

#include "arm_neon.h"

void test_vreinterpretQs16_u16 (void)
{
  int16x8_t out_int16x8_t;
  uint16x8_t arg0_uint16x8_t;

  out_int16x8_t = vreinterpretq_s16_u16 (arg0_uint16x8_t);
}

/* { dg-final { cleanup-saved-temps } } */
