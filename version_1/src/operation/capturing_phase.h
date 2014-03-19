#ifndef CAPTURING_PHASE_H
#define CAPTURING_PHASE_H

// these definitions are not necessary (defined already in the CMakeLists),
// they are added just to help qt-creator parsing headers
#if defined(_WIN32) || defined(_WIN64)
#ifndef TARGET_IA32
#define TARGET_IA32
#endif
#ifndef HOST_IA32
#define HOST_IA32
#endif
#ifndef TARGET_WINDOWS
#define TARGET_WINDOWS
#endif
#ifndef USING_XED
#define USING_XED
#endif
#endif
#include <pin.H>

//#include <functional>
//#include <boost/type_traits/function_traits.hpp>


#if defined(_WIN32) || defined(_WIN64)
namespace windows
{
#define NOMINMAX
#include <WinSock2.h>
#include <Windows.h>

// the follows should be declared more elegantly by: typedef decltype(function_name) function_name_t
typedef int recv_t        (SOCKET, char*, int, int);
typedef int recvfrom_t    (SOCKET, char*, int, int, struct sockaddr*, int*);
typedef int wsarecv_t     (SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD, LPWSAOVERLAPPED,
                           LPWSAOVERLAPPED_COMPLETION_ROUTINE);
typedef int wsarecvfrom_t (SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD, struct sockaddr*, LPINT,
                           LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
}
#endif

template <typename F> struct wrapper;

template <typename R, typename T1, typename T2, typename T3, typename T4>
struct wrapper<R(T1, T2, T3, T4)>
{
  typedef R result_type;
  typedef R (type)(AFUNPTR, T1, T2, T3, T4, CONTEXT*, THREADID);
};

//typedef decltype(windows::recv) recv_tt;
//typedef int F(windows::SOCKET, char *,int,int);
typedef wrapper<decltype(windows::recv)>::type zezer;
//typedef wrapper<int (windows::SOCKET,char *,int,int)>::type sdfsdf;


template <typename R, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
struct wrapper<R(T1, T2, T3, T4, T5, T6)>
{
  typedef R result_type;
  typedef R (type)(AFUNPTR, T1, T2, T3, T4, T5, T6, CONTEXT*, THREADID);
};

template <typename R, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6,
          typename T7>
struct wrapper<R(T1, T2, T3, T4, T5, T6, T7)>
{
  typedef R result_type;
  typedef R (type)(AFUNPTR, T1, T2, T3, T4, T5, T6, T7, CONTEXT*, THREADID);
};

template <typename R, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6,
          typename T7, typename T8, typename T9>
struct wrapper<R(T1, T2, T3, T4, T5, T6, T7, T8, T9)>
{
  typedef R result_type;
  typedef R (type)(AFUNPTR, T1, T2, T3, T4, T5, T6, T7, T8, T9, CONTEXT*, THREADID);
};


namespace capturing
{
extern auto initialize                  ()                                                -> void;

#if defined(_WIN32) || defined(_WIN64)

extern auto recvs_interceptor_before    (ADDRINT msg_addr, THREADID thread_id)            -> VOID;

extern auto recvs_interceptor_after     (UINT32 msg_length, THREADID thread_id)           -> VOID;

extern auto wsarecvs_interceptor_before (ADDRINT msg_struct_addr, THREADID thread_id)     -> VOID;

extern auto wsarecvs_interceptor_after  (THREADID thread_id)                              -> VOID;


extern wrapper<windows::recv_t>::type recv_wrapper;
typedef boost::function_traits<windows::recv_t> recv_traits_t;

extern wrapper<windows::recvfrom_t>::type recvfrom_wrapper;
typedef boost::function_traits<windows::recvfrom_t> recvfrom_traits_t;

extern wrapper<windows::wsarecv_t>::type wsarecv_wrapper;
typedef boost::function_traits<windows::wsarecv_t> wsarecv_traits_t;

extern wrapper<windows::wsarecvfrom_t>::type wsarecvfrom_wrapper;
typedef boost::function_traits<windows::wsarecvfrom_t> wsarecvfrom_traits_t;

#elif defined(__gnu_linux__)

extern VOID syscall_entry_analyzer(THREADID thread_id, CONTEXT* p_ctxt,
                                   SYSCALL_STANDARD syscall_std, VOID *data);

extern VOID syscall_exit_analyzer(THREADID thread_id, CONTEXT* p_ctxt,
                                  SYSCALL_STANDARD syscall_std, VOID *data);

#endif

extern auto mem_read_instruction  (ADDRINT ins_addr, ADDRINT r_mem_addr, UINT32 r_mem_size,
                                   CONTEXT* p_ctxt, THREADID thread_id)         -> VOID;
} // end of capturing namespace

#endif // CAPTURING_PHASE_H
