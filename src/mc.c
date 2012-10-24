#include "mc.h"
#include "init.h"


#ifndef __likes_mc__
#define __likes_mc__
#endif
#ifdef __likes_mc__

#include <sys/stat.h>
#include <sys/time.h>
#define BUFSIZE 512

//TODO nax...

extern FILE 			*flog;
extern int				max_clients;
extern fd_ctx			*clients;
extern int 				is_finish;
extern int 				is_trace;

extern struct timeval t_start; 				// start timeinterval
extern struct timeval t_end;				// finish timeinterval

extern 	 struct {
	/* some stat data */
	unsigned	connectionss;				//  active connectionss (memcache clients) 
	unsigned	cnn_count;				//  count connectionss 
	unsigned	cmd_per;					//  count of commands into period	
	unsigned	cmd_count;					//  count of commands
	float		rps;						//  last count of commands per second
	float		rps_peak;					//  peak of commands per second	
	unsigned	get;						//  count of get commands
	unsigned	set;						//  count of set/append/prepend/incr/decr  commands
	unsigned	del;						//  count of delete  commands
	unsigned	inc;						//  count of increment/decrement  commands		
	unsigned	miss;						//  count of miss keys (key not found)
	time_t 		uptime;						// uptime server
	unsigned	err;						//  count of errors
} stats;

void periodic_watcher(EV_P_ ev_timer *t, int revents);
static ev_io* memcached_client_new(int sock);
static void memcached_client(EV_P_ ev_io *io, int revents);
static void memcached_client_free(memcache_ctx *ctx);
static int setup_socket(int sock);

int num_digits(unsigned x)  
{  
    return (x < 10 ? 1 :   
        (x < 100 ? 2 :   
        (x < 1000 ? 3 :   
        (x < 10000 ? 4 :   
        (x < 100000 ? 5 :   
        (x < 1000000 ? 6 :   
        (x < 10000000 ? 7 :  
        (x < 100000000 ? 8 :  
        (x < 1000000000 ? 9 :  
        10)))))))));  
}

void close_io(EV_P_ ev_io *io)
{
	//printf("%s fd=%d\n",__FUNCTION__, io->fd);
	ev_io_stop(EV_A_ io);
	close(io->fd);
}

void close_all(EV_P) {
//	printf("%s \n",__FUNCTION__);
	int i;
	for (i=0; i < max_clients; i++) {
		if (clients[i].flags & FD_ACTIVE) {
			close_io(EV_A_ clients[i].io);
		}

		if(clients[i].mc_ctx) {
			if(clients[i].mc_ctx->value)
				free(clients[i].mc_ctx->value);
			clients[i].mc_ctx->value = NULL;
		
			free(clients[i].mc_ctx);
			clients[i].mc_ctx = NULL;
		}
	}
}

void cllear_mc_all()
{
	int i;
	for (i=0; i < max_clients; i++) {
		if (clients[i].mc_ctx) {
			if (clients[i].mc_ctx->value) {
				free(clients[i].mc_ctx->value);
				clients[i].mc_ctx->value = NULL;
			}	
			free(clients[i].mc_ctx);
			clients[i].mc_ctx = NULL;
		}
		//if(FD_ACTIVE) close_io(EV_A_ clients[i].io);
	}

}

void
periodic_watcher(EV_P_ ev_timer *t, int revents)
{
	gettimeofday(&t_end, NULL);
	long mtime, seconds, useconds;    

    seconds  = t_end.tv_sec  - t_start.tv_sec;
    useconds = t_end.tv_usec - t_start.tv_usec;
    mtime = ((seconds) * 1000 + useconds/1000.0) + 0.5;
	stats.rps = stats.cmd_per * 1000 /mtime ;
	if(stats.rps_peak < stats.rps)	stats.rps_peak = stats.rps;
	stats.cmd_per = 0;

	gettimeofday(&t_start, NULL);
}


/* Set misc socket parameters: non-blocking mode, linger, enable keep alive. */
static int 
setup_socket(int sock)
{
	int keep_alive = 1;
	struct linger l = {0, 0};
	if (set_nonblock(sock, 1) == -1) return -1;
	if (setsockopt(sock, SOL_SOCKET, SO_LINGER, &l, sizeof(l)) == -1) return -1;
	if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(keep_alive)) == -1) return -1;
	return 0;
}

static void
memcached_client_free(memcache_ctx *ctx)
{
	stats.connectionss--;	
	if(is_trace)
		printf("connection fd=%d free [%d]\n", ctx->io.fd, stats.connectionss);

	if (!ctx) return;

//	printf("%s ctx=%x free clients[%d].ctx=NULL\n",__FUNCTION__, ctx , ctx->io.fd);
	if (ctx->value) free(ctx->value);
	free(ctx);
	clients[ctx->io.fd].mc_ctx = NULL;
//	printf("%s Ok\n",__FUNCTION__);
}

/*process data for set*/
static int 
memcached_process_set( memcache_ctx *mctx, likes_ctx* ctx) {

	int len=0;
	unsigned value=0;

	int type = ctx->types[mctx->prefix];
	 // fprintf(flog,"set key type=%d\n", type);	
	ctx->type = type;
	switch	(type) {
		case LK_TYPE_PREFIX_COUNTER: 
			len = sscanf(mctx->value, "%u", &value);
			if (len!=1) return 1;
			 //fprintf(flog,"likes_set  [TYPE_PREFIX_COUNTER]  %u.%u %u\n", mctx->prefix, mctx->crc, value );			
			likes_set(ctx, mctx->prefix, mctx->crc, &value, sizeof(unsigned));
			break;
		default : 
			 // fprintf(flog,"likes_set  [TYPE_PREFIX_ANY]  %u.%u\n", mctx->prefix, mctx->crc );
			likes_set(ctx, mctx->prefix, mctx->crc, mctx->value, mctx->data_size);
	}

	stats.set ++;
	return 0;
}	


/*process data from append*/
static int 
memcached_process_append( memcache_ctx *mctx, likes_ctx* ctx) {

	int len=0;
	unsigned  index=0;

	int type = ctx->types[mctx->prefix];
	ctx->type = type;
	switch(type) {
		case LK_TYPE_PREFIX_INDEX: 
		    //fprintf(flog, "type=%d [TYPE_PREFIX_INDEX]\n", type);
			len = sscanf(mctx->value, "%u", &index);
			if (len!=1) return 1;
			 //fprintf(flog,"likes_append [friends]  key=%u.%u data[crc=%u data=%u]\n", mctx->prefix, mctx->crc, crc, value );
			
			 //fprintf(flog,"likes_append  data[crc=%u data=%u]\n", data.crc, data.data );
			likes_friends_add_like(ctx, mctx->crc, index);
			
			break;
		case LK_TYPE_PREFIX_COUNTER: 
			return 1;
		default : {
			char mix_key[LK_KEYSIZE];
			create_key(mctx->prefix, mctx->crc, (char*) mix_key);
			 //fprintf(flog,"type=%d [TYPE_PREFIX_ANY]\n", type);	
			 //fprintf(flog,"likes_append [def] %u.%u '%s'\n", mctx->prefix, mctx->crc, mctx->value);
			
			bool res = tchdbputcat(ctx->hdb, mix_key, LK_KEYSIZE, (const char *)mctx->value, (mctx->value_size-2));
			if (!res) {
				ctx->ecode = tchdbecode(ctx->hdb);

				if (ctx->ecode == TCENOREC)	{
					stats.miss ++;
					return 2;
				}

				fprintf(flog, "append error: %s\n", tchdberrmsg(ctx->ecode));
				return 1;
			}
		}
	}

	stats.set ++;
	return 0;
}	


/*process data for prepend*/
static int 
memcached_process_prepend( memcache_ctx *mctx, likes_ctx* ctx) {
	
	printf("%s\n ",__FUNCTION__ );

	int len=0;
	unsigned index = 0;

	int type = ctx->types[mctx->prefix];
	
	switch(type) {
		case LK_TYPE_PREFIX_INDEX: 
		    // fprintf(flog, "type=%d [TYPE_PREFIX_INDEX]\n", type);	
			len = sscanf(mctx->value, "%u", &index);
			if (len!=1) return 1;
			 // fprintf(flog,"likes_remove [friends]  key=%u.%u data[crc=%u data=%u]\n", mctx->prefix, mctx->crc, crc, value );
			 // fprintf(flog,"likes_remove  data[crc=%u data=%u]\n", data.crc, data.data );
			likes_friends_remove_like(ctx, mctx->crc, index);
			
			break;
		case LK_TYPE_PREFIX_COUNTER: 
			return 1;
		default : {
			char mix_key[LK_KEYSIZE];
			create_key(mctx->prefix, mctx->crc, (char*) mix_key);
			 // fprintf(flog,"type=%d [TYPE_PREFIX_ANY]\n", type);	
			 // fprintf(flog,"likes_prepend [def] %u.%u '%s'\n", mctx->prefix, mctx->crc, mctx->value);
			int size = 0;
			void * p = tchdbget(ctx->hdb, mix_key, LK_KEYSIZE, &size);
			if (!p) {
				ctx->ecode = tchdbecode(ctx->hdb);

				if (ctx->ecode == TCENOREC)	{
					stats.miss ++;
					return 2;
				}


				fprintf(flog, "prepend (get) error: %s\n", tchdberrmsg(ctx->ecode));				
			}
			
			//mctx->value_size-2
			void * tmp = malloc(size + mctx->value_size -2);

			memcpy(tmp,mctx->value,mctx->value_size - 2);			
			if (size)
				memcpy(tmp+mctx->value_size - 2,p,size);						
			
			bool res = tchdbput(ctx->hdb, mix_key, LK_KEYSIZE, (const char *)tmp, (size+mctx->value_size-2));
			free(tmp);
			free(p);
			if (!res) {
				ctx->ecode = tchdbecode(ctx->hdb);
				fprintf(flog, "prepend error: %s\n", tchdberrmsg(ctx->ecode));
				return 1;
			}
		}
	}

	stats.set ++;	
	return 0;
}	


static void 
memcached_cb_set(EV_P_ ev_io *io, int revents) {
	 //fprintf(flog,"%s revents=%d\n",__FUNCTION__, revents);
	memcache_ctx *mctx = (memcache_ctx*)io;

	if(is_trace) 
		printf("\n--------  data len=%d -----------\n'%s'\n", mctx->value_len, mctx->value);

	while (mctx->value_len < mctx->value_size) {
		size_t bytes = read(io->fd, mctx->value + mctx->value_len, mctx->value_size - mctx->value_len);
		if (bytes > 0) {
			if (bytes > BUFSIZE) {
				fprintf(flog, "%s reciive bytes %d more than BUFSIZE\n", __FUNCTION__, bytes);
				goto error;
			}
			mctx->value_len += bytes;
		} else if(bytes == -1){
			if (errno == EAGAIN) return;  
			if (errno == EINTR) continue; 
			goto disconnect;
		}		
	}
	
	set_client_timeout(io, RECV_TIMEOUT);
	mctx->value[mctx->value_size-2] = 0;
	
	//fprintf(flog,"%s value=%s (len=%d)  key=%u prefix=%u len=%d size=%d\n",__FUNCTION__,  mctx->value, strlen(mctx->value) , mctx->crc, mctx->prefix,mctx->value_len, mctx->value_size );

	likes_ctx* ctx = ev_userdata(EV_A);	
	int ret = 0;
	switch (mctx->mode) {
		case 0 : 
			ret = memcached_process_set(mctx, ctx);
			break;
		case 1 : 
			ret = memcached_process_append(mctx, ctx);
			break;
		case 2 :
			ret = memcached_process_prepend(mctx, ctx);
			break;
		default : goto error;
	};
		
	switch (ret) {
		case 1 : goto error; break;
		case 2 : goto notstored; break;
	}

	mctx->cmd_len = 0;
	ev_io_stop(EV_A_ io);
	ev_set_cb(io, memcached_client);
	ev_io_set(io, io->fd, EV_WRITE);
	obuffer_init(&mctx->response, "STORED\r\n", sizeof("STORED\r\n") - 1);
	ev_io_start(EV_A_ io);
	return;

error:
	mctx->cmd_len = 0;
	ev_io_stop(EV_A_ io);
	ev_set_cb(io, memcached_client);
	ev_io_set(io, io->fd, EV_WRITE);
	obuffer_init(&mctx->response, "ERROR\r\n", sizeof("ERROR\r\n") - 1);
	ev_io_start(EV_A_ io);
	return;

notstored:
	mctx->cmd_len = 0;
	ev_io_stop(EV_A_ io);
	ev_set_cb(io, memcached_client);
	ev_io_set(io, io->fd, EV_WRITE);
	obuffer_init(&mctx->response, "NOT_STORED\r\n", sizeof("NOT_STORED\r\n") - 1);
	ev_io_start(EV_A_ io);
	return;

disconnect:	
	close_io(EV_A_ io);	
	return;	
}

/* memcache GET handler */
int memcache_get(EV_P_  memcache_ctx *mctx) {
	int len;
	char  *key = mctx->cmd + 4;
	unsigned prefix,crc;
	int data_len=0;

	len = sscanf(key, "%u.%u", &prefix, &crc);
	if ( len != 2)	{
		return 1;
	}
	
	likes_ctx* ctx = ev_userdata(EV_A);	
	
	memset(mctx->value, BUFSIZE, '\0');
	
	char mix_key[LK_KEYSIZE];
	create_key(prefix,crc, (char*) mix_key);
	
	int type = ctx->types[prefix];

	TCHDB *hdb = (type == LK_TYPE_PREFIX_COUNTER) ? ctx->hdb_counter : ctx->hdb;

	void * pdata = tchdbget(hdb, mix_key, LK_KEYSIZE, &data_len);	
	if(!pdata) {
		ctx->ecode = tchdbecode(hdb);
		if (ctx->ecode == TCENOREC) goto end; 		//no record found = 22

		fprintf( flog, "%s, get tcdb error: %s (%d)\n", __FUNCTION__, tchdberrmsg(ctx->ecode), ctx->ecode );
		goto error;
	}

	if (prefix >ctx->types[0]) goto error;	
	
	unsigned *pvalue;
	
	switch(type) {
		case LK_TYPE_PREFIX_COUNTER: 
//			printf("%s type=COUNTER \n", __FUNCTION__);
			pvalue = (unsigned*)pdata;
			data_len = num_digits(*pvalue);			
			len = snprintf(mctx->value, BUFSIZE, "VALUE %u.%u 0 %u\r\n%u\r\nEND\r\n", prefix, crc, data_len,*pvalue );
			break;
		
		case LK_TYPE_PREFIX_INDEX: 
				len=data_len;
				unsigned *p = (unsigned *)pdata;	
				while( len ) {
					 //fprintf(flog,"%u %u\n", p->crc, p->data );
					p++;
					len -= sizeof(unsigned);
				}
			len = snprintf(mctx->value, BUFSIZE, "VALUE %u.%u 0 %u\r\n", prefix, crc, data_len );
			memcpy(mctx->value+len,pdata,data_len);
			len +=data_len;
			strcpy(mctx->value+len,"\r\nEND\r\n" );
			len += sizeof("\r\nEND\r\n");
			break;
		default : 
			len = snprintf(mctx->value, BUFSIZE, "VALUE %u.%u 0 %u\r\n%s\r\nEND\r\n", prefix, crc, data_len, (char*)pdata );
	}

	free(pdata);
	obuffer_init(&mctx->response, mctx->value, len);
	
	stats.get ++;
	return 0;

error:
	return 1;
end:
	stats.miss ++;
	return 2;	

}


/* memcache DELETE handler */
int memcache_del(EV_P_  memcache_ctx *mctx) {
	int len;
	char  *key = mctx->cmd + 7;
	unsigned prefix,crc;

	len = sscanf(key, "%u.%u", &prefix, &crc);
	if ( len != 2)	{
		return 1;
	}	 
	
	likes_ctx* ctx = ev_userdata(EV_A);	
	
//	memset(mctx->value, BUFSIZE, '\0');
	
	char mix_key[LK_KEYSIZE];
	create_key(prefix,crc, (char*) mix_key);

	int type = ctx->types[prefix];
	TCHDB *hdb = type == LK_TYPE_PREFIX_COUNTER ? ctx->hdb_counter : ctx->hdb;
	
	ctx->ecode = 0;
	bool res = tchdbout(hdb, (const void *)mix_key, LK_KEYSIZE);	
	if (res) {
		len=9;
		strcpy(mctx->value, "DELETED\r\n" );		
	} else {
		ctx->ecode = tchdbecode(hdb);
		if (ctx->ecode == TCENOREC) {
			len=11;
			stats.miss ++;
			strcpy(mctx->value, "NOT_FOUND\r\n" );	
			
		} else 	{	
			fprintf(flog, "res=false delete tcdb error[%d]: %s\n",ctx->ecode, tchdberrmsg(ctx->ecode));
			goto error;
		}
	}
	
	mctx->value[len] = '\0';
	obuffer_init(&mctx->response, mctx->value, len);
	
	stats.del ++;
	return 0;

error:	
	return 1;
}


/* memcache INCR handler */
int memcache_inc(EV_P_  memcache_ctx *mctx, int mode) {
	int len;
	char  *key = mctx->cmd + 5;
	unsigned prefix,crc,inc;
	int data_len=0, res=0;
	
	len = sscanf(key, "%u.%u %u", &prefix, &crc, &inc);
	if ( len != 3)	{
		return 1;
	}

	inc = !mode ? inc : -inc;
	//printf("[key] %u.%u inc=%u\n",prefix,crc,inc);
	char mix_key[LK_KEYSIZE];
	create_key(prefix,crc, (char*) mix_key);
	
	likes_ctx* ctx = ev_userdata(EV_A);		
	int type = ctx->types[prefix];
		
	TCHDB *hdb = type == LK_TYPE_PREFIX_COUNTER ? ctx->hdb_counter : ctx->hdb;

	switch(type) {
		case LK_TYPE_PREFIX_COUNTER: 
			data_len = tchdbvsiz(hdb, mix_key, LK_KEYSIZE);
			if (data_len > 0) {
				res = tchdbaddint(hdb, mix_key, LK_KEYSIZE, inc);
				if (res == INT_MIN) {
					fprintf( flog, "%s, inc %u.%u error\n", __FUNCTION__ , prefix,crc);
					goto error;
				}
				len = sprintf(mctx->value, "%u\r\n", res);
				break;			
			} else {				
				res = tchdbput(hdb, mix_key, LK_KEYSIZE, &inc, sizeof(unsigned));
				if (res) {
					ctx->ecode = tchdbecode(hdb);
					if (ctx->ecode != TCENOREC) {
//						printf( "%s, put error: %s (%d)\n", __FUNCTION__, tchdberrmsg(ctx->ecode), ctx->ecode );					
						fprintf( flog, "%s, put error: %s (%d)\n", __FUNCTION__, tchdberrmsg(ctx->ecode), ctx->ecode );
						goto error;
					} else
						stats.miss ++;

				}
				len = sprintf(mctx->value, "%u\r\n", inc);
				break;			
			}

		case LK_TYPE_PREFIX_INDEX: 
//			printf("%s type=INDEX path=%s\n", __FUNCTION__, tchdbpath(hdb)  );
			 // fprintf(flog,"inc INDEX type=%d\n", type);			
			res = likes_friends_inc(ctx, crc);
			if (res) goto error;
			strcpy(mctx->value, "1\r\n");
			len=3;
			break;
		default : 
			 fprintf(flog,"inc UNDEF [error] type=%d\n", type);			
			goto error;
		}
	
	obuffer_init(&mctx->response, mctx->value, len);
	stats.inc ++;
	return 0;

error:	
	return 1;
}


/* memcache SET handler */
int memcache_set(EV_P_  memcache_ctx *mctx, int readed, int  end) {
	unsigned flags, exptime, bytes, prefix, crc;
	/* Set command */
	//printf( "[%s] end=%d readed=%d size=%d len=%d\n",__FUNCTION__,  end, readed, mctx->value_size, mctx->value_len  );
	char *p;
	if (mctx->mode == 0) 
		p = mctx->cmd + 4;
	else if (mctx->mode == 1)
		p = mctx->cmd + 7;
	else if (mctx->mode == 2)
		p = mctx->cmd + 8;
	else {
		 //fprintf(flog,"%s [point Err]",__FUNCTION__);	
		return 1;
	}	

	if (sscanf(p, "%u.%u %u %u %u", &prefix, &crc, &flags, &exptime, &bytes) != 5) {
		return 1;
	}
		
	mctx->flag = flags;
	mctx->exptime = exptime;
	int len = mctx->cmd_len - end;
		
	/* Don't send messages longer than 1 megabyte */
	if (!bytes || bytes > 1 << 20) {
		fprintf(flog, "%s invalid message length: %d\n", __FUNCTION__, bytes);
		return 1;
	}
	
	mctx->cmd_len = 0;

	mctx->data_size = bytes;
	mctx->value_size = bytes+2; // TODO ????
	mctx->value_len = len;
	mctx->prefix=prefix;
	mctx->crc=crc;
	
	if (readed > end) {
		if(is_trace) 
			printf("\n--------  data -----------\n'%s'\nlen=%d", mctx->cmd+end, readed-end-3);
			
		memcpy(mctx->value,mctx->cmd+end, readed-end);
		mctx->value[readed-end] = '\0';
		likes_ctx* ctx = ev_userdata(EV_A);	
		switch (mctx->mode) {
			case 0 :  if(memcached_process_set(mctx, ctx)) 		return 1;
				break;
			case 1 : if(memcached_process_append(mctx, ctx)) 	return 1;
				break;
			case 2 : if(memcached_process_prepend(mctx, ctx)) 	return 1;
				break;
			default : 
				return 1;
		};

//		stats.set ++;
		obuffer_init(&mctx->response, "STORED\r\n", sizeof("STORED\r\n") - 1);
		return 0;
	}
	

	if (len) {
		memcpy(mctx->value, mctx->cmd + end, len);
		/* Maybe we've read all value? */
		if (len == mctx->value_size) {
			mctx->value[bytes] = 0;
			fprintf( flog, " ATTENTION!! %s we are read all data\n", __FUNCTION__);
			//mctx->value = NULL;
			obuffer_init(&mctx->response, "STORED\r\n", sizeof("STORED\r\n") - 1);
			stats.err ++;
			return 0;
		}

		return 1;
	}
	
	set_client_timeout((ev_io *)mctx, RECV_TIMEOUT);	
	ev_set_cb((ev_io *)mctx, memcached_cb_set);
	
	return 2;
}

char* show_comment(server_ctx_t* ctx,int n) {

	datatype_t * p = (datatype_t *)ctx->list_datatypes;	
	while(p) {
		if( p->number == n) {
			if (p->comment)
				return p->comment;
			else
				return "-";
		}
		p = p->next;
	}	
	return "-";
}

void memcache_show_types(EV_P_  memcache_ctx *mctx) {
	
	likes_ctx* ctx = ev_userdata(EV_A);
	assert(ctx);
	server_ctx_t* sctx = (server_ctx_t*)ctx->conf;	
	
	int count = ctx->types[0];
	int i=0,len=0;
	int size, *value;			
	
	char *p = mctx->value;
	char *comment = NULL;

	value = ctx->types;
	value++;
	while(count--) {
		i++;
		comment = show_comment(sctx, i);
		switch (*value) {
			case LK_TYPE_PREFIX_DATA: 
				size = sprintf(p,"%d\tDATA\t%s\r\n",i, comment);
				break;
			case LK_TYPE_PREFIX_COUNTER: 
				size = sprintf(p,"%d\tCOUNTER\t%s\r\n",i, comment);
				break;
			case LK_TYPE_PREFIX_INDEX:
				size = sprintf(p,"%d\tINDEX\t%s\r\n",i, comment);
				break;
			 default :
				size = sprintf(p,"%d\tUNDEF(%d)\r\n",i,ctx->types[i]);				
		}
		len += size;
		p += size;	
		value++;
	}
	obuffer_init(&mctx->response, mctx->value, len);
}

static void 
memcached_stats(EV_P_ memcache_ctx *mctx) {
			int len;
			char statsbuf[512];

			time_t t;
			time(&t);
			likes_ctx* ctx = ev_userdata(EV_A);

			len = snprintf(statsbuf, sizeof(statsbuf),
					"STAT pid %ld\r\n"
					"STAT uptime %d\r\n"
					"STAT curent connections %u\r\n"
					"STAT total connections %u\r\n"
					"STAT rps %4.2f\r\n"
					"STAT peak rps %4.2f\r\n"					
					"STAT request %u\r\n"
					"STAT get %u\r\n"
					"STAT set %u\r\n"
					"STAT inc %u\r\n"
					"STAT del %u\r\n"
					"STAT miss %u\r\n"
					"STAT error %u\r\n"
					"STAT counter keys %u\r\n"
					"STAT counter filesize %u\r\n"
					"STAT index keys %u\r\n"
					"STAT index filesize %u\r\n"
					"END\r\n",
					(long)getpid(), (int)(t-stats.uptime) ,stats.connectionss, stats.cnn_count, stats.rps, stats.rps_peak,
					stats.cmd_count, stats.get, stats.set, stats.inc, stats.del, stats.miss, stats.err ,
					(unsigned)tchdbrnum(ctx->hdb_counter), (unsigned)tchdbfsiz(ctx->hdb_counter), (unsigned)tchdbrnum(ctx->hdb), (unsigned)tchdbfsiz(ctx->hdb));
			
			memcpy(mctx->value, statsbuf, len);
			obuffer_init(&mctx->response, mctx->value, len);

}

/* Handle line-based memcached-like protocol */
static void
memcached_client(EV_P_ ev_io *io, int revents) {
	
	memcache_ctx *mctx = ( memcache_ctx*)io;
	if (revents & EV_READ) {
		int end = 0;
		int i = mctx->cmd_len ? mctx->cmd_len - 1 : 0;
				
		/* Read command till '\r\n' (actually -- till '\n') */
		size_t bytes =0;
		while (mctx->cmd_len < MAX_COMMAND_LEN && !end) {
			bytes = read(io->fd, mctx->cmd + mctx->cmd_len, MAX_COMMAND_LEN - mctx->cmd_len);
			if (bytes > 0) {

				if (bytes > BUFSIZE) {
					fprintf( flog, "%s readed=%d more as BUFSIZE\n", __FUNCTION__, bytes);
					goto send_error;
				}
				mctx->cmd_len += bytes;
				while (i < mctx->cmd_len - 1) {
					if (mctx->cmd[i] == '\r' && mctx->cmd[i+1] == '\n') {
						end = i + 2;						
						mctx->cmd[i] = 0;
						break;
					}
					i++;
				}
			} else if (bytes == -1) {
				//printf("%s readed=%d  error=%d\n", __FUNCTION__, bytes, errno);
				if (errno == EAGAIN) break;
				if (errno == EINTR) continue;
				goto disconnect;
			} else goto disconnect;
		}
		
		if (is_trace) 
			printf("%s\nend=%d bytes=%d cmd_len=%d\n",mctx->cmd, end, bytes,  mctx->cmd_len);

		/* If there is no EOL but string is too long, disconnect client */
		if (mctx->cmd_len >= MAX_COMMAND_LEN && !end) goto disconnect;
		/* If we haven't read whole command, set timeout */
		if (!end) {
			set_client_timeout(io, RECV_TIMEOUT);
			return;
		}
		
		stats.cmd_count++;
		stats.cmd_per++;
		/* handle set command */
		if (strncmp(mctx->cmd, "set ", 4) == 0) {
				
			mctx->mode =0;
			switch (memcache_set(EV_A_ mctx, bytes, end)) {
			case 0:
				goto send_reply;
			case 1:
				goto send_error;
			case 2:
				return;
			}

		/* Handle "append" command */
		} else if (strncmp(mctx->cmd, "append ", 7) == 0) {
			mctx->mode =1;
			switch (memcache_set(EV_A_ mctx, bytes, end)) {
			case 0:
				goto send_reply;
			case 1:
				goto send_error;
			case 2:
				return;
			}
		
		/* Handle "prepend" command */
		} else if (strncmp(mctx->cmd, "prepend ", 8) == 0) {
			mctx->mode =2;
			switch (memcache_set(EV_A_ mctx, bytes, end)) {
			case 0:
				goto send_reply;
			case 1:
				goto send_error;
			case 2:
				return;
			}
		/* Handle "Get" command */	
		} else if (strncmp(mctx->cmd, "get ", 4) == 0) {

			switch (memcache_get(EV_A_ mctx)) {
			case 0:
				goto send_reply;
			case 1:
				goto send_error;
			case 2:
				goto send_end;
			}


		/* Handle "Dalete" command */
		} else if (strncmp(mctx->cmd, "delete ", 7) == 0) {

			if (memcache_del(EV_A_ mctx))
				goto send_error;
			else
				goto send_reply;

		} else if (strncmp(mctx->cmd, "incr ", 5) == 0) {

			if (memcache_inc(EV_A_ mctx, 0))
				goto send_error;
			else
				goto send_reply;

		} else if (strncmp(mctx->cmd, "decr ", 5) == 0) {

			if (memcache_inc(EV_A_ mctx, 1))
				goto send_error;
			else
				goto send_reply;
				

		} else if (strncmp(mctx->cmd, "flush_all", 9) == 0) {
			likes_ctx* ctx = ev_userdata(EV_A);	
			if(tchdbvanish(ctx->hdb) && tchdbvanish(ctx->hdb_counter)) {
				obuffer_init(&mctx->response, "OK\r\n", sizeof("OK\r\n") - 1);

				stats.inc = stats.rps_peak = stats.rps = stats.cmd_count = stats.get = stats.set = stats.del = stats.miss = stats.err = 0;

				goto send_reply;
			} else 
				goto send_error;

		} else if (strncmp(mctx->cmd, "reopen", 6) == 0) {
			likes_ctx* ctx = ev_userdata(EV_A);
			likes_destroy(ctx);
			likes_init(ctx);
			obuffer_init(&mctx->response, "OK\r\n", sizeof("OK\r\n") - 1);
			goto send_reply;
			
				
		/* Close connection */
		} if (strncmp(mctx->cmd, "quit", 4) == 0) {
			if (is_finish) goto exit;
			goto disconnect;			
			
		/* Terminate server */
		} if (strncmp(mctx->cmd, "term", 4) == 0) {
			goto exit;
			
		/* show info */
		} else if (strncmp(mctx->cmd, "show types", 10) == 0) {
			memcache_show_types(EV_A_ mctx);
			goto send_reply;

			
		/* Statistics */
		} else if (strncmp(mctx->cmd, "stats", 5) == 0) {
				memcached_stats(EV_A_ mctx);
				goto send_reply;
		} else {
			/* Invalid command */
			fprintf(flog, "invalid command: \"%s\"\n", mctx->cmd);
			goto send_error;
		}
		
	} else if (revents & EV_WRITE) {
		
		switch (obuffer_send(&mctx->response, io->fd)) {
			case 0:	
				mctx->cmd_len = 0;			
				memset(mctx->cmd, 0, MAX_COMMAND_LEN);				
				reset_client_timeout(io);
				ev_io_stop(EV_A_ io);
				if (is_finish) goto exit;
				ev_io_set(io, io->fd, EV_READ);
				ev_io_start(EV_A_ io);
				break;
			case -1:
				goto disconnect;
		}
	}
	return;
send_error:
	stats.err ++;
	obuffer_init(&mctx->response, "ERROR\r\n", sizeof("ERROR\r\n") - 1);
	goto send_reply;

send_end:

	obuffer_init(&mctx->response, "END\r\n", sizeof("END\r\n") - 1);

send_reply:
	set_client_timeout(io, RECV_TIMEOUT);
	mctx->cmd_len = 0;
	ev_io_stop(EV_A_ io);
	ev_io_set(io, io->fd, EV_WRITE);
	ev_io_start(EV_A_ io);
	return;
disconnect:
	close_io(EV_A_ io);
	memcached_client_free(mctx);

	return;
exit:
	close_all(EV_A );	
	ev_unloop(EV_A_ EVUNLOOP_ALL); 	
}

/* Create connections context */
static ev_io*
memcached_client_new(int sock) {

	if(is_trace)
		printf("%s: new connection [%d]", __FUNCTION__, sock);

	if (setup_socket(sock) != -1) {
		memcache_ctx *mctx = calloc(1, sizeof(memcache_ctx));

		if (!mctx) {
			fprintf(flog, "%s: allocate error size=%d\n", __FUNCTION__, sizeof(memcache_ctx));
			return NULL;
		}
		
		mctx->value = malloc(BUFSIZE);		
		if (!mctx->value) {
			fprintf(flog, "%s: allocate error size=%d\n", __FUNCTION__, BUFSIZE);
			return NULL;
		}

		ev_io_init(&mctx->io, memcached_client, sock, EV_READ);
		clients[sock].io = &mctx->io;
		clients[sock].flags = FD_ACTIVE;
		clients[sock].cleanup = (cleanup_proc)memcached_client_free;
		clients[sock].mc_ctx = mctx;

		stats.connectionss++;
		stats.cnn_count++;
		if(is_trace)
			printf(" Ok\n");

//		printf("%s client[%d].ctx = %x  ctx->value=%x\n",__FUNCTION__, sock, mctx ,mctx->value );

		return &mctx->io;
	} else {
		if(is_trace)
			printf(" Fail\n");
		return NULL;
	}
}

/* Serve connectionss */
void
memcached_on_connect(EV_P_ ev_io *io, int revents) {
	
	while (1) {
		int client = accept(io->fd, NULL, NULL);
		
		if (client >= 0) {				
			ev_io *mctx = memcached_client_new(client);
			if (mctx) {
				ev_io_start(EV_A_ mctx);
			} else {
				fprintf(flog, "failed to create connections context %s", strerror(errno));
				close(client);
			}
		} else {
			if (errno == EAGAIN)
				return;
			if (errno == EMFILE || errno == ENFILE) {
				fprintf(flog, "out of file descriptors, dropping all clients. %s", strerror(errno));
				close_all(EV_A);
			} else if (errno != EINTR) {
				fprintf(flog, "accept_connections error: %s", strerror(errno));
			}
		}
		
	}
}

#endif
