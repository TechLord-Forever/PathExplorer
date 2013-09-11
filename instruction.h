#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include <string>
#include <map>
#include <set>
#include <vector>

#include <pin.H>
extern "C" 
{
#include <xed-interface.h>
}


/* ------------------------------------------------------------------------------------------------------------------ */
/*                                                           data types                                               */
/* ------------------------------------------------------------------------------------------------------------------ */
class instruction;
// class checkpoint;

typedef enum 
{
  syscall_inexist  = 0,
  syscall_sendto   = 44,
  syscall_recvfrom = 45
} syscall_id;

/* ------------------------------------------------------------------------------------------------------------------ */
/*                                                  class declaration                                                 */
/* ------------------------------------------------------------------------------------------------------------------ */
class instruction
{
public:                                                                         
  ADDRINT             address;                                                     
  
  std::string         disass;
  std::string         img;

  xed_category_enum_t category;
  
  UINT32              mem_read_size;
  UINT32              mem_written_size;
  
  std::map< ADDRINT, UINT8 > mem_read_map;
  std::map< ADDRINT, UINT8 > mem_written_map;

public:
  instruction();
  instruction(INS const& ins);
  instruction(instruction const& other_ins);
  instruction& operator=(instruction const& other_ins);
};

#endif // INSTRUCTION_H
