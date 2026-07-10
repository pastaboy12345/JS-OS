#include <jsos/api.h>
#include <jsos/framebuffer.h>
#include <jsos/heap.h>
#include <jsos/keyboard.h>
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

static uint64_t execution_deadline(void) {
    uint64_t now = platform_uptime_us();
    return now > UINT64_MAX - JS_EXECUTION_LIMIT_US
        ? UINT64_MAX : now + JS_EXECUTION_LIMIT_US;
}

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
    console_write("quickjs: unhandled promise rejection: ");
    console_write(message == NULL ? "<unprintable>" : message);
    console_write("\n");
    if (message != NULL) {
        JS_FreeCString(quickjs_context, message);
    }
}

void js_runtime_report_exception(JSContext *quickjs_context) {
    JSValue exception = JS_GetException(quickjs_context);
    const char *message = JS_ToCString(quickjs_context, exception);
    console_write("quickjs: ");
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
    guard.deadline = UINT64_MAX;
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
        JS_FreeContext(context);
        JS_FreeRuntime(runtime);
        context = NULL;
        runtime = NULL;
        return false;
    }

    guard.deadline = execution_deadline();
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

bool js_runtime_run(const char *source, size_t source_length,
                    const char *filename) {
    JSValue result = js_runtime_eval(source, source_length, filename);
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
        guard.deadline = execution_deadline();
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
    guard.deadline = execution_deadline();
    JSValue result = JS_Eval(context, source, source_length, filename,
                             JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_STRICT);
    guard.deadline = UINT64_MAX;
    return result;
}

static void shell_print_result(JSValueConst result) {
    JSValue global = JS_GetGlobalObject(context);
    JSValue formatter = JS_GetPropertyStr(context, global, "__shellFormat");
    JSValue formatted = JS_UNDEFINED;
    bool has_formatter = JS_IsFunction(context, formatter);
    if (has_formatter) {
        formatted = JS_Call(context, formatter, global, 1, &result);
    }

    const char *text = NULL;
    if (!JS_IsException(formatted) && !JS_IsUndefined(formatted)) {
        text = JS_ToCString(context, formatted);
    }
    if (!has_formatter && text == NULL && !JS_IsException(formatted)) {
        text = JS_ToCString(context, result);
    }
    if (text != NULL) {
        console_write(text);
        console_write("\n");
        JS_FreeCString(context, text);
    } else if (JS_IsException(formatted)) {
        js_runtime_report_exception(context);
    }
    JS_FreeValue(context, formatted);
    JS_FreeValue(context, formatter);
    JS_FreeValue(context, global);
}

static void shell_evaluate(const char *source, size_t length) {
    JSValue global = JS_GetGlobalObject(context);
    JSValue evaluator = JS_GetPropertyStr(context, global, "__shellEval");
    JSValue result;
    if (JS_IsFunction(context, evaluator)) {
        JSValue input = JS_NewStringLen(context, source, length);
        guard.deadline = execution_deadline();
        result = JS_Call(context, evaluator, global, 1, &input);
        guard.deadline = UINT64_MAX;
        JS_FreeValue(context, input);
    } else {
        result = js_runtime_eval(source, length, "<shell>");
    }
    JS_FreeValue(context, evaluator);
    JS_FreeValue(context, global);
    if (JS_IsException(result)) {
        js_runtime_report_exception(context);
        return;
    }
    shell_print_result(result);
    JS_FreeValue(context, result);
    js_runtime_drain_jobs();
}

static void shell_write_prompt(void) {
    JSValue global = JS_GetGlobalObject(context);
    JSValue prompt = JS_GetPropertyStr(context, global, "__shellPrompt");
    const char *text = JS_ToCString(context, prompt);
    console_write(text == NULL ? "quickjs@jsos:~$ " : text);
    if (text != NULL) {
        JS_FreeCString(context, text);
    }
    JS_FreeValue(context, prompt);
    JS_FreeValue(context, global);
}

_Noreturn void js_runtime_shell(void) {
    char line[1024];
    size_t length = 0;
    bool cursor_visible = true;
    uint64_t next_cursor_toggle = platform_uptime_us() + 500000;

    keyboard_init();
    shell_write_prompt();
    console_set_cursor_visible(true);
    for (;;) {
        uint64_t now = platform_uptime_us();
        if (now >= next_cursor_toggle) {
            cursor_visible = !cursor_visible;
            console_set_cursor_visible(cursor_visible);
            next_cursor_toggle = now + 500000;
        }

        KeyboardEvent event;
        if (!keyboard_read_event(&event)) {
            __asm__ volatile ("pause");
            continue;
        }
        console_set_cursor_visible(false);
        cursor_visible = false;

        if (event.type == KEYBOARD_EVENT_ENTER) {
            console_write("\n");
            if (length != 0) {
                line[length] = '\0';
                shell_evaluate(line, length);
                length = 0;
            }
            shell_write_prompt();
            cursor_visible = true;
            console_set_cursor_visible(true);
            next_cursor_toggle = platform_uptime_us() + 500000;
            continue;
        }
        if (event.type == KEYBOARD_EVENT_BACKSPACE) {
            if (length != 0) {
                length--;
                console_write("\b \b");
            }
            cursor_visible = true;
            console_set_cursor_visible(true);
            next_cursor_toggle = platform_uptime_us() + 500000;
            continue;
        }
        if (event.type == KEYBOARD_EVENT_INTERRUPT) {
            length = 0;
            console_write("^C\n");
            shell_write_prompt();
            cursor_visible = true;
            console_set_cursor_visible(true);
            next_cursor_toggle = platform_uptime_us() + 500000;
            continue;
        }
        char character = event.character;
        if (event.type != KEYBOARD_EVENT_CHARACTER || character == '\t' ||
                (unsigned char)character < 0x20 || (unsigned char)character >= 0x7f) {
            cursor_visible = true;
            console_set_cursor_visible(true);
            continue;
        }
        if (length + 1 >= sizeof(line)) {
            console_write("\nquickjs: input is limited to 1023 bytes\n");
            length = 0;
            shell_write_prompt();
            cursor_visible = true;
            console_set_cursor_visible(true);
            continue;
        }
        line[length++] = character;
        console_write_n(&character, 1);
        cursor_visible = true;
        console_set_cursor_visible(true);
        next_cursor_toggle = platform_uptime_us() + 500000;
    }
}
