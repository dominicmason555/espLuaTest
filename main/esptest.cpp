#include "driver/usb_serial_jtag.h"
#include "esp_system.h"
#include "esp_vfs.h"
#include "esp_vfs_dev.h"
#include "esp_vfs_usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "stdio.h"
#include "tinyalloc.h"
#include <cstring>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

const size_t LUA_MEM_SIZE = 100000;
static uint8_t LUA_MEM[LUA_MEM_SIZE];

const size_t IN_BUF_SIZE = 512;
static char IN_BUF[IN_BUF_SIZE];

void *allocator(void *ud, void *ptr, size_t osize, size_t nsize) {
  // Free
  if (nsize == 0) {
    ta_free(ptr);
    return nullptr;
  }
  // Realloc
  else if (ptr != nullptr && nsize > osize) {
    void *new_ptr = ta_alloc(nsize);
    memcpy(new_ptr, ptr, osize);
    ta_free(ptr);
    return new_ptr;
  }
  // Malloc
  else if (ptr == nullptr && nsize > 0) {
    return ta_alloc(nsize);
  } else {
    return ptr;
  }
}

ssize_t capture_fs_write(int fd, const void *data, size_t size) {
  if (size > 1024) {
    printf("Write too big\n");
    return -1;
  }
  char buf[1024];
  for (int i = 0; i < size; i++) {
    buf[i] = ((char *)data)[i];
  }
  buf[size] = '\0';
  printf("Writing %zu bytes to fd %d: %s\n", size, fd, buf);
  return size;
}

ssize_t capture_fs_read(int fd, void *dst, size_t size) {
  printf("Reading %zu bytes from fd %d\n", size, fd);
  return size;
}

int capture_fs_open(const char *path, int flags, int mode) {
  static int fd = 0;
  fd++;
  printf("Opening path %s, fd is %d\n", path, fd);
  return fd;
}

int capture_fs_close(int fd) {
  printf("Closing %d\n", fd);
  return 0;
}

esp_vfs_t capture_fs = {
  flags : ESP_VFS_FLAG_DEFAULT,
  write : capture_fs_write, // Write function
  lseek : nullptr,          // Not implemented
  read : capture_fs_read,   // Read function
  pread : nullptr,          // Not implemented
  pwrite : nullptr,         // Not implemented
  open : capture_fs_open,   // Open function
  close : capture_fs_close, // Close function
  fstat : nullptr,          // Below: not implemented
  stat : nullptr,
  link : nullptr,
  unlink : nullptr,
  rename : nullptr,
  opendir : nullptr,
  readdir : nullptr,
  readdir_r : nullptr,
  telldir : nullptr,
  seekdir : nullptr,
  closedir : nullptr,
  mkdir : nullptr,
  rmdir : nullptr,
  fcntl : nullptr,
  ioctl : nullptr,
  fsync : nullptr,
  access : nullptr,
  truncate : nullptr,
  ftruncate : nullptr,
  utime : nullptr,
  tcsetattr : nullptr,
  tcgetattr : nullptr,
  tcdrain : nullptr,
  tcflush : nullptr,
  tcflow : nullptr,
  tcgetsid : nullptr,
  tcsendbreak : nullptr,
  start_select : nullptr,
  socket_select : nullptr,
  stop_socket_select : nullptr,
  stop_socket_select_isr : nullptr,
  get_socket_select_semaphore : nullptr,
  end_select : nullptr
};

void superloop(void) {
  printf("Hello, World!\n");
  ta_init(LUA_MEM, LUA_MEM + LUA_MEM_SIZE - 1, 512, 16, 4);
  printf("Num Free: %d, num used: %d, num fresh: %d\n", ta_num_free(),
         ta_num_used(), ta_num_fresh());

  printf("Creating state\n");
  lua_State *L = lua_newstate(allocator, nullptr);
  printf("Opening libs\n");
  luaL_openlibs(L);

  printf("Num Free: %d, num used: %d, num fresh: %d\n", ta_num_free(),
         ta_num_used(), ta_num_fresh());

  printf("Running string\n");
  auto res = luaL_dostring(L, "print('Hi from Lua')");
  if (res) {
    printf("Lua error %d: %s\n", res, lua_tostring(L, -1));
  }

  printf("Num Free: %d, num used: %d, num fresh: %d\n", ta_num_free(),
         ta_num_used(), ta_num_fresh());

  printf("========== C file handling\n");

  printf("Creating virtual FS\n");
  ESP_ERROR_CHECK(esp_vfs_register("/capture", &capture_fs, NULL));

  printf("Opening file\n");
  FILE *capture = fopen("/capture/file", "a+");
  printf("Writing to file\n");
  fputs("fputs to capture from C\n", capture);
  printf("Reading from file\n");
  uint8_t buf[10];
  fread(buf, 1, 1, capture);
  printf("Closing file\n");
  fclose(capture);

  printf("========== Lua file handling\n");

  printf("Opening file from Lua\n");
  res = luaL_dostring(L, "cap = io.open('/capture/lua_out', 'a+'); print(cap)");
  if (res) {
    printf("Lua error %d: %s\n", res, lua_tostring(L, -1));
  }
  printf("Setting the mode to line-buffered\n");
  res = luaL_dostring(L, "cap:setvbuf('line', 1024)");
  if (res) {
    printf("Lua error %d: %s\n", res, lua_tostring(L, -1));
  }
  printf("Writing to file from Lua\n");
  res = luaL_dostring(L, "print(cap:write('Writing to cap from Lua'))");
  if (res) {
    printf("Lua error %d: %s\n", res, lua_tostring(L, -1));
  }
  printf("Reading from file from Lua\n");
  res = luaL_dostring(L, "print(cap:read(1))");
  if (res) {
    printf("Lua error %d: %s\n", res, lua_tostring(L, -1));
  }

  printf("Entering REPL\n");

  while (true) {
    int buffer_pos = 0;
    bool doubling = false;
    while (buffer_pos < IN_BUF_SIZE - 1) {
      char ch = fgetc(stdin);
      if (ch == '\r' || ch == '\n') {
        break;
      } else if (ch != 0xFF) {
        if (doubling) {
          IN_BUF[buffer_pos++] = ch;
          doubling = false;
        } else {
          doubling = true;
        }
      }
      vTaskDelay(1);
    }
    if (buffer_pos > 0) {
      IN_BUF[buffer_pos] = '\0';
      puts(IN_BUF);
      if (strcmp(IN_BUF, ".q") == 0) {
        break;
      }
      auto res = luaL_dostring(L, IN_BUF);
      if (res) {
        printf("Lua error %d: %s\n", res, lua_tostring(L, -1));
      }
      printf("Num Free: %d, num used: %d, num fresh: %d\n", ta_num_free(),
             ta_num_used(), ta_num_fresh());
    }
    vTaskDelay(1);
  }

  printf("Closing state\n");
  lua_close(L);
  printf("Num Free: %d, num used: %d, num fresh: %d\n", ta_num_free(),
         ta_num_used(), ta_num_fresh());
}

extern "C" void app_main(void) { superloop(); }
