#include <atomic>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <oneapi/tbb.h>
#include <oneapi/tbb/mutex.h>

#ifdef DEBUG
#define debug(str) std::cerr << str << std::endl;
#else
#define debug(str)
#endif
#define DIE(str) {std::cerr << progname << ": " << str << std::endl; \
	std::exit(EXIT_FAILURE);}
#define BENCH(result, code) {\
	auto start = std::chrono::high_resolution_clock::now();\
	code;\
	auto stop = std::chrono::high_resolution_clock::now();\
	std::chrono::duration<decltype(result)> diff = stop - start;\
	result = diff.count();}
#define SEQ(name, lock, unlock, push, pop, its) {\
	double result;\
	std::cout << name << "," << its << ","; \
	debug("Starting to push_front " << its << "elements..."); \
	BENCH(result, for (decltype(its) i = 0; i < its; ++i) { \
		lock; push; unlock; \
	});\
	std::cout << result << ",";\
	std::cout << ((double)its / result) << ",";\
	debug("Starting to pop_front " << its << "elements..."); \
	BENCH(result, for (decltype(its) i = 0; i < its; ++i) { \
		lock; pop; unlock; \
	});\
	std::cout << result << ",";\
	std::cout << ((double)its / result) << std::endl;}


typedef std::uint64_t u64;

const char *progname;

u64
parse_args(int argc, char *argv[])
{
	char *end;
	u64 iterations;
	progname = argv[0];
	if (argc != 2)
		DIE("usage: " << progname << " <n_iterations>");
	if ((iterations = strtoul(argv[1], &end, 10)) == ULONG_MAX)
		DIE(argv[1] << " overflows uint64_t");
	if (*end != '\0')
		DIE("could not parse `" << argv[1] << "'");
	return iterations;
}

int
main(int argc, char *argv[])
{
	u64 iterations = parse_args(argc, argv);
	std::deque<int> deque(iterations);
	std::atomic<bool> atomic(0); /* true → is locked */
	std::mutex mutex;
	oneapi::tbb::spin_mutex spin_mutex;
	oneapi::tbb::v1::mutex v1_mutex;
	oneapi::tbb::queuing_mutex queuing_mutex;

	SEQ("nothing-nothing", , ,
			asm("NOP"), asm("NOP"),
			iterations);
	SEQ("deque-nothing", , ,
			deque.push_front(i), deque.pop_back(),
			iterations);
	SEQ("deque-mutex",
			mutex.lock(), mutex.unlock(),
			deque.push_front(i), deque.pop_back(),
			iterations);
	SEQ("deque-atomic",
			while (atomic); atomic = true, atomic = false,
			deque.push_front(i), deque.pop_back(),
			iterations);
	SEQ("deque-spin_mutex",
			spin_mutex.lock(), spin_mutex.unlock(),
			deque.push_front(i), deque.pop_back(),
			iterations);
	SEQ("deque-v1_mutex",
			v1_mutex.lock(), v1_mutex.unlock(),
			deque.push_front(i), deque.pop_back(),
			iterations);
	/* Currently excluded. Only locking interface is ::scoped_lock, which
	 * seems to increase performance for other mutex types. Possibly being
	 * optimized behind the scenes.
	SEQ("deque-queuing_mutex",
			decltype(queuing_mutex)::scoped_lock lock, ,
			deque.push_front(i), deque.pop_back(),
			iterations);
	 */

	return 0;
}
