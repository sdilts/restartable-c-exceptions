#ifndef EXCEPTION_H
#define EXCEPTION_H

#include <stdbool.h>
#include <setjmp.h>
#include <stdio.h>

enum handler_result {
	// control flow should go back
	// to where the handler was established
	HANDLER_ABORT,
	// the condition has been handled and control should
	// return to where the condition was signaled from.
	HANDLER_HANDLED,
	// the handler cannot take an action and another
	// handler should be found.
	HANDLER_PASS
};

enum restart_result {
	RESTART_SUCCEED,
	RESTART_FAIL,
	RESTART_NOT_FOUND
};

struct condition {
	const char *name;
	const char *message;
	const int linenum;
	const char *filename;
};

void fprint_condition(FILE *stream, struct condition *condition);

void print_condition(struct condition *condition);

void destroy_condition(struct condition *condition);

typedef enum restart_result (*restart_func)(struct condition* cond, const void *data);

typedef enum handler_result (*handler_func)(struct condition* cond, const void *data);

typedef void (*finalizer_func)(const void *data);

struct condition_restart {
	const char *restart_name;
	const restart_func func;
	void *const data;
};

struct condition_handler {
	// the name of the condition that this handler handles:
	const char *condition_name;
	const handler_func func;
	jmp_buf buf;
	void *const data;
};

struct condition_finalizer {
	const finalizer_func func;
	void *const data;
};

#define INIT_STATIC_RESTART(name, function, _data) \
	{ .restart_name = name, .func = function, .data = data }

// adds the given restart as a valid restart:
void register_restart(struct condition_restart *restart);

void unregister_restart(struct condition_restart *restart);

#define INIT_STATIC_HANDLER(to_handle, function, _data)	\
	{ .condition_name = to_handle, .func = function, .data = _data }

void _register_handler(struct condition_handler *handler);

#define REGISTER_HANDLER(handler)	\
	_register_handler(handler);		\
		if(setjmp((handler)->buf))

void unregister_handler(struct condition_handler *handler);

// If a a condition with name condtion_name is signaled, handle it with the given restart:
enum restart_result invoke_restart(struct condition *cond, char *restart_name);

void _throw_exception(char* name, char *message, const char *filename, const int linenum);

#define throw(name, message) \
	_throw_exception(name, message, __FILE__, __LINE__)

#define warn(message) \
	_throw_exception("warning", message, __FILE__, __LINE__)

/**
 * A finalizer will always be run, even when the function is exited via a condition.
 * The finalizer is ran when it is unregistered with unregister_finalizer.
 **/
void register_finalizer(struct condition_finalizer *finalizer);

void unregister_finalizer(struct condition_finalizer *finalizer);

#define INIT_STATIC_FINALIZER(_func, _data) \
	{ .func = _func, .data = _data }

#endif
