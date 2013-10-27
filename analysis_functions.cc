#include "analysis_functions.h"

#include <iostream>
#include <map>

#include "instruction.h"
#include "stuffs.h"

extern ADDRINT logged_syscall_index;
extern ADDRINT logged_syscall_args[6];

extern std::map< ADDRINT, 
                 instruction > addr_ins_static_map;

extern UINT8    received_msg_num;
extern ADDRINT  received_msg_addr;
extern UINT32   received_msg_size;

extern KNOB<UINT32> max_trace_length;
extern KNOB<BOOL>   print_debug_text;

/*====================================================================================================================*/

VOID unhandled_analyzer(ADDRINT ins_addr, UINT32 dst_opr_id, UINT32 src_opr_id)
{
  std::cerr << addr_ins_static_map[ins_addr].disass;
  
  std::cerr << "\t\e[1;31m(Unhandled instruction).";
  std::cerr << "\n\e[0m";
  return;
}

/*====================================================================================================================*/

VOID syscall_entry_analyzer(THREADID thread_id, CONTEXT* p_ctxt, SYSCALL_STANDARD syscall_std, VOID *data)
{
  if (received_msg_num == 0)
  {
    logged_syscall_index = PIN_GetSyscallNumber(p_ctxt, syscall_std);
    if (logged_syscall_index == syscall_recvfrom)
    {
      for (UINT8 arg_id = 0; arg_id < 6; ++arg_id)
      {
        logged_syscall_args[arg_id] = PIN_GetSyscallArgument(p_ctxt, syscall_std, arg_id);
      }
    }
  }
  
  return;
}

/*====================================================================================================================*/

VOID syscall_exit_analyzer(THREADID thread_id, CONTEXT* p_ctxt, SYSCALL_STANDARD syscall_std, VOID *data)
{
  if (received_msg_num == 0)
  {
    if (logged_syscall_index == syscall_recvfrom)
    {
      ADDRINT ret_val = PIN_GetSyscallReturn(p_ctxt, syscall_std);
      if (ret_val > 0)
      {
        received_msg_num++;
        received_msg_addr = logged_syscall_args[1];
        received_msg_size = ret_val;
        
        print_debug_message_received();
                  
        PIN_RemoveInstrumentation();
      }
    }
  }
  
  return;
}
