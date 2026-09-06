#include "../Source/ExactCraft/Private/ExactCraftHitCadence.h"
#include <cassert>
#include <cstdio>

int main()
{
	FExactCraftHitCadence Cadence;
	assert(!Cadence.Tick(1.0, false)); // idle/Infinity never emits a hit
	assert(!Cadence.Tick(0.0, true));
	assert(!Cadence.Tick(-1.0, true));
	assert(Cadence.Tick(0.001, true)); // even a very short batch gets feedback
	assert(!Cadence.Tick(0.125, true));
	assert(Cadence.Tick(0.125, true));
	assert(!Cadence.Tick(100.0, false)); // pause/closed/finished do not catch up
	assert(!Cadence.Tick(0.125, true));
	assert(Cadence.Tick(0.125, true));
	assert(Cadence.Tick(100.0, true)); // a stall emits at most one hit
	assert(!Cadence.Tick(0.001, true)); // and no burst the following frame
	Cadence.Reset();
	assert(Cadence.Tick(0.001, true)); // next request has a fresh cadence

	const int FrameRates[] = {30, 60, 144};
	for (const int Fps : FrameRates)
	{
		Cadence.Reset();
		int Hits = 0;
		for (int Frame = 0; Frame < Fps * 5; ++Frame)
		{
			if (Cadence.Tick(1.0 / Fps, true)) ++Hits;
		}
		assert(Hits >= 18 && Hits <= 20);
	}
	std::puts("PASS: first hit, quarter-second cadence, idle/pause/finish gating, stalls, reset, 30/60/144 FPS");
}
