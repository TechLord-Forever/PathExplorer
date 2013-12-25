/*
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

#include "instruction.h"
#include <boost/concept_check.hpp>

namespace dataflow_analysis
{

instruction::instruction(const INS& current_instruction)
{
  this->address           = INS_Address(current_instruction);
  this->dissasembled_name = INS_Disassemble(current_instruction);
  
  // the source and target registers of an instruction can be determined statically
  REG current_register;
  uint8_t register_id;
  uint8_t register_number; 
  
  // source operands as read registers
  register_number = INS_MaxNumRRegs(current_instruction);
  for (register_id = 0; register_id < register_number; ++register_id) 
  {
    current_register = INS_RegR(current_instruction, register_id);
    
    // the source pointer is not considered when it is the instruction pointer, 
    // namely the control tainting is not counted.
    if (current_register != REG_INST_PTR) 
    {
      if (INS_IsRet(current_instruction) && (current_register == REG_STACK_PTR))
      {
        // when the instruction is ret, the esp (and rsp) register will be used 
        // implicitly to point out the address of popped value; to elimite the 
        // excessive dependence, this register is not considered.
      }
      else 
      {
        this->source_operands.insert(instruction_operand(current_register));
      }
    }
  }
  
  // target operands as written registers
  register_number = INS_MaxNumWRegs(current_instruction);
  for (register_id = 0; register_id < register_number; ++register_id) 
  {
    current_register = INS_RegW(current_instruction, register_id);
    if (current_register != REG_INST_PTR) 
    {
      if (INS_IsRet(current_instruction) && (current_register == REG_STACK_PTR))
      {
        // the explication above
      }
      else 
      {
        this->target_operands.insert(instruction_operand(current_register));
      }
    }
  }
}


/**
 * @brief the read or written memories of an instruction cannot be determined statically, so they 
 * need to be updated gradually in the running time.
 * 
 * @param access_address the beginning read/written address
 * @param access_length the read/written length
 * @param read_or_written read (false) or written (true)
 * @return void
 */
void instruction::update_memory(ADDRINT access_address, UINT8 access_length, bool read_or_written)
{
  ADDRINT address;
  ADDRINT upper_bound_address = access_address + access_length;
  
  for (address = access_address; address < upper_bound_address; ++address) 
  {
    if (read_or_written) // memory written
    {
      this->target_operands.insert(instruction_operand(address));
    }
    else // memory read
    {
      this->source_operands.insert(instruction_operand(address));
    }
  }
  
  return;
}


} // end of dataflow_analysis namespace