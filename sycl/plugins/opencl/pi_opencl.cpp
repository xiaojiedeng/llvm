//==---------- pi_opencl.cpp - OpenCL Plugin -------------------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "CL/opencl.h"
#include <CL/sycl/detail/pi.h>

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

#define CHECK_ERR_SET_NULL_RET(err, ptr, reterr)                               \
  if (err != CL_SUCCESS) {                                                     \
    if (ptr != nullptr)                                                        \
      *ptr = nullptr;                                                          \
    return cast<pi_result>(reterr);                                            \
  }

// Want all the needed casts be explicit, do not define conversion operators.
template <class To, class From> To cast(From value) {
  // TODO: see if more sanity checks are possible.
  static_assert(sizeof(From) == sizeof(To), "cast failed size check");
  return (To)(value);
}

extern "C" {

// Convenience macro makes source code search easier
#define OCL(pi_api) Ocl##pi_api

// Example of a PI interface that does not map exactly to an OpenCL one.
pi_result OCL(piPlatformsGet)(pi_uint32 num_entries, pi_platform *platforms,
                              pi_uint32 *num_platforms) {
  cl_int result = clGetPlatformIDs(cast<cl_uint>(num_entries),
                                   cast<cl_platform_id *>(platforms),
                                   cast<cl_uint *>(num_platforms));

  // Absorb the CL_PLATFORM_NOT_FOUND_KHR and just return 0 in num_platforms
  if (result == CL_PLATFORM_NOT_FOUND_KHR) {
    assert(num_platforms != 0);
    *num_platforms = 0;
    result = PI_SUCCESS;
  }
  return static_cast<pi_result>(result);
}

// Example of a PI interface that does not map exactly to an OpenCL one.
pi_result OCL(piDevicesGet)(pi_platform platform, pi_device_type device_type,
                            pi_uint32 num_entries, pi_device *devices,
                            pi_uint32 *num_devices) {
  cl_int result = clGetDeviceIDs(
      cast<cl_platform_id>(platform), cast<cl_device_type>(device_type),
      cast<cl_uint>(num_entries), cast<cl_device_id *>(devices),
      cast<cl_uint *>(num_devices));

  // Absorb the CL_DEVICE_NOT_FOUND and just return 0 in num_devices
  if (result == CL_DEVICE_NOT_FOUND) {
    assert(num_devices != 0);
    *num_devices = 0;
    result = PI_SUCCESS;
  }
  return cast<pi_result>(result);
}

pi_result OCL(piextDeviceSelectBinary)(
    pi_device device, // TODO: does this need to be context?
    pi_device_binary *images, pi_uint32 num_images,
    pi_device_binary *selected_image) {

  // TODO: this is a bare-bones implementation for choosing a device image
  // that would be compatible with the targeted device. An AOT-compiled
  // image is preferred over SPIRV for known devices (i.e. Intel devices)
  // The implementation makes no effort to differentiate between multiple images
  // for the given device, and simply picks the first one compatible
  // Real implementaion will use the same mechanism OpenCL ICD dispatcher
  // uses. Something like:
  //   PI_VALIDATE_HANDLE_RETURN_HANDLE(ctx, PI_INVALID_CONTEXT);
  //     return context->dispatch->piextDeviceSelectIR(
  //       ctx, images, num_images, selected_image);
  // where context->dispatch is set to the dispatch table provided by PI
  // plugin for platform/device the ctx was created for.

  // Choose the binary target for the provided device
  const char *image_target = nullptr;
  // Get the type of the device
  cl_device_type device_type;
  cl_int ret_err =
      clGetDeviceInfo(cast<cl_device_id>(device), CL_DEVICE_TYPE,
                      sizeof(cl_device_type), &device_type, nullptr);
  if (ret_err != CL_SUCCESS) {
    *selected_image = nullptr;
    return cast<pi_result>(ret_err);
  }

  switch (device_type) {
    // TODO: Factor out vendor specifics into a separate source
    // E.g. sycl/source/detail/vendor/intel/detail/pi_opencl.cpp?

    // We'll attempt to find an image that was AOT-compiled
    // from a SPIR-V image into an image specific for:

  case CL_DEVICE_TYPE_CPU: // OpenCL 64-bit CPU
    image_target = PI_DEVICE_BINARY_TARGET_SPIRV64_X86_64;
    break;
  case CL_DEVICE_TYPE_GPU: // OpenCL 64-bit GEN GPU
    image_target = PI_DEVICE_BINARY_TARGET_SPIRV64_GEN;
    break;
  case CL_DEVICE_TYPE_ACCELERATOR: // OpenCL 64-bit FPGA
    image_target = PI_DEVICE_BINARY_TARGET_SPIRV64_FPGA;
    break;
  default:
    // Otherwise, we'll attempt to find and JIT-compile
    // a device-independent SPIR-V image
    image_target = PI_DEVICE_BINARY_TARGET_SPIRV64;
    break;
  }

  // Find the appropriate device image, fallback to spirv if not found
  pi_device_binary fallback = nullptr;
  for (size_t i = 0; i < num_images; ++i) {
    if (strcmp(images[i]->DeviceTargetSpec, image_target) == 0) {
      *selected_image = images[i];
      return PI_SUCCESS;
    }
    if (strcmp(images[i]->DeviceTargetSpec, PI_DEVICE_BINARY_TARGET_SPIRV64) ==
        0)
      fallback = images[i];
  }
  // Points to a spirv image, if such indeed was found
  if ((*selected_image = fallback))
    return PI_SUCCESS;
  // No image can be loaded for the given device
  return PI_INVALID_BINARY;
}

pi_result OCL(piQueueCreate)(pi_context context, pi_device device,
                             pi_queue_properties properties, pi_queue *queue) {
  assert(queue && "piQueueCreate failed, queue argument is null");

  cl_platform_id curPlatform;
  cl_int ret_err =
      clGetDeviceInfo(cast<cl_device_id>(device), CL_DEVICE_PLATFORM,
                      sizeof(cl_platform_id), &curPlatform, nullptr);

  CHECK_ERR_SET_NULL_RET(ret_err, queue, ret_err);

  size_t platVerSize;
  ret_err = clGetPlatformInfo(curPlatform, CL_PLATFORM_VERSION, 0, nullptr,
                              &platVerSize);

  CHECK_ERR_SET_NULL_RET(ret_err, queue, ret_err);

  std::string platVer(platVerSize, '\0');
  ret_err = clGetPlatformInfo(curPlatform, CL_PLATFORM_VERSION, platVerSize,
                              &platVer.front(), nullptr);

  CHECK_ERR_SET_NULL_RET(ret_err, queue, ret_err);

  if (platVer.find("OpenCL 1.0") != std::string::npos ||
      platVer.find("OpenCL 1.1") != std::string::npos ||
      platVer.find("OpenCL 1.2") != std::string::npos) {
    *queue = cast<pi_queue>(clCreateCommandQueue(
        cast<cl_context>(context), cast<cl_device_id>(device),
        cast<cl_command_queue_properties>(properties), &ret_err));
    return cast<pi_result>(ret_err);
  }

  cl_queue_properties CreationFlagProperties[] = {
      CL_QUEUE_PROPERTIES, cast<cl_command_queue_properties>(properties), 0};
  *queue = cast<pi_queue>(clCreateCommandQueueWithProperties(
      cast<cl_context>(context), cast<cl_device_id>(device),
      CreationFlagProperties, &ret_err));
  return cast<pi_result>(ret_err);
}

pi_result OCL(piProgramCreate)(pi_context context, const void *il,
                               size_t length, pi_program *res_program) {

  size_t deviceCount;

  cl_int ret_err = clGetContextInfo(
      cast<cl_context>(context), CL_CONTEXT_DEVICES, 0, nullptr, &deviceCount);

  std::vector<cl_device_id> devicesInCtx(deviceCount);

  if (ret_err != CL_SUCCESS || deviceCount < 1) {
    if (res_program != nullptr)
      *res_program = nullptr;
    return cast<pi_result>(CL_INVALID_CONTEXT);
  }

  ret_err = clGetContextInfo(cast<cl_context>(context), CL_CONTEXT_DEVICES,
                             deviceCount * sizeof(cl_device_id),
                             devicesInCtx.data(), nullptr);

  CHECK_ERR_SET_NULL_RET(ret_err, res_program, CL_INVALID_CONTEXT);

  cl_platform_id curPlatform;
  ret_err = clGetDeviceInfo(devicesInCtx[0], CL_DEVICE_PLATFORM,
                            sizeof(cl_platform_id), &curPlatform, nullptr);

  CHECK_ERR_SET_NULL_RET(ret_err, res_program, CL_INVALID_CONTEXT);

  size_t devVerSize;
  ret_err = clGetPlatformInfo(curPlatform, CL_PLATFORM_VERSION, 0, nullptr,
                              &devVerSize);
  std::string devVer(devVerSize, '\0');
  ret_err = clGetPlatformInfo(curPlatform, CL_PLATFORM_VERSION, devVerSize,
                              &devVer.front(), nullptr);

  CHECK_ERR_SET_NULL_RET(ret_err, res_program, CL_INVALID_CONTEXT);

  pi_result err = PI_SUCCESS;
  if (devVer.find("OpenCL 1.0") == std::string::npos &&
      devVer.find("OpenCL 1.1") == std::string::npos &&
      devVer.find("OpenCL 1.2") == std::string::npos &&
      devVer.find("OpenCL 2.0") == std::string::npos) {
    if (res_program != nullptr)
      *res_program = cast<pi_program>(clCreateProgramWithIL(
          cast<cl_context>(context), il, length, cast<cl_int *>(&err)));
    return err;
  }

  size_t extSize;
  ret_err = clGetPlatformInfo(curPlatform, CL_PLATFORM_EXTENSIONS, 0, nullptr,
                              &extSize);
  std::string extStr(extSize, '\0');
  ret_err = clGetPlatformInfo(curPlatform, CL_PLATFORM_EXTENSIONS, extSize,
                              &extStr.front(), nullptr);

  if (ret_err != CL_SUCCESS ||
      extStr.find("cl_khr_il_program") == std::string::npos) {
    if (res_program != nullptr)
      *res_program = nullptr;
    return cast<pi_result>(CL_INVALID_CONTEXT);
  }

  using apiFuncT =
      cl_program(CL_API_CALL *)(cl_context, const void *, size_t, cl_int *);
  apiFuncT funcPtr =
      reinterpret_cast<apiFuncT>(clGetExtensionFunctionAddressForPlatform(
          curPlatform, "clCreateProgramWithILKHR"));

  assert(funcPtr != nullptr);
  if (res_program != nullptr)
    *res_program = cast<pi_program>(
        funcPtr(cast<cl_context>(context), il, length, cast<cl_int *>(&err)));
  else
    err = PI_INVALID_VALUE;

  return err;
}

pi_result OCL(piSamplerCreate)(pi_context context,
                               const pi_sampler_properties *sampler_properties,
                               pi_sampler *result_sampler) {
  // Initialize properties according to OpenCL 2.1 spec.
  pi_result error_code;
  pi_bool normalizedCoords = PI_TRUE;
  pi_sampler_addressing_mode addressingMode = PI_SAMPLER_ADDRESSING_MODE_CLAMP;
  pi_sampler_filter_mode filterMode = PI_SAMPLER_FILTER_MODE_NEAREST;

  // Unpack sampler properties
  for (std::size_t i = 0; sampler_properties && sampler_properties[i] != 0;
       ++i) {
    if (sampler_properties[i] == PI_SAMPLER_INFO_NORMALIZED_COORDS) {
      normalizedCoords = static_cast<pi_bool>(sampler_properties[++i]);
    } else if (sampler_properties[i] == PI_SAMPLER_INFO_ADDRESSING_MODE) {
      addressingMode =
          static_cast<pi_sampler_addressing_mode>(sampler_properties[++i]);
    } else if (sampler_properties[i] == PI_SAMPLER_INFO_FILTER_MODE) {
      filterMode = static_cast<pi_sampler_filter_mode>(sampler_properties[++i]);
    } else {
      assert(false && "Cannot recognize sampler property");
    }
  }

  // Always call OpenCL 1.0 API
  *result_sampler = cast<pi_sampler>(
      clCreateSampler(cast<cl_context>(context), normalizedCoords,
                      addressingMode, filterMode, cast<cl_int *>(&error_code)));
  return error_code;
}

pi_result OCL(piextGetDeviceFunctionPointer)(pi_device device,
                                             pi_program program,
                                             const char *func_name,
                                             pi_uint64 *function_pointer_ret) {
  pi_platform platform;
  cl_int ret_err =
      clGetDeviceInfo(cast<cl_device_id>(device), PI_DEVICE_INFO_PLATFORM,
                      sizeof(platform), &platform, nullptr);

  if (ret_err != CL_SUCCESS) {
    return cast<pi_result>(ret_err);
  }

  using FuncT =
      cl_int(CL_API_CALL *)(cl_device_id, cl_program, const char *, cl_ulong *);

  // TODO: add check that device supports corresponding extension
  FuncT func_ptr =
      reinterpret_cast<FuncT>(clGetExtensionFunctionAddressForPlatform(
          cast<cl_platform_id>(platform), "clGetDeviceFunctionPointerINTEL"));
  // TODO: once we have check that device supports corresponding extension,
  // we can insert an assertion that func_ptr is not nullptr. For now, let's
  // just return an error if failed to query such function
  // assert(
  //     func_ptr != nullptr &&
  //     "Failed to get address of clGetDeviceFunctionPointerINTEL function");

  if (!func_ptr) {
    if (function_pointer_ret)
      *function_pointer_ret = 0;
    return PI_INVALID_DEVICE;
  }

  return cast<pi_result>(func_ptr(cast<cl_device_id>(device),
                                  cast<cl_program>(program), func_name,
                                  function_pointer_ret));
}

pi_result OCL(piContextCreate)(
    const cl_context_properties *properties, // TODO: untie from OpenCL
    pi_uint32 num_devices, const pi_device *devices,
    void (*pfn_notify)(const char *errinfo, const void *private_info, size_t cb,
                       void *user_data1),
    void *user_data, pi_context *retcontext) {
  pi_result ret = PI_INVALID_OPERATION;
  *retcontext = cast<pi_context>(
      clCreateContext(properties, cast<cl_uint>(num_devices),
                      cast<const cl_device_id *>(devices), pfn_notify,
                      user_data, cast<cl_int *>(&ret)));

  return ret;
}

pi_result OCL(piMemBufferCreate)(pi_context context, pi_mem_flags flags,
                                 size_t size, void *host_ptr, pi_mem *ret_mem) {
  pi_result ret_err = PI_INVALID_OPERATION;
  *ret_mem = cast<pi_mem>(clCreateBuffer(cast<cl_context>(context),
                                         cast<cl_mem_flags>(flags), size,
                                         host_ptr, cast<cl_int *>(&ret_err)));

  return ret_err;
}

pi_result OCL(piMemImageCreate)(pi_context context, pi_mem_flags flags,
                                const pi_image_format *image_format,
                                const pi_image_desc *image_desc, void *host_ptr,
                                pi_mem *ret_mem) {
  pi_result ret_err = PI_INVALID_OPERATION;
  *ret_mem = cast<pi_mem>(
      clCreateImage(cast<cl_context>(context), cast<cl_mem_flags>(flags),
                    cast<const cl_image_format *>(image_format),
                    cast<const cl_image_desc *>(image_desc), host_ptr,
                    cast<cl_int *>(&ret_err)));

  return ret_err;
}

pi_result OCL(piMemBufferPartition)(pi_mem buffer, pi_mem_flags flags,
                                    pi_buffer_create_type buffer_create_type,
                                    void *buffer_create_info, pi_mem *ret_mem) {

  pi_result ret_err = PI_INVALID_OPERATION;
  *ret_mem = cast<pi_mem>(
      clCreateSubBuffer(cast<cl_mem>(buffer), cast<cl_mem_flags>(flags),
                        cast<cl_buffer_create_type>(buffer_create_type),
                        buffer_create_info, cast<cl_int *>(&ret_err)));
  return ret_err;
}

pi_result OCL(piclProgramCreateWithSource)(pi_context context, pi_uint32 count,
                                           const char **strings,
                                           const size_t *lengths,
                                           pi_program *ret_program) {

  pi_result ret_err = PI_INVALID_OPERATION;
  *ret_program = cast<pi_program>(
      clCreateProgramWithSource(cast<cl_context>(context), cast<cl_uint>(count),
                                strings, lengths, cast<cl_int *>(&ret_err)));
  return ret_err;
}

pi_result OCL(piclProgramCreateWithBinary)(
    pi_context context, pi_uint32 num_devices, const pi_device *device_list,
    const size_t *lengths, const unsigned char **binaries,
    pi_int32 *binary_status, pi_program *ret_program) {

  pi_result ret_err = PI_INVALID_OPERATION;
  *ret_program = cast<pi_program>(clCreateProgramWithBinary(
      cast<cl_context>(context), cast<cl_uint>(num_devices),
      cast<const cl_device_id *>(device_list), lengths, binaries,
      cast<cl_int *>(binary_status), cast<cl_int *>(&ret_err)));
  return ret_err;
}

pi_result OCL(piProgramLink)(pi_context context, pi_uint32 num_devices,
                             const pi_device *device_list, const char *options,
                             pi_uint32 num_input_programs,
                             const pi_program *input_programs,
                             void (*pfn_notify)(pi_program program,
                                                void *user_data),
                             void *user_data, pi_program *ret_program) {

  pi_result ret_err = PI_INVALID_OPERATION;
  *ret_program = cast<pi_program>(
      clLinkProgram(cast<cl_context>(context), cast<cl_uint>(num_devices),
                    cast<const cl_device_id *>(device_list), options,
                    cast<cl_uint>(num_input_programs),
                    cast<const cl_program *>(input_programs),
                    cast<void (*)(cl_program, void *)>(pfn_notify), user_data,
                    cast<cl_int *>(&ret_err)));
  return ret_err;
}

pi_result OCL(piKernelCreate)(pi_program program, const char *kernel_name,
                              pi_kernel *ret_kernel) {

  pi_result ret_err = PI_INVALID_OPERATION;
  *ret_kernel = cast<pi_kernel>(clCreateKernel(
      cast<cl_program>(program), kernel_name, cast<cl_int *>(&ret_err)));
  return ret_err;
}

pi_result OCL(piEventCreate)(pi_context context, pi_event *ret_event) {

  pi_result ret_err = PI_INVALID_OPERATION;
  *ret_event = cast<pi_event>(
      clCreateUserEvent(cast<cl_context>(context), cast<cl_int *>(&ret_err)));
  return ret_err;
}

pi_result OCL(piEnqueueMemBufferMap)(
    pi_queue command_queue, pi_mem buffer, pi_bool blocking_map,
    cl_map_flags map_flags, // TODO: untie from OpenCL
    size_t offset, size_t size, pi_uint32 num_events_in_wait_list,
    const pi_event *event_wait_list, pi_event *event, void **ret_map) {

  pi_result ret_err = PI_INVALID_OPERATION;
  *ret_map = cast<void *>(clEnqueueMapBuffer(
      cast<cl_command_queue>(command_queue), cast<cl_mem>(buffer),
      cast<cl_bool>(blocking_map), map_flags, offset, size,
      cast<cl_uint>(num_events_in_wait_list),
      cast<const cl_event *>(event_wait_list), cast<cl_event *>(event),
      cast<cl_int *>(&ret_err)));
  return ret_err;
}

// TODO: Remove the 'OclPtr' extension used with the PI_APIs.
// Forward calls to OpenCL RT.
#define _PI_CL(pi_api, ocl_api)                                                \
  decltype(::pi_api) *pi_api##OclPtr = cast<decltype(&::pi_api)>(&ocl_api);

// Platform
_PI_CL(piPlatformsGet, OCL(piPlatformsGet))
_PI_CL(piPlatformGetInfo, clGetPlatformInfo)
// Device
_PI_CL(piDevicesGet, OCL(piDevicesGet))
_PI_CL(piDeviceGetInfo, clGetDeviceInfo)
_PI_CL(piDevicePartition, clCreateSubDevices)
_PI_CL(piDeviceRetain, clRetainDevice)
_PI_CL(piDeviceRelease, clReleaseDevice)
_PI_CL(piextDeviceSelectBinary, OCL(piextDeviceSelectBinary))
_PI_CL(piextGetDeviceFunctionPointer, OCL(piextGetDeviceFunctionPointer))
// Context
_PI_CL(piContextCreate, OCL(piContextCreate))
_PI_CL(piContextGetInfo, clGetContextInfo)
_PI_CL(piContextRetain, clRetainContext)
_PI_CL(piContextRelease, clReleaseContext)
// Queue
_PI_CL(piQueueCreate, OCL(piQueueCreate))
_PI_CL(piQueueGetInfo, clGetCommandQueueInfo)
_PI_CL(piQueueFinish, clFinish)
_PI_CL(piQueueRetain, clRetainCommandQueue)
_PI_CL(piQueueRelease, clReleaseCommandQueue)
// Memory
_PI_CL(piMemBufferCreate, OCL(piMemBufferCreate))
_PI_CL(piMemImageCreate, OCL(piMemImageCreate))
_PI_CL(piMemGetInfo, clGetMemObjectInfo)
_PI_CL(piMemImageGetInfo, clGetImageInfo)
_PI_CL(piMemRetain, clRetainMemObject)
_PI_CL(piMemRelease, clReleaseMemObject)
_PI_CL(piMemBufferPartition, OCL(piMemBufferPartition))
// Program
_PI_CL(piProgramCreate, OCL(piProgramCreate))
_PI_CL(piclProgramCreateWithSource, OCL(piclProgramCreateWithSource))
_PI_CL(piclProgramCreateWithBinary, OCL(piclProgramCreateWithBinary))
_PI_CL(piProgramGetInfo, clGetProgramInfo)
_PI_CL(piProgramCompile, clCompileProgram)
_PI_CL(piProgramBuild, clBuildProgram)
_PI_CL(piProgramLink, OCL(piProgramLink))
_PI_CL(piProgramGetBuildInfo, clGetProgramBuildInfo)
_PI_CL(piProgramRetain, clRetainProgram)
_PI_CL(piProgramRelease, clReleaseProgram)
// Kernel
_PI_CL(piKernelCreate, OCL(piKernelCreate))
_PI_CL(piKernelSetArg, clSetKernelArg)
_PI_CL(piKernelGetInfo, clGetKernelInfo)
_PI_CL(piKernelGetGroupInfo, clGetKernelWorkGroupInfo)
_PI_CL(piKernelGetSubGroupInfo, clGetKernelSubGroupInfo)
_PI_CL(piKernelRetain, clRetainKernel)
_PI_CL(piKernelRelease, clReleaseKernel)
// Event
_PI_CL(piEventCreate, OCL(piEventCreate))
_PI_CL(piEventGetInfo, clGetEventInfo)
_PI_CL(piEventGetProfilingInfo, clGetEventProfilingInfo)
_PI_CL(piEventsWait, clWaitForEvents)
_PI_CL(piEventSetCallback, clSetEventCallback)
_PI_CL(piEventSetStatus, clSetUserEventStatus)
_PI_CL(piEventRetain, clRetainEvent)
_PI_CL(piEventRelease, clReleaseEvent)
// Sampler
_PI_CL(piSamplerCreate, OCL(piSamplerCreate))
_PI_CL(piSamplerGetInfo, clGetSamplerInfo)
_PI_CL(piSamplerRetain, clRetainSampler)
_PI_CL(piSamplerRelease, clReleaseSampler)
// Queue commands
_PI_CL(piEnqueueKernelLaunch, clEnqueueNDRangeKernel)
_PI_CL(piEnqueueNativeKernel, clEnqueueNativeKernel)
_PI_CL(piEnqueueEventsWait, clEnqueueMarkerWithWaitList)
_PI_CL(piEnqueueMemBufferRead, clEnqueueReadBuffer)
_PI_CL(piEnqueueMemBufferReadRect, clEnqueueReadBufferRect)
_PI_CL(piEnqueueMemBufferWrite, clEnqueueWriteBuffer)
_PI_CL(piEnqueueMemBufferWriteRect, clEnqueueWriteBufferRect)
_PI_CL(piEnqueueMemBufferCopy, clEnqueueCopyBuffer)
_PI_CL(piEnqueueMemBufferCopyRect, clEnqueueCopyBufferRect)
_PI_CL(piEnqueueMemBufferFill, clEnqueueFillBuffer)
_PI_CL(piEnqueueMemImageRead, clEnqueueReadImage)
_PI_CL(piEnqueueMemImageWrite, clEnqueueWriteImage)
_PI_CL(piEnqueueMemImageCopy, clEnqueueCopyImage)
_PI_CL(piEnqueueMemImageFill, clEnqueueFillImage)
_PI_CL(piEnqueueMemBufferMap, OCL(piEnqueueMemBufferMap))
_PI_CL(piEnqueueMemUnmap, clEnqueueUnmapMemObject)

#undef _PI_CL
}
