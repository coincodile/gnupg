/* seskey.c -  make sesssion keys etc.
 *	Copyright (C) 1998 Free Software Foundation, Inc.
 *
 * This file is part of GNUPG.
 *
 * GNUPG is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GNUPG is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "util.h"
#include "cipher.h"
#include "mpi.h"
#include "main.h"


/****************
 * Make a session key and put it into DEK
 */
void
make_session_key( DEK *dek )
{
    switch( dek->algo ) {
      case CIPHER_ALGO_BLOWFISH:
	dek->keylen = 20;
	randomize_buffer( dek->key, dek->keylen, 1 );
	break;
      case CIPHER_ALGO_BLOWFISH128:
	dek->keylen = 16;
	randomize_buffer( dek->key, dek->keylen, 1 );
	break;

      default: log_bug("invalid algo %d in make_session_key()\n", dek->algo);
    }
}


/****************
 * Encode the session key. NBITS is the number of bits which should be used
 * for packing the session key.
 * returns: A mpi with the session key (caller must free)
 */
MPI
encode_session_key( DEK *dek, unsigned nbits )
{
    int nframe = (nbits+7) / 8;
    byte *p;
    byte *frame;
    int i,n,c;
    u16 csum;
    MPI a;

    /* the current limitation is, that we can only use a session key
     * which length is a multiple of BITS_PER_MPI_LIMB
     * I think we can live with that.
     */
    if( dek->keylen + 7 > nframe || !nframe )
	log_bug("can't encode a %d bit key in a %d bits frame\n",
		    dek->keylen*8, nbits );

    /* We encode the session key in this way:
     *
     *	   0  2  RND(n bytes)  0  A  DEK(k bytes)  CSUM(2 bytes)
     *
     * RND are non-zero random bytes.
     * A   is the cipher algorithm
     * DEK is the encryption key (session key) length k depends on the
     *	   cipher algorithm (20 is used with blowfish).
     * CSUM is the 16 bit checksum over the DEK
     */
    csum = 0;
    for( p = dek->key, i=0; i < dek->keylen; i++ )
	csum += *p++;

    frame = m_alloc_secure( nframe );
    n = 0;
    frame[n++] = 0;
    frame[n++] = 2;
    i = nframe - 6 - dek->keylen;
    assert( i > 0 );
    for( ; i ; i-- ) {
	while( !(c = get_random_byte(1)) )
	    ;
	frame[n++] = c;
    }
    frame[n++] = 0;
    frame[n++] = dek->algo;
    memcpy( frame+n, dek->key, dek->keylen ); n += dek->keylen;
    frame[n++] = csum >>8;
    frame[n++] = csum;
    assert( n == nframe );
    a = mpi_alloc_secure( (nframe+BYTES_PER_MPI_LIMB-1) / BYTES_PER_MPI_LIMB );
    mpi_set_buffer( a, frame, nframe, 0 );
    m_free(frame);
    return a;
}


static MPI
do_encode_md( MD_HANDLE md, int algo, size_t len, unsigned nbits,
				   const byte *asn, size_t asnlen )
{
    int nframe = (nbits+7) / 8;
    byte *frame;
    int i,n;
    MPI a;

    if( len + asnlen + 4  > nframe )
	log_bug("can't encode a %d bit MD into a %d bits frame\n",
		    (int)(len*8), (int)nbits);

    /* We encode the MD in this way:
     *
     *	   0  A PAD(n bytes)   0  ASN(asnlen bytes)  MD(len bytes)
     *
     * PAD consists of FF bytes.
     */
    frame = md_is_secure(md)? m_alloc_secure( nframe ) : m_alloc( nframe );
    n = 0;
    frame[n++] = 0;
    frame[n++] = algo;
    i = nframe - len - asnlen -3 ;
    assert( i > 1 );
    memset( frame+n, 0xff, i ); n += i;
    frame[n++] = 0;
    memcpy( frame+n, asn, asnlen ); n += asnlen;
    memcpy( frame+n, md_read(md, algo), len ); n += len;
    assert( n == nframe );
    a = md_is_secure(md)?
	 mpi_alloc_secure( (nframe+BYTES_PER_MPI_LIMB-1) / BYTES_PER_MPI_LIMB )
	 : mpi_alloc( (nframe+BYTES_PER_MPI_LIMB-1) / BYTES_PER_MPI_LIMB );
    mpi_set_buffer( a, frame, nframe, 0 );
    m_free(frame);
    return a;
}


MPI
encode_md_value( MD_HANDLE md, unsigned nbits )
{
    int algo = md_get_algo(md);
    const byte *asn;
    size_t asnlen, mdlen;

    asn = md_asn_oid( algo, &asnlen, &mdlen );
    return do_encode_md( md, algo, mdlen, nbits, asn, asnlen );
}

