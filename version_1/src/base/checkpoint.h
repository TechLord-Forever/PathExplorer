#ifndef CHECKPOINT_H
#define CHECKPOINT_H

#include <pin.H>

#include <map>
#include <set>

#include <boost/shared_ptr.hpp>


/*================================================================================================*/
                
class checkpoint
{
public:
  ADDRINT                     addr;
  boost::shared_ptr<CONTEXT>  ptr_ctxt;
  
  std::map<ADDRINT, UINT8>    mem_read_log;     // map between a read address and the original value at this address
  std::map<ADDRINT, UINT8>    mem_written_log;  // map between a written address and the original value at this address

  boost::shared_ptr<UINT8>    curr_input;
  
  std::set<ADDRINT>           dep_mems;
  
  UINT32                      execution_order;
  
  UINT32                      rollback_times;
    
public:
  checkpoint(ADDRINT ip_addr, CONTEXT* new_ptr_ctxt, ADDRINT msg_read_addr, UINT32 msg_read_size);
    
  void mem_written_logging(ADDRINT mem_addr, UINT32 mem_length);
//   void mem_read_logging(ADDRINT ins_addr, ADDRINT mem_addr, UINT32 mem_length);
  
};

typedef boost::shared_ptr<checkpoint> ptr_checkpoint_t;

/*================================================================================================*/

class ptr_checkpoint_less 
{
public:
  bool operator()(ptr_checkpoint_t const& a, ptr_checkpoint_t const& b)
  {
    return (a->execution_order < b->execution_order);
  }
};

/*================================================================================================*/

void rollback_and_restore(ptr_checkpoint_t& ptr_chkpnt, UINT8* backup_input_addr);

void rollback_and_restore(ptr_checkpoint_t& ptr_chkpnt, std::set<ADDRINT>& mem_addrs);

void rollback_and_modify(ptr_checkpoint_t& ptr_chkpnt, std::set<ADDRINT>& dep_mems);

#endif // CHECKPOINT_H

