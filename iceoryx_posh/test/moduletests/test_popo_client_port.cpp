// Copyright (c) 2021 by Apex.AI Inc. All rights reserved.
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
//
// SPDX-License-Identifier: Apache-2.0

#include "iceoryx_posh/internal/popo/ports/client_port_data.hpp"
#include "iceoryx_posh/internal/popo/ports/client_port_roudi.hpp"
#include "iceoryx_posh/internal/popo/ports/client_port_user.hpp"

#include "iceoryx_hoofs/testing/watch_dog.hpp"
#include "iceoryx_posh/internal/mepoo/memory_manager.hpp"
#include "iceoryx_posh/mepoo/mepoo_config.hpp"

#include "test.hpp"

namespace
{
using namespace ::testing;
using namespace iox::popo;

class ClientPort_test : public Test
{
    // keep this the very first and also private
    iox::cxx::GenericRAII m_uniqueRouDiId{[] { iox::popo::internal::setUniqueRouDiId(0U); },
                                          [] { iox::popo::internal::unsetUniqueRouDiId(); }};

    static constexpr iox::units::Duration DEADLOCK_TIMEOUT{5_s};
    Watchdog m_deadlockWatchdog{DEADLOCK_TIMEOUT};

    struct SutClientPort
    {
        SutClientPort(const iox::capro::ServiceDescription& serviceDescription,
                      const iox::RuntimeName_t& runtimeName,
                      const ClientOptions& clientOptions,
                      iox::mepoo::MemoryManager& memoryManager)
            : portData(serviceDescription, runtimeName, clientOptions, &memoryManager)
        {
        }

        ClientPortData portData;
        ClientPortUser portUser{portData};
        ClientPortRouDi portRouDi{portData};
        ChunkQueuePusher<ClientChunkQueueData_t> chunkQueuePusher{&portData.m_chunkReceiverData};
    };

  public:
    ClientPort_test()
    {
        iox::mepoo::MePooConfig mempoolconf;
        mempoolconf.addMemPool({CHUNK_SIZE, NUM_CHUNKS});
        m_memoryManager.configureMemoryManager(mempoolconf, m_memoryAllocator, m_memoryAllocator);
    }

    void SetUp() override
    {
        m_deadlockWatchdog.watchAndActOnFailure([] { std::terminate(); });

        // this is basically what RouDi does when a client is requested
        tryAdvanceToState(clientPortWithConnectOnCreate, iox::ConnectionState::CONNECTED);
        tryAdvanceToState(clientPortWithoutConnectOnCreate, iox::ConnectionState::NOT_CONNECTED);
    }

    void TearDown() override
    {
    }

    void tryAdvanceToState(SutClientPort& clientPort, const iox::ConnectionState state)
    {
        auto maybeCaProMessage = clientPort.portRouDi.tryGetCaProMessage();
        if (state == iox::ConnectionState::NOT_CONNECTED && clientPort.portData.m_connectionState == state)
        {
            return;
        }

        ASSERT_TRUE(maybeCaProMessage.has_value());
        auto& clientMessage = maybeCaProMessage.value();
        ASSERT_THAT(clientMessage.m_type, Eq(iox::capro::CaproMessageType::CONNECT));
        ASSERT_THAT(clientMessage.m_chunkQueueData, Ne(nullptr));
        ASSERT_THAT(clientPort.portData.m_connectionState, Eq(iox::ConnectionState::CONNECT_REQUESTED));
        if (clientPort.portData.m_connectionState == state)
        {
            return;
        }

        iox::capro::CaproMessage serverMessage{
            iox::capro::CaproMessageType::ACK, m_serviceDescription, iox::capro::CaproMessageSubType::NOSUBTYPE};
        serverMessage.m_chunkQueueData = &serverChunkQueueData;
        clientPort.portRouDi.dispatchCaProMessageAndGetPossibleResponse(serverMessage);
        ASSERT_THAT(clientPort.portData.m_connectionState, Eq(iox::ConnectionState::CONNECTED));
        if (clientPort.portData.m_connectionState == state)
        {
            return;
        }

        constexpr bool NOT_IMPLEMENTED{true};
        ASSERT_FALSE(NOT_IMPLEMENTED);
    }

    uint32_t getNumberOfUsedChunks() const
    {
        return m_memoryManager.getMemPoolInfo(0U).m_usedChunks;
    }

    iox::mepoo::SharedChunk getChunkFromMemoryManager(uint32_t userPayloadSize, uint32_t userHeaderSize)
    {
        auto chunkSettingsResult = iox::mepoo::ChunkSettings::create(userPayloadSize,
                                                                     iox::CHUNK_DEFAULT_USER_PAYLOAD_ALIGNMENT,
                                                                     userHeaderSize,
                                                                     iox::CHUNK_DEFAULT_USER_PAYLOAD_ALIGNMENT);
        iox::cxx::Expects(!chunkSettingsResult.has_error());
        return m_memoryManager.getChunk(chunkSettingsResult.value());
    }

    /// @return true if all pushes succeed, false if a push failed and a chunk was lost
    bool pushResponses(ChunkQueuePusher<ClientChunkQueueData_t>& chunkQueuePusher, uint64_t numberOfPushes)
    {
        for (auto i = 0U; i < numberOfPushes; ++i)
        {
            constexpr uint32_t USER_PAYLOAD_SIZE{10};
            auto sharedChunk = getChunkFromMemoryManager(USER_PAYLOAD_SIZE, sizeof(ResponseHeader));
            if (!chunkQueuePusher.push(sharedChunk))
            {
                chunkQueuePusher.lostAChunk();
                return false;
            }
        }
        return true;
    }

    static constexpr uint64_t QUEUE_CAPACITY{4};

  private:
    static constexpr uint32_t NUM_CHUNKS = 1024U;
    static constexpr uint32_t CHUNK_SIZE = 128U;
    static constexpr size_t MEMORY_SIZE = 1024U * 1024U;
    uint8_t m_memory[MEMORY_SIZE];
    iox::posix::Allocator m_memoryAllocator{m_memory, MEMORY_SIZE};
    iox::mepoo::MemoryManager m_memoryManager;

    iox::capro::ServiceDescription m_serviceDescription{"hyp", "no", "toad"};
    iox::RuntimeName_t m_runtimeName{"hypnotoad"};

    ClientOptions m_withConnectOnCreate = [&] {
        ClientOptions options;
        options.connectOnCreate = true;
        options.responseQueueCapacity = QUEUE_CAPACITY;
        return options;
    }();
    ClientOptions m_withoutConnectOnCreate = [] {
        ClientOptions options;
        options.connectOnCreate = false;
        options.responseQueueCapacity = QUEUE_CAPACITY;
        return options;
    }();

    ClientOptions m_clientOptionWith = [&] {
        ClientOptions options;
        options.connectOnCreate = true;
        options.responseQueueCapacity = QUEUE_CAPACITY;
        return options;
    }();

    ServerChunkQueueData_t serverChunkQueueData{iox::popo::QueueFullPolicy::DISCARD_OLDEST_DATA,
                                                iox::cxx::VariantQueueTypes::SoFi_MultiProducerSingleConsumer};

  public:
    static constexpr uint32_t USER_PAYLOAD_SIZE{32U};
    static constexpr uint32_t USER_PAYLOAD_ALIGNMENT{8U};

    ChunkQueuePopper<ServerChunkQueueData_t> serverRequestQueue{&serverChunkQueueData};

    SutClientPort clientPortWithConnectOnCreate{
        m_serviceDescription, m_runtimeName, m_withConnectOnCreate, m_memoryManager};
    SutClientPort clientPortWithoutConnectOnCreate{
        m_serviceDescription, m_runtimeName, m_withoutConnectOnCreate, m_memoryManager};
};
constexpr iox::units::Duration ClientPort_test::DEADLOCK_TIMEOUT;


/// @todo iox-#27 do tests related to QueueFullPolicy in integration test with a real ServerPort and add a note in this
/// file that those tests are part of the integration test

// BEGIN ClientPortUser tests

TEST_F(ClientPort_test, InitialConnectionStateOnPortWithConnectOnCreateIs_CONNECTED)
{
    auto& sut = clientPortWithConnectOnCreate;
    EXPECT_THAT(sut.portUser.getConnectionState(), Eq(iox::ConnectionState::CONNECTED));
}

TEST_F(ClientPort_test, InitialConnectionStateOnPortWithoutConnectOnCreateIs_NOT_CONNECTED)
{
    auto& sut = clientPortWithoutConnectOnCreate;
    EXPECT_THAT(sut.portUser.getConnectionState(), Eq(iox::ConnectionState::NOT_CONNECTED));
}

TEST_F(ClientPort_test, AllocateRequestDoesNotFailAndUsesTheMempool)
{
    auto& sut = clientPortWithConnectOnCreate;
    EXPECT_THAT(getNumberOfUsedChunks(), Eq(0U));

    auto maybeRequest = sut.portUser.allocateRequest(USER_PAYLOAD_SIZE, USER_PAYLOAD_ALIGNMENT);
    ASSERT_FALSE(maybeRequest.has_error());

    EXPECT_THAT(getNumberOfUsedChunks(), Eq(1U));
}

TEST_F(ClientPort_test, FreeRequestWithNullptrCallsErrorHandler)
{
    auto& sut = clientPortWithConnectOnCreate;

    iox::cxx::optional<iox::Error> detectedError;
    auto errorHandlerGuard = iox::ErrorHandler::SetTemporaryErrorHandler(
        [&](const iox::Error error, const std::function<void()>, const iox::ErrorLevel errorLevel) {
            detectedError.emplace(error);
            EXPECT_EQ(errorLevel, iox::ErrorLevel::SEVERE);
        });

    sut.portUser.freeRequest(nullptr);

    ASSERT_TRUE(detectedError.has_value());
    EXPECT_EQ(detectedError.value(), iox::Error::kPOPO__CLIENT_PORT_INVALID_REQUEST_TO_FREE_FROM_USER);
}

TEST_F(ClientPort_test, FreeRequestWithValidRequestWorksAndReleasesTheChunkToTheMempool)
{
    auto& sut = clientPortWithConnectOnCreate;
    sut.portUser.allocateRequest(USER_PAYLOAD_SIZE, USER_PAYLOAD_ALIGNMENT)
        .and_then([&](auto& requestHeader) {
            EXPECT_THAT(this->getNumberOfUsedChunks(), Eq(1U));
            sut.portUser.freeRequest(requestHeader);
            EXPECT_THAT(this->getNumberOfUsedChunks(), Eq(0U));
        })
        .or_else([&](auto&) {
            constexpr bool UNREACHABLE{false};
            EXPECT_TRUE(UNREACHABLE);
        });
}

TEST_F(ClientPort_test, SendRequestWithNullptrOnConnectedClientPortTerminates)
{
    auto& sut = clientPortWithConnectOnCreate;

    iox::cxx::optional<iox::Error> detectedError;
    auto errorHandlerGuard = iox::ErrorHandler::SetTemporaryErrorHandler(
        [&](const iox::Error error, const std::function<void()>, const iox::ErrorLevel errorLevel) {
            detectedError.emplace(error);
            EXPECT_EQ(errorLevel, iox::ErrorLevel::SEVERE);
        });

    sut.portUser.allocateRequest(USER_PAYLOAD_SIZE, USER_PAYLOAD_ALIGNMENT)
        .and_then([&](auto&) { sut.portUser.sendRequest(nullptr); })
        .or_else([&](auto&) {
            constexpr bool UNREACHABLE{false};
            EXPECT_TRUE(UNREACHABLE);
        });

    ASSERT_TRUE(detectedError.has_value());
    EXPECT_EQ(detectedError.value(), iox::Error::kPOPO__CLIENT_PORT_INVALID_REQUEST_TO_SEND_FROM_USER);
}

TEST_F(ClientPort_test, SendRequestOnConnectedClientPortEnqueuesRequestToServerQueue)
{
    constexpr int64_t SEQUENCE_ID{42U};
    auto& sut = clientPortWithConnectOnCreate;
    sut.portUser.allocateRequest(USER_PAYLOAD_SIZE, USER_PAYLOAD_ALIGNMENT)
        .and_then([&](auto& requestHeader) {
            requestHeader->setSequenceId(SEQUENCE_ID);
            sut.portUser.sendRequest(requestHeader);
        })
        .or_else([&](auto&) {
            constexpr bool UNREACHABLE{false};
            EXPECT_TRUE(UNREACHABLE);
        });

    serverRequestQueue.tryPop()
        .and_then([&](auto& sharedChunk) {
            auto requestHeader = static_cast<RequestHeader*>(sharedChunk.getChunkHeader()->userHeader());
            EXPECT_THAT(requestHeader->getSequenceId(), Eq(SEQUENCE_ID));
        })
        .or_else([&] {
            constexpr bool UNREACHABLE{false};
            EXPECT_TRUE(UNREACHABLE);
        });
}

TEST_F(ClientPort_test, SendRequestOnNotConnectedClientPortDoesNotEnqueuesRequestToServerQueue)
{
    auto& sut = clientPortWithoutConnectOnCreate;
    sut.portUser.allocateRequest(USER_PAYLOAD_SIZE, USER_PAYLOAD_ALIGNMENT)
        .and_then([&](auto& requestHeader) { sut.portUser.sendRequest(requestHeader); })
        .or_else([&](auto&) {
            constexpr bool UNREACHABLE{false};
            EXPECT_TRUE(UNREACHABLE);
        });

    EXPECT_FALSE(serverRequestQueue.tryPop().has_value());
}

TEST_F(ClientPort_test, ConnectAfterPreviousSendRequestCallDoesNotEnqueuesRequestToServerQueue)
{
    auto& sut = clientPortWithoutConnectOnCreate;
    sut.portUser.allocateRequest(USER_PAYLOAD_SIZE, USER_PAYLOAD_ALIGNMENT)
        .and_then([&](auto& requestHeader) { sut.portUser.sendRequest(requestHeader); })
        .or_else([&](auto&) {
            constexpr bool UNREACHABLE{false};
            EXPECT_TRUE(UNREACHABLE);
        });

    sut.portUser.connect();
    tryAdvanceToState(sut, iox::ConnectionState::CONNECTED);

    EXPECT_FALSE(serverRequestQueue.tryPop().has_value());
}

TEST_F(ClientPort_test, GetResponseOnNotConnectedClientPortHasNoResponse)
{
    auto& sut = clientPortWithoutConnectOnCreate;
    sut.portUser.getResponse()
        .and_then([&](auto&) {
            constexpr bool UNREACHABLE{false};
            EXPECT_TRUE(UNREACHABLE);
        })
        .or_else([&](auto& err) { EXPECT_THAT(err, Eq(iox::popo::ChunkReceiveResult::NO_CHUNK_AVAILABLE)); });
}

TEST_F(ClientPort_test, GetResponseOnConnectedClientPortWithEmptyResponseQueueHasNoResponse)
{
    auto& sut = clientPortWithConnectOnCreate;
    sut.portUser.getResponse()
        .and_then([&](auto&) {
            constexpr bool UNREACHABLE{false};
            EXPECT_TRUE(UNREACHABLE);
        })
        .or_else([&](auto& err) { EXPECT_THAT(err, Eq(iox::popo::ChunkReceiveResult::NO_CHUNK_AVAILABLE)); });
}

TEST_F(ClientPort_test, GetResponseOnConnectedClientPortWithNonEmptyResponseQueueHasResponse)
{
    constexpr int64_t SEQUENCE_ID{13U};
    auto& sut = clientPortWithConnectOnCreate;

    constexpr uint32_t USER_PAYLOAD_SIZE{10};
    auto sharedChunk = getChunkFromMemoryManager(USER_PAYLOAD_SIZE, sizeof(ResponseHeader));
    new (sharedChunk.getChunkHeader()->userHeader())
        ResponseHeader(iox::UniquePortId(), RpcBaseHeader::UNKNOWN_CLIENT_QUEUE_INDEX, SEQUENCE_ID);
    sut.chunkQueuePusher.push(sharedChunk);

    sut.portUser.getResponse()
        .and_then([&](auto& responseHeader) { EXPECT_THAT(responseHeader->getSequenceId(), Eq(SEQUENCE_ID)); })
        .or_else([&](auto&) {
            constexpr bool UNREACHABLE{false};
            EXPECT_TRUE(UNREACHABLE);
        });
}

TEST_F(ClientPort_test, ReleaseResponseWithNullptrIsTerminating)
{
    auto& sut = clientPortWithConnectOnCreate;

    iox::cxx::optional<iox::Error> detectedError;
    auto errorHandlerGuard = iox::ErrorHandler::SetTemporaryErrorHandler(
        [&](const iox::Error error, const std::function<void()>, const iox::ErrorLevel errorLevel) {
            detectedError.emplace(error);
            EXPECT_EQ(errorLevel, iox::ErrorLevel::SEVERE);
        });

    sut.portUser.releaseResponse(nullptr);

    ASSERT_TRUE(detectedError.has_value());
    EXPECT_EQ(detectedError.value(), iox::Error::kPOPO__CLIENT_PORT_INVALID_RESPONSE_TO_RELEASE_FROM_USER);
}

TEST_F(ClientPort_test, ReleaseResponseWithValidResponseReleasesChunkToTheMempool)
{
    auto& sut = clientPortWithConnectOnCreate;

    constexpr uint32_t USER_PAYLOAD_SIZE{10};

    iox::cxx::optional<iox::mepoo::SharedChunk> sharedChunk{
        getChunkFromMemoryManager(USER_PAYLOAD_SIZE, sizeof(ResponseHeader))};
    sut.chunkQueuePusher.push(sharedChunk.value());
    sharedChunk.reset();

    sut.portUser.getResponse()
        .and_then([&](auto& responseHeader) {
            EXPECT_THAT(this->getNumberOfUsedChunks(), Eq(1U));
            sut.portUser.releaseResponse(responseHeader);
            EXPECT_THAT(this->getNumberOfUsedChunks(), Eq(0U));
        })
        .or_else([&](auto&) {
            constexpr bool UNREACHABLE{false};
            EXPECT_TRUE(UNREACHABLE);
        });
}

TEST_F(ClientPort_test, HasNewResponseOnEmptyResponseQueueReturnsFalse)
{
    auto& sut = clientPortWithConnectOnCreate;
    EXPECT_FALSE(sut.portUser.hasNewResponses());
}

TEST_F(ClientPort_test, HasNewResponseOnNonEmptyResponseQueueReturnsTrue)
{
    auto& sut = clientPortWithConnectOnCreate;

    constexpr uint32_t USER_PAYLOAD_SIZE{10};
    auto sharedChunk = getChunkFromMemoryManager(USER_PAYLOAD_SIZE, sizeof(ResponseHeader));
    sut.chunkQueuePusher.push(sharedChunk);

    EXPECT_TRUE(sut.portUser.hasNewResponses());
}

TEST_F(ClientPort_test, HasNewResponseOnEmptyResponseQueueAfterPreviouslyNotEmptyReturnsFalse)
{
    auto& sut = clientPortWithConnectOnCreate;

    constexpr uint32_t USER_PAYLOAD_SIZE{10};
    auto sharedChunk = getChunkFromMemoryManager(USER_PAYLOAD_SIZE, sizeof(ResponseHeader));
    sut.chunkQueuePusher.push(sharedChunk);

    EXPECT_FALSE(sut.portUser.getResponse().has_error());

    EXPECT_FALSE(sut.portUser.hasNewResponses());
}

TEST_F(ClientPort_test, HasLostResponsesSinceLastCallWithoutLosingResponsesReturnsFalse)
{
    auto& sut = clientPortWithConnectOnCreate;
    EXPECT_FALSE(sut.portUser.hasLostResponsesSinceLastCall());
}

TEST_F(ClientPort_test, HasLostResponsesSinceLastCallWithoutLosingResponsesAndQueueFullReturnsFalse)
{
    auto& sut = clientPortWithConnectOnCreate;

    EXPECT_TRUE(pushResponses(sut.chunkQueuePusher, QUEUE_CAPACITY));
    EXPECT_FALSE(sut.portUser.hasLostResponsesSinceLastCall());
}

TEST_F(ClientPort_test, HasLostResponsesSinceLastCallWithLosingResponsesReturnsTrue)
{
    auto& sut = clientPortWithConnectOnCreate;

    EXPECT_FALSE(pushResponses(sut.chunkQueuePusher, QUEUE_CAPACITY + 1U));
    EXPECT_TRUE(sut.portUser.hasLostResponsesSinceLastCall());
}

TEST_F(ClientPort_test, HasLostResponsesSinceLastCallReturnsFalseAfterPreviouslyReturningTrue)
{
    auto& sut = clientPortWithConnectOnCreate;

    EXPECT_FALSE(pushResponses(sut.chunkQueuePusher, QUEUE_CAPACITY + 1U));
    EXPECT_TRUE(sut.portUser.hasLostResponsesSinceLastCall());
    EXPECT_FALSE(sut.portUser.hasLostResponsesSinceLastCall());
}

TEST_F(ClientPort_test, ConditionVariableInitiallyNotSet)
{
    auto& sut = clientPortWithConnectOnCreate;
    EXPECT_FALSE(sut.portUser.isConditionVariableSet());
}

TEST_F(ClientPort_test, SettingConditionVariableWithoutConditionVariablePresentWorks)
{
    iox::popo::ConditionVariableData condVar{"hypnotoad"};
    constexpr uint32_t NOTIFICATION_INDEX{1};

    auto& sut = clientPortWithConnectOnCreate;
    sut.portUser.setConditionVariable(condVar, NOTIFICATION_INDEX);

    EXPECT_TRUE(sut.portUser.isConditionVariableSet());
}

TEST_F(ClientPort_test, UnsettingConditionVariableWithConditionVariablePresentWorks)
{
    iox::popo::ConditionVariableData condVar{"brain slug"};
    constexpr uint32_t NOTIFICATION_INDEX{2};

    auto& sut = clientPortWithConnectOnCreate;
    sut.portUser.setConditionVariable(condVar, NOTIFICATION_INDEX);

    sut.portUser.unsetConditionVariable();

    EXPECT_FALSE(sut.portUser.isConditionVariableSet());
}

TEST_F(ClientPort_test, UnsettingConditionVariableWithoutConditionVariablePresentIsHandledGracefully)
{
    auto& sut = clientPortWithConnectOnCreate;
    sut.portUser.unsetConditionVariable();

    EXPECT_FALSE(sut.portUser.isConditionVariableSet());
}

TEST_F(ClientPort_test, ConnectOnNotConnectedClientPortResultsInStateChange)
{
    auto& sut = clientPortWithoutConnectOnCreate;

    sut.portUser.connect();

    EXPECT_TRUE(sut.portRouDi.tryGetCaProMessage().has_value());
}

TEST_F(ClientPort_test, ConnectOnConnectedClientPortResultsInNoStateChange)
{
    auto& sut = clientPortWithConnectOnCreate;

    sut.portUser.connect();

    EXPECT_FALSE(sut.portRouDi.tryGetCaProMessage().has_value());
}

TEST_F(ClientPort_test, DisconnectOnConnectedClientPortResultsInStateChange)
{
    auto& sut = clientPortWithConnectOnCreate;

    sut.portUser.disconnect();

    EXPECT_TRUE(sut.portRouDi.tryGetCaProMessage().has_value());
}

TEST_F(ClientPort_test, DisconnectOnNotConnectedClientPortResultsInNoStateChange)
{
    auto& sut = clientPortWithoutConnectOnCreate;

    sut.portUser.disconnect();

    EXPECT_FALSE(sut.portRouDi.tryGetCaProMessage().has_value());
}

// END ClientPortUser tests

// BEGIN ClientPortRouDi tests

// BEGIN Valid transitions


// END Valid transitions

// BEGIN Invalid transitions

/// @todo

// END Invalid transitions

// END ClientPortRouDi tests

} // namespace
