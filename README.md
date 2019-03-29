# Conditions and Exceptions in C

This repository contains an implementation of `setjmp`/`longjmp` based
error handling for C programs. It contains facilities for throwing
and catching exceptions, as well as establishing finalizers to cleanup
resources when a non-local exit occurs. Experimental support for
restarts is also available.

The `try {} catch {} finally {}` pattern is supported, with a twist:
before the stack is unwound and finalizers executed, a `handler`
function is called that decides whether to ignore the error, decline
to handle the error, or unwind the stack to the location where the
handler was established. Because this mechanism is marginally useful
beyond just error handling, events are known as `condition`s rather
than errors. You can read about the benefits of a mechanism like this
here: https://news.ycombinator.com/item?id=8475633

## Limitations

The major limitation to this implementation is that strings are used
to represent condition types and restarts. This could cause name
collisions between two different modules, and prevents errors from
inheriting from one another. This could easily be changed, as the
strings are used for identification and nothing else.

The second limitation is the one present when using any sort of
non-local exits in c: you may bypass any cleanup code not present in a
finalizer. This is most relevant when using callbacks with an external
library that doesn't use this condition system, as establishing finalizers avoids
this problem in code that has access to them. To this end, any
function that is used as a callback to an external library should
establish handlers for any error that may be signaled during its execution.

Because `setjmp`/`longjmp` are being used, variable-modified types
like VLAs will cause undefined behavior and memory leaks.

## Usage
A complete description of each function and macro is available in
`include/exceptions.h` file.

### Non local exits
The simplest case when an error is received is to unwind the call stack
to where the handler was established, and run some code to handle the
error. In Python, the code could look something like this:

``` python3
try:
    print("I'm about to throw an error!")
    raise Error
except Error:
    print("An error occurred")
    exit(1)
````
Because we can't introduce any new syntax to C, our version is
significantly uglier. In addition, we need to have a handler function
that returns `HANDLER_ABORT`, which signals to the condition machinery
to perform a non-local exit.

The handler function is extremely straightforward:
``` c
enum handler_result error_abort_handler(struct condition *cond, const void *data) {
	return HANDLER_ABORT;
}
```
The `data` pointer will be explained in the next section.

To establish the try/catch blocks, we need to establish a
`condition_handler` struct, then register it with the condition
machinery. To make this more succinct, two macros are provided:
'INIT_STATIC_HANDLER` and `REGISTER_HANDLER`. `INIT_STATIC_HANDLER` is
just some syntax sugar to initialize a `condition_handler` struct,
while the `REGISTER_HANDLER` macro establishes the
`except Error: ...` portion of our code. Here's the first portion:
``` c
	struct condition_handler aborter = INIT_STATIC_HANDLER("error", error_abort_handler, NULL);
```
This initializes a `condition_handler` struct that handles the
condition called `"error"`, and uses the `error_abort_handler`
function to decide what action is taken.

We can then write our error handling code. To explain it, the rest of
the code will be included:
``` c
	// except block:
	REGISTER_HANDLER(&aborter) {
		printf("An error occurred");
		goto handler_scope;
	}
	// try block:
	{
		printf("I'm about to throw an error!");
		throw("error", "A diagnostic message");
	}
	// unregister the handler:
 handler_scope:
	unregister_handler(&aborter);
```
The `REGISTER_HANDLER() { ... }` portion tells the machinery about the
aborter condition handler, and provides the code that is run when an
`"error"` condition is signaled. In the handling code, we need to jump
past the try block, otherwise it will be ran again. You could use some
sort of flag instead of `goto`, but that just adds even more noise to
this already messy code snippet, especially when multiple condition
handlers are present in a function. To understand why the code is
formatted this way, research the function `setjmp` and look at the
macro definition of `REGISTER_HANDLER`.

### Handler functions

Handler functions give us the ability to decide if the code
associated with it is capable of handling the error, and provides some
side benefits that aren't directly related to error handling. To do
any of this, however, it needs information about the condition being
signaled and the context surrounding the error. This is provided in
two ways: the signaled condition is provided as an argument, as well
as a pointer to some data.

#### The void *data pointer

The data argument provides a crude way of having lambda functions in
c. By passing pointers to variables in the calling context, we can
provide handler functions information about the state of the execution
environment. An example is probably the easiest way to see how this works:
``` c
struct env_data {
	volatile int *const a;
}

void print_env(void *data) {
	struct env_data *env = (struct env_data *)data;
	printf("a = %d\n", env_data->a);
}

void modify_env(void *data) {
	struct env_data *env = (struct env_data *)data;
	env_data->a = 30;
}

int main(void) {
	volatile int a = 2;
	struct env_data data = {
		.a = &a
	};
	print_env(data);
	modify_env(data);
	printf("a = %d\n", a);
}
```
Run this, and you will get the output
```
a = 2
a = 30
```
To avoid accidentally changing anything you shouldn't, it is a good
idea to declare pointers in your environment struct as
constant. Along the same lines, declaring variables passed in
this manner as `volatile` prevents the compiler from making any
assumptions about the value of the variable and causing hard to
track down issues. If you don't modify the variable in the handler
function, the variable doesn't need to be declared `volatile`.

The `data` member in the `condition_handler` struct stores the pointer
to this data. If you are using the `INIT_STATIC_HANDLER` macro, this
is the last argument to the macro.

#### Handling and declining errors

If the handler function decides that it cannot make a decision to
unwind the stack, it can return `HANDLER_PASS` to tell the machinery
to find another error handler. Similarly, if no action further action needs to be
taken, then it can return `HANDLER_HANDLED` to return control to where
the condition was signaled.

## Finalizers

Finalizers are guaranteed to run when the call stack is
unwound. Finalizer functions operate in a similar way to handlers,
except there is no meaningful information to return. You register them
with `register_finalizer` and remove them with
`unregister_finalizer`. For programmer convenience, finalizer functions
are automatically ran when they are unregistered.

## Restarts

Restarts provide a way of allowing higher level code to choose how to
fix a problem. Use `register_restart` and `unregister_restart` to make
the machinery aware of them, and `invoke_restart` to actually call
one. Note that this function can return 3 possible values:
+ `RESTART_SUCCEED`: the restart was available and was able to perform
  its desired action. Any handler function that receives this result
  must return `HANDLER_HANDLED`.
+ `RESTART_FAIL`: the restart was unable to perform its desired
  actions and was unsuccessful.
+ `RESTART_NOT_FOUND`: The restart that was specified does not exist.


## Other uses for the condition system

'HANDLER_HANDLED' and 'HANDLER_PASS' have interesting applications
beyond error handling. While other more direct mechanisms exist, they
can be used for logging or notifying higher level code when certain
events occur. Whether or not this is a good idea is up to the
individual designer or programmer. It can abstract away
some of the mechanisms associated with these notifications, but at the
cost of a more complex code path and the extra runtime associated with it.