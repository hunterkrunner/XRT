/**
 * Copyright (C) 2016-2017 Xilinx, Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You may
 * not use this file except in compliance with the License. A copy of the
 * License is located at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */

// Copyright 2017 Xilinx, Inc. All rights reserved.

#include "xocl/config.h"
#include "xocl/core/memory.h"
#include "xocl/core/context.h"
#include "xocl/core/device.h"
#include "xrt/util/memory.h"
#include "detail/memory.h"
#include "detail/context.h"

#include <bitset>
#include "plugin/xdp/profile.h"

namespace {

inline void*
get_host_ptr(cl_mem_flags flags, void* host_ptr)
{
  return (flags & CL_MEM_EXT_PTR_XILINX)
    ? reinterpret_cast<cl_mem_ext_ptr_t*>(host_ptr)->obj
    : host_ptr;
}

inline unsigned int
get_xlnx_ext_flags(cl_mem_flags flags, const void* host_ptr)
{
  return (flags & CL_MEM_EXT_PTR_XILINX)
    ? reinterpret_cast<const cl_mem_ext_ptr_t*>(host_ptr)->flags
    : 0;
}

// Hack to determine if a context is associated with exactly one
// device.  Additionally, in emulation mode, the device must be
// active, e.g. loaded through a call to loadBinary.
//
// This works around a problem where clCreateBuffer is called in
// emulation mode before clCreateProgramWithBinary->loadBinary has
// been called.  The call to loadBinary can end up switching the
// device from swEm to hwEm.
//
// In non emulation mode it is sufficient to check that the context
// has only one device.
XRT_UNUSED static xocl::device*
singleContextDevice(cl_context context)
{
  auto device = xocl::xocl(context)->get_device_if_one();
  if (!device)
    return nullptr;

  if (!device->is_active())
    return nullptr;

  // check that all CUs in device has same single mem connectivity
  xocl::device::memidx_bitmask_type ucon;
  for (auto cu : device->get_cu_range())
    ucon |= cu->get_memidx_union();
  if (ucon.count() > 1)
    return nullptr;

  XOCL_DEBUG(std::cout,"context(",xocl::xocl(context)->get_uid(),") has single device single connection\n");
  return device;
}

}

namespace xocl {

static void
validOrError(cl_context   context,
             cl_mem_flags flags,
             size_t       size,
             void *       host_ptr,
             cl_int *     errcode_ret)
{
  if (!config::api_checks())
    return;

  // CL_INVALID_CONTEXT if context is not a valid context
  detail::context::validOrError(context);

  // CL_INVALID_VALUE if values specified in flags are not valid as
  // defined in the table above
  detail::memory::validOrError(flags);

  // CL_INVALID_BUFFER_SIZE if size is 0.
  if (!size)
    throw error(CL_INVALID_BUFFER_SIZE,"size==0");

  // CL_INVALID_HOST_PTR if host_ptr is NULL and CL_MEM_EXT_PTR_XILINX is set
  // In this case host_ptr is actually a ptr to some struct
  //
  // CL_INVALID_HOST_PTR if host_ptr is NULL and CL_MEM_USE_HOST_PTR
  // or CL_MEM_COPY_HOST_PTR are set in flags or if host_ptr is not
  // NULL but CL_MEM_COPY_HOST_PTR or CL_MEM_USE_HOST_PTR are not set
  // in flags.
  //
  // xlnx: CL_INVALID_VALUE if multiple banks are specified
  detail::memory::validHostPtrOrError(flags,host_ptr);
}


static cl_mem
clCreateBuffer(cl_context   context,
               cl_mem_flags flags,
               size_t       size,
               void *       host_ptr,
               cl_int *     errcode_ret)
{
  if (!flags)
    flags = CL_MEM_READ_WRITE;

  validOrError(context,flags,size,host_ptr,errcode_ret);

  // Adjust host_ptr based on ext flags if any
  auto ubuf = get_host_ptr(flags,host_ptr);
  auto buffer = xrt::make_unique<xocl::buffer>(xocl::xocl(context),flags,size,ubuf);

  // set fields in cl_buffer
  buffer->add_ext_flags(get_xlnx_ext_flags(flags,host_ptr));

  // allocate device buffer object if context has only one device
  // and if this is not a progvar (clCreateProgramWithBinary)
  if (!(flags & CL_MEM_PROGVAR)) {
    if (auto device = singleContextDevice(context)) {
      buffer->get_buffer_object(device);
    }
  }

  xocl::assign(errcode_ret,CL_SUCCESS);
  return buffer.release();
}

} // xocl

CL_API_ENTRY cl_mem CL_API_CALL
clCreateBuffer(cl_context   context,
               cl_mem_flags flags,
               size_t       size,
               void *       host_ptr,
               cl_int *     errcode_ret) CL_API_SUFFIX__VERSION_1_0
{
  try {
    PROFILE_LOG_FUNCTION_CALL;
    return xocl::clCreateBuffer
      (context,flags,size,host_ptr,errcode_ret);
  }
  catch (const xrt::error& ex) {
    xocl::send_exception_message(ex.what());
    xocl::assign(errcode_ret,ex.get_code());
  }
  catch (const std::exception& ex) {
    xocl::send_exception_message(ex.what());
    xocl::assign(errcode_ret,CL_OUT_OF_HOST_MEMORY);
  }
  return nullptr;
}
