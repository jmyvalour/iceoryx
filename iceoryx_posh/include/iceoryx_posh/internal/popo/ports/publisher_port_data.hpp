// Copyright (c) 2020 by Robert Bosch GmbH. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef IOX_POPO_PUBLISHER_PORT_DATA_HPP_
#define IOX_POPO_PUBLISHER_PORT_DATA_HPP_

#include "iceoryx_posh/capro/service_description.hpp"
#include "iceoryx_posh/iceoryx_posh_types.hpp"
#include "iceoryx_posh/internal/mepoo/memory_manager.hpp"
#include "iceoryx_posh/internal/popo/building_blocks/chunk_distributor_data.hpp"
#include "iceoryx_posh/internal/popo/building_blocks/chunk_sender_data.hpp"
#include "iceoryx_posh/internal/popo/ports/base_port_data.hpp"

#include <atomic>
#include <cstdint>

namespace iox
{
namespace popo
{
struct PublisherPortData : public BasePortData
{
    PublisherPortData(const capro::ServiceDescription& serviceDescription,
                      const ProcessName_t& processName,
                      mepoo::MemoryManager* const memoryManager,
                      uint64_t historyCapacity = 0u,
                      const mepoo::MemoryInfo& memoryInfo = mepoo::MemoryInfo()) noexcept
        : BasePortData(serviceDescription, processName)
        , m_chunkSenderData(memoryManager, historyCapacity, memoryInfo)
    {
    }

    using ChunkDistributorData_t =
        ChunkDistributorData<MAX_SUBSCRIBERS_PER_PUBLISHER, ThreadSafePolicy, ChunkQueuePusher>;
    ChunkSenderData<ChunkDistributorData_t> m_chunkSenderData;
    std::atomic_bool m_offeringRequested{false};
    std::atomic_bool m_offered{false};
};

} // namespace popo
} // namespace iox

#endif
