
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "compile_asm_i386_linux.h"
#include "compiler.h"

/* Linux kernel system calls on x86 system */
static const int syscall_stdin     = 0;
static const int syscall_stdout    = 1;
static const int syscall_sys_exit  = 1;
static const int syscall_sys_read  = 3;
static const int syscall_sys_write = 4;

static int
str_append(char **str, const char *format, ...)
{
    /* This is only used to combine arguments, so fixed-size string should
       be safe to use.  */
    char formatted_str[512];
    assert(strlen(format) < sizeof(formatted_str));

    va_list arg_ptr;
    va_start(arg_ptr, format);
    vsprintf(formatted_str, format, arg_ptr);

    const size_t old_length = (*str == NULL ? 0 : strlen(*str));
    char *new_str = calloc(old_length + strlen(formatted_str) + 1, sizeof(char));
    if (new_str == NULL) {
        /* Error: Out of memory */
        return 201;
    }

    if (*str != NULL) {
        strcat(new_str, *str);
    }
    strcat(new_str, formatted_str);

    free(*str);
    *str = new_str;

    return 0;
}

int
tokens_to_asm_i386_linux(ProgramSource *const source, char **final_output, size_t *final_output_length)
{
    *final_output        = NULL;
    *final_output_length = 0;
    char *output         = NULL;

    /* Initialize variables */
    str_append
    (
        &output,
        ".section		.data\n"
        "array:\n"
        "		.zero	%d\n"
        "buffer:\n"
        "		.byte	0\n"
        "\n",
        DATA_ARRAY_SIZE
    );

    /* Beginning of the code block */
    str_append
    (
        &output,
        ".section		.text\n"
        ".global			_start\n\n"
    );

    /* Subroutines for I/O */
    if (!source->no_print_commands) {
        str_append
        (
            &output,
            "print_char:\n"
            "		push	%%eax\n"
            "		push	%%ebx\n"
            "		push	%%ecx\n"
            "		push	%%edx\n"
            "		xor		%%ebx,%%ebx\n"
            "		mov		(%%eax),%%bl\n"
            "		mov		%%bl,(buffer)\n"
            "		mov		$%d,%%eax\n"
            "		mov		$%d,%%ebx\n"
            "		mov		$buffer,%%ecx\n"
            "		mov		$1,%%edx\n"
            "		int		$0x80\n"
            "		pop		%%edx\n"
            "		pop		%%ecx\n"
            "		pop		%%ebx\n"
            "		pop		%%eax\n"
            "		ret\n"
            "\n",
            syscall_sys_write,
            syscall_stdout
        );
    }
    if (!source->no_input_commands) {
        str_append
        (
            &output,
            "input_char:\n"
            "		push	%%eax\n"
            "		push	%%ebx\n"
            "		push	%%ecx\n"
            "		push	%%edx\n"
            "		mov		$%d,%%eax\n"
            "		mov		$%d,%%ebx\n"
            "		mov		$buffer,%%ecx\n"
            "		mov		$1,%%edx\n"
            "		int		$0x80\n"
            "		pop		%%edx\n"
            "		pop		%%ecx\n"
            "		pop		%%ebx\n"
            "		pop		%%eax\n"
            "		mov		(buffer),%%cl\n"
            "		mov		%%cl,(%%eax)\n"
            "		ret\n"
            "\n",
            syscall_sys_read,
            syscall_stdin
        );
    }

    /* Execution starts at this point */
    str_append
    (
        &output,
        "_start:\n"
        "		mov		$array,%%eax\n"
    );

    /* Convert tokens to machine code */
    int errorcode = 0;
    for (size_t i = 0; i < source->length && errorcode == 0; ++i) {
        const Command current = source->tokens[i];
        switch (current.token)
        {
        case T_INC:
            if (current.value > 0) {
                str_append
                (
                    &output,
                    "		mov		$%d,%%bl\n"
                    "		add		%%bl,(%%eax)\n",
                    current.value & 0xFF
                );
            }
            else if (current.value < 0) {
                str_append
                (
                    &output,
                    "		mov		$%d,%%bl\n"
                    "		sub		%%bl,(%%eax)\n",
                    (-current.value) & 0xFF
                );
            }
            else {
                /* Command has no effect */
            }
            break;

        case T_POINTER_INC:
            if (current.value > 0) {
                str_append
                (
                    &output,
                    "		mov		$%d,%%ebx\n"
                    "		add		%%ebx,%%eax\n",
                    current.value
                );
            }
            else if (current.value < 0) {
                str_append
                (
                    &output,
                    "		mov		$%d,%%ebx\n"
                    "		sub		%%ebx,%%eax\n",
                    -current.value
                );
            }
            else {
                /* Command has no effect */
            }
            break;

        case T_LABEL:
            str_append
            (
                &output,
                "\n"
                "label_%d_begin:\n"
                "		cmpb	$0,(%%eax)\n"
                "		je		label_%d_end\n",
                current.value,
                current.value
            );
            break;
        case T_JUMP:
            str_append
            (
                &output,
                "\n"
                "label_%d_end:\n"
                "		cmpb	$0,(%%eax)\n"
                "		jne		label_%d_begin\n",
                current.value,
                current.value
            );
            break;

        case T_INPUT:
            if (source->no_input_commands) {
                /* Error: Unexpected token */
                errorcode = 202;
            }
            else {
                str_append
                (
                    &output,
                    "		call	input_char\n"
                );
            }
            break;

        case T_PRINT:
            if (source->no_print_commands) {
                /* Error: Unexpected token */
                errorcode = 202;
            }
            else {
                str_append
                (
                    &output,
                    "		call	print_char\n"
                );
            }
            break;

        default:
            break;
        }
    }

    /* Write quit commands */
    if (errorcode == 0) {
        str_append
        (
            &output,
            "\n"
            "		mov		$%d,%%eax\n"
            "		mov		$0,%%ebx\n"
            "		int		$0x80\n",
            syscall_sys_exit
        );
    }
    if (errorcode != 0) {
        free(output);
        return errorcode;
    }

    *final_output = output;
    *final_output_length = strlen(output);

    return 0;
}
