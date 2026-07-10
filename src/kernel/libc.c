#include <assert.h>
#include <fenv.h>
#include <inttypes.h>
#include <jsos/heap.h>
#include <jsos/platform.h>
#include <jsos/serial.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

void *memcpy(void *destination, const void *source, size_t count) {
    unsigned char *output = destination;
    const unsigned char *input = source;
    for (size_t i = 0; i < count; i++) {
        output[i] = input[i];
    }
    return destination;
}

void *memmove(void *destination, const void *source, size_t count) {
    unsigned char *output = destination;
    const unsigned char *input = source;
    if (output < input) {
        for (size_t i = 0; i < count; i++) {
            output[i] = input[i];
        }
    } else if (output > input) {
        for (size_t i = count; i != 0; i--) {
            output[i - 1] = input[i - 1];
        }
    }
    return destination;
}

void *memset(void *destination, int value, size_t count) {
    unsigned char *output = destination;
    for (size_t i = 0; i < count; i++) {
        output[i] = (unsigned char)value;
    }
    return destination;
}

int memcmp(const void *left, const void *right, size_t count) {
    const unsigned char *a = left;
    const unsigned char *b = right;
    for (size_t i = 0; i < count; i++) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

void *memchr(const void *memory, int value, size_t count) {
    const unsigned char *bytes = memory;
    for (size_t i = 0; i < count; i++) {
        if (bytes[i] == (unsigned char)value) {
            return (void *)(bytes + i);
        }
    }
    return NULL;
}

size_t strlen(const char *text) {
    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

int strcmp(const char *left, const char *right) {
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return (unsigned char)*left - (unsigned char)*right;
}

int strncmp(const char *left, const char *right, size_t count) {
    for (size_t i = 0; i < count; i++) {
        unsigned char a = (unsigned char)left[i];
        unsigned char b = (unsigned char)right[i];
        if (a != b) {
            return a < b ? -1 : 1;
        }
        if (a == '\0') {
            return 0;
        }
    }
    return 0;
}

char *strchr(const char *text, int character) {
    do {
        if (*text == (char)character) {
            return (char *)text;
        }
    } while (*text++ != '\0');
    return NULL;
}

char *strrchr(const char *text, int character) {
    const char *last = NULL;
    do {
        if (*text == (char)character) {
            last = text;
        }
    } while (*text++ != '\0');
    return (char *)last;
}

void *malloc(size_t size) {
    return heap_allocate(size);
}

void *calloc(size_t count, size_t size) {
    return heap_callocate(count, size);
}

void *realloc(void *pointer, size_t size) {
    return heap_reallocate(pointer, size);
}

void free(void *pointer) {
    heap_release(pointer);
}

size_t malloc_usable_size(void *pointer) {
    return heap_usable_size(pointer);
}

int abs(int value) {
    return value < 0 ? -value : value;
}

int atoi(const char *text) {
    if (text == NULL) {
        return 0;
    }
    bool negative = false;
    if (*text == '-' || *text == '+') {
        negative = *text == '-';
        text++;
    }
    int value = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text++ - '0');
    }
    return negative ? -value : value;
}

static void swap_elements(unsigned char *a, unsigned char *b, size_t size) {
    for (size_t i = 0; i < size; i++) {
        unsigned char temporary = a[i];
        a[i] = b[i];
        b[i] = temporary;
    }
}

static void sift_down(unsigned char *base, size_t start, size_t end, size_t size,
                      int (*compare)(const void *, const void *)) {
    size_t root = start;
    while (root <= (end - 1) / 2) {
        size_t child = root * 2 + 1;
        if (child < end && compare(base + child * size, base + (child + 1) * size) < 0) {
            child++;
        }
        if (compare(base + root * size, base + child * size) >= 0) {
            return;
        }
        swap_elements(base + root * size, base + child * size, size);
        root = child;
    }
}

void qsort(void *base_pointer, size_t count, size_t size,
           int (*compare)(const void *, const void *)) {
    if (base_pointer == NULL || compare == NULL || count < 2 || size == 0) {
        return;
    }
    unsigned char *base = base_pointer;
    for (size_t start = count / 2; start != 0; start--) {
        sift_down(base, start - 1, count - 1, size, compare);
    }
    for (size_t end = count - 1; end != 0; end--) {
        swap_elements(base, base + end * size, size);
        sift_down(base, 0, end - 1, size, compare);
    }
}

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
} FormatSink;

static void sink_character(FormatSink *sink, char character) {
    if (sink->capacity != 0 && sink->length + 1 < sink->capacity) {
        sink->buffer[sink->length] = character;
    }
    sink->length++;
}

static void sink_repeat(FormatSink *sink, char character, int count) {
    while (count-- > 0) {
        sink_character(sink, character);
    }
}

static void sink_text(FormatSink *sink, const char *text, size_t length) {
    for (size_t i = 0; i < length; i++) {
        sink_character(sink, text[i]);
    }
}

static size_t unsigned_digits(char output[32], uintmax_t value, unsigned int base,
                              bool uppercase) {
    const char *alphabet = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    size_t length = 0;
    do {
        output[length++] = alphabet[value % base];
        value /= base;
    } while (value != 0);
    for (size_t i = 0; i < length / 2; i++) {
        char temporary = output[i];
        output[i] = output[length - i - 1];
        output[length - i - 1] = temporary;
    }
    return length;
}

static size_t fixed_double(char output[96], double value, int precision, bool exponent_mode) {
    union { double value; uint64_t bits; } representation = { value };
    size_t length = 0;
    bool negative = (representation.bits >> 63) != 0;
    uint64_t exponent_bits = (representation.bits >> 52) & 0x7ff;
    uint64_t fraction_bits = representation.bits & ((1ULL << 52) - 1);
    if (negative) {
        output[length++] = '-';
        value = -value;
    }
    if (exponent_bits == 0x7ff) {
        const char *special = fraction_bits == 0 ? "Infinity" : "NaN";
        size_t special_length = strlen(special);
        memcpy(output + length, special, special_length);
        return length + special_length;
    }
    int exponent = 0;
    if (exponent_mode && value != 0.0) {
        while (value >= 10.0 && exponent < 308) {
            value /= 10.0;
            exponent++;
        }
        while (value < 1.0 && exponent > -308) {
            value *= 10.0;
            exponent--;
        }
    }
    if (value > 18446744073709547520.0) {
        value = 18446744073709547520.0;
    }
    uint64_t integer = (uint64_t)value;
    char digits[32];
    size_t integer_length = unsigned_digits(digits, integer, 10, false);
    memcpy(output + length, digits, integer_length);
    length += integer_length;
    if (precision < 0) {
        precision = 6;
    }
    if (precision > 12) {
        precision = 12;
    }
    if (precision != 0) {
        output[length++] = '.';
        double fraction = value - (double)integer;
        for (int i = 0; i < precision; i++) {
            fraction *= 10.0;
            int digit = (int)fraction;
            output[length++] = (char)('0' + digit);
            fraction -= digit;
        }
    }
    if (exponent_mode) {
        output[length++] = 'e';
        output[length++] = exponent < 0 ? '-' : '+';
        unsigned int absolute_exponent = exponent < 0 ? (unsigned int)-exponent : (unsigned int)exponent;
        if (absolute_exponent < 10) {
            output[length++] = '0';
        }
        length += unsigned_digits(output + length, absolute_exponent, 10, false);
    }
    return length;
}

enum LengthModifier {
    LENGTH_DEFAULT,
    LENGTH_LONG,
    LENGTH_LONG_LONG,
    LENGTH_SIZE,
    LENGTH_MAXIMUM,
};

int vsnprintf(char *buffer, size_t size, const char *format, va_list arguments) {
    FormatSink sink = { .buffer = buffer, .capacity = size, .length = 0 };
    va_list args;
    va_copy(args, arguments);
    while (*format != '\0') {
        if (*format != '%') {
            sink_character(&sink, *format++);
            continue;
        }
        format++;
        if (*format == '%') {
            sink_character(&sink, *format++);
            continue;
        }
        bool left = false;
        bool plus = false;
        bool space = false;
        bool alternate = false;
        bool zero = false;
        for (;;) {
            if (*format == '-') left = true;
            else if (*format == '+') plus = true;
            else if (*format == ' ') space = true;
            else if (*format == '#') alternate = true;
            else if (*format == '0') zero = true;
            else break;
            format++;
        }
        int width = 0;
        if (*format == '*') {
            width = va_arg(args, int);
            if (width < 0) {
                left = true;
                width = -width;
            }
            format++;
        } else {
            while (*format >= '0' && *format <= '9') {
                width = width * 10 + (*format++ - '0');
            }
        }
        int precision = -1;
        if (*format == '.') {
            format++;
            precision = 0;
            if (*format == '*') {
                precision = va_arg(args, int);
                format++;
            } else {
                while (*format >= '0' && *format <= '9') {
                    precision = precision * 10 + (*format++ - '0');
                }
            }
        }
        enum LengthModifier length_modifier = LENGTH_DEFAULT;
        if (*format == 'l') {
            format++;
            length_modifier = LENGTH_LONG;
            if (*format == 'l') {
                format++;
                length_modifier = LENGTH_LONG_LONG;
            }
        } else if (*format == 'z' || *format == 't') {
            format++;
            length_modifier = LENGTH_SIZE;
        } else if (*format == 'j') {
            format++;
            length_modifier = LENGTH_MAXIMUM;
        } else if (*format == 'h') {
            format++;
            if (*format == 'h') format++;
        }

        char conversion = *format == '\0' ? '\0' : *format++;
        char temporary[128];
        size_t temporary_length = 0;
        char prefix[3];
        size_t prefix_length = 0;

        if (conversion == 's') {
            const char *text = va_arg(args, const char *);
            if (text == NULL) text = "(null)";
            temporary_length = strlen(text);
            if (precision >= 0 && (size_t)precision < temporary_length) {
                temporary_length = precision;
            }
            int padding = width > (int)temporary_length ? width - (int)temporary_length : 0;
            if (!left) sink_repeat(&sink, ' ', padding);
            sink_text(&sink, text, temporary_length);
            if (left) sink_repeat(&sink, ' ', padding);
            continue;
        }
        if (conversion == 'c') {
            temporary[0] = (char)va_arg(args, int);
            temporary_length = 1;
        } else if (conversion == 'd' || conversion == 'i') {
            intmax_t signed_value;
            if (length_modifier == LENGTH_LONG_LONG) signed_value = va_arg(args, long long);
            else if (length_modifier == LENGTH_LONG) signed_value = va_arg(args, long);
            else if (length_modifier == LENGTH_SIZE) signed_value = va_arg(args, ptrdiff_t);
            else if (length_modifier == LENGTH_MAXIMUM) signed_value = va_arg(args, intmax_t);
            else signed_value = va_arg(args, int);
            uintmax_t magnitude;
            if (signed_value < 0) {
                prefix[prefix_length++] = '-';
                magnitude = (uintmax_t)(-(signed_value + 1)) + 1;
            } else {
                if (plus) prefix[prefix_length++] = '+';
                else if (space) prefix[prefix_length++] = ' ';
                magnitude = (uintmax_t)signed_value;
            }
            temporary_length = unsigned_digits(temporary, magnitude, 10, false);
        } else if (conversion == 'u' || conversion == 'x' || conversion == 'X' || conversion == 'o') {
            uintmax_t value;
            if (length_modifier == LENGTH_LONG_LONG) value = va_arg(args, unsigned long long);
            else if (length_modifier == LENGTH_LONG) value = va_arg(args, unsigned long);
            else if (length_modifier == LENGTH_SIZE) value = va_arg(args, size_t);
            else if (length_modifier == LENGTH_MAXIMUM) value = va_arg(args, uintmax_t);
            else value = va_arg(args, unsigned int);
            unsigned int base = conversion == 'o' ? 8 : (conversion == 'u' ? 10 : 16);
            if (alternate && value != 0) {
                prefix[prefix_length++] = '0';
                if (base == 16) prefix[prefix_length++] = conversion;
            }
            temporary_length = unsigned_digits(temporary, value, base, conversion == 'X');
        } else if (conversion == 'p') {
            uintptr_t value = (uintptr_t)va_arg(args, void *);
            prefix[prefix_length++] = '0';
            prefix[prefix_length++] = 'x';
            temporary_length = unsigned_digits(temporary, value, 16, false);
        } else if (conversion == 'f' || conversion == 'F' || conversion == 'e' ||
                   conversion == 'E' || conversion == 'g' || conversion == 'G') {
            double value = va_arg(args, double);
            bool use_exponent = conversion == 'e' || conversion == 'E';
            if ((conversion == 'g' || conversion == 'G') && (value >= 1000000.0 || value <= -1000000.0 ||
                    (value != 0.0 && value < 0.0001 && value > -0.0001))) {
                use_exponent = true;
            }
            temporary_length = fixed_double(temporary, value, precision, use_exponent);
            if (plus && temporary[0] != '-') {
                prefix[prefix_length++] = '+';
            }
        } else {
            if (conversion != '\0') sink_character(&sink, conversion);
            continue;
        }

        size_t zero_count = 0;
        if (precision > 0 && conversion != 'f' && conversion != 'F' &&
                conversion != 'e' && conversion != 'E' && conversion != 'g' && conversion != 'G' &&
                (size_t)precision > temporary_length) {
            zero_count = (size_t)precision - temporary_length;
        }
        size_t total = prefix_length + zero_count + temporary_length;
        int padding = width > (int)total ? width - (int)total : 0;
        if (!left && (!zero || precision >= 0)) sink_repeat(&sink, ' ', padding);
        sink_text(&sink, prefix, prefix_length);
        if (!left && zero && precision < 0) sink_repeat(&sink, '0', padding);
        sink_repeat(&sink, '0', (int)zero_count);
        sink_text(&sink, temporary, temporary_length);
        if (left) sink_repeat(&sink, ' ', padding);
    }
    va_end(args);
    if (size != 0) {
        size_t terminator = sink.length < size ? sink.length : size - 1;
        buffer[terminator] = '\0';
    }
    return sink.length > INT32_MAX ? INT32_MAX : (int)sink.length;
}

int snprintf(char *buffer, size_t size, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int result = vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    return result;
}

static JSOS_FILE standard_output_file = { .descriptor = 1 };
static JSOS_FILE standard_error_file = { .descriptor = 2 };
FILE *stdout = &standard_output_file;
FILE *stderr = &standard_error_file;

int vfprintf(FILE *stream, const char *format, va_list arguments) {
    (void)stream;
    char buffer[2048];
    int length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    size_t written = length < 0 ? 0 : (size_t)length;
    if (written >= sizeof(buffer)) written = sizeof(buffer) - 1;
    serial_write_n(buffer, written);
    return length;
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

int printf(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int result = vfprintf(stdout, format, arguments);
    va_end(arguments);
    return result;
}

int fputc(int character, FILE *stream) {
    (void)stream;
    serial_putc((char)character);
    return (unsigned char)character;
}

int putchar(int character) {
    return fputc(character, stdout);
}

int gettimeofday(struct timeval *time_value, void *timezone) {
    (void)timezone;
    if (time_value == NULL) {
        return -1;
    }
    uint64_t microseconds = platform_unix_us();
    time_value->tv_sec = (time_t)(microseconds / 1000000ULL);
    time_value->tv_usec = (long)(microseconds % 1000000ULL);
    return 0;
}

struct tm *localtime_r(const time_t *timer, struct tm *result) {
    (void)timer;
    if (result == NULL) {
        return NULL;
    }
    memset(result, 0, sizeof(*result));
    result->tm_gmtoff = 0;
    result->tm_zone = "UTC";
    return result;
}

int fegetround(void) {
    uint32_t mxcsr;
    __asm__ volatile ("stmxcsr %0" : "=m"(mxcsr));
    return (int)((mxcsr >> 13) & 3);
}

int fesetround(int mode) {
    if (mode < 0 || mode > 3) {
        return -1;
    }
    uint32_t mxcsr;
    __asm__ volatile ("stmxcsr %0" : "=m"(mxcsr));
    mxcsr = (mxcsr & ~(3U << 13)) | ((uint32_t)mode << 13);
    __asm__ volatile ("ldmxcsr %0" : : "m"(mxcsr));
    return 0;
}

void jsos_assert_fail(const char *expression, const char *file, int line) {
    fprintf(stderr, "JS-OS assertion failed: %s (%s:%d)\n", expression, file, line);
    platform_halt();
}

_Noreturn void abort(void) {
    serial_write("JS-OS: abort()\n");
    platform_halt();
}

_Noreturn void exit(int status) {
    fprintf(stderr, "JS-OS: exit(%d)\n", status);
    platform_halt();
}
