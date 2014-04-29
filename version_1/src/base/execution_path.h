#ifndef EXECUTION_PATH_H
#define EXECUTION_PATH_H

#include "../parsing_helper.h"
#include "../operation/common.h"
#include "cond_direct_instruction.h"

typedef std::pair<addrint_value_maps_t, ptr_cond_direct_inss_t> condition_t;
typedef std::vector<condition_t> conditions_t;


class execution_path
{
public:
  order_ins_map_t   content;
  path_code_t       code;
  conditions_t      condition;
  int               condition_order;
  bool              condition_is_recursive;

  execution_path(const order_ins_map_t& current_path, const path_code_t& current_path_code);
  conditions_t lazy_condition(unsigned int n);
};

typedef std::shared_ptr<execution_path> ptr_execution_path_t;
typedef std::vector<ptr_execution_path_t> ptr_execution_paths_t;

auto show_path_condition(const ptr_execution_paths_t& exp_paths) -> void;

#endif // EXECUTION_PATH_H
