// Copyright 2017 Per Grön. All Rights Reserved.
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

#include <memory>

#include <google/bigtable/v2/bigtable.rsgrpc.pb.h>
#include <shk-store/api/shkstore.rsgrpc.pb.h>

namespace shk {

std::unique_ptr<Store> MakeStore(
    const std::shared_ptr<google::bigtable::v2::Bigtable> &bigtable);

}  // namespace shk
