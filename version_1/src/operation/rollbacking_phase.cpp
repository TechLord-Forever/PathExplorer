#include "tainting_phase.h"
#include "../common.h"
#include "../util/stuffs.h"

#include <cstdlib>
#include <limits>
#include <algorithm>
#include <functional>

/*================================================================================================*/

typedef enum
{
  randomized = 0,
  sequential = 1
}                               input_generation_mode;

static ptr_cond_direct_ins_t    active_cfi;
static ptr_checkpoint_t         active_checkpoint;
static ptr_checkpoint_t         first_checkpoint;
static UINT32                   max_rollback_num;
static UINT32                   used_rollback_num;
static UINT32                   tainted_trace_length;
static addrint_set_t            active_modified_addrs;
static addrint_value_map_t      active_modified_addrs_values;
//static addrint_value_map_t      input_on_active_modified_addrs;
//static ptr_uint8_t              fresh_input;
static ptr_uint8_t              tainting_input;
static input_generation_mode    gen_mode;

UINT8                           byte_testing_value;
UINT16                          word_testing_value;
UINT32                          dword_testing_value;
std::function<addrint_value_map_t(const addrint_value_map_t&)>  generate_testing_input;


/*================================================================================================*/

namespace rollbacking
{
/**
 * @brief randomized_generator
 */
static auto randomized_generator (const addrint_value_map_t& input_map) -> addrint_value_map_t
{
  addrint_value_map_t output_map;

//  typedef decltype(active_modified_addrs_values) addrs_values_t;
  std::for_each(/*active_modified_addrs_values*/input_map.begin(), /*active_modified_addrs_values*/input_map.end(),
                [&](/*addrs_values_t*/addrint_value_map_t::const_reference addr_value)
  {
//    addr_value.second = std::rand() % std::numeric_limits<UINT8>::max();
    output_map[addr_value.first] = std::rand() % std::numeric_limits<UINT8>::max();
  });
  return output_map;
}


/**
 * the template function is conherent only where sizeof(T) = active_modified_addrs_values.size()
 */
template <typename T>
static auto generic_sequential_generator (const addrint_value_map_t& input_map) -> addrint_value_map_t
{
  static T generic_testing_value = 0;
  addrint_value_map_t output_map;

  // because sizeof(T) = active_modified_addrs_values.size(), all elements of
  // active_modified_addrs_values will be updated
  auto addr_value = /*active_modified_addrs_values*/input_map.begin();
  for (auto idx = 0; idx < sizeof(T); ++idx)
  {
//    addr_value->second = (generic_testing_value >> (idx * 8)) & 0xFF;
    output_map[addr_value->first] = (generic_testing_value >> (idx * 8)) & 0xFF;
    addr_value = std::next(addr_value);
  }
  generic_testing_value++;
  return output_map;
}


/**
 * the template function is conherent only where sizeof(T) = active_modified_addrs_values.size()
 */
template <typename T>
static auto generic_randomized_generator (const addrint_value_map_t& input_map) -> addrint_value_map_t
{
  static T generic_testing_value = 0;
  addrint_value_map_t output_map;

  auto addr_value = /*active_modified_addrs_values*/input_map.begin();
  for (auto idx = 0; idx < sizeof(T); ++idx)
  {
//    addr_value->second = (generic_testing_value >> (idx * 8)) & 0xFF;
    output_map[addr_value->first] = (generic_testing_value >> (idx * 8)) & 0xFF;
    addr_value = std::next(addr_value);
  }
  generic_testing_value = std::rand() % std::numeric_limits<T>::max();
  return output_map;
}


/**
 * @brief initialize_values_at_active_modified_addrs
 */
static auto initialize_values_at_active_modified_addrs () -> void
{
  active_modified_addrs_values.clear();
  typedef decltype(active_modified_addrs) addrs_t;
  std::for_each(active_modified_addrs.begin(), active_modified_addrs.end(),
                [&](addrs_t::const_reference addr)
  {
    active_modified_addrs_values[addr] = 0;
  });

  switch (active_modified_addrs_values.size())
  {
  case 1:
    max_rollback_num = std::numeric_limits<UINT8>::max();
    gen_mode = sequential;
    generate_testing_input = generic_sequential_generator<UINT8>;
    break;

  case 2:
    max_rollback_num = std::numeric_limits<UINT16>::max();
    gen_mode = sequential;
    generate_testing_input = generic_sequential_generator<UINT16>;
    break;

  case 4:
    max_rollback_num = max_local_rollback_knob.Value();
    gen_mode = randomized;
//    generate_testing_values = generic_sequential_generator<UINT32>;
    generate_testing_input = generic_randomized_generator<UINT32>;
    break;

  default:
    max_rollback_num = max_local_rollback_knob.Value();
    gen_mode = randomized; generate_testing_input = randomized_generator;
    break;
  }

  used_rollback_num = 0;
  return;
}


//static inline auto generate_testing_values () -> void
//{
//  /*addrint_value_map_t::iterator*/auto addr_iter = active_modified_addrs_values.begin();
//  for (; addr_iter != active_modified_addrs_values.end(); ++addr_iter)
//  {
//    switch (gen_mode)
//    {
//    case randomized:
//      addr_iter->second = rand() % std::numeric_limits<UINT8>::max(); break;
//    case sequential:
//      addr_iter->second++; break;
//    default:
//      break;
//    }
//  }
//  return;
//}


static inline void rollback()
{
  // verify if the number of used rollbacks has reached its bound
  if (used_rollback_num < /*max_rollback_num - 1*/max_rollback_num)
  {
    // not reached yet, then just rollback again with a new value of the input
    active_cfi->used_rollback_num++; used_rollback_num++;
    active_modified_addrs_values = generate_testing_input(active_modified_addrs_values);
    rollback_with_modified_input(active_checkpoint, current_exec_order,
                                 active_modified_addrs_values);
  }
  else
  {
    // already reached, then restore the orginal value of the input
    if (used_rollback_num == /*max_rollback_num - 1*/max_rollback_num)
    {
      active_cfi->used_rollback_num++; used_rollback_num++;
      rollback_with_original_input(active_checkpoint, current_exec_order);
    }
#if !defined(NDEBUG)
    else
    {
      // exceeds
      tfm::format(log_file, "fatal: the number of used rollback (%d) exceeds its bound value (%d)\n",
                  used_rollback_num, max_rollback_num);
      PIN_ExitApplication(1);
    }
#endif
  }
  return;
}


/**
 * @brief get the next active checkpoint and the active modified addresses
 */
static auto next_checkpoint_and_addrs (ptr_checkpoint_t input_checkpoint,
                                       ptr_cond_direct_ins_t input_cfi) -> checkpoint_addrs_pair_t
{
  checkpoint_addrs_pair_t result;

//  std::vector<checkpoint_with_modified_addrs>::iterator chkpnt_iter, nx_chkpnt_iter;
  // verify if there exist an enabled active checkpoint
  if (/*active_checkpoint*/input_checkpoint)
  {
    // exist, then find the next checkpoint in the checkpoint list of the current active CFI
//    auto nx_chkpnt_iter = active_cfi->checkpoints.begin();
//    auto chkpnt_iter = nx_chkpnt_iter;
//    while (++nx_chkpnt_iter != active_cfi->checkpoints.end())
//    {
//      if (chkpnt_iter->first->exec_order == active_checkpoint->exec_order)
//      {
//        active_checkpoint = nx_chkpnt_iter->first; active_modified_addrs = nx_chkpnt_iter->second;
//        break;
//      }
//      chkpnt_iter = nx_chkpnt_iter;
//    }

//    if (nx_chkpnt_iter == active_cfi->checkpoints.end())
//    {
//      active_checkpoint.reset(); active_modified_addrs.clear();
//    }

//    typedef decltype(active_cfi->affecting_checkpoint_addrs_pairs) checkpoints_t;
    auto prev_elem = /*active_cfi*/input_cfi->affecting_checkpoint_addrs_pairs.front();
//    if (!std::any_of(std::next(active_cfi->affecting_checkpoint_addrs_pairs.begin()),
//                     active_cfi->affecting_checkpoint_addrs_pairs.end(),
//                     [&](checkpoint_addrs_pairs_t::reference checkpoint_addrs_elem) -> bool
//    {
//      if (prev_elem.first->exec_order == active_checkpoint->exec_order)
//      {
////        active_checkpoint = checkpoint_addrs_elem.first;
////        active_modified_addrs = checkpoint_addrs_elem.second;
//        result = checkpoint_addrs_elem;
//        return true;
//      }
//      else return false;
//    }))
//    {
//      active_checkpoint.reset(); active_modified_addrs.clear();
//    }
    std::any_of(std::next(input_cfi->affecting_checkpoint_addrs_pairs.begin()),
                input_cfi->affecting_checkpoint_addrs_pairs.end(),
                [&](checkpoint_addrs_pairs_t::reference checkpoint_addrs_elem) -> bool
    {
      if (prev_elem.first->exec_order == input_checkpoint->exec_order)
      {
//        active_checkpoint = checkpoint_addrs_elem.first;
//        active_modified_addrs = checkpoint_addrs_elem.second;
        result = checkpoint_addrs_elem;
        return true;
      }
      else return false;
    });

  }
  else
  {
    // doest not exist, then the active checkpoint is assigned as the first checkpoint of the
    // current active CFI
//    active_checkpoint = active_cfi->affecting_checkpoint_addrs_pairs[0].first;
//    active_modified_addrs = active_cfi->affecting_checkpoint_addrs_pairs[0].second;
    result = input_cfi->affecting_checkpoint_addrs_pairs[0];
  }

//  if (active_checkpoint) initialize_values_at_active_modified_addrs();

  return result;
}


/**
 * @brief calculate an input for the new tainting phase
 */
static auto calculate_tainting_fresh_input(const ptr_uint8_t selected_input,
                                           const addrint_value_map_t& modified_addrs_with_values) -> void
{
  // make a copy in fresh input of the selected input
//  tainting_input.reset(new UINT8[received_msg_size]);
//  std::copy(selected_input.get(), selected_input.get() + received_msg_size, tainting_input.get());
  std::copy(selected_input.get(), selected_input.get() + received_msg_size, fresh_input.get());

  // update this copy with new values at modified addresses
//  auto addr_iter = modified_addrs_with_values.begin();
//  for (; addr_iter != modified_addrs_with_values.end(); ++addr_iter)
//  {
//    fresh_input.get()[addr_iter->first - received_msg_addr] = addr_iter->second;
//  }

  std::for_each(modified_addrs_with_values.begin(), modified_addrs_with_values.end(),
                [&](addrint_value_map_t::const_reference addr_value)
  {
    fresh_input.get()[addr_value.first - received_msg_addr] = addr_value.second;
  });

  return;
}


/**
 * @brief prepare_new_tainting_phase
 */
static auto prepare_new_tainting_phase () -> void
{
  show_exploring_progress();

//  auto cfi_iter = detected_input_dep_cfis.begin();
//  // verify if there exists a resolved but unexplored CFI
//  for (; cfi_iter != detected_input_dep_cfis.end(); ++cfi_iter)
//  {
//    if ((*cfi_iter)->is_resolved && !(*cfi_iter)->is_explored) break;
//  }
//  if (cfi_iter != detected_input_dep_cfis.end())
//  {
//    // exists, then verify if the number of used rollback time has exceeded its bounded value
//    if (total_rollback_times >= max_total_rollback_times)
//    {
//      // exceeded, then stop exploring
//#if !defined(NDEBUG)
//      log_file << "stop exploring, number of used rollbacks exceeds its bounded value\n";
//#endif
//      PIN_ExitApplication(process_id);
//    }
//    else
//    {
//      // not exceeded yet, then set the CFI as explored
//      exploring_cfi = *cfi_iter; exploring_cfi->is_explored = true;
//      // calculate a new input for the next tainting phase
//      calculate_tainting_fresh_input(exploring_cfi->fresh_input,
//                                     exploring_cfi->second_input_projections[0]);

//      // initialize new tainting phase
//      current_running_phase = tainting_phase; tainting::initialize();

//#if !defined(NDEBUG)
//      tfm::format(log_file, "%s\nexplore the CFI %s at %d, start tainting\n",
//                  "=================================================================================",
//                  exploring_cfi->disassembled_name, exploring_cfi->exec_order);
//      log_file.flush();
//#endif

//      // rollback to the first checkpoint with the new input
//      PIN_RemoveInstrumentation();
//      rollback_with_new_input(first_checkpoint, current_exec_order, received_msg_addr,
//                              received_msg_size, fresh_input.get());
//    }
//  }
//  else
//  {
//    // does not exist, namely all CFI are explored
//#if !defined(NDEBUG)
//    log_file << "stop exploring, all CFI have been explored\n";
//#endif
//    PIN_ExitApplication(process_id);
//  }

  // verify if the number of used rollback time has exceeded its bounded value
  if (total_rollback_times >= max_total_rollback_times)
  {
    // exceeded, then stop exploring
#if !defined(NDEBUG)
    log_file << "stop exploring, number of used rollbacks exceeds its bounded value\n";
#endif
    PIN_ExitApplication(process_id);
  }
  else
  {
    // not exceeded yet, then verify if there exists a resolved but unexplored CFI
    typedef decltype(detected_input_dep_cfis) cfis_t;
    if (std::any_of(detected_input_dep_cfis.begin(), detected_input_dep_cfis.end(),
                    [&](cfis_t::reference cfi_elem) -> bool
    {
      if (cfi_elem->is_resolved && !cfi_elem->is_explored)
      {
        exploring_cfi = cfi_elem;
        // a unexplored CFI exists, then set it as explored
        exploring_cfi->is_explored = true;
        // calculate a new input for the next tainting phase
        calculate_tainting_fresh_input(exploring_cfi->fresh_input,
                                       exploring_cfi->second_input_projections[0]);

        // initialize new tainting phase
        current_running_phase = tainting_phase; tainting::initialize();
        return true;
      }
      else return false;
    }))
    {
      // exists, then explore this CFI
#if !defined(NDEBUG)
      tfm::format(log_file, "%s\nexplore the CFI %s at %d, start tainting\n",
                  "=================================================================================",
                  exploring_cfi->disassembled_name, exploring_cfi->exec_order);
//      log_file.flush();
#endif

      // rollback to the first checkpoint with the new input
      PIN_RemoveInstrumentation();
      rollback_with_new_input(first_checkpoint, current_exec_order, received_msg_addr,
                              received_msg_size, fresh_input.get());
    }
    else
    {
      // does not exist, namely all CFI are explored
#if !defined(NDEBUG)
      log_file << "stop exploring, all CFI have been explored\n";
#endif
      PIN_ExitApplication(process_id);
    }
  }

  return;
}


/**
 * @brief This function aims to give a generic approach for solving control-flow instructions. The
 * main idea is to verify if the re-executed trace (i.e. rollback with a modified input) is the
 * same as the original trace: if there exists an instruction that does not occur in the original
 * trace then that must be resulted from a control-flow instruction which has changed the control
 * flow, so the new instruction will not be executed and we take a rollback.
 * Its semantics is quite sophisticated because there are several conditions to check.
 *
 * @param ins_addr: the address of the current examined instruction.
 * @return no return value.
 */
auto generic_instruction (ADDRINT ins_addr, THREADID thread_id) -> VOID
{
  if (thread_id == traced_thread_id)
  {
    // verify if the execution order of the instruction exceeds the last CFI
    if (current_exec_order >= tainted_trace_length)
    {
      // exceeds, namely the rollbacking phase should stop:

      // first, save the current execution path
      current_exec_path = std::make_shared<execution_path>(ins_at_order, current_path_code);
      explored_exec_paths.push_back(current_exec_path);

      // second, prepare tainting a new path
      prepare_new_tainting_phase();
    }
    else
    {
      current_exec_order++;

      // verify if the executed instruction is in the original trace
      if (ins_at_order[current_exec_order]->address != ins_addr)
      {
        // is not in, then verify if the current control-flow instruction (abbr. CFI) is activated
        if (active_cfi)
        {
          // activated, that means the rollback from some checkpoint of this CFI will change the
          // control-flow, then verify if the CFI is the just previous executed instruction
          if (active_cfi->exec_order + 1 == current_exec_order)
          {
#if !defined(NDEBUG)
            if (!active_cfi->is_resolved)
            {
              tfm::format(log_file, "the CFI %s at %d is resolved\n", active_cfi->disassembled_name,
                          active_cfi->exec_order);
            }
#endif
            // it is, then it will be marked as resolved
            active_cfi->is_resolved = true;

            // push an input projection into the corresponding input list of the active CFI
            active_cfi->second_input_projections.push_back(active_modified_addrs_values);
          }
          else
          {
            // it is not, that means some other CFI (between the current CFI and the checkpoint) will
            // change the control flow
          }
          // in both cases, we need rollback
          rollback();
        }
#if !defined(NDEBUG)
        else
        {
          // not activated, then some errors have occurred
          tfm::format(log_file, "fatal: there is no active CFI but the original trace changes (%s %d)\n",
                      addrint_to_hexstring(ins_addr), current_exec_order);
          PIN_ExitApplication(1);
        }
#endif
      }
      else
      {
        // the executed instruction is in the original trace, then verify if there exists active CFI
        // and the executed instruction has exceeded this CFI
        if (active_cfi && (current_exec_order > active_cfi->exec_order))
        {
          // yes, then push an input projection into the corresponding input list of the active CFI
          active_cfi->first_input_projections.push_back(active_modified_addrs_values);
          // and rollback
          rollback();
        }
      }
    }
  }

  return;
}


/**
 * @brief control_flow_instruction
 */
auto control_flow_instruction(ADDRINT ins_addr, THREADID thread_id) -> VOID
{
  if (thread_id == traced_thread_id)
  {
    // consider only CFIs that are beyond the exploring CFI
    if (!exploring_cfi || (exploring_cfi && (exploring_cfi->exec_order < current_exec_order)))
    {
      // verify if there exists already an active CFI in resolving
      if (active_cfi)
      {
        // yes, then verify if the current execution order reaches the active CFI
        if (current_exec_order < active_cfi->exec_order)
        {
          // not reached yes, then do nothing
        }
        else
        {
          // reached, then normally the current CFI should be the active one
          if (current_exec_order == active_cfi->exec_order)
          {
            // verify if its current checkpoint is in the last rollback try
            if (used_rollback_num == max_rollback_num + 1)
            {
              // yes, then verify if there exists another checkpoint, note that used_rollback_num
              // will be reset to zero here
              std::tie(active_checkpoint, active_modified_addrs) =
                  next_checkpoint_and_addrs(active_checkpoint, active_cfi);
//              if (active_checkpoint) initialize_values_at_active_modified_addrs();
              if (active_checkpoint)
              {
#if !defined(NDEBUG)
                tfm::format(log_file, "the cfi at %d is still actived, its next checkpoint is at %d, modified addresses size %d\n",
                            active_cfi->exec_order, active_checkpoint->exec_order,
                            active_modified_addrs.size());
#endif
                // exists, then rollback to the new active checkpoint
                initialize_values_at_active_modified_addrs();
                rollback();
              }
              else
              {
                // the next checkpoint does not exist, all of its reserved tests have been used
                active_cfi->is_bypassed = !active_cfi->is_resolved;
                active_cfi->is_singular = active_cfi->is_bypassed &&
                    (active_cfi->affecting_checkpoint_addrs_pairs.size() == 1) && (gen_mode == sequential);
                total_rollback_times += active_cfi->used_rollback_num;

//                if (active_cfi->is_bypassed && (active_cfi->checkpoints.size() == 1) &&
//                    (gen_mode == sequential)) active_cfi->is_singular = true;
#if !defined(NDEBUG)
                if (active_cfi->is_bypassed)
                {
                  tfm::format(log_file, "the CFI %s at %d is bypassed (singularity: %s)\n",
                              active_cfi->disassembled_name, active_cfi->exec_order,
                              active_cfi->is_singular);
                }
#endif
                active_cfi.reset(); used_rollback_num = 0;
              }
            }
          }
        }
      }
      else
      {
        // there is no CFI in resolving, then verify if the current CFI depends on the input
        auto current_cfi =
            std::static_pointer_cast<cond_direct_instruction>(ins_at_order[current_exec_order]);
        if (!current_cfi->input_dep_addrs.empty())
        {
          // yes, then set it as the active CFI
          active_cfi = current_cfi;
          std::tie(active_checkpoint, active_modified_addrs) =
              next_checkpoint_and_addrs(active_checkpoint, active_cfi);
#if !defined(NDEBUG)
          tfm::format(log_file, "the CFI %s at %d is activated, its first checkpoint is at %d, modified addresses size %d\n",
                      active_cfi->disassembled_name, active_cfi->exec_order,
                      active_checkpoint->exec_order, active_modified_addrs.size());
#endif
          // make a copy of the fresh input
//          active_cfi->fresh_input.reset(new UINT8[received_msg_size]);
//          std::copy(fresh_input.get(), fresh_input.get() + received_msg_size,
//                    active_cfi->fresh_input.get());

          // push an input projection into the corresponding input list of the active CFI
          initialize_values_at_active_modified_addrs();
          active_cfi->first_input_projections.push_back(active_modified_addrs_values);
//          if (active_cfi->first_input_projections.empty())
//          {
//            project_input_on_active_modified_addrs();
//            active_cfi->first_input_projections.push_back(input_on_active_modified_addrs);
//          }

          // and rollback to resolve this new active CFI
          rollback();
        }
      }
    }
  }

  return;
}


/**
 * @brief tracking instructions that write memory
 */
auto mem_write_instruction(ADDRINT ins_addr, ADDRINT mem_addr, UINT32 mem_length,
                           THREADID thread_id) -> VOID
{
  if (thread_id == traced_thread_id)
  {
    // verify if the active checkpoint is enabled
    if (active_checkpoint)
    {
      // yes, namely we are now in some "reverse" execution from it to the active CFI, so this
      // checkpoint needs to track memory write instructions
      active_checkpoint->mem_write_tracking(mem_addr, mem_length);
    }
    else
    {
      // no, namely we are now in normal "forward" execution, so all checkpoint until the current
      // execution order need to track memory write instructions
//      auto chkpnt_iter = saved_checkpoints.begin();
//      for (; chkpnt_iter != saved_checkpoints.end(); ++chkpnt_iter)
//      {
//        if ((*chkpnt_iter)->exec_order <= current_exec_order)
//        {
//          (*chkpnt_iter)->mem_write_tracking(mem_addr, mem_length);
//        }
//        else
//        {
//          break;
//        }
//      }

      typedef decltype(saved_checkpoints) checkpoints_t;
      std::any_of(saved_checkpoints.begin(), saved_checkpoints.end(),
                  [&](checkpoints_t::reference checkpoint_elem) -> bool
      {
        if (checkpoint_elem->exec_order <= current_exec_order)
        {
          checkpoint_elem->mem_write_tracking(mem_addr, mem_length); return false;
        }
        else return true;
      });
    }
  }
  return;
}


/**
 * @brief initialize_rollbacking_phase
 */
auto initialize(UINT32 trace_length_limit) -> void
{
  // reinitialize some local variables
  active_cfi.reset(); active_checkpoint.reset();
  first_checkpoint = saved_checkpoints[0];
  active_modified_addrs.clear();
  tainted_trace_length = trace_length_limit; used_rollback_num = 0;
  max_rollback_num = max_local_rollback_knob.Value();
  gen_mode = randomized;
  return;
}

} // end of rollbacking namespace
