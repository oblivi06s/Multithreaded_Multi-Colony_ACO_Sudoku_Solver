#pragma once
#include <iostream>
#ifdef _WIN32
#include <windows.h>
typedef LARGE_INTEGER TIME_TYPE;
#else
#include <sys/time.h>
typedef unsigned long int TIME_TYPE;
#endif
class Timer
{
    TIME_TYPE timer;
	float inverseTimerFreq;
	float accumulatedTime;  // Accumulated time when paused
	bool isPaused;
	
	TIME_TYPE TimeNow()
	{
#ifdef _WIN32
		LARGE_INTEGER val;
		QueryPerformanceCounter(&val);
#else
		struct timeval tv;
		gettimeofday(&tv, NULL);
		TIME_TYPE val = tv.tv_sec*1000000 + tv.tv_usec;
#endif
		return val;
	}
public:
	Timer() : accumulatedTime(0.0f), isPaused(false)
	{
#ifdef _WIN32
		LARGE_INTEGER val;
		QueryPerformanceFrequency(&val);
		inverseTimerFreq = 1.0f / (float)val.QuadPart;
#else
		inverseTimerFreq = 1e-6f;
#endif
	}
	
	void Reset()
	{
		timer = TimeNow();
		accumulatedTime = 0.0f;
		isPaused = false;
	}
	
	void Pause()
	{
		if (!isPaused)
		{
			TIME_TYPE now = TimeNow();
#ifdef _WIN32
			long long int elapsed = now.QuadPart - timer.QuadPart;
#else
			TIME_TYPE elapsed = now - timer;
#endif
			accumulatedTime += (float)elapsed * inverseTimerFreq;
			isPaused = true;
		}
	}
	
	void Resume()
	{
		if (isPaused)
		{
			timer = TimeNow();
			isPaused = false;
		}
	}
	
	float Elapsed()
	{
		if (isPaused)
		{
			return accumulatedTime;
		}
		else
		{
			TIME_TYPE now = TimeNow();
#ifdef _WIN32		
			long long int elapsed = now.QuadPart - timer.QuadPart;
#else
			TIME_TYPE elapsed = now - timer;
#endif		
			return accumulatedTime + (float)elapsed * inverseTimerFreq;
		}
	}
};
