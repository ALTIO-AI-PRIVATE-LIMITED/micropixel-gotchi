// SPDX-License-Identifier: MIT
//
// Guest apps have no libm. These are the float helpers hourglass_physics.c
// calls, each one a single WebAssembly instruction.
float sqrtf(float value) { return __builtin_sqrtf(value); }
float fabsf(float value) { return __builtin_fabsf(value); }
float floorf(float value) { return __builtin_floorf(value); }
float ceilf(float value) { return __builtin_ceilf(value); }
float fminf(float a, float b) { return __builtin_fminf(a, b); }
float fmaxf(float a, float b) { return __builtin_fmaxf(a, b); }
