#ifndef CHECKPOINT_H
#define CHECKPOINT_H

#include <pin.H>

#include <map>
#include <set>
//#include <vector>

#include <boost/shared_ptr.hpp>


class checkpoint
{
public:
  boost::shared_ptr<CONTEXT>  context;
  std::map<ADDRINT, UINT8>    mem_written_log; // maps between written memory addresses and original values
//  boost::shared_ptr<UINT8>    curr_input;
  
  std::set<ADDRINT>           input_dep_addrs;
  UINT32                      exec_order;
    
public:
  checkpoint(CONTEXT* ptr_context, ADDRINT input_mem_read_addr, UINT32 input_mem_read_size);
  void mem_write_tracking(ADDRINT mem_addr, UINT32 mem_length);
  void rollback();
  void rollback_with_new_input(ADDRINT input_addr, UINT8* new_input_buffer, UINT32 input_size);
};

typedef boost::shared_ptr<checkpoint> ptr_checkpoint_t;
typedef std::vector<ptr_checkpoint_t> ptr_checkpoints_t;

/*================================================================================================*/

//class ptr_checkpoint_less
//{
//public:
//  bool operator()(ptr_checkpoint_t const& a, ptr_checkpoint_t const& b)
//  {
//    return (a->exec_order < b->exec_order);
//  }
//};

/*================================================================================================*/

void rollback_and_restore(ptr_checkpoint_t& ptr_chkpnt, UINT8* backup_input_addr);

void rollback_and_restore(ptr_checkpoint_t& ptr_chkpnt, std::set<ADDRINT>& mem_addrs);

void rollback_and_modify(ptr_checkpoint_t& ptr_chkpnt, std::set<ADDRINT>& dep_mems);

#endif // CHECKPOINT_H

