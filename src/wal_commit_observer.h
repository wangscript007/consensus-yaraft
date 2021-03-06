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

#pragma once

#include "base/simple_channel.h"
#include "base/status.h"

#include <silly/disallow_copying.h>

namespace consensus {

// WalCommitObserver observes updates of the committedIndex. If the Ready
// object generated by RawNode contains committedIndex updates, the observers
// will be informed, and all the registered listeners with ranges
// covering the new committedIndex will be notified.
//
// Thread-Safe
class WalCommitObserver {
  __DISALLOW_COPYING__(WalCommitObserver);

 public:
  WalCommitObserver();

  // ONLY the wal write thread is allowed to call this function.
  void Register(std::pair<uint64_t, uint64_t> range, SimpleChannel<Status> *channel);

  // ONLY the flusher thread is allowed to call this function.
  void Notify(uint64_t commitIndex);

  ~WalCommitObserver();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace consensus