
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Autogenerated by gen-libclc-test.py

// RUN: %clang -emit-llvm -S -o - %s | FileCheck %s

#include <spirv/spirv_types.h>

// CHECK-NOT: declare {{.*}} @_Z
// CHECK-NOT: call {{[^ ]*}} bitcast
__attribute__((overloadable)) __clc_vec3_uint16_t
test___spirv_ocl_vloadn_Rushort3(__clc_size_t args_0,
                                 __clc_uint16_t const __global *args_1) {
  return __spirv_ocl_vloadn_Rushort3(args_0, args_1);
}

__attribute__((overloadable)) __clc_vec3_uint16_t
test___spirv_ocl_vloadn_Rushort3(__clc_size_t args_0,
                                 __clc_uint16_t const __local *args_1) {
  return __spirv_ocl_vloadn_Rushort3(args_0, args_1);
}

__attribute__((overloadable)) __clc_vec3_uint16_t
test___spirv_ocl_vloadn_Rushort3(__clc_size_t args_0,
                                 __clc_uint16_t const __constant *args_1) {
  return __spirv_ocl_vloadn_Rushort3(args_0, args_1);
}

__attribute__((overloadable)) __clc_vec3_uint16_t
test___spirv_ocl_vloadn_Rushort3(__clc_size_t args_0,
                                 __clc_uint16_t const *args_1) {
  return __spirv_ocl_vloadn_Rushort3(args_0, args_1);
}
