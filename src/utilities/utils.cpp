/*
 * <one line to give the program's name and a brief idea of what it does.>
 * Copyright (C) 2013  Ta Thanh Dinh <email>
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

#include "utils.h"

namespace utilities 
{
	
/**
 * @brief reformat the string showing some memory address.
 * 
 * @param input input string
 * @return std::string
 */
std::string utils::remove_leading_zeros(std::string input)
{
  std::string::iterator str_iter = input.begin();
  std::string output("0x");

  while ((str_iter != input.end()) && ((*str_iter == '0') || (*str_iter == 'x'))) 
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


/**
 * @brief determine if the memory address is dependent on the input.
 * 
 * @param memory_address memory address
 * @return bool
 */
bool utils::is_input_dependent(ADDRINT memory_address)
{
	return true;
}

} // end of utilities namespace