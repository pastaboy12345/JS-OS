#ifndef JSOS_RUNTIME_H
#define JSOS_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <quickjs.h>

bool js_runtime_start(const char *source, size_t source_length,
                      const char *filename);
JSRuntime *js_runtime_instance(void);
JSContext *js_runtime_context(void);
int js_runtime_drain_jobs(void);
void js_runtime_collect(void);
JSValue js_runtime_eval(const char *source, size_t source_length,
                        const char *filename);
void js_runtime_report_exception(JSContext *context);

#endif
