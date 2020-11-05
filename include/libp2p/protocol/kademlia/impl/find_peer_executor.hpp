/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LIBP2P_PROTOCOL_KADEMLIA_FINDPEEREXECUTOR
#define LIBP2P_PROTOCOL_KADEMLIA_FINDPEEREXECUTOR

#include <memory>
#include <queue>
#include <unordered_set>

#include <libp2p/common/types.hpp>
#include <libp2p/host/host.hpp>
#include <libp2p/protocol/common/sublogger.hpp>
#include <libp2p/protocol/kademlia/common.hpp>
#include <libp2p/protocol/kademlia/config.hpp>
#include <libp2p/protocol/kademlia/impl/peer_info_with_distance.hpp>
#include <libp2p/protocol/kademlia/impl/response_handler.hpp>
#include <libp2p/protocol/kademlia/impl/session.hpp>
#include <libp2p/protocol/kademlia/impl/session_host.hpp>
#include <libp2p/protocol/kademlia/peer_routing.hpp>

namespace libp2p::protocol::kademlia {

  class FindPeerExecutor
      : public ResponseHandler,
        public std::enable_shared_from_this<FindPeerExecutor> {
   public:
    FindPeerExecutor(const Config &config, std::shared_ptr<Host> host,
                     std::shared_ptr<SessionHost> session_host,
                     std::shared_ptr<PeerRouting> peer_routing,
                     PeerId sought_peer_id,
                     std::unordered_set<PeerInfo> nearest_peer_infos,
                     FoundPeerInfoHandler handler);

    ~FindPeerExecutor() override;

    outcome::result<void> start();

    /// @see ResponseHandler::responseTimeout
    scheduler::Ticks responseTimeout() const override {
      return scheduler::toTicks(10s);  // TODO(xDimon): Need to read from config
    }

    /// @see ResponseHandler::match
    bool match(const Message &msg) const override;

    /// @see ResponseHandler::onResult
    void onResult(const std::shared_ptr<Session> &session,
                  outcome::result<Message> res) override;

   private:
    /// Spawns new request
    void spawn();

    /// Handles result of connection
    void onConnected(
        outcome::result<std::shared_ptr<connection::Stream>> stream_res);

    static std::atomic_size_t instance_number;

    // Primary
    const Config &config_;
    std::shared_ptr<Host> host_;
    std::shared_ptr<SessionHost> session_host_;
    std::shared_ptr<PeerRouting> peer_routing_;
    std::shared_ptr<Scheduler> scheduler_;

    // Secondary
    const PeerId sought_peer_id_;
    const ContentId sought_peer_id_as_content_id_;
    std::unordered_set<PeerInfo, std::hash<PeerInfo>, PeerInfo::EqualByPeerId>
        nearest_peer_infos_;
    FoundPeerInfoHandler handler_;

    std::shared_ptr<std::vector<uint8_t>> serialized_request_;
    std::priority_queue<PeerInfoWithDistance> queue_;
    size_t requests_in_progress_ = 0;
    bool started_ = false;
    bool done_ = false;

    SubLogger log_;
  };

}  // namespace libp2p::protocol::kademlia

#endif  // LIBP2P_PROTOCOL_KADEMLIA_FINDPEEREXECUTOR
