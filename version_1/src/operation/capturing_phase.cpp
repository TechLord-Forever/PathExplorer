#include "capturing_phase.h"
#include "common.h"
#include "../util/stuffs.h"

namespace capturing 
{

/*================================================================================================*/

static UINT32   received_msg_number;
static bool     function_called;
#if defined(_WIN32) || defined(_WIN64)
namespace windows
{
#include <WinSock2.h>
#include <Windows.h>
};
static ADDRINT  received_msg_struct_addr;
#endif

/*================================================================================================*/
/**
 * @brief initialize a new capturing phase
 */
void initialize()
{
  function_called = false; received_msg_number = 0;
  return;
}


/**
 * @brief prepare switching to a new tainting phase
 */
static inline void prepare_new_tainting_phase()
{
  // save a fresh copy of the input
  fresh_input.reset(new UINT8[received_msg_size]);
  std::copy(reinterpret_cast<UINT8*>(received_msg_addr),
            reinterpret_cast<UINT8*>(received_msg_addr) + received_msg_size, fresh_input.get());

  // switch to the tainting state
  current_running_phase = tainting_phase; PIN_RemoveInstrumentation();
#if !defined(NDEBUG)
  tfm::format(log_file, "%s\nthe message of order %d saved at %s with size %d bytes\n",
              "================================================================================",
              received_msg_number, addrint_to_hexstring(received_msg_addr), received_msg_size);
#endif
  return;
}


static inline void handle_received_message()
{
  if (received_msg_size > 0)
  {
    received_msg_number++;
    // verify if the received message is the interesting message
    if (received_msg_number == received_msg_order)
    {
      prepare_new_tainting_phase();
    }
  }
  return;
}


#if defined(_WIN32) || defined(_WIN64)
/**
 * @brief determine the received message address of recv or recvfrom
 */
VOID before_recvs(ADDRINT msg_addr, THREADID thread_id)
{
  received_msg_addr = msg_addr; function_called = true;
  if (!traced_thread_is_fixed)
  {
    traced_thread_id = thread_id; traced_thread_is_fixed = true;
  }
  return;
}


/**
 * @brief determine the received message length of recv or recvfrom
 */
VOID after_recvs(UINT32 msg_length, THREADID thread_id)
{
  if (function_called && traced_thread_is_fixed && (traced_thread_id == thread_id))
  {
#if !defined(NDEBUG)
    tfm::format(log_file, "message is received from recv or recvfrom at thread id %d\n", thread_id);
#endif
    function_called = false; received_msg_size = msg_length;
    handle_received_message();
  }
  else
  {
#if !defined(NDEBUG)
    tfm::format(log_file, "fatal: message receiving function returns without being called or the thread id is changed from %d to %d\n",
                traced_thread_id, thread_id);
#endif
    PIN_ExitApplication(1);
  }
  return;
}


/**
 * @brief determine the address a type LPWSABUF containing the address of the received message
 */
VOID before_wsarecvs(ADDRINT msg_struct_addr, THREADID thread_id)
{
  received_msg_struct_addr = msg_struct_addr; function_called = true;
  received_msg_addr = reinterpret_cast<ADDRINT>((reinterpret_cast<windows::LPWSABUF>(
                                                   received_msg_struct_addr))->buf);
  if (!traced_thread_is_fixed)
  {
    traced_thread_id = thread_id; traced_thread_is_fixed = true;
  }
  return;
}


/**
 * @brief determine the received message length or WSARecv or WSARecvFrom
 */
VOID after_wsarecvs(THREADID thread_id)
{
  if (function_called && traced_thread_is_fixed && (traced_thread_id == thread_id))
  {
#if !defined(NDEBUG)
    tfm::format(log_file, "message is received from WSARecv or WSARecvFrom at thread id %d\n", thread_id);
#endif
    function_called = false;
    received_msg_size = (reinterpret_cast<windows::LPWSABUF>(received_msg_struct_addr))->len;
    handle_received_message();
  }
  else
  {
#if !defined(NDEBUG)
    tfm::format(log_file, "fatal: message receiving function returns without being called or the thread id is changed from %d to %d\n",
                traced_thread_id, thread_id);
#endif
    PIN_ExitApplication(1);
  }
  return;
}
#elif defined(__gnu_linux__)
VOID syscall_entry_analyzer(THREADID thread_id,
                            CONTEXT* p_ctxt, SYSCALL_STANDARD syscall_std, VOID *data)
{
  if (current_running_phase == capturing_phase)
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


/**
 * @brief syscall_exit_analyzer
 */
VOID syscall_exit_analyzer(THREADID thread_id,
                           CONTEXT* p_ctxt, SYSCALL_STANDARD syscall_std, VOID *data)
{
  if (current_running_phase == capturing_phase)
  {
    if (logged_syscall_index == syscall_recvfrom)
    {
      ADDRINT returned_value = PIN_GetSyscallReturn(p_ctxt, syscall_std);
      if (returned_value > 0)
      {
        received_msg_num++;
        received_msg_addr = logged_syscall_args[1]; received_msg_size = returned_value;
#if !defined(NDEBUG)
        tfm::format(log_file,
                    "the first message saved at %s with size %d bytes\nstart tainting the first time with trace size %d\n",
                    addrint_to_hexstring(received_msg_addr), received_msg_size, max_trace_size);
//        log_file << boost::format("the first message saved at %s with size %d bytes\nstart tainting the first time with trace size %d\n")
//                    % addrint_to_hexstring(received_msg_addr) % received_msg_size % max_trace_size;
#endif
        // the first received message is the considered input
        if (received_msg_num == 1)
        {
          // switch to the tainting state
          current_running_phase = tainting_state; PIN_RemoveInstrumentation();
        }
      }
    }
  }
  return;
}
#endif
} // end of capturing namespace
