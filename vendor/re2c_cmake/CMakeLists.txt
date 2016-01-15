set(RE2C_DIR ../re2c/re2c)
set(RE2C_SRC ${RE2C_DIR}/src)

add_executable(re2c
  ${RE2C_SRC}/codegen/bitmap.cc
  ${RE2C_SRC}/codegen/emit_action.cc
  ${RE2C_SRC}/codegen/emit_dfa.cc
  ${RE2C_SRC}/codegen/label.cc
  ${RE2C_SRC}/codegen/prepare_dfa.cc
  ${RE2C_SRC}/codegen/go_construct.cc
  ${RE2C_SRC}/codegen/go_destruct.cc
  ${RE2C_SRC}/codegen/go_emit.cc
  ${RE2C_SRC}/codegen/go_used_labels.cc
  ${RE2C_SRC}/codegen/input_api.cc
  ${RE2C_SRC}/codegen/output.cc
  ${RE2C_SRC}/codegen/print.cc
  ${RE2C_SRC}/codegen/scc.cc
  ${RE2C_SRC}/codegen/skeleton/control_flow.cc
  ${RE2C_SRC}/codegen/skeleton/generate_code.cc
  ${RE2C_SRC}/codegen/skeleton/generate_data.cc
  ${RE2C_SRC}/codegen/skeleton/match_empty.cc
  ${RE2C_SRC}/codegen/skeleton/maxlen.cc
  ${RE2C_SRC}/codegen/skeleton/skeleton.cc
  ${RE2C_SRC}/codegen/skeleton/unreachable.cc
  ${RE2C_SRC}/codegen/skeleton/way.cc
  ${RE2C_SRC}/conf/msg.cc
  ${RE2C_SRC}/conf/opt.cc
  ${RE2C_SRC}/conf/warn.cc
  ${RE2C_SRC}/ir/bytecode/bytecode.cc
  ${RE2C_SRC}/ir/bytecode/ins.cc
  ${RE2C_SRC}/ir/bytecode/charset.cc
  ${RE2C_SRC}/ir/bytecode/split.cc
  ${RE2C_SRC}/ir/bytecode/compile.cc
  ${RE2C_SRC}/ir/bytecode/calc_size.cc
  ${RE2C_SRC}/ir/dfa/dfa.cc
  ${RE2C_SRC}/ir/regexp/display.cc
  ${RE2C_SRC}/ir/regexp/encoding/enc.cc
  ${RE2C_SRC}/ir/regexp/encoding/range_suffix.cc
  ${RE2C_SRC}/ir/regexp/encoding/utf8/utf8_regexp.cc
  ${RE2C_SRC}/ir/regexp/encoding/utf8/utf8_range.cc
  ${RE2C_SRC}/ir/regexp/encoding/utf8/utf8.cc
  ${RE2C_SRC}/ir/regexp/encoding/utf16/utf16_regexp.cc
  ${RE2C_SRC}/ir/regexp/encoding/utf16/utf16.cc
  ${RE2C_SRC}/ir/regexp/encoding/utf16/utf16_range.cc
  ${RE2C_SRC}/ir/regexp/fixed_length.cc
  ${RE2C_SRC}/ir/regexp/regexp.cc
  ${RE2C_SRC}/ir/rule_rank.cc
  ${RE2C_SRC}/main.cc
  ${RE2C_SRC}/parse/code.cc
  ${RE2C_SRC}/parse/input.cc
  ${RE2C_SRC}/parse/scanner.cc
  ${RE2C_SRC}/parse/unescape.cc
  ${RE2C_SRC}/util/s_to_n32_unsafe.cc
  ${RE2C_SRC}/util/range.cc

  ${RE2C_DIR}/bootstrap/src/parse/lex.cc
  ${RE2C_DIR}/bootstrap/src/parse/lex_conf.cc
  ${RE2C_DIR}/bootstrap/src/conf/parse_opts.cc
  ${RE2C_DIR}/bootstrap/src/parse/parser.cc)


target_include_directories(re2c PRIVATE ${RE2C_DIR})
target_include_directories(re2c PRIVATE .)
