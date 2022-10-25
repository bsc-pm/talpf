
/*
 *   Copyright 2021 Huawei Technologies Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <lpf/core.h>
#include "Test.h"

void spmd( lpf_t lpf, lpf_pid_t pid, lpf_pid_t nprocs, lpf_args_t args)
{
    (void) args; // ignore args parameter

    lpf_err_t rc = LPF_SUCCESS;
    const int n = nprocs;
    int i;
    int * xs, *ys;
    ys = malloc( sizeof(ys[0]) * n);
    xs = malloc( sizeof(xs[0]) * n);
    for (i = 0; i < n; ++i)
    {
        xs[i] = i;
        ys[i] = 0;
    }
        
    rc = lpf_resize_message_queue( lpf, 2*nprocs);
    EXPECT_EQ( "%d", LPF_SUCCESS, rc );
    rc = lpf_resize_memory_register( lpf, 2 );
    EXPECT_EQ( "%d", LPF_SUCCESS, rc );
	#pragma oss task
    rc = lpf_sync( lpf, LPF_SYNC_DEFAULT );
	#pragma oss taskwait
    EXPECT_EQ( "%d", LPF_SUCCESS, rc );
 
    lpf_memslot_t xslot = LPF_INVALID_MEMSLOT;
    lpf_memslot_t yslot = LPF_INVALID_MEMSLOT;
    rc = lpf_register_local( lpf, xs, sizeof(xs[0]) * n, &xslot );
    EXPECT_EQ( "%d", LPF_SUCCESS, rc );
    rc = lpf_register_global( lpf, ys, sizeof(ys[0]) * n, &yslot );
    EXPECT_EQ( "%d", LPF_SUCCESS, rc );

	#pragma oss task
    rc = lpf_sync( lpf, LPF_SYNC_DEFAULT);
	#pragma oss taskwait
    EXPECT_EQ( "%d", LPF_SUCCESS, rc );


    // Check that data is OK.
    for (i = 0; i < n; ++i)
    {
        EXPECT_EQ( "%d", i, xs[i] );
        EXPECT_EQ( "%d", 0, ys[i] );
    }

    // Do an all-to-all which is like transposing a matrix
    // ( pid, xs[i] ) -> ( i, ys[ pid ] )
    for ( i = 0; i < n; ++ i)
    {
	#pragma oss task
        rc = lpf_put( lpf, xslot, sizeof(xs[0])*i, 
                i, yslot, sizeof(ys[0])*pid, sizeof(xs[0]), 
                LPF_MSG_DEFAULT );
        EXPECT_EQ( "%d", LPF_SUCCESS, rc );
    }
	#pragma oss taskwait

        
	#pragma oss task
    rc = lpf_sync( lpf, LPF_SYNC_DEFAULT );
	#pragma oss taskwait
    EXPECT_EQ( "%d", LPF_SUCCESS, rc );

    for (i = 0; i < n; ++i)
    {
        EXPECT_EQ( "%d", i, xs[i] );
        EXPECT_EQ( "%d", (int) pid, ys[i] );
    }

    rc = lpf_deregister( lpf, xslot );
    EXPECT_EQ( "%d", LPF_SUCCESS, rc );

    rc = lpf_deregister( lpf, yslot );
    EXPECT_EQ( "%d", LPF_SUCCESS, rc );
}

/** 
 * \test Test lpf_put by doing an all-to-all
 * \pre P >= 1
 * \return Exit code: 0
 */
TEST( func_lpf_put_parallel_alltoall )
{
    lpf_err_t rc = lpf_exec( LPF_ROOT, LPF_MAX_P, spmd, LPF_NO_ARGS);
    EXPECT_EQ( "%d", LPF_SUCCESS, rc );
    return 0;
}
