#include "phash_vptree.h"

int main( int argc, char *argv[] )
{

	struct timeval main_start;
	gettimeofday(&main_start,NULL);

	int retval = 0;

	parse_args( argc, argv );

	switch(function)
	{
		case command_line:
			retval = command_line_function();
			break;
		case optimization:
			retval = optimize();
			break;
		default:
			retval = command_line_function();
			break;
	}
	
	struct timeval main_end;
	gettimeofday(&main_end,NULL);
	double dbegin = (double)main_start.tv_sec + (double)main_start.tv_usec / 1000000.0;
	double dnow = (double)main_end.tv_sec + (double)main_end.tv_usec / 1000000.0;
	double main_exec_time = dnow - dbegin;

	debugp( 2, "main() executed in %f seconds\n", main_exec_time );

	return retval;
}
int optimize(void)
{
	int retval = 0;
	FILE *fp;

	set_debug_level(0);

	if( num_phash_queries <= 0 )
	{
		debugp( 0, "Specify a query!\n" );
		return 3;
	}
	if( logfile_name != NULL )
	{
		fp = fopen( logfile_name, "a" );
		if( fp == NULL )
			return 1;
	}
	else
		return 2;

	char buffer[1024];
	sprintf( buffer, "======================================== %3d ITERATIONS PER TEST  ========================================\n", optimize_iterations );
	//fwrite( buffer, sizeof(char), strlen(buffer), fp );


	//start_timer();

	init_database();

	load_phashes(); // into master_list

	/* master_list gets depleated; make a copy */
	struct phash_list master_backup;
	master_backup.count = master_list.count;
	master_backup.hashes = (unsigned long long*)malloc(sizeof(unsigned long long) * master_backup.count);
	memcpy(master_backup.hashes,master_list.hashes,sizeof(unsigned long long) * master_backup.count);
		
	deinit_database();

	unsigned long long phash = phash_queries[0];

	//for( int dist=query_max_distance/2; dist<=query_max_distance*1.5; dist++ )
	for( int dist=8; dist == 8; dist ++ )
	{
		int nodesize;
		for( nodesize = 5; nodesize<= 10000; nodesize+=10 )
		{
			double total_saved = 0;
			double total_query_time = 0;
			//unsigned long total_matches = 0;
			int test = 0;
			hd_count = 0;

			max_leaf_distance = nodesize;
			query_max_distance = dist;

			if( master_list.hashes == NULL )
			{
				/* copy backup back into master_list */
				master_list.count = master_backup.count;
				master_list.hashes = (unsigned long long*)malloc(sizeof(unsigned long long) * master_backup.count);
				memcpy(master_list.hashes,master_backup.hashes,sizeof(unsigned long long) * master_backup.count);
			}

			double indexing_time = 0, query_time = 0;
			start_timer();
			set_debug_level(4);
			unsigned long long seed = get_best_seed( &master_list, 0 );

			master_tree = index_phashes(master_tree,&master_list,NULL,seed,0);
			indexing_time = stop_timer();
			set_debug_level(0);

			debugp( 0, "Querying for 0x%llX with maximum hamming distance of %d, nodesize = %d\n", phash, query_max_distance, max_leaf_distance );


			struct pmatches *mymatches = NULL;
			long qcount = 0;
			for( test=0; test<optimize_iterations; test++ )
			{
				mymatches = NULL;

				start_timer();
				if( flag_pthread_test )
					mymatches = query_phash2( master_tree, phash, query_max_distance, mymatches );
				else
					mymatches = query_phash( master_tree, phash, query_max_distance, mymatches );
				query_time = stop_timer();
				total_query_time += query_time;

				qcount += mymatches->count;

				free(mymatches);
			}
			double mean_query_time = total_query_time / ((double)test+1.0);

			int phash_count = count_all_phashes(0,master_tree);
			double saved = (double)(phash_count - comparison_count) / phash_count * 100.0;
			total_saved += saved;

			debugp( 0, "\n======= EFFICIENCY SUMMARY =======\n" );
			debugp( 0, "Indexing Time:     %15.8f\n", indexing_time );
			//debugp( 0, "HD Count:          %15lu\n", hd_count );
			debugp( 0, "P-Hash Count:      %15d\n", phash_count );
			debugp( 0, "Node Count:        %15d\n", node_count );
			debugp( 0, "Query Time:        %15.8f\n", mean_query_time );
			debugp( 0, "Result Count:      %15.8f\n", qcount / ((double)test+1.0));

			debugp( 0, "\n" );


			/* Cleanup */
			node_count = 0;
			load_time = 0;
			comparison_count = 0;

			free_vp_tree(master_tree);
			master_tree = NULL;

			master_list.count = 0;
			master_list.hashes = NULL;

			
			sprintf( buffer, "%2d,%2d,%7.6f\n",
					nodesize, 
					dist, 
					mean_query_time
					);

			fwrite( buffer, sizeof(char), strlen(buffer), fp );
			fflush(fp);

		}
	}
	fclose(fp);

	return retval;
}
int command_line_function(void)
{
	int retval = 0;
	
	start_timer();

	if( flag_efficiency_debug )
		efficiency_init();

	if( input_file == NULL )
	{
		init_database();

		load_phashes();
		for( int f=0; f < flag_load_multiple_times; f++ )
			load_phashes();

		unsigned long long seed = 0x3333CCCCCCCC3333;
		master_tree = index_phashes(master_tree,&master_list,NULL,seed,0);
	}
	else
	{
		master_tree = load_index(input_file);
	}

	load_time = stop_timer();

	double total_query_time = 0;

	if( num_pthreads > num_phash_queries )
		num_pthreads = num_phash_queries;
	struct threaded_query_pkg *packages[num_phash_queries];
	pthread_t threads[num_pthreads];
	int threads_running = 0;
	int q = num_phash_queries;

	pthread_t *current_thread = threads;
	int query_num = 1;
	int join_count = 0;
	while(q--)
	{
		packages[q] = (struct threaded_query_pkg*)malloc(sizeof(struct threaded_query_pkg));
		packages[q]->done = 0;
		packages[q]->query_num = query_num;
		packages[q]->query = phash_queries[q];

		debugp( 3, "Launching thread #%d (%d running)\n", q, threads_running );
		pthread_create( &(*current_thread), NULL, &threaded_query, (void*)packages[q] );
		query_num++;

		threads_running++;
		current_thread++;

		if( (current_thread - threads) >= num_pthreads )
		{
			current_thread = threads;
		}

		if( threads_running >= num_pthreads )
		{
			pthread_join( *current_thread, NULL );
			join_count++;
			threads_running--;
		}
	}
	for( int t = 0; t < num_pthreads; t++ )
	{
		pthread_join( threads[t], NULL );
	}

	for( q=0; q < num_phash_queries; q++ )
	{

		total_query_time += packages[q]->query_time;
	
		debugp( 1, "Query #%d: phash = 0x%llX d <= %d] (%f sec) \n", q+1, packages[q]->query, query_max_distance, packages[q]->query_time );
		
		debugp( 2, "packages[%d]->matches is at %p\n", q, packages[q]->matches );
		if(packages[q]->matches != NULL)
			debugp( 2, "packages[%d]->matches->count = %d\n", q, packages[q]->matches->count );


		if( (packages[q]->matches != NULL) && (packages[q]->matches->count) )
		{
			for( int m=0; m < packages[q]->matches->count; m++ )
			{
				debugp( 1, "  Match #%d: 0x%llX (d=%d)\n", 
						m+1, 
						packages[q]->matches->matches[m].phash,
						packages[q]->matches->matches[m].dist );
				
				if( flag_php )
				{
					printf( "%llX\n", packages[q]->matches->matches[m].phash );
				}
			}
		}
		else
		{
			debugp( 1, "  ** No Matches **\n" );
		}

		debugp( 2, "packages[%d]->done = %d\n", q, packages[q]->done );

		if(packages[q]->matches != NULL)
			free(packages[q]->matches->matches);
		free(packages[q]->matches);
		free(packages[q]);
	}
		
	double avg_query_time = total_query_time / (double)num_phash_queries;
	
	if( logfile_name != NULL )
	{
		FILE *fp;
		
		if( (fp = fopen( logfile_name, "a" )) )
		{
			char buffer[512];
			sprintf( buffer, "%d,%f,%f\n", num_phash_queries,avg_query_time,total_query_time );
			fwrite( buffer, sizeof(char), strlen(buffer), fp );
		}

		fclose(fp);
	}

	if( input_file == NULL )
		deinit_database();

	if( flag_print_vp_tree )
	{
		print_vp_tree(master_tree);
		size_t size = get_recursive_node_size(master_tree);
		debugp( 1, "Total size: %d bytes\n", size );
	}
	if( output_file != NULL )
		save_index( master_tree, output_file );
	if( flag_efficiency_debug )
		print_efficiency_summary();

	free_vp_tree(master_tree);
	
	return retval;
}
void parse_args( int argc, char *argv[] )
{

	struct option long_options[] =
	{
		{ "thread-test", no_argument, &flag_pthread_test, 1 },
		{ "threads", required_argument, NULL, 't' },
		{ "traverse-all", no_argument, &flag_traverse_all, 1 },
		{ "logfile", required_argument, NULL, 'l' },
		{ "iterations", required_argument, NULL, 0 },
		{ "optimize", optional_argument, NULL, 0 },
		{ "load-multiple-times", required_argument, NULL, 0 },
		{ "slow", required_argument, NULL, 0 },
		{ "efficiency-debug", optional_argument, NULL, 0 },
		{ "php", optional_argument, &flag_php, 1 },
		{ "query", required_argument, NULL, 'q' },
		{ "query-distance", required_argument, NULL, 'd' },
		{ "max-leaf-distance", required_argument, NULL, 0 },
		{ "syslog", optional_argument, NULL, 0 },
		{ "verbose", optional_argument, NULL, 'v' },
		{ 0, 0, 0, 0 }
	};
	int long_options_index;


	int c;
	while( ( c = getopt_long( argc, argv, "t:l:i:o:pd:vq:", long_options, &long_options_index )) != -1 ) 
	{
		unsigned long long phash = 0;
		switch(c) {
			case 0: /* Long options with no short equivalent */
				if( strcmp( long_options[long_options_index].name, "syslog" ) == 0 ) {
					debugp( 0, "Changing debug facility to syslog... goodbye!\n" );
					setup_debugp_syslog( "productometer" );
					change_debug_facility( DEBUGP_SYSLOG );
					debugp( 4, "changed debug facility to syslog\n" );
				}
				else if( strcmp( long_options[long_options_index].name, "efficiency-debug" ) == 0 ) {
					flag_efficiency_debug = 1;
				}
				else if( strcmp( long_options[long_options_index].name, "slow" ) == 0 ) {
					sscanf( optarg, "%ld", &flag_slow );
				}
				else if( strcmp( long_options[long_options_index].name, "load-multiple-times" ) == 0 ) {
					flag_load_multiple_times = atoi(optarg);
				}
				else if( strcmp( long_options[long_options_index].name, "optimize" ) == 0 ) {
					function = optimization;
				}
				else if( strcmp( long_options[long_options_index].name, "iterations" ) == 0 ) {
					optimize_iterations = atoi(optarg);
				}
				break;
			case 't':
				num_pthreads = atoi(optarg);
				break;
			case 'o':
				output_file = optarg;
				break;
			case 'i':
				input_file = optarg;
				break;
			case 'p':
				flag_print_vp_tree = 1;
				break;
			case 'v':
				change_debug_level_by(1);
				break;
			case 'q':
				if( sscanf( optarg, "%llx", &phash ) == 1 )
				{
					num_phash_queries++;
					phash_queries = (unsigned long long*)realloc(phash_queries,sizeof(unsigned long long) * num_phash_queries);
					phash_queries[num_phash_queries-1] = phash;
					debugp( 2, "Adding phash query value 0x%llX\n", phash );
				}
				else
				{
					debugp( 0, "Error parsing query: `%s'\n", optarg );
				}
				break;
			case 'd':
				query_max_distance = atoi(optarg);
				debugp( 2, "Query maximum hamming distance set to %d\n", query_max_distance );
				break;
			case 'l':
				logfile_name = optarg;
				break;
			default:
				break;
		}
	}
	return;
}
