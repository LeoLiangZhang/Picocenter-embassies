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

#include <stdint.h>
#include "SyncFactory.h"

class AckWait
{
private:
	uint64_t rpc_nonce;
	SyncFactoryEvent *evt;

public:
	AckWait(uint64_t rpc_nonce, SyncFactory *sf);
	AckWait(uint64_t rpc_nonce);	// key ctor
	~AckWait();
	bool received(int timeout_msec);
	void signal();
	static uint32_t hash(const void *v_a);
	static int cmp(const void *v_a, const void *v_b);
};

