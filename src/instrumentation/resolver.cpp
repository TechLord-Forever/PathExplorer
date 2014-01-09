/*
 * <one line to give the program's name and a brief idea of what it does.>
 * Copyright (C) 2013  Ta Thanh Dinh <thanhdinh.ta@inria.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "resolver.h"
#include "../utilities/utils.h"
#include "../engine/fast_execution.h"
#include "../main.h"
#include <boost/log/trivial.hpp>
#include <boost/format.hpp>

namespace instrumentation 
{
  
using namespace utilities;  
using namespace engine;

static resolving_state current_resolving_state = execution_with_orig_input;
static UINT32 local_reexec_number = 0;

/**
 * @brief set a new resolving state.
 * 
 * @param new_resolving_state 
 * @return void
 */
void resolver::set_resolving_state(resolving_state new_resolving_state)
{
  current_resolving_state = new_resolving_state;
  return;
}


/**
 * @brief generic callback applied for all instructions, principally it is very similar to the 
 * "generic normal instruction" callback in the trace-analyzer class. But in the trace-resolving 
 * state, it does not have to handle the system call and vdso instructions (all of them do not 
 * exist in this state), moreover it does not have to log the executed instructions; so its 
 * semantics is much more simple.
 * 
 * @param instruction_address address of the instrumented instruction
 * @return void
 */
void resolver::generic_instruction_callback(ADDRINT instruction_address)
{
  if (instruction_at_execorder[current_execorder]->address == instruction_address)
  {
    // better performance because of branch prediction ?!!
    current_execorder++;
  }
  else 
  {
    BOOST_LOG_TRIVIAL(fatal) 
      << boost::format("in trace resolving state: meet a wrong instruction at %s after %d instruction executed.") 
          % utils::addrint2hexstring(instruction_address) % current_execorder;
    PIN_ExitApplication(current_resolving_state);
  }
  
  return;
}


/**
 * @brief callback for a conditional branch.
 * 
 * @param is_branch_taken the branch will be taken or not
 * @return void
 */
void resolver::cbranch_instruction_callback(bool is_branch_taken)
{
  // verify if the current examined instruction is branch
  if (instruction_at_execorder[current_execorder]->is_cbranch) 
  {
    // yes, then verify if the branch is needed to resolve
    if (!cbranch_at_execorder[current_execorder]->is_resolved && 
        !cbranch_at_execorder[current_execorder]->is_bypassed) 
    {
      // verify if the local re-execution number reaches its bound value
      if (local_reexec_number < max_local_reexec_number) 
      {
        //
      }
      else 
      {
        //
      }
    }
  }
  else 
  {
    BOOST_LOG_TRIVIAL(fatal) 
      << boost::format("in trace resolving state: meet a wrong branch at execution order %d") 
          % current_execorder;
  }
  return;
}


/**
 * @brief callback for an indirect branch or call, it exists only in the trace-resolving state 
 * because the re-execution trace must be kept to not go to a different target (than one in the 
 * execution trace logged in the trace-analyzing state).
 * 
 * @param instruction_address address of the instrumented instruction
 * @return void
 */
void resolver::indirectBrOrCall_instruction_callback(ADDRINT target_address)
{
  // in x86-64 architecture, an indirect branch (or call) instruction is always unconditional so 
  // the target address must be the next executed instruction, let's verify that
  if (instruction_at_execorder[current_execorder + 1]->address != target_address) 
  {
    switch (current_resolving_state)
    {
      case execution_with_orig_input:
        BOOST_LOG_TRIVIAL(fatal) 
          << boost::format("indirect branch at %d will take new target in executing with the original input.")
              % current_execorder;
        PIN_ExitApplication(current_resolving_state);
        break;
        
      case execution_with_modif_input:
        fast_execution::move_backward_and_modify_input(focal_checkpoint_execorder);
        break;
        
      default:
        BOOST_LOG_TRIVIAL(fatal)
          << boost::format("trace resolver falls into a unknown running state %d")
              % current_resolving_state;
        PIN_ExitApplication(current_resolving_state);
        break;
    }
  }
  
  return;
}

  
} // end of instrumentation namespace
