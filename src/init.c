#ifndef __init_mymc__
#define __init_mymc__
#endif
#ifdef __init_mymc__

#include "main.h"
#include "init.h"
#include "mc.h"

extern FILE 			*flog;

likes_ctx* likes_init(void * conf) {
	
	likes_ctx * ctx = (likes_ctx*) malloc(sizeof(likes_ctx));	
	if (!ctx) {
		printf("error alloc size=%d\n", sizeof(likes_ctx));
		exit(1);
	}
	
	assert(ctx);	
	
	ctx->conf = conf;
	server_ctx_t * pconf = (server_ctx_t *)conf;

	ctx->hdb = tchdbnew();
	assert(ctx->hdb);
	
	pconf->datadir = strdup("data");
	
	int32_t rcnum = LIKE_CACHESIZE; // 16M
	tchdbsetcache(ctx->hdb, rcnum);
	{

		// number of elements of the bucket array. If it is not more than 0, the default value is specified. The default value is 131071.
		int64_t bnum =  pconf->index_bucket;  //4194272; //131071;// 134216704;
		// specifies the size of record,  The default value is 4 standing for 2^4=16.
		int8_t apow = 1024; // 2^10=1024 ?? (int8_t)pconf->recsize
		// сжатие
		uint8_t opts = HDBTLARGE;
		// specifies the maximum number of elements of the free block pool by power of 2.
		int8_t fpow = 10; // 2^10=1024
		//filesize 536M  for bnum = 134 216704
		
		if (!tchdbtune(ctx->hdb, bnum, apow, fpow, opts)) {
			ctx->ecode = tchdbecode(ctx->hdb);
			fprintf(flog, "tune %s error: %s\n", LK_STORAGE_FILENAME,tchdberrmsg(ctx->ecode));
			free(ctx);
			return NULL;
		}

		int len =  LK_STORAGE_FILENAME_LEN + strlen(pconf->datadir);	
		char* filename = malloc(len);
		strcpy(filename, pconf->datadir);
		strcpy(filename+strlen(pconf->datadir), LK_STORAGE_FILENAME);
				
		if(!tchdbopen(ctx->hdb, filename, HDBOWRITER| HDBOREADER | HDBOCREAT| HDBONOLCK )) {
			ctx->ecode = tchdbecode(ctx->hdb);
			fprintf(flog, "open datafile %s error: %s\n", filename, tchdberrmsg(ctx->ecode));
			free(ctx);
			return NULL;
		}
	}	

	ctx->hdb_counter = tchdbnew();
	assert(ctx->hdb_counter);

	{
		tchdbsetcache(ctx->hdb_counter, rcnum);
		
		// number of elements of the bucket array. If it is not more than 0, the default value is specified. The default value is 131071.
		int64_t bnum = pconf->counter_bucket; // 67108352;
		// specifies the size of record,  The default value is 4 standing for 2^4=16.
		int8_t apow = 2; // 2^10=1024 ??
		// сжатие
		uint8_t opts = HDBTLARGE;
		// specifies the maximum number of elements of the free block pool by power of 2.
		int8_t fpow = 10; // 2^10=1024
		//filesize 536M  for bnum = 134 216704
		
		if (!tchdbtune(ctx->hdb_counter, bnum, apow, fpow, opts)) {
			ctx->ecode = tchdbecode(ctx->hdb_counter);
			fprintf(flog, "tune %s error: %s\n", LK_COUNTER_FILENAME,tchdberrmsg(ctx->ecode));
			free(ctx);
			return NULL;
		}

		int len =  LK_COUNTER_FILENAME_LEN + strlen(pconf->datadir);	
		char* filename = malloc(len);
		strcpy(filename, pconf->datadir);
		strcpy(filename+strlen(pconf->datadir), LK_COUNTER_FILENAME);
				
		if(!tchdbopen(ctx->hdb_counter, filename, HDBOWRITER| HDBOREADER | HDBOCREAT| HDBONOLCK )) {
			ctx->ecode = tchdbecode(ctx->hdb);
			fprintf(flog, "open datafile %s error: %s\n", filename, tchdberrmsg(ctx->ecode));
			free(ctx);
			return NULL;
		}
	}

	ctx->types = malloc((pconf->max_num+1) * sizeof(int));
	ctx->types[0] = pconf->max_num;
	
	//printf("types alloc %X  count=%d\n",(unsigned)ctx->types, pconf->max_num);
	datatype_t * p = (datatype_t *)pconf->list_datatypes;	
	
	while(p) {
		ctx->types[p->number] = p->type;	
		//printf("init %d:%d [%s] \n", p->number,p->type, p->comment);
		p = p->next;
	}
		
	return ctx;
}


void likes_destroy(likes_ctx* ctx) {
  assert(ctx);
  assert(ctx->hdb);
  assert(ctx->types);
  ctx->ecode=0;

  if(!tchdbclose(ctx->hdb)){
		ctx->ecode = tchdbecode(ctx->hdb);
		fprintf(flog, "%s close %s error: %s\n", __FUNCTION__, LK_STORAGE_FILENAME,tchdberrmsg(ctx->ecode));
	}

	tchdbdel(ctx->hdb);

  if(!tchdbclose(ctx->hdb_counter)){
		ctx->ecode = tchdbecode(ctx->hdb_counter);
		fprintf(flog, "%s close %s error: %s\n", __FUNCTION__, LK_COUNTER_FILENAME, tchdberrmsg(ctx->ecode));
	}

	tchdbdel(ctx->hdb_counter);


    free(ctx->types);		
	free(ctx);
 }



int likes_set(likes_ctx* ctx, int prefix, unsigned crc, void* data, int data_len ) {
	assert(data);
	assert(data_len>0);
	
	char * key = malloc(LK_KEYSIZE);
	create_key(prefix,crc, key);	
	//print_key(key);
		
	TCHDB *hdb = ctx->type == LK_TYPE_PREFIX_COUNTER ? ctx->hdb_counter : ctx->hdb;
		
	if( !tchdbput(hdb, (const void *)key, LK_KEYSIZE, (const void *)data, data_len )) {
		ctx->ecode = tchdbecode(hdb);
		fprintf(flog, "likes_add put error: %s\n", tchdberrmsg(ctx->ecode));
		free(key);
		return 1;
	}
	free(key);
	return 0;
}

int likes_friends_inc(likes_ctx* ctx, unsigned crc) {
	int data_len;
	char * key = malloc(LK_KEYSIZE);
	create_key(LK_PREFIX_FRIENDLIST,crc, (char*) key);


	void * p = tchdbget(ctx->hdb, key, LK_KEYSIZE, &data_len);
	
	if(!p) {
		ctx->ecode = tchdbecode(ctx->hdb);
		fprintf(flog, "likes_friends_inc error: %s\n", tchdberrmsg(ctx->ecode));
		free(key);
		return 1;
	}
	
	unsigned *p_data = (unsigned*)p;
	int size = data_len / sizeof(unsigned);
	int sp=0;
	lk_key * skey = (lk_key *) key;
	skey->type = LK_PREFIX_COUNTER;
	while (size--) {		
	
		skey->crc = *p_data;

		int * res = (int*)tchdbget(ctx->hdb, (const void *)skey, LK_KEYSIZE,&sp);
		//fprintf( flog, "likes_inc key=%u\n", *p_data );
		if (res) {
			int res2 = tchdbaddint(ctx->hdb, (const void *)skey, LK_KEYSIZE, 1);
			if(res2 == INT_MIN) {
				ctx->ecode = tchdbecode(ctx->hdb);
				fprintf(flog, "likes_inc key=%u inc error: %s\n", skey->crc, tchdberrmsg(ctx->ecode));
			}
			free(res);
		} else {							
			unsigned inc = 1;
			bool res2 = tchdbput(ctx->hdb, (const void *)skey, LK_KEYSIZE, &inc, sizeof(unsigned));
			if(!res2) {
				ctx->ecode = tchdbecode(ctx->hdb);
				fprintf(flog, "likes_inc key=%u put error: %s\n", skey->crc, tchdberrmsg(ctx->ecode));
			}
		}

		fprintf(flog, "friends_inc crc=%x res=%d\n", p_data, *res);
		p_data++;
	}
	free(p);
	free(key);
	return 0;
}

/*
 Переменные left и right содержат, соответственно, левую и правую границы отрезка массива
 p - указатель на массив данных, m - результат поиска
*/
int bin_search (unsigned* p, int left, int right, unsigned key, int* m) {
	
    *m = (left + right)/2 ;
	unsigned crc = (*p + *m);
    if (key < crc) {
      right = *m - 1;	  
    } else if (key > crc) {
      left = *m + 1;
    } else {
      return -1;
    }
	
	if (left > right) {
		return -1;
	}	
	return bin_search (p, left, right, key, m);			
}

int likes_friends_add_like(likes_ctx* ctx, unsigned crc, unsigned data) {
	
	int data_len;
	char * key = malloc(LK_KEYSIZE);
	create_key(LK_PREFIX_FRIENDLIST,crc, (char*) key);

	void * p = tchdbget(ctx->hdb, key, LK_KEYSIZE, &data_len);
//	fprintf(flog,"TRACE %s crc=%u data:[crc=%u  data=%u] get_data_size=%d get=%x\n", __FUNCTION__, crc,data->crc,data->data, data_len,(unsigned)p);
	if(!p) {
		 fprintf(flog,"insert crc=%x\n", crc);
		if( !tchdbput(ctx->hdb, (const void *)key, LK_KEYSIZE, (const void *)&data,  LK_LIKESIZE )) {
			ctx->ecode = tchdbecode(ctx->hdb);
			fprintf(flog, "likes_add put error: %s\n", tchdberrmsg(ctx->ecode));
			free(key);
			return 1;
		}
		free(key);
		return 0;
	}

	unsigned* pdata = (unsigned*)p;
	const int count = data_len/LK_LIKESIZE;	
	

	if (count==1) {
		if ( data > *pdata ) {
			 fprintf(flog,"data->crc > pdata->crc : tchdbputcat data \n");
			if(!tchdbputcat(ctx->hdb, key, LK_KEYSIZE, &data, LK_LIKESIZE)) {
				ctx->ecode = tchdbecode(ctx->hdb);
				fprintf(flog, "likes_friends_inc error: %s\n", tchdberrmsg(ctx->ecode));
				free(p);
				free(key);
				return 1;
			}
			goto fin;
		}
		
		if ( data == *pdata ) {
			if(!tchdbput(ctx->hdb, key, LK_KEYSIZE, &data, LK_LIKESIZE)) {
				ctx->ecode = tchdbecode(ctx->hdb);
				fprintf(flog, "likes_friends_inc error: %s\n", tchdberrmsg(ctx->ecode));
				free(p);
				free(key);
				return 1;
			}
			goto fin;
		}

		// data < pdata		
		{
			unsigned* tmp_data = (unsigned*)malloc(LK_LIKESIZE*2); // why 2??
			unsigned* ptmp = tmp_data;
			*ptmp= data;
			ptmp++;
			*ptmp= *pdata;
		
			if( !tchdbput(ctx->hdb, (const void *)key, LK_KEYSIZE, (const void *)tmp_data, LK_LIKESIZE * 2)) {
				ctx->ecode = tchdbecode(ctx->hdb);
				fprintf(flog, "likes_add put error: %s\n", tchdberrmsg(ctx->ecode));
				free(tmp_data);
				free(p);
				free(key);
				return 1;
			}
			free(tmp_data);	
		}
	goto fin;		
	} // count=1
	
	// count > 1
	int m=0;
	bin_search (pdata, 0, count-1, data, &m);
//	 fprintf(flog,"\nbin search[0,%d] crc=%x m=%d\n",count-1, data->crc, m);
	
	if (m == count-1 && data > *(pdata+m)) {
		 fprintf(flog,"m=count & data->crc > pdata[m]->crc : tchdbputcat data \n");
		if(!tchdbputcat(ctx->hdb, key, LK_KEYSIZE, &data, LK_LIKESIZE)) {
			ctx->ecode = tchdbecode(ctx->hdb);
			fprintf(flog, "likes_friends_inc error: %s\n", tchdberrmsg(ctx->ecode));
			free(p);
			free(key);
			return 1;
		}
		goto fin;
	}
	
	if (data == *(pdata+m)) {
		 //fprintf(flog,"data->crc == pdata[%d]->crc(%x): tchdbput new data \n",m,(pdata+m)->crc);
		*(pdata+m) = data;
		if( !tchdbput(ctx->hdb, (const void *)key, LK_KEYSIZE, p, data_len)) {
			ctx->ecode = tchdbecode(ctx->hdb);
			fprintf(flog, "likes_add put error: %s\n", tchdberrmsg(ctx->ecode));
			free(p);
			free(key);
			return 1;
		}
		goto fin;
	}

	if (!m) {
		unsigned* tmp_data = (unsigned*)malloc(LK_LIKESIZE+data_len);
		unsigned* tmp = tmp_data;
		
		if (data < *pdata) {
			*tmp++ = data;
			memcpy(tmp,pdata,data_len);
			if( !tchdbput(ctx->hdb, (const void *)key, LK_KEYSIZE, tmp_data, data_len + LK_LIKESIZE)) {
				ctx->ecode = tchdbecode(ctx->hdb);
				fprintf(flog, "likes_add put error: %s\n", tchdberrmsg(ctx->ecode));
				free(tmp_data);
				free(p);
				free(key);
				return 1;
			}
		
			free(tmp_data);
			goto fin;
		}
		
		free(tmp_data);
	}
	
	/* расчет в какую об памяти копировать */
	int l,rs,d, rd;
	if (data > *(pdata+m)) { // key > p[m]
		l=m+1;
		d=m+1;
		rs=m+1;
		rd=m+2;
	}
	
	if (data < *(pdata+m)) { // key < p[m]
		l=m;
		d=m;
		rs=m;
		rd=m+1;
	}	
	{
		unsigned* tmp_data = (unsigned*)malloc(LK_LIKESIZE+data_len);
		//copy [0,l]
		memcpy(tmp_data, pdata, LK_LIKESIZE*l );
//		 fprintf(flog,"copyed=%d\n",LK_LIKESIZE*l );
		//copy data-> [d]
		*(tmp_data+d) = data;
//		 fprintf(flog,"copyed=%d\n",LK_LIKESIZE);
		//copy [r,count]
		memcpy(tmp_data+rd, pdata+rs, LK_LIKESIZE*(count-rs));
//		 fprintf(flog,"copyed=%d\n",LK_LIKESIZE*(count-rs));
		
		if( !tchdbput(ctx->hdb, (const void *)key, LK_KEYSIZE, tmp_data, data_len + LK_LIKESIZE)) {
			ctx->ecode = tchdbecode(ctx->hdb);
//			fprintf(flog, "likes_add put error: %s\n", tchdberrmsg(ctx->ecode));
			free(tmp_data);
			free(p);
			free(key);
			return 1;
		}
		
		free(tmp_data);
	}
	
fin: 
	free(p);
	free(key);
	return 0;
}

int likes_friends_remove_like(likes_ctx* ctx, unsigned crc, unsigned data) {
	ctx->ecode = 0;
	int data_len;
	char * key = malloc(LK_KEYSIZE);
	create_key(LK_PREFIX_FRIENDLIST,crc, (char*) key);

	void * p = tchdbget(ctx->hdb, key, LK_KEYSIZE, &data_len);
//	 fprintf(flog,"TRACE %s crc=%x  data=%x  data_size=%d get=%x\n", __FUNCTION__, crc,data->crc, data_len, (unsigned)p);
	if(!p) {
		ctx->ecode = tchdbecode(ctx->hdb);
		fprintf(flog, "likes_add put error: %s\n", tchdberrmsg(ctx->ecode));
		free(key);
		return 1;
	}

	unsigned *  pdata = (unsigned*) p;
	const int count = data_len/LK_LIKESIZE;
	if (!count) 
		goto finOk;
		
	if (count==1) {
		if ( *pdata == data) {
			if( !tchdbput(ctx->hdb, (const void *)key, LK_KEYSIZE, NULL, 0)) {
				ctx->ecode = tchdbecode(ctx->hdb);
				fprintf(flog, "likes_friends_remove_like put error: %s\n", tchdberrmsg(ctx->ecode));
				free(key);
				return 1;
			}
			goto finOk;
		}
		goto finErr;
	}

	int m=0;
	bin_search (pdata, 0, count-1, data, &m);
	 fprintf(flog,"\nbin search[0,%d] crc=%x m=%d\n",count-1, data, m);
	if (data == *(pdata+m)) {
		const int size = (count-1)*LK_LIKESIZE;
		unsigned * tmp = (unsigned*) malloc(size);
		
		if (!m) {
			memcpy(tmp, pdata+1, size);
			if( !tchdbput(ctx->hdb, (const void *)key, LK_KEYSIZE, tmp, size)) {
				ctx->ecode = tchdbecode(ctx->hdb);
				fprintf(flog, "likes_friends_remove_like put error: %s\n", tchdberrmsg(ctx->ecode));
				free(tmp);
				goto finErr;
			}
			free(tmp);
			goto finOk;
		}
		// copy memory blocks
		memcpy(tmp,pdata, LK_LIKESIZE * m);
		memcpy(tmp+m,pdata+m+1,LK_LIKESIZE * (count-m-1));
		if( !tchdbput(ctx->hdb, (const void *)key, LK_KEYSIZE, tmp, size)) {
			ctx->ecode = tchdbecode(ctx->hdb);
			fprintf(flog, "likes_friends_remove_like put error: %s\n", tchdberrmsg(ctx->ecode));
			free(tmp);
			goto finErr;
		}		
		free(tmp);
		goto finOk;
	} 
	goto finErr;

finOk:	
	free(p);
	free(key);
	return 0;
	
finErr:	
	free(p);
	free(key);
	return 1;
}

void print_key(char * key) {
	lk_key * skey = (lk_key *) key;
	printf("%s:(%04x %04x)\n", __FUNCTION__, skey->type, skey->crc );		
}


void create_key(int type,unsigned crc,  char * out_key) {	
	
	lk_key * key = (lk_key *) out_key;
	key->type = type;
	key->crc = crc;		
}
#endif
