
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

void spmd( lpf_t ctx, lpf_pid_t pid, lpf_pid_t nprocs, lpf_args_t args )
{
    // ignore the following parameters:
    (void) pid; 
    (void) nprocs; 
    (void) args; 

    lpf_err_t rc = LPF_SUCCESS;
        
    size_t maxMsgs = 5 , maxRegs = 7;
    rc = lpf_resize_message_queue( ctx, maxMsgs);
    EXPECT_EQ( "%d", LPF_SUCCESS, rc );
    rc = lpf_resize_memory_register( ctx, maxRegs );
    EXPECT_EQ( "%d", LPF_SUCCESS, rc );

	#pragma oss task
    rc = lpf_sync( ctx, LPF_SYNC_DEFAULT );
	#pragma oss taskwait
    EXPECT_EQ( "%d", LPF_SUCCESS, rc );

}


/** 
 * \test Test lpf_resize function in parallel and set maxMsgs to five
 * \pre P >= 1
 * \return Exit code: 0
 */
TEST( func_lpf_resize_parallel_five )
{
    lpf_err_t rc = lpf_exec( LPF_ROOT, LPF_MAX_P, &spmd, LPF_NO_ARGS);
    EXPECT_EQ( "%d", LPF_SUCCESS, rc);
    return 0;
}
