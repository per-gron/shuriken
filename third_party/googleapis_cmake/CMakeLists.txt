shk_proto(
  LIB_NAME google-api-http-proto
  PROTO_FILE google/api/http.proto
  GOOGLEAPI)

shk_proto(
  LIB_NAME google-api-annotations-proto
  PROTO_FILE google/api/annotations.proto
  DEPENDENCIES google-api-http-proto
  GOOGLEAPI)

shk_proto(
  LIB_NAME google-rpc-status-proto
  PROTO_FILE google/rpc/status.proto
  GOOGLEAPI)

shk_proto(
  LIB_NAME google-bigtable-v2-data-proto
  PROTO_FILE google/bigtable/v2/data.proto
  GOOGLEAPI)

shk_proto(
  LIB_NAME google-bigtable-v2-proto
  PROTO_FILE google/bigtable/v2/bigtable.proto
  DEPENDENCIES
    google-api-annotations-proto
    google-bigtable-v2-data-proto
    google-rpc-status-proto
  GRPC
  GOOGLEAPI)
