#include <algorithm>

#include "common.h"
#include "rollbacking_functions.h"
#include "../util/stuffs.h"

namespace tainting
{

static std::vector<df_edge_desc>  visited_edges;
static df_diagram                 dta_graph;
static df_vertex_desc_set         dta_outer_vertices;

#if !defined(NDEBUG)
static ptr_cond_direct_instructions_t newly_detected_input_dep_cfis;
static ptr_cond_direct_instructions_t newly_detected_cfis;
#endif


/**
 * @brief The df_bfs_visitor class discovering all dependent edges from a vertex.
 */
class df_bfs_visitor : public boost::default_bfs_visitor
{
public:
  template <typename Edge, typename Graph>
  void tree_edge(Edge e, const Graph& g)
  {
    visited_edges.push_back(e);
  }
};


/**
 * @brief for each executed instruction in this tainting phase, determine the set of input memory
 * addresses that affect to the instruction.
 */
inline static void determine_cfi_input_dependency()
{
  df_vertex_iter vertex_iter;
  df_vertex_iter last_vertex_iter;
  df_bfs_visitor df_visitor;
  std::vector<df_edge_desc>::iterator visited_edge_iter;

  ADDRINT mem_addr;

  UINT32 visited_edge_exec_order;
  ptr_cond_direct_instruction_t visited_cfi;

  // get the set of vertices in the tainting graph
  boost::tie(vertex_iter, last_vertex_iter) = boost::vertices(dta_graph);
  // for each vertice of the tainting graph
  for (; vertex_iter != last_vertex_iter; ++vertex_iter)
  {
    // if it represents some memory address
    if (dta_graph[*vertex_iter]->value.type() == typeid(ADDRINT))
    {
      // and this memory address belongs to the input
      mem_addr = boost::get<ADDRINT>(dta_graph[*vertex_iter]->value);
      if ((received_msg_addr <= mem_addr) && (mem_addr < received_msg_addr + received_msg_size))
      {
        // take a BFS from this vertice
        visited_edges.clear();
        boost::breadth_first_search(dta_graph, *vertex_iter, boost::visitor(df_visitor));

        // for each visited edge
        for (visited_edge_iter = visited_edges.begin();
             visited_edge_iter != visited_edges.end(); ++visited_edge_iter)
        {
          // the value of the edge is the execution order of the corresponding instruction
          visited_edge_exec_order = dta_graph[*visited_edge_iter];
          // consider only the instruction that is beyond the exploring CFI
          if (!exploring_cfi ||
              (exploring_cfi && (visited_edge_exec_order > exploring_cfi->exec_order)))
          {
            // and is some CFI
            if (ins_at_order[visited_edge_exec_order]->is_cond_direct_cf)
            {
              // then this CFI depends on the value of the memory address
              visited_cfi = boost::static_pointer_cast<cond_direct_instruction>(
                    ins_at_order[visited_edge_exec_order]);
              visited_cfi->input_dep_addrs.insert(mem_addr);
            }
          }
        }
      }
    }
  }

  return;
}


/**
 * @brief for each CFI, determine pairs of <checkpoint, affecting input addresses> so that a
 * rollback from the checkpoint with the modification on the affecting input addresses may change
 * the CFI's decision.
 */
inline static void set_checkpoints_for_cfi(ptr_cond_direct_instruction_t cfi)
{
  addrint_set_t dep_addrs = cfi->input_dep_addrs;
  addrint_set_t new_dep_addrs;
  addrint_set_t intersected_addrs;
  checkpoint_with_modified_addrs checkpoint_with_input_addrs;

  ptr_checkpoints_t::iterator chkpnt_iter = saved_checkpoints.begin();
  for (; chkpnt_iter != saved_checkpoints.end(); ++chkpnt_iter)
  {
    // find the intersection between the input addresses of the checkpoint and the affecting input
    // addresses of the CFI
    intersected_addrs.clear();
    std::set_intersection((*chkpnt_iter)->input_dep_addrs.begin(),
                          (*chkpnt_iter)->input_dep_addrs.end(),
                          dep_addrs.begin(), dep_addrs.end(),
                          std::inserter(intersected_addrs, intersected_addrs.begin()));
    // verify if the intersection is not empty
    if (!intersected_addrs.empty())
    {
      // not empty, then the checkpoint and the intersected addrs make a pair, namely when we need
      // to change the decision of the CFI then we should rollback to the checkpoint and modify some
      // value at the address of the intersected addrs
      checkpoint_with_input_addrs = std::make_pair(*chkpnt_iter, intersected_addrs);
      cfi->checkpoints.push_back(checkpoint_with_input_addrs);

      // the addrs in the intersected set are subtracted from the original dep_addrs
      new_dep_addrs.clear();
      std::set_difference(dep_addrs.begin(), dep_addrs.end(),
                          intersected_addrs.begin(), intersected_addrs.end(),
                          std::inserter(new_dep_addrs, new_dep_addrs.begin()));
      // if the rest is empty then we have finished
      if (new_dep_addrs.empty()) break;
      // but if it is not empty then we continue to the next checkpoint
      else dep_addrs = new_dep_addrs;
    }
  }
  return;
}


/**
 * @brief save new tainted CFI in this tainting phase
 */
inline static void save_detected_cfis()
{
  ptr_cond_direct_instruction_t newly_detected_cfi;
  std::map<UINT32, ptr_instruction_t>::iterator executed_ins_iter;

  // iterate over executed instructions in this tainting phase
  for (executed_ins_iter = ins_at_order.begin();
       executed_ins_iter != ins_at_order.end(); ++executed_ins_iter)
  {
    // consider only the instruction that is not behind the exploring CFI
    if (!exploring_cfi ||
        (exploring_cfi && (executed_ins_iter->first > exploring_cfi->exec_order)))
    {
      if (executed_ins_iter->second->is_cond_direct_cf)
      {
        newly_detected_cfi = boost::static_pointer_cast<cond_direct_instruction>(
              executed_ins_iter->second);
        // and depends on the input
        if (!newly_detected_cfi->input_dep_addrs.empty())
        {
          // then set its checkpoints
          set_checkpoints_for_cfi(newly_detected_cfi);
          // and save it
          detected_input_dep_cfis.push_back(newly_detected_cfi);
#if !defined(NDEBUG)
          newly_detected_input_dep_cfis.push_back(newly_detected_cfi);
#endif
        }
#if !defined(NDEBUG)
        newly_detected_cfis.push_back(newly_detected_cfi);
#endif
      }
    }
  }
  return;
}


/**
 * @brief analyze_executed_instructions
 */
inline static void analyze_executed_instructions()
{
  determine_cfi_input_dependency(); save_detected_cfis();
  return;
}


/**
 * @brief calculate_last_input_dep_cfi_exec_order
 */
inline static void calculate_last_input_dep_cfi_exec_order()
{
  last_input_dep_cfi_exec_order = 0;
  std::map<UINT32, ptr_instruction_t>::reverse_iterator ins_iter;
  ptr_cond_direct_instruction_t last_cfi;
  // reverse iterate in the list of executed instructions
  for (ins_iter = ins_at_order.rbegin(); ins_iter != ins_at_order.rend(); ++ins_iter)
  {
    // verif if the instruction is a CFI
    if (ins_iter->second->is_cond_direct_cf)
    {
      // and this CFI depends on the input
      last_cfi = boost::static_pointer_cast<cond_direct_instruction>(ins_iter->second);
      if (!last_cfi->input_dep_addrs.empty())
      {
        last_input_dep_cfi_exec_order = last_cfi->exec_order;
        break;
      }
    }
  }
  return;
}


/**
 * @brief prepare_new_rollbacking_phase
 */
inline void prepare_new_rollbacking_phase()
{
#if !defined(NDEBUG)
  BOOST_LOG_SEV(log_instance, boost::log::trivial::info)
    << boost::format("stop tainting, %d instructions executed; start analyzing...")
        % current_exec_order;
#endif

  analyze_executed_instructions();

#if !defined(NDEBUG)
  BOOST_LOG_SEV(log_instance, boost::log::trivial::info)
    << boost::format("stop analyzing, %d checkpoints, %d/%d branches detected; start rollbacking")
       % saved_checkpoints.size()
       % newly_detected_input_dep_cfis.size() % newly_detected_cfis.size();
#endif

  // calculate the new limit trace length for the rollbacking phase: the limit does not exceed
  // the execution order of the last input dependent CFI
  calculate_last_input_dep_cfi_exec_order();

  // and rollback to the first checkpoint (tainting->rollbacking transition)
  current_running_state = rollbacking_state; rollbacking::initialize_rollbacking_phase();
  PIN_RemoveInstrumentation(); saved_checkpoints[0]->rollback();

  return;
}


/**
 * @brief syscall_instruction
 * @param ins_addr
 * @return
 */
VOID syscall_instruction(ADDRINT ins_addr)
{
  // the tainting phase always finishes when a syscall is met
  prepare_new_rollbacking_phase();
  return;
}


/**
 * @brief general_instruction
 * @param ins_addr
 * @return
 */
VOID general_instruction(ADDRINT ins_addr)
{
  ptr_cond_direct_instruction_t current_cfi, duplicated_cfi;

  // verify if the execution order are not beyond the limit trace length and the executed
  // instruction is always in user-space
  if ((current_exec_order < max_trace_size) && !ins_at_addr[ins_addr]->is_mapped_from_kernel)
  {
    // yes
    current_exec_order++;
    if (ins_at_addr[ins_addr]->is_cond_direct_cf)
    {
      // duplicate a CFI
      current_cfi = boost::static_pointer_cast<cond_direct_instruction>(ins_at_addr[ins_addr]);
      duplicated_cfi.reset(new cond_direct_instruction(*current_cfi));
      duplicated_cfi->exec_order = current_exec_order;
      ins_at_order[current_exec_order] = duplicated_cfi;
    }
    else
    {
      // duplicate an instruction
      ins_at_order[current_exec_order].reset(new instruction(*ins_at_addr[ins_addr]));
    }

    BOOST_LOG_SEV(log_instance, boost::log::trivial::info)
        << boost::format("%-3d %-15s %-50s %-25s %-25s")
           % current_exec_order % addrint_to_hexstring(ins_addr)
           % ins_at_addr[ins_addr]->disassembled_name
           % ins_at_addr[ins_addr]->contained_image
           % ins_at_addr[ins_addr]->contained_function;
  }
  else
  {
    // no
    prepare_new_rollbacking_phase();
  }

  return;
}


/**
 * @brief mem_read_instruction
 * @param ins_addr
 * @param mem_read_addr
 * @param mem_read_size
 * @param p_ctxt
 * @return
 */
VOID mem_read_instruction(ADDRINT ins_addr,
                          ADDRINT mem_read_addr, UINT32 mem_read_size, CONTEXT* p_ctxt)
{
  // a new checkpoint found
  if (std::max(mem_read_addr, received_msg_addr) <
      std::min(mem_read_addr + mem_read_size, received_msg_addr + received_msg_size))
  {
    ptr_checkpoint_t new_ptr_checkpoint(new checkpoint(p_ctxt, mem_read_addr, mem_read_size));
    saved_checkpoints.push_back(new_ptr_checkpoint);

    BOOST_LOG_SEV(log_instance, boost::log::trivial::info)
      << boost::format("checkpoint detected at %d (%s: %s) because memory is read (%s: %d)")
         % new_ptr_checkpoint->exec_order
         % remove_leading_zeros(StringFromAddrint(ins_addr))
         % ins_at_addr[ins_addr]->disassembled_name
         % addrint_to_hexstring(mem_read_addr) % mem_read_size;
  }

  ptr_operand_t mem_operand;
  for (UINT32 idx = 0; idx < mem_read_size; ++idx)
  {
    mem_operand.reset(new operand(mem_read_addr + idx));
    ins_at_order[current_exec_order]->src_operands.insert(mem_operand);
  }

  return;
}


/**
 * @brief mem_write_instruction
 * @param ins_addr
 * @param mem_written_addr
 * @param mem_written_size
 * @return
 */
VOID mem_write_instruction(ADDRINT ins_addr, ADDRINT mem_written_addr, UINT32 mem_written_size)
{
  if (!saved_checkpoints.empty())
  {
    saved_checkpoints[0]->mem_write_tracking(mem_written_addr, mem_written_size);
  }

//  exepoint_checkpoints_map[current_exec_order] = saved_checkpoints;

  ptr_operand_t mem_operand;
  for (UINT32 idx = 0; idx < mem_written_size; ++idx)
  {
    mem_operand.reset(new operand(mem_written_addr + idx));
    ins_at_order[current_exec_order]->dst_operands.insert(mem_operand);
  }

  return;
}

/*================================================================================================*/

//VOID cond_branch_instruction(ADDRINT ins_addr, bool br_taken)
//{
//  ptr_branch_t new_ptr_branch(new branch(ins_addr, br_taken));

//  // save the first input
//  store_input(new_ptr_branch, br_taken);

//  // verify if the branch is a new tainted branch
//  if (exploring_ptr_branch &&
//      (new_ptr_branch->execution_order <= exploring_ptr_branch->execution_order))
//  {
//    // mark it as resolved
//    mark_resolved(new_ptr_branch);
//  }

//  order_tainted_ptr_branch_map[current_exec_order] = new_ptr_branch;

//  return;
//}

/*================================================================================================*/
#if BOOST_OS_WINDOWS
static bool function_has_been_called = false;
VOID logging_before_recv_functions_analyzer(ADDRINT msg_addr)
{
  received_msg_addr = msg_addr;
  function_has_been_called = true;

  BOOST_LOG_SEV(log_instance, boost::log::trivial::info)
    << "recv or recvfrom called, received msg buffer: "
    << remove_leading_zeros(StringFromAddrint(received_msg_addr));
  return;
}

VOID logging_after_recv_functions_analyzer(UINT32 msg_length)
{
  if (function_has_been_called)
  {
    if (msg_length > 0)
    {
      received_msg_num++; received_msg_size = msg_length;

      BOOST_LOG_SEV(log_instance, boost::log::trivial::info)
        << "recv or recvfrom returned, received msg size: " << received_msg_size;
    }

    function_has_been_called = false;
  }
  return;
}

namespace WINDOWS
{
#include <WinSock2.h>
#include <Windows.h>
};
VOID logging_before_wsarecv_functions_analyzer(ADDRINT msg_struct_adddr)
{
  received_msg_struct_addr = msg_struct_adddr;
  received_msg_addr = reinterpret_cast<ADDRINT>(
        (reinterpret_cast<WINDOWS::LPWSABUF>(received_msg_struct_addr))->buf);
  function_has_been_called = true;

  BOOST_LOG_SEV(log_instance, boost::log::trivial::info)
    << "WSARecv or WSARecvFrom called, received msg buffer: "
    << remove_leading_zeros(StringFromAddrint(received_msg_addr));

  return;
}

VOID logging_after_wsarecv_funtions_analyzer()
{
  if (function_has_been_called)
  {
    received_msg_size = (reinterpret_cast<WINDOWS::LPWSABUF>(received_msg_struct_addr))->len;
    if (received_msg_size > 0)
    {
      ++received_msg_num;

      BOOST_LOG_SEV(log_instance, boost::log::trivial::info)
        << "WSARecv or WSARecvFrom returned, received msg size: " << received_msg_size;

      BOOST_LOG_SEV(log_instance, boost::log::trivial::info)
        << boost::format("the first message saved at %s with size %d bytes")
            % remove_leading_zeros(StringFromAddrint(received_msg_addr)) % received_msg_size
        << "\n-------------------------------------------------------------------------------------------------\n"
        << boost::format("start tainting the first time with trace size %d")
            % max_trace_size;

      PIN_RemoveInstrumentation();
    }
    function_has_been_called = false;
  }

  return;
}
#elif BOOST_OS_LINUX
VOID syscall_entry_analyzer(THREADID thread_id,
                            CONTEXT* p_ctxt, SYSCALL_STANDARD syscall_std, VOID *data)
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


/**
 * @brief syscall_exit_analyzer
 * @param thread_id
 * @param p_ctxt
 * @param syscall_std
 * @param data
 * @return
 */
VOID syscall_exit_analyzer(THREADID thread_id,
                           CONTEXT* p_ctxt, SYSCALL_STANDARD syscall_std, VOID *data)
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

        BOOST_LOG_TRIVIAL(info)
          << boost::format("the first message saved at %s with size %d bytes\n%s\n%s %d")
             % addrint_to_hexstring(received_msg_addr) % received_msg_size
             % "-----------------------------------------------------------------------------------"
             % "start tainting the first time with trace size" % max_trace_size;

        PIN_RemoveInstrumentation();
      }
    }
  }

  return;
}
#endif


/**
 * @brief source_variables
 * @param idx
 * @return
 */
inline std::set<df_vertex_desc> source_variables(UINT32 ins_exec_order)
{
  df_vertex_desc_set src_vertex_descs;
  df_vertex_desc_set::iterator outer_vertex_iter;
  df_vertex_desc new_vertex_desc;
  std::set<ptr_operand_t>::iterator src_operand_iter;
  for (src_operand_iter = ins_at_order[ins_exec_order]->src_operands.begin();
       src_operand_iter != ins_at_order[ins_exec_order]->src_operands.end(); ++src_operand_iter)
  {
    // verify if the current source operand is
    for (outer_vertex_iter = dta_outer_vertices.begin();
         outer_vertex_iter != dta_outer_vertices.end(); ++outer_vertex_iter)
    {
      // found in the outer interface
      if (((*src_operand_iter)->value.type() == dta_graph[*outer_vertex_iter]->value.type()) &&
          ((*src_operand_iter)->name == dta_graph[*outer_vertex_iter]->name))
      {
        src_vertex_descs.insert(*outer_vertex_iter);
        break;
      }
    }

    // not found
    if (outer_vertex_iter == dta_outer_vertices.end())
    {
      new_vertex_desc = boost::add_vertex(*src_operand_iter, dta_graph);
      dta_outer_vertices.insert(new_vertex_desc);
      src_vertex_descs.insert(new_vertex_desc);
    }
  }

  return src_vertex_descs;
}


/**
 * @brief destination_variables
 * @param idx
 * @return
 */
inline std::set<df_vertex_desc> destination_variables(UINT32 idx)
{
  std::set<df_vertex_desc> dst_vertex_descs;
  df_vertex_desc new_vertex_desc;
  df_vertex_desc_set::iterator outer_vertex_iter;
  df_vertex_desc_set::iterator next_vertex_iter;
  std::set<ptr_operand_t>::iterator dst_operand_iter;

  for (dst_operand_iter = ins_at_order[idx]->dst_operands.begin();
       dst_operand_iter != ins_at_order[idx]->dst_operands.end(); ++dst_operand_iter)
  {
    // verify if the current target operand is
    outer_vertex_iter = dta_outer_vertices.begin();
    for (next_vertex_iter = outer_vertex_iter;
         outer_vertex_iter != dta_outer_vertices.end(); outer_vertex_iter = next_vertex_iter)
    {
      ++next_vertex_iter;

      // found in the outer interface
      if (((*dst_operand_iter)->value.type() == dta_graph[*outer_vertex_iter]->value.type())
          && ((*dst_operand_iter)->name == dta_graph[*outer_vertex_iter]->name))
      {
        // then insert the current target operand into the graph
        new_vertex_desc = boost::add_vertex(*dst_operand_iter, dta_graph);

        // and modify the outer interface by replacing the old vertex with the new vertex
        dta_outer_vertices.erase(outer_vertex_iter);
        dta_outer_vertices.insert(new_vertex_desc);

        dst_vertex_descs.insert(new_vertex_desc);
        break;
      }
    }

    // not found
    if (outer_vertex_iter == dta_outer_vertices.end())
    {
      // then insert the current target operand into the graph
      new_vertex_desc = boost::add_vertex(*dst_operand_iter, dta_graph);

      // and modify the outer interface by insert the new vertex
      dta_outer_vertices.insert(new_vertex_desc);

      dst_vertex_descs.insert(new_vertex_desc);
    }
  }

  return dst_vertex_descs;
}


/**
 * @brief graphical_propagation
 * @param ins_addr
 * @return
 */
VOID graphical_propagation(ADDRINT ins_addr)
{
  std::set<df_vertex_desc> src_vertex_descs = source_variables(current_exec_order);
  std::set<df_vertex_desc> dst_vertex_descs = destination_variables(current_exec_order);

  std::set<df_vertex_desc>::iterator src_vertex_desc_iter;
  std::set<df_vertex_desc>::iterator dst_vertex_desc_iter;

  // insert the edges between each pair (source, destination) into the tainting graph
  for (src_vertex_desc_iter = src_vertex_descs.begin();
       src_vertex_desc_iter != src_vertex_descs.end(); ++src_vertex_desc_iter) 
  {
    for (dst_vertex_desc_iter = dst_vertex_descs.begin();
         dst_vertex_desc_iter != dst_vertex_descs.end(); ++dst_vertex_desc_iter) 
    {
      boost::add_edge(*src_vertex_desc_iter, *dst_vertex_desc_iter, current_exec_order, dta_graph);
    }
  }

  return;
}

void initialize_tainting_phase()
{
  dta_graph.clear(); dta_outer_vertices.clear(); saved_checkpoints.clear();
  newly_detected_input_dep_cfis.clear(); newly_detected_cfis.clear();
  return;
}

} // end of tainting namespace
