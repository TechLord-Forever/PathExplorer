#ifndef CHECKPOINT_H
#define CHECKPOINT_H

#include "../parsing_helper.h"
#include <pin.H>

#include <map>
#include <set>
#include <vector>
#include <memory>

typedef std::map<ADDRINT, UINT8> addrint_value_map_t;

class checkpoint
{
public:
  std::shared_ptr<CONTEXT>     context;

  // maps between written memory addresses and original values
  addrint_value_map_t         mem_written_log;
  
  addrint_value_map_t         input_dep_original_values;
  UINT32                      exec_order;
    
public:
  checkpoint(UINT32 existing_exec_order, const CONTEXT* ptr_context,
             ADDRINT input_mem_read_addr, UINT32 input_mem_read_size);

  void mem_write_tracking(ADDRINT mem_addr, UINT32 mem_length);
};

typedef std::shared_ptr<checkpoint> ptr_checkpoint_t;
typedef std::vector<ptr_checkpoint_t> ptr_checkpoints_t;

extern auto rollback_with_current_input   (const ptr_checkpoint_t& destination,
                                           UINT32& existing_exec_order)                 -> void;

extern auto rollback_with_original_input  (const ptr_checkpoint_t& destination,
                                           UINT32& existing_exec_order)                 -> void;

extern auto rollback_with_new_input       (const ptr_checkpoint_t& destination,
                                           UINT32& existing_exec_order, ADDRINT input_buffer_addr,
                                           UINT32 input_buffer_size, UINT8* new_buffer) -> void;

extern auto rollback_with_modified_input  (const ptr_checkpoint_t& destination,
                                           UINT32& existing_exec_order,
                                           addrint_value_map_t& modified_addrs_values)  -> void;

#endif // CHECKPOINT_H

