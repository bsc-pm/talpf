
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
#include <string.h>
#include "Test.h"

void spmd( lpf_t lpf, lpf_pid_t pid, lpf_pid_t nprocs, lpf_args_t args )
{
    (void) args;  // ignore args parameter passed by lpf_exec

    char a[1] = { 'i' };
    char b[2] = { 'p', 'q' };
    char c[3] = { 'a', 'b', 'c'};

    lpf_memslot_t aSlot = LPF_INVALID_MEMSLOT;
    lpf_memslot_t cSlot = LPF_INVALID_MEMSLOT;
    lpf_memslot_t xSlot[10];
    lpf_err_t rc = LPF_SUCCESS;

    EXPECT_LT( "%u", pid, nprocs );

    rc = lpf_resize_message_queue( lpf, 1);
    EXPECT_EQ( "%d", LPF_SUCCESS, rc );
    rc = lpf_resize_memory_register( lpf, 2 + nprocs);
    EXPECT_EQ( "%d", LPF_SUCCESS, rc );
	#pragma oss task
    rc = lpf_sync( lpf, LPF_SYNC_DEFAULT );
	#pragma oss taskwait
    EXPECT_EQ( "%d", LPF_SUCCESS, rc );

    lpf_pid_t i;
    for (i = 0; i < pid; ++i)
    {
        int x = 0;
        rc = lpf_register_local( lpf, &x, sizeof(x)+pid, &xSlot[i] );
        EXPECT_EQ( "%d", LPF_SUCCESS, rc );
    }

    rc = lpf_register_global( lpf, &a, sizeof(a), &aSlot );
    EXPECT_EQ( "%d", LPF_SUCCESS, rc );

	#pragma oss task
    rc = lpf_sync( lpf, LPF_SYNC_DEFAULT );
	#pragma oss taskwait
    EXPECT_EQ( "%d", LPF_SUCCESS, rc );

    EXPECT_EQ( "%c", 'i', a[0]);
    EXPECT_EQ( "%c", 'p', b[0]);
    EXPECT_EQ( "%c", 'q', b[1]);
    EXPECT_EQ( "%c", 'a', c[0]);
    EXPECT_EQ( "%c", 'b', c[1]);
    EXPECT_EQ( "%c", 'c', c[2]);

    if ( 1 == pid )
    {
        rc = lpf_register_local( lpf, &c, sizeof(c), &cSlot );
        EXPECT_EQ( "%d", LPF_SUCCESS, rc );

	#pragma oss task
        rc = lpf_put( lpf, cSlot, 1u * sizeof(c[0]), 
                0u, aSlot, 0u*sizeof(a[0]), sizeof(a[0]), LPF_MSG_DEFAULT );
	#pragma oss taskwait
        EXPECT_EQ( "%d", LPF_SUCCESS, rc );
    }

	#pragma oss task
    rc = lpf_sync( lpf, LPF_SYNC_DEFAULT );
	#pragma oss taskwait
    EXPECT_EQ( "%d", LPF_SUCCESS, rc );

    EXPECT_EQ( "%c", 0 == pid ? 'b' : 'i', a[0]);
    EXPECT_EQ( "%c", 'p', b[0]);
    EXPECT_EQ( "%c", 'q', b[1]);
    EXPECT_EQ( "%c", 'a', c[0]);
    EXPECT_EQ( "%c", 'b', c[1]);
    EXPECT_EQ( "%c", 'c', c[2]);

    if ( 1 == pid) lpf_deregister( lpf, cSlot );
    lpf_deregister( lpf, aSlot );
    for ( i = 0; i < pid; ++i)
        lpf_deregister( lpf, xSlot[i] );
}

/** 
 * \test Test registering a different number of local variables on each pid
 * \pre P >= 2
 * \pre P <= 10
 * \return Exit code: 0
 */
TEST( func_lpf_register_local_parallel_multiple )
{
    lpf_exec( LPF_ROOT, LPF_MAX_P, &spmd, LPF_NO_ARGS);
    return 0;
}
