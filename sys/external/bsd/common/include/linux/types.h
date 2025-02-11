/*	$NetBSD: types.h,v 1.3 2021/12/19 01:42:09 riastradh Exp $	*/

/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Taylor R. Campbell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _LINUX_TYPES_H_
#define _LINUX_TYPES_H_

#include <sys/types.h>
#include <sys/bus.h>
#include <sys/stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef uint8_t __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef int8_t __s8;
typedef int16_t __s16;
typedef int32_t __s32;
typedef int64_t __s64;

typedef uint16_t __le16;
typedef uint32_t __le32;
typedef uint64_t __le64;

typedef uint16_t __be16;
typedef uint32_t __be32;
typedef uint64_t __be64;

#define	S8_C	INT8_C
#define	S16_C	INT16_C
#define	S32_C	INT32_C
#define	S64_C	INT64_C

#define	U8_C	UINT8_C
#define	U16_C	UINT16_C
#define	U32_C	UINT32_C
#define	U64_C	UINT64_C

/*
 * This is used for absolute bus addresses, so it has to be bus_addr_t
 * and not bus_size_t; bus_addr_t is sometimes wider than bus_size_t.
 */
typedef bus_addr_t resource_size_t;

typedef paddr_t phys_addr_t;

typedef bus_addr_t dma_addr_t;

/* XXX Is this the right type?  */
typedef unsigned long long cycles_t;

/* XXX Not sure this is correct.  */
typedef off_t loff_t;

/* For iwlwifi */
typedef uint16_t __sum16;

typedef const char * acpi_string;

#define DECLARE_BITMAP(NAME, BITS)					      \
	unsigned long NAME[((BITS) + ((NBBY*sizeof(unsigned long)) - 1)) /    \
		(NBBY*sizeof(unsigned long))]

/* Definition copied in <linux/kernel.h> for convenience.  */
#define	__user

struct list_head {
	struct list_head *prev;
	struct list_head *next;
};

#endif  /* _LINUX_TYPES_H_ */
