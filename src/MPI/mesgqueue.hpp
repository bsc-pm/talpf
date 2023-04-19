
/*
 *	 Copyright 2021 Huawei Technologies Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LPF_CORE_MESGQUEUE_HPP
#define LPF_CORE_MESGQUEUE_HPP

#include <vector>
#include <iosfwd>
#include "memorytable.hpp"
#include "types.hpp"
#include "vall2all.hpp"
#include "messagesort.hpp"
#include "mpilib.hpp"
#include "linkage.hpp"

#if __cplusplus >= 201103L
#include <memory>
#else
#include <tr1/memory>
#endif

#include "ibverbs.hpp"

namespace lpf {

class _LPFLIB_LOCAL MessageQueue
{
public:
	explicit MessageQueue( Communication & comm );

	err_t resizeMemreg( size_t nRegs );
	err_t resizeMesgQueue( size_t nMsgs );


	memslot_t addLocalReg( void * mem, std::size_t size );
	memslot_t addGlobalReg( void * mem, std::size_t size );
	void	  removeReg( memslot_t slot );

	void get( pid_t srcPid, memslot_t srcSlot, size_t srcOffset,
			memslot_t dstSlot, size_t dstOffset, size_t size );

	void put( memslot_t srcSlot, size_t srcOffset,
			pid_t dstPid, memslot_t dstSlot, size_t dstOffset, size_t size );

	void atomic_fetch_and_add( memslot_t srcSlot, size_t srcOffset,
			pid_t dstPid, memslot_t dstSlot, size_t dstOffset, uint64_t value );

	void atomic_cmp_and_swp( memslot_t srcSlot, size_t srcOffset,
        		pid_t dstPid, memslot_t dstSlot, size_t dstOffset, uint64_t cmp, uint64_t swp );

	// returns how many processes have entered in an aborted state
	int sync( bool abort, lpf_sync_attr_t attr );

private:


	const pid_t m_pid, m_nprocs;
	const size_t m_memRange;
	std::vector< int > m_vote;
	size_t m_maxNMsgs;
	size_t m_nextMemRegSize;
	bool m_resized;
	MessageSort m_msgsort;
	mpi::Comm m_comm;
	mpi::IBVerbs m_ibverbs;
	MemoryTable m_memreg;
};


}  // namespace lpf


#endif
