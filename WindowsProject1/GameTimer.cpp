#include "GameTimer.h"

GameTimer::GameTimer() : secondsPerCount(0.0), deltaTime(-1.0), baseTime(0),
pausedTime(0), stopTime(0), prevTime(0), currentTime(0), isStopped(false)
{
	__int64 countsPerSec;
	QueryPerformanceFrequency((LARGE_INTEGER*)&countsPerSec);
	secondsPerCount = 1.0 / (double)countsPerSec;
}

float GameTimer::TotalTime() const
{
	if (isStopped)
	{
		return (float)(((stopTime - pausedTime) - baseTime) * secondsPerCount);
	}

	return (float)(((currentTime - pausedTime) - baseTime) * secondsPerCount);
}

float GameTimer::DeltaTime() const
{
	return (float)deltaTime;
}

void GameTimer::Reset()
{
	__int64 currentTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currentTime);

	baseTime = currentTime;
	prevTime = currentTime;
	stopTime = 0;
	isStopped = false;
}

void GameTimer::Start()
{
	if (!isStopped)
		return;

	__int64 startTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&startTime);

	pausedTime += startTime - stopTime;
	prevTime = startTime;
	stopTime = 0;
	isStopped = false;
}

void GameTimer::Stop()
{
	if (isStopped)
		return;

	__int64 currentTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currentTime);

	stopTime = currentTime;
	isStopped = true;
}

void GameTimer::Tick()
{
	if (isStopped)
	{
		deltaTime = 0.0;
		return;
	}

	__int64 currentTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currentTime);
	this-> currentTime = currentTime;

	deltaTime = (currentTime - prevTime) * secondsPerCount;
	prevTime = currentTime;

	if (deltaTime < 0.0)
	{
		deltaTime = 0.0;
	}
}
