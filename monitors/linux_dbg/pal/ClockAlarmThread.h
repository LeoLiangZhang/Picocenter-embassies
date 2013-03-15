/* Copyright (c) Microsoft Corporation                                       */
/*                                                                           */
/* All rights reserved.                                                      */
/*                                                                           */
/* Licensed under the Apache License, Version 2.0 (the "License"); you may   */
/* not use this file except in compliance with the License.  You may obtain  */
/* a copy of the License at http://www.apache.org/licenses/LICENSE-2.0       */
/*                                                                           */
/* THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS     */
/* OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION      */
/* ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR   */
/* PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.                              */
/*                                                                           */
/* See the Apache Version 2.0 License for specific language governing        */
/* permissions and limitations under the License.                            */
/*                                                                           */
#pragma once

#include "pal_abi/pal_types.h"
#include "zemit.h"
#include "cheesylock.h"
#include "HostAlarms.h"

class ZutexTable;

class ClockAlarmThread
{
public:
	ClockAlarmThread(ZLCEmit *ze, HostAlarms *host_alarms);
	void reset_alarm(uint64_t next_alarm);
	uint64_t get_time();

private:
	CheesyLock alarm_lock;

	HostAlarms *host_alarms;

	// the current alarm
	uint64_t next_alarm;
	bool expired;	// alarm has been delivered since last set.

	// poked to be sure we know that the current alarm has changed
	int alarm_futex;

	ZLCEmit *ze;

	static int start(void *v_this);
	void run();
};

