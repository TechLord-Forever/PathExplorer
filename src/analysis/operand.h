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

#ifndef OPERAND_H
#define OPERAND_H

#include <pin.H>
#include <string>
#include <boost/shared_ptr.hpp>
#include <boost/functional/hash.hpp>
#include <boost/variant.hpp>

namespace analysis 
{

/**
 * @brief class representing instruction operands.
 * 
 */
class operand
{
public:
  std::string                           name;
  boost::variant<ADDRINT, REG, UINT32>  value;
  UINT32                                life_span;
  
public:
	operand();
  operand(ADDRINT memory_operand);
  operand(REG register_operand);
  operand(UINT32 immediate_operand);
	operand& operator=(const operand& other_operand);
};

// inline bool operator==(const instruction_operand& operand_a, const instruction_operand& operand_b) 
// {
//   return (operand_a.name == operand_b.name);
// }

typedef boost::shared_ptr<operand> ptr_insoperand_t;


/**
 * @brief a hash distinguishing instruction operands
 * 
 */
// class operand_hash
// {
// public:
//   std::size_t operator()(const instruction_operand& operand) const
//   {
//     boost::hash<std::string> string_ref_hash;
//     return string_ref_hash(operand.name);
//   }
// };

} // end of dataflow_analysis namespace

#endif // INSTRUCTION_OPERAND_H
