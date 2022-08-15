/* TBB parallel recursive Fibonacci number calculator which measures throughput
 *
 * usage: ./recursive-fib [n]
 *
 * If the n argument is given, each test will be ran n times instead of once.
 *
 * Reads lines from standard input with the following format:
 *
 * 	<n> <nthread>
 *
 * The nth Fibonacci number will be computed using nthread threads.
 *
 * Results are written to standard output with the following format:
 *
 * 	<n> <fib_number> <nthread> <jobs> <total_time> <tasks/sec> <lb>
 *
 * Where fib_number is the nth Fibonacci number and jobs is the amount of
 * parallel tasks created. lb is information related to load balancing, which
 * contains the minimum, the standard deviation from the average, and the
 * maximum tasks per TBB thread.
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <oneapi/tbb.h>
#include <sched.h>
#include <sys/sysinfo.h>
#include <vector>

#define TAB '\t'
#define die(str) {std::cerr << progname << ": " << str << std::endl;\
	std::exit(EXIT_FAILURE);}

typedef std::uint64_t u64;

/* Observer that will (do its best to) pin TBB threads to hardware threads with
 * the lowest index. In theory, this will put as many TBB threads on the same
 * NUMA node as possible (see `lscpu | grep NUMA`).
 */
class pinning_observer : public oneapi::tbb::task_scheduler_observer {
	const unsigned nprocs;
	std::atomic<unsigned> counter;
public:
	pinning_observer(oneapi::tbb::task_arena &arena);
	void on_scheduler_entry(bool is_worker);
};

struct statistics {
	std::uint64_t min, max;
	double avg, dev;
};

struct load_balance {
	int allowed;
	std::vector<u64> tbb;

	load_balance(int allowed, int total_hardware);
	void take_measurement();
};

u64 parallel_fib(int n);
/* Calculate load balance statistics */
u64 parallel_fib_lb(int n, load_balance& lb);
/* Calculate number of threads created */
u64 threads_created(int n);
/* Put the minimum, standard deviation, and maximum separated by tabs */
std::ostream& operator<<(std::ostream &str, const load_balance &lb);
/* Internal for load_balance */
statistics calc_statistics(const std::vector<std::uint64_t> &v);

const char *progname = "recursive-fib";

u64
parallel_fib(int n)
{
	if (n < 2)
		return n;

	u64 x, y;
	oneapi::tbb::parallel_invoke(
		[&]{x = parallel_fib(n - 1);},
		[&]{y = parallel_fib(n - 2);});
	return x + y;
}

u64
parallel_fib_lb(int n, load_balance& lb)
{
	if (n < 2)
		return n;

	lb.take_measurement();
	u64 x, y;
	oneapi::tbb::parallel_invoke(
		[&]{x = parallel_fib_lb(n - 1, lb);},
		[&]{y = parallel_fib_lb(n - 2, lb);});
	return x + y;
}

/* Recursively computes the number of threads created by parallel_fib(n) */
/* TODO: there should be an O(1) way to compute this */
u64
threads_created(int n)
{
	if (n < 2)
		return 0;
	return 2 + threads_created(n - 1) + threads_created(n - 2);
}

pinning_observer::pinning_observer(oneapi::tbb::task_arena &arena):
	oneapi::tbb::task_scheduler_observer(arena),
	nprocs(get_nprocs()),
	counter(0)
{
	observe(true);
}

void
pinning_observer::on_scheduler_entry(bool is_worker)
{
	cpu_set_t *mask = CPU_ALLOC(nprocs);
	auto mask_size = CPU_ALLOC_SIZE(nprocs);
	CPU_ZERO_S(mask_size, mask);
	CPU_SET_S(counter++ % nprocs, mask_size, mask);
	if (sched_setaffinity(0, mask_size, mask))
		die("sched_setaffinity: " << std::strerror(errno));
}

load_balance::load_balance(int allowed, int slots)
	: allowed(allowed), tbb(slots, 0)
{
	/* nothing else */
}

void
load_balance::take_measurement()
{
	tbb[oneapi::tbb::this_task_arena::current_thread_index()]++;
}

std::ostream&
operator<<(std::ostream& str, const load_balance& lb)
{
	std::vector<u64> v;
	auto it = std::back_inserter(v);

	std::remove_copy(lb.tbb.begin(), lb.tbb.end(), it, 0);
	std::fill_n(it, lb.allowed - std::distance(v.begin(), v.end()), 0);
	statistics stats = calc_statistics(v);
	str << stats.min << TAB << stats.dev << TAB << stats.max;

	return str;
}

statistics
calc_statistics(const std::vector<u64>& v)
{
	statistics res;
	double count = v.size();
	res.min = *std::min_element(v.begin(), v.end());
	res.max = *std::max_element(v.begin(), v.end());
	res.avg = (double)std::accumulate(v.begin(), v.end(), 0) / (double)count;
	auto dev_lambda = [res] (double acc, u64 x) {
		double delta = (double)x - res.avg;
		return acc + (delta * delta);
	};
	res.dev = std::sqrt(std::accumulate(v.begin(), v.end(), (double)0.0, dev_lambda)
			/ count);
	return res;
}

int
main(int argc, char *argv[])
{
	progname = argv[0];
	int tests = 1;
	for (int i = 1; i < argc; ++i) {
		char *end;
		tests = std::strtoul(argv[i], &end, 10);
		if (*end != '\0') {
			die("argument must be a valid integer");
		} else if (tests < 1) {
			die("argument must be greater than 0");
		}

	}

	int fib_num;
	unsigned nthread;
	while (std::cin >> fib_num >> nthread) {
		oneapi::tbb::task_arena arena(nthread);
		pinning_observer observer(arena);
		for (int i = 0; i < tests; ++i) {
			auto start_time = std::chrono::high_resolution_clock::now();
			auto result = parallel_fib(fib_num);
			auto end_time = std::chrono::high_resolution_clock::now();
			std::chrono::duration<double> d = end_time - start_time;
			double total_time = d.count();
			double jobs = threads_created(fib_num);
			double throughput = jobs / total_time;
			int nslots =
				oneapi::tbb::this_task_arena::max_concurrency();
			load_balance lb(nthread, nslots);
			parallel_fib_lb(fib_num, lb);

			std::cout << fib_num << TAB;
			std::cout << result << TAB;
			std::cout << nthread << TAB;
			std::cout << jobs << TAB;
			std::cout << total_time << TAB;
			std::cout << throughput << TAB;
			std::cout << lb << std::endl;
		}
	}

	if (!std::cin.eof() && std::cin.fail()) {
		die("error reading from stdin");
	}

	std::exit(EXIT_SUCCESS);
}
