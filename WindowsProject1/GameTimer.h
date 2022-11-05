#pragma once
#include <Windows.h>

class GameTimer
{
public:
	GameTimer();

	float GameTime() const;
	float DeltaTime() const;

	void Reset();
	void Start();
	void Stop();
	void Tick();

private:
	double secondsPerCount;
	double deltaTime;

	__int64 baseTime;
	__int64 pausedTime;
	__int64 stopTime;
	__int64 prevTime;
	__int64 currentTime;

	bool isStopped;
};