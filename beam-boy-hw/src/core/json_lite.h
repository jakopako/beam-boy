#pragma once

// Beam Boy — a deliberately tiny, deliberately strict JSON reader.
//
// This exists to read one thing: a cartridge's `meta.json`. That file is a flat
// object of string values and nothing else, so this parser accepts exactly that
// and rejects everything else -- no nesting, no numbers, no arrays, no bare
// literals.
//
// Being strict is the point rather than a shortcut. `meta.json` will eventually
// arrive from the games repo as community-submitted content, and the safest
// parser is one that cannot be talked into doing something interesting. There
// is no recursion here (so no stack to blow), every write is bounded by an
// explicit buffer size, and anything unexpected is a hard `false` instead of a
// best-effort guess. A malformed cartridge should fail to appear in the
// launcher, not half-appear with a garbled title.
//
// A full JSON library would also work, but would cost flash and would accept a
// much larger input space than the format actually needs.

#include <stddef.h>

namespace beamboy {
namespace json {

// Longest string value accepted. Comfortably above a game title; anything
// longer is rejected rather than truncated, so a too-long field is a visible
// failure rather than a silently mangled one.
constexpr size_t kMaxValueLength = 96;

// Called once per key/value pair, in document order. Both strings are NUL
// terminated and valid only for the duration of the call. Returning false
// aborts the parse and makes parseFlatObject() return false, which lets a
// caller reject a value it does not like (an over-long id, say) using the same
// failure path as a syntax error.
typedef bool (*PairHandler)(void* user, const char* key, const char* value);

// Parses a flat JSON object of string values, invoking handler for each pair.
//
// Returns false if the document is malformed, contains any value that is not a
// string, is nested, has trailing content after the closing brace, or if the
// handler rejects a pair. On false, some pairs may already have been reported;
// callers should treat a failed parse as "discard everything", which is what
// CartridgeStore does.
bool parseFlatObject(const char* text, PairHandler handler, void* user);

}  // namespace json
}  // namespace beamboy
