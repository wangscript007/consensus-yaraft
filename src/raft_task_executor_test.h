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

#include "base/testing.h"
#include "raft_task_executor.h"

#include <yaraft/yaraft.h>

namespace consensus {

class RaftTaskExecutorTest : public ::testing::Test {
 public:
  virtual void SetUp() override {
    conf_ = new yaraft::Config;
    conf_->id = 1;
    conf_->peers = {1, 2, 3};
    conf_->electionTick = 1000;
    conf_->heartbeatTick = 100;

    memstore_ = new yaraft::MemoryStorage;
    conf_->storage = memstore_;
    taskQueue_ = new TaskQueue;
  }

 protected:
  yaraft::Config *conf_;
  yaraft::MemoryStorage *memstore_;
  TaskQueue *taskQueue_;
};

}  // namespace consensus