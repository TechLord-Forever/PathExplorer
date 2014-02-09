#include <pin.H>

#include <fstream>
#include <vector>
#include <map>
#include <set>

#include <boost/timer.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/format.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>

#include "stuffs.h"
#include "branch.h"
#include "instruction.h"
#include "analysis_functions.h"
#include "tainting_functions.h"
#include "resolving_functions.h"
#include "logging_functions.h"

/*================================================================================================*/

extern std::map<ADDRINT, ptr_instruction_t>     addr_ins_static_map;
extern bool                                     in_tainting;
extern map_ins_io                               dta_inss_io;
extern UINT32                                   received_msg_num;

namespace btime = boost::posix_time;
extern boost::shared_ptr<btime::ptime>          start_ptr_time;
extern boost::shared_ptr<btime::ptime>          stop_ptr_time;

namespace logging = boost::log;
namespace sinks   = boost::log::sinks;
namespace sources = boost::log::sources;
typedef sinks::text_file_backend                text_backend;
typedef sinks::synchronous_sink<text_backend>   sink_file_backend;
typedef logging::trivial::severity_level        log_level;

extern sources::severity_logger<log_level>      log_instance;
extern boost::shared_ptr<sink_file_backend>     log_sink;

/*================================================================================================*/

/**
 * @brief instrumentation function.
 * 
 * @param ins current examined instruction.
 * @param data not used.
 * @return VOID
 */
VOID ins_instrumenter(INS ins, VOID *data)
{
  // logging the parsed instructions statically
  ADDRINT ins_addr = INS_Address(ins);
  ptr_instruction_t new_instruction(new instruction(ins));
  addr_ins_static_map[ins_addr] = new_instruction;

  /*BOOST_LOG_SEV(log_instance, boost::log::trivial::info) << boost::format("%-15s %-35s %s")
    % remove_leading_zeros(StringFromAddrint(ins_addr)) 
    % addr_ins_static_map[ins_addr].disass 
    % addr_ins_static_map[ins_addr].contained_function;*/
  //log_sink->flush();

  /*INS_InsertPredicatedCall(ins, IPOINT_BEFORE, 
                           (AFUNPTR)instruction_execution_simple_logger,
                           IARG_INST_PTR,
                           IARG_END);*/

//   addr_ins_static_map[ins_addr].contained_image = contained_image_name(ins_addr);

  if (
      false
//       || INS_IsCall(ins)
//       || INS_IsSyscall(ins)
//       || INS_IsSysret(ins)
//       || INS_IsNop(ins)
    ) 
  {
    // omit these instructions
  } 
  else 
  {
    /*BOOST_LOG_SEV(log_instance, boost::log::trivial::info) << boost::format("%-15s %-35s %s %d")
      % remove_leading_zeros(StringFromAddrint(ins_addr))
      % addr_ins_static_map[ins_addr].disass
      % addr_ins_static_map[ins_addr].contained_function % received_msg_num;
    log_sink->flush();*/

    if (received_msg_num == 1) 
    {
      if (!start_ptr_time) 
      {
        start_ptr_time.reset(new btime::ptime(btime::microsec_clock::local_time()));
      }

      if (in_tainting) 
      {
        /* START LOGGING */
        /*BOOST_LOG_SEV(log_instance, boost::log::trivial::info) 
          << "instruction tainting activated";
        log_sink->flush();*/

        if (INS_IsSyscall(ins))
        {
          INS_InsertPredicatedCall(ins, IPOINT_BEFORE, 
                                   (AFUNPTR)logging_syscall_instruction_analyzer,
                                   IARG_INST_PTR,
                                   IARG_END);
        } 
        else 
        {
          // general logging
          INS_InsertPredicatedCall(ins, IPOINT_BEFORE, 
                                   (AFUNPTR)logging_general_instruction_analyzer,
                                   IARG_INST_PTR,
                                   IARG_END);

          if (addr_ins_static_map[ins_addr]->is_cbranch/* == XED_CATEGORY_COND_BR*/)
          {
            // conditional branch logging
            INS_InsertPredicatedCall(ins, IPOINT_BEFORE, 
                                     (AFUNPTR)logging_cond_br_analyzer,
                                     IARG_INST_PTR,
                                     IARG_BRANCH_TAKEN,
                                     IARG_END);
          } 
          else 
          {
            if (INS_IsMemoryRead(ins)) 
            {
              // memory read logging
              INS_InsertPredicatedCall(ins, IPOINT_BEFORE, 
                                       (AFUNPTR)logging_mem_read_instruction_analyzer,
                                       IARG_INST_PTR,
                                       IARG_MEMORYREAD_EA, IARG_MEMORYREAD_SIZE,
                                       IARG_CONTEXT,
                                       IARG_END);

              if (INS_HasMemoryRead2(ins))
              {
                // memory read2 (e.g. cmpsb instruction)
                INS_InsertPredicatedCall(ins, IPOINT_BEFORE, 
                                         (AFUNPTR)logging_mem_read2_instruction_analyzer, 
                                         IARG_INST_PTR, 
                                         IARG_MEMORYREAD2_EA, IARG_MEMORYREAD_SIZE, 
                                         IARG_CONTEXT, 
                                         IARG_END);
              }
            }

            if (INS_IsMemoryWrite(ins)) 
            {
              // memory written logging
              INS_InsertPredicatedCall(ins, IPOINT_BEFORE, 
                                       (AFUNPTR)logging_mem_write_instruction_analyzer,
                                       IARG_INST_PTR,
                                       IARG_MEMORYWRITE_EA, IARG_MEMORYWRITE_SIZE,
                                       IARG_END );
            }
          }
        }

        /* START TAINTING */
        INS_InsertPredicatedCall(ins, IPOINT_BEFORE, 
                                 (AFUNPTR)tainting_general_instruction_analyzer,
                                 IARG_INST_PTR,
                                 IARG_END );
      } 
      else // in rollbacking 
      { 
        /* START RESOLVING */
        INS_InsertPredicatedCall(ins, IPOINT_BEFORE, 
                                 (AFUNPTR)resolving_ins_count_analyzer,
                                 IARG_INST_PTR,
                                 IARG_END );

        if (INS_IsMemoryRead(ins)) 
        {
  //           INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)resolving_mem_to_st_analyzer,
  //                                    IARG_INST_PTR,
  //                                    IARG_MEMORYREAD_EA, IARG_MEMORYREAD_SIZE,
  //                                    IARG_END);
        } 
        else 
        {
          if (INS_IsMemoryWrite(ins)) 
          {
            INS_InsertPredicatedCall(ins, IPOINT_BEFORE, 
                                     (AFUNPTR)resolving_st_to_mem_analyzer,
                                     IARG_INST_PTR,
                                     IARG_MEMORYWRITE_EA, IARG_MEMORYWRITE_SIZE,
                                     IARG_END);
          }
        }
        
        // note that conditional branches are always direct
        if (addr_ins_static_map[ins_addr]->is_cbranch/*.category == XED_CATEGORY_COND_BR*/)
        {
          INS_InsertPredicatedCall(ins, IPOINT_BEFORE,
                                   (AFUNPTR)resolving_cond_branch_analyzer,
                                   IARG_INST_PTR,
                                   IARG_BRANCH_TAKEN,
                                   IARG_END);
        }
        else 
        {
          if (INS_IsIndirectBranchOrCall(ins)) 
          {
            INS_InsertPredicatedCall(ins, IPOINT_BEFORE, 
                                     (AFUNPTR)resolving_indirect_branch_call_analyzer, 
                                     IARG_INST_PTR, 
                                     IARG_BRANCH_TARGET_ADDR, 
                                     IARG_END);
          }
        }
      }
    }
  }

  return;
}

/*================================================================================================*/

VOID image_load_instrumenter(IMG loaded_img, VOID *data)
{
  boost::filesystem::path loaded_image_path(IMG_Name(loaded_img));

  BOOST_LOG_SEV(log_instance, logging::trivial::info) 
    << boost::format("module %s loaded") % loaded_image_path.filename();

#if defined(_WIN32) || defined(_WIN64)
  // verify whether the winsock2 module is loaded
  const static std::string winsock_dll_name("WS2_32.dll");
  if (loaded_image_path.filename() == winsock_dll_name)
  {
    BOOST_LOG_SEV(log_instance, boost::log::trivial::info) 
      << "winsock module found";

    RTN recv_function = RTN_FindByName(loaded_img, "recv");
    if (RTN_Valid(recv_function))
    {
      BOOST_LOG_SEV(log_instance, boost::log::trivial::info) << "recv instrumented";

      RTN_Open(recv_function);

      RTN_InsertCall(recv_function, IPOINT_BEFORE, 
                     (AFUNPTR)logging_before_recv_functions_analyzer,
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
                     IARG_END);

      RTN_InsertCall(recv_function, IPOINT_AFTER, 
                     (AFUNPTR)logging_after_recv_functions_analyzer,
                     IARG_FUNCRET_EXITPOINT_VALUE,
                     IARG_END);

      RTN_Close(recv_function);
    }

    RTN recvfrom_function = RTN_FindByName(loaded_img, "recvfrom");
    if (RTN_Valid(recvfrom_function))
    {
      BOOST_LOG_SEV(log_instance, boost::log::trivial::info) 
        << "recvfrom instrumented";

      RTN_Open(recvfrom_function);

      RTN_InsertCall(recvfrom_function, IPOINT_BEFORE,
                     (AFUNPTR)logging_before_recv_functions_analyzer,
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
                     IARG_END);

      RTN_InsertCall(recvfrom_function, IPOINT_AFTER,
                     (AFUNPTR)logging_after_recv_functions_analyzer,
                     IARG_FUNCRET_EXITPOINT_VALUE,
                     IARG_END);

      RTN_Close(recvfrom_function);
    }

    RTN wsarecv_function = RTN_FindByName(loaded_img, "WSARecv");
    if (RTN_Valid(wsarecv_function))
    {
      BOOST_LOG_SEV(log_instance, boost::log::trivial::info) 
        << "WSARecv instrumented";

      RTN_Open(wsarecv_function);

      RTN_InsertCall(wsarecv_function, IPOINT_BEFORE,
        (AFUNPTR)logging_before_wsarecv_functions_analyzer,
        IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
        IARG_END);
      RTN_InsertCall(wsarecv_function, IPOINT_AFTER,
        (AFUNPTR)logging_after_wsarecv_funtions_analyzer,
        IARG_END);

      RTN_Close(wsarecv_function);
    }

    RTN wsarecvfrom_function = RTN_FindByName(loaded_img, "WSARecvFrom");
    if (RTN_Valid(wsarecvfrom_function))
    {
      BOOST_LOG_SEV(log_instance, boost::log::trivial::info) << "WSARecvFrom instrumented";

      RTN_Open(wsarecvfrom_function);

      RTN_InsertCall(wsarecvfrom_function, IPOINT_BEFORE,
        (AFUNPTR)logging_before_wsarecv_functions_analyzer,
        IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
        IARG_END);
      RTN_InsertCall(wsarecvfrom_function, IPOINT_AFTER,
        (AFUNPTR)logging_after_wsarecv_funtions_analyzer,
        IARG_END);

      RTN_Close(wsarecvfrom_function);
    }
  }
#endif
  return;
}

/*================================================================================================*/

BOOL process_create_instrumenter(CHILD_PROCESS created_process, VOID* data)
{
  BOOST_LOG_SEV(log_instance, logging::trivial::warning) 
    << boost::format("new process created with id %d") % CHILD_PROCESS_GetId(created_process);
  return TRUE;
}
