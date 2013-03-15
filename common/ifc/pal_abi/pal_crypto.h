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

/*
Rationale:
  Zoog abidcates almost all integrity, privacy, and authentication
  responsibilities to apps themselves, where vendors are in a better
  position to ask users sane questions about high-level app objects.
  Zoog provides the weakest semantics we can imagine: apps are set adrift
  on a high-seas network to fend for themselves. Apps even have to provide
  privacy over disk storage.

  The one tool apps need to build integral, private, authenticated
  relationships (including with their "own" disk storage) in that world
  is crypto. We assert that these are the minimal primitives necesssary
  to bootstrap crypto.
  get_secret provides an app with a persistent way to bootstrap itself
  (without requiring the user to type a passphrase into each app),
  and to verify the integrity of its state stored in untrusted services
  (the only kind, in the bootstrap case);
  get_random enables the app to generate secret keys (for secret storage
  and communication), public keys (for integrity and authentication).
*/

/*----------------------------------------------------------------------
| Function Pointer: zf_zoog_endorse_me
|
| Purpose:  Endorses a key belonging to this application
|
| Parameters: 
|   local_key       (IN) -- Key generated by the application 
|   cert_buffer_len (IN) -- Size of the buffer allocated for cert
|   cert_buffer     (OUT) -- Raw bytes of cert created by monitor
|
| Returns:  Certificate for the key, signed by the Zoog TCB
----------------------------------------------------------------------*/
typedef void (*zf_zoog_endorse_me)(
	ZPubKey* local_key, uint32_t cert_buffer_len, uint8_t* cert_buffer);

/*
Purpose:
	The host provides each application with a block of secret key material
	unique to each application on this particular host machine.

	An expected implementation is that the host machine has a constant-size
	host secret (secured for example in a TPM, in NVRAM to which only the TCB
	has access, or a section of disk to which access can be revoked after
	boot even if an untrusted driver is later allowed access to the disk).
	To satisfy this get_app_secret(), the host uses the host secret as a key
	to a PRF that it applies to the application's identity to produce a secret 
	known only to the app instance on this machine and the host itself.

	The amount of material available is implementation-dependent; the
	application is responsible to truncate or pad to desired size
*/
/*----------------------------------------------------------------------
| Function Pointer: zf_zoog_get_app_secret
|
| Purpose:  Retrieve a kernel-supplied (and hence, platform-specific), 
|           app-specific secret value
|
| Parameters: 
|   num_bytes_requested (IN)  -- Number of secret bytes app would like 
|   buffer              (OUT) -- App-supplied buffer to be filled in with secret data
|
| Returns:  Number of secret bytes the kernel actually supplied
----------------------------------------------------------------------*/
typedef uint32_t (*zf_zoog_get_app_secret)(
  uint32_t num_bytes_requested, uint8_t* buffer);


/*
Purpose:
  Applications may need a source of randomness, for example to seed
  a PRNG, or to generate DH or public keys.
  The host collects and stirs randomness from entropy sources (e.g. HIDs).
  (Bryan, can we use network and disk traffic, or would that enable an
  attacker to dilute the entropy without the system's estimate accounting
  correctly for the dilution?)
  This call returns the minimum of the requested buffer size or the
  amount of randomness available to use immediately; it is nonblocking.
  TODO introduce a ZoogHostAlarm zutex to enable an application to
  wait on the arrival of more entroy.
  TODO the flow of entropy is a (scarce!) resource; we may need a way
  to keep bad actors from starving other apps by slurping up the whole
  supply as it becomes available. It may be sufficient to use a bucket
  scheme: the host fills each app's bucket round-robin; an app can drain
  its bucket, and then has to wait until new entropy arrives. This works
  (without further hierarchical allocation) if well-behaved apps only
  need periodic, small bursts of randomness (the bucket size).
  I guess, as with anything round and robinny, there's still a Sybil
  attack: a million Sybils can reduce the flow into legitimate buckets
  to a trickle. So we may still need some better resource allocation story for
  this resource.
*/
/*----------------------------------------------------------------------
| Function Pointer: zf_zoog_get_random
|
| Purpose:  Retrieve some good randomness 
|
| Parameters: 
|   num_bytes_requested (IN)  -- Number of random bytes app would like 
|   buffer              (OUT) -- App-supplied buffer to be filled in with random data
|
| Returns:  Number of random bytes the kernel actually supplied
----------------------------------------------------------------------*/
typedef uint32_t (*zf_zoog_get_random)(
  uint32_t num_bytes_requested, uint8_t* buffer);
