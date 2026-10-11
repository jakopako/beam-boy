#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "berry.h"

void be_writebuffer(const char* buffer, size_t length) {
  fwrite(buffer, 1, length, stdout);
}

char* be_readstring(char* buffer, size_t size) {
  (void)buffer;
  (void)size;
  return NULL;
}

void* be_fopen(const char* filename, const char* modes) {
  return fopen(filename, modes);
}
int be_fclose(void* file) { return fclose(file); }
size_t be_fwrite(void* file, const void* buffer, size_t length) {
  return fwrite(buffer, 1, length, file);
}
size_t be_fread(void* file, void* buffer, size_t length) {
  return fread(buffer, 1, length, file);
}
char* be_fgets(void* file, void* buffer, int size) {
  return fgets(buffer, size, file);
}
int be_fseek(void* file, long offset) { return fseek(file, offset, SEEK_SET); }
long be_ftell(void* file) { return ftell(file); }
size_t be_fsize(void* file) {
  long position = ftell(file);
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, position, SEEK_SET);
  return (size_t)size;
}
int be_nfunc_open(bvm* vm) {
  be_raise(vm, "io_error", "file access unavailable in cartridge tests");
  return 0;
}

static int run(bvm* vm, const char* source) {
  int status = be_loadstring(vm, source);
  if (status == 0) status = be_pcall(vm, 0);
  if (status != 0) {
    for (int i = 1; i <= be_top(vm); i++) {
      fprintf(stderr, "%s\n", be_tostring(vm, i));
    }
  }
  be_pop(vm, be_top(vm));
  return status;
}

static int run_file(bvm* vm, const char* path) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    perror(path);
    return 1;
  }
  size_t size = be_fsize(file);
  char* source = malloc(size + 1);
  if (source == NULL) {
    fclose(file);
    fprintf(stderr, "cannot allocate cartridge source\n");
    return 1;
  }
  size_t read = fread(source, 1, size, file);
  fclose(file);
  if (read != size) {
    free(source);
    fprintf(stderr, "cannot read %s\n", path);
    return 1;
  }
  source[size] = '\0';
  int status = run(vm, source);
  free(source);
  return status;
}

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: test_cartridge <test.be> <game.be>\n");
    return 1;
  }
  bvm* vm = be_vm_new();
  if (vm == NULL) return 1;
  int status = run(vm, "var beam = nil");
  if (status == 0) status = run_file(vm, argv[2]);
  if (status == 0) status = run_file(vm, argv[1]);
  if (status == 0) status = run(vm, "test_cartridge()");
  be_vm_delete(vm);
  return status == 0 ? 0 : 1;
}
