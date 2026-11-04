#ifndef DETENT_READER_H
#define DETENT_READER_H
/* libdetent_reader: the reader core's C ABI (docs/design/reader.md D1; docs/mechanisms/reader-core.md).
 * Lookups write `fallback` to *out for any status but DETENT_OK, never throw, and never allocate once
 * warm. Lookups are thread-safe on one handle; detent_close must not race any other call on it. No
 * function is async-signal-safe. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define DETENT_ABI_VERSION 1
typedef struct detent_reader detent_reader;
typedef struct { const char *data; size_t size; } detent_str;   /* data == NULL: absent */
typedef enum detent_status {
  DETENT_OK = 0, DETENT_MISSING = 1, DETENT_WRONG_TYPE = 2, DETENT_INVALID_NAME = 3,
  DETENT_NOT_OPENED = 4,       /* namespace outside the handle's list */
  DETENT_SOURCE_FAILURE = 5,   /* the generation failed verification */
  DETENT_UNAVAILABLE = 6,      /* no prepared generation for this namespace yet */
  DETENT_UNTRUSTED = 7,        /* ownership, mode, ACL or ancestry refused */
  DETENT_INVALID_ARGUMENT = 8, DETENT_NO_MEMORY = 9, DETENT_IO = 10
} detent_status;               /* any value other than DETENT_OK, known or not, is an error */
typedef enum detent_kind { DETENT_KIND_BOOL = 1, DETENT_KIND_INT = 2 } detent_kind;
typedef struct {
  size_t size;                                          /* sizeof, for extension */
  detent_str store;                                     /* store directory */
  const detent_str *namespaces; size_t namespace_count; /* 0: all, including later ones */
  uint32_t publisher_uid;
} detent_open_options;
typedef struct { detent_kind kind; bool boolean; int64_t integer; } detent_value;

uint32_t detent_abi_version(void);
detent_status detent_open(const detent_open_options *, detent_reader **out);
detent_status detent_eval(detent_reader *, detent_str name, detent_str hash,
                          detent_str switch_value, bool fallback, bool *out);
detent_status detent_get(detent_reader *, detent_str name, int64_t fallback, int64_t *out);
detent_status detent_read(detent_reader *, detent_str name, detent_str hash,
                          detent_str switch_value, detent_value *out);
detent_status detent_refresh(detent_reader *);
detent_status detent_revision(detent_reader *, detent_str name_space, uint64_t *out);
const char *detent_status_name(detent_status);
void detent_close(detent_reader *);
#ifdef __cplusplus
}
#endif
#endif
