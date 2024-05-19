/*
--------------------------------------------------------------------------------
This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.
This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.
You should have received a copy of the GNU Library General Public
License along with this library; if not, write to the
Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
Boston, MA  02110-1301, USA.
--------------------------------------------------------------------------------
*/

// Copyright (c) 2016-2021 John Seamons, ZL4VO/KF6VO

#pragma once

#include "types.h"
#include "coroutines.h"     // lock_t

struct mg_connection;


typedef struct nbuf_st {
	u4_t magic;
	struct mg_connection *mc;
	char *buf;
	u2_t len, ttl, id;
	bool done, expecting_done, dequeued, isFree;
	u4_t magic_b;
	struct nbuf_st *next, *prev;
	u4_t magic_e;
} nbuf_t;

typedef struct {
	struct mg_connection *mc;
	lock_t lock;
	u4_t magic_b;
	nbuf_t *q, *q_head;
	u4_t magic_e;
	u2_t cnt, ttl;
	bool ovfl, dbug;
} ndesc_t;

void ndesc_init(ndesc_t *nd, struct mg_connection *mc);
void ndesc_register(ndesc_t *nd);

void nbuf_init();
void nbuf_stat();
void nbuf_allocq(ndesc_t *nd, char *s, int sl);
nbuf_t *nbuf_dequeue(ndesc_t *nd);
int nbuf_queued(ndesc_t *nd);
void nbuf_cleanup(ndesc_t *nd);
