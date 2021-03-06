/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tls_connection.hpp"
#include "tls_details.hpp"

namespace libp2p::connection {

  using Error = security::TlsError;
  using security::tls_details::log;

  namespace {
    template <typename Span>
    inline auto makeBuffer(Span s, size_t size) {
      return boost::asio::buffer(s.data(), size);
    }

    template <typename Span>
    inline auto makeBuffer(Span s) {
      return boost::asio::buffer(s.data(), s.size());
    }
  }  // namespace

  TlsConnection::TlsConnection(
      std::shared_ptr<RawConnection> raw_connection,
      std::shared_ptr<boost::asio::ssl::context> ssl_context,
      const peer::IdentityManager &idmgr, tcp_socket_t &tcp_socket,
      boost::optional<peer::PeerId> remote_peer)
      : local_peer_(idmgr.getId()),
        raw_connection_(std::move(raw_connection)),
        ssl_context_(std::move(ssl_context)),
        socket_(std::ref(tcp_socket), *ssl_context_),
        remote_peer_(std::move(remote_peer)) {}

  void TlsConnection::asyncHandshake(
      HandshakeCallback cb,
      std::shared_ptr<crypto::marshaller::KeyMarshaller> key_marshaller) {
    bool is_client = raw_connection_->isInitiator();

    socket_.async_handshake(is_client ? boost::asio::ssl::stream_base::client
                                      : boost::asio::ssl::stream_base::server,
                            [self = shared_from_this(), cb = std::move(cb),
                             key_marshaller = std::move(key_marshaller)](
                                const boost::system::error_code &error) {
                              self->onHandshakeResult(error, cb,
                                                      *key_marshaller);
                            });
  }

  void TlsConnection::onHandshakeResult(
      const boost::system::error_code &error, const HandshakeCallback &cb,
      const crypto::marshaller::KeyMarshaller &key_marshaller) {
    std::error_code ec = error;
    while (!ec) {
      X509 *cert = SSL_get_peer_certificate(socket_.native_handle());
      if (cert == nullptr) {
        ec = Error::TLS_NO_CERTIFICATE;
        break;
      }
      auto id_res = security::tls_details::verifyPeerAndExtractIdentity(
          cert, key_marshaller);
      if (!id_res) {
        ec = id_res.error();
        break;
      }
      auto &id = id_res.value();
      if (remote_peer_.has_value()) {
        if (remote_peer_.value() != id.peer_id) {
          log().debug("peer ids mismatch: expected={}, got={}",
                      remote_peer_.value().toBase58(), id.peer_id.toBase58());
          ec = Error::TLS_UNEXPECTED_PEER_ID;
          break;
        }
      } else {
        remote_peer_ = std::move(id.peer_id);
      }
      remote_pubkey_ = std::move(id.public_key);

      log().debug("handshake success for {}bound connection to {}",
                  (raw_connection_->isInitiator() ? "out" : "in"),
                  remote_peer_->toBase58());
      return cb(shared_from_this());
    }

    assert(ec);

    log().info("handshake error: {}", ec.message());
    if (auto close_res = close(); !close_res) {
      log().info("cannot close raw connection: {}",
                 close_res.error().message());
    }
    return cb(ec);
  }

  outcome::result<peer::PeerId> TlsConnection::localPeer() const {
    return local_peer_;
  }

  outcome::result<peer::PeerId> TlsConnection::remotePeer() const {
    if (!remote_peer_) {
      return Error::TLS_REMOTE_PEER_NOT_AVAILABLE;
    }
    return remote_peer_.value();
  }

  outcome::result<crypto::PublicKey> TlsConnection::remotePublicKey() const {
    if (!remote_pubkey_) {
      return Error::TLS_REMOTE_PUBKEY_NOT_AVAILABLE;
    }
    return remote_pubkey_.value();
  }

  bool TlsConnection::isInitiator() const noexcept {
    return raw_connection_->isInitiator();
  }

  outcome::result<multi::Multiaddress> TlsConnection::localMultiaddr() {
    return raw_connection_->localMultiaddr();
  }

  outcome::result<multi::Multiaddress> TlsConnection::remoteMultiaddr() {
    return raw_connection_->remoteMultiaddr();
  }

  template <typename Callback>
  auto closeOnError(TlsConnection &conn, Callback cb) {
    return [cb{std::move(cb)}, conn{conn.shared_from_this()}](auto &&ec,
                                                              auto &&result) {
      if (ec) {
        log().debug("connection async op error {}", ec.message());
        std::ignore = conn->close();
        return cb(std::forward<decltype(ec)>(ec));
      }
      cb(std::forward<decltype(result)>(result));
    };
  }

  void TlsConnection::read(gsl::span<uint8_t> out, size_t bytes,
                           Reader::ReadCallbackFunc f) {
    log().trace("reading {} bytes", bytes);
    boost::asio::async_read(socket_, makeBuffer(out, bytes),
                            closeOnError(*this, std::move(f)));
  }

  void TlsConnection::readSome(gsl::span<uint8_t> out, size_t bytes,
                               Reader::ReadCallbackFunc cb) {
    log().trace("reading some up to {} bytes", bytes);
    socket_.async_read_some(makeBuffer(out, bytes), closeOnError(*this, cb));
  }

  void TlsConnection::write(gsl::span<const uint8_t> in, size_t bytes,
                            Writer::WriteCallbackFunc cb) {
    log().trace("writing {} bytes", bytes);
    boost::asio::async_write(socket_, makeBuffer(in, bytes),
                             closeOnError(*this, cb));
  }

  void TlsConnection::writeSome(gsl::span<const uint8_t> in, size_t bytes,
                                Writer::WriteCallbackFunc cb) {
    log().trace("writing some up to {} bytes", bytes);
    socket_.async_write_some(makeBuffer(in, bytes), closeOnError(*this, cb));
  }

  bool TlsConnection::isClosed() const {
    return raw_connection_->isClosed();
  }

  outcome::result<void> TlsConnection::close() {
    return raw_connection_->close();
  }
}  // namespace libp2p::connection
