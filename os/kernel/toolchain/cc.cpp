#include "cc.h"
#include "../mem/kmalloc.h"
#include "../string.h"
#include "../terminal.h"

extern "C" void serial(const char *fmt, ...);

/*
 * Chrysalis Mini-Compiler
 * Supports:
 *  - print("text")
 *  - int var = value;
 *  - print_num(var1 + var2)
 */

typedef struct {
  uint8_t *buffer;
  size_t size;
  size_t capacity;
} code_gen_t;

typedef struct {
  char name[16];
  int stack_offset;
} variable_t;

static variable_t vars[16];
static int var_count = 0;

static void emit_byte(code_gen_t *g, uint8_t b) {
  if (g->size >= g->capacity)
    return;
  g->buffer[g->size++] = b;
}

static void emit_uint32(code_gen_t *g, uint32_t val) {
  emit_byte(g, val & 0xFF);
  emit_byte(g, (uint16_t)((val >> 8) & 0xFF));
  emit_byte(g, (uint16_t)((val >> 16) & 0xFF));
  emit_byte(g, (uint16_t)((val >> 24) & 0xFF));
}

int toolchain_compile_and_run(const char *source) {
  serial("[gcc] Starting compilation of source (len=%d)...\n", strlen(source));
  terminal_printf("[gcc] Compiling...\n");
  var_count = 0;

  code_gen_t gen;
  gen.capacity = 8192;
  gen.buffer = (uint8_t *)kmalloc(gen.capacity);
  gen.size = 0;

  if (!gen.buffer) {
    serial("[gcc] Error: Failed to allocate JIT buffer\n");
    return -1;
  }

  // Prologue: push ebp; mov ebp, esp; sub esp, 64 (space for vars)
  emit_byte(&gen, 0x55);
  emit_byte(&gen, 0x89);
  emit_byte(&gen, 0xE5);
  emit_byte(&gen, 0x83);
  emit_byte(&gen, 0xEC);
  emit_byte(&gen, 0x40);

  const char *p = source;
  while (*p) {
    while (*p &&
           (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ';'))
      p++;
    if (!*p)
      break;

    // print("...") or printf("...")
    if (strncmp(p, "print(\"", 7) == 0 || strncmp(p, "printf(\"", 8) == 0) {
      int skip = (strncmp(p, "printf(\"", 7) == 0) ? 8 : 7;
      p += skip;
      const char *start = p;
      while (*p && *p != '\"')
        p++;
      size_t len = p - start;
      char *str = (char *)kmalloc(len + 1);
      memcpy(str, start, len);
      str[len] = '\0';

      serial("[gcc] Emitting print: '%s' at 0x%x\n", str,
             (uint32_t)(uintptr_t)str);

      emit_byte(&gen, 0x68); // push <addr>
      emit_uint32(&gen, (uint32_t)(uintptr_t)str);

      uint32_t target_addr = (uint32_t)(uintptr_t)terminal_writestring;
      uint32_t current_addr = (uint32_t)(uintptr_t)gen.buffer + gen.size;

      emit_byte(&gen, 0xE8); // call
      emit_uint32(&gen, target_addr - (current_addr + 5));

      emit_byte(&gen, 0x83); // add esp, 4
      emit_byte(&gen, 0xC4);
      emit_byte(&gen, 0x04);

      if (*p == '\"')
        p++;
      while (*p && *p != ';')
        p++;
      if (*p == ';')
        p++;
    }
    // int x = 123;
    else if (strncmp(p, "int ", 4) == 0) {
      p += 4;
      while (*p == ' ')
        p++;
      const char *name_start = p;
      while (*p && *p != ' ' && *p != '=')
        p++;
      size_t name_len = p - name_start;
      if (name_len > 15)
        name_len = 15;
      strncpy(vars[var_count].name, name_start, name_len);
      vars[var_count].name[name_len] = '\0';
      vars[var_count].stack_offset = -((var_count + 1) * 4);

      serial("[gcc] Variable defined: %s at [ebp%d]\n", vars[var_count].name,
             vars[var_count].stack_offset);

      while (*p && *p != '=')
        p++;
      if (*p == '=')
        p++;
      while (*p && (*p == ' '))
        p++;
      int val = 0;
      while (*p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
      }

      // mov [ebp + offset], val (C7 45 <off> <val32>)
      emit_byte(&gen, 0xC7);
      emit_byte(&gen, 0x45);
      emit_byte(&gen, (uint8_t)vars[var_count].stack_offset);
      emit_uint32(&gen, val);

      var_count++;
      while (*p && *p != ';')
        p++;
      if (*p == ';')
        p++;
    }
    // print_num(x + y)
    else if (strncmp(p, "print_num(", 10) == 0) {
      p += 10;
      while (*p == ' ')
        p++;
      char n1[16], n2[16];
      int i = 0;
      while (*p && *p != ' ' && *p != '+' && *p != ')')
        n1[i++] = *p++;
      n1[i] = '\0';

      while (*p == ' ')
        p++;
      if (*p == '+') {
        p++;
        while (*p == ' ')
          p++;
        i = 0;
        while (*p && *p != ' ' && *p != ')')
          n2[i++] = *p++;
        n2[i] = '\0';

        serial("[gcc] Emitting print_num: %s + %s\n", n1, n2);

        // Load first var into EAX
        int off1 = 0;
        for (int v = 0; v < var_count; v++)
          if (strcmp(vars[v].name, n1) == 0)
            off1 = vars[v].stack_offset;
        emit_byte(&gen, 0x8B); // mov eax, [ebp + off1]
        emit_byte(&gen, 0x45);
        emit_byte(&gen, (uint8_t)off1);

        // Add second var to EAX
        int off2 = 0;
        for (int v = 0; v < var_count; v++)
          if (strcmp(vars[v].name, n2) == 0)
            off2 = vars[v].stack_offset;
        emit_byte(&gen, 0x03); // add eax, [ebp + off2]
        emit_byte(&gen, 0x45);
        emit_byte(&gen, (uint8_t)off2);
      } else {
        // Just one var
        serial("[gcc] Emitting print_num: %s\n", n1);
        int off1 = 0;
        for (int v = 0; v < var_count; v++)
          if (strcmp(vars[v].name, n1) == 0)
            off1 = vars[v].stack_offset;
        emit_byte(&gen, 0x8B);
        emit_byte(&gen, 0x45);
        emit_byte(&gen, (uint8_t)off1);
      }

      // Call terminal_writehex(eax)
      emit_byte(&gen, 0x50); // push eax
      uint32_t target_addr = (uint32_t)(uintptr_t)terminal_writehex;
      uint32_t current_addr = (uint32_t)(uintptr_t)gen.buffer + gen.size;

      emit_byte(&gen, 0xE8);
      emit_uint32(&gen, target_addr - (current_addr + 5));

      emit_byte(&gen, 0x83); // add esp, 4
      emit_byte(&gen, 0xC4);
      emit_byte(&gen, 0x04);

      // Newline after num
      emit_byte(&gen, 0x68);
      char *nl = (char *)kmalloc(2);
      nl[0] = '\n';
      nl[1] = '\0';
      emit_uint32(&gen, (uint32_t)(uintptr_t)nl);

      target_addr = (uint32_t)(uintptr_t)terminal_writestring;
      current_addr = (uint32_t)(uintptr_t)gen.buffer + gen.size;
      emit_byte(&gen, 0xE8);
      emit_uint32(&gen, target_addr - (current_addr + 5));

      emit_byte(&gen, 0x83);
      emit_byte(&gen, 0xC4);
      emit_byte(&gen, 0x04);

      while (*p && *p != ';')
        p++;
      if (*p == ';')
        p++;
    } else {
      p++;
    }
  }

  // Epilogue: mov esp, ebp; pop ebp; ret
  emit_byte(&gen, 0x89);
  emit_byte(&gen, 0xEC);
  emit_byte(&gen, 0x5D);
  emit_byte(&gen, 0xC3);

  serial("[gcc] Execution starting at 0x%x (size=%d bytes)...\n",
         (uint32_t)(uintptr_t)gen.buffer, gen.size);
  void (*func)() = (void (*)())gen.buffer;
  func();
  serial("[gcc] Execution finished.\n");

  // wm_mark_dirty(); // Force UI update if in GUI

  return 0;
}

void toolchain_init() {}
