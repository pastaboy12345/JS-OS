#include <jsos/api.h>
#include <jsos/framebuffer.h>
#include <jsos/heap.h>
#include <jsos/platform.h>
#include <jsos/runtime.h>

#include <quickjs.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define JS_MEMORY_LIMIT (24ULL * 1024ULL * 1024ULL)
#define JS_STACK_LIMIT (768ULL * 1024ULL)
#define JS_EXECUTION_LIMIT_US (5ULL * 1000ULL * 1000ULL)

typedef struct {
    uint64_t deadline;
} RuntimeGuard;

static JSRuntime *runtime;
static JSContext *context;
static RuntimeGuard guard;

static void *runtime_malloc(JSMallocState *state, size_t size) {
    if (size == 0 || state->malloc_size > state->malloc_limit ||
            size > state->malloc_limit - state->malloc_size) {
        return NULL;
    }
    void *pointer = heap_allocate(size);
    if (pointer == NULL) {
        return NULL;
    }
    size_t usable = heap_usable_size(pointer);
    state->malloc_count++;
    state->malloc_size += usable;
    return pointer;
}

static void runtime_free(JSMallocState *state, void *pointer) {
    if (pointer == NULL) {
        return;
    }
    size_t usable = heap_usable_size(pointer);
    if (state->malloc_count != 0) {
        state->malloc_count--;
    }
    if (usable <= state->malloc_size) {
        state->malloc_size -= usable;
    }
    heap_release(pointer);
}

static void *runtime_realloc(JSMallocState *state, void *pointer, size_t size) {
    if (pointer == NULL) {
        return runtime_malloc(state, size);
    }
    if (size == 0) {
        runtime_free(state, pointer);
        return NULL;
    }
    size_t old_size = heap_usable_size(pointer);
    size_t base_size = state->malloc_size >= old_size ? state->malloc_size - old_size : 0;
    if (base_size > state->malloc_limit || size > state->malloc_limit - base_size) {
        return NULL;
    }
    void *replacement = heap_reallocate(pointer, size);
    if (replacement == NULL) {
        return NULL;
    }
    state->malloc_size = base_size + heap_usable_size(replacement);
    return replacement;
}

static size_t runtime_usable_size(const void *pointer) {
    return heap_usable_size(pointer);
}

static const JSMallocFunctions allocator = {
    .js_malloc = runtime_malloc,
    .js_free = runtime_free,
    .js_realloc = runtime_realloc,
    .js_malloc_usable_size = runtime_usable_size,
};

static int interrupt_handler(JSRuntime *quickjs_runtime, void *opaque) {
    (void)quickjs_runtime;
    RuntimeGuard *runtime_guard = opaque;
    return runtime_guard->deadline != UINT64_MAX &&
           platform_uptime_us() >= runtime_guard->deadline;
}

static void promise_rejection_tracker(JSContext *quickjs_context,
                                      JSValueConst promise,
                                      JSValueConst reason,
                                      JS_BOOL handled,
                                      void *opaque) {
    (void)promise;
    (void)opaque;
    if (handled) {
        return;
    }
    const char *message = JS_ToCString(quickjs_context, reason);
    console_write("[promise rejection] ");
    console_write(message == NULL ? "<unprintable>" : message);
    console_write("\n");
    if (message != NULL) {
        JS_FreeCString(quickjs_context, message);
    }
}

void js_runtime_report_exception(JSContext *quickjs_context) {
    JSValue exception = JS_GetException(quickjs_context);
    const char *message = JS_ToCString(quickjs_context, exception);
    console_write("[QuickJS exception] ");
    console_write(message == NULL ? "<unprintable>" : message);
    console_write("\n");
    if (message != NULL) {
        JS_FreeCString(quickjs_context, message);
    }
    if (JS_IsError(quickjs_context, exception)) {
        JSValue stack = JS_GetPropertyStr(quickjs_context, exception, "stack");
        if (!JS_IsUndefined(stack)) {
            const char *stack_text = JS_ToCString(quickjs_context, stack);
            if (stack_text != NULL) {
                console_write(stack_text);
                console_write("\n");
                JS_FreeCString(quickjs_context, stack_text);
            }
        }
        JS_FreeValue(quickjs_context, stack);
    }
    JS_FreeValue(quickjs_context, exception);
}

bool js_runtime_start(const char *source, size_t source_length,
                      const char *filename) {
    runtime = JS_NewRuntime2(&allocator, NULL);
    if (runtime == NULL) {
        console_write("JS-OS: unable to allocate QuickJS runtime\n");
        return false;
    }
    JS_SetRuntimeInfo(runtime, "JS-OS kernel runtime");
    JS_SetMemoryLimit(runtime, JS_MEMORY_LIMIT);
    JS_SetMaxStackSize(runtime, JS_STACK_LIMIT);
    JS_SetCanBlock(runtime, false);
    JS_SetInterruptHandler(runtime, interrupt_handler, &guard);
    JS_SetHostPromiseRejectionTracker(runtime, promise_rejection_tracker, NULL);

    context = JS_NewContext(runtime);
    if (context == NULL) {
        console_write("JS-OS: unable to allocate QuickJS context\n");
        JS_FreeRuntime(runtime);
        runtime = NULL;
        return false;
    }
    if (jsos_install_api(context) < 0) {
        js_runtime_report_exception(context);
        return false;
    }

    guard.deadline = platform_uptime_us() + JS_EXECUTION_LIMIT_US;
    JSValue result = JS_Eval(context, source, source_length, filename,
                             JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_STRICT);
    guard.deadline = UINT64_MAX;
    if (JS_IsException(result)) {
        js_runtime_report_exception(context);
        return false;
    }
    JS_FreeValue(context, result);
    return js_runtime_drain_jobs() >= 0;
}

int js_runtime_drain_jobs(void) {
    if (runtime == NULL) {
        return -1;
    }
    int count = 0;
    while (JS_IsJobPending(runtime)) {
        JSContext *job_context = NULL;
        guard.deadline = platform_uptime_us() + JS_EXECUTION_LIMIT_US;
        int result = JS_ExecutePendingJob(runtime, &job_context);
        guard.deadline = UINT64_MAX;
        if (result < 0) {
            js_runtime_report_exception(job_context == NULL ? context : job_context);
            return -1;
        }
        count++;
    }
    return count;
}

void js_runtime_collect(void) {
    if (runtime != NULL) {
        JS_RunGC(runtime);
    }
}

JSRuntime *js_runtime_instance(void) {
    return runtime;
}

JSContext *js_runtime_context(void) {
    return context;
}

JSValue js_runtime_eval(const char *source, size_t source_length,
                        const char *filename) {
    if (context == NULL) {
        return JS_EXCEPTION;
    }
    guard.deadline = platform_uptime_us() + JS_EXECUTION_LIMIT_US;
    JSValue result = JS_Eval(context, source, source_length, filename,
                             JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_STRICT);
    guard.deadline = UINT64_MAX;
    return result;
}
