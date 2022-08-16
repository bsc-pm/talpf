
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

#include "ibverbs.hpp"
#include "log.hpp"
#include "communication.hpp"
#include "config.hpp"

#include <stdexcept>
#include <cstring>

#include <nanos6.h>
#include <nanos6/library-mode.h>

namespace lpf { namespace mpi {


struct IBVerbs::Exception : std::runtime_error { 
    Exception(const char * what) : std::runtime_error( what ) {}
};

namespace {
    ibv_mtu getMTU( unsigned  size ) {
        switch (size) {
            case 256: return IBV_MTU_256;
            case 512: return IBV_MTU_512;
            case 1024: return IBV_MTU_1024;
            case 2048: return IBV_MTU_2048;
            case 4096: return IBV_MTU_4096;
            default: throw IBVerbs::Exception("Illegal MTU size");
        }
        return IBV_MTU_4096;
    }
}

void pollingTask(void *args){
	IBVerbs *v = (IBVerbs *) args;
	v->doProgress();
}
void endPollingTask(void *args){
//	IBVerbs *v = (IBVerbs *) args;
}


IBVerbs :: IBVerbs( Communication & comm )
    : m_pid( comm.pid() )
    , m_nprocs( comm.nprocs() )
    , m_devName()
    , m_ibPort( Config::instance().getIBPort() )
    , m_gidIdx( Config::instance().getIBGidIndex() )
    , m_mtu( getMTU( Config::instance().getIBMTU() ))
    , m_maxRegSize(0)
    , m_maxMsgSize(0)
    , m_minNrMsgs(0)
    , m_maxSrs(0)
    , m_numMsgs()
    //, m_numMsgsSync()
    , m_recvCount(0)
    , m_stopProgress(0)
    , m_postCount(0)
    , m_memregSize(1)
    , m_cqSize(1)
    , m_device()
    , m_pd()
    , m_cqLocal()
    , m_cqRemote()
    , m_stagedQps( m_nprocs )
    , m_connectedQps( m_nprocs )
    , m_activePeers(0, m_nprocs)
    , m_peerList()
    , m_memreg()
    , m_dummyMemReg()
    , m_dummyBuffer() 
    , m_comm( comm )
{
    m_peerList.reserve( m_nprocs );

    int numDevices = -1;
    struct ibv_device * * const try_get_device_list = ibv_get_device_list( &numDevices );

    if (!try_get_device_list) {
        LOG(1, "Cannot get list of Infiniband devices" );
        throw Exception( "failed to get IB devices list");
    }

    shared_ptr< struct ibv_device * > devList(
            try_get_device_list,
            ibv_free_device_list );

    LOG(3, "Retrieved Infiniband device list, which has " << numDevices 
            << " devices"  );

    if (numDevices < 1) {
        LOG(1, "There are " << numDevices << " Infiniband devices"
                " available, which is not enough" );
        throw Exception( "No Infiniband devices available" );
    }


    std::string wantDevName = Config::instance().getIBDeviceName();
    LOG( 3, "Searching for device '"<< wantDevName << "'" );
    struct ibv_device * dev = NULL;
    for (int i = 0; i < numDevices; i ++) 
    {
        std::string name = ibv_get_device_name( (&*devList)[i]);
        LOG(3, "Device " << i << " has name '" << name << "'" );
        if ( wantDevName.empty() || name == wantDevName ) {
            LOG(3, "Found device '" << name << "'" );
            m_devName = name;
            dev = (&*devList)[i];
            break;
        }
    }

    if (dev == NULL) {
        LOG(1, "Could not find device '" << wantDevName << "'" );
        throw Exception("Infiniband device not found");
    }

    m_device.reset( ibv_open_device(dev), ibv_close_device );
    if (!m_device) {
        LOG(1, "Failed to open Infiniband device '" << m_devName << "'");
        throw Exception("Cannot open IB device");
    }
    LOG(3, "Opened Infiniband device '" << m_devName << "'" );

    devList.reset();
    LOG(3, "Closed Infiniband device list" );

    std::memset(&m_deviceAttr, 0, sizeof(m_deviceAttr));
    if (ibv_query_device( m_device.get(), &m_deviceAttr ))
        throw Exception("Cannot query device");

    LOG(3, "Queried IB device capabilities" );

    m_maxRegSize = m_deviceAttr.max_mr_size;
    LOG(3, "Maximum size for memory registration = " << m_maxRegSize );

    // maximum number of work requests per Queue Pair
    m_maxSrs = std::min<size_t>( m_deviceAttr.max_qp_wr, // maximum work requests per QP
                                 m_deviceAttr.max_cqe ); // maximum entries per CQ
    LOG(3, "Maximum number of send requests is the minimum of "
            << m_deviceAttr.max_qp_wr << " (the maximum of work requests per QP)"
            << " and " << m_deviceAttr.max_cqe << " (the maximum of completion "
            << " queue entries per QP), nameley " << m_maxSrs );

    if ( m_deviceAttr.max_cqe < m_nprocs )
        throw Exception("Completion queue has insufficient completion queue capabilities");

    struct ibv_port_attr port_attr; std::memset( &port_attr, 0, sizeof(port_attr));
    if (ibv_query_port( m_device.get(), m_ibPort, & port_attr )) 
        throw Exception("Cannot query IB port");
 
    LOG(3, "Queried IB port " << m_ibPort << " capabilities" );
    
    // store Maximum message size
    m_maxMsgSize = port_attr.max_msg_sz;
    LOG(3, "Maximum IB message size is " << m_maxMsgSize );

    size_t sysRam = Config::instance().getLocalRamSize();
    m_minNrMsgs = sysRam  / m_maxMsgSize;
    LOG(3, "Minimum number of messages to allocate = "
            "total system RAM / maximum message size = " 
            <<  sysRam << " / " << m_maxMsgSize << " = "  << m_minNrMsgs );
 
    // store LID
    m_lid = port_attr.lid;
    LOG(3, "LID is " << m_lid );

    m_pd.reset( ibv_alloc_pd( m_device.get()), ibv_dealloc_pd );
    if (!m_pd)
        throw Exception("Could not allocate protection domain");

    LOG(3, "Opened protection domain");

    m_cqLocal.reset( ibv_create_cq( m_device.get(), m_cqSize, NULL, NULL, 0) ); //tmp cq
    if (!m_cqLocal) {
        LOG(1, "Could not allocate completion queue with '"
                << m_nprocs << " entries" );
        throw Exception("Could not allocate completion queue");
    }
    m_cqRemote.reset( ibv_create_cq( m_device.get(), m_cqSize * m_nprocs * m_memregSize, NULL, NULL, 0) ); //tmp cq
    if (!m_cqRemote) {
        LOG(1, "Could not allocate completion queue with '"
                << m_nprocs << " entries" );
        throw Exception("Could not allocate completion queue");
    }
    atomic_init(&m_numMsgs, 0);
    m_numMsgsSync = (std::atomic_int *) calloc(m_nprocs, sizeof(std::atomic_int)); 
    for(int i = 0; i < m_nprocs; i++)
    	atomic_init(&m_numMsgsSync[i], 0);

    nanos6_spawn_function(pollingTask, this, endPollingTask, this, "POLLING_TASK");

    LOG(3, "Allocated completion queue with " << m_nprocs << " entries.");

    // allocate dummy buffer
    m_dummyBuffer.resize( 8 );
    m_dummyMemReg.reset( ibv_reg_mr( m_pd.get(),
                m_dummyBuffer.data(), m_dummyBuffer.size(),
                IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE ),
            ibv_dereg_mr );


    // Wait for all peers to finish
    LOG(3, "Queue pairs have been successfully initialized");
}

IBVerbs :: ~IBVerbs()
{
	stopProgress();
}

void IBVerbs :: stageQPs( size_t maxMsgs ) 
{
    // create the queue pairs
    for ( int i = 0; i < m_nprocs; ++i) {
        struct ibv_qp_init_attr attr;
        std::memset(&attr, 0, sizeof(attr));

        attr.qp_type = IBV_QPT_RC; // we want reliable connection
        attr.sq_sig_all = 0; // only wait for selected messages
        attr.send_cq = m_cqLocal.get();
        attr.recv_cq = m_cqRemote.get();
        attr.cap.max_send_wr = std::min<size_t>(maxMsgs + m_minNrMsgs,m_maxSrs);
        attr.cap.max_recv_wr = std::min<size_t>(maxMsgs + m_minNrMsgs,m_maxSrs); // one for the dummy 
        attr.cap.max_send_sge = 1;
        attr.cap.max_recv_sge = 1;

        m_stagedQps[i].reset( 
                ibv_create_qp( m_pd.get(), &attr ), 
                ibv_destroy_qp
        );

        if (!m_stagedQps[i]) {
            LOG( 1, "Could not create Infiniband Queue pair number " << i );
            throw std::bad_alloc();
        }

        LOG(3, "Created new Queue pair for " << m_pid << " -> " << i );
    }
}

void IBVerbs :: reconnectQPs() 
{
    ASSERT( m_stagedQps[0] );
    m_comm.barrier();

    union ibv_gid myGid;
    std::vector< uint32_t> localQpNums, remoteQpNums;
    std::vector< uint16_t> lids;
    std::vector< union ibv_gid > gids;
    try {
        // Exchange info about the queue pairs
        if (m_gidIdx >= 0) {
            if (ibv_query_gid(m_device.get(), m_ibPort, m_gidIdx, &myGid)) {
                LOG(1, "Could not get GID of Infiniband device port " << m_ibPort);
                throw Exception( "Could not get gid for IB port");
            }
            LOG(3, "GID of Infiniband device was retrieved" );
        }
        else {
            std::memset( &myGid, 0, sizeof(myGid) );
            LOG(3, "GID of Infiniband device will not be used" );
        }

        localQpNums.resize(m_nprocs);
        remoteQpNums.resize(m_nprocs);
        lids.resize(m_nprocs);
        gids.resize(m_nprocs);

        for ( int i = 0; i < m_nprocs; ++i)
            localQpNums[i] = m_stagedQps[i]->qp_num;
    }
    catch(...)
    {
        m_comm.allreduceOr( true );
        throw;
    }
    if (m_comm.allreduceOr( false) )
        throw Exception("Peer failed to allocate memory or query device while setting-up QP");
        
    m_comm.allToAll( localQpNums.data(), remoteQpNums.data() );
    m_comm.allgather( m_lid, lids.data() );
    m_comm.allgather( myGid, gids.data() );

    LOG(3, "Connection initialisation data has been exchanged");

    try {
        // Bring QPs to INIT
        for (int i = 0; i < m_nprocs; ++i ) {
            struct ibv_qp_attr  attr;
            int                 flags;

            std::memset(&attr, 0, sizeof(attr));
            attr.qp_state = IBV_QPS_INIT;
            attr.port_num = m_ibPort;
            attr.pkey_index = 0;
            attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
            flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
            if ( ibv_modify_qp(m_stagedQps[i].get(), &attr, flags) ) {
                LOG(1, "Cannot bring state of QP " << i << " to INIT");
                throw Exception("Failed to bring QP's state to Init" );
            }

            // post a dummy receive
            
            struct ibv_recv_wr rr;  std::memset(&rr, 0, sizeof(rr));
            struct ibv_sge     sge; std::memset(&sge, 0, sizeof(sge));
            struct ibv_recv_wr *bad_wr = NULL;
            sge.addr = reinterpret_cast<uintptr_t>(m_dummyBuffer.data());
            sge.length = m_dummyBuffer.size();
            sge.lkey = m_dummyMemReg->lkey;
            rr.next = NULL;
            rr.wr_id = 0;
            rr.sg_list = &sge;
            rr.num_sge = 1;

            if (ibv_post_recv(m_stagedQps[i].get(), &rr, &bad_wr)) {
                LOG(1, "Cannot post a single receive request to QP " << i );
                throw Exception("Could not post dummy receive request");
            }

            // Bring QP to RTR
            std::memset(&attr, 0, sizeof(attr));
            attr.qp_state = IBV_QPS_RTR;
            attr.path_mtu = m_mtu;
            attr.dest_qp_num = remoteQpNums[i];
            attr.rq_psn = 0;
            attr.max_dest_rd_atomic = 1;
            attr.min_rnr_timer = 0x12;
            attr.ah_attr.is_global = 0;
            attr.ah_attr.dlid = lids[i];
            attr.ah_attr.sl = 0;
            attr.ah_attr.src_path_bits  = 0;
            attr.ah_attr.port_num = m_ibPort;
            if (m_gidIdx >= 0) 
            {
                attr.ah_attr.is_global = 1;
                attr.ah_attr.port_num = 1;
                memcpy(&attr.ah_attr.grh.dgid, &gids[i], 16);
                attr.ah_attr.grh.flow_label = 0;
                attr.ah_attr.grh.hop_limit = 1;
                attr.ah_attr.grh.sgid_index = m_gidIdx;
                attr.ah_attr.grh.traffic_class = 0;
            }
            flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
            
            if (ibv_modify_qp(m_stagedQps[i].get(), &attr, flags)) {
                LOG(1, "Cannot bring state of QP " << i << " to RTR" );
                throw Exception("Failed to bring QP's state to RTR" );
            }

            // Bring QP to RTS
            std::memset(&attr, 0, sizeof(attr));
            attr.qp_state      = IBV_QPS_RTS;
            attr.timeout       = 0x12;
            attr.retry_cnt     = 6;
            attr.rnr_retry     = 0;
            attr.sq_psn        = 0;
            attr.max_rd_atomic = 1;
            flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | 
                IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
            if( ibv_modify_qp(m_stagedQps[i].get(), &attr, flags) ) {
                LOG(1, "Cannot bring state of QP " << i << " to RTS" );
                throw Exception("Failed to bring QP's state to RTS" );
            }

            LOG(3, "Connected Queue pair for " << m_pid << " -> " << i );

        } // for each peer
    } 
    catch(...) {
        m_comm.allreduceOr( true );
        throw;
    }

    if (m_comm.allreduceOr( false )) 
        throw Exception("Another peer failed to set-up Infiniband queue pairs");

    LOG(3, "All staged queue pairs have been connected" );

    m_connectedQps.swap( m_stagedQps );
    for (int i = 0; i < m_nprocs; ++i) 
        m_stagedQps[i].reset();

    LOG(3, "All old queue pairs have been removed");

    m_comm.barrier();
}



void IBVerbs :: doLocalProgress(){
    int n = m_numMsgs;
    int error = 0;
    struct ibv_wc wcs[64];


    LOG(5, "Polling for " << n << " messages" );
    int pollResult = ibv_poll_cq(m_cqLocal.get(), std::min<size_t>(64,n), wcs);
    if (pollResult > 0) {
        LOG(4, "Received " << pollResult << " acknowledgements");
        atomic_fetch_sub(&m_numMsgs, pollResult);
	
        for (int i = 0; i < pollResult ; ++i) {
            if (wcs[i].status != IBV_WC_SUCCESS) 
            {
                 LOG( 2, "Got bad completion status from IB message."
                    " status = 0x" << std::hex << wcs[i].status
                    << ", vendor syndrome = 0x" << std::hex
                    << wcs[i].vendor_err );
                error = 1;
            }
            else {
                nanos6_decrease_task_event_counter((void *)wcs[i].wr_id, 1);
            }
        }
    }
    else if (pollResult < 0) 
    {
        LOG( 1, "Failed to poll IB completion queue" );
        throw Exception("Poll CQ failure");
    }
    if (error) {
        throw Exception("Error occurred during polling");
    }
}

void IBVerbs :: doRemoteProgress(){
    //int error = 0;
    struct ibv_wc wcs[64];


    struct ibv_recv_wr wr;
    struct ibv_sge sg;
    struct ibv_recv_wr *bad_wr;


    int pollResult = ibv_poll_cq(m_cqRemote.get(), std::min<size_t>(64,m_nprocs), wcs);
    for(int i = 0; i < pollResult; i++){
        m_recvCount++;
        sg.addr = (uint64_t) NULL;
        sg.length = 0;
        sg.lkey = 0;
        wr.next = NULL;
        wr.sg_list = &sg;
        wr.num_sge = 0;
    	wr.wr_id = wcs[i].wr_id;
        ibv_post_recv(m_connectedQps[wcs[i].wr_id].get(), &wr, &bad_wr);
    }
}

void IBVerbs :: doProgress(){
    
    while(!m_stopProgress){
	doLocalProgress();
	doRemoteProgress();

	nanos6_wait_for(500);
    }
}

void IBVerbs :: stopProgress(){
	m_stopProgress = 1;
}


void IBVerbs :: resizeMemreg( size_t size )
{
    m_memregSize = size;
    if ( size > size_t(std::numeric_limits<int>::max()) )
    {
        LOG(2, "Could not expand memory register, because integer will overflow");
        throw Exception("Could not increase memory register");
    }
    if ( int(size) > m_deviceAttr.max_mr ) {
        LOG(2, "IB device only supports " << m_deviceAttr.max_mr
                << " memory registrations, while " << size
                << " are being requested" );
        throw std::bad_alloc() ;
    }

    MemoryRegistration null = { 0, 0, 0, 0 };
    MemorySlot dflt; dflt.glob.resize( m_nprocs, null );

    m_memreg.reserve( size, dflt );


    //the size of the remote cq is dependant of the memory registers, so we need to resize also here
    if(m_cqSize*m_nprocs*size >= m_postCount){
        if (m_cqRemote) { 
	    ibv_resize_cq(m_cqRemote.get(), m_cqSize * m_nprocs * size);
        }   
    }
    else{
	//TODO: destroy & create
    }
}

void IBVerbs :: resizeMesgq( size_t size )
{
    m_cqSize = size;
    if (m_cqLocal) { 
        ibv_resize_cq(m_cqLocal.get(), size);
    }   

    if(size*m_nprocs*m_memregSize >= m_postCount){
        if (m_cqRemote) { 
	    ibv_resize_cq(m_cqRemote.get(), size * m_nprocs * m_memregSize);
        }   
    }
    else{
	//TODO: destroy & create
    }

    stageQPs(size);



    LOG(4, "Message queue has been reallocated to size " << size );

}

IBVerbs :: SlotID IBVerbs :: regLocal( void * addr, size_t size )
{
    ASSERT( size <= m_maxRegSize );

    MemorySlot slot;
    if ( size > 0) {
        LOG(4, "Registering locally memory area at " << addr << " of size  " << size );
        slot.mr.reset( ibv_reg_mr( m_pd.get(), addr, size, 
                    IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE |
                    IBV_ACCESS_REMOTE_WRITE )
                     , ibv_dereg_mr );
        if (!slot.mr) {
            LOG(1, "Could not register memory area at "
                   << addr << " of size " << size << " with IB device");
            throw Exception("Could not register memory area");
        }
    }
    MemoryRegistration local;
    local.addr = addr;
    local.size = size;
    local.lkey = size?slot.mr->lkey:0;
    local.rkey = size?slot.mr->rkey:0;

    SlotID id =  m_memreg.addLocalReg( slot );

    m_memreg.update( id ).glob.resize( m_nprocs );
    m_memreg.update( id ).glob[m_pid] = local;
    LOG(4, "Memory area " << addr << " of size " << size << " has been locally registered. Slot = " << id );
    return id;
}

IBVerbs :: SlotID IBVerbs :: regGlobal( void * addr, size_t size )
{
    ASSERT( size <= m_maxRegSize );

    MemorySlot slot;
    if ( size > 0 ) {
        LOG(4, "Registering globally memory area at " << addr << " of size  " << size );
        slot.mr.reset( ibv_reg_mr( m_pd.get(), addr, size, 
                    IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE |
                    IBV_ACCESS_REMOTE_WRITE )
                     , ibv_dereg_mr );

        if (!slot.mr) {
            LOG(1, "Could not register memory area at "
                   << addr << " of size " << size << " with IB device");
            m_comm.allreduceAnd(true);
            throw Exception("Could not register memory area");
        }
    }
    if (m_comm.allreduceOr(false))
        throw Exception("Another process could not register memory area");


    SlotID id = m_memreg.addGlobalReg( slot );
    MemorySlot & ref = m_memreg.update(id);
    // exchange memory registration info globally
    ref.glob.resize(m_nprocs);

    MemoryRegistration local;
    local.addr = addr;
    local.size = size;
    local.lkey = size?slot.mr->lkey:0;
    local.rkey = size?slot.mr->rkey:0;

    LOG(4, "All-gathering memory register data" );

    struct ibv_recv_wr wr;
    struct ibv_sge sg;
    struct ibv_recv_wr *bad_wr;
    sg.addr = (uint64_t) NULL;
    sg.length = 0;
    sg.lkey = 0;
    wr.next = NULL;
    wr.sg_list = &sg;
    wr.num_sge = 0;
    for(int i = 0; i < (int)m_cqSize; ++i){
        for(int j= 0; j < m_nprocs; j++){
    	    wr.wr_id = j;
            ibv_post_recv(m_connectedQps[j].get(), &wr, &bad_wr);
            m_postCount++;
       }
    }

    m_comm.allgather( local, ref.glob.data() );
    LOG(4, "Memory area " << addr << " of size " << size << " has been globally registered. Slot = " << id );

    return id;
}

void IBVerbs :: dereg( SlotID id )
{
    m_memreg.removeReg( id );
    LOG(4, "Memory area of slot " << id << " has been deregistered");
}
void IBVerbs :: put( SlotID srcSlot, size_t srcOffset, 
              int dstPid, SlotID dstSlot, size_t dstOffset, size_t size )
{
//TODO: original put
    taput(srcSlot, srcOffset, dstPid, dstSlot, dstOffset, size);
}
void IBVerbs :: taput( SlotID srcSlot, size_t srcOffset, 
              int dstPid, SlotID dstSlot, size_t dstOffset, size_t size )
{
    const MemorySlot & src = m_memreg.lookup( srcSlot );
    const MemorySlot & dst = m_memreg.lookup( dstSlot );

    ASSERT( src.mr );

    int numMsgs = size/m_maxMsgSize + (size % m_maxMsgSize > 0); //+1 if last msg size < m_maxMsgSize

    struct ibv_sge     sges[numMsgs];
    struct ibv_send_wr srs[numMsgs];
    struct ibv_sge     *sge;
    struct ibv_send_wr *sr;


    void *counter = nanos6_get_current_event_counter(); 
    nanos6_increase_current_task_event_counter(counter, 1);
    for(int i = 0; i< numMsgs; i++){
	sge = &sges[i]; std::memset(sge, 0, sizeof(ibv_sge));
	sr = &srs[i]; std::memset(sr, 0, sizeof(ibv_send_wr));

        const char * localAddr 
            = static_cast<const char *>(src.glob[m_pid].addr) + srcOffset;
        const char * remoteAddr 
            = static_cast<const char *>(dst.glob[dstPid].addr) + dstOffset;

        sge->addr = reinterpret_cast<uintptr_t>( localAddr );
        sge->length = std::min<size_t>(size, m_maxMsgSize );
        sge->lkey = src.mr->lkey;
	

        bool lastMsg = i == numMsgs-1;
        sr->next = lastMsg ? NULL : &srs[i+1]; 
        // since reliable connection guarantees keeps packets in order,
        // we only need a signal from the last message in the queue
        sr->send_flags = lastMsg ? IBV_SEND_SIGNALED : 0 ;

        sr->wr_id = (uint64_t)counter; 

        sr->sg_list = sge;
        sr->num_sge = 1;
        sr->opcode = lastMsg ? IBV_WR_RDMA_WRITE_WITH_IMM : IBV_WR_RDMA_WRITE;
	sr->imm_data = 0;
        sr->wr.rdma.remote_addr = reinterpret_cast<uintptr_t>( remoteAddr );
        sr->wr.rdma.rkey = dst.glob[dstPid].rkey;


        size -= sge->length;
        srcOffset += sge->length;
        dstOffset += sge->length;

    }

    //Send
    struct ibv_send_wr *bad_wr = NULL;
    if (int err = ibv_post_send(m_connectedQps[dstPid].get(), &srs[0], &bad_wr ))
    {
        LOG(1, "Error while posting RDMA requests: " << std::strerror(err) );
        throw Exception("Error while posting RDMA requests");
    }
    else{
         atomic_fetch_add(&m_numMsgs, 1);
         atomic_fetch_add(&m_numMsgsSync[dstPid], 1);
    }

}
void IBVerbs :: get( int srcPid, SlotID srcSlot, size_t srcOffset, 
              SlotID dstSlot, size_t dstOffset, size_t size )
{
//TODO: original get
    taget(srcPid, srcSlot, srcOffset, dstSlot, dstOffset, size);
}
void IBVerbs :: taget( int srcPid, SlotID srcSlot, size_t srcOffset, 
              SlotID dstSlot, size_t dstOffset, size_t size )
{
    const MemorySlot & src = m_memreg.lookup( srcSlot );
    const MemorySlot & dst = m_memreg.lookup( dstSlot );

    ASSERT( dst.mr );

    int numMsgs = size/m_maxMsgSize + (size % m_maxMsgSize > 0); //+1 if last msg size < m_maxMsgSize

    struct ibv_sge     sges[numMsgs+1];
    struct ibv_send_wr srs[numMsgs+1];
    struct ibv_sge     *sge;
    struct ibv_send_wr *sr;

    for(int i = 0; i< numMsgs; i++){
	sge = &sges[i]; std::memset(sge, 0, sizeof(ibv_sge));
	sr = &srs[i]; std::memset(sr, 0, sizeof(ibv_send_wr));

        const char * localAddr 
            = static_cast<const char *>(dst.glob[m_pid].addr) + dstOffset;
        const char * remoteAddr 
            = static_cast<const char *>(src.glob[srcPid].addr) + srcOffset;

        sge->addr = reinterpret_cast<uintptr_t>( localAddr );
        sge->length = std::min<size_t>(size, m_maxMsgSize );
        sge->lkey = dst.mr->lkey;

   //     bool lastMsg =  i == numMsgs-1;
        sr->next = &srs[i+1];
        // since reliable connection guarantees keeps packets in order,
        // we only need a signal from the last message in the queue
        sr->send_flags = 0;

        sr->wr_id = m_pid; 

        sr->sg_list = sge;
        sr->num_sge = 1;
        sr->opcode = IBV_WR_RDMA_READ;
        sr->wr.rdma.remote_addr = reinterpret_cast<uintptr_t>( remoteAddr );
        sr->wr.rdma.rkey = src.glob[srcPid].rkey;

        size -= sge->length;
        srcOffset += sge->length;
        dstOffset += sge->length;
    }

    // add extra "message" to do the local and remote completion
    sge = &sges[numMsgs]; std::memset(sge, 0, sizeof(ibv_sge));
    sr = &srs[numMsgs]; std::memset(sr, 0, sizeof(ibv_send_wr));
    
    const char * localAddr = static_cast<const char *>(dst.glob[m_pid].addr);
    const char * remoteAddr = static_cast<const char *>(src.glob[srcPid].addr);

    void *counter = nanos6_get_current_event_counter(); 
    nanos6_increase_current_task_event_counter(counter, 1);

    sge->addr = reinterpret_cast<uintptr_t>( localAddr );
    sge->length = 0;
    sge->lkey = dst.mr->lkey;

    sr->wr_id = (uint64_t)(counter); 
    sr->next = NULL;
    sr->send_flags = IBV_SEND_SIGNALED;
    sr->sg_list = sge;
    sr->num_sge = 1;
    sr->opcode = IBV_WR_RDMA_WRITE_WITH_IMM; // There is no READ_WITH_IMM 
    sr->imm_data = 0;
    sr->wr.rdma.remote_addr = reinterpret_cast<uintptr_t>( remoteAddr );
    sr->wr.rdma.rkey = src.glob[srcPid].rkey;

    //Send
    struct ibv_send_wr *bad_wr = NULL;
    if (int err = ibv_post_send(m_connectedQps[srcPid].get(), &srs[0], &bad_wr ))
    {
        LOG(1, "Error while posting RDMA requests: " << std::strerror(err) );
        throw Exception("Error while posting RDMA requests");
    }
    else{
         atomic_fetch_add(&m_numMsgs, 1);
         atomic_fetch_add(&m_numMsgsSync[srcPid], 1);
    }

}


void IBVerbs :: sync( bool reconnect)
{
//TODO: original sync
    tasync(reconnect, 0);
}

#define LPF_SYNC_MODE	 	(0x6 << 27)
void IBVerbs :: tasync( bool reconnect, int attr )
{
    if (reconnect) reconnectQPs();

    int sync_mode = attr & LPF_SYNC_MODE;
    int sync_value = attr & ~LPF_SYNC_MODE;
    int sync_barrier = (attr & LPF_SYNC_BARRIER) | (sync_mode == LPF_SYNC_DEFAULT);

    int remoteMsgs = 0;
    switch (sync_mode){
        case LPF_SYNC_DEFAULT:
            fprintf(stderr, "sync: LPF_SYNC_DEFAULT\n");
            //get num remote completions
            for(int i = 0; i < m_nprocs; i++){
	        if(i == m_pid) remoteMsgs = m_comm.allreduceSum(m_numMsgsSync[i]);
	        else m_comm.allreduceSum(m_numMsgsSync[i]);
            }
            break;
        case LPF_SYNC_CACHED:
            fprintf(stderr, "sync: LPF_SYNC_CACHED\n");
            //TODO
            break;
        case LPF_SYNC_MSG(0):
            fprintf(stderr, "sync: LPF_SYNC_MSG(%d)\n", sync_value);
            remoteMsgs = sync_value;
            break;
        default:
            fprintf(stderr, "sync: UNKNOWN\n");
    }

    // wait for remote completions
    while(m_recvCount < remoteMsgs);
    if (m_recvCount > remoteMsgs) //Print warning
        fprintf(stderr, "There are more remote completions than the exxpected\n"); //TODO: log

    m_recvCount -= remoteMsgs;

    if(m_numMsgs > 0) {
        fprintf(stderr, "There are some RMA operations that still didn't finished, \
            there may be a problem with the dependencies\n"); //TODO: log
        //assert(m_numMsgs > 0);
        // wait for local completion
        while (m_numMsgs > 0);
    }
    // synchronize
    for(int i = 0; i < m_nprocs; i++)
        m_numMsgsSync[i] = 0;

    if(sync_barrier) {
        fprintf(stderr, "sync: LPF_SYNC_BARRIER\n");
        m_comm.barrier();
    }
}


} }
