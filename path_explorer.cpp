#include <pin.H>

#include <map>
#include <vector>
#include <algorithm>
#include <stack>
#include <sstream>
#include <fstream>
#include <string>
#include <iostream>
#include <limits>

#include <boost/timer.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include "instruction.h"
#include "checkpoint.h"
#include "stuffs.h"
#include "variable.h"
#include "branch.h"
#include "instrumentation_functions.h"
#include "analysis_functions.h"

extern "C" 
{
#include <xed-interface.h>
}

/* ------------------------------------------------------------------------------------------------------------------ */
/*                                                      global variables                                              */
/* ------------------------------------------------------------------------------------------------------------------ */
std::map<ADDRINT, instruction>                addr_ins_static_map;    // statically examined instructions
std::map<UINT32, instruction>                 order_ins_dynamic_map;  // dynamically examined instructions

ADDRINT                                       logged_syscall_index;   // logged syscall index
ADDRINT                                       logged_syscall_args[6]; // logged syscall arguments

UINT32                                        total_rollback_times;
UINT32                                        local_rollback_times;
UINT32                                        max_total_rollback_times;
UINT32                                        max_local_rollback_times;
UINT32                                        max_trace_size;

bool                                          in_tainting;

vdep_graph                                    dta_graph;
map_ins_io                                    dta_inss_io;
vdep_vertex_desc_set                          dta_outer_vertices;

std::vector<ptr_checkpoint>                   saved_ptr_checkpoints;
ptr_checkpoint                                master_ptr_checkpoint;
ptr_checkpoint                                active_ptr_checkpoint;

std::set<ADDRINT>                             active_input_dep_addrs;

std::pair< ptr_checkpoint, 
           std::set<ADDRINT> >                active_nearest_checkpoint;

std::map< UINT32,
          std::vector<ptr_checkpoint> >       exepoint_checkpoints_map;

std::map<UINT32, ptr_branch>                  order_input_dep_ptr_branch_map;
std::map<UINT32, ptr_branch>                  order_input_indep_ptr_branch_map;
std::map<UINT32, ptr_branch>                  order_tainted_ptr_branch_map;

std::vector<ptr_branch>                       found_new_ptr_branches;
std::vector<ptr_branch>                       resolved_ptr_branches;

ptr_branch                                    active_ptr_branch;
ptr_branch                                    exploring_ptr_branch;

std::vector<ADDRINT>                          explored_trace;

UINT8                                         received_msg_num;
ADDRINT                                       received_msg_addr;
UINT32                                        received_msg_size;

boost::shared_ptr<boost::posix_time::ptime>   start_ptr_time;
boost::shared_ptr<boost::posix_time::ptime>   stop_ptr_time;

// std::ofstream                                 tainting_log_file;

/* ------------------------------------------------------------------------------------------------------------------ */
/*                                               input handler functions                                              */
/* ------------------------------------------------------------------------------------------------------------------ */
KNOB<BOOL>    print_debug_text    (KNOB_MODE_WRITEONCE, "pintool",
                                   "d", "1",
                                   "print debug text" );

KNOB<UINT32>  max_local_rollback  (KNOB_MODE_WRITEONCE, "pintool",
                                   "r", "7000",
                                   "specify the maximum local number of rollback" );

KNOB<UINT32>  max_total_rollback  (KNOB_MODE_WRITEONCE, "pintool",
                                   "t", "4000000000",
                                   "specify the maximum total number of rollback" );

KNOB<UINT32>  max_trace_length    (KNOB_MODE_WRITEONCE, "pintool", 
                                   "l", "100", "specify the length of the longest trace" );

/* ------------------------------------------------------------------------------------------------------------------ */
/*                                                instrumental functions                                              */
/* -------------------------------------------------------+---------------------------------------------------------- */
VOID start_tracing(VOID *data)
{
  max_trace_size        = max_trace_length.Value();

  total_rollback_times = 0;
  local_rollback_times = 0;
  max_total_rollback_times = max_total_rollback.Value();
  max_local_rollback_times = max_local_rollback.Value();
  
  in_tainting           = true;
  received_msg_num      = 0;
  logged_syscall_index  = syscall_inexist;
  ::srand(::time(0));

  if ( print_debug_text ) {
//     tainting_log_file.open("tainting_log", std::ofstream::trunc);
//     std::cout << "\033[2J\033[1;1H"; // clear screen
  }

  return;
}

/*====================================================================================================================*/

VOID stop_tracing(INT32 code, VOID *data)
{
  if (!stop_ptr_time) 
  {
    stop_ptr_time.reset(new boost::posix_time::ptime(boost::posix_time::microsec_clock::local_time()));
  }

  boost::posix_time::time_duration elapsed_time = *stop_ptr_time - *start_ptr_time;
  long elapsed_millisec = elapsed_time.total_milliseconds();
  
  BOOST_LOG_TRIVIAL(info) << boost::format("Stop examining, %d milli-seconds elapsed and %d rollback used.") 
                              % elapsed_millisec % total_rollback_times
                          << boost::format("%d / %d branches resolved.") 
                              % (resolved_ptr_branches.size() + found_new_ptr_branches.size()) 
                              % order_input_dep_ptr_branch_map.size();

                          
//   if (print_debug_text) 
//   {
//     UINT32 resolved_branch_num = resolved_ptr_branches.size();
//     UINT32 input_dep_branch_num = found_new_ptr_branches.size();
// 
//     std::vector<ptr_branch>::iterator ptr_branch_iter = tainted_ptr_branches.begin();
//     for (; ptr_branch_iter != tainted_ptr_branches.end(); ++ptr_branch_iter) 
//     {
//       if (!(*ptr_branch_iter)->dep_input_addrs.empty()) 
//       {
//         input_dep_branch_num++;
//       }
//     }
// 
//     std::cerr << "\033[33mExamining stopped.\033[0m\n"
//               << "-------------------------------------------------------------------------------------------------\n"
//               << elapsed_millisec << " milli-seconds elapsed.\n"
//               << total_rollback_times << " rollbacks used.\n"
//               << resolved_branch_num << "/" << input_dep_branch_num << " branches successfully resolved.\n"
//               << "-------------------------------------------------------------------------------------------------\n";
// 
// //     journal_explored_trace("explored_trace", explored_trace);
//     journal_static_trace("static_trace");
//     journal_tainting_graph("tainting_graph.dot");
// 
// //     journal_branch_messages(resolved_ptr_branches[0]);
// //     tainting_log_file.close();
//   }

//   journal_result_total(max_total_rollback.Value(), used_rollback_times,
//                        max_trace_length.Value(), input_dep_ptr_branches.size(), succeeded_branches);

  return;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/*                                                        main function                                               */
/* ------------------------------------------------------------------------------------------------------------------ */
int main (int argc, char *argv[])
{
  PIN_InitSymbols();
  PIN_Init(argc, argv);

  // 0 is the (unused) input data
  PIN_AddApplicationStartFunction(start_tracing, 0);

  INS_AddInstrumentFunction(ins_instrumenter, 0);

  PIN_AddSyscallEntryFunction(syscall_entry_analyzer, 0 );
  PIN_AddSyscallExitFunction(syscall_exit_analyzer, 0 );

  PIN_AddFiniFunction(stop_tracing, 0 );

  // now the control is passed to pin, so the main function will never return
  PIN_StartProgram();
  return 0;
}
