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

#ifndef stk_search_CoarseSearch_hpp
#define stk_search_CoarseSearch_hpp

#include <stk_util/stk_config.h>
#include <stk_util/util/ReportHandler.hpp>
#include <stk_search/IdentProc.hpp>
#include <stk_search/BoundingBox.hpp>
#if defined(STK_HAVE_BOOSTLIB)
#include <stk_search/CoarseSearchBoostRTree.hpp>
#endif
#include <stk_search/CoarseSearchKdTree.hpp>
#include <stk_search/SearchMethod.hpp>
#include <vector>
#include <utility>


namespace stk { namespace search {

inline
std::ostream& operator<<(std::ostream &out, SearchMethod method)
{
  switch( method )   {
  case BOOST_RTREE:            out << "BOOST_RTREE"; break;
  case KDTREE:                 out << "KDTREE"; break;
  case MORTON_LINEARIZED_BVH:  out << "MORTON_LINEARIZED_BVH"; break;
  }
  return out;
}

// EXPERIMENTAL
//
// intersections returned will be those resulting from the (local) domain boxes
// intersecting range boxes from the entire distributed set.  NEEDS TO BE CALLED
// WITH EXPLICIT TEMPLATE ARGUMENTS.
template <typename DomainBox, typename DomainIdent, typename RangeBox, typename RangeIdent>
void coarse_search_nonIdentProc(
                    std::vector<std::pair<DomainBox,DomainIdent> > const& domain,
                    std::vector<std::pair<RangeBox,RangeIdent> >   const& range,
                    SearchMethod                                          method,
                    stk::ParallelMachine                                  comm,
                    std::vector< std::pair< DomainIdent, RangeIdent> > &  intersections
                  )
{
  switch( method )
  {
  case BOOST_RTREE:
#if defined(STK_HAVE_BOOSTLIB)
    coarse_search_boost_rtree_output_locally<DomainBox, DomainIdent, RangeBox, RangeIdent>(domain,range,comm,intersections);
#else
    ThrowRequireMsg(false,"ERROR, the BOOST_RTREE option in stk_search requires that Trilinos was configured with TPL_ENABLE_BoostLib:BOOL=ON");
#endif
    break;
  default:
    std::cerr << "coarse_search(..) interface used does not support std::search::coarse_search_nonIdentProc(..) yet" << method << std::endl;
    abort();
    break;
  }
}

// THIS MIGHT BE WHAT WE ACTUALLY WANT FOR THE INTERFACE.
template <typename DomainBox, typename DomainIdent, typename RangeBox, typename RangeIdent>
void coarse_search(
                    std::vector<std::pair<DomainBox,DomainIdent> > const& domain,
                    std::vector<std::pair<RangeBox, RangeIdent> >  const& range,
                    SearchMethod                                          method,
                    stk::ParallelMachine                                  comm,
                    std::vector< std::pair< IdentProc<DomainIdent, unsigned int>,
                                            IdentProc<RangeIdent, unsigned int> > > &  intersections
                  )
{
  std::cerr << "Future version of coarse_search called" << std::endl;
  abort();
}

// intersections will be those of distributed domain boxes associated with this
// processor rank via get_proc<DomainIdent>(.) that intersect distributed range
// boxes.  Optionally, also include intersections of distributed domain boxes
// with distributed range boxes associated with this processor rank via
// get_proc<RangeIdent>(.).
template <typename DomainBox, typename DomainIdent, typename RangeBox, typename RangeIdent>
void coarse_search( std::vector<std::pair<DomainBox,DomainIdent> > const& domain,
               std::vector<std::pair<RangeBox,RangeIdent> >   const& range,
               SearchMethod                                          method,
               stk::ParallelMachine                                  comm,
               std::vector< std::pair< DomainIdent, RangeIdent> > &  intersections,
               bool communicateRangeBoxInfo=true
             )
{
  switch( method )
  {
  case BOOST_RTREE:
#ifndef __NVCC__
#if defined(STK_HAVE_BOOSTLIB)
    coarse_search_boost_rtree(domain,range,comm,intersections,communicateRangeBoxInfo);
#else
    ThrowRequireMsg(false,"ERROR, the BOOST_RTREE option in stk_search requires that Trilinos was configured with TPL_ENABLE_BoostLib:BOOL=ON");
#endif
    break;
#endif
  case KDTREE:
    coarse_search_kdtree(domain,range,comm,intersections,communicateRangeBoxInfo);
    break;
  default:
    std::cerr << "coarse_search(..) interface used does not support SearchMethod " << method << std::endl;
    abort();
    break;
  }
}

}} // namespace stk::search

#endif // stk_search_CoarseSearch_hpp
