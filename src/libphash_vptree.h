#ifndef _LIBPHASHVPTREE_H_
#define _LIBPHASHVPTREE_H_

#include <stdio.h>
#include <sys/types.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <getopt.h>
#include <pthread.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <mysql.h>
#include <pthread.h>
#include <debugp.h>
#include <math.h>

struct phash_selection_candidate {
	unsigned long long phash;
	double stddev;
};
struct phash_list {
	int count;
	unsigned long long *hashes;
};
struct vpnode {
	unsigned long long phash;
	int distance;
	struct phash_list left_vals;
	struct phash_list right_vals;
	struct vpnode *left;
	struct vpnode *right;
};
struct pmatch {
	unsigned long long phash;
	int dist;
};
struct pmatches {
	int count;
	unsigned long long best_match;
	struct pmatch *matches;
};
enum fork {
	both, 
	left,
	right 
};
typedef struct {
	int count;
	enum fork *steps;
} trace;
struct pthread_hds_pkg {
	struct phash_list *list;
	unsigned long long hash;
	double stddev;
};
struct threaded_query_pkg {
	int done;
	int query_num;
	unsigned long long query;
	struct pmatches *matches;
	double query_time;
};
void *threaded_query( void *pkg );
void *pthread_compare_hds( void *pkg );
extern int num_pthreads;
extern int flag_pthread_test;


/* Efficiency debugging */
extern int comparison_count;
extern double load_time;
extern trace debug_trace;

/* Counts */
extern int node_count;

/* Globals */
extern char *input_file;
extern char *output_file;
extern struct vpnode *master_tree;
extern struct phash_list master_list;

extern MYSQL *mysql_connection;

extern char *database_host;
extern char *database_user;
extern char *database_password;
extern char *database_name;
extern int database_port;

extern const char *default_host;
extern const char *default_user;
extern const char *default_password;
extern const char *default_database;
extern const int default_port;


/* General purpose timer */
struct timeval start_time,end_time;
double stop_timer(void);
void start_timer(void);

enum functions {
	command_line,
	optimization
};

/* User settings */
extern int max_leaf_distance;
extern int query_max_distance;
extern int num_phash_queries;
extern unsigned long long *phash_queries;
extern int flag_traverse_all;
extern int flag_print_vp_tree;
extern int flag_php;
extern int flag_efficiency_debug;
extern long int flag_slow;
extern int flag_load_multiple_times;
extern enum functions function;
/* vantage point selection tuning */
extern int max_vantage_point_tests;
extern int num_random_comparisons;
extern int optimize_iterations;
extern char *logfile_name;
extern unsigned long hd_count;


/**
 * Memory management 
 */
struct memory {
	void *ptr;
	size_t size;
	struct memory *left;
	struct memory *right;
};
extern struct memory *memlist;
void* mymalloc( size_t size );
void* myrealloc( void *ptr, size_t size );
struct memory *find_memlist( struct memory *list, void *ptr );
struct memory *add_to_memlist( struct memory *list, void *ptr, size_t size );
void print_memtree( struct memory *list );

/* Functions */
int *calc_hamming_distances( unsigned long long reference, unsigned long long *hashes, int count, int thread_count, unsigned long long *total );
struct pmatches *add_match( struct pmatches *matches, struct pmatch mymatch );
struct pmatches *query_phash2( 
		struct vpnode *node, 
		unsigned long long phash, 
		int distance,
		struct pmatches *matches
		);
struct pmatches *query_phash( 
		struct vpnode *node, 
		unsigned long long phash, 
		int distance,
		struct pmatches *matches
		);
struct vpnode *new_vpnode(void);
void free_vp_tree( struct vpnode *node );
void print_vp_tree( struct vpnode *node );
void deinit_database(void);
int init_database(void);
MYSQL_RES *run_query( char *query );
void load_phashes(void);
unsigned long long get_random_phash( struct phash_list *list );
unsigned long long get_best_vantage_point( 
		struct phash_list *list,
		unsigned long long last_hash
		);
unsigned long long get_furthest_vantage_point(
		struct phash_list *list,
		unsigned long long last_hash 
		);
void phash_push( unsigned long long phash, struct phash_list *list );
unsigned long long phash_pop( struct phash_list *list );
int hamming_distance(const unsigned long long hash1,const unsigned long long hash2)
;
unsigned long long get_best_seed( struct phash_list *input_list, int );
struct vpnode *index_phashes( 
		struct vpnode *node, 
		struct phash_list *list,
		struct vpnode *parent,
		unsigned long long seed,
		int level
		);
void quick_sort( int *array, unsigned long long length );
size_t get_node_size( struct vpnode *node );
int get_recursive_node_size( struct vpnode *node );
int save_index( struct vpnode *node, char *filename );
size_t serialize_node( struct vpnode *node, FILE *fp );
struct vpnode *load_index( char *filename );
struct vpnode *unserialize_node( FILE *fp );
void print_efficiency_summary(void);
void trace_push( trace *dtrace, enum fork step );
void efficiency_init(void);
void print_trace( trace dtrace );
int count_all_phashes( int start, struct vpnode *node );
int make_utf8( char *str, unsigned unum );
struct phash_list *copy_list( struct phash_list *list );
unsigned long long pop_random_phash( struct phash_list *list );

#endif
