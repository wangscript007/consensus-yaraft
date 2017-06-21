// Copyright 2017 Wu Tao
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

#include "rpc/raft_server.h"
#include "base/stl_container_utils.h"

#include <base/env.h>
#include <boost/algorithm/string/trim.hpp>

namespace consensus {
namespace rpc {

RaftServiceImpl::~RaftServiceImpl() {
  STLDeleteContainerPointers(peerMap_.begin(), peerMap_.end());
}

Status RaftServiceImpl::broadcast(const std::vector<yaraft::pb::Message> &mails) {
  for (const auto &m : mails) {
    CHECK(peerMap_.size() < m.to() && m.to() != 0);
    RETURN_NOT_OK(peerMap_[m.to()]->AsyncSend(m));
  }
  return Status::OK();
}

bool RaftServiceImpl::broadcastAllReadyMails(yaraft::Ready *rd) {
  auto st = broadcast(rd->messages);
  if (!st.IsOK()) {
    LOG(WARNING) << "RaftServiceImpl::broadcast: " << st;
  }
  rd->messages.clear();
  return st.IsOK();
}

static void printFlags() {
  FMT_LOG(INFO, "--wal_dir=\"{}\"", FLAGS_wal_dir);
  FMT_LOG(INFO, "--name=\"{}\"", FLAGS_name);
  FMT_LOG(INFO, "--heartbeat_interval={}ms", FLAGS_heartbeat_interval);
  FMT_LOG(INFO, "--election_timeout={}ms", FLAGS_election_timeout);
  FMT_LOG(INFO, "--initial_cluster=\"{}\"", FLAGS_initial_cluster);
}

RaftServiceImpl *RaftServiceImpl::New() {
  printFlags();

  FATAL_NOT_OK(Env::Default()->CreateDirIfMissing(FLAGS_wal_dir),
               fmt::format("create wal_dir=\"{}\" if missing", FLAGS_wal_dir));
  wal::WriteAheadLog *wal;
  auto memstore = new ::yaraft::MemoryStorage;
  FATAL_NOT_OK(wal::WriteAheadLog::Default(FLAGS_wal_dir, &wal, memstore),
               fmt::format("initialize data from wal_dir=\"{}\"", FLAGS_wal_dir));

  auto conf = new yaraft::Config;
  conf->storage = memstore;
  conf->electionTick = FLAGS_election_timeout;
  conf->heartbeatTick = FLAGS_heartbeat_interval;

  std::map<std::string, std::string> peerNameToIp;
  FATAL_NOT_OK(ParseClusterMembershipFromGFlags(&peerNameToIp), "--initial_cluster parse failed.");
  for (uint64_t i = 1; i <= peerNameToIp.size(); i++) {
    conf->peers.push_back(i);
  }

  auto server = new RaftServiceImpl();
  server->peerMap_.reserve(peerNameToIp.size());
  uint64_t id = 0;
  for (auto &e : peerNameToIp) {
    ++id;

    auto it = server->peerNameToId_.find(e.first);
    CHECK(it == server->peerNameToId_.end());
    server->peerNameToId_[e.first] = id;

    // there's no need to create a connection to itself.
    if (FLAGS_name != e.first) {
      FMT_SLOG(INFO, "New peer {name: \"%s\", url: \"%s\"} added.", e.first, e.second);
      server->peerMap_[id] = new Peer(server, e.second);
    } else {
      server->url_ = e.second;
    }
  }

  conf->id = server->peerNameToId_[FLAGS_name];

  auto node = new yaraft::RawNode(conf);
  server->node_.reset(node);
  server->wal_.reset(wal);
  server->storage_ = memstore;

  return server;
}

void RaftServiceImpl::Step(google::protobuf::RpcController *controller,
                           const yaraft::pb::Message *request, pb::Response *response,
                           google::protobuf::Closure *done) {
  sofa::pbrpc::RpcController *cntl = static_cast<sofa::pbrpc::RpcController *>(controller);
  FMT_LOG(INFO, "RaftService::Step(): message from {}: {}", cntl->RemoteAddress(),
          yaraft::DumpPB(*request));

  // handling request
  handle(*request, response);
  done->Run();
}

static pb::StatusCode yaraftErrorCodeToRpcStatusCode(yaraft::Error::ErrorCodes code) {
  switch (code) {
    case yaraft::Error::StepLocalMsg:
      return pb::StepLocalMsg;
    default:
      LOG(FATAL) << "Unexpected error code: " << yaraft::Error::ToString(code);
      return pb::OK;
  }
}

void RaftServiceImpl::handle(const yaraft::pb::Message &request, pb::Response *response) {
  auto s = node_->Step(const_cast<::yaraft::pb::Message &>(request));
  if (UNLIKELY(!s.IsOK())) {
    LOG(ERROR) << "RawNode::Step: " << s.ToString();
    response->set_code(yaraftErrorCodeToRpcStatusCode(s.Code()));
    return;
  }

  // immediately handle ready after performing an FSM transition
  // TODO(optimize) batching this
  yaraft::Ready *rd = node_->GetReady();
  handleReady(rd);
}

void RaftServiceImpl::handleReady(yaraft::Ready *rd) {
  using namespace yaraft::pb;

  if (!rd->entries.empty()) {
    FATAL_NOT_OK(wal_->AppendEntries(rd->entries), "WriteAheadLog::AppendEntries");
  }

  if (!rd->hardState) {
  }

  if (!rd->messages.empty()) {
    broadcastAllReadyMails(rd);
  }
}

}  // namespace rpc
}  // namespace consensus