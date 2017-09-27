/*
 *
 * Copyright 2015 gRPC authors, 2017 Per Grön.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#pragma once

#include <memory>
#include <vector>

#include "src/compiler/config.h"
#include "src/compiler/schema_interface.h"

#include <string>

namespace rs_grpc_generator {

static const char* const kRsGeneratorServiceHeaderExt = ".rsgrpc.pb.h";
static const char* const kRsGeneratorServiceSourceExt = ".rsgrpc.pb.cc";

// Contains all the parameters that are parsed from the command line.
struct Parameters {
  // The namespace, if any, of the underlying non-rs gRPC services
  grpc::string grpc_services_namespace;
  // Puts the service into a namespace
  grpc::string rs_services_namespace;
  // Use system includes (<>) or local includes ("")
  bool use_system_headers;
  // Prefix to any grpc include
  grpc::string grpc_search_path;
};

// Return the prologue of the generated header file.
grpc::string GetHeaderPrologue(
    grpc_generator::File *file, const Parameters &params);

// Return the includes needed for generated header file.
grpc::string GetHeaderIncludes(
    grpc_generator::File *file, const Parameters &params);

// Return the includes needed for generated source file.
grpc::string GetSourceIncludes(
    grpc_generator::File *file, const Parameters &params);

// Return the epilogue of the generated header file.
grpc::string GetHeaderEpilogue(
    grpc_generator::File *file, const Parameters &params);

// Return the prologue of the generated source file.
grpc::string GetSourcePrologue(
    grpc_generator::File *file, const Parameters &params);

// Return the services for generated header file.
grpc::string GetHeaderServices(
    grpc_generator::File *file, const Parameters &params);

// Return the services for generated source file.
grpc::string GetSourceServices(
    grpc_generator::File *file, const Parameters &params);

// Return the epilogue of the generated source file.
grpc::string GetSourceEpilogue(
    grpc_generator::File *file, const Parameters &params);

}  // namespace rs_grpc_generator
