// Copyright 2002 - 2008, 2010, 2011 National Technology Engineering
// Solutions of Sandia, LLC (NTESS). Under the terms of Contract
// DE-NA0003525 with NTESS, the U.S. Government retains certain rights
// in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// 
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
// 
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
// 
//     * Neither the name of NTESS nor the names of its contributors
//       may be used to endorse or promote products derived from this
//       software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// 

#include <stk_mesh/base/FEMHelpers.hpp>
#include <stk_mesh/base/BulkData.hpp>   // for BulkData
#include <stk_mesh/base/Types.hpp>      // for PartVector, EntityId, etc
#include "stk_mesh/base/MetaData.hpp"   // for get_topology, etc
#include "stk_mesh/base/Part.hpp"       // for Part
#include "stk_topology/topology.hpp"    // for topology, etc
#include "stk_util/util/ReportHandler.hpp"  // for ThrowErrorMsgIf, etc
#include <stk_mesh/baseImpl/MeshImplUtils.hpp>
#include <stk_mesh/baseImpl/Partition.hpp>
#include <sstream>                      // for operator<<, etc
#include <string>                       // for char_traits, operator<<
#include <numeric>
#include <iterator>
#include <type_traits>
#include <algorithm>

namespace stk::mesh {
namespace {

void verify_declare_element_edge(
        const BulkData & mesh,
        const Entity elem,
        const unsigned local_edge_id
        )
{
    stk::topology elem_top = mesh.bucket(elem).topology();

    STK_ThrowErrorMsgIf( elem_top == stk::topology::INVALID_TOPOLOGY,
            "Element[" << mesh.identifier(elem) << "] has no defined topology");

    stk::topology invalid = stk::topology::INVALID_TOPOLOGY;
    stk::topology edge_top =
            (elem_top != stk::topology::INVALID_TOPOLOGY && local_edge_id < elem_top.num_edges() )
            ? elem_top.edge_topology(local_edge_id) : invalid;

    STK_ThrowErrorMsgIf( elem_top!=stk::topology::INVALID_TOPOLOGY && local_edge_id >= elem_top.num_edges(),
            "For elem " << mesh.identifier(elem) << " ("<<elem_top<<"), local_edge_id " << local_edge_id << ", " <<
            "local_edge_id exceeds " << elem_top.name() << ".edge_count = " << elem_top.num_edges());

    STK_ThrowErrorMsgIf( edge_top == stk::topology::INVALID_TOPOLOGY,
            "For elem " << mesh.identifier(elem) << ", local_edge_id " << local_edge_id << ", " <<
            "No element topology found");
}

} // unnamed namespace

Entity declare_element(BulkData & mesh,
        PartVector & parts,
        const EntityId elem_id,
        const EntityIdVector & node_ids)
{
    const MetaData & fem_meta = mesh.mesh_meta_data();
    stk::topology top = fem_meta.get_topology(*parts[0]);
    STK_ThrowAssert(node_ids.size() >= top.num_nodes());

    STK_ThrowErrorMsgIf(top == stk::topology::INVALID_TOPOLOGY,
            "Part " << parts[0]->name() << " does not have a local topology");

    PartVector empty;

    Entity elem = mesh.declare_element(elem_id, parts);

    Permutation perm = stk::mesh::Permutation::INVALID_PERMUTATION;
    OrdinalVector scratch1, scratch2, scratch3;

    for(unsigned i = 0; i < top.num_nodes(); ++i)
    {
        //declare node if it doesn't already exist
        Entity node = mesh.get_entity(stk::topology::NODE_RANK, node_ids[i]);
        if(!mesh.is_valid(node))
        {
            node = mesh.declare_node(node_ids[i], empty);
        }

        mesh.declare_relation(elem, node, i, perm, scratch1, scratch2, scratch3);
    }
    return elem;
}

Entity connect_side_to_element_with_ordinal( BulkData & mesh ,
                               Entity elem ,
                               Entity side ,
                               const unsigned sideOrdinal ,
                               stk::mesh::Part* part)
{
    stk::mesh::PartVector parts;
    if(part!=NULL)
    {
        parts.push_back(part);
    }
    stk::topology elem_top = mesh.bucket(elem).topology();
    stk::topology side_top = elem_top.side_topology(sideOrdinal);
    return impl::connect_element_to_side(mesh, elem, side, sideOrdinal, parts, side_top);
}

Entity connect_side_to_element_with_ordinal( BulkData & mesh ,
                               Entity elem ,
                               Entity side ,
                               const unsigned sideOrdinal ,
                               const std::vector<Entity> & side_nodes,
                               stk::mesh::Part* part)
{
    stk::mesh::PartVector parts;
    if(part!=NULL)
    {
        parts.push_back(part);
    }
    stk::topology elem_top = mesh.bucket(elem).topology();
    stk::topology side_top = elem_top.side_topology(sideOrdinal);
    return impl::connect_element_to_side(mesh, elem, side, sideOrdinal, parts, side_top, side_nodes);
}

Entity declare_element_edge(
        BulkData & mesh,
        const stk::mesh::EntityId global_edge_id,
        Entity elem,
        const unsigned local_edge_id,
        const stk::mesh::PartVector &parts)
{
    verify_declare_element_edge(mesh, elem, local_edge_id);

    stk::topology elem_top = mesh.bucket(elem).topology();
    stk::topology edge_top = elem_top.edge_topology(local_edge_id);

    PartVector empty_parts;
    if(mesh.mesh_meta_data().spatial_dimension() == 2)
    {
        return mesh.declare_element_side(elem, local_edge_id, parts);
    }

    Entity edge = mesh.get_entity(edge_top.rank(), global_edge_id);
    if(!mesh.is_valid(edge))
    {
        edge = mesh.declare_edge(global_edge_id, empty_parts);
        impl::connect_element_to_entity(mesh, elem, edge, local_edge_id, parts, edge_top);
    }
    return edge;
}

typedef std::pair<stk::mesh::ConnectivityOrdinal, stk::mesh::Permutation> ConnectivityAndOrdinal;


void mark_if_negative_permutation(stk::EquivalentPermutation &result, stk::topology sub_topology)
{
    if (result.is_equivalent && result.permutation_number >= sub_topology.num_positive_permutations())
    {
        result.is_equivalent = false;
    }
}

void set_ordinal_and_permutation_if_equivalent(const stk::EquivalentPermutation &result,
                                               unsigned ordinal,
                                               ConnectivityAndOrdinal &ordinalAndPermutation)
{
    if (result.is_equivalent == true)
    {
        ordinalAndPermutation.first = static_cast<stk::mesh::ConnectivityOrdinal>(ordinal);
        ordinalAndPermutation.second = static_cast<stk::mesh::Permutation>(result.permutation_number);
    }
}


class ShellPermutationFilter
{
public:
    ShellPermutationFilter(const stk::mesh::BulkData& mesh,
                           stk::mesh::Entity parent_entity,
                           stk::mesh::EntityRank to_rank)
    : m_mesh(mesh), m_entity(parent_entity), m_toRank(to_rank), m_filterForShell(false)
    {
        stk::topology elemTopology = m_mesh.bucket(m_entity).topology();
        m_filterForShell = elemTopology.is_shell() && to_rank == mesh.mesh_meta_data().side_rank();
    }

    ~ShellPermutationFilter() {}

    bool set_ordinal_and_permutation(const stk::mesh::EntityVector &nodes_of_sub_rank,
                                     stk::mesh::Entity nodes_of_sub_topology[],
                                     stk::topology sub_topology,
                                     unsigned ordinal,
                                     ConnectivityAndOrdinal &ordinalAndPermutation) const
    {
        stk::EquivalentPermutation result = sub_topology.is_equivalent(nodes_of_sub_rank.data(), nodes_of_sub_topology);

        constexpr unsigned numLegacyShellSidesTo_TEMPORARILY_PreservePreviousBehavior = 2;
        if(m_filterForShell &&
           ordinal < numLegacyShellSidesTo_TEMPORARILY_PreservePreviousBehavior) {
            mark_if_negative_permutation(result, sub_topology);
        }

        set_ordinal_and_permutation_if_equivalent(result, ordinal, ordinalAndPermutation);
        return result.is_equivalent;
    }

private:
    ShellPermutationFilter();

    const stk::mesh::BulkData& m_mesh;
    stk::mesh::Entity m_entity;
    stk::mesh::EntityRank m_toRank;
    bool m_filterForShell;
};

template<typename PermutationFilter>
OrdinalAndPermutation
get_ordinal_and_permutation_with_filter(const stk::mesh::BulkData& mesh,
                                        stk::mesh::Entity parent_entity,
                                        stk::mesh::EntityRank to_rank,
                                        const stk::mesh::EntityVector &nodes_of_sub_rank,
                                        PermutationFilter &pFilter)
{
    ConnectivityAndOrdinal ordinalAndPermutation = std::make_pair(stk::mesh::INVALID_CONNECTIVITY_ORDINAL, stk::mesh::INVALID_PERMUTATION);

    unsigned nodes_of_sub_rank_size = nodes_of_sub_rank.size();

    const Entity* elemNodes = mesh.begin_nodes(parent_entity);
    stk::topology elemTopology = mesh.bucket(parent_entity).topology();
    unsigned num_entities_of_sub_topology = elemTopology.num_sub_topology(to_rank);
    const unsigned max_nodes_possible = 100;
    stk::mesh::Entity nodes_of_sub_topology[max_nodes_possible];

    for (unsigned i=0;i<num_entities_of_sub_topology;++i)
    {
        stk::topology sub_topology = elemTopology.sub_topology(to_rank, i);
        unsigned num_nodes = sub_topology.num_nodes();

        if (num_nodes !=  nodes_of_sub_rank_size)
        {
            continue;
        }

        STK_ThrowRequireMsg(num_nodes<=max_nodes_possible, "Program error. Exceeded expected array dimensions. Contact sierra-help for support.");
        elemTopology.sub_topology_nodes(elemNodes, to_rank, i, nodes_of_sub_topology);

        pFilter.set_ordinal_and_permutation(nodes_of_sub_rank, nodes_of_sub_topology, sub_topology, i, ordinalAndPermutation);
        if (ordinalAndPermutation.first != stk::mesh::INVALID_CONNECTIVITY_ORDINAL) {
            break;
        }
    }

    return ordinalAndPermutation;
}

OrdinalAndPermutation
get_ordinal_and_permutation(const stk::mesh::BulkData& mesh,
                            stk::mesh::Entity parent_entity,
                            stk::mesh::EntityRank to_rank,
                            const stk::mesh::EntityVector &nodes_of_sub_rank)
{
    ShellPermutationFilter pFilter(mesh, parent_entity, to_rank);
    return get_ordinal_and_permutation_with_filter(mesh, parent_entity, to_rank, nodes_of_sub_rank, pFilter);
}

bool element_side_polarity(const BulkData& mesh,
                           const Entity elem ,
                           const Entity side , unsigned local_side_id )
{
    // 09/14/10:  TODO:  tscoffe:  Will this work in 1D??
    const bool is_side = mesh.entity_rank(side) != stk::topology::EDGE_RANK;
    stk::topology elem_top = mesh.bucket(elem).topology();

    const unsigned side_count = ! (elem_top != stk::topology::INVALID_TOPOLOGY) ? 0 : (
        is_side ? elem_top.num_sides()
            : elem_top.num_edges() );

    STK_ThrowErrorMsgIf( elem_top == stk::topology::INVALID_TOPOLOGY,
        "For Element[" << mesh.identifier(elem) << "], element has no defined topology");

    STK_ThrowErrorMsgIf( static_cast<unsigned>(side_count) <= local_side_id,
        "For Element[" << mesh.identifier(elem) << "], " <<
        "side: " << mesh.identifier(side) << ", " <<
        "local_side_id = " << local_side_id <<
        " ; unsupported local_side_id");

    stk::topology side_top =
        is_side ? elem_top.side_topology( local_side_id )
            : elem_top.sub_topology( stk::topology::EDGE_RANK, local_side_id );

    std::vector<unsigned> side_map(side_top.num_nodes());
    elem_top.side_node_ordinals( local_side_id, side_map.data());

    Entity const *elem_nodes = mesh.begin_nodes(elem);
    Entity const *side_nodes = mesh.begin_nodes(side);
    const unsigned n = side_top.num_nodes();
    bool good = false ;
    for ( unsigned i = 0 ; !good && i < n ; ++i ) {
        good = true;
        for ( unsigned j = 0; good && j < n ; ++j ) {
          good = side_nodes[(j+i)%n] == elem_nodes[ side_map[j] ];
        }    
    }    
    return good ;
}

stk::EquivalentPermutation sub_rank_equivalent(const stk::mesh::BulkData& mesh,
                                            stk::mesh::Entity element,
                                            unsigned ordinal,
                                            stk::mesh::EntityRank subRank,
                                            const stk::mesh::Entity* subRankNodes)
{
    const stk::mesh::Entity* elemNodes = mesh.begin_nodes(element);
    stk::topology elemTopology = mesh.bucket(element).topology();
    stk::topology subTopology = elemTopology.sub_topology(subRank, ordinal);
    const unsigned maxNumSubNodes = 144;
    stk::mesh::Entity elemSubRankNodes[maxNumSubNodes];
    STK_ThrowAssertMsg(subTopology.num_nodes() < maxNumSubNodes, "Error in sub_rank_equivalent, subTopology.num_nodes() needs to be less than hard-coded array-length "<<maxNumSubNodes);
    elemTopology.sub_topology_nodes(elemNodes, subRank, ordinal, elemSubRankNodes);
    return subTopology.is_equivalent(elemSubRankNodes, subRankNodes);
}

stk::EquivalentPermutation side_equivalent(const stk::mesh::BulkData& mesh,
                                          stk::mesh::Entity element,
                                          unsigned sideOrdinal,
                                          const stk::mesh::Entity* candidateSideNodes)
{
    unsigned rankedOrdinal;
    stk::topology::rank_t sideRank;

    auto elemTopo = mesh.bucket(element).topology();
    elemTopo.ranked_side_ordinal(sideOrdinal, rankedOrdinal, sideRank);

    return sub_rank_equivalent(mesh, element, rankedOrdinal, sideRank, candidateSideNodes);
}

bool is_side_equivalent(const stk::mesh::BulkData& mesh, stk::mesh::Entity element, unsigned sideOrdinal, const stk::mesh::Entity* candidateSideNodes)
{
    return side_equivalent(mesh, element, sideOrdinal, candidateSideNodes).is_equivalent;
}

bool is_edge_equivalent(const stk::mesh::BulkData& mesh, stk::mesh::Entity element, unsigned edgeOrdinal, const stk::mesh::Entity* candidateEdgeNodes)
{
    return sub_rank_equivalent(mesh, element, edgeOrdinal, stk::topology::EDGE_RANK, candidateEdgeNodes).is_equivalent;
}

namespace {
inline void sub_topology_check(const stk::mesh::EntityVector& candidateSideNodes,
                               stk::topology elemTopology,
                               stk::topology subTopology)
{
    bool sizeCheck = (subTopology == stk::topology::INVALID_TOPOLOGY) ? true : (candidateSideNodes.size() == subTopology.num_nodes());
    STK_ThrowRequireMsg(sizeCheck, "ERROR, Invalid number of nodes for ("
                               << elemTopology << "),subTopology=" << subTopology
                               << ", side: " << candidateSideNodes.size()
                               << ", expected: " << subTopology.num_nodes());
}

inline void sub_topology_check(size_t numCandidateSideNodes,
                               stk::topology elemTopology,
                               stk::topology subTopology)
{
    bool sizeCheck = (subTopology == stk::topology::INVALID_TOPOLOGY) ? true : (numCandidateSideNodes == subTopology.num_nodes());
    STK_ThrowRequireMsg(sizeCheck, "ERROR, Invalid number of nodes for ("
                               << elemTopology << "),subTopology=" << subTopology
                               << ", side: " << numCandidateSideNodes
                               << ", expected: " << subTopology.num_nodes());
}

}

EquivAndPositive is_shell_edge_equivalent_and_positive(const stk::mesh::BulkData& mesh,
                                                       stk::mesh::Entity element,
                                                       unsigned /*sideOrdinal*/,
                                                       const stk::mesh::EntityVector& candidateSideNodes)
{
    EquivAndPositive result = {false, false};
    stk::topology elemTopology = mesh.bucket(element).topology();
    stk::topology subTopology = elemTopology.sub_topology(mesh.mesh_meta_data().side_rank(), 0);
    stk::topology edgeTopology = subTopology.edge_topology(0);
    sub_topology_check(candidateSideNodes, elemTopology, edgeTopology);

    const stk::mesh::Entity* elemNodes = mesh.begin_nodes(element);
    unsigned numElemNodes = elemTopology.num_nodes();
    const stk::mesh::Entity* iter = std::find(elemNodes, elemNodes+numElemNodes, candidateSideNodes[0]);

    if (iter != elemNodes+numElemNodes) {
        unsigned idx = iter - elemNodes;
        unsigned nextIdx = (idx+1)%numElemNodes;
        unsigned prevIdx = (idx+numElemNodes-1)%numElemNodes;
        result.is_equiv =  (elemNodes[prevIdx] == candidateSideNodes[1]) || (elemNodes[nextIdx] == candidateSideNodes[1]);
        result.is_positive = (elemNodes[nextIdx] == candidateSideNodes[1]);
    }
    return result;
}

EquivAndPositive is_shell_side_equivalent_and_positive(const stk::mesh::BulkData& mesh,
                                                       stk::mesh::Entity element,
                                                       unsigned sideOrdinal,
                                                       const stk::mesh::EntityVector& candidateSideNodes)
{
    EquivAndPositive result = {false, false};
    stk::topology elemTopology = mesh.bucket(element).topology();
    stk::topology subTopology = elemTopology.sub_topology(mesh.mesh_meta_data().side_rank(), 0);

    if(subTopology.num_nodes() == candidateSideNodes.size()) {
        result = is_equivalent_and_positive(mesh, element, sideOrdinal, mesh.mesh_meta_data().side_rank(), candidateSideNodes.data());
    } else {
        result = is_shell_edge_equivalent_and_positive(mesh, element, sideOrdinal, candidateSideNodes);
    }

    return result;
}

EquivAndPositive is_side_equivalent_and_positive(const stk::mesh::BulkData& mesh,
                                                 stk::mesh::Entity element,
                                                 unsigned sideOrdinal,
                                                 const stk::mesh::EntityVector& candidateSideNodes)
{
    stk::topology elemTopology = mesh.bucket(element).topology();

    if(elemTopology.is_shell()) {
        return is_shell_side_equivalent_and_positive(mesh, element, sideOrdinal, candidateSideNodes);
    }

    stk::topology subTopology = elemTopology.sub_topology(mesh.mesh_meta_data().side_rank(), sideOrdinal);
    sub_topology_check(candidateSideNodes, elemTopology, subTopology);

    return is_equivalent_and_positive(mesh, element, sideOrdinal, mesh.mesh_meta_data().side_rank(), candidateSideNodes.data());
}

EquivAndPositive is_side_equivalent_and_positive(const stk::mesh::BulkData& mesh,
                                                 stk::mesh::Entity element,
                                                 unsigned sideOrdinal,
                                                 const stk::mesh::Entity* candidateSideNodes,
                                                 size_t numCandidateSideNodes)
{
    stk::topology elemTopology = mesh.bucket(element).topology();

    if(elemTopology.is_shell()) {
        stk::mesh::EntityVector candidateSideNodesVector(candidateSideNodes, candidateSideNodes+numCandidateSideNodes);
        return is_shell_side_equivalent_and_positive(mesh, element, sideOrdinal, candidateSideNodesVector);
    }

    stk::topology subTopology = elemTopology.sub_topology(mesh.mesh_meta_data().side_rank(), sideOrdinal);
    sub_topology_check(numCandidateSideNodes, elemTopology, subTopology);

    return is_equivalent_and_positive(mesh, element, sideOrdinal, mesh.mesh_meta_data().side_rank(), candidateSideNodes);
}

EquivAndPositive is_equivalent_and_positive(const stk::mesh::BulkData& mesh,
                                            stk::mesh::Entity element,
                                            unsigned ordinal,
                                            stk::mesh::EntityRank subRank,
                                            const stk::mesh::Entity* candidateNodes)
{
    stk::EquivalentPermutation result = sub_rank_equivalent(mesh, element, ordinal, subRank, candidateNodes);
    stk::topology elemTopology = mesh.bucket(element).topology();
    stk::topology subTopology = elemTopology.sub_topology(mesh.mesh_meta_data().side_rank(), ordinal);

    return EquivAndPositive{result.is_equivalent, subTopology.is_positive_polarity(result.permutation_number)};
}

stk::topology get_subcell_nodes(const BulkData& mesh, const Entity entity,
        EntityRank subcell_rank,
        unsigned subcell_ordinal,
        EntityVector & subcell_nodes)
{
    STK_ThrowAssert(subcell_rank <= stk::topology::ELEMENT_RANK);

    subcell_nodes.clear();

    stk::topology celltopology = mesh.bucket(entity).topology();

    //error checking
    {
        //no celltopology defined
        if(celltopology == stk::topology::INVALID_TOPOLOGY)
        {
            return celltopology;
        }

        // valid ranks fall within the dimension of the cell topology
        const bool bad_rank = static_cast<unsigned>(subcell_rank) >= celltopology.dimension();
        STK_ThrowInvalidArgMsgIf( bad_rank, "subcell_rank is >= celltopology dimension\n");

        // subcell_ordinal must be less than the subcell count
        bool bad_id = subcell_ordinal >= celltopology.num_sub_topology(subcell_rank);

        // FIXME SHELL_SIDE_TOPOLOGY
        if (celltopology.is_shell() && celltopology.dimension() == 3 && subcell_rank == stk::topology::FACE_RANK) {
          bad_id = (subcell_ordinal >= celltopology.num_sides());
        }

        STK_ThrowInvalidArgMsgIf( bad_id, "subcell_id is >= subcell_count\n");
    }

    stk::topology subcell_topology =
            celltopology.sub_topology(subcell_rank, subcell_ordinal);

    subcell_nodes.resize(subcell_topology.num_nodes());

    Entity const *node_relations = mesh.begin_nodes(entity);
    celltopology.sub_topology_nodes(node_relations, subcell_rank, subcell_ordinal, subcell_nodes.data());

    return subcell_topology;
}

int get_entity_subcell_id(const BulkData& mesh,
        const Entity entity,
        [[maybe_unused]] const EntityRank subcell_rank,
        stk::topology /*subcell_topology*/,
        const std::vector<Entity>& subcell_nodes)
{
    STK_ThrowAssert(subcell_rank <= stk::topology::ELEMENT_RANK);
    const int INVALID_SIDE = -1;

    stk::topology entity_topology = mesh.bucket(entity).topology();
    STK_ThrowAssert(entity_topology.num_nodes() == mesh.num_nodes(entity));

    for(size_t i = 0; i < entity_topology.num_sub_topology(subcell_rank); ++i)
    {
        if(sub_rank_equivalent(mesh, entity, i, subcell_rank, subcell_nodes.data()).is_equivalent)
        {
            return i;
        }
    }

    return INVALID_SIDE;
}

#ifndef STK_HIDE_DEPRECATED_CODE // Delete after June 2025
STK_DEPRECATED void get_parts_with_topology(stk::topology topology,
                             stk::mesh::BulkData& mesh,
                             stk::mesh::PartVector& parts,
                             bool skip_topology_root_parts)
{
  parts.clear();

  const stk::mesh::MetaData & fem_meta = mesh.mesh_meta_data();

  const stk::mesh::PartVector& all_parts = fem_meta.get_parts();

  stk::mesh::PartVector::const_iterator
    iter = all_parts.begin(),
    iter_end = all_parts.end();

  for(; iter!=iter_end; ++iter) {
    stk::mesh::Part* part =  *iter;
    if (fem_meta.get_topology(*part) == topology) {
      if (skip_topology_root_parts && stk::mesh::is_topology_root_part(*part)) {
        continue;
      }
      parts.push_back(part);
    }
  }
}
#endif

stk::mesh::Entity get_side_entity_for_elem_side_pair_of_rank(const stk::mesh::BulkData &bulk, Entity elem, int sideOrdinal, stk::mesh::EntityRank sideRank)
{
    if(bulk.is_valid(elem))
    {
        unsigned numSides = bulk.num_connectivity(elem, sideRank);
        const stk::mesh::Entity* elemSides = bulk.begin(elem, sideRank);
        const stk::mesh::ConnectivityOrdinal* sideOrds = bulk.begin_ordinals(elem, sideRank);
        for(unsigned i = 0; i < numSides; i++)
            if(sideOrds[i] == (sideOrdinal))
                return elemSides[i];
    }
    return stk::mesh::Entity();
}

stk::mesh::Entity get_side_entity_for_elem_side_pair(const stk::mesh::BulkData &bulk, Entity elem, int sideOrdinal)
{
    unsigned rankedSideOrdinal;
    stk::mesh::EntityRank sideRank;

    bulk.bucket(elem).topology().ranked_side_ordinal(sideOrdinal, rankedSideOrdinal, sideRank);
    return get_side_entity_for_elem_side_pair_of_rank(bulk, elem, rankedSideOrdinal, sideRank);
}

stk::mesh::Entity get_side_entity_for_elem_id_side_pair(const stk::mesh::BulkData &bulk, int64_t elemId, int sideOrdinal)
{
  stk::mesh::Entity const elem = bulk.get_entity(stk::topology::ELEM_RANK, elemId);
  return get_side_entity_for_elem_side_pair(bulk, elem, sideOrdinal);
}

stk::mesh::Entity get_side_entity_for_elem_id_side_pair_of_rank(const stk::mesh::BulkData &bulk, int64_t elemId, int sideOrdinal, stk::mesh::EntityRank sideRank)
{
    stk::mesh::Entity const elem = bulk.get_entity(stk::topology::ELEM_RANK, elemId);
    return get_side_entity_for_elem_side_pair_of_rank(bulk, elem, sideOrdinal, sideRank);
}

size_t num_sides(const BulkData& mesh, Entity entity)
{
  stk::topology topo = mesh.bucket(entity).topology();
  if (!topo.has_mixed_rank_sides()) {
    return mesh.num_connectivity(entity, mesh.mesh_meta_data().side_rank());
  }

  STK_ThrowRequireMsg(mesh.mesh_meta_data().spatial_dimension() == 3,
    "ERROR, topo="<<topo<<" has mixed-rank-sides, but spatial-dim="<<mesh.mesh_meta_data().spatial_dimension());

  const size_t numSides = mesh.num_connectivity(entity, stk::topology::FACE_RANK)
                        + mesh.num_connectivity(entity, stk::topology::EDGE_RANK);
  return numSides;
}

void fill_sides(const BulkData& mesh, Entity entity, stk::mesh::EntityVector& sides)
{
  sides.clear();

  stk::topology topo = mesh.bucket(entity).topology();
  if (!topo.has_mixed_rank_sides()) {
    sides.insert(sides.begin(), mesh.begin(entity, mesh.mesh_meta_data().side_rank()),
                                mesh.end(entity, mesh.mesh_meta_data().side_rank()));
    return;
  }

  STK_ThrowRequireMsg(mesh.mesh_meta_data().spatial_dimension() == 3,
    "ERROR, topo="<<topo<<" has mixed-rank-sides, but spatial-dim="<<mesh.mesh_meta_data().spatial_dimension());

  sides.insert(sides.begin(), mesh.begin(entity, mesh.mesh_meta_data().side_rank()),
                              mesh.end(entity, mesh.mesh_meta_data().side_rank()));
  sides.insert(sides.end(), mesh.begin(entity, stk::topology::EDGE_RANK),
                            mesh.end(entity, stk::topology::EDGE_RANK));
}

stk::mesh::EntityVector get_sides(const BulkData& mesh, Entity entity)
{
  stk::mesh::EntityVector sides;
  fill_sides(mesh, entity, sides);
  return sides;
}

template<typename T>
void resize_side_info_vector(const BulkData & mesh, Entity elem,
                             const std::vector<EntityRank>& allSideRanks,
                             T& sideInfoVector)
{
  size_t numConn = 0;

  for(EntityRank rank : allSideRanks) {
    numConn += mesh.num_connectivity(elem, rank);
  }

  sideInfoVector.resize(numConn);
}

void fill_side_ordinals(const BulkData & mesh, Entity elem, bool sortOrdinals, std::vector<ConnectivityOrdinal>& connectedOrdinals)
{
  auto elemTopology = mesh.bucket(elem).topology();
  auto& meta = mesh.mesh_meta_data();

  connectedOrdinals.clear();

  if (!elemTopology.has_mixed_rank_sides()) {
    connectedOrdinals.insert(connectedOrdinals.begin(), mesh.begin_ordinals(elem, meta.side_rank()),
                                                        mesh.end_ordinals(elem, meta.side_rank()));
  } else {
    STK_ThrowRequireMsg(meta.spatial_dimension() == 3,
      "ERROR, topo="<<elemTopology<<" has mixed-rank-sides, but spatial-dim="<<meta.spatial_dimension());

    std::vector<EntityRank> allSideRanks(elemTopology.num_side_ranks());
    elemTopology.side_ranks(allSideRanks.data());

    resize_side_info_vector(mesh, elem, allSideRanks, connectedOrdinals);

    unsigned numConnOfRank = 0;
    for(EntityRank rank : allSideRanks) {
      std::transform(mesh.begin_ordinals(elem, rank), mesh.end_ordinals(elem, rank), &connectedOrdinals[numConnOfRank],
                     [&elemTopology, &rank](ConnectivityOrdinal localRankedOrdinal) {
                       return elemTopology.side_ordinal(localRankedOrdinal, rank);
                     });

      numConnOfRank += mesh.num_connectivity(elem, rank);
    }
  }

  if(sortOrdinals) {
    std::sort(connectedOrdinals.begin(), connectedOrdinals.end());
  }
}

std::vector<ConnectivityOrdinal> get_side_ordinals(const BulkData & mesh, Entity elem, bool sortOrdinals)
{
  std::vector<ConnectivityOrdinal> connectedOrdinals;
  fill_side_ordinals(mesh, elem, sortOrdinals, connectedOrdinals);
  return connectedOrdinals;
}

void fill_element_side_entries(const BulkData & mesh, Entity elem, bool sortEntries, std::vector<ElemSideOrdinal>& connectedElementSide)
{
  connectedElementSide.clear();

  auto elemTopology = mesh.bucket(elem).topology();
  auto& meta = mesh.mesh_meta_data();

  if (!elemTopology.has_mixed_rank_sides()) {
    auto ordinals = mesh.begin_ordinals(elem, meta.side_rank());
    auto sides = mesh.begin(elem, meta.side_rank());
    unsigned numConn = mesh.num_connectivity(elem, meta.side_rank());
    connectedElementSide.reserve(numConn);

    for(unsigned i=0; i<numConn; i++) {
      connectedElementSide.emplace_back(elem, sides[i], ordinals[i]);
    }
  } else {
    STK_ThrowRequireMsg(meta.spatial_dimension() == 3,
      "ERROR, topo="<<elemTopology<<" has mixed-rank-sides, but spatial-dim="<<meta.spatial_dimension());

    std::vector<EntityRank> allSideRanks(elemTopology.num_side_ranks());
    elemTopology.side_ranks(allSideRanks.data());

    resize_side_info_vector(mesh, elem, allSideRanks, connectedElementSide);

    unsigned numConnOfRank = 0;
    for(EntityRank rank : allSideRanks) {
      unsigned count = 0;
      auto sides = mesh.begin(elem, rank);
      std::transform(mesh.begin_ordinals(elem, rank), mesh.end_ordinals(elem, rank), &connectedElementSide[numConnOfRank],
                     [&elemTopology, &rank, &elem, &count, sides](ConnectivityOrdinal localRankedOrdinal) {
                       unsigned sideOrdinal = elemTopology.side_ordinal(localRankedOrdinal, rank);
                       return ElemSideOrdinal{elem, sides[count++], static_cast<ConnectivityOrdinal>(sideOrdinal)};
                     });

      numConnOfRank += mesh.num_connectivity(elem, rank);
    }
  }

  if(sortEntries) {
    std::sort(connectedElementSide.begin(), connectedElementSide.end());
  }
}

std::vector<ElemSideOrdinal> get_element_side_entries(const BulkData & mesh, Entity elem, bool sortEntries)
{
  std::vector<ElemSideOrdinal> connectedElementSide;
  fill_element_side_entries(mesh, elem, sortEntries, connectedElementSide);
  return connectedElementSide;
}

template<class IterType>
EntityId get_max_id(IterType begin, IterType end)
{
  EntityId maxId = 0;
  if constexpr (std::is_same_v<std::vector<std::pair<EntityKey,Entity>>::const_iterator, IterType>)
  {
    if (begin != end) {
      IterType lastEntityKeyEntityPair = --end;
      maxId = lastEntityKeyEntityPair->first.id();
    }
  }
  else
  {
    for(IterType iter = begin; iter != end; ++iter) {
      maxId = std::max(maxId, iter->first.id());
    } 
  }
  return maxId;
}

EntityId get_max_id_on_local_proc(const BulkData& bulk, EntityRank rank)
{
  return get_max_id(bulk.begin_entities(rank), bulk.end_entities(rank));
}

}
