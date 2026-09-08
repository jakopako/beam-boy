/********************************************************************
** Beam Boy — Berry host port (Arduino/ESP32).
**
** Minimal implementation of the be_sys.h contract, in place of upstream's
** default/be_port.c (which targets a POSIX host with stdio and a real
** filesystem, neither of which applies here).
**
** Berry cartridges load from memory via be_loadstring()/be_loadbuffer(),
** never from a file, so the be_f*() family below is never actually called
** in this firmware -- BE_USE_FILE_SYSTEM is 0 and be_filelib.c (the `file`
** builtin) is excluded from the build entirely (see library.json). These
** stubs exist only because be_exec.c (module search path) and
** be_bytecode.c (save/load) reference them unconditionally; they fail
** safely rather than touch a filesystem Beam Boy does not give scripts.
********************************************************************/
#include "berry.h"
#include "be_sys.h"

#include <Arduino.h>

extern "C" {

BERRY_API void be_writebuffer(const char *buffer, size_t length) {
  Serial.write(reinterpret_cast<const uint8_t*>(buffer), length);
}

BERRY_API char* be_readstring(char *buffer, size_t size) {
  // No interactive stdin on a game console. input() is not part of the
  // Beam API surface a cartridge sees, so this is never reached.
  (void)buffer;
  (void)size;
  return nullptr;
}

void* be_fopen(const char *filename, const char *modes) {
  (void)filename;
  (void)modes;
  return nullptr;
}

int be_fclose(void *hfile) {
  (void)hfile;
  return 0;
}

size_t be_fwrite(void *hfile, const void *buffer, size_t length) {
  (void)hfile;
  (void)buffer;
  (void)length;
  return 0;
}

size_t be_fread(void *hfile, void *buffer, size_t length) {
  (void)hfile;
  (void)buffer;
  (void)length;
  return 0;
}

char* be_fgets(void *hfile, void *buffer, int size) {
  (void)hfile;
  (void)buffer;
  (void)size;
  return nullptr;
}

int be_fseek(void *hfile, long offset) {
  (void)hfile;
  (void)offset;
  return -1;
}

long int be_ftell(void *hfile) {
  (void)hfile;
  return 0;
}

size_t be_fsize(void *hfile) {
  (void)hfile;
  return 0;
}

}  // extern "C"

// be_baselib.c's precompiled builtins table always references open() as a
// global builtin regardless of BE_USE_FILE_SYSTEM (the const-object table is
// generated once by coc and baked into generate/be_fixed_m_builtin.h). Since
// be_filelib.c -- which implements it -- is excluded from this build (see
// library.json), stub it here so cartridges see a clean runtime error rather
// than the console failing to link at all.
extern "C" int be_nfunc_open(bvm *vm) {
  be_raise(vm, "io_error", "file access is not available on Beam Boy");
  return 0;
}


