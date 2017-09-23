# shk-index server

shk-index is a gRPC server that hosts the raw index with the data that makes it
possible to map build steps and input files to cached output files. Doing
lookups in this index is fairly expensive. The intention is that reads should be
done via the shk-cache service.
