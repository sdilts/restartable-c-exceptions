#include <exceptions.h>

enum handler_result error_abort_handler(struct condition *cond, const void *data) {
	return HANDLER_ABORT;
}

int main(void) {
	struct condition_handler aborter = INIT_STATIC_HANDLER("error", error_abort_handler, NULL);
	REGISTER_HANDLER(&aborter) {
		printf("An error occured\n");
		print_condition(aborter.condition);
		destroy_condition(aborter.condition);
		goto handler_scope;
	}
	// try block:
	{
		printf("I'm about to throw an error!\n");
		throw("error", "A diagnostic message\n");
	}
	// unregister the handler:
 handler_scope:
	unregister_handler(&aborter);
	return 0;
}
