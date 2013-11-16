#include <pin.H>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>

#include "variable.h"
#include "instruction.h"
#include "checkpoint.h"
#include "branch.h"
#include "stuffs.h"

extern std::map< ADDRINT,
                 instruction >                    addr_ins_static_map;
extern std::map< UINT32, 
                 instruction >                    order_ins_dynamic_map;

extern bool                                       in_tainting;

extern vdep_graph                                 dta_graph;

extern std::vector<ADDRINT>                       explored_trace;

extern ptr_checkpoint                             master_ptr_checkpoint;
extern ptr_checkpoint                             last_active_ptr_checkpoint;
extern std::vector<ptr_checkpoint>                saved_ptr_checkpoints;

extern std::pair< ptr_checkpoint, 
                  std::set<ADDRINT> >             active_nearest_checkpoint;

extern std::map< UINT32,
       std::vector<ptr_checkpoint> >              exepoint_checkpoints_map;

extern UINT32                                     total_rollback_times;
extern UINT32                                     local_rollback_times;
extern UINT32                                     max_total_rollback_times;
extern UINT32                                     max_local_rollback_times;

extern ADDRINT                                    received_msg_addr;
extern UINT32                                     received_msg_size;

extern std::map<UINT32, ptr_branch>               order_input_dep_ptr_branch_map;
extern std::map<UINT32, ptr_branch>               order_input_indep_ptr_branch_map;
extern std::map<UINT32, ptr_branch>               order_tainted_ptr_branch_map;

extern std::vector<ptr_branch>                    found_new_ptr_branches;
extern std::vector<ptr_branch>                    resolved_ptr_branches;

extern ptr_branch                                 active_ptr_branch;
extern ptr_branch                                 last_active_ptr_branch;
extern ptr_branch                                 exploring_ptr_branch;

extern std::set<ADDRINT>                          active_input_dep_addrs;

extern UINT32                                     input_dep_branch_num;
extern UINT32                                     resolved_branch_num;

extern KNOB<UINT32>                               max_total_rollback;
extern KNOB<UINT32>                               max_local_rollback;
extern KNOB<UINT32>                               max_trace_length;
extern KNOB<BOOL>                                 print_debug_text;

/*====================================================================================================================*/

inline void print_debug_found_new(ADDRINT ins_addr, ptr_branch& found_ptr_branch)
{
  if (print_debug_text) 
  {
    std::cout << boost::format("\033[35mFound new   %-5i %-20s %-35s\033[0m\n")
                  % explored_trace.size() % remove_leading_zeros(StringFromAddrint(ins_addr)) 
                  % addr_ins_static_map[ins_addr].disass;
  }
  return;
}

/*====================================================================================================================*/

VOID resolving_ins_count_analyzer(ADDRINT ins_addr)
{
  explored_trace.push_back(ins_addr);
  return;
}

/*====================================================================================================================*/
// memory read
VOID resolving_mem_to_st_analyzer(ADDRINT ins_addr, ADDRINT mem_read_addr, UINT32 mem_read_size) 
{
  return;
}

/*====================================================================================================================*/
// memory written
VOID resolving_st_to_mem_analyzer(ADDRINT ins_addr, ADDRINT mem_written_addr, UINT32 mem_written_size) 
{
//   if (active_ptr_branch) // in resolving
  if (active_nearest_checkpoint.first) // in rollbacking
  {
//     active_ptr_branch->checkpoint->mem_written_logging(ins_addr, mem_written_addr, mem_written_size);
    active_nearest_checkpoint.first->mem_written_logging(ins_addr, mem_written_addr, mem_written_size);
  }
  else // in forwarding
  {
    std::vector<ptr_checkpoint>::iterator ptr_checkpoint_iter = exepoint_checkpoints_map[explored_trace.size()].begin();
    for (; ptr_checkpoint_iter != exepoint_checkpoints_map[explored_trace.size()].end(); ++ptr_checkpoint_iter)
    {
      (*ptr_checkpoint_iter)->mem_written_logging(ins_addr, mem_written_addr, mem_written_size);
    }

    master_ptr_checkpoint->mem_written_logging(ins_addr, mem_written_addr, mem_written_size);
  }

  return;
}

/*====================================================================================================================*/

inline void prepare_new_tainting_phase(ptr_branch& unexplored_ptr_branch)
{
  in_tainting = true;
  exploring_ptr_branch = unexplored_ptr_branch;

//   vdep_graph().swap(dta_graph);
//   std::vector<ptr_branch>().swap(input_dep_ptr_branches);
//   std::vector<ptr_branch>().swap(input_indep_ptr_branches);
//   std::vector<ptr_checkpoint>().swap(saved_ptr_checkpoints);
//   std::map< UINT32, std::vector<ptr_checkpoint> >().swap(exepoint_checkpoints_map);
  
  dta_graph.clear();
  order_input_dep_ptr_branch_map.clear();
  order_input_indep_ptr_branch_map.clear();
  saved_ptr_checkpoints.clear();
  exepoint_checkpoints_map.clear();

  unexplored_ptr_branch->is_explored = true;
  
  active_ptr_branch.reset();
  active_nearest_checkpoint.first.reset();
  active_nearest_checkpoint.second.clear();

  return;
}

/*====================================================================================================================*/

inline ptr_branch first_unexplored_branch()
{
  ptr_branch unexplored_ptr_branch;
  
  std::vector<ptr_branch>::iterator unexplored_ptr_branch_iter = resolved_ptr_branches.begin();
  for (; unexplored_ptr_branch_iter != resolved_ptr_branches.end(); ++unexplored_ptr_branch_iter)
  {
    if (!(*unexplored_ptr_branch_iter)->is_explored)
    {
      unexplored_ptr_branch = *unexplored_ptr_branch_iter;
      break;
    }
  }
  return unexplored_ptr_branch;
}

/*====================================================================================================================*/

inline void accept_branch(ptr_branch& accepted_ptr_branch)
{
  accepted_ptr_branch->is_resolved      = true;
  accepted_ptr_branch->is_just_resolved = true;
  accepted_ptr_branch->is_bypassed      = false;

  resolved_ptr_branches.push_back(accepted_ptr_branch);
  return;
}

/*====================================================================================================================*/

inline void bypass_branch(ptr_branch& bypassed_ptr_branch)
{
  bypassed_ptr_branch->is_resolved      = true;
  bypassed_ptr_branch->is_just_resolved = true;
  bypassed_ptr_branch->is_bypassed      = true;
  return;
}

/*====================================================================================================================*/

inline void get_next_nearest_checkpoint(ptr_branch& current_ptr_branch) 
{
  std::map<ptr_checkpoint, std::set<ADDRINT>, ptr_checkpoint_less>::iterator nearest_checkpoint_iter;
  std::map<ptr_checkpoint, std::set<ADDRINT>, ptr_checkpoint_less>::iterator next_nearest_checkpoint_iter;
  
  if (active_nearest_checkpoint.first) 
  {
    nearest_checkpoint_iter = current_ptr_branch->nearest_checkpoints.begin();
    next_nearest_checkpoint_iter = current_ptr_branch->nearest_checkpoints.end();
    
    for (; nearest_checkpoint_iter != current_ptr_branch->nearest_checkpoints.end(); ++nearest_checkpoint_iter) 
    {
      if ((*nearest_checkpoint_iter).first == active_nearest_checkpoint.first) 
      {
        break;
      }
      else 
      {
        next_nearest_checkpoint_iter = nearest_checkpoint_iter;
      }
    }
    
    if (nearest_checkpoint_iter != current_ptr_branch->nearest_checkpoints.end()) 
    {
      if (next_nearest_checkpoint_iter != current_ptr_branch->nearest_checkpoints.end()) 
      {
        active_nearest_checkpoint.first = (*next_nearest_checkpoint_iter).first;
        active_nearest_checkpoint.second.insert((*next_nearest_checkpoint_iter).second.begin(), 
                                                (*next_nearest_checkpoint_iter).second.end());
      }
      else // the next nearest checkpoint cannot found, 
      {
        // so reset the active nearest checkpoint
        active_nearest_checkpoint.first.reset();
        active_nearest_checkpoint.second.clear();
      }
    }
    else 
    {
      BOOST_LOG_TRIVIAL(fatal) << boost::format("Nearest checkpoint for the branch at %d cannot found.") 
                                    % current_ptr_branch->trace.size();
      PIN_ExitApplication(4);
    }
  }
  else 
  {
    active_nearest_checkpoint.first = current_ptr_branch->nearest_checkpoints.rbegin()->first;
    active_nearest_checkpoint.second = current_ptr_branch->nearest_checkpoints.rbegin()->second;
  }
  
  return;
}

/*====================================================================================================================*/

inline void exploring_new_branch_or_stop()
{
  ptr_branch unexplored_ptr_branch = first_unexplored_branch();
  if (unexplored_ptr_branch) 
  {
    BOOST_LOG_TRIVIAL(info) << boost::format("Rollbacking phase stop at %d, %d / %d branches resolved") 
                                % resolved_ptr_branches.size() 
                                % resolved_ptr_branches.size() % order_tainted_ptr_branch_map.size();
                                
    prepare_new_tainting_phase(unexplored_ptr_branch);
    
    total_rollback_times++;
    local_rollback_times++;
    
    PIN_RemoveInstrumentation();
    
    bool new_br_taken = !exploring_ptr_branch->br_taken;
    rollback_with_input_replacement(master_ptr_checkpoint, exploring_ptr_branch->inputs[new_br_taken][0].get());
  }
  else 
  {
    BOOST_LOG_TRIVIAL(info) << "Stop exploring, all branches are explored.";
    PIN_ExitApplication(0);
  }
  
  return;
}


/**
 * @brief handle the case where the examined branch is marked as input dependent but it is resolved. Note that 
 * a resolved branch is examined only in rollback, moreover in the following procedure the examined branch is not the 
 * active branch.
 * 
 * @param ins_addr ...
 * @param br_taken ...
 * @param examined_ptr_branch ...
 * @return void
 */
inline void process_input_dependent_and_resolved_branch(ADDRINT ins_addr, bool br_taken, ptr_branch& examined_ptr_branch)
{
  if (examined_ptr_branch == active_ptr_branch) // this is a re-execution from a rollback_with_input_replacement
  {
    // go forward
    active_ptr_branch.reset();
    active_nearest_checkpoint.first.reset();
    active_nearest_checkpoint.second.clear();
    local_rollback_times = 0;
  }
  else 
  {
    if (examined_ptr_branch->br_taken != br_taken) // new branch taken
    {
      if (examined_ptr_branch->is_bypassed)
      {
        BOOST_LOG_TRIVIAL(trace) 
          << boost::format("\033[36mThe branch at %d is resolved accidentally in resolving the branch at %d.\033[0m") 
              % examined_ptr_branch->trace.size() % active_ptr_branch->trace.size();
              
        // the branch has been marked as bypassed before, then is resolved accidentally
        accept_branch(examined_ptr_branch);
      }
      
      total_rollback_times++; 
      if (local_rollback_times < max_local_rollback_times)
      {
        // the original trace will be lost if go further, so rollback
        local_rollback_times++;
        rollback_with_input_random_modification(active_nearest_checkpoint.first, active_nearest_checkpoint.second);
      }
      else
      {
        // back to the original trace
        local_rollback_times++;
        rollback_with_input_replacement(active_nearest_checkpoint.first, 
                                        active_ptr_branch->inputs[active_ptr_branch->br_taken][0].get());
      }
    }
  }

  return;
}


/**
 * @brief handle the case where the examined branch takes a different target. Note that this branch is always the active 
 * branch because the following procedure is used in process_input_dependent_but_unresolved_branch.
 * 
 * @param ins_addr ...
 * @param br_taken ...
 * @param examined_ptr_branch ...
 * @return void
 */
inline void new_branch_taken_processing(ADDRINT ins_addr, bool br_taken, ptr_branch& examined_ptr_branch)
{
  if (active_ptr_branch) // active_ptr_branch is enabled, namely in some rollback
  {
    // so this branch is resolved
    BOOST_LOG_TRIVIAL(trace) 
      << boost::format("\033[32mThe branch at %d is successfully resolved after %d rollbacks.\033[0m") 
          % examined_ptr_branch->trace.size() % local_rollback_times;
    
    accept_branch(active_ptr_branch);
    
    // now back to the original trace
    total_rollback_times++;
    local_rollback_times++;
    rollback_with_input_replacement(active_nearest_checkpoint.first, 
                                    active_ptr_branch->inputs[active_ptr_branch->br_taken][0].get());
  }
  else // active_ptr_branch is disabled, namely in some forward
  {
    BOOST_LOG_TRIVIAL(fatal) << boost::format("The branch at %d takes a different decision in forwarding.") 
                                  % explored_trace.size();
    PIN_ExitApplication(2);
  }

  return;
}


/**
 * @brief handle the case where the examined branch takes the same decision. Note that this branch is always the active 
 * branch because the following procedure is used in process_input_dependent_but_unresolved_branch.
 * 
 * @param ins_addr ...
 * @param br_taken ...
 * @param tainted_ptr_branch ...
 * @return void
 */
inline void same_branch_taken_processing(ADDRINT ins_addr, bool br_taken, ptr_branch& examined_ptr_branch)
{
  static ptr_checkpoint tmp_stored_ptr_checkpoint;
  
  if (active_ptr_branch) // active_ptr_branch is enabled, namely in some rollback
  {
    // so verify that
    if (active_ptr_branch != examined_ptr_branch) 
    {
      BOOST_LOG_TRIVIAL(fatal) 
        << boost::format("In rollback but the tainted (at %d) and the active branch (at %d) are not matched.") 
            % examined_ptr_branch->trace.size() % active_ptr_branch->trace.size();
      PIN_ExitApplication(3);
    }
  }
  else // active_ptr_branch is disabled, namely in some forward and meet a new input dependent branch
  {
    // so enable active_ptr_branch
    active_ptr_branch = examined_ptr_branch;
    get_next_nearest_checkpoint(active_ptr_branch); // the active_nearest_checkpoint is updated 
    
    local_rollback_times = 0;
    
    BOOST_LOG_TRIVIAL(trace) 
      << boost::format("Resolve the branch at %d by rollback to the checkpoint at %d.") 
          % examined_ptr_branch->trace.size() % active_nearest_checkpoint.first->trace.size();
  }
  
  // and resolve the active_ptr_branch
  total_rollback_times++;
  
  if (local_rollback_times < max_local_rollback_times) 
  {
    local_rollback_times++;
    rollback_with_input_random_modification(active_nearest_checkpoint.first, active_nearest_checkpoint.second);
  }
  else // reach the limit number of the rollback test for the current active_nearest_checkpoint
  {
    local_rollback_times = 0;
    
    // try to get new active_nearest_checkpoint
    last_active_ptr_checkpoint = active_nearest_checkpoint.first;
    get_next_nearest_checkpoint(active_ptr_branch);
    
    if (active_nearest_checkpoint.first) // found a new active_nearest_checkpoint
    {
      // then rollback to it
      BOOST_LOG_TRIVIAL(trace) << boost::format("Resolve the branch at %d by rollback to the checkpoint at %d.") 
                                    % active_ptr_branch->trace.size() % active_nearest_checkpoint.first->trace.size();
     
      local_rollback_times++;
      rollback_with_input_random_modification(active_nearest_checkpoint.first, active_nearest_checkpoint.second);
    }
    else // cannot found a new active_nearest_checkpoint
    {
      // so bypass this branch
      BOOST_LOG_TRIVIAL(trace) << boost::format("\033[31mCannot resolve the branch at %d, bypass it.\033[0m") 
                                    % active_ptr_branch->trace.size();
                                    
      bypass_branch(active_ptr_branch);      

      // and back to the original trace
      local_rollback_times++;
      rollback_with_input_replacement(last_active_ptr_checkpoint, 
                                      active_ptr_branch->inputs[active_ptr_branch->br_taken][0].get());
    }
  }

  return;
}


/**
 * @brief ...
 * 
 * @param ins_addr ...
 * @param br_taken ...
 * @param examined_ptr_branch ...
 * @return void
 */
inline void process_input_dependent_but_unresolved_branch(ADDRINT ins_addr, bool br_taken, ptr_branch& examined_ptr_branch)
{
  if (examined_ptr_branch->br_taken != br_taken) // other decision is taken
  {
    new_branch_taken_processing(ins_addr, br_taken, examined_ptr_branch);
  }
  else // the same decision is taken
  {
    same_branch_taken_processing(ins_addr, br_taken, examined_ptr_branch);
  }

  return;
}


/**
 * @brief handle the case where the examined branch is detected as input dependent.
 * 
 * @param ins_addr ...
 * @param br_taken ...
 * @param tainted_ptr_branch ...
 * @return void
 */
inline void process_input_dependent_branch(ADDRINT ins_addr, bool br_taken, ptr_branch& examined_ptr_branch)
{
//   if (total_rollback_times >= max_total_rollback_times)
//   {
//     BOOST_LOG_TRIVIAL(info) << "Stop exploring, the total rollback number exceeds its limit value.";
//     PIN_ExitApplication(0);
//   }
//   else 
//   {
//     
//   }
  
  if (examined_ptr_branch->is_resolved) // which is resolved
  {
    if (examined_ptr_branch == order_input_dep_ptr_branch_map.rbegin()->second) // and is the current last branch
    {
      /* FOR TESTING ONLY */
      BOOST_LOG_TRIVIAL(warning) << "FOR TESTING ONLY: stop at the last branch of the first tainting result.";
      PIN_ExitApplication(0);

      exploring_new_branch_or_stop();
    }
    else // it is not the last branch
    {
      process_input_dependent_and_resolved_branch(ins_addr, br_taken, examined_ptr_branch);
    }
  }
  else // it is not resolved yet
  {
    process_input_dependent_but_unresolved_branch(ins_addr, br_taken, examined_ptr_branch);
  }

  return;
}


/**
 * @brief ...
 * 
 * @param ins_addr ...
 * @param br_taken ...
 * @param examined_ptr_branch ...
 * @return void
 */
inline void process_input_independent_branch(ADDRINT ins_addr, bool br_taken, ptr_branch& examined_ptr_branch)
{
  if (examined_ptr_branch->br_taken != br_taken) // new taken found
  {
    if (active_ptr_branch) // active_ptr_branch is enabled, namely in some rollback
    {
      if (!examined_ptr_branch->is_resolved)
      {
        print_debug_found_new(ins_addr, examined_ptr_branch);
        accept_branch(examined_ptr_branch);
        found_new_ptr_branches.push_back(examined_ptr_branch);
      }

      // the original trace will lost if go further, so rollback
      total_rollback_times++;
      local_rollback_times++;
      
      rollback_with_input_random_modification(active_nearest_checkpoint.first, active_nearest_checkpoint.second);
    }
    else // active_ptr_branch is disabled, namely in forwarding
    {
      // but new taken found
      BOOST_LOG_TRIVIAL(fatal) << boost::format("The branch at %d takes a new decision in forwarding.") 
                                    % examined_ptr_branch->trace.size();
      PIN_ExitApplication(0);
    }
  }

  return;
}

/*====================================================================================================================*/

inline void log_input(ADDRINT ins_addr, bool br_taken)
{
  std::map<UINT32, ptr_branch>::iterator order_ptr_branch_iter;
  
  order_ptr_branch_iter = order_tainted_ptr_branch_map.find(explored_trace.size());
  if (order_ptr_branch_iter != order_tainted_ptr_branch_map.end()) 
  {
    if (order_ptr_branch_iter->second->inputs[br_taken].empty()) 
    {
      store_input(order_ptr_branch_iter->second, br_taken);
    }
  }
  else 
  {
    BOOST_LOG_TRIVIAL(fatal) << boost::format("The branch at %d cannot found.") % explored_trace.size();
    PIN_ExitApplication(0);
  }

  return;
}

/*====================================================================================================================*/

VOID resolving_cond_branch_analyzer(ADDRINT ins_addr, bool br_taken)
{
  log_input(ins_addr, br_taken);
  
  std::map<UINT32, ptr_branch>::iterator order_ptr_branch_iter;
  
  // search in the list of input dependent branches
  order_ptr_branch_iter = order_input_dep_ptr_branch_map.find(explored_trace.size());
  if (order_ptr_branch_iter != order_input_dep_ptr_branch_map.end()) 
  {
    process_input_dependent_branch(ins_addr, br_taken, order_ptr_branch_iter->second);
  }
  else 
  {
    // search in the list of input independent branches
    order_ptr_branch_iter = order_input_indep_ptr_branch_map.find(explored_trace.size());
    if (order_ptr_branch_iter != order_input_indep_ptr_branch_map.end()) 
    {
      process_input_independent_branch(ins_addr, br_taken, order_ptr_branch_iter->second);
    }
    else 
    {
      BOOST_LOG_TRIVIAL(fatal) << boost::format("The branch at %d cannot found.") % explored_trace.size();
      PIN_ExitApplication(0);
    }
  }

  return;
}


/**
 * @brief handle the case where the indirect branch may leads to a new target.
 * 
 * @param ins_addr instruction address.
 * @param target_addr target address.
 * @return VOID
 */
VOID resolving_indirect_branch_call_analyzer(ADDRINT ins_addr, ADDRINT target_addr)
{
  if (order_ins_dynamic_map[explored_trace.size() + 1].address != target_addr) 
  {
    if (active_ptr_branch) // active_ptr_branch is enabled, namely in some rollback
    {
      // the original trace will lost if go further, so rollback
      total_rollback_times++;
      local_rollback_times++;
      
      rollback_with_input_random_modification(active_nearest_checkpoint.first, active_nearest_checkpoint.second);
    }
    else // active_ptr_branch is empty, namely in forwarding, but new target found
    {
      BOOST_LOG_TRIVIAL(fatal) << boost::format("The indirect branch at %d takes a different decision in forwarding.") 
                                    % explored_trace.size();
      PIN_ExitApplication(0);
    }    
  }
  return;
}
