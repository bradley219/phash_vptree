#ifndef _LIBPHASHVPTREE_SOURCE_
#define _LIBPHASHVPTREE_SOURCE_
#include "libphash_vptree.h"

int num_pthreads = 2;
int flag_pthread_test = 0;

/* User settings */
int max_leaf_distance = 500; // nodesize
int query_max_distance = 0;

int num_phash_queries = 0;
unsigned long long *phash_queries = NULL;
int flag_traverse_all = 0;
int flag_print_vp_tree = 0;
int flag_php = 0;
int flag_efficiency_debug = 0;
long int flag_slow = 0;
int flag_load_multiple_times = 0;
enum functions function = command_line;

/* vantage point selection tuning */
int max_vantage_point_tests = 100; 
int num_random_comparisons = 100;
int optimize_iterations = 100;
char *logfile_name = NULL;

/* Globals */
char *input_file = NULL;
char *output_file = NULL;
struct vpnode *master_tree = NULL;
struct phash_list master_list = {0,NULL};
struct memory *memlist = NULL;

/* Efficiency debugging */
int comparison_count = 0;
double load_time = 0;
trace debug_trace;

unsigned long hd_count = 0;

const char *default_host = "localhost";
const char *default_user = "phash_vptree";
const char *default_password = "password";
const char *default_database = "phash_vptree";
const int default_port = 3306;

char *database_host = NULL;
char *database_user = NULL;
char *database_password = NULL;
char *database_name = NULL;
int database_port = 0;

MYSQL *mysql_connection;

/* Counts */
int node_count = 0;


void *threaded_query( void *pkg )
{
	struct timeval start_t,end_t;
	gettimeofday(&start_t,NULL);

	struct threaded_query_pkg *package = (struct threaded_query_pkg*)pkg;

	package->matches = NULL;
	package->matches = query_phash( master_tree, package->query, query_max_distance, package->matches );
	
	gettimeofday(&end_t,NULL);
	double dstart_time = (double)start_t.tv_sec + (double)start_t.tv_usec / 1000000.0;
	double dend_time = (double)end_t.tv_sec + (double)end_t.tv_usec / 1000000.0;
	package->query_time = dend_time - dstart_time;

	debugp( 2, "query #%d done!\n", package->query_num );
	package->done = 1;
	
	pthread_exit(NULL);
}
struct pmatches *add_match( struct pmatches *matches, struct pmatch mymatch )
{
	if( matches == NULL )
	{
		matches = (struct pmatches*)malloc( sizeof(struct pmatches) );
		matches->count = 0;
		matches->matches = NULL;
	}
	(matches->count++);
	matches->matches = (struct pmatch*)realloc(matches->matches,sizeof(struct pmatch) * matches->count);
	matches->matches[matches->count-1].phash = mymatch.phash;
	matches->matches[matches->count-1].dist  = mymatch.dist;

	return matches;
}
struct pmatches *query_phash2( 
		struct vpnode *node, 
		unsigned long long phash, 
		int distance,
		struct pmatches *matches
		)
{
	if( node == NULL )
		return NULL;



	if( matches == NULL )
	{
		matches = (struct pmatches*)malloc( sizeof(struct pmatches) );
		matches->count = 0;
		matches->best_match = 0;
		matches->matches = NULL;
	}

	/* Calculate distance from query hash to best match found so far */
	int dist_q_bm = hamming_distance( phash, matches->best_match );

	/* Calculate distance from query hash to vantage point hash */
	int dist_q_vp = hamming_distance( node->phash, phash );

	char mu[4];
	char sigma[4];
	make_utf8(mu,956);
	make_utf8(sigma,963);

	debugp( 4, "\n" );
	debugp( 4, "Query:         0x%016llX\n", phash );
	debugp( 4, "Best match:    0x%016llX\n", matches->best_match );
	debugp( 4, "Vantage point: 0x%016llX\n", node->phash );
	debugp( 4, "   Left vals:  %18d\n", node->left_vals.count );
	debugp( 4, "   Right vals: %18d\n", node->right_vals.count );
	debugp( 4, "Max Distance:  %18d\n", distance );
	debugp( 4, "D(Q-BM) (%s):   %18d\n", sigma, dist_q_bm ); // sigma
	debugp( 4, "D(Q-VP):       %18d\n", dist_q_vp );
	debugp( 4, "D(VP-N) (%s):   %18d\n", mu, node->distance ); // mu
	debugp( 4, "(%s + %s):       %18d\n", mu, sigma, node->distance + dist_q_bm );
	debugp( 4, "(%s - %s):       %18d\n", mu, sigma, node->distance - dist_q_bm );


	debugp( 4, "\n" );
	debugp( 4, "Match count:   %18d\n", matches->count );

	/* See http://cs-people.bu.edu/athitsos/publications/vlachos_cikm2005.pdf 
	 * Section 5 for explanation */
	int traverse_left  = 0;
	int traverse_right = 0;
	
	if( (dist_q_vp < (node->distance + dist_q_bm)) && ((node->distance - dist_q_bm) < dist_q_vp) )
	{
		// traverse both
		debugp( 5, "Traversing both...\n" );
		traverse_left  = 1;
		traverse_right = 1;
		if( flag_efficiency_debug )
			trace_push( &debug_trace, both );
	}
	else if( dist_q_vp > ( node->distance + dist_q_bm ) ) 
	{
		// traverse right 
		debugp( 5, "Traversing right only...\n" );
		traverse_right = 1;
		if( flag_efficiency_debug )
			trace_push( &debug_trace, right );
	}
	else if( dist_q_vp <= ( node->distance - dist_q_bm ) ) 
	{
		// traverse left 
		debugp( 5, "Traversing left only...\n" );
		traverse_left  = 1;
		if( flag_efficiency_debug )
			trace_push( &debug_trace, left );
	}
	if( flag_slow )
	{
		struct timespec st;
		st.tv_sec=0;
		st.tv_nsec=flag_slow;
		nanosleep(&st,NULL);
	}

	/* Traverse any values in this node if needed */
	if( traverse_left ) // LEFT
	{
		if( node->left_vals.count )
		{
			debugp( 5, "  Left:\n" );
			for( int i=0; i < node->left_vals.count; i++ )
			{
				comparison_count++;
				int hd = hamming_distance( node->left_vals.hashes[i], phash );


				debugp( 5, "    0x%llX (d=%d) ", node->left_vals.hashes[i], hd );

				if( hd <= distance )
				{
					debugp( 5, "MATCH! " );
					debugp( 3, "Match from left node's list\n" );
					struct pmatch mymatch;
					mymatch.dist = hd;
					mymatch.phash = node->left_vals.hashes[i];

					matches = add_match( matches, mymatch );
				}
				if( (hd < dist_q_bm) && (hd >= distance) )
				{
					debugp( 5, "NEW BEST MATCH! " );
					matches->best_match = node->left_vals.hashes[i];
				}
				debugp( 5, "\n" );
			}
		}

		// Step into the left node
		if( node->left != NULL )
		{
			matches = query_phash2(node->left,phash,distance,matches);
		}
	}
	if( traverse_right ) // RIGHT
	{
		if( node->right_vals.count ) 
		{
			debugp( 5, "  Right:\n" );
			for( int i=0; i < node->right_vals.count; i++ )
			{
				comparison_count++;
				int hd = hamming_distance( node->right_vals.hashes[i], phash );

				debugp( 5, "    0x%llX (d=%d) ", node->right_vals.hashes[i], hd );

				if( hd <= distance )
				{
					debugp( 5, "MATCH! " );
					debugp( 3, "Match from right node's list\n" );
					struct pmatch mymatch;
					mymatch.dist = hd;
					mymatch.phash = node->right_vals.hashes[i];

					matches = add_match( matches, mymatch );
				}
				if( (hd < dist_q_bm) && (hd >= distance) )
				{
					debugp( 5, "NEW BEST MATCH! " );
					matches->best_match = node->right_vals.hashes[i];
				}
				debugp( 5, "\n" );
			}
		}

		// Step into the right node
		if( node->right != NULL )
		{
			matches = query_phash2(node->right,phash,distance,matches);
		}
	}
	
	return matches;
}
struct pmatches *query_phash( 
		struct vpnode *node, 
		unsigned long long phash, 
		int distance,
		struct pmatches *matches
		)
{
	if( node == NULL )
		return NULL;



	if( matches == NULL )
	{
		matches = (struct pmatches*)malloc( sizeof(struct pmatches) );
		matches->count = 0;
		matches->best_match = 0;
		matches->matches = NULL;
	}

	/* Calculate distance from query hash to best match found so far */
	int dist_q_bm = hamming_distance( phash, matches->best_match );

	/* Calculate distance from query hash to vantage point hash */
	int dist_q_vp = hamming_distance( node->phash, phash );

	char mu[4];
	char sigma[4];
	make_utf8(mu,956);
	make_utf8(sigma,963);

	debugp( 4, "\n" );
	debugp( 4, "Query:         0x%016llX\n", phash );
	debugp( 4, "Best match:    0x%016llX\n", matches->best_match );
	debugp( 4, "Vantage point: 0x%016llX\n", node->phash );
	debugp( 4, "   Left vals:  %18d\n", node->left_vals.count );
	debugp( 4, "   Right vals: %18d\n", node->right_vals.count );
	debugp( 4, "Max Distance:  %18d\n", distance );
	debugp( 4, "D(Q-BM) (%s):   %18d\n", sigma, dist_q_bm ); // sigma
	debugp( 4, "D(Q-VP):       %18d\n", dist_q_vp );
	debugp( 4, "D(VP-N) (%s):   %18d\n", mu, node->distance ); // mu
	debugp( 4, "(%s + %s):       %18d\n", mu, sigma, node->distance + dist_q_bm );
	debugp( 4, "(%s - %s):       %18d\n", mu, sigma, node->distance - dist_q_bm );


	debugp( 4, "\n" );
	debugp( 4, "Match count:   %18d\n", matches->count );

	/* See http://cs-people.bu.edu/athitsos/publications/vlachos_cikm2005.pdf 
	 * Section 5 for explanation */
	int traverse_left  = 0;
	int traverse_right = 0;
	
	if( (dist_q_vp < (node->distance + dist_q_bm)) && ((node->distance - dist_q_bm) < dist_q_vp) )
	{
		// traverse both
		debugp( 5, "Traversing both...\n" );
		traverse_left  = 1;
		traverse_right = 1;
		if( flag_efficiency_debug )
			trace_push( &debug_trace, both );
	}
	else if( dist_q_vp > ( node->distance + dist_q_bm ) ) 
	{
		// traverse right 
		debugp( 5, "Traversing right only...\n" );
		traverse_right = 1;
		if( flag_efficiency_debug )
			trace_push( &debug_trace, right );
	}
	else if( dist_q_vp <= ( node->distance - dist_q_bm ) ) 
	{
		// traverse left 
		debugp( 5, "Traversing left only...\n" );
		traverse_left  = 1;
		if( flag_efficiency_debug )
			trace_push( &debug_trace, left );
	}
	if( flag_slow )
	{
		struct timespec st;
		st.tv_sec=0;
		st.tv_nsec=flag_slow;
		nanosleep(&st,NULL);
	}

	/* Traverse any values in this node if needed */
	if( traverse_left ) // LEFT
	{
		if( node->left_vals.count )
		{
			debugp( 5, "  Left:\n" );
			for( int i=0; i < node->left_vals.count; i++ )
			{
				comparison_count++;
				int hd = hamming_distance( node->left_vals.hashes[i], phash );


				debugp( 5, "    0x%llX (d=%d) ", node->left_vals.hashes[i], hd );

				if( hd <= distance )
				{
					debugp( 5, "MATCH! " );
					debugp( 3, "Match from left node's list\n" );
					struct pmatch mymatch;
					mymatch.dist = hd;
					mymatch.phash = node->left_vals.hashes[i];

					matches = add_match( matches, mymatch );
				}
				if( (hd < dist_q_bm) && (hd >= distance) )
				{
					debugp( 5, "NEW BEST MATCH! " );
					matches->best_match = node->left_vals.hashes[i];
				}
				debugp( 5, "\n" );
			}
		}

		// Step into the left node
		if( node->left != NULL )
		{
			matches = query_phash(node->left,phash,distance,matches);
		}
	}
	if( traverse_right ) // RIGHT
	{
		if( node->right_vals.count ) 
		{
			debugp( 5, "  Right:\n" );
			for( int i=0; i < node->right_vals.count; i++ )
			{
				comparison_count++;
				int hd = hamming_distance( node->right_vals.hashes[i], phash );

				debugp( 5, "    0x%llX (d=%d) ", node->right_vals.hashes[i], hd );

				if( hd <= distance )
				{
					debugp( 5, "MATCH! " );
					debugp( 3, "Match from right node's list\n" );
					struct pmatch mymatch;
					mymatch.dist = hd;
					mymatch.phash = node->right_vals.hashes[i];

					matches = add_match( matches, mymatch );
				}
				if( (hd < dist_q_bm) && (hd >= distance) )
				{
					debugp( 5, "NEW BEST MATCH! " );
					matches->best_match = node->right_vals.hashes[i];
				}
				debugp( 5, "\n" );
			}
		}

		// Step into the right node
		if( node->right != NULL )
		{
			matches = query_phash(node->right,phash,distance,matches);
		}
	}
	
	return matches;
}
void quick_sort( int *array, unsigned long long length )
{
	if( length <= 1 )
		return;

	int *less = malloc( sizeof(int) * length );
	int *greater = malloc( sizeof(int) * length );
	int *gt=greater, *lt=less;
	int i;

	/* Get middle value */
	int max=0,min=0x7fffffff;
	for( i=0; i<length; i++ )
	{
		if( array[i] > max )
			max = array[i];
		else if( array[i] < min )
			min = array[i];
	}

	int split = (max+min)/2;

	for( i=0; i<length; i++ )
	{
		/*debugp( 6, "split=%d array[%d]=%d %s\n",
				split,
				i,
				array[i],
				(array[i] < split) ? "LESS" : "     GREATER");*/
		if( array[i] < split )
			*lt++ = array[i];
		else
			*gt++ = array[i];
	}

	int greater_count = gt-greater;
	int less_count = lt-less;

	//debugp( 6, "max=%d min=%d split=%d\n", max, min, split );
	/*debugp( 6, "total_count = %d greater_count = %d; less_count = %d\n", 
			length, 
			greater_count, 
			less_count );*/

	if( (greater_count>0) && (less_count>0) )
	{
		if(greater_count > 1)
		{
			quick_sort(greater,greater_count);
		}
		if(less_count > 1)
		{
			quick_sort(less,less_count);
		}
	}
	/* Concatenate into original array */
	int *ap = array;
	gt=greater;
	lt=less;
	for( i=0; i<less_count; i++ )
	{
		*ap++ = *lt++;
	}
	for( i=0; i<greater_count; i++ )
	{
		*ap++ = *gt++;
	}
	free(less);
	free(greater);

	return;
}
unsigned long long get_furthest_vantage_point(
		struct phash_list *list,
		unsigned long long last_hash 
		)
{
	int furthest_distance = 0;
	unsigned long long furthest_hash = 0;

	/* Calc hamming distances of all points to last phash */
	for( int i=0; i < list->count; i++ )
	{
		int hd = hamming_distance( last_hash, list->hashes[i] );
		if( hd > furthest_distance )
		{
			furthest_distance = hd;
			furthest_hash = list->hashes[i];
		}
	}

	return furthest_hash;
}
unsigned long long get_best_vantage_point( 
		struct phash_list *input_list,
		unsigned long long last_hash
		)
{
	unsigned long long best_phash = 0;

	int remaining = (input_list->count < max_vantage_point_tests) ? input_list->count : max_vantage_point_tests;

	double highest_stddev = 0;
	int next_print = remaining;
	int totnum = remaining;


//	/* Calc hamming distances of all points to last phash */
//	int *hds1 = calc_hamming_distances( 
//			last_hash, 
//			input_list->hashes, 
//			input_list->count,
//			4 );
//	
//	/* Sort the list */
//	quick_sort( hds1, input_list->count );

	/* Get the furthest `remaining' hashes from the parent */
	unsigned long long *phash = &(input_list->hashes[input_list->count - remaining]);

	while( remaining-- )
	{
		if( next_print >= remaining )
		{
			debugp( 3, "\r%c%c%c", 0x1b, '[', 'K' ); // clear to end of line
			debugp( 3, "%d/%d (%2.1f%%) best=%7.6f", 
					totnum-remaining, 
					totnum, 
					(double)(totnum-remaining) / (double)totnum * 100.0,
					highest_stddev
					);
			fflush(stderr);
			next_print = remaining - 100;
		}
		///* Make a copy of the list */
		//struct phash_list *list = copy_list(input_list);
		
		/* Compare against random sample of members of list */
		unsigned long long total = 0;
		int sample_size = (input_list->count<num_random_comparisons) ? input_list->count : num_random_comparisons;

		int *hds = calc_hamming_distances(
				*phash,
				input_list->hashes,
				sample_size,
				2,
				&total );
		int *hdsp = hds;
		/* Calculate mean */
		double mean = (double)total/(double)sample_size;

		/* Calculate stddev */
		unsigned long long std_dev_tot = 0;
		hdsp = hds;
		for( int i=0; i < sample_size; i++ )
		{
			std_dev_tot += (unsigned)pow( (*hdsp++) - mean, 2 );
		}
		double stddev = sqrt( (double)std_dev_tot / (double)sample_size );

		free(hds);

		if( stddev > highest_stddev )
		{
			highest_stddev = stddev;
			best_phash = *phash;
		}

		debugp( 4, "[%03d] phash=0x%016llX mean=%5.3f stddev=%5.3f best=%5.3f 0x%llX\n", 
				remaining, *phash, mean, stddev, highest_stddev, best_phash );

		phash++;
		//free(list);
	}
	debugp( 3, " higest_stddev=%5.3f best_phash=0x%llX\n", highest_stddev, best_phash );

	return best_phash;
}
unsigned long long get_best_seed( struct phash_list *input_list, int level )
{
	unsigned long long best_seed = 0;
	double highest_stddev = 0;

	unsigned long long *phash = &(input_list->hashes[0]);

	struct timeval begin;
	gettimeofday( &begin, NULL );

	int c;
	int next_print = 0;
	for( c = 0; c < input_list->count; c++ )
	{

		struct pthread_hds_pkg *packages[num_pthreads];
		pthread_t threads[num_pthreads];

		int t;
		int thread_count = 0;
		for( t=0; t < num_pthreads; t++ )
		{
			packages[t] = (struct pthread_hds_pkg*)malloc(sizeof(struct pthread_hds_pkg));
			packages[t]->list = input_list;
			packages[t]->stddev = 0;
			packages[t]->hash = *phash++;
			
			pthread_create( &threads[t], NULL, &pthread_compare_hds, (void*)packages[t] );
			
			thread_count++;

			c++;
			if( c == input_list->count )
				break;
		}
		for( t=0; t < thread_count; t++ )
		{
			pthread_join( threads[t], NULL );

			if( packages[t]->stddev > highest_stddev )
			{
				highest_stddev = packages[t]->stddev;
				best_seed = packages[t]->hash;
				debugp( 6, "[%7d] best stddev=%5.3f 0x%llX\n", 
						c,
						highest_stddev,
						best_seed
						);
			}
			free( packages[t] );
		}
		
		struct timeval now;
		gettimeofday( &now, NULL );
		double dbegin = (double)begin.tv_sec + (double)begin.tv_usec / 1000000.0;
		double dnow = (double)now.tv_sec + (double)now.tv_usec / 1000000.0;
		double doubletime = dnow - dbegin;
		double rate = (double)c / doubletime;
		double eta = (double)(input_list->count-c) / rate;

		if( (next_print < c) || c > input_list->count - 10 )
		{
			debugp( 4, "Level %3d %7d hashes (%7.2f per second) ETA: %6.3f sec\r", level, c, rate, eta );
			fflush( stderr );
			next_print = c + 100;
		}

	}

	debugp( 4, "\n" );
	return best_seed;
}
void *pthread_compare_hds( void *pkg )
{

	struct pthread_hds_pkg *package = (struct pthread_hds_pkg*)pkg;

	/* Calc hamming distances of all points to last phash */
	unsigned long long hamming_dist_total = 0;
	int *hamming_distances = calc_hamming_distances( 
			package->hash,
			package->list->hashes,
			package->list->count,
			2,
			&hamming_dist_total );

	double mean = (double)hamming_dist_total / (double)package->list->count;

	/* Calculate stddev */
	int *hdp = hamming_distances;
	double std_dev_tot = 0;
	for( int i=0; i < package->list->count; i++ )
	{
		std_dev_tot += pow( (double)(*hdp++) - mean, 2.0 );
	}
	package->stddev = sqrt( std_dev_tot / (double)package->list->count );

	free(hamming_distances);

	pthread_exit(NULL);
}
struct vpnode *index_phashes( 
		struct vpnode *node, 
		struct phash_list *list,
		struct vpnode *parent,
		unsigned long long seed,
		int level
		)
{

	debugp( 8, "index_phashes( %p, %p, %p, 0x%llx, %d )\n", node, list, parent, seed, level );

	if( node == NULL )
		node = new_vpnode();

	if( parent == NULL )
		node->phash = seed;
	else
	{
		if( list->count > 30000 ) // <-- this is a tuning value
			node->phash = get_furthest_vantage_point(list,parent->phash);
		else
			node->phash = get_best_seed(list,level);
	}

	int i;
	int list_count = list->count; // cant' use list->count directly since it changes as we pop
	int hamming_distances[list_count];

	/* TODO: distances should only be calculated once */
	unsigned long long tot = 0;
	for( i=0; i < list_count; i++ )
	{
		unsigned long long phash = list->hashes[i];
		int hd = hamming_distance(phash,node->phash);
		tot += hd;

		hamming_distances[i] = hd;
	}

	//	/* Copy hamming distances into a temp list to be sorted */
	//	int *hds = (int*)malloc( sizeof(int) * list_count );
	//	memcpy( hds, hamming_distances, sizeof(int) * list_count );


	///* Sort hamming distances */
	//quick_sort(hds,list_count);

	//	/* Count distinct values */
	//	int distinct = 0;
	//	int lastval = 0;
	//	for( i=0; i < list_count; i++ )
	//	{
	//		tot += hds[i];
	//		if( hds[i] != lastval )
	//		{
	//			lastval = hds[i];
	//			distinct++;
	//		}
	//	}

	/* Get median value from sorted list */
	//node->distance = hds[list->count/2]; 
	
	/* Get mean value */
	node->distance = tot / list_count;
	
	//free(hds);
	
	debugp( 5, "\n" );
	debugp( 5, "New Vantage point: 0x%016llX\n", node->phash );
	debugp( 5, "List count:          %18d\n", list_count );
	debugp( 5, "Node's Distance:     %18d\n", node->distance );

	for( i=0; i < list_count; i++ )
	{
		unsigned long long phash = phash_pop(list);

		//int hd = hamming_distance(phash,node->phash);
		int hd = hamming_distances[list_count-i-1];

		if( hd <= node->distance )
		{
			phash_push( phash, &(node->left_vals) );
		}
		else
		{
			phash_push( phash, &(node->right_vals) );
		}
		debugp( 7, "[%6d] d(0x%llx,0x%llx)=%d %s  (L=%d/R=%d)\n", 
				i,
				phash,
				node->phash,
				hd,
				(hd<=node->distance) ? "LEFT      " : "     RIGHT" ,
				node->left_vals.count,
				node->right_vals.count
				);
	}
	
	struct timespec st;
	st.tv_sec=0;
	st.tv_nsec=flag_slow;


	/**
	 * Decision to branch further or become a leaf... 
	 *
	 * If all of the hashes ended up on one side of the node, stop
	 * 
	 */
	debugp( 5, "left_vals.count = %d; right_vals.count = %d\n", node->left_vals.count, node->right_vals.count );
	if( 
			//!(
			//((node->left_vals.count == 0) && (node->right_vals.count)) ||
			//((node->left_vals.count) && (node->right_vals.count == 0)) 
			//)
			!(
				(node->left_vals.count < max_leaf_distance) && (node->right_vals.count < max_leaf_distance)
			)
	  )
	{
		debugp( 5, "Branching!\n" );
		node->left = index_phashes( node->left, &(node->left_vals), node, 0, level + 1 );
		node->right = index_phashes( node->right, &(node->right_vals), node, 0, level + 1 );
	}
	else
	{
		debugp( 5, "Not Branching!\n" );
		//if( (node->left_vals.count > 10) || (node->right_vals.count > 10))
		//	exit(-10);
		if( flag_slow )
			nanosleep(&st,NULL);
	}


	/* Require at least 8 distinct values before allowing to split */
	
	//if( (distinct > 8) && (node->distance>max_leaf_distance) )
	//if( distinct > 8 )
	//{
	//	node->left = index_phashes( node->left, &(node->left_vals), node, level + 1 );
	//	node->right = index_phashes( node->right, &(node->right_vals), node, level + 1 );
	//}
	//if( (distinct > 8) && (node->distance>max_leaf_distance) )
	//if( node->right_vals.count > 5 )
	//{
	//	node->right = index_phashes( node->right, &(node->right_vals), node, level + 1 );
	//}

	return node;
}
unsigned long long get_random_phash( struct phash_list *list )
{
	unsigned long long phash = 0;
	srand(time(NULL));
	int random = (double)rand() / (double)RAND_MAX * list->count;

	phash = list->hashes[random];

	return phash;
}
unsigned long long pop_random_phash( struct phash_list *list )
{
	unsigned long long phash = 0;
	srand(time(NULL));
	int random = (double)rand() / (double)RAND_MAX * list->count;
	
	phash = list->hashes[random];

	/* Decrement the counter */
	(list->count)--;

	/* Move the last value into the hole */
	list->hashes[random] = list->hashes[list->count-1];

	/* Reallocate one shorter */
	list->hashes = (unsigned long long*)realloc(list->hashes, sizeof(unsigned long long) * list->count );

	return phash;
}
struct phash_list *copy_list( struct phash_list *list )
{
	struct phash_list *newlist = (struct phash_list*)malloc(sizeof(struct phash_list));
	newlist->count = list->count;
	newlist->hashes = (unsigned long long*)malloc(sizeof(unsigned long long) * newlist->count);
	memcpy( newlist->hashes, list->hashes, sizeof(unsigned long long) * newlist->count );
	return newlist;
}
void load_phashes(void)
{
	const char *query = "SELECT HEX(`phash`) FROM `img` WHERE `phash` IS NOT NULL GROUP BY `phash` ORDER BY `phash`";
	MYSQL_RES *result = run_query( (char*)query );

	MYSQL_ROW row;
	my_ulonglong num_rows = mysql_num_rows( result );
	debugp( 4, "%llu rows returned\n", num_rows );
	unsigned long long rownum = 1;
	while( ( row = mysql_fetch_row( result ) ) != NULL )
	{
		if( row[0] == NULL )
			continue;
		char *phash_str = row[0];
		unsigned long long phash = 0;
		sscanf( phash_str, "%llX", &phash );

		debugp( 4, "Loading phash %7llu of %7llu\r", rownum, num_rows );
		debugp( 7, "Row %6llu/%6llu: %s => 0x%llx\n", rownum, num_rows, phash_str, phash );

		phash_push( phash, &master_list );

		rownum++;
	}

	mysql_free_result(result);
	debugp( 7, "mysql_free_result(result)\n" );
	return;
}
void phash_push( unsigned long long phash, struct phash_list *list )
{
	(list->count)++;

	list->hashes = (unsigned long long*)myrealloc(list->hashes,sizeof(unsigned long long)*list->count);

	list->hashes[list->count-1] = phash;
	//debugp( 6, "Pushed value 0x%llx onto %p; total %d\n", phash, list, list->count );
	return;
}
struct vpnode *new_vpnode(void)
{
	node_count++;

	struct vpnode *node = (struct vpnode*)mymalloc(sizeof(struct vpnode));
	node->phash = node->distance = 0;
	node->left = node->right = NULL;

	node->left_vals.count = 0;
	node->right_vals.count = 0;
	node->left_vals.hashes = NULL;
	node->right_vals.hashes = NULL;
	return node;
}
void efficiency_init(void)
{
	debug_trace.count = 0;
	debug_trace.steps = NULL;
}
void print_efficiency_summary(void)
{

	int phash_count = count_all_phashes(0,master_tree);
	double saved = (double)(phash_count - comparison_count) / phash_count * 100.0;

	debugp( 0, "\n======= EFFICIENCY SUMMARY =======\n" );
	debugp( 0, "P-Hash Count:      %15d\n", phash_count );
	debugp( 0, "Node Count:        %15d\n", node_count );
	debugp( 0, "Built From:        %15s\n", (input_file==NULL) ? "MySQL" : "Index File" );
	debugp( 0, "Load Time (sec):   %15.4f\n", load_time );
	debugp( 0, "Comparisons Ran:   %15d\n", comparison_count );
	debugp( 0, "Comparisons Saved: %8d (%2.1f%%)\n", phash_count-comparison_count,saved );

	debugp( 0, "\n" );

	//print_trace( debug_trace );

	return;
}
void trace_push( trace *dtrace, enum fork step )
{
	(dtrace->count)++;
	dtrace->steps = (enum fork*)realloc(dtrace->steps,sizeof(enum fork) * dtrace->count );
	dtrace->steps[dtrace->count-1] = step;
	return;
}
void print_trace( trace dtrace )
{
	debugp( 0, "Route trace:\n" );
	for( int i=0; i < dtrace.count; i++ )
	{
		char *route;
		switch(dtrace.steps[i])
		{
			case both:
				route = "BOTH";
				break;
			case left:
				route = "LEFT";
				break;
			case right:
				route = "RIGHT";
				break;
			default:
				route = "UNDEFINED";
				break;
		}
		debugp( 0, "[%d] %s\n", i, route );
	}
}
unsigned long long phash_pop( struct phash_list *list )
{
	if( list->count == 0 )
		return 0;

	unsigned long long phash = list->hashes[list->count-1];
	(list->count)--;
	if( list->count > 0 )
	{
		struct memory *mem = find_memlist( memlist, list->hashes );

		if( (mem == NULL) || (mem->size < (sizeof(unsigned long long) * list->count)) )
		{
			size_t newsize = sizeof(unsigned long long) * list->count;
			//newsize += 100 * sizeof(unsigned long long);
			list->hashes = (unsigned long long*)myrealloc(list->hashes,newsize);
		}
	}
	else if( list->hashes != NULL )
	{
		free(list->hashes);
		list->hashes = NULL;
	}
		
	return phash;
}
void deinit_database(void)
{
	debugp( 4, "Disconnecting from MySQL server\n" );
	mysql_close( mysql_connection );
	mysql_connection = NULL;
	return;
}
int init_database(void)
{
	if( mysql_connection == NULL )
	{
		mysql_connection = mysql_init(NULL);

		my_bool reconnect = 1;
		mysql_options( mysql_connection, MYSQL_OPT_RECONNECT, &reconnect );

		/* Use defaults where missing */
		if( database_host == NULL ) database_host = (char*)default_host;
		if( database_user == NULL ) database_user = (char*)default_user;
		if( database_password == NULL ) database_password = (char*)default_password;
		if( database_name == NULL ) database_name = (char*)default_database;
		if( database_port == 0 ) database_port = default_port;

		debugp( 4, "Connecting to MySQL database with:\n" );
		debugp( 4, "   host:     %s\n", database_host );
		debugp( 4, "   user:     %s\n", database_user);
		debugp( 4, "   password: %s\n", database_password);
		debugp( 4, "   database: %s\n", database_name);
		debugp( 4, "   port:     %d\n", database_port);

		if( !mysql_real_connect( 
					mysql_connection, 
					database_host, 
					database_user, 
					database_password, 
					database_name, 
					database_port, 
					NULL /*unix socket*/, 
					0    /*client flag*/ 
					) 
				) {
			debugp( 0, "%s\n", mysql_error(mysql_connection) );
			return(-1);
		}
		else
		{
			debugp( 4, "Connection successful\n" );
		}
	}
	return 0;
}
MYSQL_RES *run_query( char *query )
{
	int retval;
	if( mysql_ping(mysql_connection) ) {
		perror( "mysql_ping" );
	}
	if( ( retval = mysql_query( mysql_connection, query ) ) ) {
		debugp( 0, "%s\n", mysql_error( mysql_connection ));
	}

	MYSQL_RES *result = NULL;
	if( (result = mysql_store_result( mysql_connection )) == NULL )
	{
		debugp( 0, "%s\n", mysql_error( mysql_connection ));
		exit(-1);
	}

	return result;
}
int hamming_distance(const unsigned long long hash1,const unsigned long long hash2)
{
	 hd_count++;
    unsigned long long x = hash1^hash2;
    const unsigned long long m1  = 0x5555555555555555ULL;
    const unsigned long long m2  = 0x3333333333333333ULL;
    const unsigned long long h01 = 0x0101010101010101ULL;
    const unsigned long long m4  = 0x0f0f0f0f0f0f0f0fULL;
    x -= (x >> 1) & m1;
    x = (x & m2) + ((x >> 2) & m2);
    x = (x + (x >> 4)) & m4;
    return (x * h01)>>56;
}
void* mymalloc( size_t size )
{
	void *ptr = malloc( size );
	
	/*
	struct memory *ml = add_to_memlist(memlist,ptr,size);
	if( memlist == NULL )
		memlist = ml;
		*/
	
	return ptr;
}
void* myrealloc( void *ptr, size_t size )
{
	ptr = realloc( ptr, size );

	/*
	struct memory *ml = add_to_memlist(memlist,ptr,size);
	if( memlist == NULL )
		memlist = ml;
		*/

	return ptr;
}
struct memory *add_to_memlist( struct memory *list, void *ptr, size_t size )
{
	return NULL;
	if( list == NULL )
	{
		list = (struct memory*)malloc( sizeof(struct memory) );
		list->left = list->right = NULL;

		list->size = size;
		list->ptr = ptr;
	}
	else
	{
		if( ptr < list->ptr )
		{
			if( list->left == NULL )
			{
				list->left = add_to_memlist( list->left, ptr, size );
				list = list->left;
			}
			else
			{
				list = add_to_memlist( list->left, ptr, size );
			}
		}
		else if( ptr > list->ptr )
		{
			if( list->right == NULL )
			{
				list->right = add_to_memlist( list->right, ptr, size );
				list = list->right;
			}
			else
			{
				list = add_to_memlist( list->right, ptr, size );
			}
		}
		else if( ptr == list->ptr )
		{
			list->size = size;
		}
	}
	//debugp( 6, "Added/modified memory allocation record for %p; size=%d\n", list->ptr, list->size );
	
	return list;
}
struct memory *find_memlist( struct memory *list, void *ptr )
{
	return NULL;
	struct memory *mylist = NULL;

	if( list != NULL )
	{
		if( list->ptr == ptr )
		{
			mylist = list;
		}
		else if( ptr < list->ptr )
		{
			mylist = find_memlist( list->left, ptr );
		}
		else if( ptr > list->ptr )
		{
			mylist = find_memlist( list->right, ptr );
		}
	}
	else
		mylist = NULL;

	if( mylist != NULL )
	{
		debugp( 6, "Found memory allocation %p; size=%d\n", mylist->ptr, mylist->size );
	}

	return mylist;
}
void print_memtree( struct memory *list )
{
	return;
	if( list != NULL )
	{
		print_memtree( list->left );
		debugp( 2, "Ptr: %016p Size: %16d\n", list->ptr, list->size );
		print_memtree( list->right );
	}
	return;
}
void free_vp_tree( struct vpnode *node )
{
	if( node == NULL )
		return;
	free_vp_tree( node->left );
	free_vp_tree( node->right );

	if( node->left != NULL )
		free(node->left);
	if( node->right != NULL )
		free(node->right);

	if( node->left_vals.hashes != NULL )
	{
		free( node->left_vals.hashes );
		node->left_vals.hashes = NULL;
		node->left_vals.count = 0;
	}
	if( node->right_vals.hashes != NULL )
	{
		free( node->right_vals.hashes );
		node->right_vals.hashes = NULL;
		node->right_vals.count = 0;
	}

	return;
}
void print_vp_tree( struct vpnode *node )
{
	if( node == NULL ) 
	{
		return;
	}
	else if( node != NULL )
	{
		if( (node->left==NULL) && (node->left==NULL) )
			debugp( 1, "LEAF!!\n\n" );

		print_vp_tree( node->left );

		debugp( 1, "Tree node\n" );
		debugp( 1, "  P-Hash:            0x%011x\n", node->phash );
		debugp( 1, "  Distance:          %13d\n", node->distance );
		debugp( 1, "  Left Value Count:  %13d\n", node->left_vals.count );
		if( node->left_vals.count )
		{
			debugp( 4, "  Left:\n" );
			for( int i=0; i < node->left_vals.count; i++ )
			{
				int hd = hamming_distance( node->left_vals.hashes[i], node->phash );
				debugp( 4, "    0x%llX (d=%d)\n", node->left_vals.hashes[i], hd );

			}
		}
		debugp( 1, "  Right Value Count: %13d\n", node->right_vals.count );
		if( node->right_vals.count )
		{
			debugp( 4, "  Right:\n" );
			for( int i=0; i < node->right_vals.count; i++ )
			{
				int hd = hamming_distance( node->right_vals.hashes[i], node->phash );
				debugp( 4, "    0x%llX (d=%d)\n", node->right_vals.hashes[i], hd );

			}
		}
		size_t size = get_node_size(node);
		debugp( 1, "Total size: %d bytes\n", size );
		debugp( 1, "\n\n" );

		print_vp_tree( node->right );
		
	}
	return;
}
size_t get_node_size( struct vpnode *node )
{
	size_t total = 0;

	total += sizeof(node->phash);
	total += sizeof(node->distance);
	total += sizeof(node->left_vals);
	total += sizeof(node->right_vals);
	total += sizeof(node->left);
	total += sizeof(node->right);

	total += sizeof(unsigned long long) * node->left_vals.count;
	total += sizeof(unsigned long long) * node->right_vals.count;

	return total;
}
int count_all_phashes( int start, struct vpnode *node )
{
	if( node == NULL )
		return start;
	start += node->left_vals.count;
	start += node->right_vals.count;

	start = count_all_phashes(start,node->left);
	start = count_all_phashes(start,node->right);

	return start;
}
int get_recursive_node_size( struct vpnode *node )
{
	size_t grand_total = 0;
	
	if( node == NULL ) 
	{
		return 0;
	}
	else if( node != NULL )
	{
		grand_total += get_node_size(node);
		grand_total += get_recursive_node_size(node->left);
		grand_total += get_recursive_node_size(node->right);
	}
	return grand_total;
}
size_t serialize_node( struct vpnode *node, FILE *fp )
{
	size_t written = 0;

	written += fwrite( &(node->phash), sizeof(node->phash), 1, fp ) * sizeof(node->phash);
	written += fwrite( &(node->distance), sizeof(node->distance), 1, fp ) * sizeof(node->distance);

	/* Left vals */
	written += fwrite( &(node->left_vals), sizeof(node->left_vals), 1, fp ) * sizeof(node->left_vals);
	if( node->left_vals.count > 0 )
	{
		written += fwrite( node->left_vals.hashes, sizeof(unsigned long long), node->left_vals.count, fp ) * sizeof(unsigned long long);
	}

	/* Right vals */
	written += fwrite( &(node->right_vals), sizeof(node->right_vals), 1, fp ) * sizeof(node->right_vals);
	if( node->right_vals.count > 0 )
	{
		written += fwrite( node->right_vals.hashes, sizeof(unsigned long long), node->right_vals.count, fp ) * sizeof(unsigned long long);
	}

	/* Left node */
	written += fwrite( &(node->left), sizeof(node->left_vals), 1, fp ) * sizeof(node->left_vals);
	if( node->left != NULL )
	{
		written += serialize_node( node->left, fp );
	}

	/* Right node */
	written += fwrite( &(node->right), sizeof(node->right_vals), 1, fp ) * sizeof(node->right_vals);
	if( node->right != NULL )
	{
		written += serialize_node( node->right, fp );
	}

	return written;
}
struct vpnode *unserialize_node( FILE *fp )
{
	struct vpnode *node = new_vpnode();

	debugp( 7, "unserialize_node(fp)\n" );
	
	size_t bytes_read = 0;

	bytes_read += fread( &(node->phash), sizeof(node->phash), 1, fp ) * sizeof(node->phash);
	debugp( 7, "node->phash=0x%llX\n", node->phash );
	bytes_read += fread( &(node->distance), sizeof(node->distance), 1, fp ) * sizeof(node->phash);
	debugp( 7, "node->distance=%d\n", node->distance);

	/* Left vals */
	bytes_read += fread( &(node->left_vals), sizeof(node->left_vals), 1, fp ) * sizeof(node->left_vals);
	debugp( 7, "node->left_vals.count=%d\n", node->left_vals.count );
	if( node->left_vals.count > 0 )
	{
		node->left_vals.hashes = (unsigned long long*)malloc(sizeof(unsigned long long)*node->left_vals.count);
		bytes_read += fread( node->left_vals.hashes, sizeof(unsigned long long), node->left_vals.count, fp ) * sizeof(unsigned long long);

		for( int i=0; i<node->left_vals.count; i++ )
		{
			debugp( 7, "  0x%llX\n", node->left_vals.hashes[i] );
		}
	}
	/* Right vals */
	bytes_read += fread( &(node->right_vals), sizeof(node->right_vals), 1, fp ) * sizeof(node->right_vals);
	debugp( 7, "node->right_vals.count=%d\n", node->right_vals.count );
	if( node->right_vals.count > 0 )
	{
		node->right_vals.hashes = (unsigned long long*)malloc(sizeof(unsigned long long)*node->right_vals.count);
		bytes_read += fread( node->right_vals.hashes, sizeof(unsigned long long), node->right_vals.count, fp ) * sizeof(unsigned long long);
		
		for( int i=0; i<node->right_vals.count; i++ )
		{
			debugp( 7, "  0x%llX\n", node->right_vals.hashes[i] );
		}
	}

	/* Left node */
	bytes_read += fread( &(node->left), sizeof(node->left_vals), 1, fp ) * sizeof(node->left_vals);
	debugp( 7, "node->left=%p\n", node->left );
	if( node->left != NULL )
	{
		node->left = unserialize_node( fp );
	}
	/* Right node */
	bytes_read += fread( &(node->right), sizeof(node->right_vals), 1, fp ) * sizeof(node->right_vals);
	debugp( 7, "node->right=%p\n", node->right );
	if( node->right != NULL )
	{
		node->right = unserialize_node( fp );
	}

	return node;
}
struct vpnode *load_index( char *filename )
{
	FILE *fp;
	fp = fopen( filename, "r" );
	if( fp == NULL )
		return NULL;

	debugp( 2, "Loading tree from file `%s'...", filename );
	struct vpnode *node = unserialize_node(fp);
	debugp( 2, "done\n" );

	fclose(fp);

	return node;
}
int save_index( struct vpnode *node, char *filename )
{
	FILE *fp;
	fp = fopen( filename, "w" );
	if( fp == NULL )
		return -1;

	size_t written = serialize_node( node, fp );

	fclose(fp);

	debugp( 1, "Saved %d bytes to file `%s'\n", written, filename );


	return 0;
}
void start_timer(void)
{
	gettimeofday( &start_time, NULL );
}
double stop_timer(void)
{
	gettimeofday( &end_time, NULL );
	double dstart_time = (double)start_time.tv_sec + (double)start_time.tv_usec / 1000000.0;
	double dend_time = (double)end_time.tv_sec + (double)end_time.tv_usec / 1000000.0;
	double exec_time = dend_time - dstart_time;
	return exec_time;
}
int make_utf8( char *str, unsigned unum )
{
	char *c = str;
	if( unum <= 0x7f )
	{
		*c++ = (char)unum;
	}
	else if( unum <= 0x7ff )
	{
		uint8_t low,high;
		low = unum & 0x3f;
		high = (unum & 0x7c0) >> 6;

		*c++ = 0xc0 | high;
		*c++ = 0x80 | low;
	}
	*c = '\0';
	return 0;
}

struct calc_hamming_distance_thread_pkg {
	unsigned long long reference;
	unsigned long long *hashes;
	int *hds;
	int count;
	unsigned long long total;
};
void *calc_hamming_distance_thread_func( void *arg );
// Calculates the hamming distances between the reference phash and each phash in an array of phashes of length `count'
// Returns an array of ints containing the distances in the same order as the phash array
int *calc_hamming_distances( unsigned long long reference, unsigned long long *hashes, int count, int thread_count, unsigned long long *total )
{
	debugp( 5, "calc_hamming_distances called for %d hashes, thread count %d\n", count, thread_count );
	int *hds = malloc( sizeof(int) * count );

	pthread_t *threads = malloc( sizeof(pthread_t) * thread_count );
	struct calc_hamming_distance_thread_pkg *packages = malloc( sizeof(struct calc_hamming_distance_thread_pkg) * thread_count );

	// Divide hashes among threads
	int hashes_per_thread = count / thread_count;
	int remaining = count;

	// Assign params to and create threads
	struct calc_hamming_distance_thread_pkg *mypkg = packages;
	pthread_t *mythread = threads;
	unsigned long long *hash_ptr = hashes;
	int *hds_ptr = hds;
	for( int t=0; t < thread_count; t++ )
	{
		mypkg->reference = reference;
		mypkg->hashes = hash_ptr;
		mypkg->hds = hds_ptr;

		if( t == (thread_count-1) )
			mypkg->count = remaining;
		else
			mypkg->count = hashes_per_thread;

		pthread_create( mythread, NULL, calc_hamming_distance_thread_func, (void*)mypkg );

		debugp( 5, "Dispatched thread %lu with %d assignments\n", (unsigned long)*mythread, mypkg->count );

		remaining -= hashes_per_thread;
		hds_ptr += hashes_per_thread;
		hash_ptr += hashes_per_thread;
		mypkg++;
		mythread++;
	}

	// Join all threads
	mythread = threads;
	mypkg = packages;

	*total = 0;
	for( int t=0; t < thread_count; t++ )
	{
		debugp( 5, "Joining thread %lu\n", (unsigned long)*mythread );
		pthread_join( *mythread, NULL );
		*total += mypkg->total;
		mythread++;
		mypkg++;
	}

	free(threads);
	free(packages);

	return hds;
}
void *calc_hamming_distance_thread_func( void *arg )
{
	struct calc_hamming_distance_thread_pkg *pkg = (struct calc_hamming_distance_thread_pkg*)arg;

	pkg->total = 0;
	unsigned long long *hash_ptr = pkg->hashes;
	int *hds_ptr = pkg->hds;
	for( int i=0; i < pkg->count; i ++ )
	{
		*hds_ptr = hamming_distance( pkg->reference, *hash_ptr++ );
		pkg->total += *hds_ptr;
		hds_ptr++;
	}
	pthread_exit(NULL);
}

#endif
