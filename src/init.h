
#define LK_STORAGE_FILENAME  "storage.tch"
#define LK_STORAGE_FILENAME_LEN  sizeof(LK_STORAGE_FILENAME)

#define LK_COUNTER_FILENAME  "counter.tch"
#define LK_COUNTER_FILENAME_LEN  sizeof(LK_COUNTER_FILENAME)

#define LK_KEYSIZE sizeof(lk_key)
#define LK_LIKESIZE sizeof(unsigned)



// prefix
#define LK_PREFIX_PRIVATE 		0
#define LK_PREFIX_FRIENDLIST 	1
#define LK_PREFIX_COUNTER	 	2

#define LK_TYPE_PREFIX_COUNTER	1
#define LK_TYPE_PREFIX_INDEX	2
#define LK_TYPE_PREFIX_DATA		3

typedef struct {
	TCHDB *			hdb;
	TCHDB *			hdb_counter;
	int 			ecode;
	unsigned * 		key;	
	int * 			types;
	void *	conf;
	int 			type;
} likes_ctx;

typedef struct {
	int type;
	unsigned crc;	
} lk_key;

// typedef struct {
// 	unsigned data;
// 	unsigned crc;	
// } lk_data;


/*
* инициализация Хранилища данных
*/
extern likes_ctx* 
likes_init(void*);

/*
* деинициализация Хранилища данных
* убираем мусор за собой
*/
extern void 
likes_destroy(likes_ctx* ctx);

/* 
* добавлекние данных data в стораж, 
* по ключу prefix + crc 
* возвращает результат 0 удачно или код ошибки
*/
int likes_set(likes_ctx* ctx, int prefix, unsigned crc, void* data, int data_len );


/* 
* увеличение всех данных  (prefix LK_PREFIX_COUNTER) списка, 
* который хранятся по ключу crc (prefix=LK_PREFIX_FRIENDLIST)  на величину data
* возвращает результат 0 удачно или код ошибки
*/
int likes_friends_inc(likes_ctx* ctx, unsigned crc);

/* 
* add_like добавляет like data в список лайков (симпатий)  для данного crc (prefix LK_PREFIX_FRIENDLIST)
* remove_like убирает like из списка
* возвращает результат 0 удачно или код ошибки
*/
int likes_friends_add_like(likes_ctx* ctx, unsigned crc, unsigned data);
int likes_friends_remove_like(likes_ctx* ctx, unsigned crc, unsigned data);


/* 
* создает ключ out_key из составляющих: prefix + crc
* out_key выделенная область памяти под ключ 
*/
void create_key(int type,unsigned crc,  char * out_key);

void print_key(char * key);

/*
бинарный поиск
*/
int bin_search (unsigned* p, int left, int right, unsigned key, int* m); 
