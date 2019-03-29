#include <stdio.h>

#include  <exceptions.h>

struct func_data {
	volatile int *a;
};

enum handler_result handle_something(struct condition *cond, const void *context) {
	const struct func_data *data = (const struct func_data *) context;
	printf("I'm handling condition\n");
	print_condition(cond);
	printf("\nMy data is: %d\n", *data->a);
	*data->a = 10;
	return HANDLER_HANDLED;
}

enum handler_result pass_handle(struct condition *cond, const void *data) {
	printf("I'll pass, thanks\n");
	return HANDLER_PASS;
}

enum handler_result abort_handler(struct condition *cond, const void *data) {
	return HANDLER_ABORT;
}

void finalize(const void *data) {
	puts("finalizer ran");
}

int main(void) {
	volatile int a = 0;
	struct func_data data = {
		.a = &a
	};
	struct condition_handler aborter = INIT_STATIC_HANDLER("something", abort_handler, NULL);
	REGISTER_HANDLER(&aborter) {
		printf("Abort handler has aborted\n");
		goto aborter_scope;
	}

	struct condition_handler something = INIT_STATIC_HANDLER("something", handle_something, &data);
	REGISTER_HANDLER(&something) {
		goto something_scope;
	}
	struct condition_handler pass = INIT_STATIC_HANDLER("something", pass_handle, NULL);
	REGISTER_HANDLER(&pass) {
		printf("Pass handler aborted!");
		goto pass_scope;
	}
	struct condition_finalizer finalizer = INIT_STATIC_FINALIZER(finalize, NULL);
	register_finalizer(&finalizer);

	a = a + 1;
	printf("In try area: a = %d\n", a);
	throw("something", "Throwing for the kick of it");

	unregister_finalizer(&finalizer);

 pass_scope:
	unregister_handler(&pass);
 something_scope:
	unregister_handler(&something);

	throw("something", "This is a message!");

 aborter_scope:
	unregister_handler(&aborter);
	printf("not in try area: a = %d\n", a);
	return 0;
}
