
// MIT License
//
// Copyright (c) 2024 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

// Configuration constants
#include <type_traits>
#define HIP_TEMPLATE_KERNEL_LAUNCH

// Reduction
#include <hip_utils.hpp>
#include <tmp_utils.hpp>

// HIP API
#include <hip/hip_runtime.h>

// STL
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <execution>
#include <iostream>
#include <iterator>
#include <random>
#include <span>
#include <utility>
#include <vector>

namespace reduction
{
template<typename T, typename F>
class v9
{
public:
    v9(const F&                kernel_op_in,
       const T                 zero_elem_in,
       const std::span<size_t> input_sizes_in,
       const std::span<size_t> block_sizes_in)
        : kernel_op{kernel_op_in}
        , zero_elem{zero_elem_in}
        , input_sizes{input_sizes_in}
        , block_sizes{block_sizes_in}
    {
        // Pessimistically allocate front buffer based on the largest input and the
        // back buffer smallest reduction factor
        auto smallest_factor = *std::min_element(block_sizes_in.begin(), block_sizes_in.end());
        auto largest_size    = *std::max_element(input_sizes.begin(), input_sizes.end());
        HIP_CHECK(hipMalloc((void**)&front, sizeof(T) * largest_size));
        HIP_CHECK(hipMalloc((void**)&back, new_size(smallest_factor, largest_size) * sizeof(T)));
        origi_front = front;
        origi_back  = back;

        hipDeviceProp_t devProp;
        int             device_id = 0;
        HIP_CHECK(hipGetDevice(&device_id));
        HIP_CHECK(hipGetDeviceProperties(&devProp, device_id));
        warp_size = devProp.warpSize;
    }

    ~v9()
    {
        HIP_CHECK(hipFree(front));
        HIP_CHECK(hipFree(back));
    }

    std::tuple<T, std::chrono::duration<float, std::milli>> operator()(
        std::span<const T> input, const std::size_t block_size, const std::size_t items_per_thread)
    {
        auto factor = block_size * items_per_thread;
        HIP_CHECK(hipMemcpy(front, input.data(), input.size() * sizeof(T), hipMemcpyHostToDevice));

        auto kernel_dispatcher = [&](std::size_t step_size)
        {
            tmp::static_switch<std::array{32, 64, 128, 256, 512, 1024}>(
                block_size,
                [&]<int BlockSize>() noexcept
                {
                    tmp::static_switch<std::array{32, 64}>(
                        warp_size,
                        [&]<int WarpSize>() noexcept
                        {
                            tmp::static_switch<std::array{1, 2, 3, 4, 8, 16}>(
                                items_per_thread,
                                [&]<int ItemsPerThread>() noexcept
                                {
                                    hipLaunchKernelGGL(kernel<BlockSize, WarpSize, ItemsPerThread>,
                                                       dim3(new_size(factor, step_size)),
                                                       dim3(BlockSize),
                                                       0,
                                                       hipStreamDefault,
                                                       front,
                                                       back,
                                                       kernel_op,
                                                       zero_elem,
                                                       step_size);
                                });
                        });
                });
        };

        hipEvent_t start, end;

        HIP_CHECK(hipEventCreate(&start));
        HIP_CHECK(hipEventCreate(&end));
        HIP_CHECK(hipEventRecord(start, 0));
        std::size_t curr = input.size();
        while(curr > 1)
        {
            kernel_dispatcher(curr);
            hip::check(hipGetLastError(), "hipKernelLaunchGGL");

            curr = new_size(factor, curr);
            if(curr > 1)
                std::swap(front, back);
        }
        HIP_CHECK(hipEventRecord(end, 0));
        HIP_CHECK(hipEventSynchronize(end));

        T result;
        HIP_CHECK(hipMemcpy(&result, back, sizeof(T), hipMemcpyDeviceToHost));

        float elapsed_mseconds;
        HIP_CHECK(hipEventElapsedTime(&elapsed_mseconds, start, end));

        HIP_CHECK(hipEventDestroy(end));
        HIP_CHECK(hipEventDestroy(start));

        front = origi_front;
        back  = origi_back;

        return {result, std::chrono::duration<float, std::milli>{elapsed_mseconds}};
    }

private:
    F                      kernel_op;
    T                      zero_elem;
    std::span<std::size_t> input_sizes, block_sizes;
    T*                     front;
    T*                     back;
    T*                     origi_front;
    T*                     origi_back;
    std::size_t            warp_size;

    std::size_t new_size(const std::size_t factor, const std::size_t actual)
    {
        return actual / factor + (actual % factor == 0 ? 0 : 1);
    }

    template<uint32_t BlockSize, uint32_t WarpSize, uint32_t ItemsPerThread>
    __global__ static __launch_bounds__(BlockSize) void kernel(
        T* front, T* back, F op, T zero_elem, uint32_t front_size)
    {
        static constexpr uint32_t WarpCount = BlockSize / WarpSize;

        __shared__ T shared[WarpCount];

        auto read_global_safe = [&](const int32_t i)
        {
            return [&]<int32_t... I>(std::integer_sequence<int32_t, I...>)
            {
                if(i + ItemsPerThread < front_size)
                    return hip::static_array<T, ItemsPerThread>{front[i + I]...};
                else
                    return hip::static_array<T, ItemsPerThread>{
                        (i + I < front_size ? front[i + I] : zero_elem)...};
            }(std::make_integer_sequence<int32_t, ItemsPerThread>());
        };
        auto read_shared_safe
            = [&](const int32_t i) { return i < WarpCount ? shared[i] : zero_elem; };

        const int32_t tid = threadIdx.x, bid = blockIdx.x,
                      gid = bid * (blockDim.x * ItemsPerThread) + tid * ItemsPerThread,
                      wid = tid / WarpSize, lid = tid % WarpSize;

        T res = [&]()
        {
            // Read input from front buffer to local
            hip::static_array<T, ItemsPerThread> arr = read_global_safe(gid);

            // Reduce ItemsPerThread to scalar
            tmp::static_for<1, tmp::less_than<ItemsPerThread>, tmp::increment<1>>(
                [&]<int I>() { get<0>(arr) = op(get<0>(arr), get<I>(arr)); });

            return get<0>(arr);
        }();

        // Perform warp reductions and communicate results via shared
        tmp::static_for<
            WarpCount,
            tmp::not_equal<0>,
            tmp::select<tmp::not_equal<1>, tmp::divide_ceil<WarpSize>, tmp::constant<0>>>(
            [&]<uint32_t ActiveWarps>()
            {
                if(wid < ActiveWarps)
                {
                    // Warp reduction
                    tmp::static_for<WarpSize / 2, tmp::not_equal<0>, tmp::divide<2>>(
                        [&]<int Delta>() { res = op(res, __shfl_down(res, Delta)); });

                    // Write warp result from local to shared
                    if(lid == 0)
                        shared[wid] = res;
                }
                __syncthreads();

                // Read warp result from shared to local
                res = read_shared_safe(tid);
            });

        // Write result from local to back buffer
        if(tid == 0)
            back[bid] = res;
    }
};
} // namespace reduction