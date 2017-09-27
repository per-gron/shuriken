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

// Generates cpp gRPC service interface out of Protobuf IDL.
//

#include <memory>
#include <sstream>

#include "src/compiler/config.h"

#include "src/compiler/generator_helpers.h"
#include "src/compiler/protobuf_plugin.h"

#include "generator.h"

class CppGrpcGenerator : public grpc::protobuf::compiler::CodeGenerator {
 public:
  CppGrpcGenerator() {}
  virtual ~CppGrpcGenerator() {}

  virtual bool Generate(
      const grpc::protobuf::FileDescriptor *file,
      const std::string &parameter,
      grpc::protobuf::compiler::GeneratorContext *context,
      std::string *error) const {
    if (file->options().cc_generic_services()) {
      *error =
          "rs grpc proto compiler plugin does not work with generic "
          "services. To generate cpp grpc APIs, please set \""
          "cc_generic_service = false\".";
      return false;
    }

    rs_grpc_generator::Parameters generator_parameters;
    generator_parameters.use_system_headers = true;

    ProtoBufFile pbfile(file);

    if (!parameter.empty()) {
      std::vector<std::string> parameters_list =
          grpc_generator::tokenize(parameter, ",");
      for (auto parameter_string = parameters_list.begin();
           parameter_string != parameters_list.end(); parameter_string++) {
        std::vector<std::string> param =
            grpc_generator::tokenize(*parameter_string, "=");
        if (param[0] == "grpc_services_namespace") {
          generator_parameters.grpc_services_namespace = param[1];
        } else if (param[0] == "rs_services_namespace") {
          generator_parameters.rs_services_namespace = param[1];
        } else if (param[0] == "use_system_headers") {
          if (param[1] == "true") {
            generator_parameters.use_system_headers = true;
          } else if (param[1] == "false") {
            generator_parameters.use_system_headers = false;
          } else {
            *error = std::string("Invalid parameter: ") + *parameter_string;
            return false;
          }
        } else if (param[0] == "grpc_search_path") {
          generator_parameters.grpc_search_path = param[1];
        } else {
          *error = std::string("Unknown parameter: ") + *parameter_string;
          return false;
        }
      }
    }

    std::string file_name = grpc_generator::StripProto(file->name());

    std::string header_code =
        rs_grpc_generator::GetHeaderPrologue(&pbfile, generator_parameters) +
        rs_grpc_generator::GetHeaderIncludes(&pbfile, generator_parameters) +
        rs_grpc_generator::GetHeaderServices(&pbfile, generator_parameters) +
        rs_grpc_generator::GetHeaderEpilogue(&pbfile, generator_parameters);
    std::unique_ptr<grpc::protobuf::io::ZeroCopyOutputStream> header_output(
        context->Open(
            file_name + rs_grpc_generator::kRsGeneratorServiceHeaderExt));
    grpc::protobuf::io::CodedOutputStream header_coded_out(header_output.get());
    header_coded_out.WriteRaw(header_code.data(), header_code.size());

    std::string source_code =
        rs_grpc_generator::GetSourcePrologue(&pbfile, generator_parameters) +
        rs_grpc_generator::GetSourceIncludes(&pbfile, generator_parameters) +
        rs_grpc_generator::GetSourceServices(&pbfile, generator_parameters) +
        rs_grpc_generator::GetSourceEpilogue(&pbfile, generator_parameters);
    std::unique_ptr<grpc::protobuf::io::ZeroCopyOutputStream> source_output(
        context->Open(
            file_name + rs_grpc_generator::kRsGeneratorServiceSourceExt));
    grpc::protobuf::io::CodedOutputStream source_coded_out(source_output.get());
    source_coded_out.WriteRaw(source_code.data(), source_code.size());

    return true;
  }

 private:
  // Insert the given code into the given file at the given insertion point.
  void Insert(
      grpc::protobuf::compiler::GeneratorContext *context,
      const std::string &filename,
      const std::string &insertion_point,
      const std::string &code) const {
    std::unique_ptr<grpc::protobuf::io::ZeroCopyOutputStream> output(
        context->OpenForInsert(filename, insertion_point));
    grpc::protobuf::io::CodedOutputStream coded_out(output.get());
    coded_out.WriteRaw(code.data(), code.size());
  }
};

int main(int argc, char *argv[]) {
  CppGrpcGenerator generator;
  return grpc::protobuf::compiler::PluginMain(argc, argv, &generator);
}
