#ifndef DETENT_TESTING_GENERATION_WRITER_H
#define DETENT_TESTING_GENERATION_WRITER_H
/* C face of the test generation writer (docs/mechanisms/generation-writer.md), for Python and Rust
 * tests. Every function returns 0 on success; on failure it returns -1 and detent_testing_error()
 * describes the last failure of that writer. */
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct detent_testing_writer detent_testing_writer;
/* Opens (creating when absent) the store directory. NULL on failure; detent_testing_open_error() holds
 * the reason for the calling thread's last failed open. */
detent_testing_writer *detent_testing_open(const char *store);
const char *detent_testing_open_error(void);
/* Publishes the next revision from knob-definition JSON; *revision receives it when not NULL. */
int detent_testing_publish(detent_testing_writer *writer, const char *json, size_t length, uint64_t *revision);
const char *detent_testing_error(const detent_testing_writer *writer);
void detent_testing_close(detent_testing_writer *writer);
#ifdef __cplusplus
}
#endif
#endif
