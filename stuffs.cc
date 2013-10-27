// #include <boost/cstdint.hpp>                                                    // for portable code

#include <map>
#include <set>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <limits>

#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/graph/graphviz.hpp>
#include <boost/graph/lookup_edge.hpp>


#include "instruction.h"
#include "variable.h"
#include "checkpoint.h"
#include "branch.h"

#include <pin.H>


/* ------------------------------------------------------------------------------------------------------------------ */
/*                                                       global variables                                             */
/* ------------------------------------------------------------------------------------------------------------------ */
extern std::map< ADDRINT, 
                 instruction >      addr_ins_static_map;
                 
extern vdep_graph                   dta_graph;
extern map_ins_io                   dta_inss_io;

extern std::vector<ptr_checkpoint>  saved_ptr_checkpoints;

extern ptr_branch                   active_ptr_branch;
extern ptr_branch                   exploring_ptr_branch;

extern std::vector<ptr_branch>      input_dep_ptr_branches;
extern std::vector<ptr_branch>      input_indep_ptr_branches;
extern std::vector<ptr_branch>      resolved_ptr_branches;
extern std::vector<ptr_branch>      found_new_ptr_branches;
extern std::vector<ptr_branch>      tainted_ptr_branches;

extern std::vector<ADDRINT>         explored_trace;

extern UINT32                       input_dep_branch_num;
// extern UINT32                       resolved_branch_num;

extern KNOB<UINT32>                 max_trace_length;
extern KNOB<BOOL>                   print_debug_text;

extern std::ofstream                tainting_log_file;


/* ------------------------------------------------------------------------------------------------------------------ */
/*                                                         implementation                                             */
/* ------------------------------------------------------------------------------------------------------------------ */

std::string remove_leading_zeros(std::string input)
{
  std::string::iterator str_iter = input.begin();
  std::string output("0x");
  
  while (
          (str_iter != input.end()) && 
          ((*str_iter == '0') || (*str_iter == 'x'))
        ) 
  {
    ++str_iter;
  }
  
  while (str_iter != input.end()) 
  {
    output.push_back(*str_iter);
    ++str_iter;
  }
  
  return output;
}

/*====================================================================================================================*/

void assign_image_name(ADDRINT ins_addr, std::string& img_name)
{
  for (IMG img = APP_ImgHead(); IMG_Valid(img); img = IMG_Next(img))
  {
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec))
    {
      if ((ins_addr >= SEC_Address(sec)) && (ins_addr < SEC_Address(sec) + SEC_Size(sec)))
      {
        img_name = IMG_Name(img);
        return;
      }
    }
  }
  
  return;
}

/*====================================================================================================================*/

void copy_instruction_mem_access(ADDRINT ins_addr, ADDRINT mem_addr, ADDRINT mem_size, UINT8 access_type)
{
  UINT8 single_byte;
  UINT32 idx;
  
  switch (access_type) 
  {
    case 0:
      std::map<ADDRINT, UINT8>().swap(addr_ins_static_map[ins_addr].mem_read_map);
      for (idx = 0; idx < mem_size; ++idx) 
      {
        PIN_SafeCopy(&single_byte, reinterpret_cast<UINT8*>(mem_addr + idx), 1);
        addr_ins_static_map[ins_addr].mem_read_map[mem_addr + idx] = single_byte;
      }
      break;
      
    case 1:
      std::map<ADDRINT, UINT8>().swap(addr_ins_static_map[ins_addr].mem_written_map);
      for (idx = 0; idx < mem_size; ++idx) 
      {
        PIN_SafeCopy(&single_byte, reinterpret_cast<UINT8*>(mem_addr + idx), 1);
        addr_ins_static_map[ins_addr].mem_written_map[mem_addr + idx] = single_byte;
      }
      break;
      
    default:
      break;
  }
  
  return;
}

/*====================================================================================================================*/

void journal_buffer(const std::string& filename, UINT8* buffer_addr, UINT32 buffer_size)
{
  std::ofstream file(filename.c_str(), 
                     std::ofstream::binary | std::ofstream::out | std::ofstream::trunc);
  
  std::copy(buffer_addr, buffer_addr + buffer_size, std::ostreambuf_iterator<char>(file));
  file.close();
  
  return;
}

/*====================================================================================================================*/

void journal_result(UINT32 rollback_times, UINT32 trace_size, UINT32 total_br_num, UINT32 resolved_br_num)
{
  std::string file_name = "result." + boost::lexical_cast<std::string>(rollback_times);
  std::fstream result_file(file_name.c_str(), std::ofstream::out | std::ofstream::app);

  if (result_file.tellp() == 0)
  {
    result_file << "depth" << "\t" << "total" << "\t" << "solved" << "\t" << "percent" << "\n"; 
  }
  
  double_t resolved_percentage = (static_cast<double_t>(resolved_br_num) / static_cast<double_t>(total_br_num)) * 100.00;
  
  result_file << trace_size << "\t" << total_br_num << "\t" << resolved_br_num << "\t" << resolved_percentage << "\n";
  
  result_file.close();
  return;
}

/*====================================================================================================================*/

void journal_result_total(UINT32 max_rollback_times, UINT32 used_rollback_times, 
                          UINT32 trace_size, UINT32 total_br_num, UINT32 resolved_br_num)
{
  std::string filename = "total_result";
  std::fstream result_file(filename.c_str(), std::ofstream::out | std::ofstream::app);
  
  if (result_file.tellp() == 0)
  {
    result_file << "max_rollback" << "\t" << "used_rollback" << "\t" 
                << "trace_depth" << "\t" << "total_branch" << "\t" << "solved_branch" << "\n"; 
  }
  
  result_file << max_rollback_times << "\t" << used_rollback_times << "\t" 
              << trace_size << "\t" << total_br_num << "\t" << resolved_br_num << "\n";
              
  result_file.close();
  return;
}

/*====================================================================================================================*/

void journal_static_trace(const std::string& filename)
{
  std::ofstream out_file(filename.c_str(), std::ofstream::out | std::ofstream::trunc);
//   UINT32 addr_dist = 0;
  
  std::map<ADDRINT, instruction>::iterator addr_ins_iter = addr_ins_static_map.begin();
  for (; addr_ins_iter != addr_ins_static_map.end(); ++addr_ins_iter)
  {
//     addr_dist++;
    out_file << boost::format("%-20s %-45s (R: %-i, W: %-i) %-50s\n") 
                % StringFromAddrint(addr_ins_iter->first) % addr_ins_iter->second.disass
                % addr_ins_iter->second.mem_read_size % addr_ins_iter->second.mem_written_size
                % addr_ins_iter->second.img;
  }
  out_file.close();
  
  std::string filename_wf = filename + "_wf";
  out_file.open(filename_wf.c_str(), std::ofstream::out | std::ofstream::trunc);
  
  addr_ins_iter = addr_ins_static_map.begin();
  for (; addr_ins_iter != addr_ins_static_map.end(); ++addr_ins_iter)
  {
    out_file << boost::format("%-s\t%-s\n") % StringFromAddrint(addr_ins_iter->first) % addr_ins_iter->second.disass;
  }
  out_file.close();
  
  return;
}

/*====================================================================================================================*/

void journal_explored_trace(const std::string& filename, std::vector< ADDRINT >& trace)
{
  std::ofstream out_file(filename.c_str(), std::ofstream::out | std::ofstream::trunc);
  
  std::vector<ADDRINT>::iterator trace_iter = trace.begin();
  for (; trace_iter != trace.end(); ++trace_iter)
  {
    out_file << boost::format("%-20s %-45s") 
                % StringFromAddrint(*trace_iter) % addr_ins_static_map[*trace_iter].disass;
//                  % addr_ins_static_map[*trace_iter].mem_read_size % addr_ins_static_map[*trace_iter].mem_written_size;
//                  % addr_ins_static_map[*trace_iter].img;
                
    std::map<ADDRINT, UINT8>::iterator mem_access_iter;
    out_file << boost::format("(R : %i") % addr_ins_static_map[*trace_iter].mem_read_size;
    for (mem_access_iter = addr_ins_static_map[*trace_iter].mem_read_map.begin(); 
         mem_access_iter != addr_ins_static_map[*trace_iter].mem_read_map.end(); ++mem_access_iter) 
    {
      out_file << boost::format(" %s(%i)") 
                  % StringFromAddrint(mem_access_iter->first) % static_cast<UINT32>(mem_access_iter->second);
    }
    
    out_file << boost::format(" W: %i") % addr_ins_static_map[*trace_iter].mem_written_size;
    for (mem_access_iter = addr_ins_static_map[*trace_iter].mem_written_map.begin(); 
         mem_access_iter != addr_ins_static_map[*trace_iter].mem_written_map.end(); ++mem_access_iter)
    {
      out_file << boost::format(" %s(%i)") 
                  % StringFromAddrint(mem_access_iter->first) % static_cast<UINT32>(mem_access_iter->second);
    }
    
    out_file << ")\n";
  }
  out_file.close();
  
//   std::string filename_wf = filename + "_wf";
//   out_file.open(filename_wf.c_str(), std::ofstream::out | std::ofstream::trunc);
//   
//   trace_iter = trace.begin();
//   for (; trace_iter != trace.end(); ++trace_iter)
//   {
//     out_file << boost::format("%-s\t%-s\n") % StringFromAddrint(*trace_iter) % addr_ins_static_map[*trace_iter].disass;
//   }
//   out_file.close();
  
  return;
}

/*====================================================================================================================*/

void journal_tainting_graph(const std::string& filename)
{
  std::ofstream out_file(filename.c_str(), std::ofstream::out | std::ofstream::trunc);
  boost::write_graphviz(out_file, dta_graph, vertex_label_writer(dta_graph), edge_label_writer(dta_graph));
  out_file.close();
  
  return;
}

/*====================================================================================================================*/

void store_input(ptr_branch& ptr_br, bool br_taken) 
{
  boost::shared_ptr<UINT8> new_input(new UINT8 [received_msg_size]);
  PIN_SafeCopy(new_input.get(), reinterpret_cast<UINT8*>(received_msg_addr), received_msg_size);  
  ptr_br->inputs[br_taken].push_back(new_input);
  
  return;   
}

/*====================================================================================================================*/

void print_debug_message_received() 
{
  if (print_debug_text)
  {
    std::cout << "\033[33mThe first message saved at " << remove_leading_zeros(StringFromAddrint(received_msg_addr))
              << " with size " << received_msg_size << ".\033[0m\n";
    std::cout << "-------------------------------------------------------------------------------------------------\n";        
    std::cout << "\033[33mStart tainting phase with maximum trace size " 
              << max_trace_length.Value() << ".\033[0m\n";
  }
  return;
}

/*====================================================================================================================*/

void print_debug_rollbacking_stop(ptr_branch& unexplored_ptr_branch)
{
  if (print_debug_text)
  {
    UINT32 resolved_branch_num = resolved_ptr_branches.size();
    UINT32 input_dep_branch_num = found_new_ptr_branches.size();
    
    std::vector<ptr_branch>::iterator ptr_branch_iter = tainted_ptr_branches.begin();
    for (; ptr_branch_iter != tainted_ptr_branches.end(); ++ptr_branch_iter) 
    {
      if (!(*ptr_branch_iter)->dep_input_addrs.empty()) 
      {
        input_dep_branch_num++;
      }
    }
    
    
    std::cerr << "\033[33mRollbacking phase stopped: " << resolved_branch_num << "/" 
              << input_dep_branch_num << " branches resolved.\033[0m\n";
    std::cerr << "\033[35m-------------------------------------------------------------------------------------------------\n"
              << "Start tainting phase exploring branch at " << unexplored_ptr_branch->trace.size() << ".\033[0m\n";
  }
  return;
}

/*====================================================================================================================*/

void print_debug_met_again(ADDRINT ins_addr, ptr_branch &met_ptr_branch)
{
  if (print_debug_text)
  {
    std::cerr << boost::format("\033[36mMet again   %-5i %-20s %-35s (%-10i rollbacks)\033[0m\n") 
                  % explored_trace.size() % remove_leading_zeros(StringFromAddrint(ins_addr)) % addr_ins_static_map[ins_addr].disass 
                  % met_ptr_branch->chkpnt->rb_times;
  }
  return;
}

/*====================================================================================================================*/

void print_debug_failed_active_forward(ADDRINT ins_addr, ptr_branch& failed_ptr_branch)
{
  if (print_debug_text)
  {
    std::cerr << boost::format("\033[31mFailed      %-5i %-20s %-35s (active branch not met in forward)\033[0m\n") 
                  % explored_trace.size() % remove_leading_zeros(StringFromAddrint(ins_addr)) 
                  % addr_ins_static_map[ins_addr].disass;
  }
  return;
}

/*====================================================================================================================*/

void print_debug_resolving_rollback(ADDRINT ins_addr, ptr_branch& new_ptr_branch) 
{
  if (print_debug_text)
  {
    std::cerr << boost::format("Resolving   %-5i %-20s %-35s (rollback to %i)\n") 
                  % explored_trace.size() % remove_leading_zeros(StringFromAddrint(ins_addr)) 
                  % addr_ins_static_map[ins_addr].disass 
                  % active_ptr_branch->chkpnt->trace.size();                
  }
  return;
}

/*====================================================================================================================*/

void print_debug_succeed(ADDRINT ins_addr, ptr_branch& succeed_ptr_branch) 
{
  if (print_debug_text)
  {
    std::cout << boost::format("\033[32mSucceeded   %-5i %-20s %-35s (%i rollbacks)\033[0m\n") 
                  % explored_trace.size() % remove_leading_zeros(StringFromAddrint(ins_addr)) 
                  % addr_ins_static_map[ins_addr].disass 
                  % succeed_ptr_branch->chkpnt->rb_times;
  }
  return;
}

/*====================================================================================================================*/

void print_debug_resolving_failed(ADDRINT ins_addr, ptr_branch& failed_ptr_branch) 
{
  if (print_debug_text)
  {
    std::cout << boost::format("\033[31mFailed      %-5i %-20s %-35s (%i rollbacks)\033[0m\n") 
                  % failed_ptr_branch->trace.size() /*explored_trace.size()*/ 
                  % remove_leading_zeros(StringFromAddrint(ins_addr)) % addr_ins_static_map[ins_addr].disass 
                  % failed_ptr_branch->chkpnt->rb_times;
  }
  return;
}

/*====================================================================================================================*/

void print_debug_found_new(ADDRINT ins_addr, ptr_branch& found_ptr_branch) 
{
  if (print_debug_text)
  {
    std::cout << boost::format("\033[35mFound new   %-5i %-20s %-35s\033[0m\n") 
                  % explored_trace.size() % remove_leading_zeros(StringFromAddrint(ins_addr)) % addr_ins_static_map[ins_addr].disass;
  }
  return;
}

/*====================================================================================================================*/

void print_debug_lost_forward(ADDRINT ins_addr, ptr_branch& lost_ptr_branch) 
{
  if (print_debug_text) 
  {
    std::cerr << boost::format("\033[31mBranch  %-1i   %-5i %-20s %-35s (lost: new branch taken in forward)\033[0m\n") 
                  % !lost_ptr_branch->br_taken % explored_trace.size() 
                  % remove_leading_zeros(StringFromAddrint(ins_addr)) 
                  % addr_ins_static_map[ins_addr].disass;
  }
  return;
}

/*====================================================================================================================*/

void print_debug_unknown_branch(ADDRINT ins_addr, ptr_branch& unknown_ptr_branch)
{
  if (print_debug_text)
  {
    std::cerr << boost::format("\033[31mLost at     %-5i %-20s %-35s (unknown branch met)\033[0m\n") 
                  % explored_trace.size() % remove_leading_zeros(StringFromAddrint(ins_addr)) 
                  % addr_ins_static_map[ins_addr].disass;
  }
  return;
}

/*====================================================================================================================*/

void print_debug_start_rollbacking()
{
  if (print_debug_text) 
  {
    std::cout << "\033[33mTainting phase stopped at " << explored_trace.size() << ", " 
              << input_dep_ptr_branches.size() << " tainted branches, " 
              << input_indep_ptr_branches.size() << " untainted branches, " 
              << saved_ptr_checkpoints.size() << " checkpoints. "
              << "Start rollbacking phase.\033[0m\n";
              
    std::cout << boost::format("\033[36mRestore to  %-5i %-20s %-35s\033[0m\n") 
                 % saved_ptr_checkpoints[0]->trace.size() 
                 % remove_leading_zeros(StringFromAddrint(saved_ptr_checkpoints[0]->addr)) 
                 % addr_ins_static_map[saved_ptr_checkpoints[0]->addr].disass;
    
    std::string tainted_trace_filename("tainted_trace");
    if (exploring_ptr_branch) 
    {
      std::stringstream ss;
      ss << exploring_ptr_branch->trace.size();
      tainted_trace_filename = tainted_trace_filename + "_" + ss.str();
    }
    journal_explored_trace(tainted_trace_filename.c_str(), explored_trace);
  }
  
  return;
}

/*====================================================================================================================*/

inline std::string sregs_format(ADDRINT ins_addr) 
{
  std::set<REG>::iterator reg_iter;
    
  std::stringstream sstream_sregs;
  sstream_sregs << "sregs: ";
  for (reg_iter = boost::get<0>(dta_inss_io[ins_addr]).first.begin(); 
        reg_iter != boost::get<0>(dta_inss_io[ins_addr]).first.end(); ++reg_iter)
  {
    sstream_sregs << REG_StringShort(*reg_iter) << " ";
  }
  
  return sstream_sregs.str();
  
    
}

inline std::string dregs_format(ADDRINT ins_addr) 
{
  std::set<REG>::iterator reg_iter;
  
  std::stringstream sstream_dregs;
  sstream_dregs << "dregs: ";
  for (reg_iter = boost::get<0>(dta_inss_io[ins_addr]).second.begin(); 
        reg_iter != boost::get<0>(dta_inss_io[ins_addr]).second.end(); ++reg_iter) 
  {
    sstream_dregs << REG_StringShort(*reg_iter) << " ";
  }
  
  return sstream_dregs.str();
}

inline std::string mem_read_format(ADDRINT mem_read_addr, UINT32 mem_read_size) 
{
  std::stringstream sstream_smems;
  sstream_smems << "smems: [" 
                << remove_leading_zeros(StringFromAddrint(mem_read_addr)) << ", "
                << remove_leading_zeros(StringFromAddrint(mem_read_addr + mem_read_size - 1)) << "]";
                
  return sstream_smems.str();
}

inline std::string mem_written_format(ADDRINT mem_written_addr, UINT32 mem_written_size) 
{
  std::stringstream sstream_dmems;
  sstream_dmems << "dmems: [" 
                << remove_leading_zeros(StringFromAddrint(mem_written_addr)) << ", "
                << remove_leading_zeros(StringFromAddrint(mem_written_addr + mem_written_size - 1)) << "]";
                  
  return sstream_dmems.str();
}

/*====================================================================================================================*/

void print_debug_mem_written(ADDRINT ins_addr, ADDRINT mem_written_addr, UINT32 mem_written_size)
{
  if (print_debug_text) 
  {
    std::stringstream sstream_smems;
    sstream_smems << "smems: []"; 
    
    tainting_log_file << boost::format("\033[33m%-4i %-16s %-34s %-20s %-20s %-40s %-40s\033[0m\n")
                          % explored_trace.size() % remove_leading_zeros(StringFromAddrint(ins_addr)) 
                          % addr_ins_static_map[ins_addr].disass  
                          % sregs_format(ins_addr) % dregs_format(ins_addr) 
                          % sstream_smems.str() % mem_written_format(mem_written_addr, mem_written_size);
//                           % "reg->mem";
  }
  return;
}

/*====================================================================================================================*/

void print_debug_mem_read(ADDRINT ins_addr, ADDRINT mem_read_addr, UINT32 mem_read_size)
{
  if (print_debug_text) 
  { 
    std::stringstream sstream_dmems;
    sstream_dmems << "dmems: []"; 
                  
    tainting_log_file << boost::format("\033[0m%-4i %-16s %-34s %-20s %-20s %-40s %-40s\033[0m\n")
                          % explored_trace.size() % remove_leading_zeros(StringFromAddrint(ins_addr)) 
                          % addr_ins_static_map[ins_addr].disass  
                          % sregs_format(ins_addr) % dregs_format(ins_addr) 
                          % mem_read_format(mem_read_addr, mem_read_size) % sstream_dmems.str();
//                           % "mem->reg";
  }
  return;
}

/*====================================================================================================================*/

void print_debug_reg_to_reg(ADDRINT ins_addr, UINT32 num_sregs, UINT32 num_dregs)
{
  if (print_debug_text) 
  {
    std::stringstream sstream_smems;
    sstream_smems << "smems: []"; 
    
    std::stringstream sstream_dmems;
    sstream_dmems << "dmems: []";
    
    tainting_log_file << boost::format("\033[0m%-4i %-16s %-34s %-20s %-20s %-40s %-40s\033[0m\n")
                          % explored_trace.size() % remove_leading_zeros(StringFromAddrint(ins_addr)) 
                          % addr_ins_static_map[ins_addr].disass  
                          % sregs_format(ins_addr) % dregs_format(ins_addr) 
                          % sstream_smems.str() % sstream_dmems.str();
//                           % "reg->reg";
  }
  return;
}

/*====================================================================================================================*/

void print_debug_new_checkpoint(ADDRINT ins_addr) 
{
  if (print_debug_text)
  { 
    std::stringstream sstream_smems;
    sstream_smems << "smems: [" 
                  << remove_leading_zeros(StringFromAddrint(*(saved_ptr_checkpoints.back()->dep_mems.begin()))) << ", "
                  << remove_leading_zeros(StringFromAddrint(*(saved_ptr_checkpoints.back()->dep_mems.rbegin()))) << "]";
                  
    std::stringstream sstream_dmems;
    sstream_dmems << "dmems: []";
    
    tainting_log_file << boost::format("\033[36m%-4i %-16s %-34s %-20s %-20s %-40s %-40s\033[0m\n")
                          % explored_trace.size() % remove_leading_zeros(StringFromAddrint(ins_addr)) 
                          % addr_ins_static_map[ins_addr].disass 
                          % sregs_format(ins_addr) % dregs_format(ins_addr) % sstream_smems.str() % sstream_dmems.str();
//                           % "checkpoint" % "mem->reg";
  }
  return;
}

/*====================================================================================================================*/

void print_debug_dep_branch(ADDRINT ins_addr, ptr_branch& dep_ptr_branch) 
{
  if (print_debug_text)
  {
    std::stringstream sstream_smems;
    sstream_smems << "smems: [" 
//                   << remove_leading_zeros(StringFromAddrint(*(saved_ptr_checkpoints.back()->dep_mems.begin()))) << ", "
                  << remove_leading_zeros(StringFromAddrint(*(dep_ptr_branch->dep_input_addrs.begin()))) << ", "
                  << remove_leading_zeros(StringFromAddrint(*(dep_ptr_branch->dep_input_addrs.begin()))) << "]";
//                   << remove_leading_zeros(StringFromAddrint(*(saved_ptr_checkpoints.back()->dep_mems.rbegin()))) << "]";
                  
    std::stringstream sstream_dmems;
    sstream_dmems << "dmems: []";
                  
    tainting_log_file << boost::format("\033[32m%-4i %-16s %-34s %-20s %-20s %-40s %-40s %-2i %-4i\033[0m\n")
                          % explored_trace.size() % remove_leading_zeros(StringFromAddrint(ins_addr)) 
                          % addr_ins_static_map[ins_addr].disass  
                          % sregs_format(ins_addr) % dregs_format(ins_addr) % sstream_smems.str() % sstream_dmems.str()
                          % dep_ptr_branch->br_taken % dep_ptr_branch->chkpnt->trace.size();
//                           % "dep" % "branch" ;
  }
  
  return;
}

/*====================================================================================================================*/

void print_debug_indep_branch(ADDRINT ins_addr, ptr_branch& indep_ptr_branch) 
{
  if (print_debug_text) 
  {

    std::stringstream sstream_smems;
    if (indep_ptr_branch->dep_other_addrs.empty()) 
    {
      sstream_smems << "smems: []"; 
    }
    else 
    {
      sstream_smems << "smems: [" 
                    << remove_leading_zeros(StringFromAddrint(*(indep_ptr_branch->dep_other_addrs.begin()))) << ", "
                    << remove_leading_zeros(StringFromAddrint(*(indep_ptr_branch->dep_other_addrs.rbegin()))) << "]";
    }
    
    std::stringstream sstream_dmems;
    sstream_dmems << "dmems: []";
                  
    tainting_log_file << boost::format("\033[33m%-4i %-16s %-34s %-20s %-20s %-40s %-40s %-2i\033[0m\n")
                            % explored_trace.size() % remove_leading_zeros(StringFromAddrint(ins_addr)) 
                            % addr_ins_static_map[ins_addr].disass  
                            % sregs_format(ins_addr) % dregs_format(ins_addr) % sstream_smems.str() % sstream_dmems.str()
                            % indep_ptr_branch->br_taken;
//                             % "indep" % "branch";
  }
  return;
}

/*====================================================================================================================*/

void journal_branch_messages(ptr_branch& ptr_resolved_branch) 
{
  std::stringstream msg_number_name;
  std::string msg_file_name;
  std::string br_taken_name;
  
  std::vector< boost::shared_ptr<UINT8> >::iterator msg_number_iter;
  
  std::map< bool, 
            std::vector< boost::shared_ptr<UINT8> > 
          >::iterator msg_map_iter = ptr_resolved_branch->inputs.begin();
  for (; msg_map_iter != ptr_resolved_branch->inputs.end(); ++msg_map_iter) 
  {
    br_taken_name = msg_map_iter->first ? "taken" : "nottaken";
    msg_file_name = "msg_" + br_taken_name + "_";
    
    msg_number_iter = msg_map_iter->second.begin();
    for (; msg_number_iter != msg_map_iter->second.end(); ++msg_number_iter)
    {
      msg_number_name << (msg_number_iter - msg_map_iter->second.begin());
      journal_buffer((msg_file_name + msg_number_name.str()).c_str(), (*msg_number_iter).get(), received_msg_size);
      msg_number_name.clear();
      msg_number_name.str("");
    }
  }
  
  return;
}
