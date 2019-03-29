#include <execeptions>

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
