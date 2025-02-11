/*	$NetBSD: errno.h,v 1.5 2021/12/19 10:59:27 riastradh Exp $	*/

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

/*
 * Notes on porting:
 *
 * - Linux consistently passes around negative errno values.  NetBSD
 *   consistently passes around positive ones, except the special magic
 *   in-kernel ones (EJUSTRETURN, ERESTART, &c.) which should not be
 *   exposed to userland *or* linux-only code using the negative pointer
 *   means error return pattern.  Be careful!  If Using ERESTARTSYS from
 *   Linux code, be sure it is remapped back to ERESTART before NetBSD
 *   code sees it.
 */

#ifndef _LINUX_ERRNO_H_
#define _LINUX_ERRNO_H_

#include <sys/errno.h>

#define	ERESTARTSYS	(ELAST+1)	/* XXX */
#define	ENOTSUPP	ENOTSUP	/* XXX ???  */
#define	EREMOTEIO	EIO	/* XXX Urk...  */
#define	ECHRNG		ERANGE	/* XXX ??? */
#define	EHWPOISON	EIO
#define ERFKILL		EIO

#endif  /* _LINUX_ERRNO_H_ */
