/*
 * This file is structured after src/base/system.c, but with CKB-specific
 * implementations.
 * We use a separate file here so we don't need to patch the original file
 * too much.
 */
#include <base/system.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
#include <ckb_consts.h>
#include <ckb_syscall_apis.h>
#ifdef __cplusplus
}
#endif /* __cplusplus */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void dbg_break() {
#ifdef __GNUC__
  __builtin_trap();
#else
  abort();
#endif
}

void dbg_assert_imp(const char* filename, int line, int test, const char* msg) {
  if (!test) {
    dbg_msg("assert", "%s(%d): %s", filename, line, msg);
    dbg_break();
  }
}

void dbg_msg(const char* sys, const char* fmt, ...) {
  va_list args;
  char str[1024 * 4];
  char* msg;
  int len;

  str_format(str, sizeof(str), "[%s]: ", sys);

  len = str_length(str);
  msg = (char*)str + len;

  va_start(args, fmt);
#if defined(CONF_FAMILY_WINDOWS) && !defined(__GNUC__)
  _vsprintf_p(msg, sizeof(str) - len, fmt, args);
#else
  vsnprintf(msg, sizeof(str) - len, fmt, args);
#endif
  va_end(args);

  ckb_debug(str);
}

/*
 * Here we implement an pseudo IO interface that reads from witness data
 * only. Each witness will be named as "0", "1", "2", etc. using its index.
 * Only reading is allowed, writing halts immediately.
 */

struct IOINTERNAL {
  uint32_t length;
  uint32_t offset;
  uint32_t index;
};

IOHANDLE io_open(const char* filename, int flags) {
  dbg_assert(((flags & IOFLAG_WRITE) == 0) && ((flags & IOFLAG_APPEND) == 0),
             "ckb only supports read");
  uint32_t index = str_toint(filename);

  // Read data length
  size_t syscall_size = 0;
  int ret = ckb_load_witness(NULL, &syscall_size, 0, index, CKB_SOURCE_INPUT);
  if (ret != 0) {
    // Unknown witness
    return 0;
  }
  IOHANDLE h = (IOHANDLE)mem_alloc(sizeof(struct IOINTERNAL));
  h->length = syscall_size;
  h->offset = 0;
  h->index = index;
  return h;
}

unsigned io_read(IOHANDLE io, void* buffer, unsigned size) {
  size_t syscall_size = size;
  int ret = ckb_load_witness(buffer, &syscall_size, io->offset, io->index,
                             CKB_SOURCE_INPUT);
  dbg_assert(ret == 0, "load witness data");
  unsigned read = (unsigned)syscall_size;
  if (read > size) {
    read = size;
  }
  io->offset += read;
  return read;
}

int io_seek(IOHANDLE io, int offset, int origin) {
  switch (origin) {
    case IOSEEK_START: {
      io->offset = offset;
    } break;
    case IOSEEK_CUR: {
      io->offset += offset;
    } break;
    case IOSEEK_END: {
      io->offset = io->length + offset;
    } break;
    default: {
      return -1;
    } break;
  }
  return 0;
}

int io_close(IOHANDLE io) {
  // Close is a dummy operation for CKB
  (void)io;
  return 0;
}

unsigned io_write(IOHANDLE io, const void* buffer, unsigned size) {
  (void)io;
  (void)buffer;
  (void)size;

  dbg_assert(0, "ckb forbids writing!");
  return (unsigned)-1;
}

unsigned io_write_newline(IOHANDLE io) {
  (void)io;

  dbg_assert(0, "ckb forbids writing!");
  return (unsigned)-1;
}

void* mem_alloc(unsigned size) { return malloc(size); }

void mem_free(void* p) { free(p); }

void mem_copy(void* dest, const void* source, unsigned size) {
  memcpy(dest, source, size);
}

void mem_move(void* dest, const void* source, unsigned size) {
  memmove(dest, source, size);
}

void mem_zero(void* block, unsigned size) { memset(block, 0, size); }

int mem_comp(const void* a, const void* b, int size) {
  return memcmp(a, b, size);
}

int mem_has_null(const void* block, unsigned size) {
  const unsigned char* bytes = block;
  unsigned i;
  for (i = 0; i < size; i++) {
    if (bytes[i] == 0) {
      return 1;
    }
  }
  return 0;
}

int64 time_get() { return ckb_current_cycles(); }

int64 time_freq() { return 1000000; }

void str_append(char* dst, const char* src, int dst_size) {
  int s;
  int i = 0;
  dbg_assert(dst_size > 0, "dst_size invalid");
  s = str_length(dst);
  while (s < dst_size) {
    dst[s] = src[i];
    if (!src[i]) /* check for null termination */
      break;
    s++;
    i++;
  }

  dst[dst_size - 1] = 0; /* assure null termination */
}

void str_copy(char* dst, const char* src, int dst_size) {
  dbg_assert(dst_size > 0, "dst_size invalid");
  dst[0] = '\0';
  strncat(dst, src, dst_size - 1);
}

int str_length(const char* str) { return (int)strlen(str); }

void str_format(char* buffer, int buffer_size, const char* format, ...) {
  va_list ap;
  dbg_assert(buffer_size > 0, "buffer_size invalid");
  va_start(ap, format);

#if defined(CONF_FAMILY_WINDOWS) && !defined(__GNUC__)
  _vsprintf_p(buffer, buffer_size, format, ap);
#else
  vsnprintf(buffer, buffer_size, format, ap);
#endif

  va_end(ap);

  buffer[buffer_size - 1] = 0; /* assure null termination */
}

int str_span(const char* str, const char* set) { return strcspn(str, set); }

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
void str_timestamp_format(char* buffer, int buffer_size, const char* format) {
  str_format(buffer, buffer_size, "%lld", time_get());
}

void str_timestamp(char* buffer, int buffer_size) {
  str_timestamp_format(buffer, buffer_size, FORMAT_NOSPACE);
}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

char* str_skip_whitespaces(char* str) {
  while (*str && (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r'))
    str++;
  return str;
}

/* case */
int str_comp_nocase(const char* a, const char* b) {
#if defined(CONF_FAMILY_WINDOWS) && !defined(__GNUC__)
  return _stricmp(a, b);
#else
  return strcasecmp(a, b);
#endif
}

int str_comp_nocase_num(const char* a, const char* b, const int num) {
#if defined(CONF_FAMILY_WINDOWS) && !defined(__GNUC__)
  return _strnicmp(a, b, num);
#else
  return strncasecmp(a, b, num);
#endif
}

int str_comp(const char* a, const char* b) { return strcmp(a, b); }

int str_comp_num(const char* a, const char* b, const int num) {
  return strncmp(a, b, num);
}

const char* str_startswith_nocase(const char* str, const char* prefix) {
  int prefixl = str_length(prefix);
  if (str_comp_nocase_num(str, prefix, prefixl) == 0) {
    return str + prefixl;
  } else {
    return 0;
  }
}

const char* str_startswith(const char* str, const char* prefix) {
  int prefixl = str_length(prefix);
  if (str_comp_num(str, prefix, prefixl) == 0) {
    return str + prefixl;
  } else {
    return 0;
  }
}

const char* str_endswith_nocase(const char* str, const char* suffix) {
  int strl = str_length(str);
  int suffixl = str_length(suffix);
  const char* strsuffix;
  if (strl < suffixl) {
    return 0;
  }
  strsuffix = str + strl - suffixl;
  if (str_comp_nocase(strsuffix, suffix) == 0) {
    return strsuffix;
  } else {
    return 0;
  }
}

const char* str_endswith(const char* str, const char* suffix) {
  int strl = str_length(str);
  int suffixl = str_length(suffix);
  const char* strsuffix;
  if (strl < suffixl) {
    return 0;
  }
  strsuffix = str + strl - suffixl;
  if (str_comp(strsuffix, suffix) == 0) {
    return strsuffix;
  } else {
    return 0;
  }
}

const char* str_find_nocase(const char* haystack, const char* needle) {
  while (*haystack) /* native implementation */
  {
    const char* a = haystack;
    const char* b = needle;
    while (*a && *b && tolower(*a) == tolower(*b)) {
      a++;
      b++;
    }
    if (!(*b)) return haystack;
    haystack++;
  }

  return 0;
}

int str_toint(const char* str) { return atoi(str); }
float str_tofloat(const char* str) { return atof(str); }

/* makes sure that the string only contains the characters between 32 and 255 */
void str_sanitize_cc(char* str_in) {
  unsigned char* str = (unsigned char*)str_in;
  while (*str) {
    if (*str < 32) *str = ' ';
    str++;
  }
}

/* check if the string contains '..' (parent directory) paths */
int str_path_unsafe(const char* str) {
  // State machine. 0 means that we're at the beginning
  // of a new directory/filename, and a positive number represents the number of
  // dots ('.') we found. -1 means we encountered a different character
  // since the last path separator (or the beginning of the string).
  int parse_counter = 0;
  while (*str) {
    if (*str == '\\' || *str == '/') {
      // A path separator. Check how many dots we found since
      // the last path separator.
      //
      // Two dots => ".." contained in the path. Return an
      // error.
      if (parse_counter == 2)
        return -1;
      else
        parse_counter = 0;
    } else if (parse_counter >= 0) {
      // If we have not encountered a non-dot character since
      // the last path separator, count the dots.
      if (*str == '.')
        parse_counter++;
      else
        parse_counter = -1;
    }

    ++str;
  }
  // If there's a ".." at the end, fail too.
  if (parse_counter == 2) return -1;
  return 0;
}

/* makes sure that the string only contains the characters between 32 and 255 +
 * \r\n\t */
void str_sanitize(char* str_in) {
  unsigned char* str = (unsigned char*)str_in;
  while (*str) {
    if (*str < 32 && !(*str == '\r') && !(*str == '\n') && !(*str == '\t'))
      *str = ' ';
    str++;
  }
}

const char* str_skip_whitespaces_const(const char* str) {
  while (*str && (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r'))
    str++;
  return str;
}

char* str_skip_to_whitespace(char* str) {
  while (*str && (*str != ' ' && *str != '\t' && *str != '\n')) str++;
  return str;
}

int str_utf8_encode(char* ptr, int chr) {
  /* encode */
  if (chr <= 0x7F) {
    ptr[0] = (char)chr;
    return 1;
  } else if (chr <= 0x7FF) {
    ptr[0] = 0xC0 | ((chr >> 6) & 0x1F);
    ptr[1] = 0x80 | (chr & 0x3F);
    return 2;
  } else if (chr <= 0xFFFF) {
    ptr[0] = 0xE0 | ((chr >> 12) & 0x0F);
    ptr[1] = 0x80 | ((chr >> 6) & 0x3F);
    ptr[2] = 0x80 | (chr & 0x3F);
    return 3;
  } else if (chr <= 0x10FFFF) {
    ptr[0] = 0xF0 | ((chr >> 18) & 0x07);
    ptr[1] = 0x80 | ((chr >> 12) & 0x3F);
    ptr[2] = 0x80 | ((chr >> 6) & 0x3F);
    ptr[3] = 0x80 | (chr & 0x3F);
    return 4;
  }

  return 0;
}

int str_utf8_decode(const char** ptr) {
  const char* buf = *ptr;
  int ch = 0;

  do {
    if ((*buf & 0x80) == 0x0) /* 0xxxxxxx */
    {
      ch = (unsigned char)*buf;
      buf++;
    } else if ((*buf & 0xE0) == 0xC0) /* 110xxxxx */
    {
      ch = (*buf++ & 0x3F) << 6;
      if (!(*buf) || (*buf & 0xC0) != 0x80) break;
      ch += (*buf++ & 0x3F);
      if (ch < 0x80 || ch > 0x7FF) ch = -1;
    } else if ((*buf & 0xF0) == 0xE0) /* 1110xxxx */
    {
      ch = (*buf++ & 0x1F) << 12;
      if (!(*buf) || (*buf & 0xC0) != 0x80) break;
      ch += (*buf++ & 0x3F) << 6;
      if (!(*buf) || (*buf & 0xC0) != 0x80) break;
      ch += (*buf++ & 0x3F);
      if (ch < 0x800 || ch > 0xFFFF) ch = -1;
    } else if ((*buf & 0xF8) == 0xF0) /* 11110xxx */
    {
      ch = (*buf++ & 0x0F) << 18;
      if (!(*buf) || (*buf & 0xC0) != 0x80) break;
      ch += (*buf++ & 0x3F) << 12;
      if (!(*buf) || (*buf & 0xC0) != 0x80) break;
      ch += (*buf++ & 0x3F) << 6;
      if (!(*buf) || (*buf & 0xC0) != 0x80) break;
      ch += (*buf++ & 0x3F);
      if (ch < 0x10000 || ch > 0x10FFFF) ch = -1;
    } else {
      /* invalid */
      buf++;
      break;
    }

    *ptr = buf;
    return ch;
  } while (0);

  /* out of bounds */
  *ptr = buf;
  return -1;
}

int str_utf8_check(const char* str) {
  while (*str) {
    if ((*str & 0x80) == 0x0)
      str++;
    else if ((*str & 0xE0) == 0xC0 && (*(str + 1) & 0xC0) == 0x80)
      str += 2;
    else if ((*str & 0xF0) == 0xE0 && (*(str + 1) & 0xC0) == 0x80 &&
             (*(str + 2) & 0xC0) == 0x80)
      str += 3;
    else if ((*str & 0xF8) == 0xF0 && (*(str + 1) & 0xC0) == 0x80 &&
             (*(str + 2) & 0xC0) == 0x80 && (*(str + 3) & 0xC0) == 0x80)
      str += 4;
    else
      return 0;
  }
  return 1;
}

void str_utf8_copy_num(char* dst, const char* src, int dst_size, int num) {
  int new_cursor;
  int cursor = 0;
  dbg_assert(dst_size > 0, "dst_size invalid");

  while (src[cursor] && num > 0) {
    new_cursor = str_utf8_forward(src, cursor);
    if (new_cursor >= dst_size)  // reserve 1 byte for the null termination
      break;
    else
      cursor = new_cursor;
    --num;
  }

  str_copy(dst, src, cursor < dst_size ? cursor + 1 : dst_size);
}

int str_utf8_is_whitespace(int code) {
  // check if unicode is not empty
  if (code > 0x20 && code != 0xA0 && code != 0x034F &&
      (code < 0x2000 || code > 0x200F) && (code < 0x2028 || code > 0x202F) &&
      (code < 0x205F || code > 0x2064) && (code < 0x206A || code > 0x206F) &&
      code != 0x3000 && (code < 0xFE00 || code > 0xFE0F) && code != 0xFEFF &&
      (code < 0xFFF9 || code > 0xFFFC)) {
    return 0;
  }
  return 1;
}

const char* str_utf8_skip_whitespaces(const char* str) {
  const char* str_old;
  int code;

  while (*str) {
    str_old = str;
    code = str_utf8_decode(&str);

    if (!str_utf8_is_whitespace(code)) {
      return str_old;
    }
  }

  return str;
}

void str_utf8_trim_whitespaces_right(char* str) {
  int cursor = str_length(str);
  const char* last = str + cursor;
  while (str_utf8_is_whitespace(str_utf8_decode(&last))) {
    str[cursor] = 0;
    cursor = str_utf8_rewind(str, cursor);
    last = str + cursor;
    if (cursor == 0) {
      break;
    }
  }
}

static int str_utf8_isstart(char c) {
  if ((c & 0xC0) == 0x80) /* 10xxxxxx */
    return 0;
  return 1;
}

int str_utf8_rewind(const char* str, int cursor) {
  while (cursor) {
    cursor--;
    if (str_utf8_isstart(*(str + cursor))) break;
  }
  return cursor;
}

int str_utf8_forward(const char* str, int cursor) {
  const char* buf = str + cursor;
  if (!buf[0]) return cursor;

  if ((*buf & 0x80) == 0x0) /* 0xxxxxxx */
    return cursor + 1;
  else if ((*buf & 0xE0) == 0xC0) /* 110xxxxx */
  {
    if (!buf[1]) return cursor + 1;
    return cursor + 2;
  } else if ((*buf & 0xF0) == 0xE0) /* 1110xxxx */
  {
    if (!buf[1]) return cursor + 1;
    if (!buf[2]) return cursor + 2;
    return cursor + 3;
  } else if ((*buf & 0xF8) == 0xF0) /* 11110xxx */
  {
    if (!buf[1]) return cursor + 1;
    if (!buf[2]) return cursor + 2;
    if (!buf[3]) return cursor + 3;
    return cursor + 4;
  }

  /* invalid */
  return cursor + 1;
}

// RNG algorithm is borrowed from musl libc
static uint64_t g_vm_rand_state = 1;

int vm_rand() {
  g_vm_rand_state = 6364136223846793005ULL * g_vm_rand_state + 1;
  return g_vm_rand_state >> 33;
}

void vm_srand(int seed) { g_vm_rand_state = seed - 1; }
