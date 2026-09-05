#pragma once

// Audio uses real elapsed time, never recipe progress or the crafting multiplier.
// Keep this independent of Unreal so the rate limit can be tested without a game.
class FExactCraftHitCadence
{
public:
	static constexpr double IntervalSeconds = 0.25;

	void Reset() { SecondsUntilNextHit = 0.0; }

	bool Tick(const double RealDeltaSeconds, const bool bMayPlay)
	{
		if (!bMayPlay || !(RealDeltaSeconds > 0.0)) return false;
		SecondsUntilNextHit -= RealDeltaSeconds;
		if (SecondsUntilNextHit > 0.0) return false;

		// First active tick plays a hit even for a very short request. Discard
		// missed intervals after a stall: never emit a catch-up burst of sounds.
		SecondsUntilNextHit = IntervalSeconds;
		return true;
	}

private:
	double SecondsUntilNextHit = 0.0;
};
