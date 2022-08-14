#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <oneapi/tbb.h>
#include <sched.h>
#include <sys/sysinfo.h>

#define NOP asm("NOP")

typedef std::uint64_t u64;

class pinning_observer : public oneapi::tbb::task_scheduler_observer {
	const unsigned nprocs;
	std::atomic<unsigned> count;
public:
	pinning_observer(oneapi::tbb::task_arena &a);
	void on_scheduler_entry(bool _w);
};

void serial(u64);
void parallel_for(u64);
void task_group(u64);

constexpr char tab = '\t';

pinning_observer::pinning_observer(oneapi::tbb::task_arena &a):
		oneapi::tbb::task_scheduler_observer(a),
		nprocs(get_nprocs()),
		count(0)
{
	observe(true);
}

void
pinning_observer::on_scheduler_entry(bool _w)
{
	cpu_set_t *mask = CPU_ALLOC(nprocs);
	auto size = CPU_ALLOC_SIZE(nprocs);
	CPU_ZERO_S(size, mask);
	CPU_SET_S(count++ % nprocs, size, mask);
	sched_setaffinity(0, size, mask);
}

void
serial(u64 n)
{
	for (u64 i = 0; i < n; ++i)
		NOP;
}

void
parallel_for(u64 n)
{
	oneapi::tbb::parallel_for((u64)0, n, [] (const u64 &_) {
		NOP;
	});
}

void
task_group(u64 n)
{
	oneapi::tbb::task_group g;
	for (u64 i = 0; i < n; ++i) {
		g.run([] {
			NOP;
		});
	}
	g.wait();
}

void
parallel_for_nanosleep(u64 n)
{
	const static struct timespec spec = {
		.tv_sec = 0,
		.tv_nsec = 0,
	};
	oneapi::tbb::parallel_for((u64)0, n, [] (const u64 &_) {
		nanosleep(&spec, NULL);
	});
}

int
main(int argc, char *argv[])
{
	int count = 1;
	if (strcmp(argv[1], "-c") == 0) {
		count = std::atoi(argv[2]);
	}
	if (count < 1) {
		std::cerr << "count must be >= 1" << std::endl;
		return 1;
	}

	int method;
	int threads;
	u64 iterations;

	while (std::cin >> method >> threads >> iterations) {
		if (threads < 2) {
			std::cerr << "Threads must be < 2" << std::endl;
			continue;
		}
		oneapi::tbb::task_arena arena(threads);
		pinning_observer observer(arena);

		void (*go)(u64) = nullptr;
		switch (method) {
		case 0:
			go = serial;
			break;
		case 1:
			go = parallel_for;
			break;
		case 2:
			go = task_group;
			break;
		case 3:
			go = parallel_for_nanosleep;
			break;
		default:
			std::cerr << "Method must be in {0, 1, 2, 3}" << std::endl;
			continue;
		}

		for (int i = 0; i < count; i++) {
			auto start = std::chrono::high_resolution_clock::now();
			arena.execute([=] {go(iterations);});
			auto end = std::chrono::high_resolution_clock::now();
			std::chrono::duration<double> diff = end - start;
			double time = diff.count();
			double thruput = (double)iterations / time;

			std::cout << method << tab;
			std::cout << threads << tab;
			std::cout << iterations << tab;
			std::cout << time << tab;
			std::cout << thruput << std::endl;
		}
	}

	if (std::cin.eof())
		return 0;

	std::cerr << "Could not read from cin" << std::endl;
	return 1;
}
