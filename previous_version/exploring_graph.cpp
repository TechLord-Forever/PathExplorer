#include "exploring_graph.h"
#include "stuffs.h"

#include <vector>
#include <list>
#include <iostream>
#include <fstream>
#include <bitset>

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graphviz.hpp>
#include <boost/dynamic_bitset.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/format.hpp>

typedef boost::tuple<ADDRINT, std::string>                    exp_vertex;
typedef boost::tuple<next_exe_type, UINT32, UINT32, UINT32>   exp_edge;
typedef boost::adjacency_list<boost::listS, boost::vecS, 
                              boost::bidirectionalS, 
                              exp_vertex, exp_edge>           exp_graph;
                              
typedef boost::graph_traits<exp_graph>::vertex_descriptor     exp_vertex_desc;
typedef boost::graph_traits<exp_graph>::edge_descriptor       exp_edge_desc;
typedef boost::graph_traits<exp_graph>::vertex_iterator       exp_vertex_iter;
typedef boost::graph_traits<exp_graph>::edge_iterator         exp_edge_iter;


/*================================================================================================*/

extern std::map<ADDRINT, instruction> addr_ins_static_map;
extern std::bitset<30>                path_code;

/*================================================================================================*/

static exp_graph internal_exp_graph;

/*================================================================================================*/

exploring_graph::exploring_graph()
{
  internal_exp_graph.clear();
}

/*------------------------------------------------------------------------------------------------*/

static inline exp_vertex make_vertex(ADDRINT node_addr, UINT32 node_br_order)
{
  boost::dynamic_bitset<> node_code(node_br_order);
  for (UINT32 bit_idx = 0; bit_idx < node_br_order; ++bit_idx) 
  {
    node_code[bit_idx] = path_code[bit_idx];
  }
  std::string node_code_str; boost::to_string(node_code, node_code_str);
  return boost::make_tuple(node_addr, node_code_str);
}

/*================================================================================================*/

void exploring_graph::add_node(ADDRINT node_addr, UINT32 node_br_order)
{
  exp_vertex curr_vertex = make_vertex(node_addr, node_br_order);
  
  exp_vertex_iter vertex_iter;
  exp_vertex_iter last_vertex_iter;
  boost::tie(vertex_iter, last_vertex_iter) = boost::vertices(internal_exp_graph);
  for (; vertex_iter != last_vertex_iter; ++vertex_iter)
  {
    if ((boost::get<0>(internal_exp_graph[*vertex_iter]) == boost::get<0>(curr_vertex)) && 
        (boost::get<1>(internal_exp_graph[*vertex_iter]) == boost::get<1>(curr_vertex)))
    {
      break;
    }
  }
  if (vertex_iter == last_vertex_iter) 
  {
    boost::add_vertex(curr_vertex, internal_exp_graph);
  }
  return;
}

/*================================================================================================*/

void exploring_graph::add_edge(ADDRINT source_addr, UINT32 source_br_order, 
                               ADDRINT target_addr, UINT32 target_br_order,
                               next_exe_type direction, UINT32 nb_bits, UINT32 rb_length, UINT32 nb_rb)
{
  exp_vertex source_vertex = make_vertex(source_addr, source_br_order);
  exp_vertex target_vertex = make_vertex(target_addr, target_br_order);
  exp_vertex curr_vertex;
  
  exp_vertex_desc source_desc = 0;
  exp_vertex_desc target_desc = 0;
  
  exp_vertex_iter vertex_iter;
  exp_vertex_iter last_vertex_iter;
  boost::tie(vertex_iter, last_vertex_iter) = boost::vertices(internal_exp_graph);
  for (; vertex_iter != last_vertex_iter; ++vertex_iter) 
  {
    curr_vertex = internal_exp_graph[*vertex_iter];
    
    if ((boost::get<0>(curr_vertex) == boost::get<0>(source_vertex)) && 
        (boost::get<1>(curr_vertex) == boost::get<1>(source_vertex)))
    {
      source_desc = *vertex_iter;
    }
    if ((boost::get<0>(curr_vertex) == boost::get<0>(target_vertex)) && 
        (boost::get<1>(curr_vertex) == boost::get<1>(target_vertex))) 
    {
      target_desc = *vertex_iter;
    }
    
    if ((source_desc != 0) && (target_desc != 0)) 
    {
      break;
    }
  }
  
  exp_edge_desc edge_desc;
  bool edge_existed;
  boost::tie(edge_desc, edge_existed) = boost::edge(source_desc, target_desc, internal_exp_graph);
  if (!edge_existed && (source_desc != 0) && (target_desc != 0)) 
  {
    boost::add_edge(source_desc, target_desc, 
                    boost::make_tuple(direction, nb_bits, rb_length, nb_rb), internal_exp_graph);
  }
//   std::cout << nb_bits << "\n";
  return;
}

/*================================================================================================*/

class exp_vertex_label_writer
{
public:
  exp_vertex_label_writer(exp_graph& g) : graph(g) 
  {
  }
  
  template <typename Vertex>
  void operator()(std::ostream& out, Vertex v) 
  {
    exp_vertex vertex_addr = graph[v];
    out << boost::format("[label=\"<%s,%s>\"]") 
            % remove_leading_zeros(StringFromAddrint(vertex_addr)) 
            % addr_ins_static_map[vertex_addr].disass;
//     std::string vertex_label;
//     vertex_label = "<" + remove_leading_zeros(StringFromAddrint(vertex_addr)) + ", " + 
//                    addr_ins_static_map[vertex_addr].disass + ">";
      
//     exp_vertex vertex_var(graph[v]);
//     std::string vertex_label;
    
    
        
//     if ((vertex_var.type == MEM_VAR) && (received_msg_addr <= vertex_var.mem) && 
//         (vertex_var.mem < received_msg_addr + received_msg_size)) 
//     {
//       vertex_label = "[color=blue, style=filled, label=\"";
//     }
//     else 
//     {
//       vertex_label = "[color=black, label=\"";
//     }
//     vertex_label += vertex_var.name;
//     vertex_label += "\"]";
    
//     out << vertex_label;
  }
  
private:
  exp_graph graph;
};

class exp_edge_label_writer
{
public:
  exp_edge_label_writer(exp_graph& g) : graph(g)
  {
  }
  
  template <typename Edge>
  void operator()(std::ostream& out, Edge edge) 
  {
//     exp_edge ve = graph[edge];
    
//     out << "[label=\"(" << decstr(ve.second) << ") " << addr_ins_static_map[ve.first].disass << "\"]";
    
    next_exe_type direction; UINT32 nb_bits; UINT32 rb_length; UINT32 nb_rb;
    boost::tie(direction, nb_bits, rb_length, nb_rb) = graph[edge];
    if (direction == ROLLBACK) 
    {
      out << boost::format("[color=red, label=\"<%d,%d,%d,%d>\"]") 
              % direction % nb_bits % rb_length % nb_rb;
    }
    else 
    {
      out << boost::format("[label=\"<%d,%d,%d,%d>\"]") 
              % direction % nb_bits % rb_length % nb_rb;
    }
    
  }
  
private:
  exp_graph graph;
};

void exploring_graph::print_to_file(const std::string& filename)
{
  std::ofstream output_file(filename.c_str(), std::ofstream::out | std::ofstream::trunc);
  boost::write_graphviz(output_file, internal_exp_graph, 
                        exp_vertex_label_writer(internal_exp_graph), 
                        exp_edge_label_writer(internal_exp_graph));
  return;
}




