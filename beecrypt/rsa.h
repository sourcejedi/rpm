/** \ingroup RSA_m
 * \file rsa.h
 *
 * RSA Encryption & signature scheme, header.
 */

/*
 * Copyright (c) 2000, 2002 Virtual Unlimited B.V.
 *
 * Author: Bob Deblier <bob@virtualunlimited.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _RSA_H
#define _RSA_H

#include "rsakp.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 */
BEECRYPTAPI /*@unused@*/
int rsapri   (const rsakp* kp, const mpnumber* m, mpnumber* c)
	/*@modifies c */;

/**
 */
BEECRYPTAPI /*@unused@*/
int rsapricrt(const rsakp* kp, const mpnumber* m, mpnumber* c)
	/*@modifies c */;

/**
 */
BEECRYPTAPI /*@unused@*/
int rsavrfy  (const rsapk* pk, const mpnumber* m, const mpnumber* c)
	/*@*/;

#ifdef __cplusplus
}
#endif

#endif
