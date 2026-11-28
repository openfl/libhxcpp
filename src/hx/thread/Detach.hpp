#pragma once

#include <thread>
#include <atomic>
#include <exception>
#include <utility>
#include <stdio.h>

namespace hx
{
	// Detaches a std::thread the runtime will never join. Where the platform
	// cannot detach, as devkitA64's newlib on Switch, whose pthread_detach
	// returns ENOSYS, std::thread::detach throws, and the joinable std::thread
	// then ended the process with std::terminate when it was destroyed, even
	// inside a catch. The thread is kept instead: it runs to its end as it
	// would detached, and what the platform holds for it is not reclaimed.
	inline void DetachThread(std::thread &ioThread)
	{
		try
		{
			ioThread.detach();
		}
		catch (const std::exception &e)
		{
			static std::atomic<bool> sReported(false);
			if (!sReported.exchange(true))
				fprintf(stderr, "Could not detach a thread (%s): finished threads will not be reclaimed\n", e.what());
			// Never destroyed, as destroying a joinable std::thread terminates
			new std::thread(std::move(ioThread));
		}
	}
}
