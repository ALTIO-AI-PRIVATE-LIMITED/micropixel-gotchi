// SPDX-License-Identifier: Apache-2.0
//
// The few C library functions the ported AirPlay code (and bplist) calls. Guests link no
// libc: memory comes from the Guest runtime's allocator (alloc_bridge.cpp), and the
// formatter covers only the conversions this App uses (d i u x X c s p, with l/ll/z,
// zero padding, width and %.*s).
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void *agent_allocate(size_t size);
void agent_release(void *pointer);

// A size header in front of each block lets realloc() copy the old contents.
typedef struct {
  size_t size;
  size_t pad;  // keeps the block 8-byte aligned
} block_header;

void *malloc(size_t size) {
  block_header *h = agent_allocate(sizeof(block_header) + size);
  if (!h) return NULL;
  h->size = size;
  return h + 1;
}

void free(void *p) {
  if (p) agent_release((block_header *)p - 1);
}

void *calloc(size_t count, size_t size) {
  if (size && count > SIZE_MAX / size) return NULL;
  void *p = malloc(count * size);
  if (p) memset(p, 0, count * size);
  return p;
}

void *realloc(void *p, size_t size) {
  if (!p) return malloc(size);
  size_t old = ((block_header *)p - 1)->size;
  void *q = malloc(size);
  if (!q) return NULL;
  memcpy(q, p, old < size ? old : size);
  free(p);
  return q;
}

double fabs(double x) { return x < 0 ? -x : x; }

// playfair's modified MD5 only calls sin(1..64), for floor(2^32 * |sin(n)|): the
// standard MD5 constants (RFC 1321). Returning K + 0.5 over 2^32 makes that
// expression yield K exactly; any other argument is not used by this App.
double sin(double x) {
  static const uint32_t k[64] = {
      0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
      0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
      0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
      0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
      0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
      0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
      0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
      0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
  };
  int n = (int)x;
  if ((double)n != x || n < 1 || n > 64) __builtin_trap();
  return ((double)k[n - 1] + 0.5) / 4294967296.0;
}

int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) a++, b++;
  return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
  for (; n; n--, a++, b++) {
    if (*a != *b || !*a) return (unsigned char)*a - (unsigned char)*b;
  }
  return 0;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

int strcasecmp(const char *a, const char *b) {
  while (*a && lower((unsigned char)*a) == lower((unsigned char)*b)) a++, b++;
  return lower((unsigned char)*a) - lower((unsigned char)*b);
}

int strncasecmp(const char *a, const char *b, size_t n) {
  for (; n; n--, a++, b++) {
    int x = lower((unsigned char)*a), y = lower((unsigned char)*b);
    if (x != y || !x) return x - y;
  }
  return 0;
}

char *strchr(const char *s, int c) {
  for (;; s++) {
    if (*s == (char)c) return (char *)s;
    if (!*s) return NULL;
  }
}

char *strncpy(char *destination, const char *source, size_t n) {
  size_t i = 0;
  for (; i < n && source[i]; i++) destination[i] = source[i];
  for (; i < n; i++) destination[i] = 0;
  return destination;
}

char *strstr(const char *haystack, const char *needle) {
  size_t n = strlen(needle);
  for (; *haystack; haystack++)
    if (!strncmp(haystack, needle, n)) return (char *)haystack;
  return n ? NULL : (char *)haystack;
}

char *strtok_r(char *s, const char *delimiters, char **state) {
  if (!s) s = *state;
  if (!s) return NULL;
  while (*s && strchr(delimiters, *s)) s++;
  if (!*s) {
    *state = NULL;
    return NULL;
  }
  char *token = s;
  while (*s && !strchr(delimiters, *s)) s++;
  if (*s) {
    *s = 0;
    *state = s + 1;
  } else {
    *state = NULL;
  }
  return token;
}

unsigned long strtoul(const char *s, char **end, int base) {
  unsigned long value = 0;
  while (*s == ' ' || *s == '\t') s++;
  if (base == 0) base = 10;
  for (;; s++) {
    int digit = *s >= '0' && *s <= '9' ? *s - '0' : lower((unsigned char)*s) >= 'a' ? lower((unsigned char)*s) - 'a' + 10 : 99;
    if (digit >= base) break;
    value = value * (unsigned long)base + (unsigned long)digit;
  }
  if (end) *end = (char *)s;
  return value;
}

typedef struct {
  char *out;
  size_t cap, len;
} sink;

static void put(sink *s, char c) {
  if (s->len + 1 < s->cap) s->out[s->len] = c;
  s->len++;
}

static void put_number(sink *s, unsigned long long v, unsigned base, int upper, int negative, int width, int zero) {
  char digits[24];
  int n = 0;
  do {
    unsigned d = (unsigned)(v % base);
    digits[n++] = (char)(d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10);
    v /= base;
  } while (v);
  int total = n + (negative ? 1 : 0);
  if (negative && zero) put(s, '-');
  for (; total < width; total++) put(s, zero ? '0' : ' ');
  if (negative && !zero) put(s, '-');
  while (n) put(s, digits[--n]);
}

int vsnprintf(char *out, size_t cap, const char *fmt, va_list args) {
  sink s = {out, cap, 0};
  for (; *fmt; fmt++) {
    if (*fmt != '%') {
      put(&s, *fmt);
      continue;
    }
    fmt++;
    int zero = 0, width = 0, precision = -1, longs = 0;
    if (*fmt == '0') zero = 1, fmt++;
    while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
    if (*fmt == '.') {
      fmt++;
      if (*fmt == '*') {
        precision = va_arg(args, int);
        fmt++;
      } else {
        precision = 0;
        while (*fmt >= '0' && *fmt <= '9') precision = precision * 10 + (*fmt++ - '0');
      }
    }
    while (*fmt == 'l') longs++, fmt++;
    if (*fmt == 'z') longs = 1, fmt++;
    switch (*fmt) {
      case 'd':
      case 'i': {
        long long v = longs >= 2 ? va_arg(args, long long) : longs ? va_arg(args, long) : va_arg(args, int);
        put_number(&s, v < 0 ? 0ULL - (unsigned long long)v : (unsigned long long)v, 10, 0, v < 0, width, zero);
        break;
      }
      case 'u':
      case 'x':
      case 'X': {
        unsigned long long v = longs >= 2 ? va_arg(args, unsigned long long)
                               : longs    ? va_arg(args, unsigned long)
                                          : va_arg(args, unsigned);
        put_number(&s, v, *fmt == 'u' ? 10 : 16, *fmt == 'X', 0, width, zero);
        break;
      }
      case 'p':
        put_number(&s, (uintptr_t)va_arg(args, void *), 16, 0, 0, width, zero);
        break;
      case 'c':
        put(&s, (char)va_arg(args, int));
        break;
      case 's': {
        const char *v = va_arg(args, const char *);
        if (!v) v = "(null)";
        size_t n = strlen(v);
        if (precision >= 0 && (size_t)precision < n) n = (size_t)precision;
        for (size_t pad = n; pad < (size_t)width; pad++) put(&s, ' ');
        for (size_t i = 0; i < n; i++) put(&s, v[i]);
        break;
      }
      case '%':
        put(&s, '%');
        break;
      default:
        put(&s, '%');
        if (*fmt) put(&s, *fmt);
        else fmt--;
        break;
    }
  }
  if (cap) out[s.len < cap ? s.len : cap - 1] = 0;
  return (int)s.len;
}

int snprintf(char *out, size_t cap, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(out, cap, fmt, args);
  va_end(args);
  return n;
}
