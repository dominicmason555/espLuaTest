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
