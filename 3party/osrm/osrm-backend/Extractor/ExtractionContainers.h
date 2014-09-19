/*

Copyright (c) 2013, Project OSRM, Dennis Luxen, others
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this list
of conditions and the following disclaimer.
Redistributions in binary form must reproduce the above copyright notice, this
list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef EXTRACTIONCONTAINERS_H_
#define EXTRACTIONCONTAINERS_H_

#include "InternalExtractorEdge.h"
#include "ExtractorStructs.h"
#include "../DataStructures/Restriction.h"
#include "../Util/FingerPrint.h"

#include <stxxl/vector>

class ExtractionContainers
{
#ifndef _MSC_VER
    constexpr static unsigned stxxl_memory = ((sizeof(std::size_t) == 4) ? std::numeric_limits<int>::max() : std::numeric_limits<unsigned>::max());
#else
    const static unsigned stxxl_memory = ((sizeof(std::size_t) == 4) ? INT_MAX : UINT_MAX);
#endif
  public:
    typedef stxxl::vector<NodeID> STXXLNodeIDVector;
    typedef stxxl::vector<ExternalMemoryNode> STXXLNodeVector;
    typedef stxxl::vector<InternalExtractorEdge> STXXLEdgeVector;
    typedef stxxl::vector<std::string> STXXLStringVector;
    typedef stxxl::vector<InputRestrictionContainer> STXXLRestrictionsVector;
    typedef stxxl::vector<WayIDStartAndEndEdge> STXXLWayIDStartEndVector;

    STXXLNodeIDVector used_node_id_list;
    STXXLNodeVector all_nodes_list;
    STXXLEdgeVector all_edges_list;
    STXXLStringVector name_list;
    STXXLRestrictionsVector restrictions_list;
    STXXLWayIDStartEndVector way_start_end_id_list;
    const FingerPrint fingerprint;

    ExtractionContainers();

    virtual ~ExtractionContainers();

    void PrepareData(const std::string &output_file_name,
                     const std::string &restrictions_file_name);
};

#endif /* EXTRACTIONCONTAINERS_H_ */