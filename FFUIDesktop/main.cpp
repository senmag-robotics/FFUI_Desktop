#include "main.h"
#include <windows.h>
#include <mmsystem.h>
#include <chrono>
#include <thread>

//timeBeginPeriod/timeEndPeriod live in winmm.lib
#pragma comment(lib, "winmm.lib")


void main(void) {
	DeviceManager	deviceManager;
	FFUIDesktop		ffuiDesktop;


	FFUIDesktop_Config desktopConfig;
	desktopConfig.targetHapticFramerate = 500;

	desktopConfig.targetUIFramerate = 10;
	desktopConfig.screenSize.x = GetSystemMetrics(SM_CXSCREEN);
	desktopConfig.screenSize.y = GetSystemMetrics(SM_CYSCREEN);
	desktopConfig.cursorFilter = 0.96;

	ffuiDesktop.initDesktop(desktopConfig);




	float cursorGain = 20;




	//Windows' default scheduler tick is ~15.6ms, which is far too coarse to pace a
	//100Hz haptic loop with sleep_for/Sleep() alone - a single Sleep(1) can overshoot
	//by 10ms+. timeBeginPeriod(1) asks the OS for a ~1ms tick instead, which combined
	//with the hybrid sleep+spin below gets us close enough to the target rate for the
	//force feedback to feel consistent rather than juddery.
	timeBeginPeriod(1);

	const std::chrono::duration<double> hapticFrameDuration(1.0 / desktopConfig.targetHapticFramerate);
	auto nextFrameTime = std::chrono::steady_clock::now();

	//How long before the deadline we stop sleeping and start spinning to close the
	//gap precisely. Sleep() (even at 1ms scheduler resolution) can still overshoot by
	//a couple of ms, so we deliberately wake early and busy-wait the remainder.
	//
	//FIX, new this round ("we're seeing quite some processor load"): this was still a
	//full 2 milliseconds - fine back when the comment above was written against a
	//100Hz target (a 10ms frame, leaving 8ms of genuine OS sleep and only a 2ms spin),
	//but targetHapticFramerate is now 500 (a 2ms frame - see desktopConfig above), so a
	//2ms spin margin covered the ENTIRE frame: sleepUntil (nextFrameTime - spinMargin)
	//was landing at or before "now" on essentially every iteration, meaning the
	//sleep_until() call below was almost never actually taken and this thread was
	//busy-spinning (this_thread::yield() in a tight loop, which still keeps a full CPU
	//core busy - yielding only offers up the current scheduling quantum, it doesn't
	//idle the core) for the full 2ms of literally every single frame, forever - a
	//constant, ~100%-of-one-core cost that has nothing to do with anything else this
	//app does, present even sitting fully idle. Cut to well under half a millisecond
	//instead, so the large majority of each 2ms frame now goes through a real
	//sleep_until() (thread genuinely off-CPU) and only the last stretch is spent
	//precision-spinning - same hybrid sleep+spin strategy the comment above describes,
	//just re-scaled to the frame period actually in use today.
	const std::chrono::microseconds spinMargin(350);

	while (1) {
		ffuiDesktop.updateFrame();

		nextFrameTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(hapticFrameDuration);
		auto now = std::chrono::steady_clock::now();

		if (nextFrameTime > now) {
			auto sleepUntil = nextFrameTime - spinMargin;
			if (sleepUntil > now) {
				std::this_thread::sleep_until(sleepUntil);
			}
			//Spin for the last stretch so the frame timing is accurate rather than
			//just "at least" the target period.
			while (std::chrono::steady_clock::now() < nextFrameTime) {
				std::this_thread::yield();
			}
		}
		else {
			//A frame took longer than the haptic budget (eg. the scanner thread held
			//the objects mutex for a while). Rather than trying to catch up - which
			//would fire a burst of frames back-to-back - resync to now so the loop
			//settles back onto a steady cadence (targetHapticFramerate, currently 500Hz)
			//from here.
			nextFrameTime = now;
		}
	}

	timeEndPeriod(1);
}