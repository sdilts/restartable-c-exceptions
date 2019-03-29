#include <utlist.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <threads.h>

#include <exceptions.h>

// represents one element in the handler stack:
enum handler_entry_contents { HANDLER, FINALIZER };
struct handler_entry {
	enum handler_entry_contents tag;
	union {
		struct condition_handler *handler;
		struct condition_finalizer *finalizer;
	};
	struct handler_entry *next;
};

struct restart_entry {
	struct condition_restart *restart;
	struct restart_entry *next;
};

/**
 * a stack that contains finalizers and handlers available
 * in the current context.
 **/
static thread_local struct handler_entry *handlers = NULL;

/**
 * a stack that contains the restarts available in the current context
 **/
static thread_local struct restart_entry *restarts = NULL;


struct private_cond  {
	char *name;
	char *message;
	int linenum;
	char *filename;
};

static struct condition *create_condition(char* name, char *message,
										const char *filename, const int linenum) {
	struct private_cond *condition = malloc(sizeof(struct private_cond));
	condition->name = malloc(strlen(name)+1);
	condition->message = malloc(strlen(message)+1);
	condition->filename = malloc(strlen(filename)+1);
	strcpy(condition->name, name);
	strcpy(condition->message, message);
	// we might not need to do this, as filename /should/ be a static string
	strcpy(condition->filename, filename);
	//condition->filename = (char *) filename;
	condition->linenum = linenum;
	return (struct condition *)condition;
}

void destroy_condition(struct condition *condition) {
	struct private_cond *cond = (struct private_cond *)condition;
	free(cond->name);
	free(cond->message);
	free(cond->filename);
	free(condition);
}

void fprint_condition(FILE* file, struct condition *cond) {
	fprintf(file, "%s:%d: %s:%s",cond->filename, cond->linenum, cond->name, cond->message);
}

void print_condition(struct condition *condition) {
	fprint_condition(stdout, condition);
}

/*
 * The register and unregister functions simply add or remove
 * the relevlent items to their stack.
 */

void register_restart(struct condition_restart *restart)  {
	struct restart_entry *entry = malloc(sizeof(struct restart_entry));
	entry->restart = restart;
	entry->next = NULL;
	LL_PREPEND(restarts, entry);
}

void unregister_restart(struct condition_restart *restart) {
	struct restart_entry *entry = restarts;
	for( ; entry != NULL; entry = entry->next) {
		if(entry->restart == restart) {
			break;
		}
	}
	if(entry) {
		LL_DELETE(restarts, entry);
	} else {
		fprintf(stderr, "cannot find restart %s\n", restart->restart_name);
	}
}

void _register_handler(struct condition_handler *handler) {
	struct handler_entry *entry = malloc(sizeof(struct handler_entry));
	entry->tag = HANDLER;
	entry->handler = handler;
	entry->next = NULL;
	LL_PREPEND(handlers, entry);
}

void unregister_handler(struct condition_handler *handler) {
	struct handler_entry *entry = handlers;
	for( ; entry != NULL; entry = entry->next) {
		if(entry->tag == HANDLER && entry->handler == handler) {
			break;
		}
	}
	if(entry && entry->tag == HANDLER) {
		LL_DELETE(handlers, entry);
		free(entry);
	} else {
		fprintf(stderr, "Trying to unregister non-existent handler");
	}
}

static struct restart_entry *find_restart_entry(struct restart_entry *head, char *restart_name) {
	struct restart_entry *entry = head;
	for( ; entry != NULL; entry = entry->next) {
		if(strcmp(entry->restart->restart_name, restart_name) == 0) {
			return entry;
		}
	}
	return entry;
}

enum restart_result invoke_restart(struct condition *cond, char *restart_name) {
	struct restart_entry *entry = find_restart_entry(restarts, restart_name);
	if(entry != NULL) {
		return entry->restart->func(cond, entry->restart->data);
	} else {
		return RESTART_NOT_FOUND;
	}
}

static struct handler_entry *find_handler_entry(struct handler_entry *head, char *condition_name) {
	struct handler_entry *entry = head;
	for( ; entry != NULL; entry = entry->next) {
		if(entry->tag == HANDLER && strcmp(entry->handler->condition_name, condition_name) == 0) {
			return entry;
		}
	}
	if (entry && entry->tag == HANDLER) {
		return entry;
	} else {
		return NULL;
	}
}

void register_finalizer(struct condition_finalizer *finalizer) {
	struct handler_entry *entry = malloc(sizeof(struct handler_entry));
	entry->tag = FINALIZER;
	entry->finalizer = finalizer;
	entry->next = NULL;
	LL_PREPEND(handlers, entry);
}

void unregister_finalizer(struct condition_finalizer *finalizer) {
	struct handler_entry *entry = handlers;
	// run the finalizer:
	finalizer->func(finalizer->data);

	for( ; entry != NULL; entry = entry->next) {
		if(entry->tag == FINALIZER && entry->finalizer == finalizer) {
			break;
		}
	}
	if(entry && entry->tag == FINALIZER) {
		LL_DELETE(handlers, entry);
		free(entry);
	} else {
		fprintf(stderr, "Trying to unregister non-existent finalizer");
	}
}

/**
 * Runs the finalizers downto the given handler_entry
 * Removes the handler_entrys in between, as they will no longer be valid
 * after the jump is executed.
 **/
static void run_finalizers_and_unwind(struct handler_entry *entry) {
	struct handler_entry *current;
	struct handler_entry *tmp;
	LL_FOREACH_SAFE(handlers, current, tmp) {
		if(current == entry) {
			break;
		}
		if(current->tag == FINALIZER) {
			current->finalizer->func(current->finalizer->data);
		}
		LL_DELETE(handlers, current);
		free(current);
	}
}

struct throw_env {
	struct condition *const condition;
};

static void condition_finalizer(const void *data) {
	const struct throw_env *env = (const struct throw_env *)data;
	destroy_condition(env->condition);
}

void _throw_exception(char *name, char *message, const char *filename, const int linenum) {
	struct condition *cond = create_condition(name, message, filename, linenum);
	struct throw_env env = { .condition = cond };
	struct condition_finalizer cond_finalizer = INIT_STATIC_FINALIZER(&condition_finalizer, &env);
	register_finalizer(&cond_finalizer);

	// search for a handler:
	struct handler_entry *canidate = handlers;
	enum handler_result result;
	while(canidate != NULL) {
		// find the next valid handler in the list:
		canidate = find_handler_entry(canidate, name);
		if (canidate == NULL) {
			break; // while
		}
		result = canidate->handler->func(cond, canidate->handler->data);
		switch(result) {
		case HANDLER_ABORT:
			// the condition finalizer is ran here, so no need to destroy it.
			run_finalizers_and_unwind(canidate);
			longjmp(canidate->handler->buf, 1);
		case HANDLER_HANDLED:
		    unregister_finalizer(&cond_finalizer);
			return;
		case HANDLER_PASS:
			// skip this handler:
			canidate = canidate->next;
			break; //switch
		default:
			fprintf(stderr, "Invalid handler option: %d", result);
			exit(1);
		}
	}
	// if we get here, it means we didn't find a handler:
	fprintf(stderr, "Fatal condition: ");
	fprint_condition(stderr, cond);
	exit(1);
}
