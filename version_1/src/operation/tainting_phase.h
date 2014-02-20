#ifndef TAINTING_PHASE_H
#define TAINTING_PHASE_H

#include <pin.H>
#include <boost/predef.h>

namespace tainting
{
extern void initalize_tainting_phase();
extern VOID syscall_instruction(ADDRINT ins_addr);
extern VOID general_instruction(ADDRINT ins_addr);
extern VOID mem_read_instruction(ADDRINT ins_addr,
                                 ADDRINT mem_read_addr, UINT32 mem_read_size, CONTEXT* p_ctxt);
extern VOID mem_write_instruction(ADDRINT ins_addr,
                                  ADDRINT mem_written_addr, UINT32 mem_written_size);
extern VOID graphical_propagation(ADDRINT ins_addr);

} // end of tainting namespace
#endif
