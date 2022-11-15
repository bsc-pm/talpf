
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

#ifndef LPF_CORE_MPI_IBVERBS_HPP
#define LPF_CORE_MPI_IBVERBS_HPP

#include <atomic>
#include <string>
#include <vector>
#if __cplusplus >= 201103L	  
  #include <memory>
#else
  #include <tr1/memory>
#endif

#include <infiniband/verbs.h>


#include "linkage.hpp"
#include "sparseset.hpp"
#include "memreg.hpp"

namespace lpf {
	
	class Communication;
	
	namespace mpi {

#if __cplusplus >= 201103L	  
using std::shared_ptr;
#else
using std::tr1::shared_ptr;
#endif

class _LPFLIB_LOCAL IBVerbs 
{
public:
	struct Exception;

	typedef size_t SlotID;

	explicit IBVerbs( Communication & );
	~IBVerbs();

	void resizeMemreg( size_t size );
	void resizeMesgq( size_t size );
	
	SlotID regLocal( void * addr, size_t size );
	SlotID regGlobal( void * addr, size_t size );
	void dereg( SlotID id );

	void put( SlotID srcSlot, size_t srcOffset, 
			  int dstPid, SlotID dstSlot, size_t dstOffset, size_t size );

	void get( int srcPid, SlotID srcSlot, size_t srcOffset, 
			 SlotID dstSlot, size_t dstOffset, size_t size );

	void atomic_fetch_and_add(SlotID srcSlot, size_t srcOffset,
			int dstPid, SlotID dstSlot, size_t dstOffset, uint64_t value);

	void atomic_cmp_and_swp(SlotID srcSlot, size_t srcOffset,
			int dstPid, SlotID dstSlot, size_t dstOffset, uint64_t cmp, uint64_t swp);

	// Do the communication and synchronize
	// 'Reconnect' must be a globally replicated value
	void sync( bool reconnect, int attr );
#ifdef TASK_AWARENESS
	void doProgress();
	void stopProgress();
#endif
private:

	IBVerbs & operator=(const IBVerbs & ); // assignment prohibited
	IBVerbs( const IBVerbs & ); // copying prohibited

	void stageQPs(size_t maxMsgs ); 
	void reconnectQPs(); 

#ifdef TASK_AWARENESS
	void doLocalProgress();
	void doRemoteProgress();
	void processSyncRequest();
	void getEnvPollingFrequency();
#endif


	struct MemoryRegistration {
		void *	 addr;
		size_t	 size;
		uint32_t lkey;
		uint32_t rkey;
	};

	struct MemorySlot {
		shared_ptr< struct ibv_mr > mr;    // verbs structure
		std::vector< MemoryRegistration > glob; // array for global registrations
	};

#ifdef TASK_AWARENESS
	struct SyncRequest {
		bool	isActive;
		bool 	withBarrier;
		int 	remoteMsgs;
		void *	counter;
		bool	secondPhase;
		void *	barrierRequest;
	};

	SyncRequest syncRequest;
	uint32_t pollingFrequency;
#endif

	int			 m_pid; // local process ID
	int			 m_nprocs; // number of processes

	std::string  m_devName; // IB device name
	int			 m_ibPort;	// local IB port to work with
	int			 m_gidIdx; 
	uint16_t	 m_lid;		// LID of the IB port
	ibv_mtu		 m_mtu;   
	struct ibv_device_attr m_deviceAttr;
	size_t		 m_maxRegSize;
	size_t		 m_maxMsgSize; 
	size_t		 m_minNrMsgs;
	size_t		 m_maxSrs; // maximum number of sends requests per QP  


	shared_ptr< struct ibv_context > m_device; // device handle
	shared_ptr< struct ibv_pd >		 m_pd;	   // protection domain
#ifdef TASK_AWARENESS
	shared_ptr< struct ibv_cq >		 m_cqLocal;		// complation queue
	shared_ptr< struct ibv_cq >		 m_cqRemote;	 // complation queue
#else
	shared_ptr< struct ibv_cq >		 m_cq;	   // complation queue
#endif

	// Disconnected queue pairs
	std::vector< shared_ptr< struct ibv_qp > > m_stagedQps; 

	// Connected queue pairs
	std::vector< shared_ptr< struct ibv_qp > > m_connectedQps; 

#ifdef TASK_AWARENESS
	std::atomic_int	m_numMsgs;
	std::atomic_int	*m_numMsgsSync;
	int		m_recvCount;
	std::atomic_int	m_stopProgress;
	size_t		m_postCount;
	size_t		m_cqSize;
	bool		m_sync_cached;
	int		m_sync_cached_value;
#else
	std::vector< struct ibv_send_wr >	m_srs; // array of send requests	 
	std::vector< size_t >			m_srsHeads; // head of send queue per peer	   
	std::vector< size_t >			m_nMsgsPerPeer; // number of messages per peer 
#endif


	SparseSet< pid_t >			 m_activePeers; // 
	std::vector< pid_t >		 m_peerList;

#ifndef TASK_AWARENESS 
	std::vector< struct ibv_sge > m_sges; // array of scatter/gather entries
	std::vector< struct ibv_wc > m_wcs; // array of work completions 
#endif

	CombinedMemoryRegister< MemorySlot > m_memreg;


	shared_ptr< struct ibv_mr > m_dummyMemReg; // registration of dummy buffer
	std::vector< char > m_dummyBuffer; // dummy receive buffer

	Communication & m_comm;
};



} }


#endif
