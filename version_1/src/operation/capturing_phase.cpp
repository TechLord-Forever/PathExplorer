#include "capturing_phase.h"
#include "common.h"
#include "../util/stuffs.h"

#include <algorithm>

namespace capturing 
{

/*================================================================================================*/

static UINT32   received_msg_number;
static bool     recv_is_locked;
static bool     recvfrom_is_locked;
static bool     wsarecv_is_locked;
static bool     wsarecvfrom_is_locked;
//static bool     interested_msg_is_received;

#if defined(_WIN32) || defined(_WIN64)
//namespace windows
//{
//#define NOMINMAX
//#include <WinSock2.h>
//#include <Windows.h>
//};
static ADDRINT  received_msg_struct_addr;
#endif


/*================================================================================================*/
/**
 * @brief initialize a new capturing phase
 */
auto initialize() -> void
{
#if defined(_WIN32) || defined(_WIN64)
  recv_is_locked = false; recvfrom_is_locked = false; wsarecv_is_locked = false;
  wsarecvfrom_is_locked = false;
#endif
  received_msg_number = 0; interested_msg_is_received = false;
  return;
}


/**
 * @brief prepare switching to a new tainting phase
 */
static inline auto prepare_new_tainting_phase() -> void
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


/**
 * @brief handle_received_message
 */
static inline auto handle_received_message() -> void
{
  if ((received_msg_size > 0) && !recv_is_locked && !wsarecv_is_locked)
  {
    received_msg_number++;
    // verify if the received message is the interesting message
    if (received_msg_number == received_msg_order)
    {
      // yes, then remove the current instrumentation for message receiving functions
      interested_msg_is_received = true; PIN_RemoveInstrumentation();
    }
  }
  return;
}


#if defined(_WIN32) || defined(_WIN64)
/**
 * @brief determine the received message address of recv or recvfrom
 */
auto recvs_interceptor_before(ADDRINT msg_addr, THREADID thread_id) -> VOID
{
  if (!traced_thread_is_fixed || (traced_thread_is_fixed && (traced_thread_id == thread_id)))
  {
    traced_thread_id = thread_id; traced_thread_is_fixed = true; received_msg_addr = msg_addr;
    recv_is_locked = true;
  }
  return;
}


/**
 * @brief determine the received message length of recv or recvfrom
 */
auto recvs_interceptor_after(UINT32 msg_length, THREADID thread_id) -> VOID
{
  if (traced_thread_is_fixed && (thread_id == traced_thread_id) && recv_is_locked)
  {
#if !defined(NDEBUG)
      tfm::format(log_file, "message is obtained from recv or recvfrom at thread id %d\n",
                  thread_id);
#endif
    received_msg_size = msg_length; recv_is_locked = false; handle_received_message();
  }
  return;
}


/**
 * @brief determine the address a type LPWSABUF containing the address of the received message
 */
auto wsarecvs_interceptor_before(ADDRINT msg_struct_addr, THREADID thread_id) -> VOID
{
  if (!traced_thread_is_fixed || (traced_thread_is_fixed && (traced_thread_id == thread_id)))
  {
    received_msg_struct_addr = msg_struct_addr;
    received_msg_addr = reinterpret_cast<ADDRINT>((reinterpret_cast<windows::LPWSABUF>(
                                                     received_msg_struct_addr))->buf);
    traced_thread_id = thread_id; traced_thread_is_fixed = true; wsarecv_is_locked = true;
  }
  return;
}


/**
 * @brief determine the received message length or WSARecv or WSARecvFrom
 */
auto wsarecvs_interceptor_after(THREADID thread_id) -> VOID
{
  if (traced_thread_is_fixed && (thread_id == traced_thread_id) && wsarecv_is_locked)
  {
#if !defined(NDEBUG)
    tfm::format(log_file, "message is obtained from WSARecv or WSARecvFrom at thread id %d\n",
                thread_id);
#endif
    received_msg_size = (reinterpret_cast<windows::LPWSABUF>(received_msg_struct_addr))->len;
    wsarecv_is_locked = false; handle_received_message();
  }
  return;
}



/**
 * @brief recv wrapper
 */
auto recv_wrapper(AFUNPTR recv_origin,
                  windows::SOCKET s,
                  char* buf,
                  int len,
                  int flags,
                  CONTEXT* p_ctxt, THREADID thread_id) -> int
{
  int result;

  if (!traced_thread_is_fixed || (traced_thread_is_fixed && (traced_thread_id == thread_id)))
  {
    received_msg_addr = reinterpret_cast<ADDRINT>(buf);
    traced_thread_id = thread_id; traced_thread_is_fixed = true; recv_is_locked = true;
  }

  std::cerr << "recv wrapper\n";
  PIN_CallApplicationFunction(p_ctxt, thread_id, CALLINGSTD_DEFAULT, recv_origin,
                              PIN_PARG(int), &result,
                              PIN_PARG(windows::SOCKET), s,
                              PIN_PARG(char*), buf,
                              PIN_PARG(int), len,
                              PIN_PARG(int), flags,
                              PIN_PARG_END());

  if (traced_thread_is_fixed && (thread_id == traced_thread_id) && recv_is_locked)
  {
#if !defined(NDEBUG)
    tfm::format(log_file, "message is obtained from recv at thread id %d\n", thread_id);
#endif
    received_msg_size = result; recv_is_locked = false; handle_received_message();
  }

  return result;
}


/**
 * @brief recvfrom wrapper
 */
auto recvfrom_wrapper(AFUNPTR recvfrom_origin,
                      windows::SOCKET s,
                      char* buf,
                      int len,
                      int flags,
                      windows::sockaddr* from,
                      int* fromlen,
                      CONTEXT* p_ctxt, THREADID thread_id) -> int
{
  int result;

  std::cerr << "recvfrom wrapper\n";
  if (!traced_thread_is_fixed || (traced_thread_is_fixed && (traced_thread_id == thread_id)))
  {
    received_msg_addr = reinterpret_cast<ADDRINT>(buf);
    traced_thread_id = thread_id; traced_thread_is_fixed = true; recv_is_locked = true;
  }

  PIN_CallApplicationFunction(p_ctxt, thread_id, CALLINGSTD_DEFAULT, recvfrom_origin,
                              PIN_PARG(int), &result,
                              PIN_PARG(char*), buf,
                              PIN_PARG(int), len,
                              PIN_PARG(int), flags,
                              PIN_PARG(windows::sockaddr*), from,
                              PIN_PARG(int*), fromlen,
                              PIN_PARG_END());

  if (traced_thread_is_fixed && (thread_id == traced_thread_id) && recv_is_locked)
  {
#if !defined(NDEBUG)
    tfm::format(log_file, "message is obtained from recvfrom at thread id %d\n", thread_id);
#endif
    received_msg_size = result; recv_is_locked = false; handle_received_message();
  }

  return result;
}

/**
 * @brief WSARecv wrapper
 */
auto wsarecv_wrapper(AFUNPTR wsarecv_origin,
                     windows::SOCKET s,
                     windows::LPWSABUF lpBuffers,
                     windows::DWORD dwBufferCount,
                     windows::LPDWORD lpNumberOfBytesRecvd,
                     windows::LPDWORD lpFlags,
                     windows::LPWSAOVERLAPPED lpOverlapped,
                     windows::LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
                     CONTEXT* p_ctxt, THREADID thread_id) -> int
{
  int result;

  std::cerr << "wsarecv wrapper\n";
  if (!traced_thread_is_fixed || (traced_thread_is_fixed && (traced_thread_id == thread_id)))
  {
    received_msg_addr = reinterpret_cast<ADDRINT>(lpBuffers->buf);
    traced_thread_id = thread_id; traced_thread_is_fixed = true; wsarecv_is_locked = true;
  }

  PIN_CallApplicationFunction(p_ctxt, thread_id, CALLINGSTD_DEFAULT, wsarecv_origin,
                              PIN_PARG(int), &result,
                              PIN_PARG(windows::SOCKET), s,
                              PIN_PARG(windows::LPWSABUF), lpBuffers,
                              PIN_PARG(windows::DWORD), dwBufferCount,
                              PIN_PARG(windows::LPDWORD), lpNumberOfBytesRecvd,
                              PIN_PARG(windows::LPDWORD), lpFlags,
                              PIN_PARG(windows::LPWSAOVERLAPPED), lpOverlapped,
                              PIN_PARG(windows::LPWSAOVERLAPPED_COMPLETION_ROUTINE), lpCompletionRoutine,
                              PIN_PARG_END());

  if (traced_thread_is_fixed && (thread_id == traced_thread_id) && wsarecv_is_locked)
  {
#if !defined(NDEBUG)
    tfm::format(log_file, "message is obtained from WSARecv at thread id %d\n", thread_id);
#endif
    received_msg_size = *lpNumberOfBytesRecvd; wsarecv_is_locked = false; handle_received_message();
  }

  return result;
}


/**
 * @brief WSARecvFrom wrapper
 */
auto wsarecvfrom_wrapper(AFUNPTR wsarecvfrom_origin, windows::SOCKET s, windows::LPWSABUF lpBuffers,
                         windows::DWORD dwBufferCount, windows::LPDWORD lpNumberOfBytesRecvd,
                         windows::LPDWORD lpFlags, windows::sockaddr* lpFrom,
                         windows::LPINT lpFromlen, windows::LPWSAOVERLAPPED lpOverlapped,
                         windows::LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
                         CONTEXT* p_ctxt, THREADID thread_id) -> int
{
  int result;

  std::cerr << "wsarecvfrom wrapper\n";
  if (!traced_thread_is_fixed || (traced_thread_is_fixed && (traced_thread_id == thread_id)))
  {
    received_msg_addr = reinterpret_cast<ADDRINT>(lpBuffers->buf);
    traced_thread_id = thread_id; traced_thread_is_fixed = true; wsarecv_is_locked = true;
  }

  PIN_CallApplicationFunction(p_ctxt, thread_id, CALLINGSTD_DEFAULT, wsarecvfrom_origin,
                              PIN_PARG(int), &result,
                              PIN_PARG(windows::SOCKET), s,
                              PIN_PARG(windows::LPWSABUF), lpBuffers,
                              PIN_PARG(windows::DWORD), dwBufferCount,
                              PIN_PARG(windows::LPDWORD), lpNumberOfBytesRecvd,
                              PIN_PARG(windows::LPDWORD), lpFlags,
                              PIN_PARG(windows::sockaddr*), lpFrom,
                              PIN_PARG(windows::LPINT), lpFromlen,
                              PIN_PARG(windows::LPWSAOVERLAPPED), lpOverlapped,
                              PIN_PARG(windows::LPWSAOVERLAPPED_COMPLETION_ROUTINE), lpCompletionRoutine,
                              PIN_PARG_END());

  if (traced_thread_is_fixed && (thread_id == traced_thread_id) && wsarecv_is_locked)
  {
#if !defined(NDEBUG)
    tfm::format(log_file, "message is obtained from WSARecvFrom at thread id %d\n", thread_id);
#endif
    received_msg_size = *lpNumberOfBytesRecvd; wsarecv_is_locked = false; handle_received_message();
  }

  return result;
}

#elif defined(__gnu_linux__)

VOID syscall_entry_analyzer(THREADID thread_id, CONTEXT* p_ctxt, SYSCALL_STANDARD syscall_std,
                            VOID *data)
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
VOID syscall_exit_analyzer(THREADID thread_id, CONTEXT* p_ctxt, SYSCALL_STANDARD syscall_std,
                           VOID *data)
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
        tfm::format(log_file, "the first message saved at %s with size %d bytes\nstart tainting \
                    the first time with trace size %d\n", addrint_to_hexstring(received_msg_addr),
                    received_msg_size, max_trace_size);
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

#endif // defined(__gnu_linux__)


/**
 * @brief handle memory read instructions in the capturing phase
 */
auto mem_read_instruction (ADDRINT ins_addr, ADDRINT r_mem_addr, UINT32 r_mem_size, CONTEXT* p_ctxt,
                           THREADID thread_id) -> VOID
{
  if (thread_id == traced_thread_id)
  {
    // verify if the instruction read some addresses in the input buffer
    if (std::max(r_mem_addr, received_msg_addr) <
        std::min(r_mem_addr + r_mem_size, received_msg_addr + received_msg_size))
    {
      // yes, then start the tainting phase
      prepare_new_tainting_phase(); PIN_ExecuteAt(p_ctxt);
    }
  }
  return;
}

} // end of capturing namespace
