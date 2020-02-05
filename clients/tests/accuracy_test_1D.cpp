// Copyright (c) 2016 - present Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include <array>
#include <gtest/gtest.h>
#include <math.h>
#include <stdexcept>
#include <vector>

#include "fftw_transform.h"
#include "rocfft.h"
#include "rocfft_against_fftw.h"

using ::testing::ValuesIn;

// TODO: handle special case where length=2 for real/complex transforms.
static std::vector<size_t> pow2_range
    = {4,      8,       16,      32,      128,     256,      512,     1024,
       2048,   4096,    8192,    16384,   32768,   65536,    131072,  262144,
       524288, 1048576, 2097152, 4194304, 8388608, 16777216, 33554432};
static std::vector<size_t> pow3_range
    = {3, 9, 27, 81, 243, 729, 2187, 6561, 19683, 59049, 177147, 531441, 1594323};
static std::vector<size_t> pow5_range
    = {5, 25, 125, 625, 3125, 15625, 78125, 390625, 1953125, 9765625, 48828125};
static std::vector<size_t> mix_range
    = {6,   10,   12,   15,   20,   30,   120,  150,  225,  240,  300,   486,   600,
       900, 1250, 1500, 1875, 2160, 2187, 2250, 2500, 3000, 4000, 12000, 24000, 72000};
static std::vector<size_t> prime_range
    = {7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97};

static size_t batch_range[] = {1, 2};

static size_t stride_range[] = {1};

static size_t stride_range_for_prime[]
    = {1, 2, 3, 64, 65}; //TODO: this will be merged back to stride_range

static rocfft_result_placement placeness_range[]
    = {rocfft_placement_notinplace, rocfft_placement_inplace};

static rocfft_transform_type transform_range[]
    = {rocfft_transform_type_complex_forward, rocfft_transform_type_complex_inverse};

static rocfft_array_type c2c_array_range[]
    = {rocfft_array_type_complex_interleaved, rocfft_array_type_complex_planar};

static rocfft_array_type r2c_array_range[]
    = {rocfft_array_type_hermitian_interleaved, rocfft_array_type_hermitian_planar};

static std::vector<size_t> generate_random(size_t number_run)
{
    std::vector<size_t> output;
    size_t              RAND_MAX_NUMBER = 6;
    for(size_t r = 0; r < number_run; r++)
    {
        // generate a integer number between [0, RAND_MAX - 1]
        size_t i, j, k;
        do
        {
            i = (size_t)(rand() % RAND_MAX_NUMBER);
            j = (size_t)(rand() % RAND_MAX_NUMBER);
            k = (size_t)(rand() % RAND_MAX_NUMBER);
        } while(i + j + k == 0);
        output.push_back(pow(2, i) * pow(3, j) * pow(5, k));
    }
    return output;
}

class accuracy_test_complex
    : public ::testing::TestWithParam<std::tuple<size_t, // N
                                                 size_t, // batch
                                                 rocfft_result_placement,
                                                 size_t, // stride
                                                 rocfft_transform_type,
                                                 rocfft_array_type, // input array type
                                                 rocfft_array_type>> // output array type
{
protected:
    void SetUp() override
    {
        rocfft_setup();
    }
    void TearDown() override
    {
        rocfft_cleanup();
    }
};

class accuracy_test_real
    : public ::testing::TestWithParam<
          //              N,  batch,               placeness, stride, output for r2c / input for c2r
          std::tuple<size_t, size_t, rocfft_result_placement, size_t, rocfft_array_type>>
{
protected:
    void SetUp() override
    {
        rocfft_setup();
    }
    void TearDown() override
    {
        rocfft_cleanup();
    }
};

class accuracy_test_complex_to_real
    : public ::testing::TestWithParam<std::tuple<size_t, size_t, rocfft_result_placement, size_t>>
{
protected:
    void SetUp() override
    {
        rocfft_setup();
    }
    void TearDown() override
    {
        rocfft_cleanup();
    }
};

template <class Tfloat>
void normal_1D_complex(size_t                  N,
                       size_t                  batch,
                       rocfft_result_placement placeness,
                       rocfft_transform_type   transform_type,
                       size_t                  stride,
                       rocfft_array_type       in_array_type,
                       rocfft_array_type       out_array_type)
{
    using fftw_complex_type = typename fftw_trait<Tfloat>::fftw_complex_type;

    const bool inplace = placeness == rocfft_placement_inplace;
    int        sign
        = transform_type == rocfft_transform_type_complex_forward ? FFTW_FORWARD : FFTW_BACKWARD;

    // filter out the invalid case and skip it
    bool valid = false;
    if(inplace)
    {
        if((in_array_type == rocfft_array_type_complex_interleaved
            && out_array_type == rocfft_array_type_complex_interleaved)
           || (in_array_type == rocfft_array_type_complex_planar
               && out_array_type == rocfft_array_type_complex_planar))
        {
            valid = true;
        }
    }
    else
    {
        if((in_array_type == rocfft_array_type_complex_interleaved
            && out_array_type == rocfft_array_type_complex_interleaved)
           || (in_array_type == rocfft_array_type_complex_interleaved
               && out_array_type == rocfft_array_type_complex_planar)
           || (in_array_type == rocfft_array_type_complex_planar
               && out_array_type == rocfft_array_type_complex_interleaved)
           || (in_array_type == rocfft_array_type_complex_planar
               && out_array_type == rocfft_array_type_complex_planar))
        {
            valid = true;
        }
    }

    if(!valid)
    {
        return;
    }

    // Dimension configuration:
    std::array<fftw_iodim64, 1> dims;
    dims[0].n  = N;
    dims[0].is = stride;
    dims[0].os = stride;

    // Batch configuration:
    std::array<fftw_iodim64, 1> howmany_dims;
    howmany_dims[0].n  = batch;
    howmany_dims[0].is = dims[0].n * dims[0].is;
    howmany_dims[0].os = dims[0].n * dims[0].os;

    const size_t isize = howmany_dims[0].n * howmany_dims[0].is;
    const size_t osize = howmany_dims[0].n * howmany_dims[0].os;

    if(verbose)
    {
        if(inplace)
            std::cout << "in-place\n";
        else
            std::cout << "out-of-place\n";
        for(int i = 0; i < dims.size(); ++i)
        {
            std::cout << "dim " << i << std::endl;
            std::cout << "\tn: " << dims[i].n << std::endl;
            std::cout << "\tis: " << dims[i].is << std::endl;
            std::cout << "\tos: " << dims[i].os << std::endl;
        }
        std::cout << "howmany_dims:\n";
        std::cout << "\tn: " << howmany_dims[0].n << std::endl;
        std::cout << "\tis: " << howmany_dims[0].is << std::endl;
        std::cout << "\tos: " << howmany_dims[0].os << std::endl;
        std::cout << "isize: " << isize << "\n";
        std::cout << "osize: " << osize << "\n";
    }

    // Set up the GPU plan:
    std::vector<size_t> length     = {N};
    rocfft_status       fft_status = rocfft_status_success;

    rocfft_plan_description gpu_description = NULL;
    fft_status                              = rocfft_plan_description_create(&gpu_description);
    ASSERT_TRUE(fft_status == rocfft_status_success) << "rocFFT description creation failure";

    std::vector<size_t> istride = {static_cast<size_t>(dims[0].is)};
    std::vector<size_t> ostride = {static_cast<size_t>(dims[0].os)};

    fft_status = rocfft_plan_description_set_data_layout(gpu_description,
                                                         in_array_type,
                                                         out_array_type,
                                                         NULL,
                                                         NULL,
                                                         istride.size(),
                                                         istride.data(),
                                                         howmany_dims[0].is,
                                                         ostride.size(),
                                                         ostride.data(),
                                                         howmany_dims[0].os);
    ASSERT_TRUE(fft_status == rocfft_status_success)
        << "rocFFT data layout failure: " << fft_status;

    rocfft_plan gpu_plan = NULL;
    fft_status
        = rocfft_plan_create(&gpu_plan,
                             inplace ? rocfft_placement_inplace : rocfft_placement_notinplace,
                             transform_type,
                             precision_selector<Tfloat>(),
                             length.size(), // Dimension
                             length.data(), // lengths
                             batch, // Number of transforms
                             gpu_description); // Description
    ASSERT_TRUE(fft_status == rocfft_status_success) << "rocFFT plan creation failure";

    rocfft_execution_info planinfo = NULL;
    fft_status                     = rocfft_execution_info_create(&planinfo);
    ASSERT_TRUE(fft_status == rocfft_status_success) << "rocFFT execution info creation failure";
    size_t workbuffersize = 0;
    fft_status            = rocfft_plan_get_work_buffer_size(gpu_plan, &workbuffersize);
    ASSERT_TRUE(fft_status == rocfft_status_success) << "rocFFT get buffer size get failure";

    if(!vram_fits_problem(isize * sizeof(std::complex<Tfloat>),
                          inplace ? 0 : osize * sizeof(std::complex<Tfloat>),
                          workbuffersize))
    {
        std::cout << "problem doesn't fit on device; skipping problem\n";

        rocfft_execution_info_destroy(planinfo);
        rocfft_plan_description_destroy(gpu_description);
        rocfft_plan_destroy(gpu_plan);

        return;
    }

    void*                 gpu_in_bufs[2]  = {NULL, NULL};
    void*                 gpu_out_bufs[2] = {NULL, NULL};
    std::complex<Tfloat>* gpu_in          = NULL;
    hipError_t            hip_status      = hipSuccess;

    if(in_array_type == rocfft_array_type_complex_interleaved)
    {
        hip_status = hipMalloc(&gpu_in_bufs[0], isize * sizeof(std::complex<Tfloat>));
        ASSERT_TRUE(hip_status == hipSuccess)
            << "hipMalloc failure, size " << isize * sizeof(std::complex<Tfloat>);
    }
    else
    {
        hip_status = hipMalloc(&gpu_in_bufs[0], isize * sizeof(Tfloat));
        ASSERT_TRUE(hip_status == hipSuccess)
            << "hipMalloc failure, size " << isize * sizeof(std::complex<Tfloat>);
        hip_status = hipMalloc(&gpu_in_bufs[1], isize * sizeof(Tfloat));
        ASSERT_TRUE(hip_status == hipSuccess)
            << "hipMalloc failure, size " << isize * sizeof(std::complex<Tfloat>);
    }

    if(inplace)
    {
        gpu_out_bufs[0] = gpu_in_bufs[0];
        gpu_out_bufs[1] = gpu_in_bufs[1];
    }
    else
    {
        if(out_array_type == rocfft_array_type_complex_interleaved)
        {
            hip_status = hipMalloc(&gpu_out_bufs[0], osize * sizeof(std::complex<Tfloat>));
            ASSERT_TRUE(hip_status == hipSuccess) << "hipMalloc failure";
        }
        else
        {
            hip_status = hipMalloc(&gpu_out_bufs[0], osize * sizeof(Tfloat));
            ASSERT_TRUE(hip_status == hipSuccess)
                << "hipMalloc failure, size " << osize * sizeof(std::complex<Tfloat>);
            hip_status = hipMalloc(&gpu_out_bufs[1], osize * sizeof(Tfloat));
            ASSERT_TRUE(hip_status == hipSuccess)
                << "hipMalloc failure, size " << osize * sizeof(std::complex<Tfloat>);
        }
    }

    void* wbuffer = NULL;
    if(workbuffersize > 0)
    {
        hip_status = hipMalloc(&wbuffer, workbuffersize);
        ASSERT_TRUE(hip_status == hipSuccess) << "hipMalloc failure";
        fft_status = rocfft_execution_info_set_work_buffer(planinfo, wbuffer, workbuffersize);
        ASSERT_TRUE(fft_status == rocfft_status_success) << "rocFFT set work buffer failure";
    }

    // Set up cpu buffers:
    std::complex<Tfloat>* cpu_in = fftw_alloc_type<std::complex<Tfloat>>(isize);
    Tfloat*               cpu_tmp_planar_bufs[2];
    if(in_array_type == rocfft_array_type_complex_planar
       || out_array_type == rocfft_array_type_complex_planar)
    {
        cpu_tmp_planar_bufs[0] = new Tfloat[isize];
        cpu_tmp_planar_bufs[1] = new Tfloat[isize];
    }
    std::complex<Tfloat>* cpu_out = inplace ? cpu_in : fftw_alloc_type<std::complex<Tfloat>>(osize);

    // Set up the CPU plan:
    auto cpu_plan = fftw_plan_guru64_dft<Tfloat>(dims.size(),
                                                 dims.data(),
                                                 howmany_dims.size(),
                                                 howmany_dims.data(),
                                                 reinterpret_cast<fftw_complex_type*>(cpu_in),
                                                 reinterpret_cast<fftw_complex_type*>(cpu_out),
                                                 sign,
                                                 FFTW_ESTIMATE);
    ASSERT_TRUE(cpu_plan != NULL) << "FFTW plan creation failure";

    // Set up the data:
    std::fill(cpu_in, cpu_in + isize, 0.0);
    srandom(3);
    for(size_t ibatch = 0; ibatch < batch; ++ibatch)
    {
        for(size_t i = 0; i < N; ++i)
        {
            std::complex<Tfloat> val((Tfloat)rand() / (Tfloat)RAND_MAX,
                                     (Tfloat)rand() / (Tfloat)RAND_MAX);
            cpu_in[howmany_dims[0].is * ibatch + dims[0].is * i] = val;
        }
    }

    if(in_array_type == rocfft_array_type_complex_planar)
    {
        for(size_t ibatch = 0; ibatch < batch; ++ibatch)
        {
            for(size_t i = 0; i < N; ++i)
            {
                size_t idx                  = howmany_dims[0].is * ibatch + dims[0].is * i;
                cpu_tmp_planar_bufs[0][idx] = cpu_in[idx].real();
                cpu_tmp_planar_bufs[1][idx] = cpu_in[idx].imag();
            }
        }
    }

    if(verbose > 1)
    {
        std::cout << "input:\n";
        for(size_t ibatch = 0; ibatch < batch; ++ibatch)
        {
            std::cout << "ibatch: " << ibatch << std::endl;
            for(size_t i = 0; i < N; i++)
            {
                std::cout << cpu_in[howmany_dims[0].is * ibatch + dims[0].is * i] << " ";
            }
            std::cout << std::endl;
        }
    }
    if(verbose > 2)
    {
        std::cout << "flat input:\n";
        for(size_t i = 0; i < isize; ++i)
        {
            std::cout << cpu_in[i] << " ";
        }
        std::cout << std::endl;
    }

    if(in_array_type == rocfft_array_type_complex_interleaved)
    {
        hip_status = hipMemcpy(
            gpu_in_bufs[0], cpu_in, isize * sizeof(std::complex<Tfloat>), hipMemcpyHostToDevice);
        ASSERT_TRUE(hip_status == hipSuccess) << "hipMemcpy failure";
    }
    else
    {
        hip_status = hipMemcpy(
            gpu_in_bufs[0], cpu_tmp_planar_bufs[0], isize * sizeof(Tfloat), hipMemcpyHostToDevice);
        ASSERT_TRUE(hip_status == hipSuccess) << "hipMemcpy failure";
        hip_status = hipMemcpy(
            gpu_in_bufs[1], cpu_tmp_planar_bufs[1], isize * sizeof(Tfloat), hipMemcpyHostToDevice);
        ASSERT_TRUE(hip_status == hipSuccess) << "hipMemcpy failure";
    }

    // Execute the GPU transform:
    fft_status = rocfft_execute(gpu_plan, // plan
                                (void**)&gpu_in_bufs, // in buffers
                                (void**)&gpu_out_bufs, // out buffers
                                planinfo); // execution info
    ASSERT_TRUE(fft_status == rocfft_status_success) << "rocFFT plan execution failure";

    // Execute the CPU transform:
    fftw_execute_type<Tfloat>(cpu_plan);

    if(verbose > 1)
    {
        std::cout << "cpu_out:\n";
        for(size_t ibatch = 0; ibatch < batch; ++ibatch)
        {
            std::cout << "ibatch: " << ibatch << std::endl;
            for(size_t i = 0; i < N; i++)
            {
                std::cout << cpu_out[howmany_dims[0].os * ibatch + dims[0].os * i] << " ";
            }
            std::cout << std::endl;
        }
    }
    if(verbose > 2)
    {
        std::cout << "flat cpu output:\n";
        for(size_t i = 0; i < osize; ++i)
        {
            std::cout << cpu_out[i] << " ";
        }
        std::cout << std::endl;
    }

    // Copy the data back and compare:
    fftw_vector<std::complex<Tfloat>> gpu_out_comp(osize);
    if(out_array_type == rocfft_array_type_complex_interleaved)
    {
        hip_status = hipMemcpy(gpu_out_comp.data(),
                               gpu_out_bufs[0],
                               osize * sizeof(std::complex<Tfloat>),
                               hipMemcpyDeviceToHost);
        ASSERT_TRUE(hip_status == hipSuccess) << "hipMemcpy failure";
    }
    else
    {
        hip_status = hipMemcpy(
            cpu_tmp_planar_bufs[0], gpu_out_bufs[0], osize * sizeof(Tfloat), hipMemcpyDeviceToHost);
        ASSERT_TRUE(hip_status == hipSuccess) << "hipMemcpy failure";
        hip_status = hipMemcpy(
            cpu_tmp_planar_bufs[1], gpu_out_bufs[1], osize * sizeof(Tfloat), hipMemcpyDeviceToHost);
        ASSERT_TRUE(hip_status == hipSuccess) << "hipMemcpy failure";
        for(size_t i = 0; i < osize; i++)
        {
            gpu_out_comp[i]
                = std::complex<Tfloat>(cpu_tmp_planar_bufs[0][i], cpu_tmp_planar_bufs[1][i]);
        }
    }

    if(verbose > 1)
    {
        std::cout << "gpu_out:\n";
        for(size_t ibatch = 0; ibatch < batch; ++ibatch)
        {
            std::cout << "ibatch: " << ibatch << std::endl;
            for(size_t i = 0; i < N; i++)
            {
                std::cout << gpu_out_comp[howmany_dims[0].os * ibatch + dims[0].os * i] << " ";
            }
            std::cout << std::endl;
        }
        std::cout << std::endl;
    }
    if(verbose > 2)
    {
        std::cout << "flat gpu output:\n";
        for(size_t i = 0; i < osize; ++i)
        {
            std::cout << gpu_out_comp[i] << " ";
        }
        std::cout << std::endl;
    }

    Tfloat L2norm   = 0.0;
    Tfloat Linfnorm = 0.0;
    for(size_t ibatch = 0; ibatch < batch; ++ibatch)
    {
        for(size_t i = 0; i < N; i++)
        {
            auto val = cpu_out[howmany_dims[0].os * ibatch + dims[0].os * i];
            Linfnorm = std::max(std::abs(val), Linfnorm);
            L2norm += std::abs(val) * std::abs(val);
        }
        L2norm = sqrt(L2norm);
    }

    Tfloat L2diff   = 0.0;
    Tfloat Linfdiff = 0.0;
    int    nwrong   = 0;
    for(size_t ibatch = 0; ibatch < batch; ++ibatch)
    {
        for(size_t i = 0; i < N; i++)
        {
            const int pos  = howmany_dims[0].os * ibatch + dims[0].os * i;
            Tfloat    diff = std::abs(cpu_out[pos] - gpu_out_comp[pos]);
            Linfdiff       = std::max(diff, Linfdiff);
            L2diff += diff * diff;
            if(std::abs(diff) / (Linfnorm * log(N)) >= type_epsilon<Tfloat>())
            {
                nwrong++;
                if(verbose > 1)
                {
                    std::cout << "ibatch: " << ibatch << ", i: " << i << ", cpu: " << cpu_out[pos]
                              << ", gpu: " << gpu_out_comp[pos] << "\n";
                }
            }
        }
    }
    L2diff = sqrt(L2diff);

    Tfloat L2error   = L2diff / (L2norm * sqrt(log(N)));
    Tfloat Linferror = Linfdiff / (Linfnorm * log(N));
    if(verbose)
    {
        std::cout << "nwrong: " << nwrong << std::endl;
        std::cout << "relative L2 error: " << L2error << std::endl;
        std::cout << "relative Linf error: " << Linferror << std::endl;
    }

    EXPECT_TRUE(L2error < type_epsilon<Tfloat>())
        << "Tolerance failure: L2error: " << L2error << ", tolerance: " << type_epsilon<Tfloat>();
    EXPECT_TRUE(Linferror < type_epsilon<Tfloat>()) << "Tolerance failure: Linferror: " << Linferror
                                                    << ", tolerance: " << type_epsilon<Tfloat>();

    // Free GPU memory:
    hipFree(gpu_in_bufs[0]);
    hipFree(gpu_in_bufs[1]);
    fftw_free(cpu_in);
    if(in_array_type == rocfft_array_type_complex_planar
       || out_array_type == rocfft_array_type_complex_planar)
    {
        delete[] cpu_tmp_planar_bufs[0];
        delete[] cpu_tmp_planar_bufs[1];
    }
    if(!inplace)
    {
        hipFree(gpu_out_bufs[0]);
        hipFree(gpu_out_bufs[1]);
        fftw_free(cpu_out);
    }
    if(wbuffer != NULL)
    {
        hipFree(wbuffer);
    }

    // Destroy plans:
    rocfft_execution_info_destroy(planinfo);
    rocfft_plan_description_destroy(gpu_description);
    rocfft_plan_destroy(gpu_plan);
    fftw_destroy_plan_type(cpu_plan);
}

// Complex to Complex

TEST_P(accuracy_test_complex, normal_1D_single_precision)
{
    size_t                  N              = std::get<0>(GetParam());
    size_t                  batch          = std::get<1>(GetParam());
    rocfft_result_placement placeness      = std::get<2>(GetParam());
    size_t                  stride         = std::get<3>(GetParam());
    rocfft_transform_type   transform_type = std::get<4>(GetParam());
    rocfft_array_type       in_array_type  = std::get<5>(GetParam());
    rocfft_array_type       out_array_type = std::get<6>(GetParam());

    try
    {
        normal_1D_complex<float>(
            N, batch, placeness, transform_type, stride, in_array_type, out_array_type);
    }
    catch(const std::exception& err)
    {
        handle_exception(err);
    }
}

TEST_P(accuracy_test_complex, normal_1D_double_precision)
{
    size_t                  N              = std::get<0>(GetParam());
    size_t                  batch          = std::get<1>(GetParam());
    rocfft_result_placement placeness      = std::get<2>(GetParam());
    size_t                  stride         = std::get<3>(GetParam());
    rocfft_transform_type   transform_type = std::get<4>(GetParam());
    rocfft_array_type       in_array_type  = std::get<5>(GetParam());
    rocfft_array_type       out_array_type = std::get<6>(GetParam());

    try
    {
        normal_1D_complex<double>(
            N, batch, placeness, transform_type, stride, in_array_type, out_array_type);
    }
    catch(const std::exception& err)
    {
        handle_exception(err);
    }
}

// Real to complex
template <class Tfloat>
void normal_1D_real_to_complex(size_t                  N,
                               size_t                  batch,
                               rocfft_result_placement placeness,
                               rocfft_transform_type   transform_type,
                               size_t                  stride,
                               rocfft_array_type       out_array_type)
{
    using fftw_complex_type = typename fftw_trait<Tfloat>::fftw_complex_type;
    const bool inplace      = placeness == rocfft_placement_inplace;

    // filter out the invalid case and skip it
    bool valid = false;
    if(inplace)
    {
        if(out_array_type == rocfft_array_type_hermitian_interleaved)
        {
            valid = true;
        }
    }
    else
    {
        if(out_array_type == rocfft_array_type_hermitian_interleaved
           || out_array_type == rocfft_array_type_hermitian_planar)
        {
            valid = true;
        }
    }

    if(!valid)
    {
        return;
    }

    const size_t Ncomplex = N / 2 + 1;

    // Dimension configuration:
    std::array<fftw_iodim64, 1> dims;
    dims[0].n  = N;
    dims[0].is = stride;
    dims[0].os = stride;

    // Batch configuration:
    std::array<fftw_iodim64, 1> howmany_dims;
    howmany_dims[0].n  = batch;
    howmany_dims[0].is = (inplace ? Ncomplex * 2 : dims[0].n) * dims[0].is;
    howmany_dims[0].os = Ncomplex * dims[0].os;

    const size_t isize = howmany_dims[0].n * howmany_dims[0].is;
    const size_t osize = howmany_dims[0].n * howmany_dims[0].os;

    if(verbose)
    {
        if(inplace)
            std::cout << "in-place\n";
        else
            std::cout << "out-of-place\n";
        for(int i = 0; i < dims.size(); ++i)
        {
            std::cout << "dim " << i << std::endl;
            std::cout << "\tn: " << dims[i].n << std::endl;
            std::cout << "\tis: " << dims[i].is << std::endl;
            std::cout << "\tos: " << dims[i].os << std::endl;
        }
        std::cout << "howmany_dims:\n";
        std::cout << "\tn: " << howmany_dims[0].n << std::endl;
        std::cout << "\tis: " << howmany_dims[0].is << std::endl;
        std::cout << "\tos: " << howmany_dims[0].os << std::endl;
        std::cout << "isize: " << isize << "\n";
        std::cout << "osize: " << osize << "\n";
    }

    // Set up the GPU plan:
    std::vector<size_t> length     = {N};
    rocfft_status       fft_status = rocfft_status_success;

    rocfft_plan_description gpu_description = NULL;
    fft_status                              = rocfft_plan_description_create(&gpu_description);
    ASSERT_TRUE(fft_status == rocfft_status_success) << "rocFFT description creation failure";

    std::vector<size_t> istride = {static_cast<size_t>(dims[0].is)};
    std::vector<size_t> ostride = {static_cast<size_t>(dims[0].os)};

    fft_status = rocfft_plan_description_set_data_layout(gpu_description,
                                                         rocfft_array_type_real,
                                                         out_array_type,
                                                         NULL,
                                                         NULL,
                                                         istride.size(),
                                                         istride.data(),
                                                         howmany_dims[0].is,
                                                         ostride.size(),
                                                         ostride.data(),
                                                         howmany_dims[0].os);
    ASSERT_TRUE(fft_status == rocfft_status_success)
        << "rocFFT data layout failure: " << fft_status;

    rocfft_plan gpu_plan = NULL;
    fft_status
        = rocfft_plan_create(&gpu_plan,
                             inplace ? rocfft_placement_inplace : rocfft_placement_notinplace,
                             rocfft_transform_type_real_forward,
                             precision_selector<Tfloat>(),
                             length.size(), // Dimension
                             length.data(), // lengths
                             batch, // Number of transforms
                             gpu_description); // Description
    ASSERT_TRUE(fft_status == rocfft_status_success) << "rocFFT plan creation failure";

    // The real-to-complex transform uses work memory, which is passed
    // via a rocfft_execution_info struct.
    rocfft_execution_info planinfo = NULL;
    fft_status                     = rocfft_execution_info_create(&planinfo);
    ASSERT_TRUE(fft_status == rocfft_status_success) << "rocFFT execution info creation failure";
    size_t workbuffersize = 0;
    fft_status            = rocfft_plan_get_work_buffer_size(gpu_plan, &workbuffersize);
    ASSERT_TRUE(fft_status == rocfft_status_success) << "rocFFT get buffer size get failure";

    if(!vram_fits_problem(isize * sizeof(Tfloat),
                          inplace ? 0 : osize * sizeof(std::complex<Tfloat>),
                          workbuffersize))
    {
        std::cout << "problem doesn't fit on device; skipping problem\n";

        rocfft_execution_info_destroy(planinfo);
        rocfft_plan_description_destroy(gpu_description);
        rocfft_plan_destroy(gpu_plan);

        return;
    }

    hipError_t hip_status = hipSuccess;

    Tfloat* gpu_in = NULL;
    hip_status     = hipMalloc(&gpu_in, isize * sizeof(Tfloat));
    ASSERT_TRUE(hip_status == hipSuccess) << "hipMalloc failure";

    void* gpu_out_bufs[2] = {NULL, NULL};

    if(inplace)
    {
        gpu_out_bufs[0] = gpu_in;
    }
    else
    {
        if(out_array_type == rocfft_array_type_hermitian_interleaved)
        {
            hip_status = hipMalloc(&gpu_out_bufs[0], osize * sizeof(std::complex<Tfloat>));
            ASSERT_TRUE(hip_status == hipSuccess) << "hipMalloc failure";
        }
        else
        {
            hip_status = hipMalloc(&gpu_out_bufs[0], osize * sizeof(Tfloat));
            ASSERT_TRUE(hip_status == hipSuccess)
                << "hipMalloc failure, size " << osize * sizeof(std::complex<Tfloat>);
            hip_status = hipMalloc(&gpu_out_bufs[1], osize * sizeof(Tfloat));
            ASSERT_TRUE(hip_status == hipSuccess)
                << "hipMalloc failure, size " << osize * sizeof(std::complex<Tfloat>);
        }
    }

    void* wbuffer = NULL;
    if(workbuffersize > 0)
    {
        hip_status = hipMalloc(&wbuffer, workbuffersize);
        ASSERT_TRUE(hip_status == hipSuccess) << "hipMalloc failure";
        fft_status = rocfft_execution_info_set_work_buffer(planinfo, wbuffer, workbuffersize);
        ASSERT_TRUE(fft_status == rocfft_status_success) << "rocFFT set work buffer failure";
    }

    // Set up cpu buffers:
    Tfloat* cpu_in = fftw_alloc_type<Tfloat>(isize);
    // Output buffer
    std::complex<Tfloat>* cpu_out
        = inplace ? (std::complex<Tfloat>*)cpu_in : fftw_alloc_type<std::complex<Tfloat>>(osize);

    // Set up the CPU plan:
    auto cpu_plan = fftw_plan_guru64_r2c<Tfloat>(dims.size(),
                                                 dims.data(),
                                                 howmany_dims.size(),
                                                 howmany_dims.data(),
                                                 cpu_in,
                                                 reinterpret_cast<fftw_complex_type*>(cpu_out),
                                                 FFTW_ESTIMATE);
    ASSERT_TRUE(cpu_plan != NULL) << "FFTW plan creation failure";

    // Set up the data:
    std::fill(cpu_in, cpu_in + isize, 0.0);
    srandom(3);
    for(size_t ibatch = 0; ibatch < batch; ++ibatch)
    {
        for(size_t i = 0; i < N; i++)
        {
            // TODO: make pattern variable
            cpu_in[howmany_dims[0].is * ibatch + dims[0].is * i]
                = (Tfloat)rand() / (Tfloat)RAND_MAX;
        }
    }

    if(verbose > 1)
    {
        std::cout << "input:\n";
        for(size_t ibatch = 0; ibatch < batch; ++ibatch)
        {
            std::cout << "ibatch: " << ibatch << std::endl;
            for(int i = 0; i < N; ++i)
            {
                std::cout << cpu_in[howmany_dims[0].is * ibatch + dims[0].is * i] << " ";
            }
            std::cout << std::endl;
        }
    }
    if(verbose > 2)
    {
        std::cout << "flat input:\n";
        for(size_t i = 0; i < isize; ++i)
        {
            std::cout << cpu_in[i] << " ";
        }
        std::cout << std::endl;
    }

    hip_status = hipMemcpy(gpu_in, cpu_in, isize * sizeof(Tfloat), hipMemcpyHostToDevice);
    ASSERT_TRUE(hip_status == hipSuccess) << "hipMemcpy failure";

    // Execute the GPU transform:
    fft_status = rocfft_execute(gpu_plan, // plan
                                (void**)&gpu_in, // in_buffer
                                (void**)&gpu_out_bufs, // out_buffers
                                planinfo); // execution info
    ASSERT_TRUE(fft_status == rocfft_status_success) << "rocFFT plan execution failure";

    // Execute the CPU transform:
    fftw_execute_type<Tfloat>(cpu_plan);

    if(verbose > 1)
    {
        std::cout << "cpu_out:\n";
        for(size_t ibatch = 0; ibatch < batch; ++ibatch)
        {
            std::cout << "ibatch: " << ibatch << std::endl;
            for(size_t i = 0; i < Ncomplex; i++)
            {
                std::cout << cpu_out[howmany_dims[0].os * ibatch + dims[0].os * i] << " ";
            }
            std::cout << std::endl;
        }
    }
    if(verbose > 2)
    {
        std::cout << "flat cpu output:\n";
        for(size_t i = 0; i < osize; ++i)
        {
            std::cout << cpu_out[i] << " ";
        }
        std::cout << std::endl;
    }

    // Copy the data back and compare:
    fftw_vector<std::complex<Tfloat>> gpu_out_comp(osize);
    if(out_array_type == rocfft_array_type_hermitian_interleaved)
    {
        hip_status = hipMemcpy(gpu_out_comp.data(),
                               gpu_out_bufs[0],
                               osize * sizeof(std::complex<Tfloat>),
                               hipMemcpyDeviceToHost);
        ASSERT_TRUE(hip_status == hipSuccess) << "hipMemcpy failure";
    }
    else
    {
        Tfloat* cpu_tmp_planar_bufs[2];
        cpu_tmp_planar_bufs[0] = new Tfloat[osize];
        cpu_tmp_planar_bufs[1] = new Tfloat[osize];

        hip_status = hipMemcpy(
            cpu_tmp_planar_bufs[0], gpu_out_bufs[0], osize * sizeof(Tfloat), hipMemcpyDeviceToHost);
        ASSERT_TRUE(hip_status == hipSuccess) << "hipMemcpy failure";
        hip_status = hipMemcpy(
            cpu_tmp_planar_bufs[1], gpu_out_bufs[1], osize * sizeof(Tfloat), hipMemcpyDeviceToHost);
        ASSERT_TRUE(hip_status == hipSuccess) << "hipMemcpy failure";
        for(size_t i = 0; i < osize; i++)
        {
            gpu_out_comp[i]
                = std::complex<Tfloat>(cpu_tmp_planar_bufs[0][i], cpu_tmp_planar_bufs[1][i]);
        }
        delete[] cpu_tmp_planar_bufs[0];
        delete[] cpu_tmp_planar_bufs[1];
    }

    if(verbose > 1)
    {
        std::cout << "gpu_out:\n";
        for(size_t ibatch = 0; ibatch < batch; ++ibatch)
        {
            std::cout << "ibatch: " << ibatch << std::endl;
            for(size_t i = 0; i < Ncomplex; i++)
            {
                std::cout << gpu_out_comp[howmany_dims[0].os * ibatch + dims[0].os * i] << " ";
            }
            std::cout << std::endl;
        }
    }
    if(verbose > 2)
    {
        std::cout << "flat gpu output:\n";
        for(size_t i = 0; i < osize; ++i)
        {
            std::cout << gpu_out_comp[i] << " ";
        }
        std::cout << std::endl;
    }

    Tfloat L2norm   = 0.0;
    Tfloat Linfnorm = 0.0;
    for(size_t ibatch = 0; ibatch < batch; ++ibatch)
    {
        for(size_t i = 0; i < Ncomplex; i++)
        {
            auto val = cpu_out[howmany_dims[0].os * ibatch + dims[0].os * i];
            Linfnorm = std::max(std::abs(val), Linfnorm);
            L2norm += std::abs(val) * std::abs(val);
        }
        L2norm = sqrt(L2norm);
    }

    Tfloat L2diff   = 0.0;
    Tfloat Linfdiff = 0.0;
    int    nwrong   = 0;
    for(size_t ibatch = 0; ibatch < batch; ++ibatch)
    {
        for(size_t i = 0; i < Ncomplex; i++)
        {
            const int pos  = howmany_dims[0].os * ibatch + dims[0].os * i;
            Tfloat    diff = std::abs(cpu_out[pos] - gpu_out_comp[pos]);
            Linfdiff       = std::max(diff, Linfdiff);
            L2diff += diff * diff;
            if(std::abs(diff) / (Linfnorm * log(N)) >= type_epsilon<Tfloat>())
            {
                nwrong++;
                if(verbose > 1)
                {
                    std::cout << "ibatch: " << ibatch << ", i: " << i
                              << ", cpu: " << std::scientific << cpu_out[pos]
                              << ", gpu: " << std::scientific << gpu_out_comp[pos] << "\n";
                }
            }
        }
    }
    L2diff = sqrt(L2diff);

    Tfloat L2error   = L2diff / (L2norm * sqrt(log(N)));
    Tfloat Linferror = Linfdiff / (Linfnorm * log(N));
    if(verbose)
    {
        std::cout << "nwrong: " << nwrong << std::endl;
        std::cout << "relative L2 error: " << L2error << std::endl;
        std::cout << "relative Linf error: " << Linferror << std::endl;
    }

    EXPECT_TRUE(L2error < type_epsilon<Tfloat>())
        << "Tolerance failure: L2error: " << L2error << ", tolerance: " << type_epsilon<Tfloat>();
    EXPECT_TRUE(Linferror < type_epsilon<Tfloat>()) << "Tolerance failure: Linferror: " << Linferror
                                                    << ", tolerance: " << type_epsilon<Tfloat>();

    // Free GPU memory:
    hipFree(gpu_in);
    fftw_free(cpu_in);
    if(!inplace)
    {
        hipFree(gpu_out_bufs[0]);
        hipFree(gpu_out_bufs[1]);
        fftw_free(cpu_out);
    }
    if(wbuffer != NULL)
    {
        hipFree(wbuffer);
    }

    // Destroy plans:
    rocfft_execution_info_destroy(planinfo);
    rocfft_plan_description_destroy(gpu_description);
    rocfft_plan_destroy(gpu_plan);
    fftw_destroy_plan_type(cpu_plan);
}

TEST_P(accuracy_test_real, normal_1D_real_to_complex_single_precision)
{
    size_t                  N         = std::get<0>(GetParam());
    size_t                  batch     = std::get<1>(GetParam());
    rocfft_result_placement placeness = std::get<2>(GetParam());
    size_t                  stride    = std::get<3>(GetParam());
    rocfft_transform_type   transform_type
        = rocfft_transform_type_real_forward; // must be real forward
    rocfft_array_type out_array_type = std::get<4>(GetParam());

    try
    {
        normal_1D_real_to_complex<float>(
            N, batch, placeness, transform_type, stride, out_array_type);
    }
    catch(const std::exception& err)
    {
        handle_exception(err);
    }
}

TEST_P(accuracy_test_real, normal_1D_real_to_complex_double_precision)
{
    size_t                  N         = std::get<0>(GetParam());
    size_t                  batch     = std::get<1>(GetParam());
    rocfft_result_placement placeness = std::get<2>(GetParam());
    size_t                  stride    = std::get<3>(GetParam());
    rocfft_transform_type   transform_type
        = rocfft_transform_type_real_forward; // must be real forward
    rocfft_array_type out_array_type = std::get<4>(GetParam());

    try
    {
        normal_1D_real_to_complex<double>(
            N, batch, placeness, transform_type, stride, out_array_type);
    }
    catch(const std::exception& err)
    {
        handle_exception(err);
    }
}

// Complex to Real
template <class Tfloat>
void normal_1D_complex_to_real(size_t                  N,
                               size_t                  batch,
                               rocfft_result_placement placeness,
                               rocfft_transform_type   transform_type,
                               size_t                  stride,
                               rocfft_array_type       in_array_type)
{
    using fftw_complex_type = typename fftw_trait<Tfloat>::fftw_complex_type;
    const bool inplace      = placeness == rocfft_placement_inplace;

    // filter out the invalid case and skip it
    bool valid = false;
    if(inplace)
    {
        if(in_array_type == rocfft_array_type_hermitian_interleaved)
        {
            valid = true;
        }
    }
    else
    {
        if(in_array_type == rocfft_array_type_hermitian_interleaved
           || in_array_type == rocfft_array_type_hermitian_planar)
        {
            valid = true;
        }
    }

    if(!valid)
    {
        return;
    }

    // TODO: add logic to deal with discontiguous data in Nystride
    const size_t Ncomplex = N / 2 + 1;

    // Dimension configuration:
    std::array<fftw_iodim64, 1> dims;
    dims[0].n  = N;
    dims[0].is = stride;
    dims[0].os = stride;

    // Batch configuration:
    std::array<fftw_iodim64, 1> howmany_dims;
    howmany_dims[0].n  = batch;
    howmany_dims[0].is = Ncomplex * dims[0].is;
    howmany_dims[0].os = (inplace ? Ncomplex * 2 : dims[0].n) * dims[0].os;

    const size_t isize = howmany_dims[0].n * howmany_dims[0].is;
    const size_t osize = howmany_dims[0].n * howmany_dims[0].os;

    if(verbose)
    {
        if(inplace)
            std::cout << "in-place\n";
        else
            std::cout << "out-of-place\n";
        for(int i = 0; i < dims.size(); ++i)
        {
            std::cout << "dim " << i << std::endl;
            std::cout << "\tn: " << dims[i].n << std::endl;
            std::cout << "\tis: " << dims[i].is << std::endl;
            std::cout << "\tos: " << dims[i].os << std::endl;
        }
        std::cout << "howmany_dims:\n";
        std::cout << "\tn: " << howmany_dims[0].n << std::endl;
        std::cout << "\tis: " << howmany_dims[0].is << std::endl;
        std::cout << "\tos: " << howmany_dims[0].os << std::endl;
        std::cout << "isize: " << isize << "\n";
        std::cout << "osize: " << osize << "\n";
    }

    // Set up the GPU plan:
    std::vector<size_t> length     = {N};
    rocfft_status       fft_status = rocfft_status_success;

    rocfft_plan_description gpu_description = NULL;
    fft_status                              = rocfft_plan_description_create(&gpu_description);
    ASSERT_TRUE(fft_status == rocfft_status_success) << "rocFFT description creation failure";

    std::vector<size_t> istride = {static_cast<size_t>(dims[0].is)};
    std::vector<size_t> ostride = {static_cast<size_t>(dims[0].os)};

    fft_status = rocfft_plan_description_set_data_layout(gpu_description,
                                                         in_array_type,
                                                         rocfft_array_type_real,
                                                         NULL,
                                                         NULL,
                                                         istride.size(),
                                                         istride.data(),
                                                         howmany_dims[0].is,
                                                         ostride.size(),
                                                         ostride.data(),
                                                         howmany_dims[0].os);
    ASSERT_TRUE(fft_status == rocfft_status_success)
        << "rocFFT data layout failure: " << fft_status;

    rocfft_plan gpu_plan = NULL;
    fft_status
        = rocfft_plan_create(&gpu_plan,
                             inplace ? rocfft_placement_inplace : rocfft_placement_notinplace,
                             rocfft_transform_type_real_inverse,
                             precision_selector<Tfloat>(),
                             length.size(), // Dimension
                             length.data(), // lengths
                             batch, // Number of transforms
                             gpu_description); // Description
    ASSERT_TRUE(fft_status == rocfft_status_success) << "rocFFT plan creation failure";

    // The real-to-complex transform uses work memory, which is passed
    // via a rocfft_execution_info struct.
    rocfft_execution_info planinfo = NULL;
    fft_status                     = rocfft_execution_info_create(&planinfo);
    ASSERT_TRUE(fft_status == rocfft_status_success) << "rocFFT execution info creation failure";
    size_t workbuffersize = 0;
    fft_status            = rocfft_plan_get_work_buffer_size(gpu_plan, &workbuffersize);
    ASSERT_TRUE(fft_status == rocfft_status_success) << "rocFFT get buffer size get failure";

    if(!vram_fits_problem(isize * sizeof(Tfloat),
                          inplace ? 0 : osize * sizeof(std::complex<Tfloat>),
                          workbuffersize))
    {
        std::cout << "problem doesn't fit on device; skipping problem\n";

        rocfft_execution_info_destroy(planinfo);
        rocfft_plan_description_destroy(gpu_description);
        rocfft_plan_destroy(gpu_plan);

        return;
    }

    hipError_t hip_status = hipSuccess;

    void* gpu_in_bufs[2] = {NULL, NULL};
    if(in_array_type == rocfft_array_type_hermitian_interleaved)
    {
        hip_status = hipMalloc(&gpu_in_bufs[0], isize * sizeof(std::complex<Tfloat>));
        ASSERT_TRUE(hip_status == hipSuccess)
            << "hipMalloc failure, size " << isize * sizeof(std::complex<Tfloat>);
    }
    else
    {
        hip_status = hipMalloc(&gpu_in_bufs[0], isize * sizeof(Tfloat));
        ASSERT_TRUE(hip_status == hipSuccess)
            << "hipMalloc failure, size " << isize * sizeof(std::complex<Tfloat>);
        hip_status = hipMalloc(&gpu_in_bufs[1], isize * sizeof(Tfloat));
        ASSERT_TRUE(hip_status == hipSuccess)
            << "hipMalloc failure, size " << isize * sizeof(std::complex<Tfloat>);
    }

    Tfloat* gpu_out = inplace ? (Tfloat*)gpu_in_bufs[0] : NULL;
    if(!inplace)
    {
        hip_status = hipMalloc(&gpu_out, osize * sizeof(Tfloat));
        ASSERT_TRUE(hip_status == hipSuccess) << "hipMalloc failure";
    }

    void* gpu_planwbuffer = NULL;
    if(workbuffersize > 0)
    {
        hip_status = hipMalloc(&gpu_planwbuffer, workbuffersize);
        ASSERT_TRUE(hip_status == hipSuccess) << "hipMalloc failure";
        fft_status
            = rocfft_execution_info_set_work_buffer(planinfo, gpu_planwbuffer, workbuffersize);
        ASSERT_TRUE(fft_status == rocfft_status_success) << "rocFFT set work buffer failure";
    }

    // Set up cpu buffers:
    std::complex<Tfloat>* cpu_in = fftw_alloc_type<std::complex<Tfloat>>(isize);
    Tfloat*               cpu_tmp_planar_bufs[2];
    if(in_array_type == rocfft_array_type_hermitian_planar)
    {
        cpu_tmp_planar_bufs[0] = new Tfloat[isize];
        cpu_tmp_planar_bufs[1] = new Tfloat[isize];
    }
    // Output buffer
    Tfloat* cpu_out = inplace ? (Tfloat*)cpu_in : fftw_alloc_type<Tfloat>(osize);

    // Set up the CPU plan:
    auto cpu_plan = fftw_plan_guru64_c2r<Tfloat>(dims.size(),
                                                 dims.data(),
                                                 howmany_dims.size(),
                                                 howmany_dims.data(),
                                                 reinterpret_cast<fftw_complex_type*>(cpu_in),
                                                 cpu_out,
                                                 FFTW_ESTIMATE);
    ASSERT_TRUE(cpu_plan != NULL) << "FFTW plan creation failure";

    // Set up the data:
    std::fill(cpu_in, cpu_in + isize, 0.0);
    srandom(3);
    for(size_t ibatch = 0; ibatch < batch; ++ibatch)
    {
        for(size_t i = 0; i < Ncomplex; i++)
        {
            // TODO: make pattern variable
            cpu_in[howmany_dims[0].is * ibatch + dims[0].is * i] = std::complex<Tfloat>(
                (Tfloat)rand() / (Tfloat)RAND_MAX, (Tfloat)rand() / (Tfloat)RAND_MAX);
        }
    }

    // Impose Hermitian symmetry:
    for(size_t ibatch = 0; ibatch < batch; ++ibatch)
    {
        // origin:
        cpu_in[howmany_dims[0].is * ibatch].imag(0.0);

        if(N % 2 == 0)
        {
            // x-Nyquist is real-valued
            cpu_in[howmany_dims[0].is * ibatch + dims[0].is * (N / 2)].imag(0.0);
        }
    }

    if(in_array_type == rocfft_array_type_hermitian_planar)
    {
        for(size_t ibatch = 0; ibatch < batch; ++ibatch)
        {
            for(size_t i = 0; i < Ncomplex; i++)
            {
                size_t idx                  = howmany_dims[0].is * ibatch + dims[0].is * i;
                cpu_tmp_planar_bufs[0][idx] = cpu_in[idx].real();
                cpu_tmp_planar_bufs[1][idx] = cpu_in[idx].imag();
            }
        }
    }

    if(verbose > 1)
    {
        std::cout << "input:\n";
        for(size_t ibatch = 0; ibatch < batch; ++ibatch)
        {
            std::cout << "ibatch: " << ibatch << std::endl;
            for(size_t i = 0; i < N / 2 + 1; i++)
            {
                std::cout << cpu_in[howmany_dims[0].is * ibatch + dims[0].is * i] << " ";
            }
            std::cout << std::endl;
        }
    }
    if(verbose > 2)
    {
        std::cout << "flat input:\n";
        for(size_t i = 0; i < isize; ++i)
        {
            std::cout << cpu_in[i] << " ";
        }
        std::cout << std::endl;
    }

    if(in_array_type == rocfft_array_type_hermitian_interleaved)
    {
        hip_status = hipMemcpy(
            gpu_in_bufs[0], cpu_in, isize * sizeof(std::complex<Tfloat>), hipMemcpyHostToDevice);
        ASSERT_TRUE(hip_status == hipSuccess) << "hipMemcpy failure";
    }
    else
    {
        hip_status = hipMemcpy(
            gpu_in_bufs[0], cpu_tmp_planar_bufs[0], isize * sizeof(Tfloat), hipMemcpyHostToDevice);
        ASSERT_TRUE(hip_status == hipSuccess) << "hipMemcpy failure";
        hip_status = hipMemcpy(
            gpu_in_bufs[1], cpu_tmp_planar_bufs[1], isize * sizeof(Tfloat), hipMemcpyHostToDevice);
        ASSERT_TRUE(hip_status == hipSuccess) << "hipMemcpy failure";
    }

    // Execute the GPU transform:
    fft_status = rocfft_execute(gpu_plan, // plan
                                (void**)&gpu_in_bufs, // in_buffers
                                (void**)&gpu_out, // out_buffer
                                planinfo); // execution info
    ASSERT_TRUE(fft_status == rocfft_status_success) << "rocFFT plan execution failure";

    // Execute the CPU transform:
    fftw_execute_type<Tfloat>(cpu_plan);

    if(verbose > 1)
    {
        std::cout << "cpu_out:\n";
        for(size_t ibatch = 0; ibatch < batch; ++ibatch)
        {
            std::cout << "ibatch: " << ibatch << std::endl;
            for(size_t i = 0; i < dims[0].n; i++)
            {
                std::cout << cpu_out[howmany_dims[0].os * ibatch + dims[0].os * i] << " ";
            }
            std::cout << std::endl;
        }
    }
    if(verbose > 2)
    {
        std::cout << "flat cpu output:\n";
        for(size_t i = 0; i < osize; ++i)
        {
            std::cout << cpu_out[i] << " ";
        }
        std::cout << std::endl;
    }

    // Copy the data back and compare:
    std::vector<Tfloat> gpu_out_comp(osize);
    hip_status
        = hipMemcpy(gpu_out_comp.data(), gpu_out, osize * sizeof(Tfloat), hipMemcpyDeviceToHost);
    ASSERT_TRUE(hip_status == hipSuccess) << "hipMalloc failure";

    if(verbose > 1)
    {
        std::cout << "gpu_out:\n";
        for(size_t ibatch = 0; ibatch < batch; ++ibatch)
        {
            std::cout << "ibatch: " << ibatch << std::endl;
            for(size_t i = 0; i < dims[0].n; i++)
            {
                std::cout << gpu_out_comp[howmany_dims[0].os * ibatch + dims[0].os * i] << " ";
            }
            std::cout << std::endl;
        }
    }
    if(verbose > 2)
    {
        std::cout << "flat gpu output:\n";
        for(size_t i = 0; i < osize; ++i)
        {
            std::cout << gpu_out_comp[i] << " ";
        }
        std::cout << std::endl;
    }

    Tfloat L2diff   = 0.0;
    Tfloat Linfdiff = 0.0;
    Tfloat L2norm   = 0.0;
    Tfloat Linfnorm = 0.0;
    for(size_t ibatch = 0; ibatch < batch; ++ibatch)
    {
        for(size_t i = 0; i < N; i++)
        {
            const int pos  = howmany_dims[0].os * ibatch + dims[0].os * i;
            Tfloat    diff = std::abs(cpu_out[pos] - gpu_out_comp[pos]);
            Linfnorm       = std::max(std::abs(cpu_out[pos]), Linfnorm);
            L2norm += std::abs(cpu_out[pos]) * std::abs(cpu_out[pos]);
            Linfdiff = std::max(diff, Linfdiff);
            L2diff += diff * diff;
        }
    }
    L2norm = sqrt(L2norm);
    L2diff = sqrt(L2diff);
    ASSERT_TRUE(std::isfinite(L2norm)) << "L2norm isn't finite: " << L2norm;
    ASSERT_TRUE(std::isfinite(Linfnorm)) << "Linfnorm isn't finite: " << Linfnorm;

    Tfloat L2error   = L2diff / (L2norm * sqrt(log(N)));
    Tfloat Linferror = Linfdiff / (Linfnorm * log(N));
    if(verbose)
    {
        std::cout << "relative L2 error: " << L2error << std::endl;
        std::cout << "relative Linf error: " << Linferror << std::endl;
    }

    EXPECT_TRUE(L2error < type_epsilon<Tfloat>())
        << "Tolerance failure: L2error: " << L2error << ", tolerance: " << type_epsilon<Tfloat>();
    EXPECT_TRUE(Linferror < type_epsilon<Tfloat>()) << "Tolerance failure: Linferror: " << Linferror
                                                    << ", tolerance: " << type_epsilon<Tfloat>();

    // Free GPU memory:
    hipFree(gpu_in_bufs[0]);
    hipFree(gpu_in_bufs[1]);
    fftw_free(cpu_in);
    if(in_array_type == rocfft_array_type_hermitian_planar)
    {
        delete[] cpu_tmp_planar_bufs[0];
        delete[] cpu_tmp_planar_bufs[1];
    }
    if(!inplace)
    {
        hipFree(gpu_out);
        fftw_free(cpu_out);
    }
    if(gpu_planwbuffer != NULL)
    {
        hipFree(gpu_planwbuffer);
    }

    // Destroy plans:
    rocfft_execution_info_destroy(planinfo);
    rocfft_plan_description_destroy(gpu_description);
    rocfft_plan_destroy(gpu_plan);
    fftw_destroy_plan_type(cpu_plan);
}

TEST_P(accuracy_test_real, normal_1D_complex_to_real_single_precision)
{
    size_t                  N         = std::get<0>(GetParam());
    size_t                  batch     = std::get<1>(GetParam());
    rocfft_result_placement placeness = std::get<2>(GetParam());
    size_t                  stride    = std::get<3>(GetParam());
    rocfft_transform_type   transform_type
        = rocfft_transform_type_real_inverse; // must be real inverse
    rocfft_array_type in_array_type = std::get<4>(GetParam());

    try
    {
        normal_1D_complex_to_real<float>(
            N, batch, placeness, transform_type, stride, in_array_type);
    }
    catch(const std::exception& err)
    {
        handle_exception(err);
    }
}

TEST_P(accuracy_test_real, normal_1D_complex_to_real_double_precision)
{
    size_t                  N         = std::get<0>(GetParam());
    size_t                  batch     = std::get<1>(GetParam());
    rocfft_result_placement placeness = std::get<2>(GetParam());
    size_t                  stride    = std::get<3>(GetParam());
    rocfft_transform_type   transform_type
        = rocfft_transform_type_real_inverse; // must be real inverse
    rocfft_array_type in_array_type = std::get<4>(GetParam());

    try
    {
        normal_1D_complex_to_real<double>(
            N, batch, placeness, transform_type, stride, in_array_type);
    }
    catch(const std::exception& err)
    {
        handle_exception(err);
    }
}

// COMPLEX TO COMPLEX
INSTANTIATE_TEST_CASE_P(rocfft_pow2_1D,
                        accuracy_test_complex,
                        ::testing::Combine(ValuesIn(pow2_range),
                                           ValuesIn(batch_range),
                                           ValuesIn(placeness_range),
                                           ValuesIn(stride_range),
                                           ValuesIn(transform_range),
                                           ValuesIn(c2c_array_range),
                                           ValuesIn(c2c_array_range)));

INSTANTIATE_TEST_CASE_P(rocfft_pow3_1D,
                        accuracy_test_complex,
                        ::testing::Combine(ValuesIn(pow3_range),
                                           ValuesIn(batch_range),
                                           ValuesIn(placeness_range),
                                           ValuesIn(stride_range),
                                           ValuesIn(transform_range),
                                           ValuesIn(c2c_array_range),
                                           ValuesIn(c2c_array_range)));

INSTANTIATE_TEST_CASE_P(rocfft_pow5_1D,
                        accuracy_test_complex,
                        ::testing::Combine(ValuesIn(pow5_range),
                                           ValuesIn(batch_range),
                                           ValuesIn(placeness_range),
                                           ValuesIn(stride_range),
                                           ValuesIn(transform_range),
                                           ValuesIn(c2c_array_range),
                                           ValuesIn(c2c_array_range)));

INSTANTIATE_TEST_CASE_P(rocfft_pow_mix_1D,
                        accuracy_test_complex,
                        ::testing::Combine(ValuesIn(mix_range),
                                           ValuesIn(batch_range),
                                           ValuesIn(placeness_range),
                                           ValuesIn(stride_range),
                                           ValuesIn(transform_range),
                                           ValuesIn(c2c_array_range),
                                           ValuesIn(c2c_array_range)));

INSTANTIATE_TEST_CASE_P(rocfft_pow_random_1D,
                        accuracy_test_complex,
                        ::testing::Combine(ValuesIn(generate_random(20)),
                                           ValuesIn(batch_range),
                                           ValuesIn(placeness_range),
                                           ValuesIn(stride_range),
                                           ValuesIn(transform_range),
                                           ValuesIn(c2c_array_range),
                                           ValuesIn(c2c_array_range)));

INSTANTIATE_TEST_CASE_P(rocfft_prime_1D,
                        accuracy_test_complex,
                        ::testing::Combine(ValuesIn(prime_range),
                                           ValuesIn(batch_range),
                                           ValuesIn(placeness_range),
                                           ValuesIn(stride_range_for_prime),
                                           ValuesIn(transform_range),
                                           ValuesIn(c2c_array_range),
                                           ValuesIn(c2c_array_range)));

// REAL <-> COMPLEX
INSTANTIATE_TEST_CASE_P(rocfft_pow2_1D,
                        accuracy_test_real,
                        ::testing::Combine(ValuesIn(pow2_range),
                                           ValuesIn(batch_range),
                                           ValuesIn(placeness_range),
                                           ValuesIn(stride_range),
                                           ValuesIn(r2c_array_range)));

INSTANTIATE_TEST_CASE_P(rocfft_pow3_1D,
                        accuracy_test_real,
                        ::testing::Combine(ValuesIn(pow3_range),
                                           ValuesIn(batch_range),
                                           ValuesIn(placeness_range),
                                           ValuesIn(stride_range),
                                           ValuesIn(r2c_array_range)));

INSTANTIATE_TEST_CASE_P(rocfft_pow5_1D,
                        accuracy_test_real,
                        ::testing::Combine(ValuesIn(pow5_range),
                                           ValuesIn(batch_range),
                                           ValuesIn(placeness_range),
                                           ValuesIn(stride_range),
                                           ValuesIn(r2c_array_range)));

INSTANTIATE_TEST_CASE_P(rocfft_pow_mix_1D,
                        accuracy_test_real,
                        ::testing::Combine(ValuesIn(mix_range),
                                           ValuesIn(batch_range),
                                           ValuesIn(placeness_range),
                                           ValuesIn(stride_range),
                                           ValuesIn(r2c_array_range)));

INSTANTIATE_TEST_CASE_P(rocfft_prime_1D,
                        accuracy_test_real,
                        ::testing::Combine(ValuesIn(prime_range),
                                           ValuesIn(batch_range),
                                           ValuesIn(placeness_range),
                                           ValuesIn(stride_range),
                                           ValuesIn(r2c_array_range)));

// TESTS disabled by default since they take a long time to execute
// TO enable this tests
// 1. make sure ENV CLFFT_REQUEST_LIB_NOMEMALLOC=1
// 2. pass --gtest_also_run_disabled_tests to TEST.exe

// #define CLFFT_TEST_HUGE
// #ifdef CLFFT_TEST_HUGE

// #define HUGE_TEST_MAKE(test_name, len, bat)                                              \
//     template <class T>                                                                   \
//     void test_name()                                                                     \
//     {                                                                                    \
//         std::vector<size_t> lengths;                                                     \
//         lengths.push_back(len);                                                          \
//         size_t batch = bat;                                                              \
//                                                                                          \
//         std::vector<size_t>     input_strides;                                           \
//         std::vector<size_t>     output_strides;                                          \
//         size_t                  input_distance  = 0;                                     \
//         size_t                  output_distance = 0;                                     \
//         rocfft_array_type       in_array_type   = rocfft_array_type_complex_planar;      \
//         rocfft_array_type       out_array_type  = rocfft_array_type_complex_planar;      \
//         rocfft_result_placement placeness       = rocfft_placement_inplace;              \
//         rocfft_transform_type   transform_type  = rocfft_transform_type_complex_forward; \
//                                                                                          \
//         data_pattern pattern = sawtooth;                                                 \
//         complex_to_complex<T>(pattern,                                                   \
//                               transform_type,                                            \
//                               lengths,                                                   \
//                               batch,                                                     \
//                               input_strides,                                             \
//                               output_strides,                                            \
//                               input_distance,                                            \
//                               output_distance,                                           \
//                               in_array_type,                                             \
//                               out_array_type,                                            \
//                               placeness);                                                \
//     }

// #define SP_HUGE_TEST(test_name, len, bat)                \
//                                                          \
//     HUGE_TEST_MAKE(test_name, len, bat)                  \
//                                                          \
//     TEST_F(accuracy_test_complex_pow2_single, test_name) \
//     {                                                    \
//         try                                              \
//         {                                                \
//             test_name<float>();                          \
//         }                                                \
//         catch(const std::exception& err)                 \
//         {                                                \
//             handle_exception(err);                       \
//         }                                                \
//     }

// #define DP_HUGE_TEST(test_name, len, bat)                \
//                                                          \
//     HUGE_TEST_MAKE(test_name, len, bat)                  \
//                                                          \
//     TEST_F(accuracy_test_complex_pow2_double, test_name) \
//     {                                                    \
//         try                                              \
//         {                                                \
//             test_name<double>();                         \
//         }                                                \
//         catch(const std::exception& err)                 \
//         {                                                \
//             handle_exception(err);                       \
//         }                                                \
//     }

// SP_HUGE_TEST(DISABLED_huge_sp_test_1, 1048576, 11)
// SP_HUGE_TEST(DISABLED_huge_sp_test_2, 1048576 * 2, 7)
// SP_HUGE_TEST(DISABLED_huge_sp_test_3, 1048576 * 4, 3)
// SP_HUGE_TEST(DISABLED_huge_sp_test_4, 1048576 * 8, 5)
// SP_HUGE_TEST(DISABLED_huge_sp_test_5, 1048576 * 16, 3)
// SP_HUGE_TEST(DISABLED_huge_sp_test_6, 1048576 * 32, 2)
// SP_HUGE_TEST(DISABLED_huge_sp_test_7, 1048576 * 64, 1)

// DP_HUGE_TEST(DISABLED_huge_dp_test_1, 524288, 11)
// DP_HUGE_TEST(DISABLED_huge_dp_test_2, 524288 * 2, 7)
// DP_HUGE_TEST(DISABLED_huge_dp_test_3, 524288 * 4, 3)
// DP_HUGE_TEST(DISABLED_huge_dp_test_4, 524288 * 8, 5)
// DP_HUGE_TEST(DISABLED_huge_dp_test_5, 524288 * 16, 3)
// DP_HUGE_TEST(DISABLED_huge_dp_test_6, 524288 * 32, 2)
// DP_HUGE_TEST(DISABLED_huge_dp_test_7, 524288 * 64, 1)

// SP_HUGE_TEST(DISABLED_large_sp_test_1, 8192, 11)
// SP_HUGE_TEST(DISABLED_large_sp_test_2, 8192 * 2, 7)
// SP_HUGE_TEST(DISABLED_large_sp_test_3, 8192 * 4, 3)
// SP_HUGE_TEST(DISABLED_large_sp_test_4, 8192 * 8, 5)
// SP_HUGE_TEST(DISABLED_large_sp_test_5, 8192 * 16, 3)
// SP_HUGE_TEST(DISABLED_large_sp_test_6, 8192 * 32, 21)
// SP_HUGE_TEST(DISABLED_large_sp_test_7, 8192 * 64, 17)

// DP_HUGE_TEST(DISABLED_large_dp_test_1, 4096, 11)
// DP_HUGE_TEST(DISABLED_large_dp_test_2, 4096 * 2, 7)
// DP_HUGE_TEST(DISABLED_large_dp_test_3, 4096 * 4, 3)
// DP_HUGE_TEST(DISABLED_large_dp_test_4, 4096 * 8, 5)
// DP_HUGE_TEST(DISABLED_large_dp_test_5, 4096 * 16, 3)
// DP_HUGE_TEST(DISABLED_large_dp_test_6, 4096 * 32, 21)
// DP_HUGE_TEST(DISABLED_large_dp_test_7, 4096 * 64, 17)

// #endif
